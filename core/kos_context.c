/*
 * Copyright (c) 2014-2018 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "../inc/kos_context.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../inc/kos_utils.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system.h"
#include "kos_try.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_init[]                    = "init";
static const char str_backtrace[]               = "backtrace";
static const char str_builtin[]                 = "<builtin>";
static const char str_err_not_array[]           = "object is not an array";
static const char str_err_out_of_memory[]       = "out of memory";
static const char str_err_thread_registered[]   = "thread already registered";
static const char str_file[]                    = "file";
static const char str_format_exception[]        = "Exception: ";
static const char str_format_hash[]             = "  #";
static const char str_format_line[]             = ":";
static const char str_format_function[]         = " in '";
static const char str_format_module[]           = "' in ";
static const char str_format_offset[]           = "  ";
static const char str_format_question_marks[]   = "???";
static const char str_function[]                = "function";
static const char str_line[]                    = "line";
static const char str_module[]                  = "module";
static const char str_offset[]                  = "offset";
static const char str_value[]                   = "value";

const struct _KOS_CONST_OBJECT _kos_void  = KOS_CONST_OBJECT_INIT(OBJ_VOID,    0);
const struct _KOS_CONST_OBJECT _kos_false = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 0);
const struct _KOS_CONST_OBJECT _kos_true  = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 1);

#ifdef CONFIG_PERF
struct _KOS_PERF _kos_perf = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 },
    0, 0,
    0, 0, 0, 0
};
#endif

#ifdef CONFIG_SEQFAIL
static int                  _kos_seq_init      = 0;
static KOS_ATOMIC(uint32_t) _kos_seq;
static uint32_t             _kos_seq_threshold = ~0U;

int _KOS_seq_fail(void)
{
    if ( ! _kos_seq_init) {

        struct _KOS_VECTOR cstr;
        int64_t            value = -1;

        _KOS_vector_init(&cstr);

        if (_KOS_get_env("KOSSEQFAIL", &cstr) == KOS_SUCCESS
                && cstr.size > 0
                && _KOS_parse_int(cstr.buffer, cstr.buffer + cstr.size - 1, &value) == KOS_SUCCESS)
            _kos_seq_threshold = (uint32_t)value;

        _KOS_vector_destroy(&cstr);

        KOS_atomic_write_u32(_kos_seq, 0);

        _kos_seq_init = 1;
    }

    if ((uint32_t)KOS_atomic_add_i32(_kos_seq, 1) >= _kos_seq_threshold)
        return KOS_ERROR_INTERNAL;

    return KOS_SUCCESS;
}
#endif

static int _register_thread(KOS_CONTEXT        *ctx,
                            KOS_THREAD_CONTEXT *thread_ctx)
{
    int error = KOS_SUCCESS;

    assert( ! _KOS_tls_get(ctx->threads.thread_key));

    thread_ctx->ctx   = ctx;
    thread_ctx->frame = (KOS_FRAME)_KOS_heap_early_alloc(ctx,
                                                         thread_ctx,
                                                         OBJ_STACK_FRAME,
                                                         (uint32_t)sizeof(KOS_STACK_FRAME));

    if ( ! thread_ctx->frame)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    TRY(_KOS_init_stack_frame(thread_ctx->frame, thread_ctx, OBJPTR(MODULE, ctx->modules.init_module), 0));

    if (_KOS_tls_get(ctx->threads.thread_key)) {

        KOS_OBJ_ID err = KOS_context_get_cstring(thread_ctx->frame, str_err_thread_registered);
        KOS_raise_exception(thread_ctx->frame, err);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    _KOS_tls_set(ctx->threads.thread_key, thread_ctx);

_error:
    if (error)
        _KOS_heap_release_thread_page(thread_ctx);

    return error;
}

void KOS_context_unregister_thread(KOS_CONTEXT        *ctx,
                                   KOS_THREAD_CONTEXT *thread_ctx)
{
    _KOS_heap_release_thread_page(thread_ctx);

    assert((KOS_THREAD_CONTEXT *)_KOS_tls_get(ctx->threads.thread_key) == thread_ctx);

    _KOS_tls_set(ctx->threads.thread_key, 0);

    _KOS_lock_mutex(&ctx->threads.mutex);

    assert(thread_ctx != &ctx->threads.main_thread);

    if (thread_ctx->prev)
        thread_ctx->prev->next = thread_ctx->next;

    if (thread_ctx->next)
        thread_ctx->next->prev = thread_ctx->prev;

    _KOS_unlock_mutex(&ctx->threads.mutex);
}

int KOS_context_register_thread(KOS_CONTEXT        *ctx,
                                KOS_THREAD_CONTEXT *thread_ctx)
{
    int error;

    _KOS_lock_mutex(&ctx->threads.mutex);

    thread_ctx->prev              = &ctx->threads.main_thread;
    thread_ctx->next              = ctx->threads.main_thread.next;
    ctx->threads.main_thread.next = thread_ctx;
    if (thread_ctx->next)
        thread_ctx->next->prev    = thread_ctx;

    _KOS_unlock_mutex(&ctx->threads.mutex);

    thread_ctx->cur_page = 0;

    error = _register_thread(ctx, thread_ctx);

    if (error)
        KOS_context_unregister_thread(ctx, thread_ctx);

    return error;
}

static int _add_multiple_paths(KOS_FRAME frame, struct _KOS_VECTOR *cpaths)
{
    int   error = KOS_SUCCESS;
    char *buf   = cpaths->buffer;

    while ( ! error) {
        char *end = strchr(buf, KOS_PATH_LIST_SEPARATOR);

        if (end)
            *end = '\0';

        error = KOS_context_add_path(frame, buf);

        if (end)
            buf = end + 1;
        else
            break;
    }

    return error;
}

static int _init_search_paths(KOS_FRAME frame)
{
#ifdef CONFIG_DISABLE_KOSPATH
    return KOS_SUCCESS;
#else
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpaths;

    _KOS_vector_init(&cpaths);

    if (_KOS_get_env("KOSPATH", &cpaths) == KOS_SUCCESS)
        error = _add_multiple_paths(frame, &cpaths);

    _KOS_vector_destroy(&cpaths);

    return error;
#endif
}

static KOS_OBJ_ID _alloc_empty_string(KOS_FRAME frame)
{
    KOS_STRING *str = (KOS_STRING *)_KOS_alloc_object(frame,
                                                      KOS_ALLOC_PERSISTENT,
                                                      OBJ_STRING,
                                                      sizeof(KOS_STRING));

    if (str) {
        str->header.flags  = (uint8_t)KOS_STRING_ELEM_8 | (uint8_t)KOS_STRING_LOCAL;
        str->header.length = 0;
        str->header.hash   = 0;
    }

    return OBJID(STRING, str);
}

int KOS_context_init(KOS_CONTEXT *ctx,
                     KOS_FRAME   *out_frame)
{
    int         error;
    int         heap_ok   = 0;
    int         thread_ok = 0;
    KOS_FRAME   frame;
    KOS_MODULE *init_module;

    memset(ctx, 0, sizeof(*ctx));

    TRY(_KOS_tls_create(&ctx->threads.thread_key));
    error = _KOS_create_mutex(&ctx->threads.mutex);
    if (error) {
        _KOS_tls_destroy(ctx->threads.thread_key);
        goto _error;
    }
    thread_ok = 1;

    TRY(_KOS_heap_init(ctx));
    heap_ok = 1;

    ctx->threads.main_thread.cur_page = 0;

    init_module = (KOS_MODULE *)_KOS_heap_early_alloc(ctx,
                                                      &ctx->threads.main_thread,
                                                      OBJ_MODULE,
                                                      (uint32_t)sizeof(KOS_MODULE));
    if ( ! init_module)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    init_module->flags             = 0;
    init_module->num_regs          = 0;
    init_module->instr_offs        = 0;
    init_module->name              = KOS_BADPTR;
    init_module->path              = KOS_BADPTR;
    init_module->context           = ctx;
    init_module->constants_storage = KOS_BADPTR;
    init_module->constants         = 0;
    init_module->global_names      = KOS_BADPTR;
    init_module->globals           = KOS_BADPTR;
    init_module->module_names      = KOS_BADPTR;
    init_module->bytecode          = 0;
    init_module->line_addrs        = 0;
    init_module->func_addrs        = 0;
    init_module->num_line_addrs    = 0;
    init_module->num_func_addrs    = 0;
    init_module->bytecode_size     = 0;

    ctx->modules.init_module  = OBJID(MODULE, init_module);
    ctx->modules.module_names = KOS_BADPTR;
    ctx->modules.modules      = KOS_BADPTR;
    ctx->modules.search_paths = KOS_BADPTR;

    ctx->args = KOS_BADPTR;

    TRY(_register_thread(ctx, &ctx->threads.main_thread));

    frame = ctx->threads.main_thread.frame;

    ctx->empty_string = _alloc_empty_string(frame);
    TRY_OBJID(ctx->empty_string);

    {
        KOS_OBJ_ID str = KOS_new_cstring(frame, str_err_out_of_memory);
        TRY_OBJID(str);
        ctx->heap.str_oom_id = str;
    }

    TRY_OBJID(ctx->prototypes.object_proto        = KOS_new_object_with_prototype(frame, KOS_BADPTR));
    TRY_OBJID(ctx->prototypes.number_proto        = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.integer_proto       = KOS_new_object_with_prototype(frame, ctx->prototypes.number_proto));
    TRY_OBJID(ctx->prototypes.float_proto         = KOS_new_object_with_prototype(frame, ctx->prototypes.number_proto));
    TRY_OBJID(ctx->prototypes.string_proto        = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.boolean_proto       = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.void_proto          = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.array_proto         = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.buffer_proto        = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.function_proto      = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.class_proto         = KOS_new_object_with_prototype(frame, ctx->prototypes.function_proto));
    TRY_OBJID(ctx->prototypes.generator_proto     = KOS_new_object_with_prototype(frame, ctx->prototypes.function_proto));
    TRY_OBJID(ctx->prototypes.exception_proto     = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.generator_end_proto = KOS_new_object(frame));
    TRY_OBJID(ctx->prototypes.thread_proto        = KOS_new_object(frame));

    TRY_OBJID(init_module->name         = KOS_context_get_cstring(frame, str_init));
    TRY_OBJID(init_module->globals      = KOS_new_array(frame, 0));
    TRY_OBJID(init_module->global_names = KOS_new_object(frame));
    TRY_OBJID(init_module->module_names = KOS_new_object(frame));
    TRY_OBJID(ctx->modules.module_names = KOS_new_object(frame));
    TRY_OBJID(ctx->modules.modules      = KOS_new_array(frame, 0));
    TRY_OBJID(ctx->modules.search_paths = KOS_new_array(frame, 0));

    TRY_OBJID(ctx->args = KOS_new_array(frame, 0));

    TRY(_init_search_paths(frame));

    *out_frame = frame;

_error:
    if (error && heap_ok)
        _KOS_heap_destroy(ctx);

    if (error && thread_ok) {
        _KOS_tls_destroy(ctx->threads.thread_key);
        _KOS_destroy_mutex(&ctx->threads.mutex);
    }

    return error;
}

void KOS_context_destroy(KOS_CONTEXT *ctx)
{
    uint32_t  i;
    uint32_t  num_modules = KOS_get_array_size(ctx->modules.modules);
    KOS_FRAME frame       = ctx->threads.main_thread.frame;

    for (i = 0; i < num_modules; i++) {
        KOS_OBJ_ID module_obj = KOS_array_read(frame, ctx->modules.modules, (int)i);
        assert(!IS_BAD_PTR(module_obj));
        if (IS_BAD_PTR(module_obj))
            KOS_clear_exception(frame);
        else if (GET_OBJ_TYPE(module_obj) == OBJ_MODULE) {
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_BYTECODE))
                _KOS_free((void *)OBJPTR(MODULE, module_obj)->bytecode);
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_LINE_ADDRS))
                _KOS_free((void *)OBJPTR(MODULE, module_obj)->line_addrs);
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_FUNC_ADDRS))
                _KOS_free((void *)OBJPTR(MODULE, module_obj)->func_addrs);
        }
        else {
            /* failed e.g. during compilation */
            assert(GET_OBJ_TYPE(module_obj) == OBJ_VOID);
        }
    }

    _KOS_heap_destroy(ctx);

    _KOS_tls_destroy(ctx->threads.thread_key);

    _KOS_destroy_mutex(&ctx->threads.mutex);

    memset(ctx, 0, sizeof(*ctx));

#ifdef CONFIG_PERF
#   define PERF_RATIO(a) do {                                             \
        const uint32_t va = KOS_atomic_read_u32(_kos_perf.a##_success);   \
        const uint32_t vb = KOS_atomic_read_u32(_kos_perf.a##_fail);      \
        uint32_t       total = va + vb;                                   \
        if (total == 0) total = 1;                                        \
        fprintf(stderr, "    " #a "\t%u / %u (%u%%)\n",                   \
                va, total, va * 100 / total);                             \
    } while (0)
#   define PERF_VALUE(a) do {                                             \
        const uint32_t va = KOS_atomic_read_u32(_kos_perf.a);             \
        fprintf(stderr, "    " #a "\t%u\n", va);                          \
    } while (0)
    printf("Performance stats:\n");
    PERF_RATIO(object_get);
    PERF_RATIO(object_set);
    PERF_RATIO(object_delete);
    PERF_RATIO(object_resize);
    PERF_RATIO(object_salvage);
    PERF_VALUE(object_collision[0]);
    PERF_VALUE(object_collision[1]);
    PERF_VALUE(object_collision[2]);
    PERF_VALUE(object_collision[3]);
    PERF_RATIO(array_salvage);
    PERF_VALUE(alloc_object);
    PERF_VALUE(alloc_huge_object);
    PERF_VALUE(alloc_new_page);
    PERF_VALUE(alloc_free_page);
#endif
}

int KOS_context_add_path(KOS_FRAME frame, const char *module_search_path)
{
    int          error;
    uint32_t     len;
    KOS_OBJ_ID   path_str;
    KOS_CONTEXT *ctx = KOS_context_from_frame(frame);

    path_str = KOS_new_cstring(frame, module_search_path);
    TRY_OBJID(path_str);

    len = KOS_get_array_size(ctx->modules.search_paths);
    TRY(KOS_array_resize(frame, ctx->modules.search_paths, len+1));
    TRY(KOS_array_write(frame, ctx->modules.search_paths, (int)len, path_str));

_error:
    return error;
}

#ifndef CONFIG_MODULE_PATH
#   ifdef _WIN32
#       define CONFIG_MODULE_PATH "modules"
#   else
#       define CONFIG_MODULE_PATH "../share/kos/modules"
#   endif
#endif

int KOS_context_add_default_path(KOS_FRAME frame, const char *argv0)
{
    int                error      = KOS_ERROR_NOT_FOUND;
    struct _KOS_VECTOR cstr;
    struct _KOS_VECTOR cpath;
    size_t             pos;
    static const char  rel_path[] = CONFIG_MODULE_PATH;

    _KOS_vector_init(&cstr);
    _KOS_vector_init(&cpath);

    if (argv0) {

        size_t len = strlen(argv0);

        if ( ! len)
            goto _error;

        /* Absolute or relative path */
        if (strchr(argv0, KOS_PATH_SEPARATOR)) {

            if ( ! _KOS_does_file_exist(argv0))
                RAISE_ERROR(KOS_ERROR_NOT_FOUND);

            len += 1;
            TRY(_KOS_vector_resize(&cstr, len));

            memcpy(cstr.buffer, argv0, len);
        }
        /* Just executable name, scan PATH */
        else {

            char *buf;

            TRY(_KOS_get_env("PATH", &cpath));

            buf = cpath.buffer;

            TRY(_KOS_vector_reserve(&cstr, cpath.size + len + 1));

            cstr.size = 0;

            while ((size_t)(buf - cpath.buffer + 1) < cpath.size) {

                char  *end = strchr(buf, KOS_PATH_LIST_SEPARATOR);
                size_t base_len;

                if ( ! end)
                    end = cpath.buffer + cpath.size - 1;

                base_len = end - buf;

                TRY(_KOS_vector_resize(&cstr, base_len + 1 + len + 1));

                memcpy(cstr.buffer, buf, base_len);
                cstr.buffer[base_len] = KOS_PATH_SEPARATOR;
                memcpy(&cstr.buffer[base_len + 1], argv0, len);
                cstr.buffer[base_len + 1 + len] = 0;

                if (_KOS_does_file_exist(cstr.buffer))
                    break;

                cstr.size = 0;

                buf = end + 1;
            }

            if (cstr.size == 0)
                RAISE_ERROR(KOS_ERROR_NOT_FOUND);
        }
    }
    else {
        if (_KOS_seq_fail())
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        TRY(_KOS_executable_path(&cstr));
    }

    TRY(_KOS_get_absolute_path(&cstr));

    assert(cstr.size > 0);

    for (pos = cstr.size - 1; pos > 0 && cstr.buffer[pos] != KOS_PATH_SEPARATOR; --pos);

    if ( ! pos)
        RAISE_ERROR(KOS_ERROR_NOT_FOUND);

    TRY(_KOS_vector_resize(&cstr, pos + 1 + sizeof(rel_path)));

    memcpy(&cstr.buffer[pos + 1], rel_path, sizeof(rel_path));

    TRY(KOS_context_add_path(frame, cstr.buffer));

_error:
    _KOS_vector_destroy(&cpath);
    _KOS_vector_destroy(&cstr);

    return error;
}

int KOS_context_set_args(KOS_FRAME    frame,
                         int          argc,
                         const char **argv)
{
    int                       error;
    int                       i;
    KOS_CONTEXT              *ctx        = KOS_context_from_frame(frame);

    assert(argc >= 0);

    if (argc <= 0)
        return KOS_SUCCESS;

    TRY(KOS_array_resize(frame, ctx->args, (uint32_t)argc));

    for (i = 0; i < argc; i++) {
        KOS_OBJ_ID arg_str = KOS_new_cstring(frame, argv[i]);
        TRY_OBJID(arg_str);

        TRY(KOS_array_write(frame, ctx->args, i, arg_str));
    }

_error:
    return error;
}

static int _module_init_compare(struct _KOS_RED_BLACK_NODE *a,
                                struct _KOS_RED_BLACK_NODE *b)
{
    struct _KOS_MODULE_INIT *init_a = (struct _KOS_MODULE_INIT *)a;
    struct _KOS_MODULE_INIT *init_b = (struct _KOS_MODULE_INIT *)b;

    return KOS_string_compare(init_a->name, init_b->name);
}

int KOS_context_register_builtin(KOS_FRAME        frame,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init)
{
    int                      error = KOS_SUCCESS;
    struct _KOS_MODULE_INIT *mod_init;
    KOS_CONTEXT             *ctx   = KOS_context_from_frame(frame);
    KOS_OBJ_ID               module_name;

    module_name = KOS_new_cstring(frame, module);
    TRY_OBJID(module_name);

    mod_init = (struct _KOS_MODULE_INIT *)_KOS_alloc_object(frame,
                                                            KOS_ALLOC_PERSISTENT,
                                                            OBJ_OPAQUE, /* TODO double check type */
                                                            sizeof(struct _KOS_MODULE_INIT));
    if ( ! mod_init)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    mod_init->name = module_name;
    mod_init->init = init;

    _KOS_red_black_insert(&ctx->modules.module_inits, &mod_init->rb_tree_node, _module_init_compare);

_error:
    return error;
}

KOS_OBJ_ID KOS_context_get_cstring(KOS_FRAME   frame,
                                   const char *cstr)
{
    /* TODO lookup in array */
    KOS_OBJ_ID str = KOS_new_const_ascii_cstring(frame, cstr);

    if (IS_BAD_PTR(str)) {
        KOS_clear_exception(frame);
        str = KOS_VOID;
    }

    return str;
}

#ifndef NDEBUG
void KOS_context_validate(KOS_FRAME frame)
{
    KOS_CONTEXT        *ctx = KOS_context_from_frame(frame);
    KOS_THREAD_CONTEXT *thread_ctx;

    assert(ctx);

    thread_ctx = (KOS_THREAD_CONTEXT *)_KOS_tls_get(ctx->threads.thread_key);

    assert(thread_ctx);
    assert(thread_ctx == frame->thread_ctx);
}
#endif

void KOS_raise_exception(KOS_FRAME  frame,
                         KOS_OBJ_ID exception_obj)
{
    /* Nested exceptions are not allowed. */
    /* This can only happen if there is a bug and an exception has been ignored. */
    assert(IS_BAD_PTR(frame->exception));

    if (IS_BAD_PTR(frame->exception))
        frame->exception = exception_obj;
}

void KOS_raise_exception_cstring(KOS_FRAME   frame,
                                 const char *cstr)
{
    KOS_raise_exception(frame, KOS_context_get_cstring(frame, cstr));
}

void KOS_clear_exception(KOS_FRAME frame)
{
    frame->exception = KOS_BADPTR;
}

int KOS_is_exception_pending(KOS_FRAME frame)
{
    return ! IS_BAD_PTR(frame->exception);
}

KOS_OBJ_ID KOS_get_exception(KOS_FRAME frame)
{
    return frame->exception;
}

void _KOS_wrap_exception(KOS_FRAME frame)
{
    int                error         = KOS_SUCCESS;
    unsigned           depth;
    KOS_OBJ_ID         exception;
    KOS_OBJ_ID         backtrace;
    KOS_OBJ_ID         thrown_object = frame->exception;
    KOS_FRAME          next_frame;
    KOS_CONTEXT *const ctx           = KOS_context_from_frame(frame);
    int                partial_wrap  = 0;

    assert(!IS_BAD_PTR(thrown_object));

    if (GET_OBJ_TYPE(thrown_object) == OBJ_OBJECT) {

        const KOS_OBJ_ID proto = KOS_get_prototype(frame, thrown_object);

        if (proto == ctx->prototypes.exception_proto)
            /* Exception already wrapped */
            return;
    }

    KOS_clear_exception(frame);

    exception = KOS_new_object_with_prototype(frame, ctx->prototypes.exception_proto);
    TRY_OBJID(exception);

    TRY(KOS_set_property(frame, exception, KOS_context_get_cstring(frame, str_value), thrown_object));

    partial_wrap = 1;

    depth      = 0;
    next_frame = frame;
    while (next_frame) {
        ++depth;
        next_frame = IS_BAD_PTR(next_frame->parent) ? 0 : OBJPTR(STACK_FRAME, next_frame->parent);
    }

    backtrace = KOS_new_array(frame, depth);
    TRY_OBJID(backtrace);

    TRY(KOS_array_resize(frame, backtrace, depth));

    TRY(KOS_set_property(frame, exception, KOS_context_get_cstring(frame, str_backtrace), backtrace));

    depth      = 0;
    next_frame = frame;
    while (next_frame) {
        KOS_MODULE    *module      = OBJPTR(MODULE, next_frame->module);
        const unsigned line        = KOS_module_addr_to_line(module, next_frame->instr_offs);
        KOS_OBJ_ID     module_name = KOS_context_get_cstring(frame, str_builtin);
        KOS_OBJ_ID     module_path = KOS_context_get_cstring(frame, str_builtin);
        KOS_OBJ_ID     func_name   = KOS_module_addr_to_func_name(module, next_frame->instr_offs);

        KOS_OBJ_ID frame_desc = KOS_new_object(frame);
        TRY_OBJID(frame_desc);

        if (IS_BAD_PTR(func_name))
            func_name = KOS_context_get_cstring(frame, str_builtin); /* TODO add builtin function name */

        assert(depth < KOS_get_array_size(backtrace));
        TRY(KOS_array_write(frame, backtrace, (int)depth, frame_desc));

        /* TODO use builtin function pointer for offset */

        if (module) {
            module_name = module->name;
            module_path = module->path;
        }

        TRY(KOS_set_property(frame, frame_desc, KOS_context_get_cstring(frame, str_module),   module_name));
        TRY(KOS_set_property(frame, frame_desc, KOS_context_get_cstring(frame, str_file),     module_path));
        TRY(KOS_set_property(frame, frame_desc, KOS_context_get_cstring(frame, str_line),     TO_SMALL_INT((int)line)));
        TRY(KOS_set_property(frame, frame_desc, KOS_context_get_cstring(frame, str_offset),   TO_SMALL_INT((int)next_frame->instr_offs)));
        TRY(KOS_set_property(frame, frame_desc, KOS_context_get_cstring(frame, str_function), func_name));

        ++depth;
        next_frame = IS_BAD_PTR(next_frame->parent) ? 0 : OBJPTR(STACK_FRAME, next_frame->parent);
    }

    frame->exception = exception;

_error:
    if (error)
        frame->exception = partial_wrap ? exception : thrown_object;
}

KOS_OBJ_ID KOS_format_exception(KOS_FRAME  frame,
                                KOS_OBJ_ID exception)
{
    int        error;
    unsigned   i;
    unsigned   depth;
    KOS_OBJ_ID value;
    KOS_OBJ_ID backtrace;
    KOS_OBJ_ID array = KOS_BADPTR;
    KOS_OBJ_ID str;

    value = KOS_get_property(frame, exception, KOS_context_get_cstring(frame, str_value));
    TRY_OBJID(value);

    backtrace = KOS_get_property(frame, exception, KOS_context_get_cstring(frame, str_backtrace));
    TRY_OBJID(backtrace);

    if (GET_OBJ_TYPE(backtrace) != OBJ_ARRAY) {
        KOS_raise_exception_cstring(frame, str_err_not_array);
        TRY(KOS_ERROR_EXCEPTION);
    }

    depth = KOS_get_array_size(backtrace);
    array = KOS_new_array(frame, 1+depth);
    TRY_OBJID(array);

    str = KOS_object_to_string(frame, value);
    TRY_OBJID(str);

    str = KOS_string_add(frame, KOS_context_get_cstring(frame, str_format_exception), str);
    TRY_OBJID(str);

    TRY(KOS_array_write(frame, array, 0, str));

    for (i = 0; i < depth; i++) {
        KOS_OBJ_ID             frame_desc = KOS_array_read(frame, backtrace, (int)i);
        KOS_ATOMIC(KOS_OBJ_ID) parts[10];

        TRY_OBJID(frame_desc);

        parts[0] = KOS_context_get_cstring(frame, str_format_hash);

        parts[1] = KOS_object_to_string(frame, TO_SMALL_INT((int)i));
        TRY_OBJID(parts[1]);

        parts[2] = KOS_context_get_cstring(frame, str_format_offset);

        str = KOS_get_property(frame, frame_desc, KOS_context_get_cstring(frame, str_offset));
        TRY_OBJID(str);
        if (IS_SMALL_INT(str)) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "0x%X", (unsigned)GET_SMALL_INT(str));
            parts[3] = KOS_new_cstring(frame, cbuf);
            TRY_OBJID(parts[3]);
        }
        else
            parts[3] = KOS_context_get_cstring(frame, str_format_question_marks);

        parts[4] = KOS_context_get_cstring(frame, str_format_function);

        str = KOS_get_property(frame, frame_desc, KOS_context_get_cstring(frame, str_function));
        TRY_OBJID(str);
        parts[5] = str;

        parts[6] = KOS_context_get_cstring(frame, str_format_module);

        str = KOS_get_property(frame, frame_desc, KOS_context_get_cstring(frame, str_file));
        TRY_OBJID(str);
        str = KOS_get_file_name(frame, str);
        TRY_OBJID(str);
        parts[7] = str;

        parts[8] = KOS_context_get_cstring(frame, str_format_line);

        str = KOS_get_property(frame, frame_desc, KOS_context_get_cstring(frame, str_line));
        TRY_OBJID(str);
        parts[9] = KOS_object_to_string(frame, str);
        TRY_OBJID(parts[9]);

        str = KOS_string_add_many(frame, parts, sizeof(parts)/sizeof(parts[0]));
        TRY_OBJID(str);

        TRY(KOS_array_write(frame, array, 1+(int)i, str));
    }

_error:
    if (error)
        array = KOS_BADPTR;

    return array;
}

void KOS_raise_generator_end(KOS_FRAME frame)
{
    KOS_CONTEXT *const ctx = KOS_context_from_frame(frame);

    const KOS_OBJ_ID exception =
            KOS_new_object_with_prototype(frame, ctx->prototypes.generator_end_proto);

    if ( ! IS_BAD_PTR(exception))
        KOS_raise_exception(frame, exception);
}

void KOS_track_ref(KOS_FRAME    frame,
                   KOS_OBJ_REF *ref)
{
    ref->next       = frame->obj_refs;
    frame->obj_refs = ref;
}

void KOS_untrack_ref(KOS_FRAME    frame,
                     KOS_OBJ_REF *ref)
{
    KOS_OBJ_REF **slot = &frame->obj_refs;
    KOS_OBJ_REF  *cur;

    cur = *slot;

    while (cur && cur != ref) {

        slot = &cur->next;
        cur  = *slot;
    }

    assert(cur);

    *slot = ref->next;
}

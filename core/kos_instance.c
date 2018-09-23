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

#include "../inc/kos_instance.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
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
static const char str_offset[]                  = "offset";
static const char str_value[]                   = "value";

DECLARE_CONST_OBJECT(_kos_void)  = KOS_CONST_OBJECT_INIT(OBJ_VOID,    0);
DECLARE_CONST_OBJECT(_kos_false) = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 0);
DECLARE_CONST_OBJECT(_kos_true)  = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 1);

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

static int _register_thread(KOS_INSTANCE *inst,
                            KOS_CONTEXT   ctx)
{
    int error = KOS_SUCCESS;

    assert( ! _KOS_tls_get(inst->threads.thread_key));

    ctx->inst        = inst;
    ctx->exception   = KOS_BADPTR;
    ctx->retval      = KOS_BADPTR;
    ctx->obj_refs    = 0;
    ctx->stack       = KOS_BADPTR;
    ctx->regs_idx    = 0;
    ctx->stack_depth = 0;

    if (_KOS_tls_get(inst->threads.thread_key)) {

        KOS_OBJ_ID err = KOS_instance_get_cstring(ctx, str_err_thread_registered);
        KOS_raise_exception(ctx, err);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    _KOS_tls_set(inst->threads.thread_key, ctx);

_error:
    if (error)
        _KOS_heap_release_thread_page(ctx);

    return error;
}

static void _unregister_thread(KOS_INSTANCE *inst,
                               KOS_CONTEXT   ctx)
{
    _KOS_heap_release_thread_page(ctx);

    _KOS_tls_set(inst->threads.thread_key, 0);

    _KOS_lock_mutex(&inst->threads.mutex);

    assert(ctx != &inst->threads.main_thread);

    if (ctx->prev)
        ctx->prev->next = ctx->next;

    if (ctx->next)
        ctx->next->prev = ctx->prev;

    _KOS_unlock_mutex(&inst->threads.mutex);
}

int KOS_instance_register_thread(KOS_INSTANCE *inst,
                                 KOS_CONTEXT   ctx)
{
    int error;

    _KOS_lock_mutex(&inst->threads.mutex);

    ctx->prev                      = &inst->threads.main_thread;
    ctx->next                      = inst->threads.main_thread.next;
    inst->threads.main_thread.next = ctx;
    if (ctx->next)
        ctx->next->prev            = ctx;

    _KOS_unlock_mutex(&inst->threads.mutex);

    ctx->cur_page = 0;

    error = _register_thread(inst, ctx);

    if (error)
        _unregister_thread(inst, ctx);

    return error;
}

void KOS_instance_unregister_thread(KOS_INSTANCE *inst,
                                    KOS_CONTEXT   ctx)
{
    assert((KOS_CONTEXT)_KOS_tls_get(inst->threads.thread_key) == ctx);

    _unregister_thread(inst, ctx);
}

static int _add_multiple_paths(KOS_CONTEXT ctx, struct _KOS_VECTOR *cpaths)
{
    int   error = KOS_SUCCESS;
    char *buf   = cpaths->buffer;

    while ( ! error) {
        char *end = strchr(buf, KOS_PATH_LIST_SEPARATOR);

        if (end)
            *end = '\0';

        error = KOS_instance_add_path(ctx, buf);

        if (end)
            buf = end + 1;
        else
            break;
    }

    return error;
}

static int _init_search_paths(KOS_CONTEXT ctx)
{
#ifdef CONFIG_DISABLE_KOSPATH
    return KOS_SUCCESS;
#else
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpaths;

    _KOS_vector_init(&cpaths);

    if (_KOS_get_env("KOSPATH", &cpaths) == KOS_SUCCESS)
        error = _add_multiple_paths(ctx, &cpaths);

    _KOS_vector_destroy(&cpaths);

    return error;
#endif
}

static KOS_OBJ_ID _alloc_empty_string(KOS_CONTEXT ctx)
{
    KOS_STRING *str = (KOS_STRING *)_KOS_alloc_object(ctx,
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

static void _clear_instance(KOS_INSTANCE *inst)
{
    inst->flags                           = 0;
    inst->empty_string                    = KOS_BADPTR;
    inst->args                            = KOS_BADPTR;
    inst->prototypes.object_proto         = KOS_BADPTR;
    inst->prototypes.number_proto         = KOS_BADPTR;
    inst->prototypes.integer_proto        = KOS_BADPTR;
    inst->prototypes.float_proto          = KOS_BADPTR;
    inst->prototypes.string_proto         = KOS_BADPTR;
    inst->prototypes.boolean_proto        = KOS_BADPTR;
    inst->prototypes.array_proto          = KOS_BADPTR;
    inst->prototypes.buffer_proto         = KOS_BADPTR;
    inst->prototypes.function_proto       = KOS_BADPTR;
    inst->prototypes.class_proto          = KOS_BADPTR;
    inst->prototypes.generator_proto      = KOS_BADPTR;
    inst->prototypes.exception_proto      = KOS_BADPTR;
    inst->prototypes.generator_end_proto  = KOS_BADPTR;
    inst->prototypes.thread_proto         = KOS_BADPTR;
    inst->modules.search_paths            = KOS_BADPTR;
    inst->modules.module_names            = KOS_BADPTR;
    inst->modules.modules                 = KOS_BADPTR;
    inst->modules.init_module             = KOS_BADPTR;
    inst->modules.module_inits            = 0;
    inst->modules.load_chain              = 0;
    inst->threads.main_thread.next        = 0;
    inst->threads.main_thread.prev        = 0;
    inst->threads.main_thread.inst        = inst;
    inst->threads.main_thread.cur_page    = 0;
    inst->threads.main_thread.exception   = KOS_BADPTR;
    inst->threads.main_thread.retval      = KOS_BADPTR;
    inst->threads.main_thread.obj_refs    = 0;
    inst->threads.main_thread.stack       = KOS_BADPTR;
    inst->threads.main_thread.stack_depth = 0;
}

int KOS_instance_init(KOS_INSTANCE *inst,
                      KOS_CONTEXT     *out_frame)
{
    int         error;
    int         heap_ok   = 0;
    int         thread_ok = 0;
    KOS_MODULE *init_module;
    KOS_CONTEXT ctx;

    assert(!IS_HEAP_OBJECT(KOS_VOID));
    assert(!IS_HEAP_OBJECT(KOS_FALSE));
    assert(!IS_HEAP_OBJECT(KOS_TRUE));

    _clear_instance(inst);

    TRY(_KOS_tls_create(&inst->threads.thread_key));
    error = _KOS_create_mutex(&inst->threads.mutex);
    if (error) {
        _KOS_tls_destroy(inst->threads.thread_key);
        goto _error;
    }
    thread_ok = 1;

    TRY(_KOS_heap_init(inst));
    heap_ok = 1;

    init_module = (KOS_MODULE *)_KOS_heap_early_alloc(inst,
                                                      &inst->threads.main_thread,
                                                      OBJ_MODULE,
                                                      (uint32_t)sizeof(KOS_MODULE));
    if ( ! init_module)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    init_module->flags             = 0;
    init_module->name              = KOS_BADPTR;
    init_module->path              = KOS_BADPTR;
    init_module->inst              = inst;
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

    inst->modules.init_module  = OBJID(MODULE, init_module);

    TRY(_register_thread(inst, &inst->threads.main_thread));

    ctx = &inst->threads.main_thread;

    inst->empty_string = _alloc_empty_string(ctx);
    TRY_OBJID(inst->empty_string);

    {
        KOS_OBJ_ID str = KOS_new_cstring(ctx, str_err_out_of_memory);
        TRY_OBJID(str);
        inst->heap.str_oom_id = str;
    }

    TRY_OBJID(inst->prototypes.object_proto        = KOS_new_object_with_prototype(ctx, KOS_VOID));
    TRY_OBJID(inst->prototypes.number_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.integer_proto       = KOS_new_object_with_prototype(ctx, inst->prototypes.number_proto));
    TRY_OBJID(inst->prototypes.float_proto         = KOS_new_object_with_prototype(ctx, inst->prototypes.number_proto));
    TRY_OBJID(inst->prototypes.string_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.boolean_proto       = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.array_proto         = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.buffer_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.function_proto      = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.class_proto         = KOS_new_object_with_prototype(ctx, inst->prototypes.function_proto));
    TRY_OBJID(inst->prototypes.generator_proto     = KOS_new_object_with_prototype(ctx, inst->prototypes.function_proto));
    TRY_OBJID(inst->prototypes.exception_proto     = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.generator_end_proto = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.thread_proto        = KOS_new_object(ctx));

    TRY_OBJID(init_module->name          = KOS_instance_get_cstring(ctx, str_init));
    TRY_OBJID(init_module->globals       = KOS_new_array(ctx, 0));
    TRY_OBJID(init_module->global_names  = KOS_new_object(ctx));
    TRY_OBJID(init_module->module_names  = KOS_new_object(ctx));
    TRY_OBJID(inst->modules.module_names = KOS_new_object(ctx));
    TRY_OBJID(inst->modules.modules      = KOS_new_array(ctx, 0));
    TRY_OBJID(inst->modules.search_paths = KOS_new_array(ctx, 0));

    TRY_OBJID(inst->args = KOS_new_array(ctx, 0));

    TRY(_init_search_paths(ctx));

    *out_frame = ctx;

_error:
    if (error && heap_ok)
        _KOS_heap_destroy(inst);

    if (error && thread_ok) {
        _KOS_tls_destroy(inst->threads.thread_key);
        _KOS_destroy_mutex(&inst->threads.mutex);
    }

    return error;
}

void KOS_instance_destroy(KOS_INSTANCE *inst)
{
    uint32_t i;
    uint32_t num_modules = KOS_get_array_size(inst->modules.modules);
    KOS_CONTEXT ctx        = &inst->threads.main_thread;

    for (i = 0; i < num_modules; i++) {
        KOS_OBJ_ID module_obj = KOS_array_read(ctx, inst->modules.modules, (int)i);
        assert(!IS_BAD_PTR(module_obj));
        if (IS_BAD_PTR(module_obj))
            KOS_clear_exception(ctx);
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

    _KOS_heap_destroy(inst);

    _KOS_tls_destroy(inst->threads.thread_key);

    _KOS_destroy_mutex(&inst->threads.mutex);

    _clear_instance(inst);

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

int KOS_instance_add_path(KOS_CONTEXT ctx, const char *module_search_path)
{
    int           error;
    uint32_t      len;
    KOS_OBJ_ID    path_str;
    KOS_INSTANCE *inst = ctx->inst;

    path_str = KOS_new_cstring(ctx, module_search_path);
    TRY_OBJID(path_str);

    len = KOS_get_array_size(inst->modules.search_paths);
    TRY(KOS_array_resize(ctx, inst->modules.search_paths, len+1));
    TRY(KOS_array_write(ctx, inst->modules.search_paths, (int)len, path_str));

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

int KOS_instance_add_default_path(KOS_CONTEXT ctx, const char *argv0)
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

    TRY(KOS_instance_add_path(ctx, cstr.buffer));

_error:
    _KOS_vector_destroy(&cpath);
    _KOS_vector_destroy(&cstr);

    return error;
}

int KOS_instance_set_args(KOS_CONTEXT  ctx,
                          int          argc,
                          const char **argv)
{
    int           error;
    int           i;
    KOS_INSTANCE *inst = ctx->inst;

    assert(argc >= 0);

    if (argc <= 0)
        return KOS_SUCCESS;

    TRY(KOS_array_resize(ctx, inst->args, (uint32_t)argc));

    for (i = 0; i < argc; i++) {
        KOS_OBJ_ID arg_str = KOS_new_cstring(ctx, argv[i]);
        TRY_OBJID(arg_str);

        TRY(KOS_array_write(ctx, inst->args, i, arg_str));
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

int KOS_instance_register_builtin(KOS_CONTEXT      ctx,
                                  const char      *module,
                                  KOS_BUILTIN_INIT init)
{
    int                      error = KOS_SUCCESS;
    struct _KOS_MODULE_INIT *mod_init;
    KOS_INSTANCE      *const inst  = ctx->inst;
    KOS_OBJ_ID               module_name;

    module_name = KOS_new_cstring(ctx, module);
    TRY_OBJID(module_name);

    mod_init = (struct _KOS_MODULE_INIT *)_KOS_alloc_object(ctx,
                                                            KOS_ALLOC_PERSISTENT,
                                                            OBJ_OPAQUE, /* TODO double check type */
                                                            sizeof(struct _KOS_MODULE_INIT));
    if ( ! mod_init)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    mod_init->name = module_name;
    mod_init->init = init;

    _KOS_red_black_insert(&inst->modules.module_inits, &mod_init->rb_tree_node, _module_init_compare);

_error:
    return error;
}

KOS_OBJ_ID KOS_instance_get_cstring(KOS_CONTEXT ctx,
                                    const char *cstr)
{
    /* TODO lookup in array */
    KOS_OBJ_ID str = KOS_new_const_ascii_cstring(ctx, cstr);

    if (IS_BAD_PTR(str)) {
        KOS_clear_exception(ctx);
        str = KOS_VOID;
    }

    return str;
}

#ifndef NDEBUG
void KOS_instance_validate(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *inst = ctx->inst;
    KOS_CONTEXT   thread_ctx;

    assert(inst);

    thread_ctx = (KOS_CONTEXT)_KOS_tls_get(inst->threads.thread_key);

    assert(thread_ctx);
    assert(thread_ctx == ctx);
}
#endif

void KOS_raise_exception(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  exception_obj)
{
    /* Nested exceptions are not allowed. */
    /* This can only happen if there is a bug and an exception has been ignored. */
    assert(IS_BAD_PTR(ctx->exception));

    if (IS_BAD_PTR(ctx->exception))
        ctx->exception = exception_obj;
}

void KOS_raise_exception_cstring(KOS_CONTEXT ctx,
                                 const char *cstr)
{
    KOS_raise_exception(ctx, KOS_instance_get_cstring(ctx, cstr));
}

KOS_OBJ_ID KOS_format_exception(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  exception)
{
    int        error;
    unsigned   i;
    unsigned   depth;
    KOS_OBJ_ID value;
    KOS_OBJ_ID backtrace;
    KOS_OBJ_ID array = KOS_BADPTR;
    KOS_OBJ_ID str;

    value = KOS_get_property(ctx, exception, KOS_instance_get_cstring(ctx, str_value));
    TRY_OBJID(value);

    backtrace = KOS_get_property(ctx, exception, KOS_instance_get_cstring(ctx, str_backtrace));
    TRY_OBJID(backtrace);

    if (GET_OBJ_TYPE(backtrace) != OBJ_ARRAY) {
        KOS_raise_exception_cstring(ctx, str_err_not_array);
        TRY(KOS_ERROR_EXCEPTION);
    }

    depth = KOS_get_array_size(backtrace);
    array = KOS_new_array(ctx, 1+depth);
    TRY_OBJID(array);

    str = KOS_object_to_string(ctx, value);
    TRY_OBJID(str);

    str = KOS_string_add(ctx, KOS_instance_get_cstring(ctx, str_format_exception), str);
    TRY_OBJID(str);

    TRY(KOS_array_write(ctx, array, 0, str));

    for (i = 0; i < depth; i++) {
        KOS_OBJ_ID             frame_desc = KOS_array_read(ctx, backtrace, (int)i);
        KOS_ATOMIC(KOS_OBJ_ID) parts[10];

        TRY_OBJID(frame_desc);

        parts[0] = KOS_instance_get_cstring(ctx, str_format_hash);

        parts[1] = KOS_object_to_string(ctx, TO_SMALL_INT((int)i));
        TRY_OBJID(parts[1]);

        parts[2] = KOS_instance_get_cstring(ctx, str_format_offset);

        str = KOS_get_property(ctx, frame_desc, KOS_instance_get_cstring(ctx, str_offset));
        TRY_OBJID(str);
        if (IS_SMALL_INT(str)) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "0x%X", (unsigned)GET_SMALL_INT(str));
            parts[3] = KOS_new_cstring(ctx, cbuf);
            TRY_OBJID(parts[3]);
        }
        else
            parts[3] = KOS_instance_get_cstring(ctx, str_format_question_marks);

        parts[4] = KOS_instance_get_cstring(ctx, str_format_function);

        str = KOS_get_property(ctx, frame_desc, KOS_instance_get_cstring(ctx, str_function));
        TRY_OBJID(str);
        parts[5] = str;

        parts[6] = KOS_instance_get_cstring(ctx, str_format_module);

        str = KOS_get_property(ctx, frame_desc, KOS_instance_get_cstring(ctx, str_file));
        TRY_OBJID(str);
        str = KOS_get_file_name(ctx, str);
        TRY_OBJID(str);
        parts[7] = str;

        parts[8] = KOS_instance_get_cstring(ctx, str_format_line);

        str = KOS_get_property(ctx, frame_desc, KOS_instance_get_cstring(ctx, str_line));
        TRY_OBJID(str);
        parts[9] = KOS_object_to_string(ctx, str);
        TRY_OBJID(parts[9]);

        str = KOS_string_add_many(ctx, parts, sizeof(parts)/sizeof(parts[0]));
        TRY_OBJID(str);

        TRY(KOS_array_write(ctx, array, 1+(int)i, str));
    }

_error:
    if (error)
        array = KOS_BADPTR;

    return array;
}

void KOS_raise_generator_end(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    const KOS_OBJ_ID exception =
            KOS_new_object_with_prototype(ctx, inst->prototypes.generator_end_proto);

    if ( ! IS_BAD_PTR(exception))
        KOS_raise_exception(ctx, exception);
}

void KOS_track_ref(KOS_CONTEXT  ctx,
                   KOS_OBJ_REF *ref)
{
    ref->next     = ctx->obj_refs;
    ctx->obj_refs = ref;
}

void KOS_untrack_ref(KOS_CONTEXT  ctx,
                     KOS_OBJ_REF *ref)
{
    KOS_OBJ_REF **slot = &ctx->obj_refs;
    KOS_OBJ_REF  *cur;

    cur = *slot;

    while (cur && cur != ref) {

        slot = &cur->next;
        cur  = *slot;
    }

    assert(cur);

    *slot = ref->next;
}

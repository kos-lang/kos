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
#include "kos_file.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char str_init[]                    = "init";
static const char str_backtrace[]               = "backtrace";
static const char str_builtin[]                 = "<builtin>";
static const char str_err_not_array[]           = "object is not an array";
static const char str_err_number_out_of_range[] = "number out of range";
static const char str_err_out_of_memory[]       = "out of memory";
static const char str_err_unsup_operand_types[] = "unsupported operand types";
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
                            KOS_THREAD_ROOT    *thread_root)
{
    int error = KOS_SUCCESS;

    TRY(_KOS_init_stack_frame(&thread_root->frame, &ctx->init_module, 0));

    if (_KOS_tls_get(ctx->thread_key)) {

        KOS_OBJ_ID err;

        assert( ! _KOS_tls_get(ctx->thread_key));

        err = KOS_context_get_cstring(&thread_root->frame, str_err_thread_registered);
        KOS_raise_exception(&thread_root->frame, err);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    _KOS_tls_set(ctx->thread_key, thread_root);

_error:
    return error;
}

int KOS_context_register_thread(KOS_CONTEXT *ctx, KOS_THREAD_ROOT *thread_root)
{
    return _register_thread(ctx, thread_root);
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

static KOS_OBJ_ID _alloc_void(KOS_FRAME frame)
{
    KOS_VOID *obj = (KOS_VOID *)_KOS_alloc_object(frame,
                                                  KOS_ALLOC_PERSISTENT,
                                                  OBJ_VOID,
                                                  sizeof(KOS_VOID));

    return OBJID(VOID, obj);
}

static KOS_OBJ_ID _alloc_boolean(KOS_FRAME frame, uint8_t value)
{
    KOS_BOOLEAN *obj = (KOS_BOOLEAN *)_KOS_alloc_object(frame,
                                                        KOS_ALLOC_PERSISTENT,
                                                        OBJ_BOOLEAN,
                                                        sizeof(KOS_BOOLEAN));

    if (obj)
        obj->boolean.value = (uint8_t)value;

    return OBJID(BOOLEAN, obj);
}

static KOS_OBJ_ID _alloc_dummy(KOS_FRAME frame)
{
    KOS_OPAQUE *const obj = (KOS_OPAQUE *)
            _KOS_alloc_object(frame,
                              KOS_ALLOC_PERSISTENT,
                              OBJ_OPAQUE,
                              sizeof(KOS_OPAQUE));

    return OBJID(OPAQUE, obj);
}

int KOS_context_init(KOS_CONTEXT *ctx,
                     KOS_FRAME   *out_frame)
{
    int       error;
    int       alloc_ok = 0;
    int       tls_ok   = 0;
    KOS_FRAME frame    = &ctx->main_thread.frame;

    memset(ctx, 0, sizeof(*ctx));

    TRY(_KOS_tls_create(&ctx->thread_key));
    tls_ok = 1;

    TRY(_KOS_alloc_init(ctx));
    alloc_ok = 1;

    ctx->init_module.header.type  = OBJ_MODULE;
    ctx->init_module.name         = KOS_BADPTR;
    ctx->init_module.context      = ctx;
    ctx->init_module.global_names = KOS_BADPTR;
    ctx->init_module.globals      = KOS_BADPTR;
    ctx->init_module.module_names = KOS_BADPTR;
    ctx->module_names             = KOS_BADPTR;
    ctx->modules                  = KOS_BADPTR;
    ctx->module_search_paths      = KOS_BADPTR;
    ctx->args                     = KOS_BADPTR;

    TRY(_register_thread(ctx, &ctx->main_thread));

    ctx->empty_string = _alloc_empty_string(frame);
    TRY_OBJID(ctx->empty_string);

    ctx->void_obj = _alloc_void(frame);
    TRY_OBJID(ctx->void_obj);

    ctx->false_obj = _alloc_boolean(frame, 0);
    TRY_OBJID(ctx->false_obj);

    ctx->true_obj = _alloc_boolean(frame, 1);
    TRY_OBJID(ctx->true_obj);

    ctx->tombstone_obj = _alloc_dummy(frame);
    TRY_OBJID(ctx->tombstone_obj);

    ctx->closed_obj = _alloc_dummy(frame);
    TRY_OBJID(ctx->closed_obj);

    ctx->reserved_obj = _alloc_dummy(frame);
    TRY_OBJID(ctx->reserved_obj);

    ctx->new_this_obj = _alloc_dummy(frame);
    TRY_OBJID(ctx->new_this_obj);

    {
        KOS_OBJ_ID str = KOS_new_cstring(frame, str_err_out_of_memory);
        TRY_OBJID(str);
        ctx->allocator.str_oom_id = str;
    }

    TRY_OBJID(ctx->object_prototype        = KOS_new_object_with_prototype(frame, KOS_BADPTR));
    TRY_OBJID(ctx->number_prototype        = KOS_new_object(frame));
    TRY_OBJID(ctx->integer_prototype       = KOS_new_object_with_prototype(frame, ctx->number_prototype));
    TRY_OBJID(ctx->float_prototype         = KOS_new_object_with_prototype(frame, ctx->number_prototype));
    TRY_OBJID(ctx->string_prototype        = KOS_new_object(frame));
    TRY_OBJID(ctx->boolean_prototype       = KOS_new_object(frame));
    TRY_OBJID(ctx->void_prototype          = KOS_new_object(frame));
    TRY_OBJID(ctx->array_prototype         = KOS_new_object(frame));
    TRY_OBJID(ctx->buffer_prototype        = KOS_new_object(frame));
    TRY_OBJID(ctx->function_prototype      = KOS_new_object(frame));
    TRY_OBJID(ctx->constructor_prototype   = KOS_new_object_with_prototype(frame, ctx->function_prototype));
    TRY_OBJID(ctx->generator_prototype     = KOS_new_object_with_prototype(frame, ctx->function_prototype));
    TRY_OBJID(ctx->exception_prototype     = KOS_new_object(frame));
    TRY_OBJID(ctx->generator_end_prototype = KOS_new_object(frame));
    TRY_OBJID(ctx->thread_prototype        = KOS_new_object(frame));

    TRY_OBJID(ctx->init_module.name         = KOS_context_get_cstring(frame, str_init));
    TRY_OBJID(ctx->init_module.globals      = KOS_new_array(frame, 0));
    TRY_OBJID(ctx->init_module.global_names = KOS_new_object(frame));
    TRY_OBJID(ctx->init_module.module_names = KOS_new_object(frame));
    TRY_OBJID(ctx->module_names             = KOS_new_object(frame));
    TRY_OBJID(ctx->modules                  = KOS_new_array(frame, 0));
    TRY_OBJID(ctx->module_search_paths      = KOS_new_array(frame, 0));
    TRY_OBJID(ctx->args                     = KOS_new_array(frame, 0));

    TRY(_init_search_paths(frame));

    *out_frame = frame;

_error:
    if (error && alloc_ok)
        _KOS_alloc_destroy(ctx);

    if (error && tls_ok)
        _KOS_tls_destroy(ctx->thread_key);

    return error;
}

void KOS_context_destroy(KOS_CONTEXT *ctx)
{
    uint32_t  i;
    uint32_t  num_modules = KOS_get_array_size(ctx->modules);
    KOS_FRAME frame       = &ctx->main_thread.frame;

    for (i = 0; i < num_modules; i++) {
        KOS_OBJ_ID module_obj = KOS_array_read(frame, ctx->modules, (int)i);
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

    if (ctx->prototypes)
        _KOS_free(ctx->prototypes);

    _KOS_alloc_destroy(ctx);

    _KOS_tls_destroy(ctx->thread_key);

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
    int                       error;
    uint32_t                  len;
    KOS_OBJ_ID                path_str;
    KOS_CONTEXT              *ctx        = KOS_context_from_frame(frame);

    path_str = KOS_new_cstring(frame, module_search_path);
    TRY_OBJID(path_str);

    len = KOS_get_array_size(ctx->module_search_paths);
    TRY(KOS_array_resize(frame, ctx->module_search_paths, len+1));
    TRY(KOS_array_write(frame, ctx->module_search_paths, (int)len, path_str));

_error:
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

    _KOS_red_black_insert(&ctx->module_inits, &mod_init->rb_tree_node, _module_init_compare);

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
        str = KOS_new_void(frame);
    }

    return str;
}

#ifndef NDEBUG
void KOS_context_validate(KOS_FRAME frame)
{
    KOS_CONTEXT     *ctx = KOS_context_from_frame(frame);
    KOS_THREAD_ROOT *thread_root;

    assert(ctx);

    thread_root = (KOS_THREAD_ROOT *)_KOS_tls_get(ctx->thread_key);

    assert(thread_root);
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

        if (proto == ctx->exception_prototype)
            /* Exception already wrapped */
            return;
    }

    KOS_clear_exception(frame);

    exception = KOS_new_object_with_prototype(frame, ctx->exception_prototype);
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

KOS_OBJ_ID KOS_get_file_name(KOS_FRAME  frame,
                             KOS_OBJ_ID full_path)
{
    int      error = KOS_SUCCESS;
    unsigned i;
    unsigned len;

    assert(GET_OBJ_TYPE(full_path) == OBJ_STRING);

    len = KOS_get_string_length(full_path);
    for (i = len; i > 0; i--) {
        const unsigned c = KOS_string_get_char_code(frame, full_path, (int)i - 1);
        if (c == ~0U)
            TRY(KOS_ERROR_EXCEPTION);
        if (c == '/' || c == '\\')
            break;
    }

    if (i == len)
        i = 0;

_error:
    if (error)
        return KOS_BADPTR;
    return KOS_string_slice(frame, full_path, i, len);
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
            KOS_new_object_with_prototype(frame, ctx->generator_end_prototype);

    if ( ! IS_BAD_PTR(exception))
        KOS_raise_exception(frame, exception);
}

int KOS_get_integer(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int64_t   *ret)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj_id));

    if (IS_SMALL_INT(obj_id))
        *ret = GET_SMALL_INT(obj_id);

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            *ret = OBJPTR(INTEGER, obj_id)->value;
            break;

        case OBJ_FLOAT: {
            const double number = OBJPTR(FLOAT, obj_id)->value;
            if (number <= -9223372036854775808.0 || number >= 9223372036854775808.0) {
                KOS_raise_exception_cstring(frame, str_err_number_out_of_range);
                error = KOS_ERROR_EXCEPTION;
            }
            else
                *ret = (int64_t)floor(number);
            break;

        }

        default:
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
            error = KOS_ERROR_EXCEPTION;
            break;
    }

    return error;
}

struct _KOS_PROTOTYPE_ITEM {
    KOS_ATOMIC(KOS_OBJ_ID) prototype;
    KOS_ATOMIC(void *)     id;
    KOS_ATOMIC(uint32_t)   hash;
    uint32_t               _align;
};

typedef struct _KOS_PROTOTYPE_ITEM KOS_PROTO_ITEM;

struct _KOS_PROTOTYPES {
    uint32_t       capacity;
    KOS_PROTO_ITEM items[1];
};

typedef struct _KOS_PROTOTYPES *KOS_PROTOTYPES;

static uint32_t _calc_proto_id_hash(uintptr_t id)
{
    /* djb2a algorithm */
    uint32_t hash = 5381;

    do {
        hash = (hash * 33U) ^ (uint32_t)(id & 0xFFU);
        id >>= 8;
    }
    while (id);

    return hash;
}

KOS_OBJ_ID KOS_gen_prototype(KOS_FRAME   frame,
                             const void *ptr)
{
    const uintptr_t id         = (uintptr_t)ptr;
    KOS_OBJ_ID      ret        = KOS_BADPTR;
    const uint32_t  hash       = _calc_proto_id_hash(id);
    KOS_PROTOTYPES  prototypes;
    KOS_CONTEXT    *ctx        = KOS_context_from_frame(frame);

    prototypes = (KOS_PROTOTYPES)KOS_atomic_read_ptr(ctx->prototypes);

    for (;;) {

        uint32_t        count    = 0;
        uint32_t        capacity = 64U; /* first time this gets multiplied by 2 */
        uint32_t        mask     = 0;
        uint32_t        idx      = 0;
        KOS_PROTO_ITEM *items    = 0;
        KOS_PROTO_ITEM *cur_item = 0;
        uintptr_t       cur_id   = 0;

        if (prototypes) {
            capacity = prototypes->capacity;
            mask     = prototypes->capacity - 1;
            idx      = hash;
            count    = KOS_MAX_PROP_REPROBES;
            items    = prototypes->items;
        }

        while (count) {
            cur_item = &items[idx & mask];
            cur_id   = (uintptr_t)KOS_atomic_read_ptr(cur_item->id);

            if (id == cur_id || ! cur_id)
                break;

            ++idx;
            --count;
        }

        if (count) {

            if (cur_id == id) {
                ret = (KOS_OBJ_ID)KOS_atomic_read_ptr(cur_item->prototype);
                break;
            }

            assert( ! cur_id);

            _KOS_spin_lock(&ctx->prototypes_lock);

            if (prototypes == (KOS_PROTOTYPES)KOS_atomic_read_ptr(ctx->prototypes)) {

                ret = KOS_new_object(frame);

                if ( ! IS_BAD_PTR(ret)) {
                    KOS_atomic_write_ptr(cur_item->prototype, ret);
                    KOS_atomic_write_u32(cur_item->hash,      hash);
                    KOS_atomic_write_ptr(cur_item->id,        (void *)id);
                }

                _KOS_spin_unlock(&ctx->prototypes_lock);
                break;
            }

            _KOS_spin_unlock(&ctx->prototypes_lock);
        }
        else {

            const uint32_t  new_capacity   = capacity * 2U;
            const uint32_t  new_mask       = new_capacity - 1;
            KOS_PROTO_ITEM *end            = items + capacity;
            KOS_PROTOTYPES  new_prototypes = (KOS_PROTOTYPES)_KOS_malloc(
                    sizeof(struct _KOS_PROTOTYPES) +
                    sizeof(struct _KOS_PROTOTYPE_ITEM) * (new_capacity - 1));
            KOS_PROTO_ITEM *new_items      = new_prototypes->items;

            if ( ! new_prototypes) {
                KOS_raise_exception_cstring(frame, str_err_out_of_memory);
                break;
            }

            new_prototypes->capacity = new_capacity;

            memset(new_items, 0, sizeof(struct _KOS_PROTOTYPE_ITEM) * new_capacity);

            _KOS_spin_lock(&ctx->prototypes_lock);

            if (prototypes)
                for ( ; items < end; ++items) {
                    void *const cid = KOS_atomic_read_ptr(items->id);

                    if ( ! cid)
                        continue;

                    idx = items->hash;

                    do
                        cur_item = &new_items[idx++ & new_mask];
                    while (KOS_atomic_read_ptr(cur_item->id));

                    KOS_atomic_write_ptr(cur_item->prototype, KOS_atomic_read_ptr(items->prototype));
                    KOS_atomic_write_u32(cur_item->hash,      KOS_atomic_read_u32(items->hash));
                    KOS_atomic_write_ptr(cur_item->id,        cid);
                }

            assert(KOS_atomic_read_ptr(ctx->prototypes) == prototypes);

            KOS_atomic_write_ptr(ctx->prototypes, (void *)new_prototypes);

            /* TODO delay this, it causes a race */
            _KOS_free(prototypes);

            prototypes = new_prototypes;

            _KOS_spin_unlock(&ctx->prototypes_lock);
        }
    }

    return ret;
}

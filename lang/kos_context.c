/*
 * Copyright (c) 2014-2016 Chris Dragan
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
#include "kos_file.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_threads.h"
#include "kos_try.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_VERSION_STR
#define CONFIG_VERSION_STR "0"
#endif

#ifdef _WIN32
#define CONFIG_PATH_SEP_STR ";"
#define CONFIG_PATH_SEP_CHR ';'
#else
#define CONFIG_PATH_SEP_STR ":"
#define CONFIG_PATH_SEP_CHR ':'
#endif

#ifndef CONFIG_MODULE_PATH
#if defined(__linux__)
#define CONFIG_MODULE_PATH "/usr/lib/kos/" CONFIG_VERSION_STR CONFIG_PATH_SEP_STR \
                           "/usr/local/lib/kos/" CONFIG_VERSION_STR
#elif defined(__APPLE__)
#define CONFIG_MODULE_PATH "/System/Library/Frameworks/Kos.framework/" CONFIG_VERSION_STR CONFIG_PATH_SEP_STR \
                           "/Library/Kos/" CONFIG_VERSION_STR
#endif
#endif

static KOS_ASCII_STRING(str_backtrace,               "backtrace");
static KOS_ASCII_STRING(str_builtin,                 "<builtin>");
static KOS_ASCII_STRING(str_err_not_array,           "object is not an array");
static KOS_ASCII_STRING(str_err_number_out_of_range, "number out of range");
static KOS_ASCII_STRING(str_err_unsup_operand_types, "unsupported operand types");
static KOS_ASCII_STRING(str_file,                    "file");
static KOS_ASCII_STRING(str_format_exception,        "Exception: ");
static KOS_ASCII_STRING(str_format_hash,             "  #");
static KOS_ASCII_STRING(str_format_line,             ":");
static KOS_ASCII_STRING(str_format_function,         " in '");
static KOS_ASCII_STRING(str_format_module,           "' in ");
static KOS_ASCII_STRING(str_format_offset,           "  ");
static KOS_ASCII_STRING(str_format_question_marks,   "???");
static KOS_ASCII_STRING(str_function,                "function");
static KOS_ASCII_STRING(str_line,                    "line");
static KOS_ASCII_STRING(str_module,                  "module");
static KOS_ASCII_STRING(str_offset,                  "offset");
static KOS_ASCII_STRING(str_value,                   "value");

void _KOS_init_prototype(KOS_OBJECT *obj, KOS_OBJECT *base_proto)
{
    obj->type      = OBJ_OBJECT;
    obj->prototype = TO_OBJPTR(base_proto);
    obj->priv      = 0;

    _KOS_init_properties((struct _KOS_PROPERTIES *)obj);
}

static int _add_multiple_paths(KOS_CONTEXT *ctx, struct _KOS_VECTOR *cpaths)
{
    int   error = KOS_SUCCESS;
    char *buf   = cpaths->buffer;

    while ( ! error) {
        char *end = strchr(buf, CONFIG_PATH_SEP_CHR);

        if (end)
            *end = '\0';

        error = KOS_context_add_path(ctx, buf);

        if (end)
            buf = end + 1;
        else
            break;
    }

    return error;
}

static int _init_search_paths(KOS_CONTEXT *ctx)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpaths;

    _KOS_vector_init(&cpaths);

    if (_KOS_get_env("KOSPATH", &cpaths) == KOS_SUCCESS)
        error = _add_multiple_paths(ctx, &cpaths);

#ifdef CONFIG_MODULE_PATH
    if ( ! error) {
        static const char module_path[] = CONFIG_MODULE_PATH;
        error = _KOS_vector_resize(&cpaths, sizeof(module_path));
        if ( ! error) {
            memcpy(cpaths.buffer, module_path, sizeof(module_path));
            error = _add_multiple_paths(ctx, &cpaths);
        }
    }
#endif

    _KOS_vector_destroy(&cpaths);

    return error;
}

int KOS_context_init(KOS_CONTEXT *ctx)
{
    int error;
    int alloc_ok;
    int tls_ok = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->stack_frame = TO_OBJPTR(&ctx->root_stack_frame);

    _KOS_stack_frame_init(&ctx->root_stack_frame);

    _KOS_init_prototype(&ctx->object_prototype,   0);
    _KOS_init_prototype(&ctx->number_prototype,   &ctx->object_prototype);
    _KOS_init_prototype(&ctx->integer_prototype,  &ctx->number_prototype);
    _KOS_init_prototype(&ctx->float_prototype,    &ctx->number_prototype);
    _KOS_init_prototype(&ctx->string_prototype,   &ctx->object_prototype);
    _KOS_init_prototype(&ctx->boolean_prototype,  &ctx->object_prototype);
    _KOS_init_prototype(&ctx->void_prototype,     &ctx->object_prototype);
    _KOS_init_prototype(&ctx->array_prototype,    &ctx->object_prototype);
    _KOS_init_prototype(&ctx->buffer_prototype,   &ctx->object_prototype);
    _KOS_init_prototype(&ctx->function_prototype, &ctx->object_prototype);

    error = _KOS_alloc_init(ctx);

    alloc_ok = ! error;

    if (!error) {
        error  = _KOS_tls_create(&ctx->exception_key);
        tls_ok = ! error;
    }

    if (!error) {
        _KOS_init_object(&ctx->module_names, TO_OBJPTR(&ctx->object_prototype));
        error = _KOS_init_array(ctx, &ctx->modules, 0);
    }

    if (!error)
        error = _KOS_init_array(ctx, &ctx->module_search_paths, 0);

    if (!error)
        error = _init_search_paths(ctx);

    if (error && tls_ok)
        _KOS_tls_destroy(ctx->exception_key);

    if (error && alloc_ok)
        _KOS_alloc_destroy(ctx);

    return error;
}

void KOS_context_destroy(KOS_CONTEXT *ctx)
{
    uint32_t i;
    uint32_t num_modules = KOS_get_array_size(TO_OBJPTR(&ctx->modules));

    for (i = 0; i < num_modules; i++) {
        KOS_OBJ_PTR module_obj = KOS_array_read(ctx, TO_OBJPTR(&ctx->modules), (int)i);
        assert(!IS_BAD_PTR(module_obj));
        if (IS_BAD_PTR(module_obj))
            KOS_clear_exception(ctx);
        else {
            if ((OBJPTR(KOS_MODULE, module_obj)->flags & KOS_MODULE_OWN_BYTECODE))
                _KOS_free((void *)OBJPTR(KOS_MODULE, module_obj)->bytecode);
            if ((OBJPTR(KOS_MODULE, module_obj)->flags & KOS_MODULE_OWN_LINE_ADDRS))
                _KOS_free((void *)OBJPTR(KOS_MODULE, module_obj)->line_addrs);
            if ((OBJPTR(KOS_MODULE, module_obj)->flags & KOS_MODULE_OWN_FUNC_ADDRS))
                _KOS_free((void *)OBJPTR(KOS_MODULE, module_obj)->func_addrs);
        }
    }

    if (ctx->prototypes)
        _KOS_free(ctx->prototypes);

    _KOS_tls_destroy(ctx->exception_key);

    _KOS_alloc_destroy(ctx);
    memset(ctx, 0, sizeof(*ctx));
}

int KOS_context_add_path(KOS_CONTEXT *ctx, const char *module_search_path)
{
    int         error;
    uint32_t    len;
    KOS_OBJ_PTR path_str;

    path_str = KOS_new_cstring(ctx, module_search_path);
    if (IS_BAD_PTR(path_str))
        TRY(KOS_ERROR_OUT_OF_MEMORY);

    len = KOS_get_array_size(TO_OBJPTR(&ctx->module_search_paths));
    TRY(KOS_array_resize(ctx, TO_OBJPTR(&ctx->module_search_paths), len+1));
    TRY(KOS_array_write(ctx, TO_OBJPTR(&ctx->module_search_paths), (int)len, path_str));

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

int KOS_context_register_builtin(KOS_CONTEXT     *ctx,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init)
{
    int error = KOS_SUCCESS;

    KOS_OBJ_PTR module_name = KOS_new_cstring(ctx, module);
    if (IS_BAD_PTR(module_name))
        error = KOS_ERROR_EXCEPTION;
    else {

        /* TODO alloc from context's one-way buffer */
        struct _KOS_MODULE_INIT *mod_init = (struct _KOS_MODULE_INIT *)
                                            _KOS_alloc_buffer(ctx, sizeof(struct _KOS_MODULE_INIT));

        mod_init->name = module_name;
        mod_init->init = init;

        _KOS_red_black_insert(&ctx->module_inits, &mod_init->rb_tree_node, _module_init_compare);
    }

    return error;
}

int KOS_context_load_module(KOS_CONTEXT *ctx, const char *path)
{
    int         idx;
    KOS_OBJ_PTR module = _KOS_module_import(ctx, path, (unsigned)strlen(path), KOS_MODULE_MANDATORY, &idx);

    return IS_BAD_PTR(module) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

void KOS_raise_exception(KOS_CONTEXT *ctx,
                         KOS_OBJ_PTR  obj)
{
    void* cur_exception = _KOS_tls_get(ctx->exception_key);

    /* Nested exceptions are not allowed. */
    /* This can only happen if there is a bug and an exception has been ignored. */
    assert(!cur_exception);

    if (!cur_exception)
        _KOS_tls_set(ctx->exception_key, OBJPTR(void, obj));
}

void KOS_clear_exception(KOS_CONTEXT *ctx)
{
    _KOS_tls_set(ctx->exception_key, 0);
}

int KOS_is_exception_pending(KOS_CONTEXT *ctx)
{
    return !! _KOS_tls_get(ctx->exception_key);
}

KOS_OBJ_PTR KOS_get_exception(KOS_CONTEXT *ctx)
{
    return TO_OBJPTR(_KOS_tls_get(ctx->exception_key));
}

void _KOS_wrap_exception(KOS_CONTEXT     *ctx,
                         KOS_STACK_FRAME *stack_frame)
{
    int              error;
    unsigned         depth;
    KOS_STACK_FRAME *frame;
    KOS_OBJ_PTR      exception;
    KOS_OBJ_PTR      backtrace;
    KOS_OBJ_PTR      thrown_object = KOS_get_exception(ctx);

    assert(!IS_BAD_PTR(thrown_object));

    if ( ! IS_BAD_PTR(thrown_object) && ! IS_SMALL_INT(thrown_object) &&
        GET_OBJ_TYPE(thrown_object) == OBJ_OBJECT) {

        KOS_OBJ_PTR obj = KOS_get_property(ctx, thrown_object, TO_OBJPTR(&str_backtrace));
        if ( ! IS_BAD_PTR(obj)) {
            obj = KOS_get_property(ctx, thrown_object, TO_OBJPTR(&str_value));

            /* If both value and backtrace properties exist, restore the
               original thrown object. */
            if ( ! IS_BAD_PTR(obj))
                TRY(KOS_SUCCESS_RETURN);
        }

        KOS_clear_exception(ctx);
    }

    assert(TO_OBJPTR(stack_frame) == ctx->stack_frame);

    exception = KOS_new_object(ctx);
    if (IS_BAD_PTR(exception))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_set_property(ctx, exception, TO_OBJPTR(&str_value), thrown_object));

    depth = 0;
    frame = stack_frame;
    while (frame && frame != &ctx->root_stack_frame) {
        ++depth;
        frame = OBJPTR(KOS_STACK_FRAME, frame->parent);
    }

    backtrace = KOS_new_array(ctx, depth);
    if (IS_BAD_PTR(backtrace))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_array_resize(ctx, backtrace, depth));

    TRY(KOS_set_property(ctx, exception, TO_OBJPTR(&str_backtrace), backtrace));

    depth = 0;
    frame = stack_frame;
    while (frame && frame != &ctx->root_stack_frame) {
        KOS_MODULE    *module      = OBJPTR(KOS_MODULE, frame->module);
        const unsigned line        = KOS_module_addr_to_line(module, frame->instr_offs);
        KOS_OBJ_PTR    module_name = TO_OBJPTR(&str_builtin);
        KOS_OBJ_PTR    module_path = TO_OBJPTR(&str_builtin);
        KOS_OBJ_PTR    func_name   = KOS_module_addr_to_func_name(module, frame->instr_offs);

        KOS_OBJ_PTR frame_desc = KOS_new_object(ctx);
        if (IS_BAD_PTR(frame_desc))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_BAD_PTR(func_name))
            func_name = TO_OBJPTR(&str_builtin); /* TODO add builtin function name */

        assert(depth < KOS_get_array_size(backtrace));
        TRY(KOS_array_write(ctx, backtrace, (int)depth, frame_desc));

        /* TODO use builtin function pointer for offset */

        if (module) {
            module_name = module->name;
            module_path = module->path;
        }

        TRY(KOS_set_property(ctx, frame_desc, TO_OBJPTR(&str_module),   module_name));
        TRY(KOS_set_property(ctx, frame_desc, TO_OBJPTR(&str_file),     module_path));
        TRY(KOS_set_property(ctx, frame_desc, TO_OBJPTR(&str_line),     TO_SMALL_INT((int)line)));
        TRY(KOS_set_property(ctx, frame_desc, TO_OBJPTR(&str_offset),   TO_SMALL_INT((int)frame->instr_offs)));
        TRY(KOS_set_property(ctx, frame_desc, TO_OBJPTR(&str_function), func_name));

        ++depth;
        frame = OBJPTR(KOS_STACK_FRAME, frame->parent);
    }

    _KOS_tls_set(ctx->exception_key, OBJPTR(void, exception));

_error:
    if (error)
        _KOS_tls_set(ctx->exception_key, OBJPTR(void, thrown_object));
}

KOS_OBJ_PTR KOS_get_file_name(KOS_CONTEXT *ctx,
                              KOS_OBJ_PTR  full_path)
{
    int      error = KOS_SUCCESS;
    unsigned i;
    unsigned len;

    assert( ! IS_BAD_PTR(full_path));
    assert( ! IS_SMALL_INT(full_path));
    assert(IS_STRING_OBJ(full_path));

    len = KOS_get_string_length(full_path);
    for (i = len; i > 0; i--) {
        const unsigned c = KOS_string_get_char_code(ctx, full_path, (int)i - 1);
        if (c == ~0U)
            TRY(KOS_ERROR_EXCEPTION);
        if (c == '/' || c == '\\')
            break;
    }

    if (i == len)
        i = 0;

_error:
    if (error)
        return TO_OBJPTR(0);
    return KOS_string_slice(ctx, full_path, i, len);
}

KOS_OBJ_PTR KOS_format_exception(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  exception)
{
    int         error;
    unsigned    i;
    unsigned    depth;
    KOS_OBJ_PTR value;
    KOS_OBJ_PTR backtrace;
    KOS_OBJ_PTR array = TO_OBJPTR(0);
    KOS_OBJ_PTR str;

    value = KOS_get_property(ctx, exception, TO_OBJPTR(&str_value));
    if (IS_BAD_PTR(value))
        TRY(KOS_ERROR_EXCEPTION);

    backtrace = KOS_get_property(ctx, exception, TO_OBJPTR(&str_backtrace));
    if (IS_BAD_PTR(backtrace))
        TRY(KOS_ERROR_EXCEPTION);

    if (IS_BAD_PTR(backtrace) || IS_SMALL_INT(backtrace) || GET_OBJ_TYPE(backtrace) != OBJ_ARRAY) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_array));
        TRY(KOS_ERROR_EXCEPTION);
    }

    depth = KOS_get_array_size(backtrace);
    array = KOS_new_array(ctx, 1+depth);
    if (IS_BAD_PTR(array))
        TRY(KOS_ERROR_EXCEPTION);

    str = KOS_object_to_string(ctx, value);
    if (IS_BAD_PTR(array))
        TRY(KOS_ERROR_EXCEPTION);

    str = KOS_string_add(ctx, TO_OBJPTR(&str_format_exception), str);
    if (IS_BAD_PTR(array))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_array_write(ctx, array, 0, str));

    for (i = 0; i < depth; i++) {
        KOS_OBJ_PTR             frame_desc = KOS_array_read(ctx, backtrace, (int)i);
        KOS_ATOMIC(KOS_OBJ_PTR) parts[10];

        if (IS_BAD_PTR(frame_desc))
            TRY(KOS_ERROR_EXCEPTION);

        parts[0] = TO_OBJPTR(&str_format_hash);

        parts[1] = KOS_object_to_string(ctx, TO_SMALL_INT((int)i));
        if (IS_BAD_PTR(parts[1]))
            TRY(KOS_ERROR_EXCEPTION);

        parts[2] = TO_OBJPTR(&str_format_offset);

        str = KOS_get_property(ctx, frame_desc, TO_OBJPTR(&str_offset));
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);
        if (IS_SMALL_INT(str)) {
            char cbuf[16];
            snprintf(cbuf, sizeof(cbuf), "0x%X", (unsigned)GET_SMALL_INT(str));
            parts[3] = KOS_new_cstring(ctx, cbuf);
            if (IS_BAD_PTR(parts[3]))
                TRY(KOS_ERROR_EXCEPTION);
        }
        else
            parts[3] = TO_OBJPTR(&str_format_question_marks);

        parts[4] = TO_OBJPTR(&str_format_function);

        str = KOS_get_property(ctx, frame_desc, TO_OBJPTR(&str_function));
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);
        parts[5] = str;

        parts[6] = TO_OBJPTR(&str_format_module);

        str = KOS_get_property(ctx, frame_desc, TO_OBJPTR(&str_file));
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);
        str = KOS_get_file_name(ctx, str);
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);
        parts[7] = str;

        parts[8] = TO_OBJPTR(&str_format_line);

        str = KOS_get_property(ctx, frame_desc, TO_OBJPTR(&str_line));
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);
        parts[9] = KOS_object_to_string(ctx, str);
        if (IS_BAD_PTR(parts[9]))
            TRY(KOS_ERROR_EXCEPTION);

        str = KOS_string_add_many(ctx, parts, sizeof(parts)/sizeof(parts[0]));
        if (IS_BAD_PTR(str))
            TRY(KOS_ERROR_EXCEPTION);

        TRY(KOS_array_write(ctx, array, 1+(int)i, str));
    }

_error:
    if (error)
        array = TO_OBJPTR(0);

    return array;
}

int KOS_get_integer(KOS_CONTEXT *ctx,
                    KOS_OBJ_PTR  obj,
                    int64_t     *ret)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj));

    if (IS_SMALL_INT(obj))
        *ret = GET_SMALL_INT(obj);

    else switch (GET_OBJ_TYPE(obj)) {

        case OBJ_INTEGER:
            *ret = OBJPTR(KOS_INTEGER, obj)->number;
            break;

        case OBJ_FLOAT: {
            const double number = OBJPTR(KOS_FLOAT, obj)->number;
            if (number < 0) {
                if (number <= -9223372036854775808.0) {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_number_out_of_range));
                    error = KOS_ERROR_EXCEPTION;
                }
                else
                    *ret = (int64_t)ceil(number);
            }
            else {
                if (number >= 9223372036854775808.0) {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_number_out_of_range));
                    error = KOS_ERROR_EXCEPTION;
                }
                else
                    *ret = (int64_t)floor(number);
            }
            break;
        }

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

struct _KOS_PROTOTYPE_ITEM {
    KOS_ATOMIC(KOS_OBJ_PTR) prototype;
    KOS_ATOMIC(void *)      id;
    KOS_ATOMIC(uint32_t)    hash;
    uint32_t                _align;
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

KOS_OBJ_PTR KOS_gen_prototype(KOS_CONTEXT *ctx,
                              const void  *ptr)
{
    const uintptr_t id         = (uintptr_t)ptr;
    KOS_OBJ_PTR     ret        = TO_OBJPTR(0);
    const uint32_t  hash       = _calc_proto_id_hash(id);
    KOS_PROTOTYPES  prototypes = (KOS_PROTOTYPES)KOS_atomic_read_ptr(ctx->prototypes);

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
                ret = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->prototype);
                break;
            }

            assert( ! cur_id);

            _KOS_spin_lock(&ctx->prototypes_lock);

            if (prototypes == (KOS_PROTOTYPES)KOS_atomic_read_ptr(ctx->prototypes)) {

                ret = KOS_new_object(ctx);

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

            if ( ! new_prototypes)
                break;

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

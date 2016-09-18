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

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../lang/kos_memory.h"
#include "../lang/kos_misc.h"
#include "../lang/kos_object_internal.h"
#include "../lang/kos_try.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>

static KOS_ASCII_STRING(str_builtin,                       "<builtin>");
static KOS_ASCII_STRING(str_err_bad_obj_iter,              "invalid invocation of object iterator");
static KOS_ASCII_STRING(str_err_bad_number,                "number parse failed");
static KOS_ASCII_STRING(str_err_bad_pack_value,            "invalid value type for pack format");
static KOS_ASCII_STRING(str_err_cannot_convert_to_array,   "unsupported type passed to array constructor");
static KOS_ASCII_STRING(str_err_cannot_override_prototype, "cannot override prototype");
static KOS_ASCII_STRING(str_err_invalid_array_size,        "array size out of range");
static KOS_ASCII_STRING(str_err_invalid_byte_value,        "buffer element value out of range");
static KOS_ASCII_STRING(str_err_invalid_buffer_size,       "buffer size out of range");
static KOS_ASCII_STRING(str_err_invalid_pack_format,       "invalid pack format");
static KOS_ASCII_STRING(str_err_not_array,                 "object is not an array");
static KOS_ASCII_STRING(str_err_not_buffer,                "object is not a buffer");
static KOS_ASCII_STRING(str_err_not_enough_pack_values,    "insufficient number of packed values");
static KOS_ASCII_STRING(str_err_not_function,              "object is not a function");
static KOS_ASCII_STRING(str_err_not_string,                "object is not a string");
static KOS_ASCII_STRING(str_err_unpack_buf_too_short,      "unpacked buffer too short");
static KOS_ASCII_STRING(str_err_unsup_operand_types,       "unsupported operand types");
static KOS_ASCII_STRING(str_prototype,                     "prototype");

#define TRY_CREATE_CONSTRUCTOR(name)                   \
do {                                                   \
    static KOS_ASCII_STRING(str_name, #name);          \
    TRY(_create_constructor(module->context,           \
                            TO_OBJPTR(module),         \
                            TO_OBJPTR(&str_name),      \
                            _##name##_constructor,     \
                            _get_##name##_prototype)); \
} while (0)

#define PROTO(type) (TO_OBJPTR(&module->context->type##_prototype))

static KOS_OBJ_PTR _print(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  this_obj,
                          KOS_OBJ_PTR  args_obj)
{
    uint32_t           len;
    uint32_t           i;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    len = KOS_get_array_size(args_obj);

    for (i = 0; i < len; i++) {
        KOS_OBJ_PTR obj = KOS_array_read(ctx, args_obj, (int)i);
        if (!IS_BAD_PTR(obj)) {
            if (i > 0)
                printf(" ");
            if (IS_SMALL_INT(obj)) {
                const int64_t value = GET_SMALL_INT(obj);
                printf("%" PRId64, value);
            }
            else switch (GET_OBJ_TYPE(obj)) {

                case OBJ_INTEGER:
                    printf("%" PRId64, OBJPTR(KOS_INTEGER, obj)->number);
                    break;

                case OBJ_FLOAT:
                    printf("%f", OBJPTR(KOS_FLOAT, obj)->number);
                    break;

                case OBJ_STRING_8:
                    /* fall through */
                case OBJ_STRING_16:
                    /* fall through */
                case OBJ_STRING_32: {
                    if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, obj, &cstr)) {
                        assert(cstr.size > 0);
                        printf("%.*s", (int)cstr.size-1, cstr.buffer);
                    }
                    else
                        i = len;
                    break;
                }

                case OBJ_VOID:
                    printf("void");
                    break;

                case OBJ_BOOLEAN:
                    if (KOS_get_bool(obj))
                        printf("true");
                    else
                        printf("false");
                    break;

                case OBJ_ARRAY:
                    /* TODO */
                    printf("<array>");
                    break;

                case OBJ_BUFFER:
                    /* TODO */
                    printf("<buffer>");
                    break;

                case OBJ_OBJECT:
                    /* TODO */
                    printf("<object>");
                    break;

                case OBJ_FUNCTION:
                    /* TODO print module, name, line, offset */
                    printf("<function>");
                    break;

                default:
                    assert(0);
                    break;
            }
        }
    }

    _KOS_vector_destroy(&cstr);

    return KOS_VOID;
}

static KOS_OBJ_PTR _object_iterator(KOS_CONTEXT               *ctx,
                                    KOS_OBJ_PTR                regs_obj,
                                    KOS_OBJ_PTR                args_obj,
                                    enum KOS_OBJECT_WALK_DEPTH deep)
{
    int                  error = KOS_SUCCESS;
    KOS_OBJ_PTR          walk;
    KOS_OBJECT_WALK_ELEM elem = { TO_OBJPTR(0), TO_OBJPTR(0) };

    assert( ! IS_BAD_PTR(regs_obj));
    if (IS_SMALL_INT(regs_obj) || GET_OBJ_TYPE(regs_obj) != OBJ_ARRAY ||
            KOS_get_array_size(regs_obj) == 0) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_obj_iter));
        TRY(KOS_ERROR_EXCEPTION);
    }

    walk = KOS_array_read(ctx, regs_obj, 0);
    assert( ! IS_BAD_PTR(walk));

    if (IS_SMALL_INT(walk) || GET_OBJ_TYPE(walk) != OBJ_OBJECT_WALK) {
        walk = KOS_new_object_walk(ctx, walk, deep);
        if (IS_BAD_PTR(walk))
            TRY(KOS_ERROR_EXCEPTION);

        TRY(KOS_array_write(ctx, regs_obj, 0, walk));
    }

    elem = KOS_object_walk(ctx, OBJPTR(KOS_OBJECT_WALK, walk));

_error:
    return elem.key;
}

static KOS_OBJ_PTR _shallow(KOS_CONTEXT *ctx,
                            KOS_OBJ_PTR  regs_obj,
                            KOS_OBJ_PTR  args_obj)
{
    return _object_iterator(ctx, regs_obj, args_obj, KOS_SHALLOW);
}

static KOS_OBJ_PTR _deep(KOS_CONTEXT *ctx,
                         KOS_OBJ_PTR  regs_obj,
                         KOS_OBJ_PTR  args_obj)
{
    return _object_iterator(ctx, regs_obj, args_obj, KOS_DEEP);
}

static KOS_OBJ_PTR _iterator(KOS_CONTEXT *ctx,
                             KOS_OBJ_PTR  regs_obj,
                             KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(0);
}

static KOS_OBJ_PTR _set_prototype(KOS_CONTEXT *ctx,
                                  KOS_OBJ_PTR  this_obj,
                                  KOS_OBJ_PTR  args_obj)
{
    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_override_prototype));
    return TO_OBJPTR(0);
}

static int _create_constructor(KOS_CONTEXT         *ctx,
                               KOS_OBJ_PTR          module_obj,
                               KOS_OBJ_PTR          str_name,
                               KOS_FUNCTION_HANDLER constructor,
                               KOS_FUNCTION_HANDLER get_prototype)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR func_obj = KOS_new_function(ctx, KOS_VOID);

    if (IS_BAD_PTR(func_obj)) {
        assert(KOS_is_exception_pending(ctx));
        error = KOS_ERROR_EXCEPTION;
    }
    else {
        OBJPTR(KOS_FUNCTION, func_obj)->handler = constructor;
        OBJPTR(KOS_FUNCTION, func_obj)->module  = module_obj;

        error = KOS_module_add_global(OBJPTR(KOS_MODULE, module_obj),
                                      str_name,
                                      func_obj,
                                      0);

        if ( ! error)
            error = KOS_set_builtin_dynamic_property(ctx,
                                                     func_obj,
                                                     TO_OBJPTR(&str_prototype),
                                                     get_prototype,
                                                     _set_prototype);
    }

    return error;
}

static KOS_OBJ_PTR _number_constructor(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  this_obj,
                                       KOS_OBJ_PTR  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        KOS_OBJ_PTR arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_SMALL_INT(arg))
            ret = arg;
        else switch (GET_OBJ_TYPE(arg)) {

            case OBJ_INTEGER:
                /* fall through */
            case OBJ_FLOAT:
                ret = arg;
                break;

            case OBJ_STRING_8:
                /* fall through */
            case OBJ_STRING_16:
                /* fall through */
            case OBJ_STRING_32: {
                struct _KOS_VECTOR cstr;
                _KOS_vector_init(&cstr);

                if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, arg, &cstr)) {

                    const char *begin = cstr.buffer;
                    const char *end   = begin + cstr.size - 1;
                    const char *s;

                    assert(begin <= end);

                    for (s = begin; s < end; s++) {
                        const char c = *s;
                        if (c == '.' || c == 'e' || c == 'E')
                            break;
                    }

                    if (s < end) {

                        double    value;
                        const int error = _KOS_parse_double(begin, end, &value);

                        if (error)
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_number));
                        else
                            ret = KOS_new_float(ctx, value);
                    }
                    else {

                        int64_t   value;
                        const int error = _KOS_parse_int(begin, end, &value);

                        if (error)
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_number));
                        else
                            ret = KOS_new_int(ctx, value);
                    }
                }

                _KOS_vector_destroy(&cstr);
                break;
            }

            default:
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                break;
        }
    }

    return ret;
}

static KOS_OBJ_PTR _get_number_prototype(KOS_CONTEXT *ctx,
                                         KOS_OBJ_PTR  this_obj,
                                         KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->number_prototype);
}

static KOS_OBJ_PTR _integer_constructor(KOS_CONTEXT *ctx,
                                        KOS_OBJ_PTR  this_obj,
                                        KOS_OBJ_PTR  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        int64_t     value;
        KOS_OBJ_PTR arg = KOS_array_read(ctx, args_obj, 0);

        if ( ! IS_BAD_PTR(arg)) {
            if (IS_NUMERIC_OBJ(arg)) {
                if (KOS_get_integer(ctx, arg, &value) == KOS_SUCCESS)
                    ret = KOS_new_int(ctx, value);
            }
            else if (IS_STRING_OBJ(arg)) {

                struct _KOS_VECTOR cstr;
                _KOS_vector_init(&cstr);

                if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, arg, &cstr)) {

                    const char *begin = cstr.buffer;
                    const char *end   = begin + cstr.size - 1;
                    int         error;

                    assert(begin <= end);

                    error = _KOS_parse_int(begin, end, &value);

                    if (error)
                        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_number));
                    else
                        ret = KOS_new_int(ctx, value);
                }

                _KOS_vector_destroy(&cstr);
            }
            else
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        }
    }

    return ret;
}

static KOS_OBJ_PTR _get_integer_prototype(KOS_CONTEXT *ctx,
                                          KOS_OBJ_PTR  this_obj,
                                          KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->integer_prototype);
}

static KOS_OBJ_PTR _float_constructor(KOS_CONTEXT *ctx,
                                      KOS_OBJ_PTR  this_obj,
                                      KOS_OBJ_PTR  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = KOS_new_float(ctx, 0);
    else {
        KOS_OBJ_PTR arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_BAD_PTR(arg)) {
        }
        else if (IS_SMALL_INT(arg))
            ret = KOS_new_float(ctx, (double)GET_SMALL_INT(arg));
        else switch (GET_OBJ_TYPE(arg)) {

            case OBJ_INTEGER: {
                ret = KOS_new_float(ctx, (double)OBJPTR(KOS_INTEGER, arg)->number);
                break;
            }

            case OBJ_FLOAT: {
                ret = arg;
                break;
            }

            case OBJ_STRING_8:
                /* fall through */
            case OBJ_STRING_16:
                /* fall through */
            case OBJ_STRING_32: {

                struct _KOS_VECTOR cstr;
                _KOS_vector_init(&cstr);

                if (KOS_string_to_cstr_vec(ctx, arg, &cstr) == KOS_SUCCESS) {

                    const char *begin = cstr.buffer;
                    const char *end   = begin + cstr.size - 1;
                    double      value;
                    int         error;

                    assert(begin <= end);

                    error = _KOS_parse_double(begin, end, &value);

                    if (error)
                        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_number));
                    else
                        ret = KOS_new_float(ctx, value);
                }

                _KOS_vector_destroy(&cstr);
                break;
            }

            default:
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                break;
        }
    }

    return ret;
}

static KOS_OBJ_PTR _get_float_prototype(KOS_CONTEXT *ctx,
                                        KOS_OBJ_PTR  this_obj,
                                        KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->float_prototype);
}

static KOS_OBJ_PTR _boolean_constructor(KOS_CONTEXT *ctx,
                                        KOS_OBJ_PTR  this_obj,
                                        KOS_OBJ_PTR  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = KOS_FALSE;

    if (num_args > 0) {
        KOS_OBJ_PTR arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_BAD_PTR(arg))
            ret = TO_OBJPTR(0);
        else
            ret = KOS_BOOL(_KOS_is_truthy(arg));
    }

    return ret;
}

static KOS_OBJ_PTR _get_boolean_prototype(KOS_CONTEXT *ctx,
                                          KOS_OBJ_PTR  this_obj,
                                          KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->boolean_prototype);
}

static KOS_OBJ_PTR _void_constructor(KOS_CONTEXT *ctx,
                                     KOS_OBJ_PTR  this_obj,
                                     KOS_OBJ_PTR  args_obj)
{
    return KOS_VOID;
}

static KOS_OBJ_PTR _get_void_prototype(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  this_obj,
                                       KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->void_prototype);
}

static KOS_OBJ_PTR _string_constructor(KOS_CONTEXT *ctx,
                                        KOS_OBJ_PTR this_obj,
                                        KOS_OBJ_PTR args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = KOS_new_string(ctx, 0, 0);

    else if (num_args == 1) {

        KOS_OBJ_PTR obj = KOS_array_read(ctx, args_obj, 0);

        if ( ! IS_BAD_PTR(obj))
            ret = KOS_object_to_string(ctx, obj);
    }
    else {

        uint32_t i;

        for (i=0; i < num_args; i++) {
            KOS_OBJ_PTR obj = KOS_array_read(ctx, args_obj, (int)i);
            if (IS_BAD_PTR(obj))
                break;

            if ( ! IS_STRING_OBJ(obj)) {

                int error;

                obj = KOS_object_to_string(ctx, obj);
                if (IS_BAD_PTR(obj))
                    break;

                error = KOS_array_write(ctx, args_obj, (int)i, obj);
                assert( ! error);
                if (error)
                    break;
            }
        }

        if (i == num_args)
            ret = KOS_string_add_many(ctx, _KOS_get_array_buffer(OBJPTR(KOS_ARRAY, args_obj)), num_args);
    }
    return ret;
}

static KOS_OBJ_PTR _get_string_prototype(KOS_CONTEXT *ctx,
                                         KOS_OBJ_PTR  this_obj,
                                         KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->string_prototype);
}

static KOS_OBJ_PTR _object_constructor(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  this_obj,
                                       KOS_OBJ_PTR  args_obj)
{
    return KOS_new_object(ctx);
}

static KOS_OBJ_PTR _get_object_prototype(KOS_CONTEXT *ctx,
                                         KOS_OBJ_PTR  this_obj,
                                         KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->object_prototype);
}

static KOS_OBJ_PTR _array_constructor(KOS_CONTEXT *ctx,
                                      KOS_OBJ_PTR  this_obj,
                                      KOS_OBJ_PTR  args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_PTR    array    = KOS_new_array(ctx, 0);
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i_arg;

    if (IS_BAD_PTR(array))
        TRY(KOS_ERROR_EXCEPTION);

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const uint32_t cur_size = KOS_get_array_size(array);

        KOS_OBJ_PTR elem = KOS_array_read(ctx, args_obj, (int)i_arg);
        if (IS_BAD_PTR(elem))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(elem)) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_convert_to_array));
            TRY(KOS_ERROR_EXCEPTION);
        }

        switch (GET_OBJ_TYPE(elem)) {

            case OBJ_ARRAY:
                TRY(KOS_array_insert(ctx, array, cur_size, cur_size,
                                     elem, 0, (int32_t)KOS_get_array_size(elem)));
                break;

            case OBJ_STRING_8:
            case OBJ_STRING_16:
            case OBJ_STRING_32: {
                const uint32_t len = KOS_get_string_length(elem);
                uint32_t       i;

                TRY(KOS_array_resize(ctx, array, cur_size + len));

                for (i = 0; i < len; i++) {
                    KOS_OBJ_PTR ch = KOS_string_get_char(ctx, elem, (int)i);
                    if (IS_BAD_PTR(ch))
                        TRY(KOS_ERROR_EXCEPTION);
                    TRY(KOS_array_write(ctx, array, (int)(cur_size + i), ch));
                }
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t size = KOS_get_buffer_size(elem);
                uint32_t       i;
                uint8_t *const buf  = KOS_buffer_data(ctx, elem);

                assert(buf);

                TRY(KOS_array_resize(ctx, array, cur_size + size));

                for (i = 0; i < size; i++) {
                    const KOS_OBJ_PTR byte = TO_SMALL_INT((int)buf[i]);
                    TRY(KOS_array_write(ctx, array, (int)(cur_size + i), byte));
                }
                break;
            }

            case OBJ_FUNCTION: {
                KOS_OBJ_PTR               gen_args;
                enum _KOS_GENERATOR_STATE state = OBJPTR(KOS_FUNCTION, elem)->generator_state;

                if (state != KOS_GEN_READY && state != KOS_GEN_ACTIVE && state != KOS_GEN_DONE) {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_convert_to_array));
                    TRY(KOS_ERROR_EXCEPTION);
                }

                gen_args = KOS_new_array(ctx, 0);
                if (IS_BAD_PTR(gen_args))
                    TRY(KOS_ERROR_EXCEPTION);

                if (state != KOS_GEN_DONE) {
                    for (;;) {
                        KOS_OBJ_PTR ret = KOS_call_function(ctx, elem, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret))
                            break;
                        TRY(KOS_array_push(ctx, array, ret));
                    }
                }
                break;
            }

            case OBJ_OBJECT:
                /* TODO keys */
                break;

            default:
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_convert_to_array));
                error = KOS_ERROR_EXCEPTION;
                goto _error;
        }
    }

_error:
    return error ? TO_OBJPTR(0) : array;
}

static KOS_OBJ_PTR _get_array_prototype(KOS_CONTEXT *ctx,
                                        KOS_OBJ_PTR  this_obj,
                                        KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->array_prototype);
}

static KOS_OBJ_PTR _buffer_constructor(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  this_obj,
                                       KOS_OBJ_PTR  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    int64_t        size     = 0;
    KOS_OBJ_PTR    buffer   = TO_OBJPTR(0);

    /* TODO handle multiple inputs: array of numbers, string, generator */

    if (num_args > 0)
        TRY(KOS_get_integer(ctx, KOS_array_read(ctx, args_obj, 0), &size));

    if (size < 0 || size > UINT_MAX) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_buffer_size));
        TRY(KOS_ERROR_EXCEPTION);
    }

    buffer = KOS_new_buffer(ctx, (uint32_t)size);

    if ( ! IS_BAD_PTR(buffer))
        memset(KOS_buffer_data(ctx, buffer), 0, size);

_error:
    return buffer;
}

static KOS_OBJ_PTR _get_buffer_prototype(KOS_CONTEXT *ctx,
                                         KOS_OBJ_PTR  this_obj,
                                         KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->buffer_prototype);
}

static KOS_OBJ_PTR _function_constructor(KOS_CONTEXT *ctx,
                                         KOS_OBJ_PTR  this_obj,
                                         KOS_OBJ_PTR  args_obj)
{
    /* TODO copy function object */
    return TO_OBJPTR(0);
}

static KOS_OBJ_PTR _get_function_prototype(KOS_CONTEXT *ctx,
                                           KOS_OBJ_PTR  this_obj,
                                           KOS_OBJ_PTR  args_obj)
{
    return TO_OBJPTR(&ctx->function_prototype);
}

static KOS_OBJ_PTR _apply(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  this_obj,
                          KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);
    KOS_OBJ_PTR    arg_this = KOS_array_read(ctx, args_obj, 0);
    KOS_OBJ_PTR    arg_args = TO_OBJPTR(0);

    if ( ! IS_BAD_PTR(arg_this))
        arg_args = KOS_array_read(ctx, args_obj, 1);

    if ( ! IS_BAD_PTR(arg_args))
        ret = KOS_call_function(ctx, this_obj, arg_this, arg_args);

    return ret;
}

static KOS_OBJ_PTR _slice(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  this_obj,
                          KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    KOS_OBJ_PTR a_obj;
    KOS_OBJ_PTR b_obj;
    int64_t     idx_a;
    int64_t     idx_b;

    a_obj = KOS_array_read(ctx, args_obj, 0);
    if (IS_BAD_PTR(a_obj))
        TRY(KOS_ERROR_EXCEPTION);

    b_obj = KOS_array_read(ctx, args_obj, 1);
    if (IS_BAD_PTR(b_obj))
        TRY(KOS_ERROR_EXCEPTION);

    if (IS_SMALL_INT(a_obj) || GET_OBJ_TYPE(a_obj) != OBJ_VOID)
        TRY(KOS_get_integer(ctx, a_obj, &idx_a));
    else
        idx_a = 0;

    if (IS_SMALL_INT(b_obj) || GET_OBJ_TYPE(b_obj) != OBJ_VOID)
        TRY(KOS_get_integer(ctx, b_obj, &idx_b));
    else
        idx_b = MAX_INT64;

    if (IS_STRING_OBJ(this_obj))
        ret = KOS_string_slice(ctx, this_obj, idx_a, idx_b);
    else if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_buffer_slice(ctx, this_obj, idx_a, idx_b);
    else
        ret = KOS_array_slice(ctx, this_obj, idx_a, idx_b);

_error:
    return ret;
}

static KOS_OBJ_PTR _get_array_size(KOS_CONTEXT *ctx,
                                   KOS_OBJ_PTR  this_obj,
                                   KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_ARRAY)
        ret = KOS_new_int(ctx, (int64_t)KOS_get_array_size(this_obj));
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_array));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_buffer_size(KOS_CONTEXT *ctx,
                                    KOS_OBJ_PTR  this_obj,
                                    KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_new_int(ctx, (int64_t)KOS_get_buffer_size(this_obj));
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_buffer));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _resize(KOS_CONTEXT *ctx,
                           KOS_OBJ_PTR  this_obj,
                           KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR size_obj;
    int64_t     size;

    size_obj = KOS_array_read(ctx, args_obj, 0);
    if (IS_BAD_PTR(size_obj))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_get_integer(ctx, size_obj, &size));

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        if (size < 0 || size > UINT_MAX) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_buffer_size));
            TRY(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_buffer_resize(ctx, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > UINT_MAX) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_array_size));
            TRY(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_array_resize(ctx, this_obj, (uint32_t)size));
    }

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _fill(KOS_CONTEXT *ctx,
                         KOS_OBJ_PTR  this_obj,
                         KOS_OBJ_PTR  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    arg      = KOS_array_read(ctx, args_obj, 0);
    int64_t        begin;
    int64_t        end;
    int64_t        value;

    if (num_args > 2) {

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &begin));
        else
            begin = 0;

        arg = KOS_array_read(ctx, args_obj, 1);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &end));
        else
            end = MAX_INT64;

        arg = KOS_array_read(ctx, args_obj, 2);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        TRY(KOS_get_integer(ctx, arg, &value));
    }
    else if (num_args > 1) {

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &begin));
        else
            begin = 0;

        end = MAX_INT64;

        arg = KOS_array_read(ctx, args_obj, 1);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        TRY(KOS_get_integer(ctx, arg, &value));
    }
    else {

        begin = 0;
        end   = MAX_INT64;

        TRY(KOS_get_integer(ctx, arg, &value));
    }

    if (value < 0 || value > 255) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_byte_value));
        TRY(KOS_ERROR_EXCEPTION);
    }

    error = KOS_buffer_fill(ctx, this_obj, begin, end, (uint8_t)value);

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

struct _KOS_PACK_FORMAT {
    KOS_OBJ_PTR fmt_str;
    KOS_OBJ_PTR data;
    int         idx;
    int         big_end;
};

typedef int (*_KOS_PACK_FORMAT_FUNC)(KOS_CONTEXT             *ctx,
                                     struct _KOS_PACK_FORMAT *fmt,
                                     KOS_OBJ_PTR              buffer_obj,
                                     char                     value_fmt,
                                     unsigned                 size,
                                     unsigned                 count);

static int _is_whitespace(unsigned char_code)
{
    return char_code == 0      || /* NUL */
           char_code == 11     || /* VTAB */
           char_code == 12     || /* FF */
           char_code == 32     || /* space */
           char_code == 0xA0   || /* NBSP */
           char_code == 0x2028 || /* line separator */
           char_code == 0x2029 || /* paragraph separator */
           char_code == 0xFEFF;   /* BOM */
}

static int _pack_format_skip_spaces(KOS_CONTEXT *ctx,
                                    KOS_OBJ_PTR  fmt_str,
                                    unsigned    *i_ptr)
{
    const unsigned size = KOS_get_string_length(fmt_str);
    unsigned       i    = *i_ptr;
    unsigned       c;

    if (i >= size)
        return KOS_SUCCESS;

    do
        c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);
    while (i < size && _is_whitespace(c));

    if (c != ~0U)
        i--;

    *i_ptr = i;

    return c == ~0U ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static unsigned _pack_format_get_count(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  fmt_str,
                                       unsigned    *i_ptr)
{
    const unsigned size  = KOS_get_string_length(fmt_str);
    unsigned       i     = *i_ptr;
    unsigned       c;
    unsigned       count;

    assert(i < size);

    c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);

    assert(c >= '0' && c <= '9');

    count = c - (unsigned)'0';

    while (i < size) {

        c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);

        if (c == ~0U) {
            count = ~0U;
            break;
        }

        if (c < '0' || c > '9') {
            i--;
            break;
        }

        count = count * 10 + (c - (unsigned)'0');
    }

    *i_ptr = i;
    return count;
}

static int _process_pack_format(KOS_CONTEXT             *ctx,
                                KOS_OBJ_PTR              buffer_obj,
                                _KOS_PACK_FORMAT_FUNC    handler,
                                struct _KOS_PACK_FORMAT *fmt)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_PTR    fmt_str  = fmt->fmt_str;
    const unsigned fmt_size = KOS_get_string_length(fmt_str);
    unsigned       i_fmt    = 0;

    while (i_fmt < fmt_size) {

        unsigned count = 1;
        unsigned size  = 1;
        unsigned c;

        TRY(_pack_format_skip_spaces(ctx, fmt_str, &i_fmt));

        if (i_fmt >= fmt_size)
            break;

        c = KOS_string_get_char_code(ctx, fmt_str, (int)i_fmt++);
        if (c == ~0U)
            TRY(KOS_ERROR_EXCEPTION);

        if (c >= '0' && c <= '9') {
            --i_fmt;
            count = _pack_format_get_count(ctx, fmt_str, &i_fmt);

            if (count == ~0U)
                TRY(KOS_ERROR_EXCEPTION);

            TRY(_pack_format_skip_spaces(ctx, fmt_str, &i_fmt));

            if (i_fmt >= fmt_size) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_pack_format));
                TRY(KOS_ERROR_EXCEPTION);
            }

            c = KOS_string_get_char_code(ctx, fmt_str, (int)i_fmt++);
            if (c == ~0U)
                TRY(KOS_ERROR_EXCEPTION);
        }

        switch (c) {

            case '<':
                fmt->big_end = 0;
                break;

            case '>':
                fmt->big_end = 1;
                break;

            case 'x':
                break;

            case 'u':
                /* fall through */
            case 'i':
                /* fall through */
            case 'f':
                /* fall through */
            case 'b':
                /* fall through */
            case 's': {
                unsigned next_c;
                TRY(_pack_format_skip_spaces(ctx, fmt_str, &i_fmt));
                next_c = (i_fmt < fmt_size) ? KOS_string_get_char_code(ctx, fmt_str, (int)i_fmt) : ~0U;
                if (next_c >= '0' && next_c <= '9') {
                    size = _pack_format_get_count(ctx, fmt_str, &i_fmt);
                }
                else {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_pack_format));
                    TRY(KOS_ERROR_EXCEPTION);
                }
                break;
            }

            default:
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_pack_format));
                TRY(KOS_ERROR_EXCEPTION);
        }

        if (c != '<' && c != '>')
            TRY(handler(ctx, fmt, buffer_obj, (char)c, size, count));
    }

_error:
    return error;
}

static int _pack_format(KOS_CONTEXT             *ctx,
                        struct _KOS_PACK_FORMAT *fmt,
                        KOS_OBJ_PTR              buffer_obj,
                        char                     value_fmt,
                        unsigned                 size,
                        unsigned                 count)
{
    int                error = KOS_SUCCESS;
    int                big_end;
    uint8_t           *dst;
    struct _KOS_VECTOR str_buf;

    _KOS_vector_init(&str_buf);

    if (fmt->idx < 0) {
        KOS_OBJ_PTR obj = fmt->data;

        fmt->idx = 1;

        if (KOS_get_array_size(obj) > 1) {

            obj = KOS_array_read(ctx, obj, 1);

            if ( ! IS_SMALL_INT(obj) && GET_OBJ_TYPE(obj) == OBJ_ARRAY) {
                fmt->data = obj;
                fmt->idx  = 0;
            }
        }
    }

    dst = KOS_buffer_make_room(ctx, buffer_obj, size * count);
    if ( ! dst)
        TRY(KOS_ERROR_EXCEPTION);

    big_end = fmt->big_end;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);
            memset(dst, 0, size * count);
            break;

        case 'u':
            /* fall through */
        case 'i': {
            if (size != 1 && size != 2 && size != 4 && size != 8) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_pack_format));
                TRY(KOS_ERROR_EXCEPTION);
            }
            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data)) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_enough_pack_values));
                TRY(KOS_ERROR_EXCEPTION);
            }
            for ( ; count; count--) {
                unsigned    i;
                int64_t     value;
                KOS_OBJ_PTR value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);

                if (IS_BAD_PTR(value_obj))
                    TRY(KOS_ERROR_EXCEPTION);

                if ( ! IS_SMALL_INT(value_obj)             &&
                    GET_OBJ_TYPE(value_obj) != OBJ_INTEGER &&
                    GET_OBJ_TYPE(value_obj) != OBJ_FLOAT) {

                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_pack_value));
                    TRY(KOS_ERROR_EXCEPTION);
                }

                TRY(KOS_get_integer(ctx, value_obj, &value));

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? (size - 1U - i) : i;
                    dst[offs] = (uint8_t)(value & 0xFF);
                    value >>= 8;
                }

                dst += size;
            }
            break;
        }

        case 'f': {
            if (size != 4 && size != 8) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_pack_format));
                TRY(KOS_ERROR_EXCEPTION);
            }
            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data)) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_enough_pack_values));
                TRY(KOS_ERROR_EXCEPTION);
            }
            for ( ; count; count--) {
                unsigned    i;
                KOS_OBJ_PTR value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                double      value;
                uint64_t    out_val;

                if (IS_BAD_PTR(value_obj))
                    TRY(KOS_ERROR_EXCEPTION);

                if ( ! IS_SMALL_INT(value_obj)             &&
                    GET_OBJ_TYPE(value_obj) != OBJ_INTEGER &&
                    GET_OBJ_TYPE(value_obj) != OBJ_FLOAT) {

                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_pack_value));
                    TRY(KOS_ERROR_EXCEPTION);
                }

                if (IS_SMALL_INT(value_obj))
                    value = (double)GET_SMALL_INT(value_obj);
                else if (GET_OBJ_TYPE(value_obj) == OBJ_INTEGER)
                    value = (double)OBJPTR(KOS_INTEGER, value_obj)->number;
                else
                    value = OBJPTR(KOS_FLOAT, value_obj)->number;

                if (size == 4)
                    out_val = _KOS_float_to_uint32_t((float)value);
                else
                    out_val = _KOS_double_to_uint64_t(value);

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? (size - 1U - i) : i;
                    dst[offs] = (uint8_t)(out_val & 0xFFU);
                    out_val >>= 8;
                }

                dst += size;
            }
            break;
        }

        case 'b': {
            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data)) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_enough_pack_values));
                TRY(KOS_ERROR_EXCEPTION);
            }
            for ( ; count; count--) {
                KOS_OBJ_PTR value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                uint8_t    *data;
                uint32_t    data_size;
                uint32_t    copy_size;

                if (IS_BAD_PTR(value_obj))
                    TRY(KOS_ERROR_EXCEPTION);

                if (IS_SMALL_INT(value_obj) || GET_OBJ_TYPE(value_obj) != OBJ_BUFFER) {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_pack_value));
                    TRY(KOS_ERROR_EXCEPTION);
                }

                data      = KOS_buffer_data(ctx, value_obj);
                data_size = KOS_get_buffer_size(value_obj);

                copy_size = size > data_size ? data_size : size;

                if (copy_size)
                    /* TODO what if input buf == output buf ? */
                    memcpy(dst, data, copy_size);

                if (copy_size < size)
                    memset(dst + copy_size, 0, size - copy_size);

                dst += size;
            }
            break;
        }

        case 's': {
            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data)) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_enough_pack_values));
                TRY(KOS_ERROR_EXCEPTION);
            }
            for ( ; count; count--) {
                KOS_OBJ_PTR value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                uint32_t    copy_size;

                if (IS_BAD_PTR(value_obj))
                    TRY(KOS_ERROR_EXCEPTION);

                if ( ! IS_STRING_OBJ(value_obj)) {
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_pack_value));
                    TRY(KOS_ERROR_EXCEPTION);
                }

                TRY(KOS_string_to_cstr_vec(ctx, value_obj, &str_buf));

                copy_size = size > str_buf.size ? (uint32_t)str_buf.size : size;

                if (copy_size)
                    memcpy(dst, str_buf.buffer, copy_size);

                if (copy_size < size)
                    memset(dst + copy_size, 0, size - copy_size);

                dst += size;
            }
            break;
        }

        default: assert(0);
    }

_error:
    _KOS_vector_destroy(&str_buf);
    return error;
}

static int _unpack_format(KOS_CONTEXT             *ctx,
                          struct _KOS_PACK_FORMAT *fmt,
                          KOS_OBJ_PTR              buffer_obj,
                          char                     value_fmt,
                          unsigned                 size,
                          unsigned                 count)
{
    int            error     = KOS_SUCCESS;
    uint8_t       *data      = KOS_buffer_data(ctx, buffer_obj);
    const uint32_t data_size = KOS_get_buffer_size(buffer_obj);
    int            big_end   = fmt->big_end;
    KOS_OBJ_PTR    obj;

    if (fmt->idx + size * count > data_size) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unpack_buf_too_short));
        return KOS_ERROR_EXCEPTION;
    }

    data += fmt->idx;

    switch (value_fmt)
    {
        case 'x':
            assert(size == 1);
            data += size * count;
            break;

        case 'f':
            /* fall through */
        case 'i':
            /* fall through */
        case 'u': {
            for ( ; count; count--) {
                uint64_t value = 0;
                unsigned i;

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? i : (size - 1 - i);
                    value = (value << 8) | data[offs];
                }

                if (value_fmt == 'i' && size < 8) {
                    const unsigned shift = 64 - 8 * size;
                    const int64_t  ival  = (int64_t)(value << shift);
                    obj = KOS_new_int(ctx, ival >> shift);
                }
                else if (value_fmt == 'f') {
                    double fvalue;
                    if (size == 4) {
                        union {
                            float    f;
                            uint32_t u;
                        } u2f;
                        u2f.u  = (uint32_t)value;
                        fvalue = u2f.f;
                    }
                    else {
                        union {
                            double   f;
                            uint64_t u;
                        } u2f;
                        u2f.u  = value;
                        fvalue = u2f.f;
                    }
                    obj = KOS_new_float(ctx, fvalue);
                }
                else
                    obj = KOS_new_int(ctx, (int64_t)value);

                if (IS_BAD_PTR(obj))
                    TRY(KOS_ERROR_EXCEPTION);

                TRY(KOS_array_push(ctx, fmt->data, obj));

                data += size;
            }
            break;
        }

        case 'b': {
            for ( ; count; count--) {
                obj = KOS_new_buffer(ctx, size);

                if (IS_BAD_PTR(obj))
                    TRY(KOS_ERROR_EXCEPTION);

                memcpy(KOS_buffer_data(ctx, obj), data, size);

                TRY(KOS_array_push(ctx, fmt->data, obj));

                data += size;
            }
            break;
        }

        case 's': {
            for ( ; count; count--) {
                obj = KOS_new_string(ctx, (char *)data, size);

                if (IS_BAD_PTR(obj))
                    TRY(KOS_ERROR_EXCEPTION);

                TRY(KOS_array_push(ctx, fmt->data, obj));

                data += size;
            }
            break;
        }

        default:
            assert(0);
            break;
    }

    fmt->idx = (int)(data - KOS_buffer_data(ctx, buffer_obj));

_error:
    return error;
}

static KOS_OBJ_PTR _pack(KOS_CONTEXT *ctx,
                         KOS_OBJ_PTR  this_obj,
                         KOS_OBJ_PTR  args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = KOS_array_read(ctx, args_obj, 0);
    fmt.data    = args_obj;
    fmt.idx     = -1;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(fmt.fmt_str));

    if (IS_STRING_OBJ(fmt.fmt_str))
        error = _process_pack_format(ctx, this_obj, _pack_format, &fmt);
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_string));
        error = KOS_ERROR_EXCEPTION;
    }

    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _unpack(KOS_CONTEXT *ctx,
                           KOS_OBJ_PTR  this_obj,
                           KOS_OBJ_PTR  args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = KOS_array_read(ctx, args_obj, 0);
    fmt.data    = KOS_new_array(ctx, 0);
    fmt.idx     = 0;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(fmt.fmt_str));

    if (IS_BAD_PTR(fmt.data))
        error = KOS_ERROR_EXCEPTION;
    else
        if (IS_STRING_OBJ(fmt.fmt_str))
            error = _process_pack_format(ctx, this_obj, _unpack_format, &fmt);
        else {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_string));
            error = KOS_ERROR_EXCEPTION;
        }

    return error ? TO_OBJPTR(0) : fmt.data;
}

static KOS_OBJ_PTR _copy_buffer(KOS_CONTEXT *ctx,
                                KOS_OBJ_PTR  this_obj,
                                KOS_OBJ_PTR  args_obj)
{
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    arg        = KOS_array_read(ctx, args_obj, 0);
    int64_t        dest_begin = 0;
    int64_t        src_begin  = 0;
    int64_t        src_end    = MAX_INT64;
    KOS_OBJ_PTR    src;

    if (num_args > 3) {

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &dest_begin));

        src = KOS_array_read(ctx, args_obj, 1);
        if (IS_BAD_PTR(src))
            TRY(KOS_ERROR_EXCEPTION);

        arg = KOS_array_read(ctx, args_obj, 2);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &src_begin));

        arg = KOS_array_read(ctx, args_obj, 3);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &src_end));
    }
    else if (num_args > 2) {

        int arg_idx = 1;

        if (IS_NUMERIC_OBJ(arg) || GET_OBJ_TYPE(arg) == OBJ_VOID) {

            arg_idx = 2;

            if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
                TRY(KOS_get_integer(ctx, arg, &dest_begin));

            src = KOS_array_read(ctx, args_obj, 1);
            if (IS_BAD_PTR(src))
                TRY(KOS_ERROR_EXCEPTION);
        }
        else
            src = arg;

        arg = KOS_array_read(ctx, args_obj, arg_idx);
        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
            TRY(KOS_get_integer(ctx, arg, &src_begin));

        if (arg_idx == 1) {

            arg = KOS_array_read(ctx, args_obj, arg_idx+1);
            if (IS_BAD_PTR(arg))
                TRY(KOS_ERROR_EXCEPTION);

            if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
                TRY(KOS_get_integer(ctx, arg, &src_end));
        }
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg) || GET_OBJ_TYPE(arg) == OBJ_VOID) {

            if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
                TRY(KOS_get_integer(ctx, arg, &dest_begin));

            src = KOS_array_read(ctx, args_obj, 1);
            if (IS_BAD_PTR(src))
                TRY(KOS_ERROR_EXCEPTION);
        }
        else {

            src = arg;

            arg = KOS_array_read(ctx, args_obj, 1);
            if (IS_BAD_PTR(arg))
                TRY(KOS_ERROR_EXCEPTION);

            if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_VOID)
                TRY(KOS_get_integer(ctx, arg, &src_begin));
        }
    }
    else {

        src        = arg;
        dest_begin = 0;
        src_begin  = 0;
        src_end    = MAX_INT64;
    }

    error = KOS_buffer_copy(ctx, this_obj, dest_begin, src, src_begin, src_end);

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _reserve(KOS_CONTEXT *ctx,
                            KOS_OBJ_PTR  this_obj,
                            KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR size_obj;
    int64_t     size;

    size_obj = KOS_array_read(ctx, args_obj, 0);
    if (IS_BAD_PTR(size_obj))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_get_integer(ctx, size_obj, &size));

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        if (size < 0 || size > UINT_MAX) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_buffer_size));
            TRY(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_buffer_reserve(ctx, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > UINT_MAX) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_array_size));
            TRY(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_array_reserve(ctx, this_obj, (uint32_t)size));
    }

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _insert_array(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  this_obj,
                                 KOS_OBJ_PTR  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    begin_obj;
    KOS_OBJ_PTR    end_obj;
    KOS_OBJ_PTR    src_obj;
    int64_t        begin    = 0;
    int64_t        end;
    int64_t        src_len;

    begin_obj = KOS_array_read(ctx, args_obj, 0);
    if (IS_BAD_PTR(begin_obj))
        TRY(KOS_ERROR_EXCEPTION);

    end_obj = KOS_array_read(ctx, args_obj, 1);
    if (IS_BAD_PTR(end_obj))
        TRY(KOS_ERROR_EXCEPTION);

    if (num_args > 2) {
        src_obj = KOS_array_read(ctx, args_obj, 2);
        if (IS_BAD_PTR(src_obj))
            TRY(KOS_ERROR_EXCEPTION);
    }
    else {
        src_obj = end_obj;
        end_obj = begin_obj;
    }

    if (IS_SMALL_INT(this_obj) || GET_OBJ_TYPE(this_obj) != OBJ_ARRAY ||
        IS_SMALL_INT(src_obj)  || GET_OBJ_TYPE(src_obj)  != OBJ_ARRAY) {

        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_array));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (IS_SMALL_INT(begin_obj) || GET_OBJ_TYPE(begin_obj) != OBJ_VOID)
        TRY(KOS_get_integer(ctx, begin_obj, &begin));
    else if (num_args == 2)
        begin = MAX_INT64;

    if (IS_SMALL_INT(end_obj) || GET_OBJ_TYPE(end_obj) != OBJ_VOID)
        TRY(KOS_get_integer(ctx, end_obj, &end));
    else
        end = MAX_INT64;

    src_len = MAX_INT64;

    TRY(KOS_array_insert(ctx, this_obj, begin, end, src_obj, 0, src_len));

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _get_string_size(KOS_CONTEXT *ctx,
                                    KOS_OBJ_PTR  this_obj,
                                    KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_STRING_OBJ(this_obj))
        ret = KOS_new_int(ctx, (int64_t)KOS_get_string_length(this_obj));
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_string));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_function_name(KOS_CONTEXT *ctx,
                                      KOS_OBJ_PTR  this_obj,
                                      KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func = OBJPTR(KOS_FUNCTION, this_obj);

        /* TODO add builtin function name */
        if (IS_BAD_PTR(func->module) || func->instr_offs == ~0U)
            ret = TO_OBJPTR(&str_builtin);
        else
            ret = KOS_module_addr_to_func_name(OBJPTR(KOS_MODULE, func->module),
                                               func->instr_offs);
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_instructions(KOS_CONTEXT *ctx,
                                     KOS_OBJ_PTR  this_obj,
                                     KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func      = OBJPTR(KOS_FUNCTION, this_obj);
        uint32_t            num_instr = 0;

        if ( ! IS_BAD_PTR(func->module))
            num_instr = KOS_module_func_get_num_instr(OBJPTR(KOS_MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(ctx, (int64_t)num_instr);
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_code_size(KOS_CONTEXT *ctx,
                                  KOS_OBJ_PTR  this_obj,
                                  KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func      = OBJPTR(KOS_FUNCTION, this_obj);
        uint32_t            code_size = 0;

        if ( ! IS_BAD_PTR(func->module))
            code_size = KOS_module_func_get_code_size(OBJPTR(KOS_MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(ctx, (int64_t)code_size);
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_registers(KOS_CONTEXT *ctx,
                                  KOS_OBJ_PTR  this_obj,
                                  KOS_OBJ_PTR  args_obj)
{
    KOS_OBJ_PTR ret;

    if ( ! IS_SMALL_INT(this_obj) && GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func = OBJPTR(KOS_FUNCTION, this_obj);

        ret = KOS_new_int(ctx, (int64_t)func->num_regs);
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

int _KOS_module_lang_init(KOS_MODULE *module)
{
    int error = KOS_SUCCESS;

    TRY_ADD_FUNCTION( module, "print",   _print,   0);
    TRY_ADD_GENERATOR(module, "deep",    _deep,    1);
    TRY_ADD_GENERATOR(module, "shallow", _shallow, 1);

    TRY_CREATE_CONSTRUCTOR(array);
    TRY_CREATE_CONSTRUCTOR(boolean);
    TRY_CREATE_CONSTRUCTOR(buffer);
    TRY_CREATE_CONSTRUCTOR(float);
    TRY_CREATE_CONSTRUCTOR(function);
    TRY_CREATE_CONSTRUCTOR(integer);
    TRY_CREATE_CONSTRUCTOR(number);
    TRY_CREATE_CONSTRUCTOR(object);
    TRY_CREATE_CONSTRUCTOR(string);
    TRY_CREATE_CONSTRUCTOR(void);

    TRY_ADD_MEMBER_FUNCTION( module, PROTO(array),    "insert_array", _insert_array,      2);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(array),    "reserve",      _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(array),    "resize",       _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(array),    "slice",        _slice,             2);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(array),    "size",         _get_array_size,    0);

    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "copy_buffer",  _copy_buffer,       1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "fill",         _fill,              1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "pack",         _pack,              1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "reserve",      _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "resize",       _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "slice",        _slice,             2);
    TRY_ADD_MEMBER_FUNCTION( module, PROTO(buffer),   "unpack",       _unpack,            1);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(buffer),   "size",         _get_buffer_size,   0);

    TRY_ADD_MEMBER_FUNCTION( module, PROTO(function), "apply",        _apply,             2);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(function), "instructions", _get_instructions,  0);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(function), "name",         _get_function_name, 0);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(function), "registers",    _get_registers,     0);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(function), "size",         _get_code_size,     0);

    TRY_ADD_MEMBER_FUNCTION( module, PROTO(string),   "slice",        _slice,             2);
    TRY_ADD_MEMBER_PROPERTY( module, PROTO(string),   "size",         _get_string_size,   0);

    TRY_ADD_MEMBER_GENERATOR(module, PROTO(void),     "iterator",     _iterator,          0);

_error:
    return error;
}

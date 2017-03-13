/*
 * Copyright (c) 2014-2017 Chris Dragan
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
#include "../inc/kos_utils.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>

static KOS_ASCII_STRING(str_builtin,                       "<builtin>");
static KOS_ASCII_STRING(str_err_bad_number,                "number parse failed");
static KOS_ASCII_STRING(str_err_bad_pack_value,            "invalid value type for pack format");
static KOS_ASCII_STRING(str_err_cannot_convert_to_array,   "unsupported type passed to array constructor");
static KOS_ASCII_STRING(str_err_cannot_override_prototype, "cannot override prototype");
static KOS_ASCII_STRING(str_err_invalid_array_size,        "array size out of range");
static KOS_ASCII_STRING(str_err_invalid_byte_value,        "buffer element value out of range");
static KOS_ASCII_STRING(str_err_invalid_buffer_size,       "buffer size out of range");
static KOS_ASCII_STRING(str_err_invalid_pack_format,       "invalid pack format");
static KOS_ASCII_STRING(str_err_invalid_string_idx,        "string index is out of range");
static KOS_ASCII_STRING(str_err_not_array,                 "object is not an array");
static KOS_ASCII_STRING(str_err_not_buffer,                "object is not a buffer");
static KOS_ASCII_STRING(str_err_not_enough_pack_values,    "insufficient number of packed values");
static KOS_ASCII_STRING(str_err_not_function,              "object is not a function");
static KOS_ASCII_STRING(str_err_not_string,                "object is not a string");
static KOS_ASCII_STRING(str_err_unpack_buf_too_short,      "unpacked buffer too short");
static KOS_ASCII_STRING(str_err_unsup_operand_types,       "unsupported operand types");

#define TRY_CREATE_CONSTRUCTOR(name)                               \
do {                                                               \
    static KOS_ASCII_STRING(str_name, #name);                      \
    KOS_CONTEXT *ctx = OBJPTR(KOS_MODULE, frame->module)->context; \
    TRY(_create_constructor(frame,                                 \
                            TO_OBJPTR(&str_name),                  \
                            _##name##_constructor,                 \
                            TO_OBJPTR(&ctx->name##_prototype)));   \
} while (0)

#define PROTO(type) (TO_OBJPTR(&OBJPTR(KOS_MODULE, frame->module)->context->type##_prototype))

static KOS_OBJ_PTR _print(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      this_obj,
                          KOS_OBJ_PTR      args_obj)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    TRY(KOS_print_to_cstr_vec(frame, args_obj, &cstr, " ", 1));

    if (cstr.size)
        printf("%.*s\n", (int)cstr.size-1, cstr.buffer);
    else
        printf("\n");

_error:
    _KOS_vector_destroy(&cstr);

    return error ? TO_OBJPTR(0) : KOS_VOID;
}

static KOS_OBJ_PTR _print_(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      this_obj,
                           KOS_OBJ_PTR      args_obj)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    TRY(KOS_print_to_cstr_vec(frame, args_obj, &cstr, " ", 1));

    if (cstr.size)
        printf("%.*s", (int)cstr.size-1, cstr.buffer);

_error:
    _KOS_vector_destroy(&cstr);

    return error ? TO_OBJPTR(0) : KOS_VOID;
}

static KOS_OBJ_PTR _object_iterator(KOS_STACK_FRAME           *frame,
                                    KOS_OBJ_PTR                regs_obj,
                                    KOS_OBJ_PTR                args_obj,
                                    enum KOS_OBJECT_WALK_DEPTH deep)
{
    int                  error = KOS_SUCCESS;
    KOS_OBJ_PTR          walk;
    KOS_OBJECT_WALK_ELEM elem = { TO_OBJPTR(0), TO_OBJPTR(0) };

    assert( ! IS_BAD_PTR(regs_obj));
    TRY_OBJPTR(regs_obj);

    assert(IS_TYPE(OBJ_ARRAY, regs_obj));
    assert(KOS_get_array_size(regs_obj) > 0);

    walk = KOS_array_read(frame, regs_obj, 0);
    assert( ! IS_BAD_PTR(walk));
    TRY_OBJPTR(walk);

    if ( ! IS_TYPE(OBJ_OBJECT_WALK, walk)) {
        walk = KOS_new_object_walk(frame, walk, deep);
        TRY_OBJPTR(walk);

        TRY(KOS_array_write(frame, regs_obj, 0, walk));
    }

    elem = KOS_object_walk(frame, OBJPTR(KOS_OBJECT_WALK, walk));

_error:
    return elem.key;
}

static KOS_OBJ_PTR _shallow(KOS_STACK_FRAME *frame,
                            KOS_OBJ_PTR      regs_obj,
                            KOS_OBJ_PTR      args_obj)
{
    return _object_iterator(frame, regs_obj, args_obj, KOS_SHALLOW);
}

static KOS_OBJ_PTR _deep(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      regs_obj,
                         KOS_OBJ_PTR      args_obj)
{
    return _object_iterator(frame, regs_obj, args_obj, KOS_DEEP);
}

static KOS_OBJ_PTR _iterator(KOS_STACK_FRAME *frame,
                             KOS_OBJ_PTR      regs_obj,
                             KOS_OBJ_PTR      args_obj)
{
    return TO_OBJPTR(0);
}

static int _create_constructor(KOS_STACK_FRAME     *frame,
                               KOS_OBJ_PTR          str_name,
                               KOS_FUNCTION_HANDLER constructor,
                               KOS_OBJ_PTR          prototype)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR func_obj = KOS_new_function(frame, prototype);

    TRY_OBJPTR(func_obj);

    assert( ! IS_BAD_PTR(frame->module));

    OBJPTR(KOS_FUNCTION, func_obj)->handler = constructor;
    OBJPTR(KOS_FUNCTION, func_obj)->module  = frame->module;

    TRY(KOS_module_add_global(frame,
                              str_name,
                              func_obj,
                              0));

_error:
    return error;
}

static KOS_OBJ_PTR _number_constructor(KOS_STACK_FRAME *frame,
                                       KOS_OBJ_PTR      this_obj,
                                       KOS_OBJ_PTR      args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

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

                if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, arg, &cstr)) {

                    const char         *begin = cstr.buffer;
                    const char         *end   = begin + cstr.size - 1;
                    struct _KOS_NUMERIC numeric;

                    assert(begin <= end);

                    if (KOS_SUCCESS == _KOS_parse_numeric(begin, end, &numeric)) {

                        if (numeric.type == KOS_INTEGER_VALUE)
                            ret = KOS_new_int(frame, numeric.u.i);
                        else {
                            assert(numeric.type == KOS_FLOAT_VALUE);
                            ret = KOS_new_float(frame, numeric.u.d);
                        }
                    }
                    else
                        KOS_raise_exception(frame, TO_OBJPTR(&str_err_bad_number));
                }

                _KOS_vector_destroy(&cstr);
                break;
            }

            default:
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
                break;
        }
    }

    return ret;
}

static KOS_OBJ_PTR _integer_constructor(KOS_STACK_FRAME *frame,
                                        KOS_OBJ_PTR      this_obj,
                                        KOS_OBJ_PTR      args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        int64_t     value;
        KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

        if ( ! IS_BAD_PTR(arg)) {
            if (IS_NUMERIC_OBJ(arg)) {
                if (KOS_get_integer(frame, arg, &value) == KOS_SUCCESS)
                    ret = KOS_new_int(frame, value);
            }
            else if (IS_STRING_OBJ(arg)) {

                struct _KOS_VECTOR cstr;
                _KOS_vector_init(&cstr);

                if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, arg, &cstr)) {

                    const char *begin = cstr.buffer;
                    const char *end   = begin + cstr.size - 1;
                    int         error;

                    assert(begin <= end);

                    error = _KOS_parse_int(begin, end, &value);

                    if (error)
                        KOS_raise_exception(frame, TO_OBJPTR(&str_err_bad_number));
                    else
                        ret = KOS_new_int(frame, value);
                }

                _KOS_vector_destroy(&cstr);
            }
            else
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
        }
    }

    return ret;
}

static KOS_OBJ_PTR _float_constructor(KOS_STACK_FRAME *frame,
                                      KOS_OBJ_PTR      this_obj,
                                      KOS_OBJ_PTR      args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = KOS_new_float(frame, 0);
    else {
        KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

        if ( ! IS_BAD_PTR(arg)) {
            if (IS_SMALL_INT(arg))
                ret = KOS_new_float(frame, (double)GET_SMALL_INT(arg));
            else switch (GET_OBJ_TYPE(arg)) {

                case OBJ_INTEGER: {
                    ret = KOS_new_float(frame, (double)OBJPTR(KOS_INTEGER, arg)->number);
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

                    if (KOS_string_to_cstr_vec(frame, arg, &cstr) == KOS_SUCCESS) {

                        const char *begin = cstr.buffer;
                        const char *end   = begin + cstr.size - 1;
                        double      value;
                        int         error;

                        assert(begin <= end);

                        error = _KOS_parse_double(begin, end, &value);

                        if (error)
                            KOS_raise_exception(frame, TO_OBJPTR(&str_err_bad_number));
                        else
                            ret = KOS_new_float(frame, value);
                    }

                    _KOS_vector_destroy(&cstr);
                    break;
                }

                default:
                    KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
                    break;
            }
        }
    }

    return ret;
}

static KOS_OBJ_PTR _boolean_constructor(KOS_STACK_FRAME *frame,
                                        KOS_OBJ_PTR      this_obj,
                                        KOS_OBJ_PTR      args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args > 0) {
        KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

        if ( ! IS_BAD_PTR(arg))
            ret = KOS_BOOL(_KOS_is_truthy(arg));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

static KOS_OBJ_PTR _void_constructor(KOS_STACK_FRAME *frame,
                                     KOS_OBJ_PTR      this_obj,
                                     KOS_OBJ_PTR      args_obj)
{
    return KOS_VOID;
}

static KOS_OBJ_PTR _string_constructor(KOS_STACK_FRAME *frame,
                                       KOS_OBJ_PTR     this_obj,
                                       KOS_OBJ_PTR     args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);

    if (num_args == 0)
        ret = KOS_new_string(frame, 0, 0);

    else if (num_args == 1) {

        KOS_OBJ_PTR obj = KOS_array_read(frame, args_obj, 0);
        TRY_OBJPTR(obj);

        ret = KOS_object_to_string(frame, obj);
    }
    else {

        uint32_t i;

        for (i=0; i < num_args; i++) {
            KOS_OBJ_PTR obj = KOS_array_read(frame, args_obj, (int)i);
            TRY_OBJPTR(obj);

            if ( ! IS_STRING_OBJ(obj)) {

                obj = KOS_object_to_string(frame, obj);
                TRY_OBJPTR(obj);

                TRY(KOS_array_write(frame, args_obj, (int)i, obj));
            }
        }

        if (i == num_args)
            ret = KOS_string_add_many(frame, _KOS_get_array_buffer(OBJPTR(KOS_ARRAY, args_obj)), num_args);
    }

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _object_constructor(KOS_STACK_FRAME *frame,
                                       KOS_OBJ_PTR      this_obj,
                                       KOS_OBJ_PTR      args_obj)
{
    return KOS_new_object(frame);
}

static KOS_OBJ_PTR _array_constructor(KOS_STACK_FRAME *frame,
                                      KOS_OBJ_PTR      this_obj,
                                      KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_PTR    array    = KOS_new_array(frame, 0);
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i_arg;

    TRY_OBJPTR(array);

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const uint32_t cur_size = KOS_get_array_size(array);

        KOS_OBJ_PTR elem = KOS_array_read(frame, args_obj, (int)i_arg);
        TRY_OBJPTR(elem);

        if (IS_SMALL_INT(elem)) {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_cannot_convert_to_array));
            return TO_OBJPTR(0);
        }

        switch (GET_OBJ_TYPE(elem)) {

            case OBJ_ARRAY:
                TRY(KOS_array_insert(frame, array, cur_size, cur_size,
                                     elem, 0, (int32_t)KOS_get_array_size(elem)));
                break;

            case OBJ_STRING_8:
            case OBJ_STRING_16:
            case OBJ_STRING_32: {
                const uint32_t len = KOS_get_string_length(elem);
                uint32_t       i;

                TRY(KOS_array_resize(frame, array, cur_size + len));

                for (i = 0; i < len; i++) {
                    KOS_OBJ_PTR ch = KOS_string_get_char(frame, elem, (int)i);
                    TRY_OBJPTR(ch);
                    TRY(KOS_array_write(frame, array, (int)(cur_size + i), ch));
                }
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t size = KOS_get_buffer_size(elem);
                uint32_t       i;
                uint8_t       *buf  = 0;

                if (size) {
                    buf = KOS_buffer_data(elem);
                    assert(buf);
                }

                TRY(KOS_array_resize(frame, array, cur_size + size));

                for (i = 0; i < size; i++) {
                    const KOS_OBJ_PTR byte = TO_SMALL_INT((int)buf[i]);
                    TRY(KOS_array_write(frame, array, (int)(cur_size + i), byte));
                }
                break;
            }

            case OBJ_FUNCTION: {
                KOS_OBJ_PTR               gen_args;
                enum _KOS_GENERATOR_STATE state = OBJPTR(KOS_FUNCTION, elem)->generator_state;

                if (state != KOS_GEN_READY && state != KOS_GEN_ACTIVE && state != KOS_GEN_DONE) {
                    KOS_raise_exception(frame, TO_OBJPTR(&str_err_cannot_convert_to_array));
                    return TO_OBJPTR(0);
                }

                gen_args = KOS_new_array(frame, 0);
                TRY_OBJPTR(gen_args);

                if (state != KOS_GEN_DONE) {
                    for (;;) {
                        KOS_OBJ_PTR ret = KOS_call_function(frame, elem, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret)) /* end of iterator */
                            break;
                        TRY(KOS_array_push(frame, array, ret, 0));
                    }
                }
                break;
            }

            case OBJ_OBJECT:
                /* TODO keys */
                break;

            default:
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_cannot_convert_to_array));
                return TO_OBJPTR(0);
        }
    }

_error:
    return error ? TO_OBJPTR(0) : array;
}

static KOS_OBJ_PTR _buffer_constructor(KOS_STACK_FRAME *frame,
                                       KOS_OBJ_PTR      this_obj,
                                       KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    int64_t        size     = 0;
    KOS_OBJ_PTR    buffer   = TO_OBJPTR(0);

    /* TODO handle multiple inputs: array of numbers, string, generator */

    if (num_args > 0)
        TRY(KOS_get_integer(frame, KOS_array_read(frame, args_obj, 0), &size));

    if (size < 0 || size > INT_MAX) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_buffer_size));
        return TO_OBJPTR(0);
    }

    buffer = KOS_new_buffer(frame, (uint32_t)size);
    TRY_OBJPTR(buffer);

    if (size)
        memset(KOS_buffer_data(buffer), 0, (size_t)size);

_error:
    return buffer;
}

static KOS_OBJ_PTR _function_constructor(KOS_STACK_FRAME *frame,
                                         KOS_OBJ_PTR      this_obj,
                                         KOS_OBJ_PTR      args_obj)
{
    /* TODO copy function object */
    return TO_OBJPTR(0);
}

static KOS_OBJ_PTR _apply(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      this_obj,
                          KOS_OBJ_PTR      args_obj)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR ret      = TO_OBJPTR(0);
    KOS_OBJ_PTR arg_args = TO_OBJPTR(0);
    KOS_OBJ_PTR arg_this;

    arg_this = KOS_array_read(frame, args_obj, 0);
    TRY_OBJPTR(arg_this);

    arg_args = KOS_array_read(frame, args_obj, 1);
    TRY_OBJPTR(arg_args);

    arg_args = KOS_array_slice(frame, arg_args, 0, MAX_INT64);
    TRY_OBJPTR(arg_args);

    ret = KOS_call_function(frame, this_obj, arg_this, arg_args);

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _set_prototype(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      this_obj,
                                  KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    assert( ! IS_BAD_PTR(this_obj));

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_OBJ_PTR                arg     = KOS_array_read(frame, args_obj, 0);
        const KOS_FUNCTION_HANDLER handler = OBJPTR(KOS_FUNCTION, this_obj)->handler;

        if (handler == _array_constructor    ||
            handler == _boolean_constructor  ||
            handler == _buffer_constructor   ||
            handler == _float_constructor    ||
            handler == _function_constructor ||
            handler == _integer_constructor  ||
            handler == _number_constructor   ||
            handler == _object_constructor   ||
            handler == _string_constructor   ||
            handler == _void_constructor) {

            KOS_raise_exception(frame, TO_OBJPTR(&str_err_cannot_override_prototype));
            arg = TO_OBJPTR(0);
        }

        if ( ! IS_BAD_PTR(arg)) {
            KOS_FUNCTION *func = OBJPTR(KOS_FUNCTION, this_obj);
            KOS_atomic_write_ptr(func->prototype, (void *)arg);
            ret = this_obj;
        }
    }
    else
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));

    return ret;
}

static KOS_OBJ_PTR _slice(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      this_obj,
                          KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    KOS_OBJ_PTR a_obj;
    KOS_OBJ_PTR b_obj;
    int64_t     idx_a = 0;
    int64_t     idx_b = 0;

    a_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJPTR(a_obj);

    b_obj = KOS_array_read(frame, args_obj, 1);
    TRY_OBJPTR(b_obj);

    if (IS_NUMERIC_OBJ(a_obj))
        TRY(KOS_get_integer(frame, a_obj, &idx_a));
    else if (IS_TYPE(OBJ_VOID, a_obj))
        idx_a = 0;
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
        return TO_OBJPTR(0);
    }

    if (IS_NUMERIC_OBJ(b_obj))
        TRY(KOS_get_integer(frame, b_obj, &idx_b));
    else if (IS_TYPE(OBJ_VOID, b_obj))
        idx_b = MAX_INT64;
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
        return TO_OBJPTR(0);
    }

    if (IS_STRING_OBJ(this_obj))
        ret = KOS_string_slice(frame, this_obj, idx_a, idx_b);
    else if (IS_TYPE(OBJ_BUFFER, this_obj))
        ret = KOS_buffer_slice(frame, this_obj, idx_a, idx_b);
    else
        ret = KOS_array_slice(frame, this_obj, idx_a, idx_b);

_error:
    return ret;
}

static KOS_OBJ_PTR _get_array_size(KOS_STACK_FRAME *frame,
                                   KOS_OBJ_PTR      this_obj,
                                   KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    assert( ! IS_BAD_PTR(this_obj));

    if (IS_TYPE(OBJ_ARRAY, this_obj))
        ret = KOS_new_int(frame, (int64_t)KOS_get_array_size(this_obj));
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_buffer_size(KOS_STACK_FRAME *frame,
                                    KOS_OBJ_PTR      this_obj,
                                    KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    assert( ! IS_BAD_PTR(this_obj));
    assert(IS_TYPE(OBJ_BUFFER, this_obj));

    if (IS_TYPE(OBJ_BUFFER, this_obj))
        ret = KOS_new_int(frame, (int64_t)KOS_get_buffer_size(this_obj));

    return ret;
}

static KOS_OBJ_PTR _resize(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      this_obj,
                           KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR size_obj;
    int64_t     size;

    size_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJPTR(size_obj);

    TRY(KOS_get_integer(frame, size_obj, &size));

    assert( ! IS_BAD_PTR(this_obj));

    if (IS_TYPE(OBJ_BUFFER, this_obj)) {
        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_buffer_size));
            return TO_OBJPTR(0);
        }

        TRY(KOS_buffer_resize(frame, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_array_size));
            return TO_OBJPTR(0);
        }

        TRY(KOS_array_resize(frame, this_obj, (uint32_t)size));
    }

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _fill(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    arg      = KOS_array_read(frame, args_obj, 0);
    int64_t        begin    = 0;
    int64_t        end      = 0;
    int64_t        value;

    if (num_args > 2) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &begin));
        else if (IS_TYPE(OBJ_VOID, arg))
            begin = 0;
        else {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
        }

        arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJPTR(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &end));
        else if (IS_TYPE(OBJ_VOID, arg))
            end = MAX_INT64;
        else {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
        }

        arg = KOS_array_read(frame, args_obj, 2);
        TRY_OBJPTR(arg);

        TRY(KOS_get_integer(frame, arg, &value));
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &begin));
        else if (IS_TYPE(OBJ_VOID, arg))
            begin = 0;
        else {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
        }

        end = MAX_INT64;

        arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJPTR(arg);

        TRY(KOS_get_integer(frame, arg, &value));
    }
    else {

        begin = 0;
        end   = MAX_INT64;

        TRY(KOS_get_integer(frame, arg, &value));
    }

    if (value < 0 || value > 255) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_byte_value));
        return TO_OBJPTR(0);
    }

    error = KOS_buffer_fill(frame, this_obj, begin, end, (uint8_t)value);

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

struct _KOS_PACK_FORMAT {
    KOS_OBJ_PTR fmt_str;
    KOS_OBJ_PTR data;
    int         idx;
    int         big_end;
};

typedef int (*_KOS_PACK_FORMAT_FUNC)(KOS_STACK_FRAME         *frame,
                                     struct _KOS_PACK_FORMAT *fmt,
                                     KOS_OBJ_PTR              buffer_obj,
                                     char                     value_fmt,
                                     unsigned                 size,
                                     unsigned                 count);

static int _is_whitespace(unsigned char_code)
{
    return char_code == 0      || /* NUL */
           char_code == 9      || /* TAB */
           char_code == 11     || /* VTAB */
           char_code == 12     || /* FF */
           char_code == 32     || /* space */
           char_code == 0xA0   || /* NBSP */
           char_code == 0x2028 || /* line separator */
           char_code == 0x2029 || /* paragraph separator */
           char_code == 0xFEFF;   /* BOM */
}

static void _pack_format_skip_spaces(KOS_STACK_FRAME *frame,
                                     KOS_OBJ_PTR      fmt_str,
                                     unsigned        *i_ptr)
{
    const unsigned size = KOS_get_string_length(fmt_str);
    unsigned       i    = *i_ptr;
    unsigned       c;

    if (i >= size)
        return;

    do {
        c = KOS_string_get_char_code(frame, fmt_str, (int)i++);
        assert(c != ~0U);
    }
    while (i < size && _is_whitespace(c));

    if (i < size || ! _is_whitespace(c))
        i--;

    *i_ptr = i;
}

static unsigned _pack_format_get_count(KOS_STACK_FRAME *frame,
                                       KOS_OBJ_PTR      fmt_str,
                                       unsigned        *i_ptr)
{
    const unsigned size  = KOS_get_string_length(fmt_str);
    unsigned       i     = *i_ptr;
    unsigned       c;
    unsigned       count;

    assert(i < size);

    c = KOS_string_get_char_code(frame, fmt_str, (int)i++);

    assert(c >= '0' && c <= '9');

    count = c - (unsigned)'0';

    while (i < size) {

        c = KOS_string_get_char_code(frame, fmt_str, (int)i++);

        assert(c != ~0U);

        if (c < '0' || c > '9') {
            i--;
            break;
        }

        count = count * 10 + (c - (unsigned)'0');
    }

    *i_ptr = i;
    return count;
}

static int _process_pack_format(KOS_STACK_FRAME         *frame,
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

        _pack_format_skip_spaces(frame, fmt_str, &i_fmt);

        if (i_fmt >= fmt_size)
            break;

        c = KOS_string_get_char_code(frame, fmt_str, (int)i_fmt++);
        assert(c != ~0U);

        if (c >= '0' && c <= '9') {
            --i_fmt;
            count = _pack_format_get_count(frame, fmt_str, &i_fmt);
            assert(count != ~0U);

            _pack_format_skip_spaces(frame, fmt_str, &i_fmt);

            if (i_fmt >= fmt_size)
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));

            c = KOS_string_get_char_code(frame, fmt_str, (int)i_fmt++);
            assert(c != ~0U);
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
                _pack_format_skip_spaces(frame, fmt_str, &i_fmt);
                next_c = (i_fmt < fmt_size) ? KOS_string_get_char_code(frame, fmt_str, (int)i_fmt) : ~0U;
                if (next_c >= '0' && next_c <= '9') {
                    size = _pack_format_get_count(frame, fmt_str, &i_fmt);
                }
                else if (c == 's') {
                    size = ~0U;
                }
                else
                    RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));
                break;
            }

            default:
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));
        }

        if (c != '<' && c != '>')
            TRY(handler(frame, fmt, buffer_obj, (char)c, size, count));
    }

_error:
    return error;
}

static int _pack_format(KOS_STACK_FRAME         *frame,
                        struct _KOS_PACK_FORMAT *fmt,
                        KOS_OBJ_PTR              buffer_obj,
                        char                     value_fmt,
                        unsigned                 size,
                        unsigned                 count)
{
    int                error = KOS_SUCCESS;
    int                big_end;
    uint8_t           *dst   = 0;
    struct _KOS_VECTOR str_buf;

    _KOS_vector_init(&str_buf);

    if (fmt->idx < 0) {
        KOS_OBJ_PTR obj = fmt->data;

        fmt->idx = 1;

        if (KOS_get_array_size(obj) > 1) {

            obj = KOS_array_read(frame, obj, 1);

            if (IS_TYPE(OBJ_ARRAY, obj)) {
                fmt->data = obj;
                fmt->idx  = 0;
            }
        }
    }

    assert(size != ~0U || value_fmt == 's');

    if (size != ~0U && size && count) {
        dst = KOS_buffer_make_room(frame, buffer_obj, size * count);
        if ( ! dst)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else if (size == ~0U && value_fmt != 's')
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));

    big_end = fmt->big_end;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);

            if (count)
                memset(dst, 0, size * count);
            break;

        case 'u':
            /* fall through */
        case 'i': {

            if (size != 1 && size != 2 && size != 4 && size != 8)
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_enough_pack_values));

            for ( ; count; count--) {
                unsigned    i;
                int64_t     value;
                KOS_OBJ_PTR value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);

                TRY_OBJPTR(value_obj);

                if ( ! IS_NUMERIC_OBJ(value_obj))
                    RAISE_EXCEPTION(TO_OBJPTR(&str_err_bad_pack_value));

                TRY(KOS_get_integer(frame, value_obj, &value));

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

            if (size != 4 && size != 8)
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_enough_pack_values));

            for ( ; count; count--) {
                unsigned    i;
                KOS_OBJ_PTR value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                double      value     = 0;
                uint64_t    out_val;

                TRY_OBJPTR(value_obj);

                if (IS_SMALL_INT(value_obj))
                    value = (double)GET_SMALL_INT(value_obj);
                else if (GET_OBJ_TYPE(value_obj) == OBJ_INTEGER)
                    value = (double)OBJPTR(KOS_INTEGER, value_obj)->number;
                else if (GET_OBJ_TYPE(value_obj) == OBJ_FLOAT)
                    value = OBJPTR(KOS_FLOAT, value_obj)->number;
                else
                    RAISE_EXCEPTION(TO_OBJPTR(&str_err_bad_pack_value));

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

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_enough_pack_values));

            for ( ; count; count--) {
                KOS_OBJ_PTR value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                uint8_t    *data      = 0;
                uint32_t    data_size;
                uint32_t    copy_size;

                TRY_OBJPTR(value_obj);

                if ( ! IS_TYPE(OBJ_BUFFER, value_obj))
                    RAISE_EXCEPTION(TO_OBJPTR(&str_err_bad_pack_value));

                data_size = KOS_get_buffer_size(value_obj);
                if (data_size)
                    data = KOS_buffer_data(value_obj);

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

        case 's':
            /* fall through */
        default: {

            assert(value_fmt == 's');

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_enough_pack_values));

            for ( ; count; count--) {
                KOS_OBJ_PTR value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                uint32_t    copy_size;

                TRY_OBJPTR(value_obj);

                if ( ! IS_STRING_OBJ(value_obj))
                    RAISE_EXCEPTION(TO_OBJPTR(&str_err_bad_pack_value));

                TRY(KOS_string_to_cstr_vec(frame, value_obj, &str_buf));

                copy_size = size > str_buf.size-1 ? (uint32_t)str_buf.size-1 : size;

                if (size == ~0U)
                    dst = KOS_buffer_make_room(frame, buffer_obj, copy_size);

                if (copy_size)
                    memcpy(dst, str_buf.buffer, copy_size);

                if (size != ~0U) {
                    if (copy_size < size)
                        memset(dst + copy_size, 0, size - copy_size);

                    dst += size;
                }
            }
            break;
        }
    }

_error:
    _KOS_vector_destroy(&str_buf);
    return error;
}

static int _unpack_format(KOS_STACK_FRAME         *frame,
                          struct _KOS_PACK_FORMAT *fmt,
                          KOS_OBJ_PTR              buffer_obj,
                          char                     value_fmt,
                          unsigned                 size,
                          unsigned                 count)
{
    int            error     = KOS_SUCCESS;
    uint8_t       *data      = 0;
    const uint32_t data_size = KOS_get_buffer_size(buffer_obj);
    int            big_end   = fmt->big_end;
    KOS_OBJ_PTR    obj;

    if (size == ~0U)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_pack_format));

    if (fmt->idx + size * count > data_size)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_unpack_buf_too_short));

    assert(data_size);
    data = KOS_buffer_data(buffer_obj);
    assert(data);

    data += fmt->idx;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);
            data += size * count;
            break;

        case 'f':
            /* fall through */
        case 'i':
            /* fall through */
        case 'u': {
            assert(size == 1 || size == 2 || size == 4 || size == 8);
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
                    obj = KOS_new_int(frame, ival >> shift);
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
                    obj = KOS_new_float(frame, fvalue);
                }
                else
                    obj = KOS_new_int(frame, (int64_t)value);

                TRY_OBJPTR(obj);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }

        case 'b': {
            for ( ; count; count--) {
                obj = KOS_new_buffer(frame, size);

                TRY_OBJPTR(obj);

                if (size)
                    memcpy(KOS_buffer_data(obj), data, size);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }

        case 's':
            /* fall through */
        default: {
            assert(value_fmt == 's');
            for ( ; count; count--) {
                obj = KOS_new_string(frame, (char *)data, size);

                TRY_OBJPTR(obj);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }
    }

    fmt->idx = (int)(data - KOS_buffer_data(buffer_obj));

_error:
    return error;
}

static KOS_OBJ_PTR _pack(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = KOS_array_read(frame, args_obj, 0);
    fmt.data    = args_obj;
    fmt.idx     = -1;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(fmt.fmt_str));

    if (IS_STRING_OBJ(fmt.fmt_str))
        error = _process_pack_format(frame, this_obj, _pack_format, &fmt);
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
        error = KOS_ERROR_EXCEPTION;
    }

    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _unpack(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      this_obj,
                           KOS_OBJ_PTR      args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = TO_OBJPTR(0);
    fmt.data    = TO_OBJPTR(0);
    fmt.idx     = 0;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(this_obj));

    if ( ! IS_TYPE(OBJ_BUFFER, this_obj))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_buffer));

    fmt.fmt_str = KOS_array_read(frame, args_obj, 0);
    fmt.data    = KOS_new_array(frame, 0);

    TRY_OBJPTR(fmt.fmt_str);
    TRY_OBJPTR(fmt.data);

    if (IS_NUMERIC_OBJ(fmt.fmt_str)) {
        int64_t idx = 0;

        TRY(KOS_get_integer(frame, fmt.fmt_str, &idx));

        idx = _KOS_fix_index(idx, KOS_get_buffer_size(this_obj));

        fmt.idx = (int)idx;

        fmt.fmt_str = KOS_array_read(frame, args_obj, 1);
        TRY_OBJPTR(fmt.fmt_str);
    }

    if ( ! IS_STRING_OBJ(fmt.fmt_str))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_string));

    TRY(_process_pack_format(frame, this_obj, _unpack_format, &fmt));

_error:
    return error ? TO_OBJPTR(0) : fmt.data;
}

static KOS_OBJ_PTR _copy_buffer(KOS_STACK_FRAME *frame,
                                KOS_OBJ_PTR      this_obj,
                                KOS_OBJ_PTR      args_obj)
{
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    arg        = KOS_array_read(frame, args_obj, 0);
    int64_t        dest_begin = 0;
    int64_t        src_begin  = 0;
    int64_t        src_end    = MAX_INT64;
    KOS_OBJ_PTR    src;

    if (num_args > 3) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &dest_begin));
        else if ( ! IS_TYPE(OBJ_VOID, arg))
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));

        src = KOS_array_read(frame, args_obj, 1);
        TRY_OBJPTR(src);

        arg = KOS_array_read(frame, args_obj, 2);
        TRY_OBJPTR(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_begin));
        else if ( ! IS_TYPE(OBJ_VOID, arg))
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));

        arg = KOS_array_read(frame, args_obj, 3);
        TRY_OBJPTR(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_end));
        else if ( ! IS_TYPE(OBJ_VOID, arg))
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));
    }
    else if (num_args > 2) {

        int arg_idx = 1;

        if (IS_NUMERIC_OBJ(arg) || GET_OBJ_TYPE(arg) == OBJ_VOID) {

            arg_idx = 2;

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &dest_begin));

            src = KOS_array_read(frame, args_obj, 1);
            TRY_OBJPTR(src);
        }
        else
            src = arg;

        arg = KOS_array_read(frame, args_obj, arg_idx);
        TRY_OBJPTR(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_begin));
        else if ( ! IS_TYPE(OBJ_VOID, arg))
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));

        if (arg_idx == 1) {

            arg = KOS_array_read(frame, args_obj, arg_idx+1);
            TRY_OBJPTR(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &src_end));
            else if ( ! IS_TYPE(OBJ_VOID, arg))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));
        }
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg) || GET_OBJ_TYPE(arg) == OBJ_VOID) {

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &dest_begin));

            src = KOS_array_read(frame, args_obj, 1);
            TRY_OBJPTR(src);
        }
        else {

            src = arg;

            arg = KOS_array_read(frame, args_obj, 1);
            TRY_OBJPTR(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &src_begin));
            else if ( ! IS_TYPE(OBJ_VOID, arg))
                RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));
        }
    }
    else {

        src        = arg;
        dest_begin = 0;
        src_begin  = 0;
        src_end    = MAX_INT64;
    }

    error = KOS_buffer_copy(frame, this_obj, dest_begin, src, src_begin, src_end);

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _reserve(KOS_STACK_FRAME *frame,
                            KOS_OBJ_PTR      this_obj,
                            KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR size_obj;
    int64_t     size;

    size_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJPTR(size_obj);

    TRY(KOS_get_integer(frame, size_obj, &size));

    if (IS_TYPE(OBJ_BUFFER, this_obj)) {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_buffer_size));

        TRY(KOS_buffer_reserve(frame, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_array_size));

        TRY(KOS_array_reserve(frame, this_obj, (uint32_t)size));
    }

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _insert_array(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      this_obj,
                                 KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    begin_obj;
    KOS_OBJ_PTR    end_obj;
    KOS_OBJ_PTR    src_obj;
    int64_t        begin    = 0;
    int64_t        end      = 0;
    int64_t        src_len;

    begin_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJPTR(begin_obj);

    end_obj = KOS_array_read(frame, args_obj, 1);
    TRY_OBJPTR(end_obj);

    if (num_args > 2) {
        src_obj = KOS_array_read(frame, args_obj, 2);
        TRY_OBJPTR(src_obj);
    }
    else {
        src_obj = end_obj;
        end_obj = begin_obj;
    }

    if ( ! IS_TYPE(OBJ_ARRAY, this_obj) ||
         ! IS_TYPE(OBJ_ARRAY, src_obj))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));

    if (IS_NUMERIC_OBJ(begin_obj))
        TRY(KOS_get_integer(frame, begin_obj, &begin));
    else if (IS_TYPE(OBJ_VOID, begin_obj))
        begin = num_args == 2 ? MAX_INT64 : 0;
    else
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));

    if (IS_NUMERIC_OBJ(end_obj))
        TRY(KOS_get_integer(frame, end_obj, &end));
    else if (IS_TYPE(OBJ_VOID, end_obj))
        end = MAX_INT64;
    else
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_unsup_operand_types));

    src_len = MAX_INT64;

    TRY(KOS_array_insert(frame, this_obj, begin, end, src_obj, 0, src_len));

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _pop(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      this_obj,
                        KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_PTR    ret      = TO_OBJPTR(0);
    const uint32_t num_args = KOS_get_array_size(args_obj);

    if (num_args == 0)
        ret = KOS_array_pop(frame, this_obj);

    else {
        int64_t     num = 0;
        int         idx;
        KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);
        TRY_OBJPTR(arg);

        TRY(KOS_get_integer(frame, arg, &num));

        if (num < 0 || num > INT_MAX)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_array_size));

        ret = KOS_new_array(frame, (unsigned)num);
        TRY_OBJPTR(ret);

        for (idx = (int)(num - 1); idx >= 0; idx--) {
            arg = KOS_array_pop(frame, this_obj);
            TRY_OBJPTR(arg);

            TRY(KOS_array_write(frame, ret, idx, arg));
        }
    }

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _push(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_PTR    ret      = KOS_VOID;
    uint32_t       i;

    for (i = 0; i < num_args; i++) {
        uint32_t    idx      = ~0U;
        KOS_OBJ_PTR elem_obj = KOS_array_read(frame, args_obj, (int)i);
        TRY_OBJPTR(elem_obj);

        TRY(KOS_array_push(frame, this_obj, elem_obj, &idx));

        if (i == 0) {
            ret = KOS_new_int(frame, (int64_t)idx);
            TRY_OBJPTR(ret);
        }
    }

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _get_char_code(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      this_obj,
                                  KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    KOS_OBJ_PTR arg   = KOS_array_read(frame, args_obj, 0);
    int64_t     idx;
    unsigned    code;

    TRY_OBJPTR(arg);

    TRY(KOS_get_integer(frame, arg, &idx));

    if (idx < INT_MIN || idx > INT_MAX)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_string_idx));

    code = KOS_string_get_char_code(frame, this_obj, (int)idx);
    if (code == ~0U)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    ret = KOS_new_int(frame, (int64_t)code);

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _get_string_size(KOS_STACK_FRAME *frame,
                                    KOS_OBJ_PTR      this_obj,
                                    KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_STRING_OBJ(this_obj))
        ret = KOS_new_int(frame, (int64_t)KOS_get_string_length(this_obj));
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_function_name(KOS_STACK_FRAME *frame,
                                      KOS_OBJ_PTR      this_obj,
                                      KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_FUNCTION *const func = OBJPTR(KOS_FUNCTION, this_obj);

        /* TODO add builtin function name */
        if (IS_BAD_PTR(func->module) || func->instr_offs == ~0U)
            ret = TO_OBJPTR(&str_builtin);
        else
            ret = KOS_module_addr_to_func_name(OBJPTR(KOS_MODULE, func->module),
                                               func->instr_offs);
    }
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_instructions(KOS_STACK_FRAME *frame,
                                     KOS_OBJ_PTR      this_obj,
                                     KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_FUNCTION *const func      = OBJPTR(KOS_FUNCTION, this_obj);
        uint32_t            num_instr = 0;

        if ( ! IS_BAD_PTR(func->module))
            num_instr = KOS_module_func_get_num_instr(OBJPTR(KOS_MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(frame, (int64_t)num_instr);
    }
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_code_size(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      this_obj,
                                  KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_FUNCTION *const func      = OBJPTR(KOS_FUNCTION, this_obj);
        uint32_t            code_size = 0;

        if ( ! IS_BAD_PTR(func->module))
            code_size = KOS_module_func_get_code_size(OBJPTR(KOS_MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(frame, (int64_t)code_size);
    }
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_prototype(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      this_obj,
                                  KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_FUNCTION *const func = OBJPTR(KOS_FUNCTION, this_obj);

        ret = (KOS_OBJ_PTR)KOS_atomic_read_ptr(func->prototype);

        assert( ! IS_BAD_PTR(ret));
    }
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _get_registers(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      this_obj,
                                  KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret;

    if (IS_TYPE(OBJ_FUNCTION, this_obj)) {

        KOS_FUNCTION *const func = OBJPTR(KOS_FUNCTION, this_obj);

        ret = KOS_new_int(frame, (int64_t)func->num_regs);
    }
    else {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_function));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

int _KOS_module_lang_init(KOS_STACK_FRAME *frame)
{
    int error = KOS_SUCCESS;

    TRY_ADD_FUNCTION( frame, "print",   _print,   0);
    TRY_ADD_FUNCTION( frame, "print_",  _print_,  0);
    TRY_ADD_GENERATOR(frame, "deep",    _deep,    1);
    TRY_ADD_GENERATOR(frame, "shallow", _shallow, 1);

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

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "insert_array",  _insert_array,      2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "pop",           _pop,               0);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "push",          _push,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "reserve",       _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "resize",        _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),    "slice",         _slice,             2);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(array),    "size",          _get_array_size,    0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "copy_buffer",   _copy_buffer,       1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "fill",          _fill,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "pack",          _pack,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "reserve",       _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "resize",        _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "slice",         _slice,             2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),   "unpack",        _unpack,            1);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(buffer),   "size",          _get_buffer_size,   0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(function), "apply",         _apply,             2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(function), "set_prototype", _set_prototype,     1);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function), "instructions",  _get_instructions,  0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function), "name",          _get_function_name, 0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function), "prototype",     _get_prototype,     0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function), "registers",     _get_registers,     0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function), "size",          _get_code_size,     0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),   "get_char_code", _get_char_code,     1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),   "slice",         _slice,             2);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(string),   "size",          _get_string_size,   0);

    TRY_ADD_MEMBER_GENERATOR(frame, PROTO(void),     "iterator",      _iterator,          0);

_error:
    return error;
}

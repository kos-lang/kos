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

#include "../inc/kos_utils.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static KOS_ASCII_STRING(str_array,              "<array>");
static KOS_ASCII_STRING(str_err_invalid_string, "invalid string");
static KOS_ASCII_STRING(str_err_not_array,      "object is not an array");
static KOS_ASCII_STRING(str_err_not_number,     "object is not a number");
static KOS_ASCII_STRING(str_err_out_of_memory,  "out of memory");
static KOS_ASCII_STRING(str_false,              "false");
static KOS_ASCII_STRING(str_function,           "<function>");
static KOS_ASCII_STRING(str_object,             "<object>");
static KOS_ASCII_STRING(str_true,               "true");
static KOS_ASCII_STRING(str_void,               "void");

int KOS_get_numeric_arg(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      args_obj,
                        int              idx,
                        KOS_NUMERIC     *numeric)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR arg;

    assert( ! IS_BAD_PTR(args_obj));
    assert( ! IS_SMALL_INT(args_obj));
    assert(GET_OBJ_TYPE(args_obj) == OBJ_ARRAY);
    assert(idx < (int)KOS_get_array_size(args_obj));

    arg = KOS_array_read(frame, args_obj, idx);
    TRY_OBJPTR(arg);

    if (IS_SMALL_INT(arg)) {
        numeric->type = KOS_INTEGER_VALUE;
        numeric->u.i  = GET_SMALL_INT(arg);
    }
    else switch (GET_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            numeric->type = KOS_INTEGER_VALUE;
            numeric->u.i  = OBJPTR(KOS_INTEGER, arg)->number;
            break;

        case OBJ_FLOAT:
            numeric->type = KOS_FLOAT_VALUE;
            numeric->u.d  = OBJPTR(KOS_FLOAT, arg)->number;
            break;

        default:
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_number));
    }

_error:
    return error;
}

void KOS_print_exception(KOS_STACK_FRAME *frame)
{
    struct _KOS_VECTOR cstr;
    KOS_OBJ_PTR        exception;

    _KOS_vector_init(&cstr);

    exception = KOS_get_exception(frame);
    assert(!IS_BAD_PTR(exception));

    if (!IS_SMALL_INT(exception) && IS_STRING_OBJ(exception)) {
        KOS_clear_exception(frame);
        if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, exception, &cstr))
            fprintf(stderr, "%s\n", cstr.buffer);
    }
    else {
        KOS_OBJ_PTR formatted;

        KOS_clear_exception(frame);
        formatted = KOS_format_exception(frame, exception);
        if (IS_BAD_PTR(formatted)) {
            KOS_OBJ_PTR str;

            KOS_clear_exception(frame);

            str = KOS_object_to_string(frame, exception);

            KOS_clear_exception(frame);

            if (IS_BAD_PTR(str) || KOS_string_to_cstr_vec(frame, str, &cstr))
                fprintf(stderr, "Exception: <unable to format>\n");
            else
                fprintf(stderr, "%s\n", cstr.buffer);
        }
        else {
            uint32_t i;
            uint32_t lines;

            assert(!IS_SMALL_INT(formatted) && GET_OBJ_TYPE(formatted) == OBJ_ARRAY);

            lines = KOS_get_array_size(formatted);

            for (i = 0; i < lines; i++) {
                KOS_OBJ_PTR line = KOS_array_read(frame, formatted, (int)i);
                assert(!KOS_is_exception_pending(frame));
                if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, line, &cstr))
                    fprintf(stderr, "%s\n", cstr.buffer);
            }
        }
    }

    _KOS_vector_destroy(&cstr);
}

static int _int_to_str(KOS_STACK_FRAME    *frame,
                       int64_t             value,
                       KOS_OBJ_PTR        *str,
                       struct _KOS_VECTOR *cstr_vec)
{
    int     error = KOS_SUCCESS;
    uint8_t buf[64];
    char   *ptr   = (char *)buf;

    if (cstr_vec) {
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    snprintf(ptr, sizeof(buf), "%" PRId64, value);

    if (cstr_vec) {
        TRY(_KOS_vector_resize(cstr_vec, cstr_vec->size + strlen(ptr) +
                    (cstr_vec->size ? 0 : 1)));
    }
    else {
        KOS_OBJ_PTR ret = KOS_new_cstring(frame, ptr);
        TRY_OBJPTR(ret);
        *str = ret;
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int _float_to_str(KOS_STACK_FRAME    *frame,
                         double              value,
                         KOS_OBJ_PTR        *str,
                         struct _KOS_VECTOR *cstr_vec)
{
    int     error = KOS_SUCCESS;
    uint8_t buf[64];
    char   *ptr   = (char *)buf;

    if (cstr_vec) {
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    /* TODO don't print trailing zeroes, print with variable precision */
    snprintf(ptr, sizeof(buf), "%f", value);

    if (cstr_vec) {
        TRY(_KOS_vector_resize(cstr_vec, cstr_vec->size + strlen(ptr) +
                    (cstr_vec->size ? 0 : 1)));
    }
    else {
        KOS_OBJ_PTR ret = KOS_new_cstring(frame, ptr);
        TRY_OBJPTR(ret);
        *str = ret;
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int _vector_append_cstr(KOS_STACK_FRAME    *frame,
                               struct _KOS_VECTOR *cstr_vec,
                               const char         *str,
                               size_t              len)
{
    const size_t pos   = cstr_vec->size;
    int          error = _KOS_vector_resize(cstr_vec, pos + len + (pos ? 0 : 1));

    if (error) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        error = KOS_ERROR_EXCEPTION;
    }
    else
        memcpy(&cstr_vec->buffer[pos ? pos - 1 : pos], str, len + 1);

    return error;
}

static int _vector_append_str(KOS_STACK_FRAME    *frame,
                              struct _KOS_VECTOR *cstr_vec,
                              KOS_OBJ_PTR         obj)
{
    unsigned str_len = 0;
    size_t   pos     = cstr_vec->size;
    int      error;

    if (KOS_get_string_length(obj) > 0) {

        str_len = KOS_string_to_utf8(obj, 0, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_string));
            return KOS_ERROR_EXCEPTION;
        }
    }

    error = _KOS_vector_resize(cstr_vec, pos + str_len + (pos ? 0 : 1));

    if (error) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        return KOS_ERROR_EXCEPTION;
    }

    if (str_len)
        KOS_string_to_utf8(obj, &cstr_vec->buffer[pos ? pos - 1 : pos], str_len);

    return KOS_SUCCESS;
}

int KOS_object_to_string_or_cstr_vec(KOS_STACK_FRAME    *frame,
                                     KOS_OBJ_PTR         obj,
                                     KOS_OBJ_PTR        *str,
                                     struct _KOS_VECTOR *cstr_vec)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj));
    assert(str || cstr_vec);
    assert( ! str || ! cstr_vec);

    if (IS_BAD_PTR(obj))
        return KOS_ERROR_EXCEPTION;

    if (IS_SMALL_INT(obj))
        error = _int_to_str(frame, GET_SMALL_INT(obj), str, cstr_vec);

    else switch (GET_OBJ_TYPE(obj)) {

        case OBJ_INTEGER:
            error  =_int_to_str(frame, OBJPTR(KOS_INTEGER, obj)->number, str, cstr_vec);
            break;

        case OBJ_FLOAT:
            error = _float_to_str(frame, OBJPTR(KOS_FLOAT, obj)->number, str, cstr_vec);
            break;

        case OBJ_STRING_8:
            /* fall through */
        case OBJ_STRING_16:
            /* fall through */
        case OBJ_STRING_32:
            if (cstr_vec)
                error = _vector_append_str(frame, cstr_vec, obj);
            else
                *str = obj;
            break;

        case OBJ_VOID:
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "void", 4);
            else
                *str = TO_OBJPTR(&str_void);
            break;

        case OBJ_BOOLEAN:
            if (KOS_get_bool(obj)) {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "true", 4);
                else
                    *str = TO_OBJPTR(&str_true);
            }
            else {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "false", 5);
                else
                    *str = TO_OBJPTR(&str_false);
            }
            break;

        case OBJ_ARRAY:
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<array>", 7);
            else
                *str = TO_OBJPTR(&str_array);
            break;

        case OBJ_OBJECT:
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<object>", 8);
            else
                *str = TO_OBJPTR(&str_object);
            break;

        case OBJ_FUNCTION:
            /* fall through */
        default:
            assert(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<function>", 10);
            else
                *str = TO_OBJPTR(&str_function);
            break;
    }

    return error;
}

KOS_OBJ_PTR KOS_object_to_string(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      obj)
{
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    const int   error = KOS_object_to_string_or_cstr_vec(frame, obj, &ret, 0);

    return error ? TO_OBJPTR(0) : ret;
}

int KOS_print_to_cstr_vec(KOS_STACK_FRAME    *frame,
                          KOS_OBJ_PTR         array,
                          struct _KOS_VECTOR *cstr_vec,
                          const char         *sep,
                          unsigned            sep_len)
{
    int      error = KOS_SUCCESS;
    uint32_t len;
    uint32_t i;
    uint32_t first_sep_i;

    assert(cstr_vec);
    assert( ! IS_BAD_PTR(array));
    assert(IS_TYPE(OBJ_ARRAY, array));

    if (IS_BAD_PTR(array) || ! IS_TYPE(OBJ_ARRAY, array))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));

    first_sep_i = cstr_vec->size ? 0U : 1U;

    len = KOS_get_array_size(array);

    if (len)
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + 128U));

    for (i = 0; i < len; i++) {
        KOS_OBJ_PTR obj = KOS_array_read(frame, array, (int)i);
        TRY_OBJPTR(obj);

        if (i >= first_sep_i) {
            const size_t pos = cstr_vec->size;
            TRY(_KOS_vector_resize(cstr_vec, pos + sep_len));
            memcpy(&cstr_vec->buffer[pos-1], sep, sep_len+1);
        }

        TRY(KOS_object_to_string_or_cstr_vec(frame, obj, 0, cstr_vec));
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

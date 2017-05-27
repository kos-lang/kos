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
#include "kos_utf8.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char str_array[]              = "<array>";
static const char str_err_invalid_string[] = "invalid string";
static const char str_err_not_array[]      = "object is not an array";
static const char str_err_not_number[]     = "object is not a number";
static const char str_err_out_of_memory[]  = "out of memory";
static const char str_false[]              = "false";
static const char str_function[]           = "<function>";
static const char str_object[]             = "<object>";
static const char str_true[]               = "true";
static const char str_void[]               = "void";

static const int8_t _extra_len[256] = {
    /* 0 .. 127 */
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* <- " */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  /* <- backslash */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,
    /* 128 .. 191 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 192 .. 223 */
    3,  3,  3,  3,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    /* 224 .. 239 */
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    /* 240 .. 247 */
    8,  8,  8,  8,  8,  8,  8,  8,
    /* 148 .. 255 */
    3,  3,  3,  3,  3,  3,  3,  3
};

static const char _hex_digits[] = "0123456789abcdef";

int KOS_get_numeric_arg(KOS_FRAME    frame,
                        KOS_OBJ_ID   args_obj,
                        int          idx,
                        KOS_NUMERIC *numeric)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg;

    assert(GET_OBJ_TYPE(args_obj) == OBJ_ARRAY);
    assert(idx < (int)KOS_get_array_size(args_obj));

    arg = KOS_array_read(frame, args_obj, idx);
    TRY_OBJID(arg);

    if (IS_NUMERIC_OBJ(arg)) {

        switch (GET_NUMERIC_TYPE(arg)) {

            default:
                numeric->type = KOS_INTEGER_VALUE;
                numeric->u.i  = GET_SMALL_INT(arg);
                break;

            case OBJ_NUM_INTEGER:
                numeric->type = KOS_INTEGER_VALUE;
                numeric->u.i  = *OBJPTR(INTEGER, arg);
                break;

            case OBJ_NUM_FLOAT:
                numeric->type = KOS_FLOAT_VALUE;
                numeric->u.d  = *OBJPTR(FLOAT, arg);
                break;
        }
    }
    else
        RAISE_EXCEPTION(str_err_not_number);

_error:
    return error;
}

void KOS_print_exception(KOS_FRAME frame)
{
    struct _KOS_VECTOR cstr;
    KOS_OBJ_ID         exception;

    _KOS_vector_init(&cstr);

    exception = KOS_get_exception(frame);
    assert(!IS_BAD_PTR(exception));

    if (GET_OBJ_TYPE(exception) == OBJ_STRING) {
        KOS_clear_exception(frame);
        if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, exception, &cstr))
            fprintf(stderr, "%s\n", cstr.buffer);
    }
    else {
        KOS_OBJ_ID formatted;

        KOS_clear_exception(frame);
        formatted = KOS_format_exception(frame, exception);
        if (IS_BAD_PTR(formatted)) {
            KOS_OBJ_ID str;

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

            assert(GET_OBJ_TYPE(formatted) == OBJ_ARRAY);

            lines = KOS_get_array_size(formatted);

            for (i = 0; i < lines; i++) {
                KOS_OBJ_ID line = KOS_array_read(frame, formatted, (int)i);
                assert(!KOS_is_exception_pending(frame));
                if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, line, &cstr))
                    fprintf(stderr, "%s\n", cstr.buffer);
            }
        }
    }

    _KOS_vector_destroy(&cstr);
}

static int _int_to_str(KOS_FRAME           frame,
                       int64_t             value,
                       KOS_OBJ_ID         *str,
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
        KOS_OBJ_ID ret = KOS_new_cstring(frame, ptr);
        TRY_OBJID(ret);
        *str = ret;
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int _float_to_str(KOS_FRAME           frame,
                         double              value,
                         KOS_OBJ_ID         *str,
                         struct _KOS_VECTOR *cstr_vec)
{
    int     error = KOS_SUCCESS;
    uint8_t buf[32];
    char   *ptr   = (char *)buf;
    char   *end;

    if (cstr_vec) {
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    snprintf(ptr, sizeof(buf), "%.15f", value);

    for (end = ptr + strlen(ptr) - 1; end > ptr && *end == '0'; --end);
    if (*end == '.')
        ++end;
    ++end;

    if (cstr_vec) {
        TRY(_KOS_vector_resize(cstr_vec, cstr_vec->size + (end - ptr) +
                    (cstr_vec->size ? 0 : 1)));
    }
    else {
        KOS_OBJ_ID ret = KOS_new_string(frame, ptr, (unsigned)(end - ptr));
        TRY_OBJID(ret);
        *str = ret;
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int _vector_append_cstr(KOS_FRAME           frame,
                               struct _KOS_VECTOR *cstr_vec,
                               const char         *str,
                               size_t              len)
{
    const size_t pos   = cstr_vec->size;
    int          error = _KOS_vector_resize(cstr_vec, pos + len + (pos ? 0 : 1));

    if (error) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        error = KOS_ERROR_EXCEPTION;
    }
    else
        memcpy(&cstr_vec->buffer[pos ? pos - 1 : pos], str, len + 1);

    return error;
}

static int _vector_append_str(KOS_FRAME           frame,
                              struct _KOS_VECTOR *cstr_vec,
                              KOS_OBJ_ID          obj,
                              enum _KOS_QUOTE_STR quote_str)
{
    unsigned str_len = 0;
    size_t   pos     = cstr_vec->size;
    int      error   = KOS_SUCCESS;

    if (pos)
        --pos;

    if (KOS_get_string_length(obj) > 0) {

        str_len = KOS_string_to_utf8(obj, 0, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception_cstring(frame, str_err_invalid_string);
            return KOS_ERROR_EXCEPTION;
        }
    }

    if ( ! str_len && ! quote_str)
        return KOS_SUCCESS;

    error = _KOS_vector_resize(cstr_vec, pos + str_len + 1 + (quote_str ? 2 : 0));

    if (error) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        return KOS_ERROR_EXCEPTION;
    }

    if (quote_str) {
        cstr_vec->buffer[pos] = '"';
        ++pos;
    }

    if (str_len)
        KOS_string_to_utf8(obj, &cstr_vec->buffer[pos], str_len);

    if (quote_str) {

        unsigned i;
        unsigned extra_len = 0;

        for (i = 0; i < str_len; i++) {

            const uint8_t c = (uint8_t)cstr_vec->buffer[pos + i];

            extra_len += _extra_len[c];
        }

        if (extra_len) {

            char    *src;
            char    *dst;
            unsigned num_utf8_cont = 0;

            error = _KOS_vector_resize(cstr_vec, cstr_vec->size + extra_len);

            if (error) {
                KOS_raise_exception_cstring(frame, str_err_out_of_memory);
                return KOS_ERROR_EXCEPTION;
            }

            src = &cstr_vec->buffer[pos + str_len];
            dst = src + extra_len;

            while (src < dst) {

                const uint8_t c = (uint8_t)*(--src);

                const int esc_len = _extra_len[c];

                switch (esc_len) {

                    case -1:
                        ++num_utf8_cont;
                        break;

                    case 0:
                        *(--dst) = (char)c;
                        break;

                    case 1:
                        *(--dst) = (char)c;
                        *(--dst) = '\\';
                        break;

                    case 3: {

                        uint32_t code = c;
                        int      lo;
                        int      hi;

                        if (num_utf8_cont) {
                            assert(num_utf8_cont == 1);

                            _KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code);

                            num_utf8_cont = 0;
                        }
                        else
                            code = c;

                        lo = (int)code & 0xF;
                        hi = (int)code >> 4;

                        dst   -= 4;
                        *dst   = '\\';
                        dst[1] = 'x';
                        dst[2] = _hex_digits[hi];
                        dst[3] = _hex_digits[lo];

                        break;
                    }

                    default: {
                        uint32_t code;

                        assert(num_utf8_cont < 5);

                        _KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code);

                        *(--dst) = '}';

                        for (i = (unsigned)esc_len - 3U; i > 0; i--) {
                            *(--dst) = _hex_digits[code & 0xFU];
                            code   >>= 4;
                        }

                        dst   -= 3;
                        *dst   = '\\';
                        dst[1] = 'x';
                        dst[2] = '{';

                        num_utf8_cont = 0;
                        break;
                    }
                }
            }

            pos += extra_len;
        }
    }

    pos += str_len;

    if (quote_str) {
        cstr_vec->buffer[pos] = '"';
        ++pos;
    }

    cstr_vec->buffer[pos] = 0;

    return KOS_SUCCESS;
}

static int _make_quoted_str(KOS_FRAME           frame,
                            KOS_OBJ_ID          obj_id,
                            KOS_OBJ_ID         *str,
                            struct _KOS_VECTOR *cstr_vec)
{
    int    error;
    size_t old_size;

    assert(cstr_vec);

    old_size = cstr_vec->size;

    error = _vector_append_str(frame, cstr_vec, obj_id, KOS_QUOTE_STRINGS);

    if ( ! error) {
        const char  *buf  = &cstr_vec->buffer[old_size ? old_size - 1 : 0];
        const size_t size = cstr_vec->size - old_size - (old_size ? 0 : 1);
        KOS_OBJ_ID new_str = KOS_new_string(frame, buf, (unsigned)size);
        if (IS_BAD_PTR(new_str))
            error = KOS_ERROR_EXCEPTION;
        else
            *str = new_str;
    }

    cstr_vec->size = old_size;

    if (old_size)
        cstr_vec->buffer[old_size - 1] = 0;

    return error;
}

int KOS_object_to_string_or_cstr_vec(KOS_FRAME           frame,
                                     KOS_OBJ_ID          obj_id,
                                     enum _KOS_QUOTE_STR quote_str,
                                     KOS_OBJ_ID         *str,
                                     struct _KOS_VECTOR *cstr_vec)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj_id));
    assert(str || cstr_vec);
    assert( ! str || ! cstr_vec || quote_str);

    if (IS_BAD_PTR(obj_id))
        return KOS_ERROR_EXCEPTION;

    if (IS_SMALL_INT(obj_id))
        error = _int_to_str(frame, GET_SMALL_INT(obj_id), str, cstr_vec);

    else switch (GET_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            /* fall through */
        case OBJ_INTEGER2:
            error = _int_to_str(frame, *OBJPTR(INTEGER, obj_id), str, cstr_vec);
            break;

        case OBJ_FLOAT:
            /* fall through */
        case OBJ_FLOAT2:
            error = _float_to_str(frame, *OBJPTR(FLOAT, obj_id), str, cstr_vec);
            break;

        case OBJ_STRING:
            if ( ! str)
                error = _vector_append_str(frame, cstr_vec, obj_id, quote_str);
            else if (quote_str)
                error = _make_quoted_str(frame, obj_id, str, cstr_vec);
            else
                *str = obj_id;
            break;

        case OBJ_IMMEDIATE:
            if (obj_id == KOS_VOID) {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "void", 4);
                else
                    *str = KOS_context_get_cstring(frame, str_void);
            }
            else if (obj_id == KOS_TRUE) {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "true", 4);
                else
                    *str = KOS_context_get_cstring(frame, str_true);
            }
            else {
                assert(obj_id == KOS_FALSE);
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "false", 5);
                else
                    *str = KOS_context_get_cstring(frame, str_false);
            }
            break;

        case OBJ_ARRAY:
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<array>", 7);
            else
                *str = KOS_context_get_cstring(frame, str_array);
            break;

        case OBJ_OBJECT:
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<object>", 8);
            else
                *str = KOS_context_get_cstring(frame, str_object);
            break;

        case OBJ_FUNCTION:
            /* fall through */
        default:
            assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION);
            /* TODO */
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "<function>", 10);
            else
                *str = KOS_context_get_cstring(frame, str_function);
            break;
    }

    return error;
}

KOS_OBJ_ID KOS_object_to_string(KOS_FRAME  frame,
                                KOS_OBJ_ID obj)
{
    KOS_OBJ_ID ret   = KOS_BADPTR;
    const int  error = KOS_object_to_string_or_cstr_vec(frame, obj, KOS_DONT_QUOTE, &ret, 0);

    return error ? KOS_BADPTR : ret;
}

int KOS_print_to_cstr_vec(KOS_FRAME           frame,
                          KOS_OBJ_ID          array,
                          enum _KOS_QUOTE_STR quote_str,
                          struct _KOS_VECTOR *cstr_vec,
                          const char         *sep,
                          unsigned            sep_len)
{
    int      error = KOS_SUCCESS;
    uint32_t len;
    uint32_t i;
    uint32_t first_sep_i;

    assert(cstr_vec);
    assert(GET_OBJ_TYPE(array) == OBJ_ARRAY);

    if (GET_OBJ_TYPE(array) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    first_sep_i = cstr_vec->size ? 0U : 1U;

    len = KOS_get_array_size(array);

    if (len)
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + 128U));

    for (i = 0; i < len; i++) {
        KOS_OBJ_ID obj = KOS_array_read(frame, array, (int)i);
        TRY_OBJID(obj);

        if (i >= first_sep_i) {
            const size_t pos = cstr_vec->size;
            TRY(_KOS_vector_resize(cstr_vec, pos + sep_len));
            memcpy(&cstr_vec->buffer[pos-1], sep, sep_len+1);
        }

        TRY(KOS_object_to_string_or_cstr_vec(frame, obj, quote_str, 0, cstr_vec));
    }

_error:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

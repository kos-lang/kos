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

#include "../inc/kos_utils.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include "kos_utf8.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char str_array_close[]             = "]";
static const char str_array_comma[]             = ", ";
static const char str_array_open[]              = "[";
static const char str_buffer_close[]            = ">";
static const char str_buffer_open[]             = "<";
static const char str_builtin[]                 = "built-in";
static const char str_class_open[]              = "<class ";
static const char str_empty_array[]             = "[]";
static const char str_empty_buffer[]            = "<>";
static const char str_err_cannot_expand[]       = "cannot expand object";
static const char str_err_invalid_string[]      = "invalid string";
static const char str_err_not_array[]           = "object is not an array";
static const char str_err_not_number[]          = "object is not a number";
static const char str_err_number_out_of_range[] = "number out of range";
static const char str_err_out_of_memory[]       = "out of memory";
static const char str_err_unsup_operand_types[] = "unsupported operand types";
static const char str_false[]                   = "false";
static const char str_function_open[]           = "<function ";
static const char str_object[]                  = "<object>";
static const char str_true[]                    = "true";
static const char str_void[]                    = "void";

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

    if (IS_SMALL_INT(arg)) {
        numeric->type = KOS_INTEGER_VALUE;
        numeric->u.i  = GET_SMALL_INT(arg);
    }
    else switch (READ_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            numeric->type = KOS_INTEGER_VALUE;
            numeric->u.i  = OBJPTR(INTEGER, arg)->value;
            break;

        case OBJ_FLOAT:
            numeric->type = KOS_FLOAT_VALUE;
            numeric->u.d  = OBJPTR(FLOAT, arg)->value;
            break;

        default:
            RAISE_EXCEPTION(str_err_not_number);
            break;
    }

_error:
    return error;
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
    int      error = KOS_SUCCESS;
    uint8_t  buf[32];
    char    *ptr   = (char *)buf;
    unsigned size;

    if (cstr_vec) {
        TRY(_KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    size = _KOS_print_float(ptr, sizeof(buf), value);

    if (cstr_vec) {
        TRY(_KOS_vector_resize(cstr_vec, cstr_vec->size + size +
                    (cstr_vec->size ? 0 : 1)));
    }
    else {
        KOS_OBJ_ID ret = KOS_new_string(frame, ptr, size);
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

                        uint32_t code[1];
                        int      lo;
                        int      hi;

                        code[0] = c;

                        if (num_utf8_cont) {
                            assert(num_utf8_cont == 1);

                            _KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code[0]);

                            num_utf8_cont = 0;
                        }
                        else
                            code[0] = c;

                        lo = (int)code[0] & 0xF;
                        hi = (int)code[0] >> 4;

                        dst   -= 4;
                        *dst   = '\\';
                        dst[1] = 'x';
                        dst[2] = _hex_digits[hi];
                        dst[3] = _hex_digits[lo];

                        break;
                    }

                    default: {
                        uint32_t code[1];

                        assert(num_utf8_cont < 5);

                        _KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code[0]);

                        *(--dst) = '}';

                        for (i = (unsigned)esc_len - 3U; i > 0; i--) {
                            *(--dst) = _hex_digits[code[0] & 0xFU];
                            code[0]   >>= 4;
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

static int _vector_append_array(KOS_FRAME           frame,
                                struct _KOS_VECTOR *cstr_vec,
                                KOS_OBJ_ID          obj_id,
                                enum _KOS_QUOTE_STR quote_str)
{
    int      error;
    uint32_t length;
    uint32_t i;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

    length = KOS_get_array_size(obj_id);

    TRY(_vector_append_cstr(frame, cstr_vec, str_array_open, sizeof(str_array_open)-1));

    for (i = 0; i < length; ) {

        KOS_OBJ_ID val_id = KOS_array_read(frame, obj_id, i);
        TRY_OBJID(val_id);

        TRY(KOS_object_to_string_or_cstr_vec(frame, val_id, quote_str, 0, cstr_vec));

        ++i;

        if (i < length)
            TRY(_vector_append_cstr(frame, cstr_vec, str_array_comma, sizeof(str_array_comma)-1));
    }

    TRY(_vector_append_cstr(frame, cstr_vec, str_array_close, sizeof(str_array_close)-1));

_error:
    return error;
}

static KOS_OBJ_ID _array_to_str(KOS_FRAME           frame,
                                KOS_OBJ_ID          obj_id,
                                enum _KOS_QUOTE_STR quote_str)
{
    int        error;
    uint32_t   length;
    uint32_t   i;
    KOS_OBJ_ID ret       = KOS_BADPTR;
    KOS_OBJ_ID aux_array_id;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

    length = KOS_get_array_size(obj_id);

    if (length == 0)
        return KOS_context_get_cstring(frame, str_empty_array);

    aux_array_id = KOS_new_array(frame, length * 2 + 1);
    TRY_OBJID(aux_array_id);

    TRY(KOS_array_write(frame, aux_array_id, 0,
                KOS_context_get_cstring(frame, str_array_open)));

    for (i = 0; i < length; ) {

        KOS_OBJ_ID val_id = KOS_array_read(frame, obj_id, i);
        TRY_OBJID(val_id);

        TRY(KOS_object_to_string_or_cstr_vec(frame, val_id, quote_str, &val_id, 0));

        TRY(KOS_array_write(frame, aux_array_id, 1 + i * 2, val_id));

        ++i;

        if (i < length)
            TRY(KOS_array_write(frame, aux_array_id, i * 2,
                        KOS_context_get_cstring(frame, str_array_comma)));
    }

    TRY(KOS_array_write(frame, aux_array_id, length * 2,
                KOS_context_get_cstring(frame, str_array_close)));

    ret = KOS_string_add_many(frame, _KOS_get_array_buffer(OBJPTR(ARRAY, aux_array_id)), length * 2 + 1);

_error:
    return error ? KOS_BADPTR : ret;
}

static int _vector_append_buffer(KOS_FRAME           frame,
                                 struct _KOS_VECTOR *cstr_vec,
                                 KOS_OBJ_ID          obj_id)
{
    int            error;
    uint32_t       size;
    const uint8_t *src;
    const uint8_t *end;
    char          *dest;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);

    size = KOS_get_buffer_size(obj_id);

    error = _KOS_vector_reserve(cstr_vec, cstr_vec->size + size * 3 + 2);
    if (error)
        RAISE_EXCEPTION(str_err_out_of_memory);
    assert(sizeof(str_buffer_open)  == 2);
    assert(sizeof(str_buffer_close) == 2);

    TRY(_vector_append_cstr(frame, cstr_vec, str_buffer_open, sizeof(str_buffer_open)-1));

    dest = cstr_vec->buffer + cstr_vec->size - 1;
    src  = KOS_buffer_data(obj_id);
    end  = src + size;

    while (src < end) {

        const uint8_t b = *(src++);

        dest[0] = _hex_digits[b >> 4];
        dest[1] = _hex_digits[b & 15];
        dest[2] = ' ';

        dest += 3;
    }

    if (size) {
        *(--dest) = 0;
        cstr_vec->size += size * 3 - 1;
    }

    TRY(_vector_append_cstr(frame, cstr_vec, str_buffer_close, sizeof(str_buffer_close)-1));

_error:
    return error;
}

static KOS_OBJ_ID _buffer_to_str(KOS_FRAME  frame,
                                 KOS_OBJ_ID obj_id)
{
    int                error = KOS_SUCCESS;
    KOS_OBJ_ID         ret   = KOS_BADPTR;
    struct _KOS_VECTOR cstr_vec;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);

    if (KOS_get_buffer_size(obj_id) == 0)
        return KOS_context_get_cstring(frame, str_empty_buffer);

    _KOS_vector_init(&cstr_vec);

    TRY(_vector_append_buffer(frame, &cstr_vec, obj_id));

    assert(cstr_vec.size > 1);

    ret = KOS_new_string(frame, cstr_vec.buffer, (unsigned)cstr_vec.size - 1U);

_error:
    _KOS_vector_destroy(&cstr_vec);

    return error ? KOS_BADPTR : ret;
}

static int _vector_append_function(KOS_FRAME           frame,
                                   struct _KOS_VECTOR *cstr_vec,
                                   KOS_OBJ_ID          obj_id)
{
    int           error = KOS_SUCCESS;
    KOS_FUNCTION *func;
    char          cstr_ptr[22];

    switch (GET_OBJ_TYPE(obj_id)) {

        case OBJ_FUNCTION:
            func = OBJPTR(FUNCTION, obj_id);
            TRY(_vector_append_cstr(frame, cstr_vec, str_function_open, sizeof(str_function_open) - 1));
            break;

        default:
            assert(GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
            func = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
            TRY(_vector_append_cstr(frame, cstr_vec, str_class_open, sizeof(str_class_open) - 1));
            break;
    }

    if (func->handler) {
        /* TODO get built-in function name */
        TRY(_vector_append_cstr(frame, cstr_vec, str_builtin, sizeof(str_builtin) - 1));
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%" PRIu64 ">", (uint64_t)(uintptr_t)func->handler);
    }
    else {
        KOS_OBJ_ID name_str = KOS_module_addr_to_func_name(OBJPTR(MODULE, func->module),
                                                           func->instr_offs);
        TRY_OBJID(name_str);
        TRY(_vector_append_str(frame, cstr_vec, name_str, KOS_DONT_QUOTE));
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%x>", (unsigned)func->instr_offs);
    }
    TRY(_vector_append_cstr(frame, cstr_vec, cstr_ptr, strlen(cstr_ptr)));

_error:
    return error;
}

static KOS_OBJ_ID _function_to_str(KOS_FRAME  frame,
                                   KOS_OBJ_ID obj_id)
{
    int                    error = KOS_SUCCESS;
    KOS_OBJ_ID             ret   = KOS_BADPTR;
    KOS_FUNCTION          *func;
    const char            *str_func;
    KOS_ATOMIC(KOS_OBJ_ID) strings[3];
    char                   cstr_ptr[22];

    switch (GET_OBJ_TYPE(obj_id)) {

        case OBJ_FUNCTION:
            func     = OBJPTR(FUNCTION, obj_id);
            str_func = str_function_open;
            break;

        default:
            assert(GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
            func     = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
            str_func = str_class_open;
            break;
    }

    strings[0] = KOS_context_get_cstring(frame, str_func);
    if (func->handler) {
        /* TODO get built-in function name */
        strings[1] = KOS_context_get_cstring(frame, str_builtin);
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%" PRIu64 ">", (uint64_t)(uintptr_t)func->handler);
    }
    else {
        strings[1] = KOS_module_addr_to_func_name(OBJPTR(MODULE, func->module),
                                                  func->instr_offs);
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%x>", (unsigned)func->instr_offs);
    }
    strings[2] = KOS_new_cstring(frame, cstr_ptr);
    TRY_OBJID(strings[2]);

    ret = KOS_string_add_many(frame, strings, sizeof(strings) / sizeof(strings[0]));

_error:
    return error ? KOS_BADPTR : ret;
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

    if (IS_SMALL_INT(obj_id))
        error = _int_to_str(frame, GET_SMALL_INT(obj_id), str, cstr_vec);

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            error = _int_to_str(frame, OBJPTR(INTEGER, obj_id)->value, str, cstr_vec);
            break;

        case OBJ_FLOAT:
            error = _float_to_str(frame, OBJPTR(FLOAT, obj_id)->value, str, cstr_vec);
            break;

        case OBJ_STRING:
            if ( ! str)
                error = _vector_append_str(frame, cstr_vec, obj_id, quote_str);
            else if (quote_str)
                error = _make_quoted_str(frame, obj_id, str, cstr_vec);
            else
                *str = obj_id;
            break;

        case OBJ_VOID:
            /* fall through */
        default:
            assert(READ_OBJ_TYPE(obj_id) == OBJ_VOID);
            if (cstr_vec)
                error = _vector_append_cstr(frame, cstr_vec, "void", 4);
            else
                *str = KOS_context_get_cstring(frame, str_void);
            break;

        case OBJ_BOOLEAN:
            if (KOS_get_bool(obj_id)) {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "true", 4);
                else
                    *str = KOS_context_get_cstring(frame, str_true);
            }
            else {
                if (cstr_vec)
                    error = _vector_append_cstr(frame, cstr_vec, "false", 5);
                else
                    *str = KOS_context_get_cstring(frame, str_false);
            }
            break;

        case OBJ_ARRAY:
            if (cstr_vec)
                error = _vector_append_array(frame, cstr_vec, obj_id, quote_str);
            else
                *str = _array_to_str(frame, obj_id, quote_str);
            break;

        case OBJ_BUFFER:
            if (cstr_vec)
                error = _vector_append_buffer(frame, cstr_vec, obj_id);
            else
                *str = _buffer_to_str(frame, obj_id);
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
        case OBJ_CLASS:
            if (cstr_vec)
                error = _vector_append_function(frame, cstr_vec, obj_id);
            else
                *str = _function_to_str(frame, obj_id);
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

        if (i >= first_sep_i && sep_len) {
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

int KOS_array_push_expand(KOS_FRAME  frame,
                          KOS_OBJ_ID array,
                          KOS_OBJ_ID value)
{
    int      error = KOS_SUCCESS;
    uint32_t cur_size;

    if (GET_OBJ_TYPE(array) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    cur_size = KOS_get_array_size(array);

    switch (GET_OBJ_TYPE(value)) {

        case OBJ_ARRAY:
            TRY(KOS_array_insert(frame, array, cur_size, cur_size,
                                 value, 0, (int32_t)KOS_get_array_size(value)));
            break;

        case OBJ_STRING: {
            const uint32_t len = KOS_get_string_length(value);
            uint32_t       i;

            TRY(KOS_array_resize(frame, array, cur_size + len));

            for (i = 0; i < len; i++) {
                KOS_OBJ_ID ch = KOS_string_get_char(frame, value, (int)i);
                TRY_OBJID(ch);
                TRY(KOS_array_write(frame, array, (int)(cur_size + i), ch));
            }
            break;
        }

        case OBJ_BUFFER: {
            const uint32_t size = KOS_get_buffer_size(value);
            uint32_t       i;
            uint8_t       *buf  = 0;

            if (size) {
                buf = KOS_buffer_data(value);
                assert(buf);
            }

            TRY(KOS_array_resize(frame, array, cur_size + size));

            for (i = 0; i < size; i++) {
                const KOS_OBJ_ID byte = TO_SMALL_INT((int)buf[i]);
                TRY(KOS_array_write(frame, array, (int)(cur_size + i), byte));
            }
            break;
        }

        case OBJ_FUNCTION: {
            enum _KOS_FUNCTION_STATE state =
                    (enum _KOS_FUNCTION_STATE)OBJPTR(FUNCTION, value)->state;

            if (state != KOS_GEN_READY && state != KOS_GEN_ACTIVE && state != KOS_GEN_DONE)
                RAISE_EXCEPTION(str_err_cannot_expand);

            if (state != KOS_GEN_DONE) {
                KOS_OBJ_ID void_obj = KOS_VOID;
                KOS_OBJ_ID gen_args = KOS_new_array(frame, 0);
                TRY_OBJID(gen_args);

                for (;;) {
                    KOS_OBJ_ID ret = KOS_call_generator(frame, value, void_obj, gen_args);
                    if (IS_BAD_PTR(ret)) { /* end of iterator */
                        if (KOS_is_exception_pending(frame))
                            error = KOS_ERROR_EXCEPTION;
                        break;
                    }
                    TRY(KOS_array_push(frame, array, ret, 0));
                }
            }
            break;
        }

        default:
            RAISE_EXCEPTION(str_err_cannot_expand);
    }

_error:
    return error;
}

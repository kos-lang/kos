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

#include "../inc/kos_string.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "kos_memory.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_threads.h"
#include "kos_unicode.h"
#include "kos_utf8.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static KOS_ASCII_STRING(_empty_string,          "");
static KOS_ASCII_STRING(str_array,              "<array>");
static KOS_ASCII_STRING(str_err_invalid_index,  "string index is out of range");
static KOS_ASCII_STRING(str_err_invalid_string, "invalid string");
static KOS_ASCII_STRING(str_err_invalid_utf8,   "invalid UTF-8 sequence");
static KOS_ASCII_STRING(str_err_not_string,     "object is not a string");
static KOS_ASCII_STRING(str_err_null_pointer,   "null pointer");
static KOS_ASCII_STRING(str_err_out_of_memory,  "out of memory");
static KOS_ASCII_STRING(str_false,              "false");
static KOS_ASCII_STRING(str_function,           "<function>");
static KOS_ASCII_STRING(str_object,             "<object>");
static KOS_ASCII_STRING(str_true,               "true");
static KOS_ASCII_STRING(str_void,               "void");

static KOS_STRING *_new_empty_string(KOS_STACK_FRAME *frame, unsigned length, enum KOS_OBJECT_TYPE type)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);
    assert(length > 0U);

    str = &_KOS_alloc_object(frame, KOS_STRING)->string;

    assert(type == OBJ_STRING_8 || type == OBJ_STRING_16 || type == OBJ_STRING_32);

    if (str) {

        const int shift = type - OBJ_STRING_8;

        str->type   = (_KOS_TYPE_STORAGE)type;
        str->hash   = 0;
        str->length = (uint16_t)length;

        if ((length << shift) > sizeof(str->data)) {
            void *ptr     = _KOS_alloc_buffer(frame, (length << shift));
            str->data.ptr = ptr;
            str->flags    = KOS_STRING_BUFFER;

            if (!ptr) {
                str->length = 0;
                str         = 0; /* object is garbage-collected */
            }
        }
        else
            str->flags = KOS_STRING_LOCAL;
    }

    return str;
}

KOS_OBJ_PTR KOS_new_cstring(KOS_STACK_FRAME *frame, const char *s)
{
    return KOS_new_string(frame, s, s ? (unsigned)strlen(s) : 0U);
}

KOS_OBJ_PTR KOS_new_string(KOS_STACK_FRAME *frame, const char *s, unsigned length)
{
    KOS_STRING *str;

    if (length) {
        uint32_t max_code;
        unsigned count = _KOS_utf8_get_len(s, length, KOS_UTF8_NO_ESCAPE, &max_code);

        if (count != ~0U) {
            assert(count <= 0xFFFFU);
            str = &_KOS_alloc_object(frame, KOS_STRING)->string;
        }
        else {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_utf8));
            str = 0;
        }

        if (str) {

            void *ptr = 0;

            enum KOS_OBJECT_TYPE type;

            if (max_code > 0xFFFFU)
                type = OBJ_STRING_32;
            else if (max_code > 0xFFU)
                type = OBJ_STRING_16;
            else
                type = OBJ_STRING_8;

            str->type   = (_KOS_TYPE_STORAGE)type;
            str->hash   = 0;
            str->length = (uint16_t)count;

            if ((length << (type - OBJ_STRING_8)) > sizeof(str->data)) {
                ptr           = _KOS_alloc_buffer(frame, length << (type - OBJ_STRING_8));
                str->data.ptr = ptr;
                str->flags    = KOS_STRING_BUFFER;
            }
            else {
                ptr        = &str->data.buf;
                str->flags = KOS_STRING_LOCAL;
            }

            if (!ptr) {
                str->length = 0;
                str         = 0; /* object is garbage-collected */
            }
            else if (type == OBJ_STRING_8) {
                if (KOS_SUCCESS != _KOS_utf8_decode_8(s, length, KOS_UTF8_NO_ESCAPE, (uint8_t *)ptr)) {
                    KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
            else if (type == OBJ_STRING_16) {
                if (KOS_SUCCESS != _KOS_utf8_decode_16(s, length, KOS_UTF8_NO_ESCAPE, (uint16_t *)ptr)) {
                    KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
            else {
                if (KOS_SUCCESS != _KOS_utf8_decode_32(s, length, KOS_UTF8_NO_ESCAPE, (uint32_t *)ptr)) {
                    KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
        }
    }
    else
        str = &_empty_string;

    return TO_OBJPTR(str);
}

KOS_OBJ_PTR KOS_new_const_ascii_cstring(KOS_STACK_FRAME *frame,
                                        const char      *s)
{
    return KOS_new_const_string(frame, s, s ? (unsigned)strlen(s) : 0U, OBJ_STRING_8);
}

KOS_OBJ_PTR KOS_new_const_ascii_string(KOS_STACK_FRAME *frame,
                                       const char      *s,
                                       unsigned         length)
{
    return KOS_new_const_string(frame, s, length, OBJ_STRING_8);
}

KOS_OBJ_PTR KOS_new_const_string(KOS_STACK_FRAME     *frame,
                                 const void          *data,
                                 unsigned             length,
                                 enum KOS_OBJECT_TYPE type)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);

    if (length) {
        str = &_KOS_alloc_object(frame, KOS_STRING)->string;

        if (str) {
            str->type     = (_KOS_TYPE_STORAGE)type;
            str->flags    = KOS_STRING_PTR;
            str->length   = (uint16_t)length;
            str->hash     = 0;
            str->data.ptr = data;
        }
    }
    else
        str = &_empty_string;

    return TO_OBJPTR(str);
}

unsigned KOS_string_to_utf8(KOS_OBJ_PTR objptr,
                            void       *buf,
                            unsigned    buf_size)
{
    unsigned    num_out = 0;
    KOS_STRING *str     = OBJPTR(KOS_STRING, objptr);
    uint8_t    *pdest   = (uint8_t *)buf;
    const void *src_buf;

    assert( ! IS_BAD_PTR(objptr) && IS_STRING_OBJ(objptr));

    switch (str->type) {

        case OBJ_STRING_8: {
            src_buf = _KOS_get_string_buffer(str);

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_8((const uint8_t *)src_buf, str->length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                if (num_out == str->length)
                    memcpy(buf, src_buf, num_out);
                else
                    _KOS_utf8_encode_8((const uint8_t *)src_buf, str->length, pdest);
            }
            break;
        }

        case OBJ_STRING_16: {
            src_buf = _KOS_get_string_buffer(str);

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_16((const uint16_t *)src_buf, str->length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                _KOS_utf8_encode_16((const uint16_t *)src_buf, str->length, pdest);
            }
            break;
        }

        default: {
            assert(str->type == OBJ_STRING_32);
            src_buf = _KOS_get_string_buffer(str);

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_32((const uint32_t *)src_buf, str->length);

            /* Fill out the buffer. */
            if (buf && num_out != ~0U) {
                assert(num_out <= buf_size);
                _KOS_utf8_encode_32((const uint32_t *)src_buf, str->length, pdest);
            }
            break;
        }
    }

    return num_out;
}

int KOS_string_to_cstr_vec(KOS_STACK_FRAME    *frame,
                           KOS_OBJ_PTR         objptr,
                           struct _KOS_VECTOR *str_vec)
{
    int      error   = KOS_SUCCESS;
    unsigned str_len = 0;

    assert( ! IS_BAD_PTR(objptr));

    if ( ! IS_STRING_OBJ(objptr)) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
        return KOS_ERROR_EXCEPTION;
    }

    if (KOS_get_string_length(objptr) > 0) {

        str_len = KOS_string_to_utf8(objptr, 0, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_string));
            return KOS_ERROR_EXCEPTION;
        }
    }

    error = _KOS_vector_resize(str_vec, str_len+1);

    if (error) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        return KOS_ERROR_EXCEPTION;
    }

    if (str_len)
        KOS_string_to_utf8(objptr, str_vec->buffer, str_len);

    str_vec->buffer[str_len] = 0;

    return error;
}

static void _init_empty_string(KOS_STRING *dest,
                               unsigned    offs,
                               KOS_STRING *src,
                               unsigned    len)
{
    if (len) {
        const unsigned dest_shift = (unsigned)dest->type - OBJ_STRING_8;

        void       *dest_buf = (void *)_KOS_get_string_buffer(dest);
        const void *src_buf  = _KOS_get_string_buffer(src);

        assert(len <= src->length);

        if (dest->type == src->type)

            memcpy((char *)dest_buf + (offs << dest_shift),
                   src_buf,
                   len << dest_shift);

        else switch (dest->type) {

            case OBJ_STRING_16: {
                uint16_t *pdest = (uint16_t *)dest_buf + offs;
                uint8_t  *psrc  = (uint8_t  *)src_buf;
                uint8_t  *pend  = psrc + len;
                assert(src->type == OBJ_STRING_8);
                for ( ; psrc != pend; ++pdest, ++psrc)
                    *pdest = *psrc;
                break;
            }

            default:
                assert(dest->type == OBJ_STRING_32);
                assert(src->type == OBJ_STRING_8 || src->type == OBJ_STRING_16);
                if (src->type == OBJ_STRING_8) {
                    uint32_t *pdest = (uint32_t *)dest_buf + offs;
                    uint8_t  *psrc  = (uint8_t  *)src_buf;
                    uint8_t  *pend  = psrc + len;
                    for ( ; psrc != pend; ++pdest, ++psrc)
                        *pdest = *psrc;
                }
                else {
                    uint32_t *pdest = (uint32_t *)dest_buf + offs;
                    uint16_t *psrc  = (uint16_t *)src_buf;
                    uint16_t *pend  = psrc + len;
                    for ( ; psrc != pend; ++pdest, ++psrc)
                        *pdest = *psrc;
                }
                break;
        }
    }
}

KOS_OBJ_PTR KOS_string_add(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      objptr_a,
                           KOS_OBJ_PTR      objptr_b)
{
    KOS_ATOMIC(KOS_OBJ_PTR) array[2];
    array[0] = objptr_a;
    array[1] = objptr_b;
    return KOS_string_add_many(frame, array, 2);
}

KOS_OBJ_PTR KOS_string_add_many(KOS_STACK_FRAME         *frame,
                                KOS_ATOMIC(KOS_OBJ_PTR) *objptr_array,
                                unsigned                 num_strings)
{
    KOS_STRING *new_str;

    if (num_strings == 1)
        new_str = OBJPTR(KOS_STRING, *objptr_array);

    else {
        enum KOS_OBJECT_TYPE     type    = OBJ_STRING_8;
        unsigned                 new_len = 0;
        KOS_ATOMIC(KOS_OBJ_PTR) *end     = objptr_array + num_strings;
        KOS_ATOMIC(KOS_OBJ_PTR) *cur_ptr;

        new_str = &_empty_string;

        for (cur_ptr = objptr_array; cur_ptr != end; ++cur_ptr) {
            KOS_OBJ_PTR          cur_str = (KOS_OBJ_PTR)KOS_atomic_read_ptr(*cur_ptr);
            enum KOS_OBJECT_TYPE cur_type;
            unsigned             cur_len;

            if (IS_BAD_PTR(cur_str) || ! IS_STRING_OBJ(cur_str)) {
                new_str = 0;
                new_len = 0;
                KOS_raise_exception(frame, IS_BAD_PTR(cur_str) ?
                        TO_OBJPTR(&str_err_null_pointer) : TO_OBJPTR(&str_err_not_string));
                break;
            }

            cur_type = GET_OBJ_TYPE(cur_str);
            cur_len  = KOS_get_string_length(cur_str);

            if (cur_type > type)
                type = cur_type;
            new_len += cur_len;
        }

        if (new_len) {
            new_str = _new_empty_string(frame, new_len, type);

            if (new_str) {
                unsigned pos = 0;
                for (cur_ptr = objptr_array; cur_ptr != end; ++cur_ptr) {
                    KOS_OBJ_PTR    str_obj = (KOS_OBJ_PTR)KOS_atomic_read_ptr(*cur_ptr);
                    KOS_STRING    *cur_str = OBJPTR(KOS_STRING, str_obj);
                    const unsigned cur_len = cur_str->length;
                    _init_empty_string(new_str, pos, cur_str, cur_len);
                    pos += cur_len;
                }
            }
        }
    }

    return TO_OBJPTR(new_str);
}

KOS_OBJ_PTR KOS_string_slice(KOS_STACK_FRAME *frame,
                             KOS_OBJ_PTR      objptr,
                             int64_t          idx_a,
                             int64_t          idx_b)
{
    KOS_STRING *new_str = 0;

    if (IS_BAD_PTR(objptr) || ! IS_STRING_OBJ(objptr))
        KOS_raise_exception(frame, IS_BAD_PTR(objptr) ?
                TO_OBJPTR(&str_err_null_pointer): TO_OBJPTR(&str_err_not_string));
    else {
        KOS_STRING                *str   = OBJPTR(KOS_STRING, objptr);
        const enum KOS_OBJECT_TYPE type  = GET_OBJ_TYPE(objptr);
        int                        shift = type - OBJ_STRING_8;
        const int64_t              len   = str->length;
        unsigned                   new_len;
        const uint8_t             *buf;

        if (len) {
            if (idx_a < 0)
                idx_a += len;

            if (idx_b < 0)
                idx_b += len;

            if (idx_a < 0)
                idx_a = 0;

            if (idx_b > len)
                idx_b = len;

            if (idx_b < idx_a)
                idx_b = idx_a;

            {
                const int64_t new_len_64 = idx_b - idx_a;
                assert(new_len_64 <= 0xFFFF);
                new_len = (unsigned)new_len_64;
            }

            if (new_len) {
                buf = (const uint8_t *)_KOS_get_string_buffer(str) + (idx_a << shift);

                if (str->flags == KOS_STRING_LOCAL || (new_len << shift) <= sizeof(str->data)) {
                    new_str = _new_empty_string(frame, new_len, type);
                    memcpy((void *)_KOS_get_string_buffer(new_str), buf, new_len << shift);
                }
                else if (str->flags == KOS_STRING_PTR)
                    new_str = OBJPTR(KOS_STRING, KOS_new_const_string(frame, buf, new_len, type));
                else {
                    new_str               = &_KOS_alloc_object(frame, KOS_STRING)->string;
                    new_str->type         = (_KOS_TYPE_STORAGE)type;
                    new_str->flags        = KOS_STRING_REF;
                    new_str->length       = (uint16_t)new_len;
                    new_str->hash         = 0;
                    new_str->data.ref.ptr = buf;
                    new_str->data.ref.str = TO_OBJPTR(str);
                }
            }
            else
                new_str = &_empty_string;
        }
        else
            new_str = &_empty_string;
    }

    return TO_OBJPTR(new_str);
}

KOS_OBJ_PTR KOS_string_get_char(KOS_STACK_FRAME *frame,
                                KOS_OBJ_PTR      objptr,
                                int              idx)
{
    KOS_STRING *new_str = 0;

    if (IS_BAD_PTR(objptr) || ! IS_STRING_OBJ(objptr))
        KOS_raise_exception(frame, IS_BAD_PTR(objptr) ?
                TO_OBJPTR(&str_err_null_pointer): TO_OBJPTR(&str_err_not_string));
    else {
        KOS_STRING                *str   = OBJPTR(KOS_STRING, objptr);
        const enum KOS_OBJECT_TYPE type  = GET_OBJ_TYPE(objptr);
        int                        shift = type - OBJ_STRING_8;
        const int                  len   = (int)str->length;
        const uint8_t             *buf;
        uint8_t                   *new_buf;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {
            buf = (const uint8_t *)_KOS_get_string_buffer(str) + (idx << shift);

            new_str = _new_empty_string(frame, 1, type);

            new_buf = (uint8_t *)_KOS_get_string_buffer(new_str);

            switch (type) {

                case OBJ_STRING_8:
                    *new_buf = *buf;
                    break;

                case OBJ_STRING_16:
                    *(uint16_t *)new_buf = *(const uint16_t *)buf;
                    break;

                default: /* OBJ_STRING_32 */
                    assert(type == OBJ_STRING_32);
                    *(uint32_t *)new_buf = *(const uint32_t *)buf;
                    break;
            }
        }
        else
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_index));
    }

    return TO_OBJPTR(new_str);
}

unsigned KOS_string_get_char_code(KOS_STACK_FRAME *frame,
                                  KOS_OBJ_PTR      objptr,
                                  int              idx)
{
    uint32_t code = ~0U;

    if (IS_BAD_PTR(objptr) || ! IS_STRING_OBJ(objptr))
        KOS_raise_exception(frame, IS_BAD_PTR(objptr) ?
                TO_OBJPTR(&str_err_null_pointer): TO_OBJPTR(&str_err_not_string));
    else {
        KOS_STRING                *str   = OBJPTR(KOS_STRING, objptr);
        const enum KOS_OBJECT_TYPE type  = GET_OBJ_TYPE(objptr);
        int                        shift = type - OBJ_STRING_8;
        const int                  len   = (int)str->length;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {
            const uint8_t *buf = (const uint8_t *)_KOS_get_string_buffer(str) + (idx << shift);

            switch (type) {

                case OBJ_STRING_8:
                    code = *buf;
                    break;

                case OBJ_STRING_16:
                    code = *(const uint16_t*)buf;
                    break;

                default: /* OBJ_STRING_32 */
                    assert(type == OBJ_STRING_32);
                    code = *(const uint32_t*)buf;
                    break;
            }
        }
        else
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_index));
    }

    return code;
}

static int _strcmp_8_16(KOS_STRING *a,
                        KOS_STRING *b)
{
    const unsigned  cmp_len = a->length < b->length ? a->length : b->length;
    const uint8_t  *pa      = (const uint8_t  *)_KOS_get_string_buffer(a);
    const uint16_t *pb      = (const uint16_t *)_KOS_get_string_buffer(b);
    const uint8_t  *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint8_t  ca = *pa;
            const uint16_t cb = *pb;
            if (ca != cb) {
                result = _KOS_unicode_compare(ca, cb);
                break;
            }
        }
        else {
            result = a->length - b->length;
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

static int _strcmp_8_32(KOS_STRING *a,
                        KOS_STRING *b)
{
    const unsigned  cmp_len = a->length < b->length ? a->length : b->length;
    const uint8_t  *pa      = (const uint8_t  *)_KOS_get_string_buffer(a);
    const uint32_t *pb      = (const uint32_t *)_KOS_get_string_buffer(b);
    const uint8_t  *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint8_t  ca = *pa;
            const uint32_t cb = *pb;
            if (ca != cb) {
                result = _KOS_unicode_compare(ca, cb);
                break;
            }
        }
        else {
            result = a->length - b->length;
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

static int _strcmp_16_32(KOS_STRING *a,
                         KOS_STRING *b)
{
    const unsigned  cmp_len = a->length < b->length ? a->length : b->length;
    const uint16_t *pa      = (const uint16_t *)_KOS_get_string_buffer(a);
    const uint32_t *pb      = (const uint32_t *)_KOS_get_string_buffer(b);
    const uint16_t *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint16_t ca = *pa;
            const uint32_t cb = *pb;
            if (ca != cb) {
                result = _KOS_unicode_compare(ca, cb);
                break;
            }
        }
        else {
            result = a->length - b->length;
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

int KOS_string_compare(KOS_OBJ_PTR objptr_a,
                       KOS_OBJ_PTR objptr_b)
{
    KOS_STRING *str_a  = OBJPTR(KOS_STRING, objptr_a);
    KOS_STRING *str_b  = OBJPTR(KOS_STRING, objptr_b);
    int         result = 0;

    assert( ! IS_BAD_PTR(objptr_a) && IS_STRING_OBJ(objptr_a));
    assert( ! IS_BAD_PTR(objptr_b) && IS_STRING_OBJ(objptr_b));

    if (str_a->type == str_b->type) {

        const unsigned cmp_len =
                str_a->length < str_b->length ? str_a->length : str_b->length;

        const uint8_t *pa    = (const uint8_t *)_KOS_get_string_buffer(str_a);
        const uint8_t *pb    = (const uint8_t *)_KOS_get_string_buffer(str_b);
        const unsigned num_b = cmp_len << (str_a->type - OBJ_STRING_8);
        const uint8_t *pend  = pa + num_b;
        const uint8_t *pend8 = (const uint8_t *)((uintptr_t)pend & ~(uintptr_t)7);

        uint32_t ca = 0;
        uint32_t cb = 0;

        if (((uintptr_t)pa & 7U) == ((uintptr_t)pb & 7U)) {

            while (((uintptr_t)pa & 7U) && *pa == *pb) {
                ++pa;
                ++pb;
            }

            while (pa < pend8 && *(const uint64_t *)pa == *(const uint64_t *)pb) {
                pa += 8;
                pb += 8;
            }
        }

        switch (str_a->type) {

            case OBJ_STRING_8:
                while (pa < pend && *pa == *pb) {
                    ++pa;
                    ++pb;
                }
                if (pa < pend) {
                    ca = *pa;
                    cb = *pb;
                }
                break;

            case OBJ_STRING_16:
                while (pa < pend && *(const uint16_t *)pa == *(const uint16_t *)pb) {
                    pa += 2;
                    pb += 2;
                }
                if (pa < pend) {
                    ca = *(const uint16_t *)pa;
                    cb = *(const uint16_t *)pb;
                }
                break;

            default: /* OBJ_STRING_32 */
                assert(str_a->type == OBJ_STRING_32);
                while (pa < pend && *(const uint32_t *)pa == *(const uint32_t *)pb) {
                    pa += 4;
                    pb += 4;
                }
                if (pa < pend) {
                    ca = *(const uint32_t *)pa;
                    cb = *(const uint32_t *)pb;
                }
                break;
        }

        if (pa < pend)
            result = _KOS_unicode_compare(ca, cb);
        else
            result = (int)str_a->length - (int)str_b->length;
    }
    else {
        const int neg = str_a->type < str_b->type ? 1 : -1;
        if (neg < 0) {
            KOS_STRING *tmp = str_a;
            str_a           = str_b;
            str_b           = tmp;
        }

        if (str_a->type == OBJ_STRING_8) {
            if (str_b->type == OBJ_STRING_16)
                result = _strcmp_8_16(str_a, str_b);
            else {
                assert(str_b->type == OBJ_STRING_32);
                result = _strcmp_8_32(str_a, str_b);
            }
        }
        else {
            assert(str_a->type == OBJ_STRING_16 && str_b->type == OBJ_STRING_32);
            result = _strcmp_16_32(str_a, str_b);
        }

        result *= neg;
    }

    return result;
}

uint32_t KOS_string_get_hash(KOS_OBJ_PTR objptr)
{
    uint32_t hash;

    KOS_STRING *str = OBJPTR(KOS_STRING, objptr);

    assert( ! IS_BAD_PTR(objptr) && IS_STRING_OBJ(objptr));

    hash = KOS_atomic_read_u32(str->hash);

    if (!hash) {

        const void *buf = _KOS_get_string_buffer(str);

        /* djb2a algorithm */

        hash = 5381;

        switch (str->type) {

            case OBJ_STRING_8: {
                const uint8_t *s   = (uint8_t *)buf;
                const uint8_t *end = s + str->length;

                while (s < end)
                    hash = (hash * 33U) ^ (uint32_t)*(s++);
                break;
            }

            case OBJ_STRING_16: {
                const uint16_t *s   = (uint16_t *)buf;
                const uint16_t *end = s + str->length;

                while (s < end)
                    hash = (hash * 33U) ^ (uint32_t)*(s++);
                break;
            }

            default: /* OBJ_STRING_32 */
                assert(str->type == OBJ_STRING_32);
                {
                    const uint32_t *s   = (uint32_t *)buf;
                    const uint32_t *end = s + str->length;

                    while (s < end)
                        hash = (hash * 33U) ^ (uint32_t)*(s++);
                }
                break;
        }

        assert(hash);

        KOS_atomic_write_u32(str->hash, hash);
    }

    return hash;
}

KOS_OBJ_PTR KOS_object_to_string(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      obj)
{
    char        buf[64];
    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    assert( ! IS_BAD_PTR(obj));

    if (IS_SMALL_INT(obj)) {
        const int64_t value = GET_SMALL_INT(obj);
        snprintf(buf, sizeof(buf), "%" PRId64, value);
        ret = KOS_new_cstring(frame, buf);
    }
    else switch (GET_OBJ_TYPE(obj)) {

        case OBJ_INTEGER:
            snprintf(buf, sizeof(buf), "%" PRId64, OBJPTR(KOS_INTEGER, obj)->number);
            ret = KOS_new_cstring(frame, buf);
            break;

        case OBJ_FLOAT:
            /* TODO don't print trailing zeroes, print with variable precision */
            snprintf(buf, sizeof(buf), "%f", OBJPTR(KOS_FLOAT, obj)->number);
            ret = KOS_new_cstring(frame, buf);
            break;

        case OBJ_STRING_8:
            /* fall through */
        case OBJ_STRING_16:
            /* fall through */
        case OBJ_STRING_32:
            ret = obj;
            break;

        case OBJ_VOID:
            ret = TO_OBJPTR(&str_void);
            break;

        case OBJ_BOOLEAN:
            if (KOS_get_bool(obj))
                ret = TO_OBJPTR(&str_true);
            else
                ret = TO_OBJPTR(&str_false);
            break;

        case OBJ_ARRAY:
            /* TODO */
            ret = TO_OBJPTR(&str_array);
            break;

        case OBJ_OBJECT:
            /* TODO */
            ret = TO_OBJPTR(&str_object);
            break;

        case OBJ_FUNCTION:
            /* fall through */
        default:
            assert(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
            ret = TO_OBJPTR(&str_function);
            break;
    }

    return ret;
}

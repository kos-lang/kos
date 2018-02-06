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

#include "../inc/kos_string.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_threads.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include "kos_unicode.h"
#include "kos_utf8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_err_array_too_large[]   = "input array too large";
static const char str_err_invalid_char_code[] = "invalid character code";
static const char str_err_invalid_index[]     = "string index is out of range";
static const char str_err_invalid_string[]    = "invalid string";
static const char str_err_invalid_utf8[]      = "invalid UTF-8 sequence";
static const char str_err_not_string[]        = "object is not a string";
static const char str_err_null_pointer[]      = "null pointer";
static const char str_err_out_of_memory[]     = "out of memory";
static const char str_err_too_many_repeats[]  = "repeated string too long";

#ifdef CONFIG_STRING16
#define _override_elem_size(size) ((size) < KOS_STRING_ELEM_16 ? KOS_STRING_ELEM_16 : (size))
#elif defined(CONFIG_STRING32)
#define _override_elem_size(size) KOS_STRING_ELEM_32
#else
#define _override_elem_size(size) (size)
#endif

static KOS_STRING *_new_empty_string(KOS_FRAME              frame,
                                     unsigned               length,
                                     enum _KOS_STRING_FLAGS elem_size)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);
    assert(length > 0U);

    str = (KOS_STRING *)_KOS_alloc_object(frame,
                                          KOS_ALLOC_DEFAULT,
                                          OBJ_STRING,
                                          sizeof(struct _KOS_STRING_LOCAL) - 1U + (length << elem_size));

    if (str) {
        assert(str->header.type == OBJ_STRING);
        str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_LOCAL;
        str->header.length = (uint16_t)length;
        str->header.hash   = 0;
    }

    return str;
}

static KOS_OBJ_ID _new_string(KOS_FRAME             frame,
                              const char           *s,
                              unsigned              length,
                              enum _KOS_UTF8_ESCAPE escape)
{
    KOS_STRING            *str;
    enum _KOS_STRING_FLAGS elem_size = KOS_STRING_ELEM_8;

    if (length) {
        uint32_t max_code;
        unsigned count = _KOS_utf8_get_len(s, length, escape, &max_code);

        if (count != ~0U) {

            assert(count <= 0xFFFFU);

            if (max_code > 0xFFFFU)
                elem_size = KOS_STRING_ELEM_32;
            else if (max_code > 0xFFU)
                elem_size = KOS_STRING_ELEM_16;
            else
                elem_size = KOS_STRING_ELEM_8;

            elem_size = _override_elem_size(elem_size);

            str = (KOS_STRING *)_KOS_alloc_object(frame,
                                                  KOS_ALLOC_DEFAULT,
                                                  OBJ_STRING,
                                                  sizeof(struct _KOS_STRING_LOCAL) - 1U + (length << elem_size));
        }
        else {
            KOS_raise_exception_cstring(frame, str_err_invalid_utf8);
            str = 0;
        }

        if (str) {

            void *ptr;

            assert(str->header.type == OBJ_STRING);

            str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_LOCAL;
            str->header.length = (uint16_t)count;
            str->header.hash   = 0;

            ptr = (void *)_KOS_get_string_buffer(str);

            if (elem_size == KOS_STRING_ELEM_8) {
                if (KOS_SUCCESS != _KOS_utf8_decode_8(s, length, escape, (uint8_t *)ptr)) {
                    KOS_raise_exception_cstring(frame, str_err_invalid_utf8);
                    str = 0; /* object is garbage-collected */
                }
            }
            else if (elem_size == KOS_STRING_ELEM_16) {
                if (KOS_SUCCESS != _KOS_utf8_decode_16(s, length, escape, (uint16_t *)ptr)) {
                    KOS_raise_exception_cstring(frame, str_err_invalid_utf8);
                    str = 0; /* object is garbage-collected */
                }
            }
            else {
                if (KOS_SUCCESS != _KOS_utf8_decode_32(s, length, escape, (uint32_t *)ptr)) {
                    KOS_raise_exception_cstring(frame, str_err_invalid_utf8);
                    str = 0; /* object is garbage-collected */
                }
            }
        }
    }
    else
        str = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);

    return OBJID(STRING, str);
}

KOS_OBJ_ID KOS_new_cstring(KOS_FRAME frame, const char *s)
{
    return _new_string(frame, s, s ? (unsigned)strlen(s) : 0U, KOS_UTF8_NO_ESCAPE);
}

KOS_OBJ_ID KOS_new_string(KOS_FRAME frame, const char *s, unsigned length)
{
    return _new_string(frame, s, length, KOS_UTF8_NO_ESCAPE);
}

KOS_OBJ_ID KOS_new_string_esc(KOS_FRAME frame, const char *s, unsigned length)
{
    return _new_string(frame, s, length, KOS_UTF8_WITH_ESCAPE);
}

KOS_OBJ_ID KOS_new_const_ascii_cstring(KOS_FRAME   frame,
                                       const char *s)
{
    return KOS_new_const_string(frame, s, s ? (unsigned)strlen(s) : 0U, KOS_STRING_ELEM_8);
}

KOS_OBJ_ID KOS_new_const_ascii_string(KOS_FRAME   frame,
                                      const char *s,
                                      unsigned    length)
{
    return KOS_new_const_string(frame, s, length, KOS_STRING_ELEM_8);
}

KOS_OBJ_ID KOS_new_const_string(KOS_FRAME              frame,
                                const void            *data,
                                unsigned               length,
                                enum _KOS_STRING_FLAGS elem_size)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);
    assert(elem_size <= KOS_STRING_ELEM_32);

    if (length) {
        str = (KOS_STRING *)_KOS_alloc_object(frame,
                                              KOS_ALLOC_DEFAULT,
                                              OBJ_STRING,
                                              sizeof(struct _KOS_STRING_PTR));

        if (str) {
            assert(str->header.type == OBJ_STRING);

            str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_PTR;
            str->header.length = (uint16_t)length;
            str->header.hash   = 0;
            str->ptr.data_ptr  = data;
        }
    }
    else
        str = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);

    return OBJID(STRING, str);
}

KOS_OBJ_ID KOS_new_string_from_codes(KOS_FRAME  frame,
                                     KOS_OBJ_ID codes)
{
    int                     error     = KOS_SUCCESS;
    uint32_t                length;
    uint32_t                i;
    KOS_ATOMIC(KOS_OBJ_ID) *codes_buf = 0;
    enum _KOS_STRING_FLAGS  elem_size = KOS_STRING_ELEM_8;
    KOS_STRING             *ret       = 0;
    void                   *str_buf;

    assert(GET_OBJ_TYPE(codes) == OBJ_ARRAY);

    length = KOS_get_array_size(codes);

    if (length > 0xFFFFU)
        RAISE_EXCEPTION(str_err_array_too_large);

    if (length) {
        codes_buf = _KOS_get_array_buffer(OBJPTR(ARRAY, codes));

        for (i = 0; i < length; i++) {

            const KOS_OBJ_ID elem = (KOS_OBJ_ID)KOS_atomic_read_ptr(codes_buf[i]);
            int64_t          code;

            if ( ! IS_NUMERIC_OBJ(elem))
                RAISE_EXCEPTION(str_err_invalid_char_code);

            TRY(KOS_get_integer(frame, elem, &code));

            if (code < 0 || code > 0x1FFFFF)
                RAISE_EXCEPTION(str_err_invalid_char_code);

            if (code > 0xFF) {
                if (code > 0xFFFF)
                    elem_size = KOS_STRING_ELEM_32;
                else if (elem_size == KOS_STRING_ELEM_8)
                    elem_size = KOS_STRING_ELEM_16;
            }
        }

        elem_size = _override_elem_size(elem_size);

        ret = _new_empty_string(frame, length, elem_size);
        if ( ! ret)
            goto _error;
    }
    else
        ret = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);

    str_buf = (void *)_KOS_get_string_buffer(ret);

    switch (elem_size) {

        case KOS_STRING_ELEM_8: {
            for (i = 0; i < length; i++) {

                const KOS_OBJ_ID elem = (KOS_OBJ_ID)KOS_atomic_read_ptr(codes_buf[i]);
                int64_t          code;

                TRY(KOS_get_integer(frame, elem, &code));

                ((uint8_t *)str_buf)[i] = (uint8_t)(uint64_t)code;
            }

            break;
        }

        case KOS_STRING_ELEM_16: {
            for (i = 0; i < length; i++) {

                const KOS_OBJ_ID elem = (KOS_OBJ_ID)KOS_atomic_read_ptr(codes_buf[i]);
                int64_t          code;

                TRY(KOS_get_integer(frame, elem, &code));

                ((uint16_t *)str_buf)[i] = (uint16_t)(uint64_t)code;
            }

            break;
        }

        default: { /* KOS_STRING_ELEM_32 */
            assert(_KOS_get_string_elem_size(ret) == KOS_STRING_ELEM_32);

            for (i = 0; i < length; i++) {

                const KOS_OBJ_ID elem = (KOS_OBJ_ID)KOS_atomic_read_ptr(codes_buf[i]);
                int64_t          code;

                TRY(KOS_get_integer(frame, elem, &code));

                ((uint32_t *)str_buf)[i] = (uint32_t)(uint64_t)code;
            }

            break;
        }
    }

_error:
    return error ? KOS_BADPTR : OBJID(STRING, ret);
}

unsigned KOS_string_to_utf8(KOS_OBJ_ID obj_id,
                            void      *buf,
                            unsigned   buf_size)
{
    unsigned    num_out = 0;
    KOS_STRING *str     = OBJPTR(STRING, obj_id);
    uint8_t    *pdest   = (uint8_t *)buf;
    const void *src_buf;

    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);

    src_buf = _KOS_get_string_buffer(str);

    switch (_KOS_get_string_elem_size(str)) {

        case KOS_STRING_ELEM_8: {

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_8((const uint8_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                if (num_out == str->header.length)
                    memcpy(buf, src_buf, num_out);
                else
                    _KOS_utf8_encode_8((const uint8_t *)src_buf, str->header.length, pdest);
            }
            break;
        }

        case KOS_STRING_ELEM_16: {

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_16((const uint16_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                _KOS_utf8_encode_16((const uint16_t *)src_buf, str->header.length, pdest);
            }
            break;
        }

        default: {
            assert(_KOS_get_string_elem_size(str) == KOS_STRING_ELEM_32);

            /* Calculate how many bytes we need. */
            num_out = _KOS_utf8_calc_buf_size_32((const uint32_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf && num_out != ~0U) {
                assert(num_out <= buf_size);
                _KOS_utf8_encode_32((const uint32_t *)src_buf, str->header.length, pdest);
            }
            break;
        }
    }

    return num_out;
}

int KOS_string_to_cstr_vec(KOS_FRAME           frame,
                           KOS_OBJ_ID          obj_id,
                           struct _KOS_VECTOR *str_vec)
{
    int      error   = KOS_SUCCESS;
    unsigned str_len = 0;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception_cstring(frame, str_err_not_string);
        return KOS_ERROR_EXCEPTION;
    }

    if (KOS_get_string_length(obj_id) > 0) {

        str_len = KOS_string_to_utf8(obj_id, 0, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception_cstring(frame, str_err_invalid_string);
            return KOS_ERROR_EXCEPTION;
        }
    }

    error = _KOS_vector_resize(str_vec, str_len+1);

    if (error) {
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        return KOS_ERROR_EXCEPTION;
    }

    if (str_len)
        KOS_string_to_utf8(obj_id, str_vec->buffer, str_len);

    str_vec->buffer[str_len] = 0;

    return error;
}

uint32_t KOS_string_get_hash(KOS_OBJ_ID obj_id)
{
    uint32_t hash;

    KOS_STRING *str = OBJPTR(STRING, obj_id);

    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);

    hash = KOS_atomic_read_u32(str->header.hash);

    if (!hash) {

        const void *buf = _KOS_get_string_buffer(str);

        /* djb2a algorithm */

        hash = 5381;

        switch (_KOS_get_string_elem_size(str)) {

            case KOS_STRING_ELEM_8: {
                const uint8_t *s   = (uint8_t *)buf;
                const uint8_t *end = s + str->header.length;

                while (s < end)
                    hash = (hash * 33U) ^ (uint32_t)*(s++);
                break;
            }

            case KOS_STRING_ELEM_16: {
                const uint16_t *s   = (uint16_t *)buf;
                const uint16_t *end = s + str->header.length;

                while (s < end)
                    hash = (hash * 33U) ^ (uint32_t)*(s++);
                break;
            }

            default: /* KOS_STRING_ELEM_32 */
                assert(_KOS_get_string_elem_size(str) == KOS_STRING_ELEM_32);
                {
                    const uint32_t *s   = (uint32_t *)buf;
                    const uint32_t *end = s + str->header.length;

                    while (s < end)
                        hash = (hash * 33U) ^ (uint32_t)*(s++);
                }
                break;
        }

        assert(hash);

        KOS_atomic_write_u32(str->header.hash, hash);
    }

    return hash;
}

static void _init_empty_string(KOS_STRING *dest,
                               unsigned    offs,
                               KOS_STRING *src,
                               unsigned    len)
{
    if (len) {
        void       *dest_buf = (void *)_KOS_get_string_buffer(dest);
        const void *src_buf  = _KOS_get_string_buffer(src);

        assert(len <= src->header.length);

        if (_KOS_get_string_elem_size(dest) == _KOS_get_string_elem_size(src)) {

            const int dest_shift = _KOS_get_string_elem_size(dest);

            memcpy((char *)dest_buf + (offs << dest_shift),
                   src_buf,
                   len << dest_shift);
        }
        else switch (_KOS_get_string_elem_size(dest)) {

            case KOS_STRING_ELEM_16: {
                uint16_t *pdest = (uint16_t *)dest_buf + offs;
                uint8_t  *psrc  = (uint8_t  *)src_buf;
                uint8_t  *pend  = psrc + len;
                assert(_KOS_get_string_elem_size(src) == KOS_STRING_ELEM_8);
                for ( ; psrc != pend; ++pdest, ++psrc)
                    *pdest = *psrc;
                break;
            }

            default:
                assert(_KOS_get_string_elem_size(dest) == KOS_STRING_ELEM_32);
                assert(_KOS_get_string_elem_size(src) == KOS_STRING_ELEM_8 ||
                       _KOS_get_string_elem_size(src) == KOS_STRING_ELEM_16);
                if (_KOS_get_string_elem_size(src) == KOS_STRING_ELEM_8) {
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

KOS_OBJ_ID KOS_string_add(KOS_FRAME  frame,
                          KOS_OBJ_ID obj_id_a,
                          KOS_OBJ_ID obj_id_b)
{
    KOS_ATOMIC(KOS_OBJ_ID) array[2];
    array[0] = obj_id_a;
    array[1] = obj_id_b;
    return KOS_string_add_many(frame, array, 2);
}

KOS_OBJ_ID KOS_string_add_many(KOS_FRAME               frame,
                               KOS_ATOMIC(KOS_OBJ_ID) *obj_id_array,
                               unsigned                num_strings)
{
    KOS_STRING *new_str;

    if (num_strings == 1)
        new_str = OBJPTR(STRING, *obj_id_array);

    else {
        enum _KOS_STRING_FLAGS  elem_size = KOS_STRING_ELEM_8;
        unsigned                new_len   = 0;
        unsigned                num_non_0 = 0;
        KOS_ATOMIC(KOS_OBJ_ID) *end       = obj_id_array + num_strings;
        KOS_ATOMIC(KOS_OBJ_ID) *cur_ptr;
        KOS_OBJ_ID              non_0_str = KOS_new_void(frame);

        new_str = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);

        for (cur_ptr = obj_id_array; cur_ptr != end; ++cur_ptr) {
            KOS_OBJ_ID             cur_str = (KOS_OBJ_ID)KOS_atomic_read_ptr(*cur_ptr);
            enum _KOS_STRING_FLAGS cur_elem_size;
            unsigned               cur_len;

            if (IS_BAD_PTR(cur_str) || GET_OBJ_TYPE(cur_str) != OBJ_STRING) {
                new_str = 0;
                new_len = 0;
                KOS_raise_exception(frame, IS_BAD_PTR(cur_str) ?
                        KOS_context_get_cstring(frame, str_err_null_pointer) : KOS_context_get_cstring(frame, str_err_not_string));
                break;
            }

            cur_elem_size = _KOS_get_string_elem_size(OBJPTR(STRING, cur_str));
            cur_len       = KOS_get_string_length(cur_str);

            if (cur_elem_size > elem_size)
                elem_size = cur_elem_size;

            new_len += cur_len;

            if (cur_len) {
                ++num_non_0;
                non_0_str = cur_str;
            }
        }

        if (num_non_0 == 1 && new_len)
            new_str = OBJPTR(STRING, non_0_str);

        else if (new_len) {
            elem_size = _override_elem_size(elem_size);

            new_str = _new_empty_string(frame, new_len, elem_size);

            if (new_str) {
                unsigned pos = 0;
                for (cur_ptr = obj_id_array; cur_ptr != end; ++cur_ptr) {
                    KOS_OBJ_ID     str_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(*cur_ptr);
                    KOS_STRING    *cur_str = OBJPTR(STRING, str_obj);
                    const unsigned cur_len = cur_str->header.length;
                    _init_empty_string(new_str, pos, cur_str, cur_len);
                    pos += cur_len;
                }
            }
        }
    }

    return OBJID(STRING, new_str);
}

KOS_OBJ_ID KOS_string_slice(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id,
                            int64_t    begin,
                            int64_t    end)
{
    KOS_STRING *new_str = 0;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(frame, IS_BAD_PTR(obj_id) ?
                KOS_context_get_cstring(frame, str_err_null_pointer): KOS_context_get_cstring(frame, str_err_not_string));
    else {
        KOS_STRING                  *str       = OBJPTR(STRING, obj_id);
        const enum _KOS_STRING_FLAGS elem_size = _KOS_get_string_elem_size(str);
        const int64_t                len       = str->header.length;
        unsigned                     new_len;
        const uint8_t               *buf;

        if (len) {
            if (begin < 0)
                begin += len;

            if (end < 0)
                end += len;

            if (begin < 0)
                begin = 0;

            if (end > len)
                end = len;

            if (end < begin)
                end = begin;

            {
                const int64_t new_len_64 = end - begin;
                assert(new_len_64 <= 0xFFFF);
                new_len = (unsigned)new_len_64;
            }

            if (new_len == len)
                new_str = OBJPTR(STRING, obj_id);
            else if (new_len) {
                buf = (const uint8_t *)_KOS_get_string_buffer(str) + (begin << elem_size);

                if ((new_len << elem_size) <= 2 * sizeof(void *)) {
                    new_str = _new_empty_string(frame, new_len, elem_size);
                    if (new_str)
                        memcpy((void *)_KOS_get_string_buffer(new_str), buf, new_len << elem_size);
                }
                else if (str->header.flags & KOS_STRING_PTR)
                    new_str = OBJPTR(STRING, KOS_new_const_string(frame, buf, new_len, elem_size));
                else {

                    new_str = (KOS_STRING *)_KOS_alloc_object(frame,
                                                              KOS_ALLOC_DEFAULT,
                                                              OBJ_STRING,
                                                              sizeof(struct _KOS_STRING_REF));

                    if (new_str) {
                        struct _KOS_STRING_REF *const ref = (struct _KOS_STRING_REF *)new_str;

                        assert(new_str->header.type == OBJ_STRING);

                        new_str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_REF;
                        new_str->header.length = (uint16_t)new_len;
                        new_str->header.hash   = 0;
                        ref->data_ptr          = buf;
                        ref->obj_id            = OBJID(STRING, str);
                    }
                }
            }
            else
                new_str = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);
        }
        else
            new_str = OBJPTR(STRING, KOS_context_from_frame(frame)->empty_string);
    }

    return OBJID(STRING, new_str);
}

KOS_OBJ_ID KOS_string_get_char(KOS_FRAME  frame,
                               KOS_OBJ_ID obj_id,
                               int        idx)
{
    KOS_STRING *new_str = 0;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(frame, IS_BAD_PTR(obj_id)
                ? KOS_context_get_cstring(frame, str_err_null_pointer)
                : KOS_context_get_cstring(frame, str_err_not_string));
    else {
        KOS_STRING                  *str       = OBJPTR(STRING, obj_id);
        const enum _KOS_STRING_FLAGS elem_size = _KOS_get_string_elem_size(str);
        const int                    len       = (int)str->header.length;
        const uint8_t               *buf;
        uint8_t                     *new_buf;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {
            buf = (const uint8_t *)_KOS_get_string_buffer(str) + (idx << elem_size);

            new_str = _new_empty_string(frame, 1, elem_size);

            if (new_str) {

                new_buf = (uint8_t *)_KOS_get_string_buffer(new_str);

                switch (elem_size) {

                    case KOS_STRING_ELEM_8:
                        *new_buf = *buf;
                        break;

                    case KOS_STRING_ELEM_16:
                        *(uint16_t *)new_buf = *(const uint16_t *)buf;
                        break;

                    default: /* KOS_STRING_ELEM_32 */
                        assert(elem_size == KOS_STRING_ELEM_32);
                        *(uint32_t *)new_buf = *(const uint32_t *)buf;
                        break;
                }
            }
        }
        else
            KOS_raise_exception_cstring(frame, str_err_invalid_index);
    }

    return OBJID(STRING, new_str);
}

unsigned KOS_string_get_char_code(KOS_FRAME  frame,
                                  KOS_OBJ_ID obj_id,
                                  int        idx)
{
    uint32_t code = ~0U;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(frame, IS_BAD_PTR(obj_id)
                ? KOS_context_get_cstring(frame, str_err_null_pointer)
                : KOS_context_get_cstring(frame, str_err_not_string));
    else {
        KOS_STRING                  *str       = OBJPTR(STRING, obj_id);
        const enum _KOS_STRING_FLAGS elem_size = _KOS_get_string_elem_size(str);
        const int                    len       = (int)str->header.length;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {
            const uint8_t *buf = (const uint8_t *)_KOS_get_string_buffer(str) + (idx << elem_size);

            switch (elem_size) {

                case KOS_STRING_ELEM_8:
                    code = *buf;
                    break;

                case KOS_STRING_ELEM_16:
                    code = *(const uint16_t*)buf;
                    break;

                default: /* KOS_STRING_ELEM_32 */
                    assert(elem_size == KOS_STRING_ELEM_32);
                    code = *(const uint32_t*)buf;
                    break;
            }
        }
        else
            KOS_raise_exception_cstring(frame, str_err_invalid_index);
    }

    return code;
}

static int _strcmp_8_16(KOS_STRING *a,
                        unsigned    a_begin,
                        unsigned    a_len,
                        KOS_STRING *b,
                        unsigned    b_begin,
                        unsigned    b_len)
{
    const unsigned  cmp_len = KOS_min(a_len, b_len);
    const uint8_t  *pa      = (const uint8_t  *)_KOS_get_string_buffer(a) + a_begin;
    const uint16_t *pb      = (const uint16_t *)_KOS_get_string_buffer(b) + b_begin;
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
            result = (int)(a_len - b_len);
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

static int _strcmp_8_32(KOS_STRING *a,
                        unsigned    a_begin,
                        unsigned    a_len,
                        KOS_STRING *b,
                        unsigned    b_begin,
                        unsigned    b_len)
{
    const unsigned  cmp_len = KOS_min(a_len, b_len);
    const uint8_t  *pa      = (const uint8_t  *)_KOS_get_string_buffer(a) + a_begin;
    const uint32_t *pb      = (const uint32_t *)_KOS_get_string_buffer(b) + b_begin;
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
            result = (int)(a_len - b_len);
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

static int _strcmp_16_32(KOS_STRING *a,
                         unsigned    a_begin,
                         unsigned    a_len,
                         KOS_STRING *b,
                         unsigned    b_begin,
                         unsigned    b_len)
{
    const unsigned  cmp_len = KOS_min(a_len, b_len);
    const uint16_t *pa      = (const uint16_t *)_KOS_get_string_buffer(a) + a_begin;
    const uint32_t *pb      = (const uint32_t *)_KOS_get_string_buffer(b) + b_begin;
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
            result = (int)(a_len - b_len);
            break;
        }
        ++pa;
        ++pb;
    }

    return result;
}

static int _compare_slice(KOS_STRING *str_a,
                          unsigned    a_begin,
                          unsigned    a_end,
                          KOS_STRING *str_b,
                          unsigned    b_begin,
                          unsigned    b_end)
{
    int      result      = 0;
    unsigned a_len       = a_end - a_begin;
    unsigned b_len       = b_end - b_begin;
    unsigned a_elem_size = _KOS_get_string_elem_size(str_a);
    unsigned b_elem_size = _KOS_get_string_elem_size(str_b);

    assert(a_end   <= str_a->header.length);
    assert(a_begin <= a_end);
    assert(b_end   <= str_b->header.length);
    assert(b_begin <= b_end);

    if (a_elem_size == b_elem_size) {

        const unsigned cmp_len = KOS_min(a_len, b_len);

        const uint8_t *pa    = (const uint8_t *)_KOS_get_string_buffer(str_a) + (a_begin << a_elem_size);
        const uint8_t *pb    = (const uint8_t *)_KOS_get_string_buffer(str_b) + (b_begin << a_elem_size);
        const unsigned num_b = cmp_len << a_elem_size;
        const uint8_t *pend  = pa + num_b;
        const uint8_t *pend8 = (const uint8_t *)((uintptr_t)pend & ~(uintptr_t)7);

        uint32_t ca = 0;
        uint32_t cb = 0;

        if (((uintptr_t)pa & 7U) == ((uintptr_t)pb & 7U) && pa + 8 < pend8) {

            while (((uintptr_t)pa & 7U) && *pa == *pb) {
                ++pa;
                ++pb;
            }

            if ( ! ((uintptr_t)pa & 7U)) {
                while (pa < pend8 && *(const uint64_t *)pa == *(const uint64_t *)pb) {
                    pa += 8;
                    pb += 8;
                }
            }
        }

        switch (a_elem_size) {

            case KOS_STRING_ELEM_8:
                while (pa < pend && *pa == *pb) {
                    ++pa;
                    ++pb;
                }
                if (pa < pend) {
                    ca = *pa;
                    cb = *pb;
                }
                break;

            case KOS_STRING_ELEM_16:
                while (pa < pend && *(const uint16_t *)pa == *(const uint16_t *)pb) {
                    pa += 2;
                    pb += 2;
                }
                if (pa < pend) {
                    ca = *(const uint16_t *)pa;
                    cb = *(const uint16_t *)pb;
                }
                break;

            default: /* KOS_STRING_ELEM_32 */
                assert(a_elem_size == KOS_STRING_ELEM_32);
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
            result = (int)a_len - (int)b_len;
    }
    else {
        const int neg = a_elem_size < b_elem_size ? 1 : -1;
        if (neg < 0) {
            unsigned    utmp;
            KOS_STRING *tmp = str_a;
            str_a           = str_b;
            str_b           = tmp;

            utmp    = a_begin;
            a_begin = b_begin;
            b_begin = utmp;

            utmp  = a_len;
            a_len = b_len;
            b_len = utmp;

            utmp        = a_elem_size;
            a_elem_size = b_elem_size;
            b_elem_size = utmp;
        }

        if (a_elem_size == KOS_STRING_ELEM_8) {
            if (b_elem_size == KOS_STRING_ELEM_16)
                result = _strcmp_8_16(str_a, a_begin, a_len, str_b, b_begin, b_len);
            else {
                assert(b_elem_size == KOS_STRING_ELEM_32);
                result = _strcmp_8_32(str_a, a_begin, a_len, str_b, b_begin, b_len);
            }
        }
        else {
            assert(a_elem_size == KOS_STRING_ELEM_16 && b_elem_size == KOS_STRING_ELEM_32);
            result = _strcmp_16_32(str_a, a_begin, a_len, str_b, b_begin, b_len);
        }

        result *= neg;
    }

    return result;
}

int KOS_string_compare(KOS_OBJ_ID obj_id_a,
                       KOS_OBJ_ID obj_id_b)
{
    unsigned len_a;
    unsigned len_b;

    assert(GET_OBJ_TYPE(obj_id_a) == OBJ_STRING);
    assert(GET_OBJ_TYPE(obj_id_b) == OBJ_STRING);

    len_a = KOS_get_string_length(obj_id_a);
    len_b = KOS_get_string_length(obj_id_b);

    return _compare_slice(OBJPTR(STRING, obj_id_a),
                          0,
                          len_a,
                          OBJPTR(STRING, obj_id_b),
                          0,
                          len_b);
}

int KOS_string_compare_slice(KOS_OBJ_ID obj_id_a,
                             int64_t    a_begin,
                             int64_t    a_end,
                             KOS_OBJ_ID obj_id_b,
                             int64_t    b_begin,
                             int64_t    b_end)
{
    int64_t len_a;
    int64_t len_b;

    assert(GET_OBJ_TYPE(obj_id_a) == OBJ_STRING);
    assert(GET_OBJ_TYPE(obj_id_b) == OBJ_STRING);

    len_a = (int64_t)KOS_get_string_length(obj_id_a);
    len_b = (int64_t)KOS_get_string_length(obj_id_b);

    if (a_begin < 0)
        a_begin += len_a;
    if (a_end < 0)
        a_end += len_a;
    if (a_begin < 0)
        a_begin = 0;
    if (a_end > len_a)
        a_end = len_a;
    if (a_end < a_begin)
        a_end = a_begin;

    if (b_begin < 0)
        b_begin += len_b;
    if (b_end < 0)
        b_end += len_b;
    if (b_begin < 0)
        b_begin = 0;
    if (b_end > len_b)
        b_end = len_b;
    if (b_end < b_begin)
        b_end = b_begin;

    return _compare_slice(OBJPTR(STRING, obj_id_a),
                          (unsigned)a_begin,
                          (unsigned)a_end,
                          OBJPTR(STRING, obj_id_b),
                          (unsigned)b_begin,
                          (unsigned)b_end);
}

static void _string_find_brute_force(KOS_OBJ_ID         obj_id_text,
                                     KOS_OBJ_ID         obj_id_pattern,
                                     enum _KOS_FIND_DIR reverse,
                                     int               *pos)
{
    const int text_len     = (int)KOS_get_string_length(obj_id_text);
    const int pat_len      = (int)KOS_get_string_length(obj_id_pattern);
    int       text_pos     = *pos;
    const int end_text_pos = reverse ? -1 : text_len - pat_len + 1;
    const int delta        = reverse ? -1 : 1;

    while (text_pos != end_text_pos) {

        if (_compare_slice(OBJPTR(STRING, obj_id_text), (unsigned)text_pos, (unsigned)(text_pos + pat_len),
                           OBJPTR(STRING, obj_id_pattern), 0, (unsigned)pat_len) == 0) {

            *pos = text_pos;
            return;
        }

        text_pos += delta;
    }

    *pos = -1;
}

int KOS_string_find(KOS_FRAME          frame,
                    KOS_OBJ_ID         obj_id_text,
                    KOS_OBJ_ID         obj_id_pattern,
                    enum _KOS_FIND_DIR reverse,
                    int               *pos)
{
    int text_len;
    int pattern_len;
    int cur_pos;

    if (GET_OBJ_TYPE(obj_id_text) != OBJ_STRING ||
        GET_OBJ_TYPE(obj_id_pattern) != OBJ_STRING) {

        KOS_raise_exception_cstring(frame, str_err_not_string);
        return KOS_ERROR_EXCEPTION;
    }

    pattern_len = (int)KOS_get_string_length(obj_id_pattern);

    if (pattern_len == 0)
        return KOS_SUCCESS;

    cur_pos  = *pos;
    text_len = (int)KOS_get_string_length(obj_id_text);

    if (cur_pos < 0 || cur_pos + pattern_len > text_len) {
        *pos = -1;
        return KOS_SUCCESS;
    }

    if (pattern_len == 1)
        return KOS_string_scan(frame, obj_id_text, obj_id_pattern, reverse, KOS_SCAN_INCLUDE, pos);

    /* TODO Optimize */

    _string_find_brute_force(obj_id_text, obj_id_pattern, reverse, pos);
    return KOS_SUCCESS;
}

int KOS_string_scan(KOS_FRAME              frame,
                    KOS_OBJ_ID             obj_id_text,
                    KOS_OBJ_ID             obj_id_pattern,
                    enum _KOS_FIND_DIR     reverse,
                    enum _KOS_SCAN_INCLUDE include,
                    int                   *pos)
{
    int     text_len;
    int     pattern_len;
    int     cur_pos;
    uint8_t text_elem_size;
    uint8_t pattern_elem_size;

    if (GET_OBJ_TYPE(obj_id_text) != OBJ_STRING ||
        GET_OBJ_TYPE(obj_id_pattern) != OBJ_STRING) {

        KOS_raise_exception_cstring(frame, str_err_not_string);
        return KOS_ERROR_EXCEPTION;
    }

    pattern_len = (int)KOS_get_string_length(obj_id_pattern);

    if (pattern_len == 0)
        return KOS_SUCCESS;

    cur_pos  = *pos;
    text_len = (int)KOS_get_string_length(obj_id_text);

    if (cur_pos < 0 || cur_pos >= text_len) {
        *pos = -1;
        return KOS_SUCCESS;
    }

    /* TODO Optimize */

    text_elem_size    = _KOS_get_string_elem_size(OBJPTR(STRING, obj_id_text));
    pattern_elem_size = _KOS_get_string_elem_size(OBJPTR(STRING, obj_id_pattern));

    if ( ! reverse && include && pattern_len == 1
        && text_elem_size    == KOS_STRING_ELEM_8
        && pattern_elem_size == KOS_STRING_ELEM_8) {

        const uint8_t *text     = (const uint8_t *)_KOS_get_string_buffer(OBJPTR(STRING, obj_id_text));
        const uint8_t *pattern  = (const uint8_t *)_KOS_get_string_buffer(OBJPTR(STRING, obj_id_pattern));
        const uint8_t *location = text + cur_pos;

        location = (const uint8_t *)memchr(location, (int)*pattern, (size_t)(text_len - cur_pos));

        *pos = location ? (int)(location - text) : -1;
        return KOS_SUCCESS;
    }
    else {
        const uint8_t *text     = (const uint8_t *)_KOS_get_string_buffer(OBJPTR(STRING, obj_id_text));
        const uint8_t *pattern  = (const uint8_t *)_KOS_get_string_buffer(OBJPTR(STRING, obj_id_pattern));
        const uint8_t *location = text + (cur_pos << text_elem_size);
        const uint8_t *text_end = reverse ? text - (1 << text_elem_size) : text + (text_len << text_elem_size);
        const int      delta    = reverse ? (int)((unsigned)-1 << text_elem_size) : (1 << text_elem_size);
        const uint32_t c_mask   = (pattern_elem_size == KOS_STRING_ELEM_8)  ? ~0xFFU :
                                  (pattern_elem_size == KOS_STRING_ELEM_16) ? ~0xFFFFU : 0U;

        for ( ; location != text_end; location += delta) {

            uint32_t code;

            switch (text_elem_size) {

                case KOS_STRING_ELEM_8:
                    code = *location;
                    break;

                case KOS_STRING_ELEM_16:
                    code = *(const uint16_t *)location;
                    if (code & c_mask)
                        continue;
                    break;

                default:
                    assert(text_elem_size == KOS_STRING_ELEM_32);
                    code = *(const uint32_t *)location;
                    if (code & c_mask)
                        continue;
                    break;
            }

            switch (pattern_elem_size) {

                case KOS_STRING_ELEM_8: {
                    const uint8_t         *pat_ptr = pattern;
                    const uint8_t         *pat_end = pat_ptr + pattern_len;
                    enum _KOS_SCAN_INCLUDE match   = KOS_SCAN_EXCLUDE;
                    for ( ; pat_ptr != pat_end; ++pat_ptr)
                        if (*pat_ptr == code) {
                            match = KOS_SCAN_INCLUDE;
                            break;
                        }
                    if (match == include) {
                        *pos = (int)((location - text) >> text_elem_size);
                        return KOS_SUCCESS;
                    }
                    break;
                }

                case KOS_STRING_ELEM_16: {
                    const uint16_t        *pat_ptr = (const uint16_t *)pattern;
                    const uint16_t        *pat_end = pat_ptr + pattern_len;
                    enum _KOS_SCAN_INCLUDE match   = KOS_SCAN_EXCLUDE;
                    for ( ; pat_ptr != pat_end; ++pat_ptr)
                        if (*pat_ptr == code) {
                            match = KOS_SCAN_INCLUDE;
                            break;
                        }
                    if (match == include) {
                        *pos = (int)((location - text) >> text_elem_size);
                        return KOS_SUCCESS;
                    }
                    break;
                }

                default: {
                    const uint32_t        *pat_ptr = (const uint32_t *)pattern;
                    const uint32_t        *pat_end = pat_ptr + pattern_len;
                    enum _KOS_SCAN_INCLUDE match   = KOS_SCAN_EXCLUDE;
                    assert(pattern_elem_size == KOS_STRING_ELEM_32);
                    for ( ; pat_ptr != pat_end; ++pat_ptr)
                        if (*pat_ptr == code) {
                            match = KOS_SCAN_INCLUDE;
                            break;
                        }
                    if (match == include) {
                        *pos = (int)((location - text) >> text_elem_size);
                        return KOS_SUCCESS;
                    }
                    break;
                }
            }
        }

        *pos = -1;
        return KOS_SUCCESS;
    }
}

KOS_OBJ_ID KOS_string_reverse(KOS_FRAME  frame,
                              KOS_OBJ_ID obj_id)
{
    KOS_STRING *ret;
    KOS_STRING *str;
    unsigned    len;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception_cstring(frame, str_err_not_string);
        return KOS_BADPTR;
    }

    len = KOS_get_string_length(obj_id);

    if (len < 2)
        return obj_id;

    str = OBJPTR(STRING, obj_id);

    ret = _new_empty_string(frame, len, _KOS_get_string_elem_size(str));
    if ( ! ret)
        return KOS_BADPTR;

    switch (_KOS_get_string_elem_size(str)) {

        case KOS_STRING_ELEM_8: {
            const uint8_t *src  = (const uint8_t *)_KOS_get_string_buffer(str);
            const uint8_t *end  = src + len;
            uint8_t       *dest = (uint8_t *)_KOS_get_string_buffer(ret) + len - 1;
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }

        case KOS_STRING_ELEM_16: {
            const uint16_t *src  = (const uint16_t *)_KOS_get_string_buffer(str);
            const uint16_t *end  = src + len;
            uint16_t       *dest = (uint16_t *)_KOS_get_string_buffer(ret) + len - 1;
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }

        default: {
            const uint32_t *src  = (const uint32_t *)_KOS_get_string_buffer(str);
            const uint32_t *end  = src + len;
            uint32_t       *dest = (uint32_t *)_KOS_get_string_buffer(ret) + len - 1;
            assert(_KOS_get_string_elem_size(str) == KOS_STRING_ELEM_32);
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }
    }

    return OBJID(STRING, ret);
}

KOS_OBJ_ID KOS_string_repeat(KOS_FRAME  frame,
                             KOS_OBJ_ID obj_id,
                             unsigned   num_repeat)
{
    unsigned               len;
    enum _KOS_STRING_FLAGS elem_size;
    KOS_STRING            *in_str;
    KOS_STRING            *new_str;
    uint8_t               *in_buf;
    uint8_t               *new_buf;
    uint8_t               *end_buf;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception_cstring(frame, str_err_not_string);
        return KOS_BADPTR;
    }

    len = KOS_get_string_length(obj_id);

    if (len == 0 || num_repeat == 0)
        return KOS_context_from_frame(frame)->empty_string;

    if (num_repeat == 1)
        return obj_id;

    if (num_repeat > 0xFFFFU || (len * num_repeat) > 0xFFFFU) {
        KOS_raise_exception_cstring(frame, str_err_too_many_repeats);
        return KOS_BADPTR;
    }

    in_str    = OBJPTR(STRING, obj_id);
    elem_size = _KOS_get_string_elem_size(in_str);
    new_str   = _new_empty_string(frame, len * num_repeat, elem_size);
    if ( ! new_str)
        return KOS_BADPTR;

    in_buf  = (uint8_t *)_KOS_get_string_buffer(in_str);
    new_buf = (uint8_t *)_KOS_get_string_buffer(new_str);

    len <<= elem_size;

    end_buf = new_buf + (len * num_repeat);

    while (new_buf < end_buf) {
        memcpy(new_buf, in_buf, len);
        new_buf += len;
    }

    return OBJID(STRING, new_str);
}

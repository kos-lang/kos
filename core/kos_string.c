/*
 * Copyright (c) 2014-2020 Chris Dragan
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
#include "../inc/kos_atomic.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_utils.h"
#include "kos_const_strings.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include "kos_unicode.h"
#include "kos_utf8.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_array_too_large,      "input array too large");
KOS_DECLARE_STATIC_CONST_STRING(str_err_buffer_too_large,     "input buffer too large");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_buffer_index, "buffer index is out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_char_code,    "invalid character code");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_index,        "string index is out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_string,       "invalid string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_utf8,         "invalid UTF-8 sequence");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_array,            "object is not an array");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_string,           "object is not a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_null_ptr,             "null pointer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_string_too_long,      "string too long");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_many_repeats,     "repeated string too long");

#ifdef CONFIG_STRING16
#define _override_elem_size(size) do { (size) = (size) < KOS_STRING_ELEM_16 ? KOS_STRING_ELEM_16 : (size); } while (0)
#elif defined(CONFIG_STRING32)
#define _override_elem_size(size) do { (size) = KOS_STRING_ELEM_32; } while (0)
#else
#define _override_elem_size(size) do { } while (0)
#endif

static KOS_STRING *_new_empty_string(KOS_CONTEXT      ctx,
                                     unsigned         length,
                                     KOS_STRING_FLAGS elem_size)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);
    assert(length > 0U);

    str = (KOS_STRING *)kos_alloc_object(ctx,
                                         KOS_ALLOC_MOVABLE,
                                         OBJ_STRING,
                                         sizeof(KOS_STR_HEADER) + (length << elem_size));

    if (str) {
        assert(kos_get_object_type(str->header) == OBJ_STRING);
        str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_LOCAL;
        str->header.length = (uint16_t)length;
        str->header.hash   = 0;
    }

    return str;
}

static KOS_OBJ_ID _new_string(KOS_CONTEXT     ctx,
                              const char     *s,
                              unsigned        length,
                              KOS_UTF8_ESCAPE escape)
{
    KOS_STRING      *str;
    KOS_STRING_FLAGS elem_size = KOS_STRING_ELEM_8;

    if (length > 4U * 0xFFFFU) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_string_too_long));
        str = 0;
    }
    else if (length) {
        uint32_t max_code;
        unsigned count = kos_utf8_get_len(s, length, escape, &max_code);

        if (count < 0xFFFFU) {

            if (max_code > 0xFFFFU)
                elem_size = KOS_STRING_ELEM_32;
            else if (max_code > 0xFFU)
                elem_size = KOS_STRING_ELEM_16;
            else
                elem_size = KOS_STRING_ELEM_8;

            _override_elem_size(elem_size);

            str = (KOS_STRING *)kos_alloc_object(ctx,
                                                 KOS_ALLOC_MOVABLE,
                                                 OBJ_STRING,
                                                 sizeof(KOS_STR_HEADER) + (length << elem_size));
        }
        else {
            KOS_raise_exception(ctx, count == ~0U ?
                                     KOS_CONST_ID(str_err_invalid_utf8) :
                                     KOS_CONST_ID(str_err_string_too_long));
            str = 0;
        }

        if (str) {

            void *ptr;

            assert(kos_get_object_type(str->header) == OBJ_STRING);

            str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_LOCAL;
            str->header.length = (uint16_t)count;
            str->header.hash   = 0;

            ptr = (void *)kos_get_string_buffer(str);

            if (elem_size == KOS_STRING_ELEM_8) {
                if (KOS_SUCCESS != kos_utf8_decode_8(s, length, escape, (uint8_t *)ptr)) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
            else if (elem_size == KOS_STRING_ELEM_16) {
                if (KOS_SUCCESS != kos_utf8_decode_16(s, length, escape, (uint16_t *)ptr)) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
            else {
                if (KOS_SUCCESS != kos_utf8_decode_32(s, length, escape, (uint32_t *)ptr)) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
                    str = 0; /* object is garbage-collected */
                }
            }
        }
    }
    else
        str = OBJPTR(STRING, KOS_STR_EMPTY);

    return OBJID(STRING, str);
}

KOS_OBJ_ID KOS_new_cstring(KOS_CONTEXT ctx, const char *utf8_str)
{
    return _new_string(ctx, utf8_str, utf8_str ? (unsigned)strlen(utf8_str) : 0U, KOS_UTF8_NO_ESCAPE);
}

KOS_OBJ_ID KOS_new_string(KOS_CONTEXT ctx, const char *utf8_str, unsigned length)
{
    return _new_string(ctx, utf8_str, length, KOS_UTF8_NO_ESCAPE);
}

KOS_OBJ_ID KOS_new_string_esc(KOS_CONTEXT ctx, const char *utf8_str, unsigned length)
{
    return _new_string(ctx, utf8_str, length, KOS_UTF8_WITH_ESCAPE);
}

KOS_OBJ_ID KOS_new_const_ascii_cstring(KOS_CONTEXT ctx,
                                       const char *ascii_str)
{
    return KOS_new_const_string(ctx, ascii_str, ascii_str ? (unsigned)strlen(ascii_str) : 0U, KOS_STRING_ELEM_8);
}

KOS_OBJ_ID KOS_new_const_ascii_string(KOS_CONTEXT ctx,
                                      const char *ascii_str,
                                      unsigned    length)
{
    return KOS_new_const_string(ctx, ascii_str, length, KOS_STRING_ELEM_8);
}

KOS_OBJ_ID KOS_new_const_string(KOS_CONTEXT      ctx,
                                const void      *str_data,
                                unsigned         length,
                                KOS_STRING_FLAGS elem_size)
{
    KOS_STRING *str;

    assert(length <= 0xFFFFU);
    assert(elem_size <= KOS_STRING_ELEM_32);

    if (length) {
        str = (KOS_STRING *)kos_alloc_object(ctx,
                                             KOS_ALLOC_MOVABLE,
                                             OBJ_STRING,
                                             sizeof(struct KOS_STRING_PTR_S));

        if (str) {
            assert(kos_get_object_type(str->header) == OBJ_STRING);

            str->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_PTR;
            str->header.length = (uint16_t)length;
            str->header.hash   = 0;
            str->ptr.data_ptr  = str_data;
        }
    }
    else
        str = OBJPTR(STRING, KOS_STR_EMPTY);

    return OBJID(STRING, str);
}

KOS_OBJ_ID KOS_new_string_from_codes(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  codes)
{
    int              error     = KOS_SUCCESS;
    uint32_t         length;
    uint32_t         i;
    KOS_STRING_FLAGS elem_size = KOS_STRING_ELEM_8;
    KOS_STRING      *ret       = 0;
    void            *str_buf;

    assert(GET_OBJ_TYPE(codes) == OBJ_ARRAY);

    length = KOS_get_array_size(codes);

    if (length > 0xFFFFU)
        RAISE_EXCEPTION_STR(str_err_array_too_large);

    if (length) {
        codes = kos_get_array_storage(codes);

        for (i = 0; i < length; i++) {

            const KOS_OBJ_ID elem = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, codes)->buf[i]);
            int64_t          code;

            if ( ! IS_NUMERIC_OBJ(elem))
                RAISE_EXCEPTION_STR(str_err_invalid_char_code);

            TRY(KOS_get_integer(ctx, elem, &code));

            if (code < 0 || code > 0x1FFFFF)
                RAISE_EXCEPTION_STR(str_err_invalid_char_code);

            if (code > 0xFF) {
                if (code > 0xFFFF)
                    elem_size = KOS_STRING_ELEM_32;
                else if (elem_size == KOS_STRING_ELEM_8)
                    elem_size = KOS_STRING_ELEM_16;
            }
        }

        _override_elem_size(elem_size);

        kos_track_refs(ctx, 1, &codes);

        ret = _new_empty_string(ctx, length, elem_size);

        kos_untrack_refs(ctx, 1);

        if ( ! ret)
            goto cleanup;
    }
    else
        ret = OBJPTR(STRING, KOS_STR_EMPTY);

    str_buf = (void *)kos_get_string_buffer(ret);

    switch (elem_size) {

        case KOS_STRING_ELEM_8: {
            for (i = 0; i < length; i++) {

                int64_t          code;
                const KOS_OBJ_ID elem = KOS_atomic_read_relaxed_obj(
                                        OBJPTR(ARRAY_STORAGE, codes)->buf[i]);

                TRY(KOS_get_integer(ctx, elem, &code));

                ((uint8_t *)str_buf)[i] = (uint8_t)(uint64_t)code;
            }

            break;
        }

        case KOS_STRING_ELEM_16: {
            for (i = 0; i < length; i++) {

                int64_t          code;
                const KOS_OBJ_ID elem = KOS_atomic_read_relaxed_obj(
                                        OBJPTR(ARRAY_STORAGE, codes)->buf[i]);

                TRY(KOS_get_integer(ctx, elem, &code));

                ((uint16_t *)str_buf)[i] = (uint16_t)(uint64_t)code;
            }

            break;
        }

        default: { /* KOS_STRING_ELEM_32 */
            assert(kos_get_string_elem_size(ret) == KOS_STRING_ELEM_32);

            for (i = 0; i < length; i++) {

                int64_t          code;
                const KOS_OBJ_ID elem = KOS_atomic_read_relaxed_obj(
                                        OBJPTR(ARRAY_STORAGE, codes)->buf[i]);

                TRY(KOS_get_integer(ctx, elem, &code));

                ((uint32_t *)str_buf)[i] = (uint32_t)(uint64_t)code;
            }

            break;
        }
    }

cleanup:
    return error ? KOS_BADPTR : OBJID(STRING, ret);
}

KOS_OBJ_ID KOS_new_string_from_buffer(KOS_CONTEXT ctx,
                                      KOS_OBJ_ID  utf8_buf,
                                      unsigned    begin,
                                      unsigned    end)
{
    KOS_STRING      *str       = 0;
    KOS_STRING_FLAGS elem_size = KOS_STRING_ELEM_8;
    int              pushed    = 0;
    uint32_t         size;
    uint32_t         max_code;
    unsigned         length;
    void            *ptr;

    assert(GET_OBJ_TYPE(utf8_buf) == OBJ_BUFFER);

    size = KOS_get_buffer_size(utf8_buf);
    if ( ! size && begin == end)
        return KOS_STR_EMPTY;
    if (begin > end || end > size) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_buffer_index));
        goto cleanup;
    }

    size = end - begin;

    utf8_buf = KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, utf8_buf)->data);

    length = kos_utf8_get_len((const char *)&OBJPTR(BUFFER_STORAGE, utf8_buf)->buf[begin],
                              size, KOS_UTF8_NO_ESCAPE, &max_code);
    if (length == ~0U) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
        goto cleanup;
    }

    if (KOS_push_locals(ctx, &pushed, 1, &utf8_buf))
        goto cleanup;

    if (max_code > 0xFFFFU)
        elem_size = KOS_STRING_ELEM_32;
    else if (max_code > 0xFFU)
        elem_size = KOS_STRING_ELEM_16;
    else
        elem_size = KOS_STRING_ELEM_8;

    _override_elem_size(elem_size);

    if (length > 0xFFFFU) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_buffer_too_large));
        goto cleanup;
    }

    str = _new_empty_string(ctx, length, elem_size);
    if ( ! str)
        goto cleanup;

    ptr = (void *)kos_get_string_buffer(str);

    if (elem_size == KOS_STRING_ELEM_8) {
        if (KOS_SUCCESS != kos_utf8_decode_8((const char *)&OBJPTR(BUFFER_STORAGE, utf8_buf)->buf[begin],
                                             size, KOS_UTF8_NO_ESCAPE, (uint8_t *)ptr)) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
            str = 0; /* object is garbage-collected */
        }
    }
    else if (elem_size == KOS_STRING_ELEM_16) {
        if (KOS_SUCCESS != kos_utf8_decode_16((const char *)&OBJPTR(BUFFER_STORAGE, utf8_buf)->buf[begin],
                                              size, KOS_UTF8_NO_ESCAPE, (uint16_t *)ptr)) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
            str = 0; /* object is garbage-collected */
        }
    }
    else {
        if (KOS_SUCCESS != kos_utf8_decode_32((const char *)&OBJPTR(BUFFER_STORAGE, utf8_buf)->buf[begin],
                                              size, KOS_UTF8_NO_ESCAPE, (uint32_t *)ptr)) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_utf8));
            str = 0; /* object is garbage-collected */
        }
    }

cleanup:
    KOS_pop_locals(ctx, pushed);

    return OBJID(STRING, str);
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

    src_buf = kos_get_string_buffer(str);

    switch (kos_get_string_elem_size(str)) {

        case KOS_STRING_ELEM_8: {

            /* Calculate how many bytes we need. */
            num_out = kos_utf8_calc_buf_size_8((const uint8_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                if (num_out == str->header.length)
                    memcpy(buf, src_buf, num_out);
                else
                    kos_utf8_encode_8((const uint8_t *)src_buf, str->header.length, pdest);
            }
            break;
        }

        case KOS_STRING_ELEM_16: {

            /* Calculate how many bytes we need. */
            num_out = kos_utf8_calc_buf_size_16((const uint16_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf) {
                assert(num_out <= buf_size);
                kos_utf8_encode_16((const uint16_t *)src_buf, str->header.length, pdest);
            }
            break;
        }

        default: {
            assert(kos_get_string_elem_size(str) == KOS_STRING_ELEM_32);

            /* Calculate how many bytes we need. */
            num_out = kos_utf8_calc_buf_size_32((const uint32_t *)src_buf, str->header.length);

            /* Fill out the buffer. */
            if (buf && num_out != ~0U) {
                assert(num_out <= buf_size);
                kos_utf8_encode_32((const uint32_t *)src_buf, str->header.length, pdest);
            }
            break;
        }
    }

    return num_out;
}

int KOS_string_to_cstr_vec(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           KOS_VECTOR *str_vec)
{
    int      error   = KOS_SUCCESS;
    unsigned str_len = 0;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_ERROR_EXCEPTION;
    }

    if (KOS_get_string_length(obj_id) > 0) {

        str_len = KOS_string_to_utf8(obj_id, 0, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_string));
            return KOS_ERROR_EXCEPTION;
        }
    }

    error = kos_vector_resize(str_vec, str_len+1);

    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
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

    hash = KOS_atomic_read_relaxed_u32(str->header.hash);

    if (!hash) {

        const void *buf = kos_get_string_buffer(str);

        /* djb2a algorithm */

        hash = 5381;

        switch (kos_get_string_elem_size(str)) {

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
                assert(kos_get_string_elem_size(str) == KOS_STRING_ELEM_32);
                {
                    const uint32_t *s   = (uint32_t *)buf;
                    const uint32_t *end = s + str->header.length;

                    while (s < end)
                        hash = (hash * 33U) ^ (uint32_t)*(s++);
                }
                break;
        }

        assert(hash);

        KOS_atomic_write_relaxed_u32(str->header.hash, hash);
    }

    return hash;
}

static void _init_empty_string(KOS_STRING *dest,
                               unsigned    offs,
                               KOS_STRING *src,
                               unsigned    len)
{
    if (len) {
        void       *dest_buf = (void *)kos_get_string_buffer(dest);
        const void *src_buf  = kos_get_string_buffer(src);

        assert(len <= src->header.length);

        if (kos_get_string_elem_size(dest) == kos_get_string_elem_size(src)) {

            const int dest_shift = kos_get_string_elem_size(dest);

            memcpy((char *)dest_buf + (offs << dest_shift),
                   src_buf,
                   len << dest_shift);
        }
        else switch (kos_get_string_elem_size(dest)) {

            case KOS_STRING_ELEM_16: {
                uint16_t *pdest = (uint16_t *)dest_buf + offs;
                uint8_t  *psrc  = (uint8_t  *)src_buf;
                uint8_t  *pend  = psrc + len;
                assert(kos_get_string_elem_size(src) == KOS_STRING_ELEM_8);
                for ( ; psrc != pend; ++pdest, ++psrc)
                    *pdest = *psrc;
                break;
            }

            default:
                assert(kos_get_string_elem_size(dest) == KOS_STRING_ELEM_32);
                assert(kos_get_string_elem_size(src) == KOS_STRING_ELEM_8 ||
                       kos_get_string_elem_size(src) == KOS_STRING_ELEM_16);
                if (kos_get_string_elem_size(src) == KOS_STRING_ELEM_8) {
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

KOS_OBJ_ID KOS_string_add_n(KOS_CONTEXT ctx,
                            KOS_OBJ_ID *str_id_array,
                            unsigned    num_strings)
{
    KOS_OBJ_ID new_str_id = KOS_BADPTR;

    kos_track_refs(ctx, 1, &new_str_id);

    if (num_strings == 1)
        new_str_id = *str_id_array;

    else {
        KOS_STRING_FLAGS  elem_size = KOS_STRING_ELEM_8;
        unsigned          new_len   = 0;
        unsigned          num_non_0 = 0;
        KOS_OBJ_ID *const end       = str_id_array + num_strings;
        KOS_OBJ_ID       *cur_ptr;
        KOS_OBJ_ID        non_0_str = KOS_VOID;

        new_str_id = KOS_STR_EMPTY;

        for (cur_ptr = str_id_array; cur_ptr != end; ++cur_ptr) {
            KOS_OBJ_ID       cur_str = *cur_ptr;
            KOS_STRING_FLAGS cur_elem_size;
            unsigned         cur_len;

            if (IS_BAD_PTR(cur_str) || GET_OBJ_TYPE(cur_str) != OBJ_STRING) {
                new_str_id = KOS_BADPTR;
                new_len    = 0;
                KOS_raise_exception(ctx, IS_BAD_PTR(cur_str) ?
                                         KOS_CONST_ID(str_err_null_ptr) :
                                         KOS_CONST_ID(str_err_not_string));
                break;
            }

            cur_elem_size = kos_get_string_elem_size(OBJPTR(STRING, cur_str));
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
            new_str_id = non_0_str;

        else if (new_len) {
            _override_elem_size(elem_size);

            if (new_len <= 0xFFFFU)
                new_str_id = OBJID(STRING, _new_empty_string(ctx, new_len, elem_size));
            else {
                new_str_id = KOS_BADPTR;
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_string_too_long));
            }

            if ( ! IS_BAD_PTR(new_str_id)) {

                unsigned pos = 0;

                for (cur_ptr = str_id_array; cur_ptr != end; ++cur_ptr) {
                    KOS_OBJ_ID     str_obj = *cur_ptr;
                    const unsigned cur_len = OBJPTR(STRING, str_obj)->header.length;
                    _init_empty_string(OBJPTR(STRING, new_str_id), pos, OBJPTR(STRING, str_obj), cur_len);
                    pos += cur_len;
                }
            }
        }
    }

    kos_untrack_refs(ctx, 1);

    return new_str_id;
}

KOS_OBJ_ID KOS_string_add(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  str_array_id)
{
    KOS_OBJ_ID new_str_id = KOS_BADPTR;
    unsigned   num_strings;

    if (IS_BAD_PTR(str_array_id) || GET_OBJ_TYPE(str_array_id) != OBJ_ARRAY) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
        return KOS_BADPTR;
    }

    num_strings = KOS_get_array_size(str_array_id);

    kos_track_refs(ctx, 2, &new_str_id, &str_array_id);

    if (num_strings == 1) {
        new_str_id = KOS_array_read(ctx, str_array_id, 0);

        if ( ! IS_BAD_PTR(new_str_id) && GET_OBJ_TYPE(new_str_id) != OBJ_STRING) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
            new_str_id = KOS_BADPTR;
        }
    }
    else {
        KOS_STRING_FLAGS elem_size = KOS_STRING_ELEM_8;
        unsigned         new_len   = 0;
        unsigned         num_non_0 = 0;
        unsigned         i;
        KOS_OBJ_ID       non_0_str = KOS_VOID;

        new_str_id = KOS_STR_EMPTY;

        for (i = 0; i < num_strings; ++i) {
            KOS_OBJ_ID       cur_str = KOS_array_read(ctx, str_array_id, i);
            KOS_STRING_FLAGS cur_elem_size;
            unsigned         cur_len;

            if (IS_BAD_PTR(cur_str) || GET_OBJ_TYPE(cur_str) != OBJ_STRING) {
                new_str_id = KOS_BADPTR;
                new_len    = 0;
                if ( ! IS_BAD_PTR(cur_str))
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
                break;
            }

            cur_elem_size = kos_get_string_elem_size(OBJPTR(STRING, cur_str));
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
            new_str_id = non_0_str;

        else if (new_len) {
            _override_elem_size(elem_size);

            if (new_len <= 0xFFFFU)
                new_str_id = OBJID(STRING, _new_empty_string(ctx, new_len, elem_size));
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_string_too_long));
                new_str_id = KOS_BADPTR;
            }

            if ( ! IS_BAD_PTR(new_str_id)) {

                unsigned pos = 0;

                for (i = 0; i < num_strings; ++i) {
                    KOS_OBJ_ID str_obj = KOS_array_read(ctx, str_array_id, i);
                    unsigned   cur_len;

                    if (IS_BAD_PTR(str_obj) || GET_OBJ_TYPE(str_obj) != OBJ_STRING) {
                        new_str_id = KOS_BADPTR;
                        if ( ! IS_BAD_PTR(str_obj))
                            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
                        break;
                    }

                    cur_len = OBJPTR(STRING, str_obj)->header.length;
                    _init_empty_string(OBJPTR(STRING, new_str_id), pos, OBJPTR(STRING, str_obj), cur_len);
                    pos += cur_len;
                }
            }
        }
    }

    kos_untrack_refs(ctx, 2);

    return new_str_id;
}

KOS_OBJ_ID KOS_string_slice(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            int64_t     begin,
                            int64_t     end)
{
    KOS_OBJ_ID new_str = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(ctx, IS_BAD_PTR(obj_id) ?
                                 KOS_CONST_ID(str_err_null_ptr) :
                                 KOS_CONST_ID(str_err_not_string));
    else {
        const KOS_STRING_FLAGS elem_size =
            kos_get_string_elem_size(OBJPTR(STRING, obj_id));

        const int64_t  len = OBJPTR(STRING, obj_id)->header.length;
        const uint8_t *buf;

        if (len) {
            unsigned new_len;

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
                new_str = obj_id;
            else if (new_len) {
                kos_track_refs(ctx, 1, &obj_id);

                if ((new_len << elem_size) <= 2 * sizeof(void *)) {
                    new_str = OBJID(STRING, _new_empty_string(ctx, new_len, elem_size));
                    buf = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id)) + (begin << elem_size);
                    if ( ! IS_BAD_PTR(new_str))
                        memcpy((void *)kos_get_string_buffer(OBJPTR(STRING, new_str)),
                               buf,
                               new_len << elem_size);
                }
                /* if KOS_STRING_PTR */
                else if ( ! (OBJPTR(STRING, obj_id)->header.flags & ~KOS_STRING_ELEM_MASK)) {
                    buf = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id)) + (begin << elem_size);
                    new_str = KOS_new_const_string(ctx, buf, new_len, elem_size);
                }
                else {

                    new_str = OBJID(STRING, (KOS_STRING *)
                              kos_alloc_object(ctx,
                                               KOS_ALLOC_MOVABLE,
                                               OBJ_STRING,
                                               sizeof(struct KOS_STRING_REF_S)));

                    if ( ! IS_BAD_PTR(new_str)) {
                        struct KOS_STRING_REF_S *const ref = &OBJPTR(STRING, new_str)->ref;

                        buf = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id)) + (begin << elem_size);

                        assert(READ_OBJ_TYPE(new_str) == OBJ_STRING);

                        OBJPTR(STRING, new_str)->header.flags  = (uint8_t)elem_size | (uint8_t)KOS_STRING_REF;
                        OBJPTR(STRING, new_str)->header.length = (uint16_t)new_len;
                        OBJPTR(STRING, new_str)->header.hash   = 0;
                        ref->data_ptr                          = buf;

                        if (OBJPTR(STRING, obj_id)->header.flags & KOS_STRING_REF)
                            ref->obj_id = OBJPTR(STRING, obj_id)->ref.obj_id;
                        else
                            ref->obj_id = obj_id;
                    }
                }

                kos_untrack_refs(ctx, 1);
            }
            else
                new_str = KOS_STR_EMPTY;
        }
        else
            new_str = KOS_STR_EMPTY;
    }

    return new_str;
}

KOS_OBJ_ID KOS_string_get_char(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id,
                               int         idx)
{
    KOS_STRING *new_str = 0;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(ctx, IS_BAD_PTR(obj_id) ?
                                 KOS_CONST_ID(str_err_null_ptr) :
                                 KOS_CONST_ID(str_err_not_string));
    else {
        const KOS_STRING_FLAGS elem_size = kos_get_string_elem_size(OBJPTR(STRING, obj_id));
        const int              len       = (int)OBJPTR(STRING, obj_id)->header.length;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {

            kos_track_refs(ctx, 1, &obj_id);

            new_str = _new_empty_string(ctx, 1, elem_size);

            kos_untrack_refs(ctx, 1);

            if (new_str) {

                const uint8_t *buf = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id))
                                     + (idx << elem_size);

                uint8_t *new_buf = (uint8_t *)kos_get_string_buffer(new_str);

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
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
    }

    return OBJID(STRING, new_str);
}

unsigned KOS_string_get_char_code(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id,
                                  int         idx)
{
    uint32_t code = ~0U;

    if (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        KOS_raise_exception(ctx, IS_BAD_PTR(obj_id) ?
                                 KOS_CONST_ID(str_err_null_ptr) :
                                 KOS_CONST_ID(str_err_not_string));
    else {
        KOS_STRING            *str       = OBJPTR(STRING, obj_id);
        const KOS_STRING_FLAGS elem_size = kos_get_string_elem_size(str);
        const int              len       = (int)str->header.length;

        if (idx < 0)
            idx += len;

        if (idx >= 0 && idx < len) {
            const uint8_t *buf = (const uint8_t *)kos_get_string_buffer(str) + (idx << elem_size);

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
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
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
    const uint8_t  *pa      = (const uint8_t  *)kos_get_string_buffer(a) + a_begin;
    const uint16_t *pb      = (const uint16_t *)kos_get_string_buffer(b) + b_begin;
    const uint8_t  *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint8_t  ca = *pa;
            const uint16_t cb = *pb;
            if (ca != cb) {
                result = kos_unicode_compare(ca, cb);
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
    const uint8_t  *pa      = (const uint8_t  *)kos_get_string_buffer(a) + a_begin;
    const uint32_t *pb      = (const uint32_t *)kos_get_string_buffer(b) + b_begin;
    const uint8_t  *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint8_t  ca = *pa;
            const uint32_t cb = *pb;
            if (ca != cb) {
                result = kos_unicode_compare(ca, cb);
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
    const uint16_t *pa      = (const uint16_t *)kos_get_string_buffer(a) + a_begin;
    const uint32_t *pb      = (const uint32_t *)kos_get_string_buffer(b) + b_begin;
    const uint16_t *pend    = pa + cmp_len;
    int             result  = 0;

    for (;;) {
        if (pa < pend) {
            const uint16_t ca = *pa;
            const uint32_t cb = *pb;
            if (ca != cb) {
                result = kos_unicode_compare(ca, cb);
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
    unsigned a_elem_size = kos_get_string_elem_size(str_a);
    unsigned b_elem_size = kos_get_string_elem_size(str_b);

    assert(a_end   <= str_a->header.length);
    assert(a_begin <= a_end);
    assert(b_end   <= str_b->header.length);
    assert(b_begin <= b_end);

    if (a_elem_size == b_elem_size) {

        const unsigned cmp_len = KOS_min(a_len, b_len);

        const uint8_t *pa    = (const uint8_t *)kos_get_string_buffer(str_a) + (a_begin << a_elem_size);
        const uint8_t *pb    = (const uint8_t *)kos_get_string_buffer(str_b) + (b_begin << a_elem_size);
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
            result = kos_unicode_compare(ca, cb);
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

static void _string_find_brute_force(KOS_OBJ_ID          obj_id_text,
                                     KOS_OBJ_ID          obj_id_pattern,
                                     enum KOS_FIND_DIR_E reverse,
                                     int                *pos)
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

int KOS_string_find(KOS_CONTEXT         ctx,
                    KOS_OBJ_ID          obj_id_text,
                    KOS_OBJ_ID          obj_id_pattern,
                    enum KOS_FIND_DIR_E reverse,
                    int                *pos)
{
    int text_len;
    int pattern_len;
    int cur_pos;

    if (GET_OBJ_TYPE(obj_id_text) != OBJ_STRING ||
        GET_OBJ_TYPE(obj_id_pattern) != OBJ_STRING) {

        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
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
        return KOS_string_scan(ctx, obj_id_text, obj_id_pattern, reverse, KOS_SCAN_INCLUDE, pos);

    /* TODO Optimize */

    _string_find_brute_force(obj_id_text, obj_id_pattern, reverse, pos);
    return KOS_SUCCESS;
}

int KOS_string_scan(KOS_CONTEXT             ctx,
                    KOS_OBJ_ID              obj_id_text,
                    KOS_OBJ_ID              obj_id_pattern,
                    enum KOS_FIND_DIR_E     reverse,
                    enum KOS_SCAN_INCLUDE_E include,
                    int                    *pos)
{
    int     text_len;
    int     pattern_len;
    int     cur_pos;
    uint8_t text_elem_size;
    uint8_t pattern_elem_size;

    if (GET_OBJ_TYPE(obj_id_text) != OBJ_STRING ||
        GET_OBJ_TYPE(obj_id_pattern) != OBJ_STRING) {

        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
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

    text_elem_size    = (uint8_t)kos_get_string_elem_size(OBJPTR(STRING, obj_id_text));
    pattern_elem_size = (uint8_t)kos_get_string_elem_size(OBJPTR(STRING, obj_id_pattern));

    if ( ! reverse && include && pattern_len == 1
        && text_elem_size    == KOS_STRING_ELEM_8
        && pattern_elem_size == KOS_STRING_ELEM_8) {

        const uint8_t *text     = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id_text));
        const uint8_t *pattern  = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id_pattern));
        const uint8_t *location = text + cur_pos;

        location = (const uint8_t *)memchr(location, (int)*pattern, (size_t)(text_len - cur_pos));

        *pos = location ? (int)(location - text) : -1;
        return KOS_SUCCESS;
    }
    else {
        const uint8_t *text     = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id_text));
        const uint8_t *pattern  = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id_pattern));
        const uint8_t *location = text + (cur_pos << text_elem_size);
        const uint8_t *text_end = text + (reverse ? -((intptr_t)1 << text_elem_size) : ((intptr_t)text_len << text_elem_size));
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
                    const uint8_t          *pat_ptr = pattern;
                    const uint8_t          *pat_end = pat_ptr + pattern_len;
                    enum KOS_SCAN_INCLUDE_E match   = KOS_SCAN_EXCLUDE;
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
                    const uint16_t         *pat_ptr = (const uint16_t *)pattern;
                    const uint16_t         *pat_end = pat_ptr + pattern_len;
                    enum KOS_SCAN_INCLUDE_E match   = KOS_SCAN_EXCLUDE;
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
                    const uint32_t         *pat_ptr = (const uint32_t *)pattern;
                    const uint32_t         *pat_end = pat_ptr + pattern_len;
                    enum KOS_SCAN_INCLUDE_E match   = KOS_SCAN_EXCLUDE;
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

KOS_OBJ_ID KOS_string_reverse(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id)
{
    KOS_STRING *ret;
    unsigned    len;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_BADPTR;
    }

    len = KOS_get_string_length(obj_id);

    if (len < 2)
        return obj_id;

    kos_track_refs(ctx, 1, &obj_id);

    ret = _new_empty_string(ctx, len, kos_get_string_elem_size(OBJPTR(STRING, obj_id)));

    kos_untrack_refs(ctx, 1);

    if ( ! ret)
        return KOS_BADPTR;

    switch (kos_get_string_elem_size(OBJPTR(STRING, obj_id))) {

        case KOS_STRING_ELEM_8: {
            const uint8_t *src  = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            const uint8_t *end  = src + len;
            uint8_t       *dest = (uint8_t *)kos_get_string_buffer(ret) + len - 1;
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }

        case KOS_STRING_ELEM_16: {
            const uint16_t *src  = (const uint16_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            const uint16_t *end  = src + len;
            uint16_t       *dest = (uint16_t *)kos_get_string_buffer(ret) + len - 1;
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }

        default: {
            const uint32_t *src  = (const uint32_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            const uint32_t *end  = src + len;
            uint32_t       *dest = (uint32_t *)kos_get_string_buffer(ret) + len - 1;
            assert(kos_get_string_elem_size(OBJPTR(STRING, obj_id)) == KOS_STRING_ELEM_32);
            for ( ; src != end; ++src, --dest)
                *dest = *src;
            break;
        }
    }

    return OBJID(STRING, ret);
}

KOS_OBJ_ID KOS_string_repeat(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id,
                             unsigned    num_repeat)
{
    unsigned         len;
    KOS_STRING_FLAGS elem_size;
    KOS_STRING      *new_str;
    uint8_t         *in_buf;
    uint8_t         *new_buf;
    uint8_t         *end_buf;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_BADPTR;
    }

    len = KOS_get_string_length(obj_id);

    if (len == 0 || num_repeat == 0)
        return KOS_STR_EMPTY;

    if (num_repeat == 1)
        return obj_id;

    if (num_repeat > 0xFFFFU || (len * num_repeat) > 0xFFFFU) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_too_many_repeats));
        return KOS_BADPTR;
    }

    elem_size = kos_get_string_elem_size(OBJPTR(STRING, obj_id));

    kos_track_refs(ctx, 1, &obj_id);

    new_str = _new_empty_string(ctx, len * num_repeat, elem_size);

    kos_untrack_refs(ctx, 1);

    if ( ! new_str)
        return KOS_BADPTR;

    in_buf  = (uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
    new_buf = (uint8_t *)kos_get_string_buffer(new_str);

    len <<= elem_size;

    end_buf = new_buf + (len * num_repeat);

    while (new_buf < end_buf) {
        memcpy(new_buf, in_buf, len);
        new_buf += len;
    }

    return OBJID(STRING, new_str);
}

int kos_append_cstr(KOS_CONTEXT ctx,
                    KOS_VECTOR *cstr_vec,
                    const char *str,
                    size_t      len)
{
    const size_t pos   = cstr_vec->size;
    int          error = kos_vector_resize(cstr_vec, pos + len + (pos ? 0 : 1));

    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }
    else
        memcpy(&cstr_vec->buffer[pos ? pos - 1 : pos], str, len + 1);

    return error;
}

KOS_OBJ_ID KOS_string_lowercase(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
{
    KOS_STRING*      new_str;
    unsigned         len;
    unsigned         i;
    KOS_STRING_FLAGS elem_size;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_BADPTR;
    }

    len       = KOS_get_string_length(obj_id);
    elem_size = kos_get_string_elem_size(OBJPTR(STRING, obj_id));

    if (len == 0)
        return KOS_STR_EMPTY;

    kos_track_refs(ctx, 1, &obj_id);

    new_str = _new_empty_string(ctx, len, elem_size);

    kos_untrack_refs(ctx, 1);

    if ( ! new_str)
        return OBJID(STRING, new_str);

    switch (elem_size) {

        case KOS_STRING_ELEM_8: {
            const uint8_t *src  = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint8_t       *dest = (uint8_t *)kos_get_string_buffer(new_str);

            for (i = 0; i < len; i++) {
                const uint8_t c = *(src++);
                *(dest++) = (uint8_t)kos_unicode_to_lower(c);
            }
            break;
        }

        case KOS_STRING_ELEM_16: {
            const uint16_t *src  = (const uint16_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint16_t       *dest = (uint16_t *)kos_get_string_buffer(new_str);

            for (i = 0; i < len; i++) {
                const uint16_t c = *(src++);
                *(dest++) = (uint16_t)kos_unicode_to_lower(c);
            }
            break;
        }

        default: {
            const uint32_t *src  = (const uint32_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint32_t       *dest = (uint32_t *)kos_get_string_buffer(new_str);

            assert(elem_size == KOS_STRING_ELEM_32);

            for (i = 0; i < len; i++) {
                uint32_t c = *(src++);

                if (c < 0x10000U)
                    c = (uint32_t)kos_unicode_to_lower((uint16_t)c);

                *(dest++) = c;
            }
            break;
        }
    }

    return OBJID(STRING, new_str);
}

KOS_OBJ_ID KOS_string_uppercase(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
{
    KOS_STRING*      new_str;
    unsigned         len;
    unsigned         i;
    KOS_STRING_FLAGS elem_size;

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_BADPTR;
    }

    len       = KOS_get_string_length(obj_id);
    elem_size = kos_get_string_elem_size(OBJPTR(STRING, obj_id));

    if (len == 0)
        return KOS_STR_EMPTY;

    kos_track_refs(ctx, 1, &obj_id);

    new_str = _new_empty_string(ctx, len, elem_size);

    kos_untrack_refs(ctx, 1);

    if ( ! new_str)
        return OBJID(STRING, new_str);

    switch (elem_size) {

        case KOS_STRING_ELEM_8: {
            const uint8_t *src  = (const uint8_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint8_t       *dest = (uint8_t *)kos_get_string_buffer(new_str);

            for (i = 0; i < len; i++) {
                const uint8_t c = *(src++);
                *(dest++) = (uint8_t)kos_unicode_to_upper(c);
            }
            break;
        }

        case KOS_STRING_ELEM_16: {
            const uint16_t *src  = (const uint16_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint16_t       *dest = (uint16_t *)kos_get_string_buffer(new_str);

            for (i = 0; i < len; i++) {
                const uint16_t c = *(src++);
                *(dest++) = (uint16_t)kos_unicode_to_upper(c);
            }
            break;
        }

        default: {
            const uint32_t *src  = (const uint32_t *)kos_get_string_buffer(OBJPTR(STRING, obj_id));
            uint32_t       *dest = (uint32_t *)kos_get_string_buffer(new_str);

            assert(elem_size == KOS_STRING_ELEM_32);

            for (i = 0; i < len; i++) {
                uint32_t c = *(src++);

                if (c < 0x10000U)
                    c = (uint32_t)kos_unicode_to_upper((uint16_t)c);

                *(dest++) = c;
            }
            break;
        }
    }

    return OBJID(STRING, new_str);
}

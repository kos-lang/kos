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

#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_math.h"
#include "kos_misc.h"
#include <string.h>

static const char str_err_make_room_size[] = "buffer size limit exceeded";
static const char str_err_not_buffer[]     = "object is not a buffer";
static const char str_err_null_ptr[]       = "null pointer";

typedef struct _KOS_BUFFER_DATA BUFFER_DATA;

#define KOS_buffer_alloc_size(cap) (sizeof(BUFFER_DATA) + ((cap) - 1U))

static BUFFER_DATA *_alloc_buffer(KOS_FRAME frame, unsigned capacity)
{
    BUFFER_DATA *const data = (BUFFER_DATA *)
            _KOS_alloc_buffer(frame, KOS_buffer_alloc_size(capacity));

#ifndef NDEBUG
    /*
     * The caller is supposed to fill it out completely and reliably.
     * Therefore in debug builds, we fill it with random data to trigger
     * any bugs more easily.
     */
    if (data) {
        static struct KOS_RNG rng;
        static int            init = 0;
        uint64_t             *buf = (uint64_t *)data;
        uint64_t             *end = buf + capacity / sizeof(uint64_t);

        if ( ! init) {
            _KOS_rng_init(&rng);
            init = 1;
        }

        while (buf < end)
            *(buf++) = _KOS_rng_random(&rng);
    }
#endif

    return data;
}

KOS_OBJ_ID KOS_new_buffer(KOS_FRAME frame,
                          unsigned  size)
{
    KOS_BUFFER     *buffer   = (KOS_BUFFER *)_KOS_alloc_object(frame, BUFFER);
    const unsigned  capacity = (size + (KOS_BUFFER_CAPACITY_ALIGN-1)) & ~(KOS_BUFFER_CAPACITY_ALIGN-1);

    if (buffer) {

        buffer->size = size;

        if (capacity) {
            BUFFER_DATA *data = _alloc_buffer(frame, capacity);

            if (data) {
                data->capacity = capacity;
                buffer->data   = data;
            }
            else
                buffer = 0;
        }
        else
            buffer->data = (void *)0;
    }

    return OBJID(BUFFER, buffer);
}

int KOS_buffer_reserve(KOS_FRAME  frame,
                       KOS_OBJ_ID obj_id,
                       unsigned   new_capacity)
{
    int error = KOS_ERROR_EXCEPTION;

    new_capacity = (new_capacity + (KOS_BUFFER_CAPACITY_ALIGN-1)) & ~(KOS_BUFFER_CAPACITY_ALIGN-1);

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER *const buffer = OBJPTR(BUFFER, obj_id);

        for (;;) {
            BUFFER_DATA *const old_buf  = (BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);
            const uint32_t     capacity = old_buf ? old_buf->capacity : 0;

            if (new_capacity > capacity) {

                uint32_t           size;
                BUFFER_DATA *const buf = _alloc_buffer(frame, new_capacity);
                if ( ! buf)
                    break;

                size = KOS_atomic_read_u32(buffer->size);

                if (size > capacity) {
                    _KOS_free_buffer(frame, buf, KOS_buffer_alloc_size(new_capacity));
                    continue;
                }

                buf->capacity = new_capacity;
                if (size)
                    memcpy(&buf->buf[0], &old_buf->buf[0], size);

                if ( ! KOS_atomic_cas_ptr(buffer->data, (void *)old_buf, (void *)buf)) {
                    _KOS_free_buffer(frame, buf, KOS_buffer_alloc_size(new_capacity));
                    continue;
                }

                /* TODO another thread may still be using it */
                if (old_buf)
                    _KOS_free_buffer(frame, old_buf, KOS_buffer_alloc_size(capacity));
            }

            error = KOS_SUCCESS;
            break;
        }
    }

    return error;
}

int KOS_buffer_resize(KOS_FRAME  frame,
                      KOS_OBJ_ID obj_id,
                      unsigned   size)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER *const buffer   = OBJPTR(BUFFER, obj_id);
        const uint32_t    old_size = KOS_atomic_read_u32(buffer->size);

        error = KOS_SUCCESS;

        if (size > old_size) {

            BUFFER_DATA *const data     = (BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);
            const uint32_t     capacity = data ? data->capacity : 0;

            if (size > capacity) {
                const uint32_t new_capacity = size > capacity * 2 ? size : capacity * 2;
                error = KOS_buffer_reserve(frame, obj_id, new_capacity);
            }
        }

        if ( ! error)
            KOS_atomic_swap_u32(buffer->size, size);
    }

    return error;
}

uint8_t *KOS_buffer_make_room(KOS_FRAME  frame,
                              KOS_OBJ_ID obj_id,
                              unsigned   size_delta)
{
    uint8_t *ret = 0;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER *const buffer = OBJPTR(BUFFER, obj_id);

        if (size_delta == 0)
            return 0;

        for (;;) {
            const uint32_t     old_size = KOS_atomic_read_u32(buffer->size);
            const uint32_t     new_size = old_size + size_delta;
            BUFFER_DATA *const data     = (BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);
            const uint32_t     capacity = data ? data->capacity : 0;

            if (size_delta > 0xFFFFFFFFU - old_size) {
                KOS_raise_exception_cstring(frame, str_err_make_room_size);
                return 0;
            }

            if (new_size > capacity) {
                const uint32_t new_capacity = new_size > capacity * 2 ? new_size : capacity * 2;
                const int      error        = KOS_buffer_reserve(frame, obj_id, new_capacity);
                if (error)
                    return 0;
            }

            if (KOS_atomic_cas_u32(buffer->size, old_size, new_size)) {
                ret = KOS_buffer_data(obj_id) + old_size;
                break;
            }
        }
    }

    return ret;
}

int KOS_buffer_fill(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int64_t    begin,
                    int64_t    end,
                    uint8_t    value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER  *const buffer = OBJPTR(BUFFER, obj_id);
        uint32_t           size   = KOS_atomic_read_u32(buffer->size);
        BUFFER_DATA *const data   = (BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);

        begin = _KOS_fix_index(begin, size);
        end   = _KOS_fix_index(end,   size);

        if (begin < end)
            memset(&data->buf[begin], (int)value, (size_t)(end - begin));

        error = KOS_SUCCESS;
    }

    return error;
}

int KOS_buffer_copy(KOS_FRAME  frame,
                    KOS_OBJ_ID destptr,
                    int64_t    dest_begin,
                    KOS_OBJ_ID srcptr,
                    int64_t    src_begin,
                    int64_t    src_end)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(destptr) || IS_BAD_PTR(srcptr))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(destptr) != OBJ_BUFFER ||
             GET_OBJ_TYPE(srcptr)  != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER  *const dest_buffer = OBJPTR(BUFFER, destptr);
        uint32_t           dest_size   = KOS_atomic_read_u32(dest_buffer->size);
        BUFFER_DATA *const dest_data   = (BUFFER_DATA *)KOS_atomic_read_ptr(dest_buffer->data);

        KOS_BUFFER  *const src_buffer  = OBJPTR(BUFFER, srcptr);
        uint32_t           src_size    = KOS_atomic_read_u32(src_buffer->size);
        BUFFER_DATA *const src_data    = (BUFFER_DATA *)KOS_atomic_read_ptr(src_buffer->data);

        dest_begin = _KOS_fix_index(dest_begin, dest_size);
        src_begin  = _KOS_fix_index(src_begin, src_size);
        src_end    = _KOS_fix_index(src_end,   src_size);

        if (src_begin < src_end && dest_begin < dest_size) {
            const size_t size = (size_t)KOS_min(src_end - src_begin, dest_size - dest_begin);
            uint8_t     *dest = &dest_data->buf[dest_begin];
            uint8_t     *src  = &src_data->buf[src_begin];

            if (src >= dest + size || src + size <= dest)
                memcpy(dest, src, size);
            else
                memmove(dest, src, size);
        }

        error = KOS_SUCCESS;
    }

    return error;
}

KOS_OBJ_ID KOS_buffer_slice(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id,
                            int64_t    begin,
                            int64_t    end)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
    else {
        KOS_BUFFER *const  src_buf  = OBJPTR(BUFFER, obj_id);
        uint32_t           src_size = KOS_atomic_read_u32(src_buf->size);
        BUFFER_DATA *const src_data = (BUFFER_DATA *)KOS_atomic_read_ptr(src_buf->data);

        if (src_size) {

            uint32_t new_size;
            int64_t  new_size_64;

            begin = _KOS_fix_index(begin, src_size);
            end   = _KOS_fix_index(end,   src_size);

            if (end < begin)
                end = begin;

            new_size_64 = end - begin;
            assert(new_size_64 <= 0xFFFFFFFF);
            new_size = (uint32_t)new_size_64;

            ret = KOS_new_buffer(frame, new_size);

            if (new_size && ! IS_BAD_PTR(ret)) {
                KOS_BUFFER *const  dst_buf  = OBJPTR(BUFFER, ret);
                BUFFER_DATA *const dst_data = (BUFFER_DATA *)KOS_atomic_read_ptr(dst_buf->data);
                memcpy(dst_data->buf, &src_data->buf[begin], new_size);
            }
        }
        else
            ret = KOS_new_buffer(frame, 0);
    }

    return ret;
}

int KOS_buffer_rotate(KOS_FRAME  frame,
                      KOS_OBJ_ID obj_id,
                      int64_t    begin,
                      int64_t    mid,
                      int64_t    end)
{
    /* TODO */
    return KOS_ERROR_INTERNAL;
}

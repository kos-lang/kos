/*
 * Copyright (c) 2014-2019 Chris Dragan
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
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include <string.h>

static const char str_err_make_room_size[] = "buffer size limit exceeded";
static const char str_err_not_buffer[]     = "object is not a buffer";
static const char str_err_null_ptr[]       = "null pointer";

#define KOS_buffer_alloc_size(cap) (sizeof(KOS_BUFFER_STORAGE) + ((cap) - 1U))

static KOS_BUFFER_STORAGE *_alloc_buffer(KOS_CONTEXT ctx, unsigned capacity)
{
    KOS_BUFFER_STORAGE *const data = (KOS_BUFFER_STORAGE *)
            kos_alloc_object(ctx,
                             KOS_ALLOC_MOVABLE,
                             OBJ_BUFFER_STORAGE,
                             KOS_buffer_alloc_size(capacity));

#ifndef NDEBUG
    /*
     * The caller is supposed to fill it out completely and reliably.
     * Therefore in debug builds, we fill it with random data to trigger
     * any bugs more easily.
     */
    if (data) {
        static struct KOS_RNG rng;
        static int            init = 0;
        uint64_t             *buf = (uint64_t *)&data->buf[0];
        uint64_t             *end = buf + capacity / sizeof(uint64_t);

        assert(kos_get_object_type(data->header) == OBJ_BUFFER_STORAGE);

        if ( ! init) {
            kos_rng_init(&rng);
            init = 1;
        }

        while (buf < end)
            *(buf++) = kos_rng_random(&rng);
    }
#endif

    if (data)
        KOS_atomic_write_release_u32(data->capacity, capacity);

    return data;
}

KOS_OBJ_ID KOS_new_buffer(KOS_CONTEXT ctx,
                          unsigned    size)
{
    const unsigned capacity = (size + (KOS_BUFFER_CAPACITY_ALIGN-1)) & ~(KOS_BUFFER_CAPACITY_ALIGN-1);
    KOS_OBJ_ID     obj_id;

    obj_id = OBJID(BUFFER, (KOS_BUFFER *)kos_alloc_object(ctx,
                                                          KOS_ALLOC_MOVABLE,
                                                          OBJ_BUFFER,
                                                          sizeof(KOS_BUFFER)));
    if ( ! IS_BAD_PTR(obj_id)) {

        OBJPTR(BUFFER, obj_id)->size = size;
        OBJPTR(BUFFER, obj_id)->data = KOS_BADPTR;

        if (capacity) {

            KOS_BUFFER_STORAGE *data;

            kos_track_refs(ctx, 1, &obj_id);

            data = _alloc_buffer(ctx, capacity);

            kos_untrack_refs(ctx, 1);

            if (data)
                KOS_atomic_write_release_ptr(OBJPTR(BUFFER, obj_id)->data,
                                             OBJID(BUFFER_STORAGE, data));
            else
                obj_id = KOS_BADPTR;
        }
    }

    return obj_id;
}

static KOS_BUFFER_STORAGE *_get_data(KOS_OBJ_ID obj_id)
{
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_acquire_obj(OBJPTR(BUFFER, obj_id)->data);
    return IS_BAD_PTR(buf_obj) ? 0 : OBJPTR(BUFFER_STORAGE, buf_obj);
}

static KOS_OBJ_ID get_storage(KOS_OBJ_ID obj_id)
{
    const KOS_OBJ_ID storage_obj = KOS_atomic_read_acquire_obj(OBJPTR(BUFFER, obj_id)->data);
    return storage_obj;
}

int KOS_buffer_reserve(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  obj_id,
                       unsigned    new_capacity)
{
    int error = KOS_ERROR_EXCEPTION;

    new_capacity = (new_capacity + (KOS_BUFFER_CAPACITY_ALIGN-1)) & ~(KOS_BUFFER_CAPACITY_ALIGN-1);

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        KOS_OBJ_ID old_buf = KOS_BADPTR;

        kos_track_refs(ctx, 2, &obj_id, &old_buf);

        for (;;) {
            uint32_t capacity;

            old_buf  = get_storage(obj_id);
            capacity = IS_BAD_PTR(old_buf) ? 0
                     : KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER_STORAGE, old_buf)->capacity);

            if (new_capacity > capacity) {

                uint32_t                  size;
                KOS_BUFFER_STORAGE *const buf = _alloc_buffer(ctx, new_capacity);
                if ( ! buf)
                    break;

                size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);

                if (size > capacity)
                    continue;

                if (size)
                    memcpy(&buf->buf[0], &OBJPTR(BUFFER_STORAGE, old_buf)->buf[0], size);

                (void)KOS_atomic_cas_strong_ptr(OBJPTR(BUFFER, obj_id)->data,
                                         old_buf,
                                         OBJID(BUFFER_STORAGE, buf));
            }

            error = KOS_SUCCESS;
            break;
        }

        kos_untrack_refs(ctx, 2);
    }

    return error;
}

int KOS_buffer_resize(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      unsigned    size)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        const uint32_t old_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);

        error = KOS_SUCCESS;

        if (size > old_size) {

            KOS_BUFFER_STORAGE *const data = _get_data(obj_id);
            const uint32_t capacity = data ? KOS_atomic_read_relaxed_u32(data->capacity) : 0;

            if (size > capacity) {
                const uint32_t new_capacity = size > capacity * 2 ? size : capacity * 2;

                kos_track_refs(ctx, 1, &obj_id);

                error = KOS_buffer_reserve(ctx, obj_id, new_capacity);

                kos_untrack_refs(ctx, 1);
            }
        }

        if ( ! error)
            KOS_atomic_swap_u32(OBJPTR(BUFFER, obj_id)->size, size);
    }

    return error;
}

uint8_t *KOS_buffer_data(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id)
{
    uint8_t *ret = 0;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {

        KOS_OBJ_ID buf_id = get_storage(obj_id);

        if (IS_BAD_PTR(buf_id) || kos_is_heap_object(buf_id)) {

            int error;

            kos_track_refs(ctx, 1, &obj_id);

            error = KOS_buffer_reserve(ctx, obj_id, KOS_MAX_HEAP_OBJ_SIZE * 2U);

            kos_untrack_refs(ctx, 1);

            if (error)
                goto cleanup;

            buf_id = get_storage(obj_id);
        }

        assert(kos_is_tracked_object(buf_id) && ! kos_is_heap_object(buf_id));

        ret = &OBJPTR(BUFFER_STORAGE, buf_id)->buf[0];
    }

cleanup:
    return ret;
}

uint8_t *KOS_buffer_make_room(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id,
                              unsigned    size_delta)
{
    uint8_t *ret = 0;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        for (;;) {
            const uint32_t   old_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);
            const uint32_t   new_size = old_size + size_delta;
            const KOS_OBJ_ID data_id  = get_storage(obj_id);
            const uint32_t   capacity = IS_BAD_PTR(data_id) ? 0 :
                    KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER_STORAGE, data_id)->capacity);

            /* Ensure that the new buffer is allocated off heap */
            const uint32_t off_heap_size = KOS_max(new_size, KOS_MAX_HEAP_OBJ_SIZE * 2U);

            if (size_delta > 0xFFFFFFFFU - old_size) {
                KOS_raise_exception_cstring(ctx, str_err_make_room_size);
                break;
            }

            if (off_heap_size > capacity) {
                const uint32_t new_capacity = new_size > capacity * 2 ? new_size : capacity * 2;
                int            error;

                kos_track_refs(ctx, 1, &obj_id);

                error = KOS_buffer_reserve(ctx, obj_id, new_capacity);

                kos_untrack_refs(ctx, 1);

                if (error)
                    break;
            }

            if (KOS_atomic_cas_strong_u32(OBJPTR(BUFFER, obj_id)->size, old_size, new_size)) {
                ret = KOS_buffer_data_volatile(obj_id) + old_size;
                break;
            }
        }
    }

    return ret;
}

int KOS_buffer_fill(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t     begin,
                    int64_t     end,
                    uint8_t     value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        uint32_t size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);
        KOS_BUFFER_STORAGE *const data = _get_data(obj_id);

        begin = kos_fix_index(begin, size);
        end   = kos_fix_index(end,   size);

        if (begin < end)
            memset(&data->buf[begin], (int)value, (size_t)(end - begin));

        error = KOS_SUCCESS;
    }

    return error;
}

int KOS_buffer_copy(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  destptr,
                    int64_t     dest_begin,
                    KOS_OBJ_ID  srcptr,
                    int64_t     src_begin,
                    int64_t     src_end)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(destptr) || IS_BAD_PTR(srcptr))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(destptr) != OBJ_BUFFER ||
             GET_OBJ_TYPE(srcptr)  != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        uint32_t dest_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, destptr)->size);
        KOS_BUFFER_STORAGE *const dest_data = _get_data(destptr);

        uint32_t src_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, srcptr)->size);
        KOS_BUFFER_STORAGE *const src_data = _get_data(srcptr);

        dest_begin = kos_fix_index(dest_begin, dest_size);
        src_begin  = kos_fix_index(src_begin, src_size);
        src_end    = kos_fix_index(src_end,   src_size);

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

KOS_OBJ_ID KOS_buffer_slice(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            int64_t     begin,
                            int64_t     end)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
    else {
        const uint32_t src_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);

        if (src_size) {

            uint32_t new_size;
            int64_t  new_size_64;

            begin = kos_fix_index(begin, src_size);
            end   = kos_fix_index(end,   src_size);

            if (end < begin)
                end = begin;

            new_size_64 = end - begin;
            assert(new_size_64 <= 0xFFFFFFFF);
            new_size = (uint32_t)new_size_64;

            kos_track_refs(ctx, 1, &obj_id);

            ret = KOS_new_buffer(ctx, new_size);

            kos_untrack_refs(ctx, 1);

            if (new_size && ! IS_BAD_PTR(ret)) {
                KOS_BUFFER_STORAGE *const dst_data = _get_data(ret);
                memcpy(dst_data->buf, &_get_data(obj_id)->buf[begin], new_size);
            }
        }
        else
            ret = KOS_new_buffer(ctx, 0);
    }

    return ret;
}

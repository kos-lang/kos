/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "../inc/kos_utils.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_empty,          "cannot modify empty buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_make_room_size, "buffer size limit exceeded");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer,     "object is not a buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_read_only,      "buffer is read-only");

#define KOS_buffer_alloc_size(cap) (sizeof(KOS_BUFFER_STORAGE) + ((cap) - 1U))

static KOS_BUFFER_STORAGE *alloc_buffer(KOS_CONTEXT ctx, unsigned capacity)
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
        uint64_t             *end = (uint64_t *)((intptr_t)buf + capacity);

        assert(kos_get_object_type(data->header) == OBJ_BUFFER_STORAGE);

        if ( ! init) {
            kos_rng_init(&rng);
            init = 1;
        }

        /* Cheat, reuse ctx_mutex to ensure the RNG is not banged from
         * multiple threads. */
        kos_lock_mutex(&ctx->inst->threads.ctx_mutex);

        while (((intptr_t)buf & 7) && buf < end) {
            *(uint8_t *)buf = (uint8_t)kos_rng_random(&rng);
            buf = (uint64_t *)((intptr_t)buf + 1);
        }

        while (buf + 1 <= end)
            *(buf++) = kos_rng_random(&rng);

        kos_unlock_mutex(&ctx->inst->threads.ctx_mutex);
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
    KOS_LOCAL      obj;

    KOS_init_local_with(ctx, &obj, OBJID(BUFFER, (KOS_BUFFER *)
                kos_alloc_object(ctx,
                                 KOS_ALLOC_MOVABLE,
                                 OBJ_BUFFER,
                                 sizeof(KOS_BUFFER))));
    if ( ! IS_BAD_PTR(obj.o)) {

        OBJPTR(BUFFER, obj.o)->size  = size;
        OBJPTR(BUFFER, obj.o)->flags = 0;
        OBJPTR(BUFFER, obj.o)->data  = KOS_BADPTR;

        if (capacity) {

            KOS_BUFFER_STORAGE *data;

            data = alloc_buffer(ctx, capacity);

            if (data)
                KOS_atomic_write_release_ptr(OBJPTR(BUFFER, obj.o)->data,
                                             OBJID(BUFFER_STORAGE, data));
            else
                obj.o = KOS_BADPTR;
        }
    }

    return KOS_destroy_top_local(ctx, &obj);
}

static KOS_BUFFER_STORAGE *get_data(KOS_OBJ_ID obj_id)
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
    KOS_LOCAL obj;
    KOS_LOCAL old_buf;
    int       error = KOS_ERROR_EXCEPTION;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local_with(ctx, &obj, obj_id);
    KOS_init_local(ctx, &old_buf);

    new_capacity = (new_capacity + (KOS_BUFFER_CAPACITY_ALIGN-1)) & ~(KOS_BUFFER_CAPACITY_ALIGN-1);

    if (GET_OBJ_TYPE(obj.o) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj.o)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        for (;;) {
            uint32_t capacity;

            old_buf.o = get_storage(obj.o);
            capacity  = IS_BAD_PTR(old_buf.o) ? 0
                      : KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER_STORAGE, old_buf.o)->capacity);

            if (new_capacity > capacity) {

                uint32_t                  size;
                KOS_BUFFER_STORAGE *const buf = alloc_buffer(ctx, new_capacity);
                if ( ! buf)
                    break;

                size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj.o)->size);

                if (size > capacity)
                    continue;

                if (size)
                    memcpy(&buf->buf[0], &OBJPTR(BUFFER_STORAGE, old_buf.o)->buf[0], size);

                (void)KOS_atomic_cas_strong_ptr(OBJPTR(BUFFER, obj.o)->data,
                                                old_buf.o,
                                                OBJID(BUFFER_STORAGE, buf));
            }

            error = KOS_SUCCESS;
            break;
        }
    }

    KOS_destroy_top_locals(ctx, &old_buf, &obj);

    return error;
}

int KOS_buffer_resize(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      unsigned    size)
{
    int error = KOS_ERROR_EXCEPTION;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        const uint32_t old_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);

        error = KOS_SUCCESS;

        if (size > old_size) {

            KOS_BUFFER_STORAGE *const data = get_data(obj_id);
            const uint32_t capacity = data ? KOS_atomic_read_relaxed_u32(data->capacity) : 0;

            if (size > capacity) {
                KOS_LOCAL      obj;
                const uint32_t new_capacity = size > capacity * 2 ? size : capacity * 2;

                KOS_init_local_with(ctx, &obj, obj_id);

                error = KOS_buffer_reserve(ctx, obj.o, new_capacity);

                obj_id = KOS_destroy_top_local(ctx, &obj);
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

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {

        KOS_OBJ_ID buf_id = get_storage(obj_id);

        if (IS_BAD_PTR(buf_id) || kos_is_heap_object(buf_id)) {

            KOS_LOCAL obj;
            int       error;

            KOS_init_local_with(ctx, &obj, obj_id);

            error = KOS_buffer_reserve(ctx, obj.o, KOS_MAX_HEAP_OBJ_SIZE * 2U);

            obj_id = KOS_destroy_top_local(ctx, &obj);

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

uint8_t *KOS_buffer_data_volatile(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id)
{
    uint8_t *ret = 0;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        const KOS_OBJ_ID buf_obj = KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data);

        if (IS_BAD_PTR(buf_obj))
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_empty));
        else {
            KOS_BUFFER_STORAGE *data = OBJPTR(BUFFER_STORAGE, buf_obj);

            ret = &data->buf[0];
        }
    }

    return ret;
}

uint8_t *KOS_buffer_make_room(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id,
                              unsigned    size_delta)
{
    uint8_t *ret = 0;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
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
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_make_room_size));
                break;
            }

            if (off_heap_size > capacity) {
                KOS_LOCAL      obj;
                const uint32_t new_capacity = new_size > capacity * 2 ? new_size : capacity * 2;
                int            error;

                KOS_init_local_with(ctx, &obj, obj_id);

                error = KOS_buffer_reserve(ctx, obj.o, new_capacity);

                obj_id = KOS_destroy_top_local(ctx, &obj);

                if (error)
                    break;
            }

            if (KOS_atomic_cas_strong_u32(OBJPTR(BUFFER, obj_id)->size, old_size, new_size)) {
                ret = KOS_buffer_data_volatile(ctx, obj_id);
                if (ret)
                    ret += old_size;
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

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        uint32_t size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);
        KOS_BUFFER_STORAGE *const data = get_data(obj_id);

        begin = KOS_fix_index(begin, size);
        end   = KOS_fix_index(end,   size);

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

    assert( ! IS_BAD_PTR(srcptr));
    assert( ! IS_BAD_PTR(destptr));

    if (GET_OBJ_TYPE(destptr) != OBJ_BUFFER ||
        GET_OBJ_TYPE(srcptr)  != OBJ_BUFFER)

        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, destptr)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        uint32_t dest_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, destptr)->size);
        KOS_BUFFER_STORAGE *const dest_data = get_data(destptr);

        uint32_t src_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, srcptr)->size);
        KOS_BUFFER_STORAGE *const src_data = get_data(srcptr);

        dest_begin = KOS_fix_index(dest_begin, dest_size);
        src_begin  = KOS_fix_index(src_begin, src_size);
        src_end    = KOS_fix_index(src_end,   src_size);

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
    KOS_LOCAL  obj;
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local_with(ctx, &obj, obj_id);

    if (GET_OBJ_TYPE(obj.o) != OBJ_BUFFER)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_buffer));
    else {
        const uint32_t src_size = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj.o)->size);

        if (src_size) {

            uint32_t new_size;
            int64_t  new_size_64;

            begin = KOS_fix_index(begin, src_size);
            end   = KOS_fix_index(end,   src_size);

            if (end < begin)
                end = begin;

            new_size_64 = end - begin;
            assert(new_size_64 <= 0xFFFFFFFF);
            new_size = (uint32_t)new_size_64;

            ret = KOS_new_buffer(ctx, new_size);

            if (new_size && ! IS_BAD_PTR(ret)) {
                KOS_BUFFER_STORAGE *const dst_data = get_data(ret);
                memcpy(dst_data->buf, &get_data(obj.o)->buf[begin], new_size);
            }
        }
        else
            ret = KOS_new_buffer(ctx, 0);
    }

    KOS_destroy_top_local(ctx, &obj);

    return ret;
}

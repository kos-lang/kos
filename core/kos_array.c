/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_atomic.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>

KOS_API_VAR_DEF KOS_DECLARE_ALIGNED(32, const struct KOS_CONST_ARRAY_S KOS_empty_array) =
    { { { 0, 0 } }, { OBJ_ARRAY, 0, KOS_READ_ONLY, KOS_BADPTR } };

KOS_DECLARE_STATIC_CONST_STRING(str_err_empty,         "array is empty");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_index, "array index is out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_array,     "object is not an array");
KOS_DECLARE_STATIC_CONST_STRING(str_err_read_only,     "array is read-only");

DECLARE_STATIC_CONST_OBJECT(tombstone, OBJ_OPAQUE, 0xA0);
DECLARE_STATIC_CONST_OBJECT(closed,    OBJ_OPAQUE, 0xA1);

/* TOMBSTONE indicates that an array element has been deleted due to a resize. */
#define TOMBSTONE KOS_CONST_ID(tombstone)
/* CLOSED indicates that an array element has been moved to a new buffer. */
#define CLOSED    KOS_CONST_ID(closed)

#define KOS_buffer_alloc_size(cap) (KOS_align_up((uint32_t)sizeof(KOS_ARRAY_STORAGE) \
                                                   + (uint32_t)(((cap) - 1) * sizeof(KOS_OBJ_ID)), \
                                                 1U << KOS_OBJ_ALIGN_BITS))

static void atomic_fill_ptr(KOS_ATOMIC(KOS_OBJ_ID) *dest,
                            unsigned                count,
                            KOS_OBJ_ID              value)
{
    KOS_ATOMIC(KOS_OBJ_ID) *const end = dest + count;
    while (dest < end) {
        KOS_atomic_write_relaxed_ptr(*dest, value);
        ++dest;
    }
}

static KOS_ARRAY_STORAGE *alloc_buffer(KOS_CONTEXT ctx, uint32_t capacity)
{
    KOS_ARRAY_STORAGE *buf            = KOS_NULL;
    const uint32_t     buf_alloc_size = KOS_buffer_alloc_size(capacity);

    if (capacity < KOS_MAX_ARRAY_SIZE)
        buf = (KOS_ARRAY_STORAGE *)kos_alloc_object(ctx,
                                                    KOS_ALLOC_MOVABLE,
                                                    OBJ_ARRAY_STORAGE,
                                                    buf_alloc_size);
    else
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    if (buf) {
        assert(kos_get_object_type(buf->header) == OBJ_ARRAY_STORAGE);

        capacity = 1U + (buf_alloc_size - sizeof(KOS_ARRAY_STORAGE)) / sizeof(KOS_OBJ_ID);
        KOS_atomic_write_relaxed_u32(buf->capacity,       capacity);
        KOS_atomic_write_relaxed_u32(buf->num_slots_open, capacity);
        KOS_atomic_write_relaxed_ptr(buf->next,           KOS_BADPTR);
    }

    return buf;
}

KOS_OBJ_ID KOS_new_array(KOS_CONTEXT ctx,
                         uint32_t    size)
{
    const uint32_t array_obj_size = KOS_align_up((uint32_t)sizeof(KOS_ARRAY), 1U << KOS_OBJ_ALIGN_BITS);
    const uint32_t buf_alloc_size = size ? KOS_buffer_alloc_size(size) : 0;
    const int      buf_built_in   = array_obj_size + buf_alloc_size <= 256U;
    const uint32_t alloc_size     = buf_built_in ? array_obj_size + buf_alloc_size : array_obj_size;
    KOS_ARRAY     *array          = KOS_NULL;

    if (size < KOS_MAX_ARRAY_SIZE)
        array = (KOS_ARRAY *)kos_alloc_object(ctx,
                                              KOS_ALLOC_MOVABLE,
                                              OBJ_ARRAY,
                                              alloc_size);
    else
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    if (array) {
        KOS_ARRAY_STORAGE *storage = KOS_NULL;

        KOS_atomic_write_relaxed_u32(array->flags, 0);

        if (buf_built_in) {
            if (buf_alloc_size) {

                const uint32_t capacity = 1U + (buf_alloc_size - sizeof(KOS_ARRAY_STORAGE)) / sizeof(KOS_OBJ_ID);

                storage = (KOS_ARRAY_STORAGE *)((uintptr_t)array + array_obj_size);
                kos_set_object_type_size(storage->header, OBJ_ARRAY_STORAGE, buf_alloc_size);

                KOS_atomic_write_relaxed_u32(storage->capacity,       capacity);
                KOS_atomic_write_relaxed_u32(storage->num_slots_open, capacity);
                KOS_atomic_write_relaxed_ptr(storage->next,           KOS_BADPTR);

                kos_set_object_size(array->header, array_obj_size);

                KOS_atomic_write_relaxed_ptr(array->data, OBJID(ARRAY_STORAGE, storage));
            }
            else
                KOS_atomic_write_relaxed_ptr(array->data, KOS_BADPTR);
        }
        else {
            KOS_LOCAL saved_array;

            KOS_atomic_write_relaxed_ptr(array->data, KOS_BADPTR);

            KOS_init_local_with(ctx, &saved_array, OBJID(ARRAY, array));

            storage = alloc_buffer(ctx, size);

            if (storage) {
                array = OBJPTR(ARRAY, saved_array.o);

                KOS_atomic_write_relaxed_ptr(array->data, OBJID(ARRAY_STORAGE, storage));
            }
            else
                array = KOS_NULL;

            KOS_destroy_top_local(ctx, &saved_array);
        }

        if (array) {

            KOS_atomic_write_relaxed_u32(array->size, size);

            if (storage) {
                const uint32_t capacity = KOS_atomic_read_relaxed_u32(storage->capacity);

                if (size)
                    atomic_fill_ptr(&storage->buf[0], size, KOS_VOID);

                if (size < capacity)
                    atomic_fill_ptr(&storage->buf[size], capacity - size, TOMBSTONE);
            }
        }
    }

    return OBJID(ARRAY, array);
}

static KOS_ARRAY_STORAGE *get_data(KOS_OBJ_ID obj_id)
{
    const KOS_OBJ_ID buf_obj = kos_get_array_storage(obj_id);
    return IS_BAD_PTR(buf_obj) ? KOS_NULL : OBJPTR(ARRAY_STORAGE, buf_obj);
}

static KOS_ARRAY_STORAGE *get_next(KOS_ARRAY_STORAGE *storage)
{
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_acquire_obj(storage->next);
    return IS_BAD_PTR(buf_obj) ? KOS_NULL : OBJPTR(ARRAY_STORAGE, buf_obj);
}

static void copy_buf(KOS_CONTEXT        ctx,
                     KOS_ARRAY         *array,
                     KOS_ARRAY_STORAGE *old_buf,
                     KOS_ARRAY_STORAGE *new_buf)
{
    KOS_ATOMIC(KOS_OBJ_ID) *src      = &old_buf->buf[0];
    KOS_ATOMIC(KOS_OBJ_ID) *dst      = &new_buf->buf[0];
    const uint32_t          capacity = KOS_atomic_read_relaxed_u32(old_buf->capacity);
    const uint32_t          fuzz     = KOS_atomic_read_relaxed_u32(old_buf->num_slots_open);
    uint32_t                i        = (capacity - fuzz) % capacity;

    for (;;) {
        KOS_OBJ_ID in_dst   = TOMBSTONE;
        int        salvaged = 0;

        /* Salvage item to the new buffer */
        for (;;) {
            const KOS_OBJ_ID value = KOS_atomic_read_relaxed_obj(src[i]);

            /* Another thread copied it */
            if (value == CLOSED)
                break;

            /* Write value to new buffer */
            if ( ! KOS_atomic_cas_strong_ptr(dst[i], in_dst, value))
                /* Another thread wrote something to dest */
                break;
            in_dst = value;

            /* Close the slot in the old buffer */
            if (KOS_atomic_cas_weak_ptr(src[i], value, CLOSED)) {
                salvaged = 1;
                break;
            }
            /* If failed to close, someone wrote a new value, try again */
        }

        if (salvaged)
            KOS_PERF_CNT(array_salvage_success);
        else
            KOS_PERF_CNT(array_salvage_fail);

        /* Exit early if another thread finished it */
        if ( ! salvaged && KOS_atomic_read_relaxed_u32(old_buf->num_slots_open) == 0)
            break;

        /* Update number of closed slots */
        if (salvaged && KOS_atomic_add_i32(old_buf->num_slots_open, -1) == 1)
            break;

        /* Try next slot */
        ++i;
        if (i == capacity)
            i = 0;
    }

    (void)KOS_atomic_cas_strong_ptr(array->data,
                                    OBJID(ARRAY_STORAGE, old_buf),
                                    OBJID(ARRAY_STORAGE, new_buf));
}

KOS_OBJ_ID KOS_array_read(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, int idx)
{
    KOS_OBJ_ID elem = KOS_BADPTR;

    assert( ! IS_BAD_PTR(obj_id));

    if ((GET_OBJ_TYPE(obj_id) != OBJ_ARRAY) || kos_seq_fail())
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else {
        const uint32_t size   = KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->size);
        const uint32_t bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {
            KOS_ARRAY_STORAGE *buf = get_data(obj_id);

            for (;;) {
                elem = KOS_atomic_read_relaxed_obj(buf->buf[bufidx]);

                if (elem == TOMBSTONE) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
                    elem = KOS_BADPTR;
                    break;
                }

                if (elem == CLOSED)
                    buf = get_next(buf);
                else
                    break;
            }
        }
        else
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
    }

    return elem;
}

int KOS_array_write(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, int idx, KOS_OBJ_ID value)
{
    int error = KOS_ERROR_EXCEPTION;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        const uint32_t   size   = KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->size);
        const uint32_t   bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {

            KOS_ARRAY_STORAGE *buf = get_data(obj_id);

            for (;;) {

                KOS_OBJ_ID cur = KOS_atomic_read_relaxed_obj(buf->buf[bufidx]);

                if (cur == TOMBSTONE) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
                    break;
                }

                if (cur == CLOSED) {
                    KOS_ARRAY_STORAGE *new_buf = get_next(buf);
                    copy_buf(ctx, OBJPTR(ARRAY, obj_id), buf, new_buf);
                    buf = new_buf;
                }
                else if (KOS_atomic_cas_weak_ptr(buf->buf[bufidx], cur, value)) {
                    error = KOS_SUCCESS;
                    break;
                }
            }
        }
        else
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
    }

    return error;
}

KOS_OBJ_ID KOS_array_cas(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id,
                         int         idx,
                         KOS_OBJ_ID  old_value,
                         KOS_OBJ_ID  new_value)
{
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        const uint32_t size   = KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->size);
        const uint32_t bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {

            KOS_ARRAY_STORAGE *buf = get_data(obj_id);

            for (;;) {

                KOS_OBJ_ID cur = KOS_atomic_read_relaxed_obj(buf->buf[bufidx]);

                if (cur == TOMBSTONE) {
                    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
                    break;
                }

                if (cur == CLOSED) {
                    KOS_ARRAY_STORAGE *new_buf = get_next(buf);
                    copy_buf(ctx, OBJPTR(ARRAY, obj_id), buf, new_buf);
                    buf = new_buf;
                }
                else if (cur != old_value) {
                    retval = cur;
                    break;
                }
                else if (KOS_atomic_cas_weak_ptr(buf->buf[bufidx], cur, new_value)) {
                    retval = cur;
                    break;
                }
            }
        }
        else
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
    }

    return retval;
}

static int resize_storage(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  obj_id,
                          uint32_t    new_capacity)
{
    KOS_ARRAY_STORAGE *old_buf;
    KOS_ARRAY_STORAGE *new_buf;
    KOS_LOCAL          array;
    int                error = KOS_ERROR_EXCEPTION;

    KOS_init_local_with(ctx, &array, obj_id);

    new_buf = alloc_buffer(ctx, new_capacity);

    if (new_buf) {

        old_buf = get_data(array.o);

        atomic_fill_ptr(&new_buf->buf[0], KOS_atomic_read_relaxed_u32(new_buf->capacity), TOMBSTONE);

        if ( ! old_buf)
            (void)KOS_atomic_cas_strong_ptr(OBJPTR(ARRAY, array.o)->data, KOS_BADPTR, OBJID(ARRAY_STORAGE, new_buf));
        else if (KOS_atomic_cas_strong_ptr(old_buf->next, KOS_BADPTR, OBJID(ARRAY_STORAGE, new_buf)))
            copy_buf(ctx, OBJPTR(ARRAY, array.o), old_buf, new_buf);
        else {
            KOS_ARRAY_STORAGE *const buf = get_next(old_buf);

            copy_buf(ctx, OBJPTR(ARRAY, array.o), old_buf, buf);
        }

        error = KOS_SUCCESS;
    }

    KOS_destroy_top_local(ctx, &array);

    return error;
}

int kos_array_copy_storage(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id)
{
    KOS_ARRAY_STORAGE *buf;
    uint32_t           capacity;

    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

    buf      = get_data(obj_id);
    capacity = buf ? KOS_atomic_read_relaxed_u32(buf->capacity) : 0U;

    return resize_storage(ctx, obj_id, capacity);
}

int KOS_array_reserve(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, uint32_t new_capacity)
{
    int       error = KOS_ERROR_EXCEPTION;
    KOS_LOCAL array;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local_with(ctx, &array, obj_id);

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, array.o)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        KOS_ARRAY_STORAGE *old_buf  = get_data(array.o);
        uint32_t           capacity = old_buf ? KOS_atomic_read_relaxed_u32(old_buf->capacity) : 0U;

        error = KOS_SUCCESS;

        while (new_capacity > capacity) {
            error = resize_storage(ctx, array.o, new_capacity);
            if (error)
                break;

            old_buf = get_data(array.o);
            assert(old_buf);
            capacity = KOS_atomic_read_relaxed_u32(old_buf->capacity);
        }
    }

    KOS_destroy_top_local(ctx, &array);

    return error;
}

int KOS_array_resize(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, uint32_t size)
{
    int       error = KOS_ERROR_EXCEPTION;
    KOS_LOCAL array;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local_with(ctx, &array, obj_id);

#ifdef CONFIG_MAD_GC
    error = kos_trigger_mad_gc(ctx);
    if (error) {
        KOS_destroy_top_local(ctx, &array);
        return error;
    }
    error = KOS_ERROR_EXCEPTION;
#endif

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, array.o)->flags) & KOS_READ_ONLY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
    else {
        KOS_ARRAY_STORAGE *buf      = get_data(array.o);
        const uint32_t     capacity = buf ? KOS_atomic_read_relaxed_u32(buf->capacity) : 0U;
        uint32_t           old_size;

        if (size > capacity) {
            const uint32_t new_cap = KOS_max(capacity * 2, size);

            TRY(KOS_array_reserve(ctx, array.o, new_cap));

            buf = get_data(array.o);
        }

        old_size = KOS_atomic_swap_u32(OBJPTR(ARRAY, array.o)->size, size);

        if (size != old_size) {
            if (size > old_size) {
                const KOS_OBJ_ID void_obj = KOS_VOID;
                /* TODO try to improve this */
                KOS_ATOMIC(KOS_OBJ_ID) *ptr = &buf->buf[old_size];
                KOS_ATOMIC(KOS_OBJ_ID) *end = &buf->buf[size];
                while (ptr < end)
                    KOS_atomic_write_relaxed_ptr(*(ptr++), void_obj);
            }
            else {
                /* TODO try to improve this */
                KOS_ATOMIC(KOS_OBJ_ID) *ptr = &buf->buf[size];
                KOS_ATOMIC(KOS_OBJ_ID) *end = &buf->buf[old_size];
                while (ptr < end)
                    KOS_atomic_write_relaxed_ptr(*(ptr++), TOMBSTONE);
            }
        }

        error = KOS_SUCCESS;
    }

cleanup:
    KOS_destroy_top_local(ctx, &array);

    return error;
}

KOS_OBJ_ID KOS_array_slice(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           int64_t     begin,
                           int64_t     end)
{
    KOS_LOCAL array;
    KOS_LOCAL ret;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local(ctx, &ret);
    KOS_init_local_with(ctx, &array, obj_id);

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
    else {
        const uint32_t len = KOS_get_array_size(array.o);

        ret.o = KOS_new_array(ctx, 0);

        if (len && ! IS_BAD_PTR(ret.o)) {

            uint32_t new_len;
            int64_t  new_len_64;

            begin = KOS_fix_index(begin, len);
            end   = KOS_fix_index(end, len);

            if (end < begin)
                end = begin;

            new_len_64 = end - begin;
            assert(new_len_64 <= 0xFFFFFFFF);
            new_len = (uint32_t)new_len_64;

            if (new_len) {

                KOS_ARRAY_STORAGE *dest_buf = alloc_buffer(ctx, new_len);

                if (dest_buf) {
                    KOS_ARRAY   *const new_array = OBJPTR(ARRAY, ret.o);
                    KOS_ARRAY_STORAGE *src_buf   = get_data(array.o);
                    uint32_t           idx       = 0;

                    KOS_ATOMIC(KOS_OBJ_ID) *dest = &dest_buf->buf[0];

                    KOS_atomic_write_relaxed_ptr(new_array->data, OBJID(ARRAY_STORAGE, dest_buf));

                    while (idx < new_len) {

                        const KOS_OBJ_ID value =
                            KOS_atomic_read_relaxed_obj(src_buf->buf[begin + idx]);

                        if (value == TOMBSTONE) {
                            new_len = idx;
                            break;
                        }

                        if (value == CLOSED) {
                            src_buf = get_next(src_buf);
                            continue;
                        }

                        KOS_atomic_write_relaxed_ptr(*(dest++), value);
                        ++idx;
                    }

                    KOS_atomic_write_relaxed_u32(new_array->size, new_len);

                    if (new_len < KOS_atomic_read_relaxed_u32(dest_buf->capacity))
                        atomic_fill_ptr(&dest_buf->buf[new_len],
                                        KOS_atomic_read_relaxed_u32(dest_buf->capacity) - new_len,
                                        TOMBSTONE);
                }
                else
                    ret.o = KOS_BADPTR;
            }
        }
    }

    return KOS_destroy_top_locals(ctx, &array, &ret);
}

int KOS_array_insert(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  dest_obj_id,
                     int64_t     dest_begin,
                     int64_t     dest_end,
                     KOS_OBJ_ID  src_obj_id,
                     int64_t     src_begin,
                     int64_t     src_end)
{
    /* TODO rewrite lock-free */

    int                error   = KOS_SUCCESS;
    uint32_t           dest_len;
    uint32_t           src_len = 0;
    uint32_t           dest_delta;
    uint32_t           src_delta;
    KOS_LOCAL          src;
    KOS_LOCAL          dest;
    KOS_ARRAY_STORAGE *dest_buf;
    KOS_ARRAY_STORAGE *src_buf = KOS_NULL;

    assert( ! IS_BAD_PTR(src_obj_id));
    assert( ! IS_BAD_PTR(dest_obj_id));

    KOS_init_local_with(ctx, &dest, dest_obj_id);
    KOS_init_local_with(ctx, &src,  src_obj_id);

    if (GET_OBJ_TYPE(dest.o) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, dest.o)->flags) & KOS_READ_ONLY)
        RAISE_EXCEPTION_STR(str_err_read_only);
    else if (src_begin != src_end && GET_OBJ_TYPE(src.o) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);

    dest_len = KOS_get_array_size(dest.o);

    dest_begin = KOS_fix_index(dest_begin, dest_len);
    dest_end   = KOS_fix_index(dest_end, dest_len);

    if (dest_end < dest_begin)
        dest_end = dest_begin;

    dest_delta = (uint32_t)(dest_end - dest_begin);

    if (src_begin != src_end) {
        src_len   = KOS_get_array_size(src.o);
        src_begin = KOS_fix_index(src_begin, src_len);
        src_end   = KOS_fix_index(src_end, src_len);

        if (src_end < src_begin)
            src_end = src_begin;
    }

    src_delta = (uint32_t)(src_end - src_begin);

    if (src_delta > dest_delta)
        TRY(KOS_array_resize(ctx, dest.o, dest_len - dest_delta + src_delta));

    dest_buf = get_data(dest.o);
    if (src_begin != src_end)
        src_buf = get_data(src.o);

    if (src.o != dest.o || src_end <= dest_begin || src_begin >= dest_end || ! src_delta) {

        if (src_delta != dest_delta && dest_end < dest_len)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end - dest_delta + src_delta],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                (unsigned)(dest_len - dest_end));

        if (src.o == dest.o && src_begin >= dest_end)
            src_begin += src_delta - dest_delta;

        if (src_delta)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                (KOS_ATOMIC(void *) *)&src_buf->buf[src_begin],
                                src_delta);
    }
    else if (dest_delta >= src_delta) {

        if (src_begin != dest_begin)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[src_begin],
                                src_delta);

        if (dest_end < dest_len)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + src_delta],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                (unsigned)(dest_len - dest_end));
    }
    else {

        const int64_t mid = KOS_min(dest_begin + src_delta, src_end);

        if (dest_end < dest_len)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + src_delta],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                (unsigned)(dest_len - dest_end));
        if (mid > src_begin)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[src_begin],
                                (unsigned)(mid - src_begin));

        if (mid < src_end)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + mid - src_begin],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[mid + src_delta - dest_delta],
                                (unsigned)(src_end - mid));
    }

    if (src_delta < dest_delta)
        TRY(KOS_array_resize(ctx, dest.o, dest_len - dest_delta + src_delta));

cleanup:
    KOS_destroy_top_locals(ctx, &src, &dest);

    return error;
}

int KOS_array_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  obj_id,
                   KOS_OBJ_ID  value_id,
                   uint32_t   *idx)
{
    int                error = KOS_SUCCESS;
    KOS_LOCAL          array;
    KOS_LOCAL          value;
    KOS_ARRAY_STORAGE *buf;
    uint32_t           len;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local_with(ctx, &value, value_id);
    KOS_init_local_with(ctx, &array, obj_id);

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, array.o)->flags) & KOS_READ_ONLY)
        RAISE_EXCEPTION_STR(str_err_read_only);

    buf = get_data(array.o);

    /* Increment index */
    for (;;) {

        const uint32_t capacity = buf ? KOS_atomic_read_relaxed_u32(buf->capacity) : 0;

        len = KOS_get_array_size(array.o);

        if (len >= capacity) {
            const uint32_t new_cap = KOS_max(capacity * 2, len + 1);

            TRY(KOS_array_reserve(ctx, array.o, new_cap));

            buf = get_data(array.o);
            continue;
        }

        /* TODO this is not atomic wrt pop! */

        if (KOS_atomic_cas_weak_u32(OBJPTR(ARRAY, array.o)->size, len, len+1))
            break;
    }

    /* Write new value */
    for (;;) {

        const KOS_OBJ_ID cur_value = KOS_atomic_read_relaxed_ptr(buf->buf[len]);

        if (cur_value == CLOSED) {
            buf = get_next(buf);
            continue;
        }

        /* TODO What if cur_value != TOMBSTONE ??? ABA? */

        if (KOS_atomic_cas_weak_ptr(buf->buf[len], cur_value, value.o))
            break;
    }

    if (idx)
        *idx = len;

cleanup:
    KOS_destroy_top_locals(ctx, &array, &value);

    return error;
}

KOS_OBJ_ID KOS_array_pop(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id)
{
    /* TODO rewrite lock-free */

    int       error = KOS_SUCCESS;
    uint32_t  len;
    KOS_LOCAL array;
    KOS_LOCAL ret;

    assert( ! IS_BAD_PTR(obj_id));

    KOS_init_local(ctx, &ret);
    KOS_init_local_with(ctx, &array, obj_id);

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, array.o)->flags) & KOS_READ_ONLY)
        RAISE_EXCEPTION_STR(str_err_read_only);

    len = KOS_get_array_size(array.o);

    if (len == 0)
        RAISE_EXCEPTION_STR(str_err_empty);

    ret.o = KOS_array_read(ctx, array.o, (int)len-1);
    TRY_OBJID(ret.o);

    error = KOS_array_resize(ctx, array.o, len-1);

cleanup:
    ret.o = KOS_destroy_top_locals(ctx, &array, &ret);

    return error ? KOS_BADPTR : ret.o;
}

int KOS_array_fill(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  obj_id,
                   int64_t     begin,
                   int64_t     end,
                   KOS_OBJ_ID  value)
{
    uint32_t           len;
    KOS_ARRAY_STORAGE *buf;

    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_array));
        return KOS_ERROR_EXCEPTION;
    }
    else if (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, obj_id)->flags) & KOS_READ_ONLY) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_read_only));
        return KOS_ERROR_EXCEPTION;
    }

    len = KOS_get_array_size(obj_id);

    begin = KOS_fix_index(begin, len);
    end   = KOS_fix_index(end, len);

    buf = get_data(obj_id);

    while (begin < end) {

        KOS_OBJ_ID cur = KOS_atomic_read_relaxed_obj(buf->buf[begin]);

        if (cur == TOMBSTONE)
            break;

        if (cur == CLOSED) {
            KOS_ARRAY_STORAGE *new_buf = get_next(buf);
            copy_buf(ctx, OBJPTR(ARRAY, obj_id), buf, new_buf);
            buf = new_buf;
        }
        else {
            if (KOS_atomic_cas_weak_ptr(buf->buf[begin], cur, value))
                ++begin;
        }
    }

    return KOS_SUCCESS;
}

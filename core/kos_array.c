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

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_threads.h"
#include "kos_config.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>

static const char str_err_empty[]         = "array is empty";
static const char str_err_invalid_index[] = "array index is out of range";
static const char str_err_not_array[]     = "object is not an array";
static const char str_err_null_ptr[]      = "null pointer";

DECLARE_STATIC_CONST_OBJECT(tombstone) = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xA0);
DECLARE_STATIC_CONST_OBJECT(closed)    = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xA1);

/* TOMBSTONE indicates that an array element has been deleted due to a resize. */
#define TOMBSTONE KOS_CONST_ID(tombstone)
/* CLOSED indicates that an array element has been moved to a new buffer. */
#define CLOSED    KOS_CONST_ID(closed)

#define KOS_buffer_alloc_size(cap) (KOS_align_up((uint32_t)sizeof(KOS_ARRAY_STORAGE) \
                                                   + (uint32_t)(((cap) - 1) * sizeof(KOS_OBJ_ID)), \
                                                 1U << _KOS_OBJ_ALIGN_BITS))

static void _atomic_fill_ptr(KOS_ATOMIC(KOS_OBJ_ID) *dest,
                             unsigned                count,
                             KOS_OBJ_ID              value)
{
    KOS_ATOMIC(KOS_OBJ_ID) *const end = dest + count;
    while (dest < end) {
        KOS_atomic_write_ptr(*dest, value);
        ++dest;
    }
}

static KOS_ARRAY_STORAGE *_alloc_buffer(KOS_CONTEXT ctx, uint32_t capacity)
{
    KOS_ARRAY_STORAGE *buf            = 0;
    const uint32_t     buf_alloc_size = KOS_buffer_alloc_size(capacity);

    if (capacity < 256U * 1024U * 1024U)
        buf = (KOS_ARRAY_STORAGE *)kos_alloc_object(ctx,
                                                    OBJ_ARRAY_STORAGE,
                                                    buf_alloc_size);
    else
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));

    if (buf) {
        assert(buf->header.type == OBJ_ARRAY_STORAGE);
        capacity = 1U + (buf_alloc_size - sizeof(KOS_ARRAY_STORAGE)) / sizeof(KOS_OBJ_ID);
        KOS_atomic_write_u32(buf->capacity,       capacity);
        KOS_atomic_write_u32(buf->num_slots_open, capacity);
        KOS_atomic_write_ptr(buf->next,           KOS_BADPTR);
    }

    return buf;
}

KOS_OBJ_ID KOS_new_array(KOS_CONTEXT ctx,
                         uint32_t    size)
{
    const uint32_t array_obj_size = KOS_align_up((uint32_t)sizeof(KOS_ARRAY), 1U << _KOS_OBJ_ALIGN_BITS);
    const uint32_t buf_alloc_size = size ? KOS_buffer_alloc_size(size) : 0;
    const int      buf_built_in   = array_obj_size + buf_alloc_size <= 256U;
    const uint32_t alloc_size     = buf_built_in ? array_obj_size + buf_alloc_size : array_obj_size;
    KOS_ARRAY     *array          = (KOS_ARRAY *)kos_alloc_object(ctx,
                                                                  OBJ_ARRAY,
                                                                  alloc_size);

    if (array) {
        KOS_ARRAY_STORAGE *storage = 0;

        if (buf_built_in) {
            if (buf_alloc_size) {

                const uint32_t capacity = 1U + (buf_alloc_size - sizeof(KOS_ARRAY_STORAGE)) / sizeof(KOS_OBJ_ID);

                storage = (KOS_ARRAY_STORAGE *)((uint8_t *)array + array_obj_size);
                storage->header.alloc_size = TO_SMALL_INT(buf_alloc_size);
                storage->header.type       = OBJ_ARRAY_STORAGE;

                KOS_atomic_write_u32(storage->capacity,       capacity);
                KOS_atomic_write_u32(storage->num_slots_open, capacity);
                KOS_atomic_write_ptr(storage->next,           KOS_BADPTR);

                array->header.alloc_size = TO_SMALL_INT(array_obj_size);

                KOS_atomic_write_ptr(array->data, OBJID(ARRAY_STORAGE, storage));
            }
            else
                KOS_atomic_write_ptr(array->data, KOS_BADPTR);
        }
        else {
            KOS_OBJ_ID array_id;

            KOS_atomic_write_ptr(array->data, KOS_BADPTR);

            array_id = OBJID(ARRAY, array);
            kos_track_refs(ctx, 1, &array_id);

            storage = _alloc_buffer(ctx, size);

            kos_untrack_refs(ctx, 1);

            if (storage) {
                array = OBJPTR(ARRAY, array_id);

                KOS_atomic_write_ptr(array->data, OBJID(ARRAY_STORAGE, storage));
            }
            else
                array = 0;
        }

        if (array) {

            kos_set_return_value(ctx, OBJID(ARRAY, array));

            KOS_atomic_write_u32(array->size, size);

            if (storage) {
                const uint32_t capacity = KOS_atomic_read_u32(storage->capacity);

                if (size)
                    _atomic_fill_ptr(&storage->buf[0], size, KOS_VOID);

                if (size < capacity)
                    _atomic_fill_ptr(&storage->buf[size], capacity - size, TOMBSTONE);
            }
        }
    }

    return OBJID(ARRAY, array);
}

static KOS_ARRAY_STORAGE *_get_data(KOS_OBJ_ID obj_id)
{
    const KOS_OBJ_ID buf_obj = kos_get_array_storage(obj_id);
    /* TODO use read with acquire semantics */
    KOS_atomic_acquire_barrier();
    return IS_BAD_PTR(buf_obj) ? 0 : OBJPTR(ARRAY_STORAGE, buf_obj);
}

static KOS_ARRAY_STORAGE *_get_next(KOS_ARRAY_STORAGE *storage)
{
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_obj(storage->next);
    return IS_BAD_PTR(buf_obj) ? 0 : OBJPTR(ARRAY_STORAGE, buf_obj);
}

static void _copy_buf(KOS_CONTEXT        ctx,
                      KOS_ARRAY         *array,
                      KOS_ARRAY_STORAGE *old_buf,
                      KOS_ARRAY_STORAGE *new_buf)
{
    KOS_ATOMIC(KOS_OBJ_ID) *src      = &old_buf->buf[0];
    KOS_ATOMIC(KOS_OBJ_ID) *dst      = &new_buf->buf[0];
    const uint32_t          capacity = KOS_atomic_read_u32(old_buf->capacity);
    const uint32_t          fuzz     = KOS_atomic_read_u32(old_buf->num_slots_open);
    uint32_t                i        = (capacity - fuzz) % capacity;

    for (;;) {
        KOS_OBJ_ID in_dst   = TOMBSTONE;
        int        salvaged = 0;

        /* Salvage item to the new buffer */
        for (;;) {
            const KOS_OBJ_ID value = KOS_atomic_read_obj(src[i]);

            /* Another thread copied it */
            if (value == CLOSED)
                break;

            /* Write value to new buffer */
            if ( ! KOS_atomic_cas_ptr(dst[i], in_dst, value))
                /* Another thread wrote something to dest */
                break;
            in_dst = value;

            /* Close the slot in the old buffer */
            if (KOS_atomic_cas_ptr(src[i], value, CLOSED)) {
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
        if ( ! salvaged && KOS_atomic_read_u32(old_buf->num_slots_open) == 0)
            break;

        /* Update number of closed slots */
        if (salvaged && KOS_atomic_add_i32(old_buf->num_slots_open, -1) == 1)
            break;

        /* Try next slot */
        ++i;
        if (i == capacity)
            i = 0;
    }

    (void)KOS_atomic_cas_ptr(array->data,
                             OBJID(ARRAY_STORAGE, old_buf),
                             OBJID(ARRAY_STORAGE, new_buf));
}

KOS_OBJ_ID KOS_array_read(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, int idx)
{
    KOS_OBJ_ID elem = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(ctx, str_err_not_array);
    else {
        const uint32_t size   = KOS_atomic_read_u32(OBJPTR(ARRAY, obj_id)->size);
        const uint32_t bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {
            KOS_ARRAY_STORAGE *buf = _get_data(obj_id);

            for (;;) {
                elem = KOS_atomic_read_obj(buf->buf[bufidx]);

                if (elem == TOMBSTONE) {
                    KOS_raise_exception_cstring(ctx, str_err_invalid_index);
                    elem = KOS_BADPTR;
                    break;
                }

                if (elem == CLOSED)
                    buf = _get_next(buf);
                else {
                    kos_set_return_value(ctx, elem);
                    break;
                }
            }
        }
        else
            KOS_raise_exception_cstring(ctx, str_err_invalid_index);
    }

    return elem;
}

int KOS_array_write(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, int idx, KOS_OBJ_ID value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(ctx, str_err_not_array);
    else {
        const uint32_t   size   = KOS_atomic_read_u32(OBJPTR(ARRAY, obj_id)->size);
        const uint32_t   bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {

            KOS_ARRAY_STORAGE *buf = _get_data(obj_id);

            for (;;) {

                KOS_OBJ_ID cur = KOS_atomic_read_obj(buf->buf[bufidx]);

                if (cur == TOMBSTONE) {
                    KOS_raise_exception_cstring(ctx, str_err_invalid_index);
                    break;
                }

                if (cur == CLOSED) {
                    KOS_ARRAY_STORAGE *new_buf = _get_next(buf);
                    _copy_buf(ctx, OBJPTR(ARRAY, obj_id), buf, new_buf);
                    buf = new_buf;
                }
                else {
                    if (KOS_atomic_cas_ptr(buf->buf[bufidx], cur, value)) {
                        error = KOS_SUCCESS;
                        break;
                    }
                }
            }
        }
        else
            KOS_raise_exception_cstring(ctx, str_err_invalid_index);
    }

    return error;
}

static int _resize_storage(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           uint32_t    new_capacity)
{
    KOS_ARRAY_STORAGE *old_buf;
    KOS_ARRAY_STORAGE *new_buf;

    kos_track_refs(ctx, 1, &obj_id);

    new_buf = _alloc_buffer(ctx, new_capacity);

    kos_untrack_refs(ctx, 1);

    if ( ! new_buf)
        return KOS_ERROR_EXCEPTION;

    old_buf = _get_data(obj_id);

    _atomic_fill_ptr(&new_buf->buf[0], KOS_atomic_read_u32(new_buf->capacity), TOMBSTONE);

    if (!old_buf)
        (void)KOS_atomic_cas_ptr(OBJPTR(ARRAY, obj_id)->data, KOS_BADPTR, OBJID(ARRAY_STORAGE, new_buf));
    else if (KOS_atomic_cas_ptr(old_buf->next, KOS_BADPTR, OBJID(ARRAY_STORAGE, new_buf)))
        _copy_buf(ctx, OBJPTR(ARRAY, obj_id), old_buf, new_buf);
    else {
        KOS_ARRAY_STORAGE *const buf = _get_next(old_buf);

        _copy_buf(ctx, OBJPTR(ARRAY, obj_id), old_buf, buf);
    }

    return KOS_SUCCESS;
}

int kos_array_copy_storage(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id)
{
    KOS_ARRAY_STORAGE *buf;
    uint32_t           capacity;

    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

    buf      = _get_data(obj_id);
    capacity = buf ? KOS_atomic_read_u32(buf->capacity) : 0U;

    return _resize_storage(ctx, obj_id, capacity);
}

int KOS_array_reserve(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, uint32_t new_capacity)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(ctx, str_err_not_array);
    else {
        KOS_ARRAY_STORAGE *old_buf  = _get_data(obj_id);
        uint32_t           capacity = old_buf ? KOS_atomic_read_u32(old_buf->capacity) : 0U;

        kos_track_refs(ctx, 1, &obj_id);

        error = KOS_SUCCESS;

        while (new_capacity > capacity) {
            error = _resize_storage(ctx, obj_id, new_capacity);
            if (error)
                break;

            old_buf = _get_data(obj_id);
            assert(old_buf);
            capacity = KOS_atomic_read_u32(old_buf->capacity);
        }

        kos_untrack_refs(ctx, 1);
    }

    return error;
}

int KOS_array_resize(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id, uint32_t size)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(ctx, str_err_not_array);
    else {
        KOS_ARRAY_STORAGE *buf      = _get_data(obj_id);
        const uint32_t     capacity = buf ? KOS_atomic_read_u32(buf->capacity) : 0U;
        uint32_t           old_size;

        if (size > capacity) {
            const uint32_t new_cap = KOS_max(capacity * 2, size);

            kos_track_refs(ctx, 1, &obj_id);

            TRY(KOS_array_reserve(ctx, obj_id, new_cap));

            kos_untrack_refs(ctx, 1);

            buf = _get_data(obj_id);
        }

        old_size = KOS_atomic_swap_u32(OBJPTR(ARRAY, obj_id)->size, size);

        if (size != old_size) {
            if (size > old_size) {
                const KOS_OBJ_ID void_obj = KOS_VOID;
                /* TODO try to improve this */
                KOS_ATOMIC(KOS_OBJ_ID) *ptr = &buf->buf[old_size];
                KOS_ATOMIC(KOS_OBJ_ID) *end = &buf->buf[size];
                while (ptr < end)
                    KOS_atomic_write_ptr(*(ptr++), void_obj);
            }
            else {
                /* TODO try to improve this */
                KOS_ATOMIC(KOS_OBJ_ID) *ptr = &buf->buf[size];
                KOS_ATOMIC(KOS_OBJ_ID) *end = &buf->buf[old_size];
                while (ptr < end)
                    KOS_atomic_write_ptr(*(ptr++), TOMBSTONE);
            }
        }

        error = KOS_SUCCESS;
    }

cleanup:
    return error;
}

KOS_OBJ_ID KOS_array_slice(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           int64_t     begin,
                           int64_t     end)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(ctx, str_err_not_array);
    else {
        const uint32_t len = KOS_get_array_size(obj_id);

        kos_track_refs(ctx, 1, &obj_id);

        ret = KOS_new_array(ctx, 0);

        kos_track_refs(ctx, 1, &ret);

        if (len && ! IS_BAD_PTR(ret)) {

            uint32_t new_len;
            int64_t  new_len_64;

            begin = kos_fix_index(begin, len);
            end   = kos_fix_index(end, len);

            if (end < begin)
                end = begin;

            new_len_64 = end - begin;
            assert(new_len_64 <= 0xFFFFFFFF);
            new_len = (uint32_t)new_len_64;

            if (new_len) {

                KOS_ARRAY_STORAGE *dest_buf = _alloc_buffer(ctx, new_len);

                if (dest_buf && ! IS_BAD_PTR(ret)) {
                    KOS_ARRAY   *const new_array = OBJPTR(ARRAY, ret);
                    KOS_ARRAY_STORAGE *src_buf   = _get_data(obj_id);
                    uint32_t           idx       = 0;

                    KOS_ATOMIC(KOS_OBJ_ID) *dest = &dest_buf->buf[0];

                    KOS_atomic_write_ptr(new_array->data, OBJID(ARRAY_STORAGE, dest_buf));

                    while (idx < new_len) {

                        const KOS_OBJ_ID value =
                            KOS_atomic_read_obj(src_buf->buf[begin + idx]);

                        if (value == TOMBSTONE) {
                            new_len = idx;
                            break;
                        }

                        if (value == CLOSED) {
                            src_buf = _get_next(src_buf);
                            continue;
                        }

                        KOS_atomic_write_ptr(*(dest++), value);
                        ++idx;
                    }

                    KOS_atomic_write_u32(new_array->size, new_len);

                    if (new_len < KOS_atomic_read_u32(dest_buf->capacity))
                        _atomic_fill_ptr(&dest_buf->buf[new_len],
                                         KOS_atomic_read_u32(dest_buf->capacity) - new_len,
                                         TOMBSTONE);
                }
                else
                    ret = KOS_BADPTR;
            }
        }

        kos_untrack_refs(ctx, 2);
    }

    return ret;
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
    KOS_ARRAY_STORAGE *dest_buf;
    KOS_ARRAY_STORAGE *src_buf = 0;
    uint32_t           dest_delta;
    uint32_t           src_delta;

    if (IS_BAD_PTR(dest_obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(dest_obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);
    else if (src_begin != src_end && IS_BAD_PTR(src_obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (src_begin != src_end && GET_OBJ_TYPE(src_obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    dest_len = KOS_get_array_size(dest_obj_id);

    dest_begin = kos_fix_index(dest_begin, dest_len);
    dest_end   = kos_fix_index(dest_end, dest_len);

    if (dest_end < dest_begin)
        dest_end = dest_begin;

    dest_delta = (uint32_t)(dest_end - dest_begin);

    if (src_begin != src_end) {
        src_len   = KOS_get_array_size(src_obj_id);
        src_begin = kos_fix_index(src_begin, src_len);
        src_end   = kos_fix_index(src_end, src_len);

        if (src_end < src_begin)
            src_end = src_begin;
    }

    src_delta = (uint32_t)(src_end - src_begin);

    if (src_delta > dest_delta) {
        kos_track_refs(ctx, 2, &dest_obj_id, &src_obj_id);

        TRY(KOS_array_resize(ctx, dest_obj_id, dest_len - dest_delta + src_delta));

        kos_untrack_refs(ctx, 2);
    }

    dest_buf = _get_data(dest_obj_id);
    if (src_begin != src_end)
        src_buf = _get_data(src_obj_id);

    if (src_obj_id != dest_obj_id || src_end <= dest_begin || src_begin >= dest_end || ! src_delta) {

        if (src_delta != dest_delta && dest_end < dest_len)
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end - dest_delta + src_delta],
                                (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                (unsigned)(dest_len - dest_end));

        if (src_obj_id == dest_obj_id && src_begin >= dest_end)
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
        TRY(KOS_array_resize(ctx, dest_obj_id, dest_len - dest_delta + src_delta));

cleanup:
    return error;
}

int KOS_array_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  obj_id,
                   KOS_OBJ_ID  value,
                   uint32_t   *idx)
{
    int                error = KOS_SUCCESS;
    KOS_ARRAY_STORAGE *buf;
    uint32_t           len;

    if (IS_BAD_PTR(obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    buf = _get_data(obj_id);

    /* Increment index */
    for (;;) {

        const uint32_t capacity = buf ? KOS_atomic_read_u32(buf->capacity) : 0;

        len = KOS_get_array_size(obj_id);

        if (len >= capacity) {
            const uint32_t new_cap = KOS_max(capacity * 2, len + 1);

            kos_track_refs(ctx, 2, &obj_id, &value);

            TRY(KOS_array_reserve(ctx, obj_id, new_cap));

            kos_untrack_refs(ctx, 2);

            buf = _get_data(obj_id);
            continue;
        }

        /* TODO this is not atomic wrt pop! */

        if (KOS_atomic_cas_u32(OBJPTR(ARRAY, obj_id)->size, len, len+1))
            break;
    }

    /* Write new value */
    for (;;) {

        const KOS_OBJ_ID cur_value = KOS_atomic_read_ptr(buf->buf[len]);

        if (cur_value == CLOSED) {
            buf = _get_next(buf);
            continue;
        }

        /* TODO What if cur_value != TOMBSTONE ??? ABA? */

        if (KOS_atomic_cas_ptr(buf->buf[len], cur_value, value))
            break;
    }

    if (idx)
        *idx = len;

cleanup:
    return error;
}

KOS_OBJ_ID KOS_array_pop(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id)
{
    /* TODO rewrite lock-free */

    int        error = KOS_SUCCESS;
    uint32_t   len;
    KOS_OBJ_ID ret = KOS_BADPTR;

    kos_track_refs(ctx, 2, &obj_id, &ret);

    if (IS_BAD_PTR(obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    len = KOS_get_array_size(obj_id);

    if (len == 0)
        RAISE_EXCEPTION(str_err_empty);

    ret = KOS_array_read(ctx, obj_id, (int)len-1);
    TRY_OBJID(ret);

    error = KOS_array_resize(ctx, obj_id, len-1);

cleanup:
    kos_untrack_refs(ctx, 2);

    return error ? KOS_BADPTR : ret;
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
        KOS_raise_exception_cstring(ctx, str_err_not_array);
        return KOS_ERROR_EXCEPTION;
    }

    len = KOS_get_array_size(obj_id);

    begin = kos_fix_index(begin, len);
    end   = kos_fix_index(end, len);

    buf = _get_data(obj_id);

    while (begin < end) {

        KOS_OBJ_ID cur = KOS_atomic_read_obj(buf->buf[begin]);

        if (cur == TOMBSTONE)
            break;

        if (cur == CLOSED) {
            KOS_ARRAY_STORAGE *new_buf = _get_next(buf);
            _copy_buf(ctx, OBJPTR(ARRAY, obj_id), buf, new_buf);
            buf = new_buf;
        }
        else {
            if (KOS_atomic_cas_ptr(buf->buf[begin], cur, value))
                ++begin;
        }
    }

    return KOS_SUCCESS;
}

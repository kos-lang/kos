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

#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_threads.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>

static const char str_err_empty[]         = "array is empty";
static const char str_err_invalid_index[] = "array index is out of range";
static const char str_err_not_array[]     = "object is not an array";
static const char str_err_null_ptr[]      = "null pointer";
static const char str_err_too_small[]     = "not enough elements to add defaults";

/* TOMBSTONE indicates that an array element has been deleted due to a resize. */
#define TOMBSTONE ((KOS_OBJ_ID)(intptr_t)0xA001)
/* CLOSED indicates that an array element has been moved to a new buffer. */
#define CLOSED    ((KOS_OBJ_ID)(intptr_t)0xA101)

#define KOS_buffer_alloc_size(cap) (sizeof(ARRAY_BUF) + ((cap) - 1) * sizeof(KOS_OBJ_ID))

typedef struct _KOS_ARRAY_BUFFER ARRAY_BUF;

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

static ARRAY_BUF *_alloc_buffer(KOS_FRAME frame, uint32_t capacity)
{
    ARRAY_BUF     *buf;
    const uint32_t max_step = KOS_ARRAY_CAPACITY_STEP / sizeof(KOS_OBJ_ID);

    capacity = KOS_align_up(capacity, max_step);

    buf = (ARRAY_BUF *)_KOS_alloc_buffer(frame, KOS_buffer_alloc_size(capacity));

    if (buf) {
        buf->capacity = capacity;
        KOS_atomic_write_u32(buf->num_slots_open, capacity);
        KOS_atomic_write_ptr(buf->next,           (void *)0);
    }

    return buf;
}

static uint32_t _double_capacity(uint32_t capacity)
{
    const uint32_t max_step = KOS_ARRAY_CAPACITY_STEP / sizeof(KOS_OBJ_ID);
    const uint32_t aligned  = KOS_align_up(capacity + max_step - 1, max_step);
    return KOS_min(capacity * 2, aligned);
}

int _KOS_init_array(KOS_FRAME  frame,
                    KOS_ARRAY *array,
                    uint32_t   size)
{
    int        error = KOS_SUCCESS;
    ARRAY_BUF *buf;
    uint32_t   capacity;

    capacity = KOS_max(size, KOS_MIN_ARRAY_CAPACITY);

    KOS_atomic_write_u32(array->size, size);

    if (size) {
        buf = _alloc_buffer(frame, capacity);
        KOS_atomic_write_ptr(array->data, (void *)buf);

        if (buf) {
            capacity = buf->capacity;

            _atomic_fill_ptr(&buf->buf[0], size, KOS_VOID);

            if (size < capacity)
                _atomic_fill_ptr(&buf->buf[size], capacity - size, TOMBSTONE);
        }
        else
            error = KOS_ERROR_EXCEPTION;
    }
    else
        KOS_atomic_write_ptr(array->data, (void *)0);

    return error;
}

KOS_OBJ_ID KOS_new_array(KOS_FRAME frame,
                         uint32_t  size)
{
    KOS_ARRAY *array = (KOS_ARRAY *)_KOS_alloc_object(frame, ARRAY);

    if (array && KOS_SUCCESS != _KOS_init_array(frame, array, size))
        array = 0;

    return OBJID(ARRAY, array);
}

static void _copy_buf(KOS_FRAME  frame,
                      KOS_ARRAY *array,
                      ARRAY_BUF *old_buf,
                      ARRAY_BUF *new_buf)
{
    KOS_ATOMIC(KOS_OBJ_ID) *src      = &old_buf->buf[0];
    KOS_ATOMIC(KOS_OBJ_ID) *dst      = &new_buf->buf[0];
    const uint32_t          capacity = old_buf->capacity;
    const uint32_t          fuzz     = KOS_atomic_read_u32(old_buf->num_slots_open);
    uint32_t                i        = (capacity - fuzz) % capacity;

    for (;;) {
        KOS_OBJ_ID in_dst   = TOMBSTONE;
        int        salvaged = 0;

        /* Salvage item to the new buffer */
        for (;;) {
            const KOS_OBJ_ID value = (KOS_OBJ_ID)KOS_atomic_read_ptr(src[i]);

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

    if (KOS_atomic_cas_ptr(array->data, (void *)old_buf, (void *)new_buf))
        _KOS_free_buffer(frame, old_buf, KOS_buffer_alloc_size(capacity));
}

KOS_OBJ_ID KOS_array_read(KOS_FRAME frame, KOS_OBJ_ID obj_id, int idx)
{
    KOS_OBJ_ID elem = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(frame, str_err_not_array);
    else {
        KOS_ARRAY *const array  = OBJPTR(ARRAY, obj_id);
        const uint32_t   size   = KOS_atomic_read_u32(array->size);
        const uint32_t   bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {
            ARRAY_BUF *buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);

            for (;;) {
                elem = (KOS_OBJ_ID)KOS_atomic_read_ptr(buf->buf[bufidx]);

                if (elem == TOMBSTONE) {
                    KOS_raise_exception_cstring(frame, str_err_invalid_index);
                    elem = KOS_BADPTR;
                    break;
                }

                if (elem == CLOSED)
                    buf = (ARRAY_BUF *)KOS_atomic_read_ptr(buf->next);
                else
                    break;
            }
        }
        else
            KOS_raise_exception_cstring(frame, str_err_invalid_index);
    }

    return elem;
}

int KOS_array_write(KOS_FRAME frame, KOS_OBJ_ID obj_id, int idx, KOS_OBJ_ID value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(frame, str_err_not_array);
    else {
        KOS_ARRAY *const array  = OBJPTR(ARRAY, obj_id);
        const uint32_t   size   = KOS_atomic_read_u32(array->size);
        const uint32_t   bufidx = (idx < 0) ? ((uint32_t)idx + size) : (uint32_t)idx;

        if (bufidx < size) {

            ARRAY_BUF *buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);

            for (;;) {

                KOS_OBJ_ID cur = (KOS_OBJ_ID)KOS_atomic_read_ptr(buf->buf[bufidx]);

                if (cur == TOMBSTONE) {
                    KOS_raise_exception_cstring(frame, str_err_invalid_index);
                    break;
                }

                if (cur == CLOSED) {
                    ARRAY_BUF *new_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(buf->next);
                    _copy_buf(frame, array, buf, new_buf);
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
            KOS_raise_exception_cstring(frame, str_err_invalid_index);
    }

    return error;
}

static int _resize_storage(KOS_FRAME  frame,
                           KOS_OBJ_ID obj_id,
                           uint32_t   new_capacity)
{
    KOS_ARRAY *const array   = OBJPTR(ARRAY, obj_id);
    ARRAY_BUF       *old_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
    ARRAY_BUF *const new_buf = _alloc_buffer(frame, new_capacity);

    if ( ! new_buf)
        return KOS_ERROR_EXCEPTION;

    _atomic_fill_ptr(&new_buf->buf[0], new_buf->capacity, TOMBSTONE);

    if (!old_buf) {
        if ( ! KOS_atomic_cas_ptr(array->data, (void *)0, (void *)new_buf))
            _KOS_free_buffer(frame, new_buf, KOS_buffer_alloc_size(new_buf->capacity));
    }
    else if (KOS_atomic_cas_ptr(old_buf->next, (void *)0, (void *)new_buf))
        _copy_buf(frame, array, old_buf, new_buf);
    else {
        ARRAY_BUF *const buf = (ARRAY_BUF *)KOS_atomic_read_ptr(old_buf->next);

        _copy_buf(frame, array, old_buf, buf);

        _KOS_free_buffer(frame, new_buf, KOS_buffer_alloc_size(new_buf->capacity));
    }

    return KOS_SUCCESS;
}

int _KOS_array_copy_storage(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id)
{
    KOS_ARRAY *array;
    ARRAY_BUF *buf;
    uint32_t   capacity;

    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

    array    = OBJPTR(ARRAY, obj_id);
    buf      = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
    capacity = buf ? buf->capacity : 0U;

    return _resize_storage(frame, obj_id, capacity);
}

int KOS_array_reserve(KOS_FRAME frame, KOS_OBJ_ID obj_id, uint32_t new_capacity)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(frame, str_err_not_array);
    else {
        KOS_ARRAY *const array    = OBJPTR(ARRAY, obj_id);
        ARRAY_BUF       *old_buf  = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
        uint32_t         capacity = old_buf ? old_buf->capacity : 0U;

        error = KOS_SUCCESS;

        while (new_capacity > capacity) {
            error = _resize_storage(frame, obj_id, new_capacity);
            if (error)
                break;

            old_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
            assert(old_buf);
            capacity = old_buf->capacity;
        }
    }

    return error;
}

int KOS_array_resize(KOS_FRAME frame, KOS_OBJ_ID obj_id, uint32_t size)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(frame, str_err_not_array);
    else {
        KOS_ARRAY *const array    = OBJPTR(ARRAY, obj_id);
        ARRAY_BUF       *buf      = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
        const uint32_t   capacity = buf ? buf->capacity : 0U;
        uint32_t         old_size;

        if (size > capacity) {
            const uint32_t new_cap = KOS_max(_double_capacity(capacity), size);
            TRY(KOS_array_reserve(frame, obj_id, new_cap));

            buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
        }

        old_size = KOS_atomic_swap_u32(array->size, size);

        if (size != old_size) {
            if (size > old_size) {
                /* TODO try to improve this */
                KOS_ATOMIC(KOS_OBJ_ID) *ptr = &buf->buf[old_size];
                KOS_ATOMIC(KOS_OBJ_ID) *end = &buf->buf[size];
                while (ptr < end)
                    KOS_atomic_write_ptr(*(ptr++), KOS_VOID);
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

_error:
    return error;
}

KOS_OBJ_ID KOS_array_slice(KOS_FRAME  frame,
                           KOS_OBJ_ID obj_id,
                           int64_t    begin,
                           int64_t    end)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        KOS_raise_exception_cstring(frame, str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        KOS_raise_exception_cstring(frame, str_err_not_array);
    else {
        KOS_ARRAY *const array = OBJPTR(ARRAY, obj_id);
        const uint32_t   len   = KOS_get_array_size(obj_id);

        ret = KOS_new_array(frame, 0);

        if (len && ! IS_BAD_PTR(ret)) {

            uint32_t new_len;
            int64_t  new_len_64;

            begin = _KOS_fix_index(begin, len);
            end   = _KOS_fix_index(end, len);

            if (end < begin)
                end = begin;

            new_len_64 = end - begin;
            assert(new_len_64 <= 0xFFFFFFFF);
            new_len = (uint32_t)new_len_64;

            if (new_len) {

                ARRAY_BUF *dest_buf = _alloc_buffer(frame, new_len);

                if (dest_buf && ! IS_BAD_PTR(ret)) {
                    KOS_ARRAY *const new_array = OBJPTR(ARRAY, ret);
                    ARRAY_BUF       *src_buf   = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
                    uint32_t         idx       = 0;

                    KOS_ATOMIC(KOS_OBJ_ID) *dest = &dest_buf->buf[0];

                    KOS_atomic_write_ptr(new_array->data, (void *)dest_buf);

                    while (idx < new_len) {

                        const KOS_OBJ_ID value =
                            (KOS_OBJ_ID)KOS_atomic_read_ptr(src_buf->buf[begin + idx]);

                        if (value == TOMBSTONE) {
                            new_len = idx;
                            break;
                        }

                        if (value == CLOSED) {
                            src_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(src_buf->next);
                            continue;
                        }

                        KOS_atomic_write_ptr(*(dest++), value);
                        ++idx;
                    }

                    KOS_atomic_write_u32(new_array->size, new_len);

                    if (new_len < dest_buf->capacity)
                        _atomic_fill_ptr(&dest_buf->buf[new_len],
                                         dest_buf->capacity - new_len,
                                         TOMBSTONE);
                }
                else
                    ret = KOS_BADPTR;
            }
        }
    }

    return ret;
}

int KOS_array_insert(KOS_FRAME  frame,
                     KOS_OBJ_ID dest_obj_id,
                     int64_t    dest_begin,
                     int64_t    dest_end,
                     KOS_OBJ_ID src_obj_id,
                     int64_t    src_begin,
                     int64_t    src_end)
{
    /* TODO rewrite lock-free */

    int        error   = KOS_SUCCESS;
    uint32_t   dest_len;
    uint32_t   src_len = 0;
    ARRAY_BUF *dest_buf;
    ARRAY_BUF *src_buf = 0;
    uint32_t   dest_delta;
    uint32_t   src_delta;

    if (IS_BAD_PTR(dest_obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(dest_obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);
    else if (src_begin != src_end && IS_BAD_PTR(src_obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (src_begin != src_end && GET_OBJ_TYPE(src_obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    dest_len = KOS_get_array_size(dest_obj_id);

    dest_begin = _KOS_fix_index(dest_begin, dest_len);
    dest_end   = _KOS_fix_index(dest_end, dest_len);

    if (dest_end < dest_begin)
        dest_end = dest_begin;

    dest_delta = (uint32_t)(dest_end - dest_begin);

    if (src_begin != src_end) {
        src_len   = KOS_get_array_size(src_obj_id);
        src_begin = _KOS_fix_index(src_begin, src_len);
        src_end   = _KOS_fix_index(src_end, src_len);

        if (src_end < src_begin)
            src_end = src_begin;
    }

    src_delta = (uint32_t)(src_end - src_begin);

    if (src_delta > dest_delta)
        TRY(KOS_array_resize(frame, dest_obj_id, dest_len - dest_delta + src_delta));

    dest_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(OBJPTR(ARRAY, dest_obj_id)->data);
    if (src_begin != src_end)
        src_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(OBJPTR(ARRAY, src_obj_id)->data);

    if (src_obj_id != dest_obj_id || src_end <= dest_begin || src_begin >= dest_end || ! src_delta) {

        if (src_delta != dest_delta && dest_end < dest_len)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end - dest_delta + src_delta],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                 (unsigned)(dest_len - dest_end));

        if (src_obj_id == dest_obj_id && src_begin >= dest_end)
            src_begin += src_delta - dest_delta;

        if (src_delta)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                 (KOS_ATOMIC(void *) *)&src_buf->buf[src_begin],
                                 src_delta);
    }
    else if (dest_delta >= src_delta) {

        if (src_begin != dest_begin)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[src_begin],
                                 src_delta);

        if (dest_end < dest_len)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + src_delta],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                 (unsigned)(dest_len - dest_end));
    }
    else {

        const int64_t mid = KOS_min(dest_begin + src_delta, src_end);

        if (dest_end < dest_len)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + src_delta],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                 (unsigned)(dest_len - dest_end));
        if (mid > src_begin)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[src_begin],
                                 (unsigned)(mid - src_begin));

        if (mid < src_end)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_begin + mid - src_begin],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[mid + src_delta - dest_delta],
                                 (unsigned)(src_end - mid));
    }

    if (src_delta < dest_delta)
        TRY(KOS_array_resize(frame, dest_obj_id, dest_len - dest_delta + src_delta));

_error:
    return error;
}

int KOS_array_push(KOS_FRAME  frame,
                   KOS_OBJ_ID obj_id,
                   KOS_OBJ_ID value,
                   uint32_t  *idx)
{
    int        error = KOS_SUCCESS;
    KOS_ARRAY *array = OBJPTR(ARRAY, obj_id);
    ARRAY_BUF *buf;
    uint32_t   len;

    if (IS_BAD_PTR(obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);

    /* Increment index */
    for (;;) {

        const uint32_t capacity = buf ? buf->capacity : 0;

        len = KOS_get_array_size(obj_id);

        if (len >= capacity) {
            const uint32_t new_cap = KOS_max(capacity * 2, len + 1);
            TRY(KOS_array_reserve(frame, obj_id, new_cap));

            buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->data);
            continue;
        }

        /* TODO this is not atomic wrt pop! */

        if (KOS_atomic_cas_u32(array->size, len, len+1))
            break;
    }

    /* Write new value */
    for (;;) {

        const KOS_OBJ_ID cur_value = KOS_atomic_read_ptr(buf->buf[len]);

        if (cur_value == CLOSED) {
            buf = (ARRAY_BUF *)KOS_atomic_read_ptr(buf->next);
            continue;
        }

        /* TODO What if cur_value != TOMBSTONE ??? ABA? */

        if (KOS_atomic_cas_ptr(buf->buf[len], cur_value, value))
            break;
    }

    if (idx)
        *idx = len;

_error:
    return error;
}

KOS_OBJ_ID KOS_array_pop(KOS_FRAME  frame,
                         KOS_OBJ_ID obj_id)
{
    /* TODO rewrite lock-free */

    int        error = KOS_SUCCESS;
    uint32_t   len;
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id))
        RAISE_EXCEPTION(str_err_null_ptr);
    else if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    len = KOS_get_array_size(obj_id);

    if (len == 0)
        RAISE_EXCEPTION(str_err_empty);

    ret = KOS_array_read(frame, obj_id, (int)len-1);
    TRY_OBJID(ret);

    error = KOS_array_resize(frame, obj_id, len-1);

_error:
    return error ? KOS_BADPTR : ret;
}

int KOS_array_fill(KOS_FRAME  frame,
                   KOS_OBJ_ID obj_id,
                   int64_t    begin,
                   int64_t    end,
                   KOS_OBJ_ID value)
{
    uint32_t   len;
    ARRAY_BUF *buf;

    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY) {
        KOS_raise_exception_cstring(frame, str_err_not_array);
        return KOS_ERROR_EXCEPTION;
    }

    len = KOS_get_array_size(obj_id);

    begin = _KOS_fix_index(begin, len);
    end   = _KOS_fix_index(end, len);

    buf = (ARRAY_BUF *)KOS_atomic_read_ptr(OBJPTR(ARRAY, obj_id)->data);

    while (begin < end) {

        KOS_OBJ_ID cur = (KOS_OBJ_ID)KOS_atomic_read_ptr(buf->buf[begin]);

        if (cur == TOMBSTONE)
            break;

        if (cur == CLOSED) {
            ARRAY_BUF *new_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(buf->next);
            _copy_buf(frame, OBJPTR(ARRAY, obj_id), buf, new_buf);
            buf = new_buf;
        }
        else {
            if (KOS_atomic_cas_ptr(buf->buf[begin], cur, value))
                ++begin;
        }
    }

    return KOS_SUCCESS;
}

int KOS_array_set_defaults(KOS_FRAME  frame,
                           KOS_OBJ_ID obj_id,
                           uint32_t   idx,
                           KOS_OBJ_ID src_id)
{
    int      error = KOS_SUCCESS;
    uint32_t len;
    uint32_t num_elems;

    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    if (GET_OBJ_TYPE(src_id) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    len       = KOS_get_array_size(obj_id);
    num_elems = KOS_get_array_size(src_id);

    if (len < idx)
        RAISE_EXCEPTION(str_err_too_small);

    if (idx + num_elems > len) {

        const uint32_t          already_have = len - idx;
        KOS_ATOMIC(KOS_OBJ_ID) *src          = _KOS_get_array_buffer(OBJPTR(ARRAY, src_id));
        KOS_ATOMIC(KOS_OBJ_ID) *buf;
        KOS_ATOMIC(KOS_OBJ_ID) *end;

        src       += already_have;
        num_elems -= already_have;

        TRY(KOS_array_reserve(frame, obj_id, len + num_elems));

        buf = _KOS_get_array_buffer(OBJPTR(ARRAY, obj_id)) + len;
        end = buf + num_elems;

        len += num_elems;

        while (buf < end)
            KOS_atomic_write_ptr(*(buf++), KOS_atomic_read_ptr(*(src++)));

        KOS_atomic_write_u32(OBJPTR(ARRAY, obj_id)->size, len);
    }

_error:
    return error;
}

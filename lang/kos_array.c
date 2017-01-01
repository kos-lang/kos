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
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_threads.h"
#include "kos_try.h"
#include <assert.h>

static KOS_ASCII_STRING(str_err_empty,         "array is empty");
static KOS_ASCII_STRING(str_err_invalid_index, "array index is out of range");
static KOS_ASCII_STRING(str_err_not_array,     "object is not an array");
static KOS_ASCII_STRING(str_err_null_ptr,      "null pointer");

#define KOS_buffer_alloc_size(cap) (sizeof(ARRAY_BUF) + ((cap) - 1) * sizeof(KOS_OBJ_PTR))

typedef struct _KOS_ARRAY_BUFFER ARRAY_BUF;

static ARRAY_BUF *_alloc_buffer(KOS_STACK_FRAME *frame, unsigned capacity)
{
    return (ARRAY_BUF *)_KOS_alloc_buffer(frame, KOS_buffer_alloc_size(capacity));
}

int _KOS_init_array(KOS_STACK_FRAME *frame, KOS_ARRAY *array, unsigned capacity)
{
    int error = KOS_SUCCESS;

    capacity = KOS_max(capacity, KOS_MIN_ARRAY_CAPACITY);

    array->type     = OBJ_ARRAY;
    array->buffer   = _alloc_buffer(frame, capacity);
    array->capacity = capacity;
    array->length   = 0;

    if (array->buffer) {
        KOS_ATOMIC(KOS_OBJ_PTR) *ptr = &((ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer))->buf[0];
        KOS_ATOMIC(KOS_OBJ_PTR) *end = ptr + (size_t)array->capacity;

        for ( ; ptr != end; ++ptr)
            KOS_atomic_write_ptr(*ptr, KOS_VOID);
    }
    else
        error = KOS_ERROR_EXCEPTION;

    return error;
}

KOS_OBJ_PTR KOS_new_array(KOS_STACK_FRAME *frame, unsigned length)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(frame, KOS_ARRAY);

    if (obj && KOS_SUCCESS == _KOS_init_array(frame, &obj->array, length))
        obj->array.length = length;
    else
        obj = 0;

    return TO_OBJPTR(obj);
}

KOS_OBJ_PTR KOS_array_read(KOS_STACK_FRAME *frame, KOS_OBJ_PTR objptr, int idx)
{
    /* TODO rewrite lock-free */

    KOS_OBJ_PTR elem = TO_OBJPTR(0);

    if (IS_BAD_PTR(objptr))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
    else {
        KOS_ARRAY *const array  = OBJPTR(KOS_ARRAY, objptr);
        const uint32_t   length = array->length;
        const unsigned   bufidx = (idx < 0) ? ((unsigned)idx + length) : (unsigned)idx;

        if (bufidx < length) {
            ARRAY_BUF *const buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer);
            elem                 = (KOS_OBJ_PTR)KOS_atomic_read_ptr(buf->buf[bufidx]);
        }
        else
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_index));
    }

    return elem;
}

int KOS_array_write(KOS_STACK_FRAME *frame, KOS_OBJ_PTR objptr, int idx, KOS_OBJ_PTR value)
{
    /* TODO rewrite lock-free */

    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(objptr))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
    else {
        KOS_ARRAY *const array  = OBJPTR(KOS_ARRAY, objptr);
        const uint32_t   length = array->length;
        const unsigned   bufidx = (idx < 0) ? ((unsigned)idx + length) : (unsigned)idx;

        if (bufidx < length) {
            ARRAY_BUF *const buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer);
            KOS_atomic_write_ptr(buf->buf[bufidx], value);
            error = KOS_SUCCESS;
        }
        else
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_index));
    }

    return error;
}

int KOS_array_reserve(KOS_STACK_FRAME *frame, KOS_OBJ_PTR objptr, uint32_t new_capacity)
{
    /* TODO rewrite lock-free */

    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(objptr))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
    else {
        KOS_ARRAY *const array    = OBJPTR(KOS_ARRAY, objptr);
        const uint32_t   capacity = KOS_atomic_read_u32(array->capacity);

        if (new_capacity > capacity) {
            const uint32_t           length  = KOS_atomic_read_u32(array->length);
            ARRAY_BUF *const         buf     = _alloc_buffer(frame, new_capacity);
            ARRAY_BUF *const         old_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer);
            KOS_ATOMIC(KOS_OBJ_PTR) *ptr     = &buf->buf[length];
            KOS_ATOMIC(KOS_OBJ_PTR) *end     = &buf->buf[0] + new_capacity;
            KOS_ATOMIC(KOS_OBJ_PTR) *src;

            for ( ; ptr != end; ++ptr)
                KOS_atomic_write_ptr(*ptr, KOS_VOID);

            src = &old_buf->buf[0];
            end = src + length;
            ptr = &buf->buf[0];

            for ( ; src != end; ++src, ++ptr) {
                const KOS_OBJ_PTR value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(*src);
                KOS_atomic_write_ptr(*ptr, value);
            }

            KOS_atomic_write_ptr(array->buffer,   (void *)buf);
            KOS_atomic_write_u32(array->capacity, new_capacity);

            _KOS_free_buffer(frame, old_buf, KOS_buffer_alloc_size(capacity));
        }

        error = KOS_SUCCESS;
    }

    return error;
}

int KOS_array_resize(KOS_STACK_FRAME *frame, KOS_OBJ_PTR objptr, uint32_t length)
{
    /* TODO rewrite lock-free */

    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(objptr))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
    else {
        KOS_ARRAY *const array    = OBJPTR(KOS_ARRAY, objptr);
        const uint32_t   capacity = KOS_atomic_read_u32(array->capacity);

        assert(capacity > 0);

        if (length > capacity) {
            const uint32_t max_step = KOS_ARRAY_CAPACITY_STEP / sizeof(KOS_OBJ_PTR);
            const uint32_t cap_x2   = KOS_min(capacity * 2, KOS_align_up(capacity + max_step - 1, max_step));
            const uint32_t new_cap  = KOS_max(cap_x2, length);
            TRY(KOS_array_reserve(frame, objptr, new_cap));
        }
        else {
            ARRAY_BUF *const buf = (ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer);
            uint32_t i           = KOS_atomic_read_u32(array->length);
            for ( ; i < length; i++)
                KOS_atomic_write_ptr(buf->buf[i], KOS_VOID);
        }

        KOS_atomic_write_u32(array->length, length);

        error = KOS_SUCCESS;
    }

_error:
    return error;
}

KOS_OBJ_PTR KOS_array_slice(KOS_STACK_FRAME *frame,
                            KOS_OBJ_PTR      objptr,
                            int64_t          begin,
                            int64_t          end)
{
    /* TODO rewrite lock-free */

    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    if (IS_BAD_PTR(objptr))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_array));
    else {
        KOS_ARRAY *const array = OBJPTR(KOS_ARRAY, objptr);
        const uint32_t   len   = KOS_get_array_size(objptr);

        if (len) {

            uint32_t new_len;
            int64_t  new_len_64;

            begin = _KOS_fix_index(begin, len);
            end   = _KOS_fix_index(end, len);

            if (end < begin)
                end = begin;

            new_len_64 = end - begin;
            assert(new_len_64 <= 0xFFFFFFFF);
            new_len = (uint32_t)new_len_64;

            ret = KOS_new_array(frame, new_len);

            if (new_len && ! IS_BAD_PTR(ret)) {
                KOS_ARRAY *const new_array = OBJPTR(KOS_ARRAY, ret);
                ARRAY_BUF *const src_buf   = (ARRAY_BUF *)KOS_atomic_read_ptr(array->buffer);
                ARRAY_BUF *const dest_buf  = (ARRAY_BUF *)KOS_atomic_read_ptr(new_array->buffer);

                KOS_ATOMIC(KOS_OBJ_PTR) *src       = &src_buf->buf[begin];
                KOS_ATOMIC(KOS_OBJ_PTR) *dest      = &dest_buf->buf[0];
                uint32_t                 remaining = new_len;

                while (remaining--) {
                    const KOS_OBJ_PTR value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(*(src++));
                    KOS_atomic_write_ptr(*(dest++), value);
                }

                KOS_atomic_write_u32(new_array->length, new_len);
            }
        }
        else
            ret = KOS_new_array(frame, 0);
    }

    return ret;
}

int KOS_array_insert(KOS_STACK_FRAME *frame,
                     KOS_OBJ_PTR      dest_objptr,
                     int64_t          dest_begin,
                     int64_t          dest_end,
                     KOS_OBJ_PTR      src_objptr,
                     int64_t          src_begin,
                     int64_t          src_end)
{
    /* TODO rewrite lock-free */

    int        error   = KOS_SUCCESS;
    uint32_t   dest_len;
    uint32_t   src_len = 0;
    ARRAY_BUF *dest_buf;
    ARRAY_BUF *src_buf = 0;
    uint32_t   dest_delta;
    uint32_t   src_delta;

    if (IS_BAD_PTR(dest_objptr))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(dest_objptr) || GET_OBJ_TYPE(dest_objptr) != OBJ_ARRAY)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));
    else if (src_begin != src_end && IS_BAD_PTR(src_objptr))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_null_ptr));
    else if (src_begin != src_end && (IS_SMALL_INT(src_objptr) || GET_OBJ_TYPE(src_objptr) != OBJ_ARRAY))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));

    dest_len = KOS_get_array_size(dest_objptr);

    dest_begin = _KOS_fix_index(dest_begin, dest_len);
    dest_end   = _KOS_fix_index(dest_end, dest_len);

    if (dest_end < dest_begin)
        dest_end = dest_begin;

    dest_delta = (uint32_t)(dest_end - dest_begin);

    if (src_begin != src_end) {
        src_len   = KOS_get_array_size(src_objptr);
        src_begin = _KOS_fix_index(src_begin, src_len);
        src_end   = _KOS_fix_index(src_end, src_len);

        if (src_end < src_begin)
            src_end = src_begin;
    }

    src_delta = (uint32_t)(src_end - src_begin);

    if (src_delta > dest_delta)
        TRY(KOS_array_resize(frame, dest_objptr, dest_len - dest_delta + src_delta));

    dest_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(OBJPTR(KOS_ARRAY, dest_objptr)->buffer);
    if (src_begin != src_end)
        src_buf = (ARRAY_BUF *)KOS_atomic_read_ptr(OBJPTR(KOS_ARRAY, src_objptr)->buffer);

    if (src_objptr != dest_objptr || src_end <= dest_begin || src_begin >= dest_end || ! src_delta) {

        if (src_delta != dest_delta && dest_end < dest_len)
            _KOS_atomic_move_ptr((KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end - dest_delta + src_delta],
                                 (KOS_ATOMIC(void *) *)&dest_buf->buf[dest_end],
                                 (unsigned)(dest_len - dest_end));

        if (src_objptr == dest_objptr && src_begin >= dest_end)
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
        TRY(KOS_array_resize(frame, dest_objptr, dest_len - dest_delta + src_delta));

_error:
    return error;
}

int KOS_array_rotate(KOS_STACK_FRAME *frame,
                     KOS_OBJ_PTR      objptr,
                     int64_t          begin,
                     int64_t          mid,
                     int64_t          end)
{
    /* TODO */
    assert(0);
    return KOS_ERROR_INTERNAL;
}

int KOS_array_push(KOS_STACK_FRAME *frame,
                   KOS_OBJ_PTR      objptr,
                   KOS_OBJ_PTR      value)
{
    /* TODO rewrite lock-free */

    int      error = KOS_SUCCESS;
    uint32_t len;

    if (IS_BAD_PTR(objptr))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));

    len = KOS_get_array_size(objptr);

    TRY(KOS_array_resize(frame, objptr, len + 1));

    TRY(KOS_array_write(frame, objptr, (int)len, value));

_error:
    return error;
}

KOS_OBJ_PTR KOS_array_pop(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      objptr)
{
    /* TODO rewrite lock-free */

    int         error = KOS_SUCCESS;
    uint32_t    len;
    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    if (IS_BAD_PTR(objptr))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) != OBJ_ARRAY)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_array));

    len = KOS_get_array_size(objptr);

    if (len == 0)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_empty));

    ret = KOS_array_read(frame, objptr, (int)len-1);
    TRY_OBJPTR(ret);

    error = KOS_array_resize(frame, objptr, len-1);

_error:
    return error ? TO_OBJPTR(0) : ret;
}

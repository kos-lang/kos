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

#ifndef __KOS_BUFFER_H
#define __KOS_BUFFER_H

#include "kos_object_base.h"
#include "../lang/kos_threads.h"
#include <assert.h>

struct _KOS_BUFFER_DATA {
    uint32_t capacity;
    uint32_t _align;
    uint8_t  buf[1];
};

#ifdef __cplusplus

static inline uint32_t KOS_get_buffer_size(KOS_OBJ_PTR objptr)
{
    assert( ! IS_SMALL_INT(objptr) && ! IS_BAD_PTR(objptr));
    KOS_BUFFER *const buffer = OBJPTR(KOS_BUFFER, objptr);
    assert(buffer->type == OBJ_BUFFER);
    return KOS_atomic_read_u32(buffer->size);
}

static inline uint8_t *KOS_buffer_data(KOS_OBJ_PTR objptr)
{
    struct _KOS_BUFFER_DATA *data = 0;
    assert( ! IS_SMALL_INT(objptr) && ! IS_BAD_PTR(objptr));
    KOS_BUFFER *const buffer = OBJPTR(KOS_BUFFER, objptr);
    assert(buffer->type == OBJ_BUFFER);
    data = (struct _KOS_BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);
    return &data->buf[0];
}

#else

#define KOS_get_buffer_size(objptr) (KOS_atomic_read_u32(OBJPTR(KOS_BUFFER, (objptr))->size))
#define KOS_buffer_data(objptr) (&((struct _KOS_BUFFER_DATA *)KOS_atomic_read_ptr(OBJPTR(KOS_BUFFER, (objptr))->data))->buf[0])

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_PTR KOS_new_buffer(KOS_STACK_FRAME *frame,
                           unsigned         size);

int KOS_buffer_reserve(KOS_STACK_FRAME *frame,
                       KOS_OBJ_PTR      objptr,
                       unsigned         capacity);

int KOS_buffer_resize(KOS_STACK_FRAME *frame,
                      KOS_OBJ_PTR      objptr,
                      unsigned         size);

uint8_t *KOS_buffer_make_room(KOS_STACK_FRAME *frame,
                              KOS_OBJ_PTR      objptr,
                              unsigned         size_delta);

int KOS_buffer_fill(KOS_STACK_FRAME *frame,
                    KOS_OBJ_PTR      objptr,
                    int64_t          begin,
                    int64_t          end,
                    uint8_t          value);

int KOS_buffer_copy(KOS_STACK_FRAME *frame,
                    KOS_OBJ_PTR      destptr,
                    int64_t          dest_begin,
                    KOS_OBJ_PTR      srcptr,
                    int64_t          src_begin,
                    int64_t          src_end);

KOS_OBJ_PTR KOS_buffer_slice(KOS_STACK_FRAME *frame,
                             KOS_OBJ_PTR      objptr,
                             int64_t          begin,
                             int64_t          end);

int KOS_buffer_rotate(KOS_STACK_FRAME *frame,
                      KOS_OBJ_PTR      objptr,
                      int64_t          begin,
                      int64_t          mid,
                      int64_t          end);

#ifdef __cplusplus
}
#endif

#endif

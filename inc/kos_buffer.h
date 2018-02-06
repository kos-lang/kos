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

#ifndef __KOS_BUFFER_H
#define __KOS_BUFFER_H

#include "kos_object_base.h"
#include "kos_threads.h"
#include <assert.h>

typedef struct _KOS_BUFFER_STORAGE {
    KOS_OBJ_HEADER header;
    uint32_t       capacity;
    uint8_t        buf[1];
} KOS_BUFFER_STORAGE;

#ifdef __cplusplus

static inline uint32_t KOS_get_buffer_size(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    KOS_BUFFER *const buffer = OBJPTR(BUFFER, obj_id);
    return KOS_atomic_read_u32(buffer->size);
}

static inline uint8_t *KOS_buffer_data(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    KOS_BUFFER         *const buffer  = OBJPTR(BUFFER, obj_id);
    const KOS_OBJ_ID          buf_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(buffer->data);
    KOS_BUFFER_STORAGE       *data;
    assert( ! IS_BAD_PTR(buf_obj));
    data = OBJPTR(BUFFER_STORAGE, buf_obj);
    return &data->buf[0];
}

#else

#define KOS_get_buffer_size(obj_id) (KOS_atomic_read_u32(OBJPTR(BUFFER, (obj_id))->size))
#define KOS_buffer_data(obj_id) (&OBJPTR(BUFFER_STORAGE, (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(BUFFER, (obj_id))->data))->buf[0])

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_buffer(KOS_FRAME frame,
                          unsigned  size);

int KOS_buffer_reserve(KOS_FRAME  frame,
                       KOS_OBJ_ID obj_id,
                       unsigned   capacity);

int KOS_buffer_resize(KOS_FRAME  frame,
                      KOS_OBJ_ID obj_id,
                      unsigned   size);

uint8_t *KOS_buffer_make_room(KOS_FRAME  frame,
                              KOS_OBJ_ID obj_id,
                              unsigned   size_delta);

int KOS_buffer_fill(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int64_t    begin,
                    int64_t    end,
                    uint8_t    value);

int KOS_buffer_copy(KOS_FRAME  frame,
                    KOS_OBJ_ID destptr,
                    int64_t    dest_begin,
                    KOS_OBJ_ID srcptr,
                    int64_t    src_begin,
                    int64_t    src_end);

KOS_OBJ_ID KOS_buffer_slice(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id,
                            int64_t    begin,
                            int64_t    end);

int KOS_buffer_rotate(KOS_FRAME  frame,
                      KOS_OBJ_ID obj_id,
                      int64_t    begin,
                      int64_t    mid,
                      int64_t    end);

#ifdef __cplusplus
}
#endif

#endif

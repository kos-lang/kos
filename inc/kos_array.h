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

#ifndef __KOS_ARRAY_H
#define __KOS_ARRAY_H

#include "kos_object_base.h"
#include "kos_threads.h"
#include <assert.h>

#ifdef __cplusplus

static inline uint32_t KOS_get_array_size(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
    KOS_ARRAY *const array = OBJPTR(ARRAY, obj_id);
    return KOS_atomic_read_u32(array->size);
}

#else

#define KOS_get_array_size(obj_id) (KOS_atomic_read_u32(OBJPTR(ARRAY, (obj_id))->size))

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_array(KOS_FRAME frame,
                         uint32_t  size);

KOS_OBJ_ID KOS_array_read(KOS_FRAME  frame,
                          KOS_OBJ_ID obj_id,
                          int        idx);

int KOS_array_write(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int        idx,
                    KOS_OBJ_ID value);

int KOS_array_reserve(KOS_FRAME  frame,
                      KOS_OBJ_ID obj_id,
                      uint32_t   capacity);

int KOS_array_resize(KOS_FRAME  frame,
                     KOS_OBJ_ID obj_id,
                     uint32_t   size);

KOS_OBJ_ID KOS_array_slice(KOS_FRAME  frame,
                           KOS_OBJ_ID obj_id,
                           int64_t    begin,
                           int64_t    end);

int KOS_array_insert(KOS_FRAME  frame,
                     KOS_OBJ_ID dest_obj_id,
                     int64_t    dest_begin,
                     int64_t    dest_end,
                     KOS_OBJ_ID src_obj_id,
                     int64_t    src_begin,
                     int64_t    src_end);

int KOS_array_push(KOS_FRAME  frame,
                   KOS_OBJ_ID obj_id,
                   KOS_OBJ_ID value,
                   uint32_t  *idx);

KOS_OBJ_ID KOS_array_pop(KOS_FRAME  frame,
                         KOS_OBJ_ID obj_id);

int KOS_array_fill(KOS_FRAME  frame,
                   KOS_OBJ_ID obj_id,
                   int64_t    begin,
                   int64_t    end,
                   KOS_OBJ_ID value);

#ifdef __cplusplus
}
#endif

#endif

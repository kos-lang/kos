/*
 * Copyright (c) 2014-2016 Chris Dragan
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
#include "../lang/kos_threads.h"
#include <assert.h>

#ifdef __cplusplus

static inline uint32_t KOS_get_array_size(KOS_OBJ_PTR objptr)
{
    assert( ! IS_SMALL_INT(objptr) && ! IS_BAD_PTR(objptr));
    KOS_ARRAY *const array = OBJPTR(KOS_ARRAY, objptr);
    assert(array->type == OBJ_ARRAY);
    return KOS_atomic_read_u32(array->length);
}

#else

#define KOS_get_array_size(objptr) (KOS_atomic_read_u32(OBJPTR(KOS_ARRAY, (objptr))->length))

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_PTR KOS_new_array(KOS_CONTEXT *ctx,
                          unsigned     length);

KOS_OBJ_PTR KOS_array_read(KOS_CONTEXT *ctx,
                           KOS_OBJ_PTR  objptr,
                           int          idx);

int KOS_array_write(KOS_CONTEXT *ctx,
                    KOS_OBJ_PTR  objptr,
                    int          idx,
                    KOS_OBJ_PTR  value);

int KOS_array_reserve(KOS_CONTEXT *ctx,
                      KOS_OBJ_PTR  objptr,
                      uint32_t     capacity);

int KOS_array_resize(KOS_CONTEXT *ctx,
                     KOS_OBJ_PTR  objptr,
                     uint32_t     length);

KOS_OBJ_PTR KOS_array_slice(KOS_CONTEXT *ctx,
                            KOS_OBJ_PTR  objptr,
                            int64_t      begin,
                            int64_t      end);

int KOS_array_insert(KOS_CONTEXT *ctx,
                     KOS_OBJ_PTR  dest_objptr,
                     int64_t      dest_begin,
                     int64_t      dest_end,
                     KOS_OBJ_PTR  src_objptr,
                     int64_t      src_begin,
                     int64_t      src_end);

int KOS_array_rotate(KOS_CONTEXT *ctx,
                     KOS_OBJ_PTR  objptr,
                     int64_t      begin,
                     int64_t      mid,
                     int64_t      end);

int KOS_array_push(KOS_CONTEXT *ctx,
                   KOS_OBJ_PTR  objptr,
                   KOS_OBJ_PTR  value);

KOS_OBJ_PTR KOS_array_pop(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  objptr);

#ifdef __cplusplus
}
#endif

#endif

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

#ifndef __KOS_OBJECT_ALLOC_H
#define __KOS_OBJECT_ALLOC_H

#include "../inc/kos_object_base.h"
#include <stddef.h>

enum _KOS_AREA_TYPE {
    KOS_AREA_FREE,
    KOS_AREA_RECLAIMABLE,
    KOS_AREA_FIXED,
    KOS_AREA_STACK
};

enum _KOS_AREA_ELEM_SIZE {
    KOS_AREA_NONE = 0,
    KOS_AREA_8    = 3,
    KOS_AREA_16   = 4,
    KOS_AREA_32   = 5,
    KOS_AREA_64   = 6,
    KOS_AREA_128  = 7
};

/* TODO allocate array, buffer in 32-bit builds from KOS_AREA_8, but limit
 * them to be allocated in even-index cells only. */

#define _KOS_POT_FROM_TYPE(type) ((OBJ_ ## type & 1)   == 0   ? KOS_AREA_8   : \
                                  sizeof(KOS_ ## type) <= 16  ? KOS_AREA_16  : \
                                  sizeof(KOS_ ## type) <= 32  ? KOS_AREA_32  : \
                                  sizeof(KOS_ ## type) <= 64  ? KOS_AREA_64  : \
                                  sizeof(KOS_ ## type) <= 128 ? KOS_AREA_128 : KOS_AREA_NONE)

#define _KOS_alloc_object(frame, type) (_KOS_alloc_object_internal(frame, _KOS_POT_FROM_TYPE(type), (int)sizeof(KOS_ ## type)))

int   _KOS_alloc_init(KOS_CONTEXT *ctx);

void  _KOS_alloc_destroy(KOS_CONTEXT *ctx);

void  _KOS_alloc_set_mode(KOS_FRAME           frame,
                          enum _KOS_AREA_TYPE alloc_mode);

enum _KOS_AREA_TYPE _KOS_alloc_get_mode(KOS_FRAME frame);

void *_KOS_alloc_object_internal(KOS_FRAME                frame,
                                 enum _KOS_AREA_ELEM_SIZE elem_size_pot,
                                 int                      elem_size);

void *_KOS_alloc_buffer(KOS_FRAME frame, size_t size);

void  _KOS_free_buffer(KOS_FRAME frame, void *ptr, size_t size);

#endif

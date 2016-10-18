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

#ifndef __KOS_OBJECT_ALLOC_H
#define __KOS_OBJECT_ALLOC_H

#include "../inc/kos_object_base.h"
#include <stddef.h>

#define _KOS_alloc_object(frame, type) (sizeof(type) <= 16  ? _KOS_alloc_16(frame)  : \
                                        sizeof(type) <= 32  ? _KOS_alloc_32(frame)  : \
                                        sizeof(type) <= 64  ? _KOS_alloc_64(frame)  : \
                                        sizeof(type) <= 128 ? _KOS_alloc_128(frame) : 0)

int             _KOS_alloc_init(KOS_CONTEXT *ctx);
void            _KOS_alloc_destroy(KOS_CONTEXT *ctx);
KOS_ANY_OBJECT *_KOS_alloc_16(KOS_STACK_FRAME *frame);
KOS_ANY_OBJECT *_KOS_alloc_32(KOS_STACK_FRAME *frame);
KOS_ANY_OBJECT *_KOS_alloc_64(KOS_STACK_FRAME *frame);
KOS_ANY_OBJECT *_KOS_alloc_128(KOS_STACK_FRAME *frame);
void           *_KOS_alloc_buffer(KOS_STACK_FRAME *frame, size_t size);
void            _KOS_free_buffer(KOS_STACK_FRAME *frame, void *ptr, size_t size);

#endif

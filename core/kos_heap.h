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

#ifndef __KOS_OBJECT_ALLOC_H
#define __KOS_OBJECT_ALLOC_H

#include "../inc/kos_object_base.h"
#include <stddef.h>

int   _KOS_heap_init(KOS_INSTANCE *inst);

void  _KOS_heap_destroy(KOS_INSTANCE *inst);

void *_KOS_alloc_object(KOS_CONTEXT          ctx,
                        enum KOS_OBJECT_TYPE object_type,
                        uint32_t             size);

void *_KOS_alloc_object_page(KOS_CONTEXT          ctx,
                             enum KOS_OBJECT_TYPE object_type);

void *_KOS_heap_early_alloc(KOS_INSTANCE         *inst,
                            KOS_CONTEXT           ctx,
                            enum KOS_OBJECT_TYPE  object_type,
                            uint32_t              size);

void _KOS_heap_release_thread_page(KOS_CONTEXT ctx);

#endif

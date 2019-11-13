/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#ifndef KOS_OBJECT_ALLOC_H_INCLUDED
#define KOS_OBJECT_ALLOC_H_INCLUDED

#include "../inc/kos_object_base.h"
#include <stddef.h>

typedef enum KOS_ALLOC_FLAG_E {
    KOS_ALLOC_MOVABLE,
    KOS_ALLOC_IMMOVABLE
} KOS_ALLOC_FLAG;

int   kos_heap_init(KOS_INSTANCE *inst);

void  kos_heap_destroy(KOS_INSTANCE *inst);

void *kos_alloc_object(KOS_CONTEXT    ctx,
                       KOS_ALLOC_FLAG flags,
                       KOS_TYPE       object_type,
                       uint32_t       size);

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type);

void kos_heap_release_thread_page(KOS_CONTEXT ctx);

void kos_print_heap(KOS_CONTEXT ctx);

#ifdef CONFIG_MAD_GC
int kos_trigger_mad_gc(KOS_CONTEXT ctx);
#else
#define kos_trigger_mad_gc(ctx) 0
#endif

#if !defined(NDEBUG) || defined(CONFIG_MAD_GC)
int kos_gc_active(KOS_CONTEXT ctx);
#endif

#endif

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

#include "kos_object_alloc.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_threads.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include <stdio.h>

static KOS_ASCII_STRING(str_err_out_of_memory, "out of memory");

#if CONFIG_ALLOCATOR == 0xDEB

#ifdef CONFIG_ALLOC_STATS
static KOS_ATOMIC(int32_t) num_16  = 0;
static KOS_ATOMIC(int32_t) num_32  = 0;
static KOS_ATOMIC(int32_t) num_64  = 0;
static KOS_ATOMIC(int32_t) num_128 = 0;
static KOS_ATOMIC(int32_t) num_buf = 0;
static KOS_ATOMIC(int32_t) total   = 0;
#endif

int _KOS_alloc_init(KOS_CONTEXT *ctx)
{
    return KOS_SUCCESS;
}

void _KOS_alloc_destroy(KOS_CONTEXT *ctx)
{
    void *objects = ctx->allocator.objects;

    while (objects) {
        void *del = objects;
        objects   = *(void **)objects;
        _KOS_free(del);
    }

#ifdef CONFIG_ALLOC_STATS
    printf(" 16B - %u\n", KOS_atomic_read_u32(num_16));
    printf(" 32B - %u\n", KOS_atomic_read_u32(num_32));
    printf(" 64B - %u\n", KOS_atomic_read_u32(num_64));
    printf("128B - %u\n", KOS_atomic_read_u32(num_128));
    printf("buf  - %uB\n", KOS_atomic_read_u32(num_buf) -
                           KOS_atomic_read_u32(num_16) -
                           KOS_atomic_read_u32(num_32) -
                           KOS_atomic_read_u32(num_64) -
                           KOS_atomic_read_u32(num_128));
    printf("mem  - %uB\n", KOS_atomic_read_u32(total));
#endif
}

KOS_ANY_OBJECT *_KOS_alloc_16(KOS_STACK_FRAME *frame)
{
#ifdef CONFIG_ALLOC_STATS
    KOS_atomic_add_i32(num_16, 1);
#endif
    return (KOS_ANY_OBJECT *)_KOS_alloc_buffer(frame, 16);
}

KOS_ANY_OBJECT *_KOS_alloc_32(KOS_STACK_FRAME *frame)
{
#ifdef CONFIG_ALLOC_STATS
    KOS_atomic_add_i32(num_32, 1);
#endif
    return (KOS_ANY_OBJECT *)_KOS_alloc_buffer(frame, 32);
}

KOS_ANY_OBJECT *_KOS_alloc_64(KOS_STACK_FRAME *frame)
{
#ifdef CONFIG_ALLOC_STATS
    KOS_atomic_add_i32(num_64, 1);
#endif
    return (KOS_ANY_OBJECT *)_KOS_alloc_buffer(frame, 64);
}

KOS_ANY_OBJECT *_KOS_alloc_128(KOS_STACK_FRAME *frame)
{
#ifdef CONFIG_ALLOC_STATS
    KOS_atomic_add_i32(num_128, 1);
#endif
    return (KOS_ANY_OBJECT *)_KOS_alloc_buffer(frame, 128);
}

void *_KOS_alloc_buffer(KOS_STACK_FRAME *frame, size_t size)
{
    uint64_t *obj = (uint64_t *)_KOS_malloc(size+sizeof(uint64_t));

    if (obj) {
        struct _KOS_ALLOC_DEBUG *allocator = frame->allocator;

#ifdef CONFIG_ALLOC_STATS
        KOS_atomic_add_i32(num_buf, 1);
        KOS_atomic_add_i32(total, size);
#endif

        void **ptr = (void **)obj;

        for (;;) {
            void *next = allocator->objects;
            *ptr       = next;
            if (KOS_atomic_cas_ptr(allocator->objects, next, (void *)obj))
                break;
        }

        ++obj;
    }
    else
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));

    return (KOS_ANY_OBJECT *)obj;
}

void _KOS_free_buffer(KOS_STACK_FRAME *frame, void *ptr, size_t size)
{
}

#endif

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
#include "kos_perf.h"
#include "kos_threads.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include <stdio.h>

static const char str_err_out_of_memory[] = "out of memory";

#if 1

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
}

void *_KOS_alloc_16(KOS_FRAME frame)
{
    KOS_PERF_CNT(alloc_object_16);
    return _KOS_alloc_buffer(frame, 16);
}

void *_KOS_alloc_32(KOS_FRAME frame)
{
    KOS_PERF_CNT(alloc_object_32);
    return _KOS_alloc_buffer(frame, 32);
}

void *_KOS_alloc_64(KOS_FRAME frame)
{
    KOS_PERF_CNT(alloc_object_64);
    return _KOS_alloc_buffer(frame, 64);
}

void *_KOS_alloc_128(KOS_FRAME frame)
{
    KOS_PERF_CNT(alloc_object_64);
    return _KOS_alloc_buffer(frame, 128);
}

void *_KOS_alloc_buffer(KOS_FRAME frame, size_t size)
{
    uint8_t *obj = (uint8_t *)_KOS_malloc(size + sizeof(uint64_t) + 0x10);

    if (obj) {
        struct _KOS_ALLOCATOR *allocator = frame->allocator;

        void **ptr = (void **)obj;

        KOS_PERF_CNT(alloc_buffer);
        KOS_PERF_ADD(alloc_buffer_total, (uint32_t)size);

        for (;;) {
            void *next = allocator->objects;
            *ptr       = next;
            if (KOS_atomic_cas_ptr(allocator->objects, next, (void *)obj))
                break;
        }

        obj += sizeof(void *);
        obj += 0x10 - ((int)(uintptr_t)obj & 0xF);
    }
    else
        KOS_raise_exception_cstring(frame, str_err_out_of_memory);

    return (void *)obj;
}

void _KOS_free_buffer(KOS_FRAME frame, void *ptr, size_t size)
{
}

#else

enum _KOS_AREA_ELEM_SIZE {
    KOS_AREA_8  = 3,
    KOS_AREA_16 = 4,
    KOS_AREA_32 = 5,
    KOS_AREA_64 = 6
};

struct _KOS_AREA {
    KOS_ATOMIC(void *)  *next;
    uint8_t              type;
    uint8_t              elem_size_pot;
    KOS_ATOMIC(uint32_t) free_lookup_offs;
    KOS_ATOMIC(uint32_t) bitmap[1];
};

struct _KOS_FIXED_AREA {
    KOS_ATOMIC(void *)  *next;
    uint8_t              type;
    KOS_ATOMIC(uint32_t) first_free_offs;
};

struct _KOS_FREE_AREA {
    KOS_ATOMIC(void *)  *next;
    uint8_t              type;
};

#define AVAIL_AREA_SIZE (_KOS_AREA_SIZE - sizeof(struct _KOS_AREA) + sizeof(KOS_ATOMIC(uint32_t)))

#define NUM_CHUNKS(size_pot) (AVAIL_AREA_SIZE * 64 / ((1 << ((size_pot)+6)) + 8))

#define BITMAP_BYTES(num_chunks) ((((num_chunks) + 31) >> 5) << 2)

#define AREA_FROM_OBJ_ID(obj_id) ((struct _KOS_AREA *)((intptr_t)(obj_id) & ~(intptr_t)(_KOS_AREA_SIZE - 1)))

static int _alloc_areas(struct _KOS_ALLOCATOR *allocator)
{
    uint8_t *pool = (uint8_t *)_KOS_malloc(_KOS_AREA_SIZE * _KOS_AREAS_POOL_SIZE);

    if (pool) {

        /* TODO register buffer */

        uint8_t *cur = pool + _KOS_AREA_SIZE * _KOS_AREAS_POOL_SIZE;

        struct _KOS_FREE_AREA *prev   = 0;
        KOS_ATOMIC(void *)    *hookup = &allocator->areas_free;

        /* Align to pool size */
        cur  = (uint8_t *)((intptr_t)cur & ~(intptr_t)(_KOS_AREA_SIZE - 1));
        pool = (uint8_t *)(((intptr_t)cur + _KOS_AREA_SIZE - 1) & ~(intptr_t)(_KOS_AREA_SIZE - 1));

        while (cur > pool) {

            struct _KOS_FREE_AREA *const area = (struct _KOS_FREE_AREA *)(cur - _KOS_AREA_SIZE);

            _KOS_atomic_write_ptr(area->next, prev);
            area->type = (uint8_t)KOS_AREA_FREE;
            prev       = area;

            cur = (uint8_t *)area;
        }

        _KOS_spin_lock(&allocator->lock);

        while (KOS_atomic_read_ptr(*hookup))
            hookup = &((struct _KOS_FREE_AREA *)KOS_atomic_read_ptr(*hookup))->next;

        KOS_atomic_write_ptr(*hookup, prev);

        _KOS_spin_unlock(&allocator->lock);
    }
    else
        return KOS_ERROR_OUT_OF_MEMORY;
}

static int _alloc_area(struct _KOS_ALLOCATOR   *allocator,
                       enum _KOS_AREA_TYPE      type,
                       enum _KOS_AREA_ELEM_SIZE elem_size_pot)
{
    void *area;

    assert(type != KOS_AREA_FREE);

    do {

        _KOS_spin_lock(&allocator->lock);

        area = KOS_atomic_read_ptr(allocator->areas_free);
        if (area)
            KOS_atomic_write_ptr(allocator->areas_free,
                                 ((struct _KOS_FREE_AREA *)area)->next);

        _KOS_spin_unlock(&allocator->lock);

        if ( ! area) {
            const int error = _alloc_areas(allocator);
            if (error)
                return error;
        }
    } while ( ! area);

    assert(((struct _KOS_FREE_AREA *)area)->type == KOS_AREA_FREE);

    if (type == KOS_AREA_FIXED) {

        struct _KOS_FIXED_AREA *new_area = (struct _KOS_FIXED_AREA *)area;

        memset(new_area, 0, _KOS_AREA_SIZE);

        new_area->type = (uint8_t)KOS_AREA_FIXED;

        _KOS_spin_lock(&allocator->lock);

        _KOS_atomic_write_ptr(new_area->next, _KOS_atomic_read_ptr(allocator->areas_fixed));
        _KOS_atomic_write_ptr(allocator->areas_fixed, new_area);

        _KOS_spin_unlock(&allocator->lock);
    }
    /* TODO add KOS_AREA_STACK */
    else {

        struct _KOS_AREA   *new_area = (struct _KOS_AREA *)area;
        KOS_ATOMIC(void *) *areas    = &allocator->areas[(int)elem_size_pot - 3];

        assert(type == KOS_AREA_RECLAIMABLE);

        memset(new_area, 0, _KOS_AREA_SIZE);

        new_area->type          = (uint8_t)KOS_AREA_RECLAIMABLE;
        new_area->elem_size_pot = (uint8_t)elem_size_pot;

        _KOS_spin_lock(&allocator->lock);

        _KOS_atomic_write_ptr(new_area->next, _KOS_atomic_read_ptr(*areas));
        _KOS_atomic_write_ptr(*areas, new_area);

        _KOS_spin_unlock(&allocator->lock);
    }

    return KOS_SUCCESS;
}

int _KOS_alloc_init(KOS_CONTEXT *ctx)
{
    struct _KOS_ALLOCATOR *allocator = &ctx->allocator;

    memset(allocator, 0, sizeof(*allocator));

    return _alloc_areas(allocator);
}

void _KOS_alloc_destroy(KOS_CONTEXT *ctx)
{
    /* TODO free registered buffers */
}

#endif

void _KOS_alloc_set_mode(KOS_FRAME frame, enum _KOS_AREA_TYPE alloc_mode)
{
    assert(alloc_mode != KOS_AREA_FREE);
    frame->alloc_mode = (uint8_t)alloc_mode;
}

enum _KOS_AREA_TYPE _KOS_alloc_get_mode(KOS_FRAME frame)
{
    return (enum _KOS_AREA_TYPE)frame->alloc_mode;
}

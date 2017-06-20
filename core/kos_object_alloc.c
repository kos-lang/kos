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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_perf.h"
#include <stdio.h>
#include <string.h>

struct _KOS_AREA {
    KOS_ATOMIC(void *)   next;
    uint8_t              type;
    uint8_t              elem_size_pot;
    KOS_ATOMIC(uint32_t) free_lookup_offs;
    KOS_ATOMIC(uint32_t) bitmap[1];
};

struct _KOS_FIXED_AREA {
    KOS_ATOMIC(void *)   next;
    uint8_t              type;
    KOS_ATOMIC(uint32_t) first_free_offs;
};

struct _KOS_FREE_AREA {
    KOS_ATOMIC(void *)   next;
    uint8_t              type;
};

#define AVAIL_AREA_SIZE (_KOS_AREA_SIZE - sizeof(struct _KOS_AREA) + sizeof(KOS_ATOMIC(uint32_t)))

#define NUM_CHUNKS(size_pot) (AVAIL_AREA_SIZE * 64 / ((1 << ((size_pot)+6)) + 8))

#define BITMAP_ELEMS(num_chunks) (((num_chunks) + 31) >> 5)

#define AREA_FROM_OBJ_ID(obj_id) ((struct _KOS_AREA *)((intptr_t)(obj_id) & ~(intptr_t)(_KOS_AREA_SIZE - 1)))

static void *_alloc_buffer(struct _KOS_ALLOCATOR *allocator, size_t size);

static int _alloc_areas(struct _KOS_ALLOCATOR *allocator)
{
    uint8_t *pool = (uint8_t *)_alloc_buffer(allocator, _KOS_AREA_SIZE * _KOS_AREAS_POOL_SIZE);

    if (pool) {

        uint8_t *cur = pool + _KOS_AREA_SIZE * _KOS_AREAS_POOL_SIZE;

        struct _KOS_FREE_AREA *prev   = 0;
        KOS_ATOMIC(void *)    *hookup = &allocator->areas_free;

        /* Align to pool size */
        /* TODO see if _alloc_buffer can take care of alignment */
        cur  = (uint8_t *)((intptr_t)cur & ~(intptr_t)(_KOS_AREA_SIZE - 1));
        pool = (uint8_t *)(((intptr_t)pool + _KOS_AREA_SIZE - 1) & ~(intptr_t)(_KOS_AREA_SIZE - 1));

        while (cur > pool) {

            struct _KOS_FREE_AREA *const area = (struct _KOS_FREE_AREA *)(cur - _KOS_AREA_SIZE);

            KOS_atomic_write_ptr(area->next, (void *)prev);
            area->type = (uint8_t)KOS_AREA_FREE;
            prev       = area;

            cur = (uint8_t *)area;
        }

        _KOS_spin_lock(&allocator->lock);

        while (KOS_atomic_read_ptr(*hookup))
            hookup = &((struct _KOS_FREE_AREA *)KOS_atomic_read_ptr(*hookup))->next;

        KOS_atomic_write_ptr(*hookup, (void *)prev);

        _KOS_spin_unlock(&allocator->lock);

        return KOS_SUCCESS;
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
                                 (void *)((struct _KOS_FREE_AREA *)area)->next);

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

        KOS_atomic_write_u32(new_area->first_free_offs,
                              ((uint32_t)sizeof(struct _KOS_FIXED_AREA) + 15U) & ~15U);

        _KOS_spin_lock(&allocator->lock);

        KOS_atomic_write_ptr(new_area->next, KOS_atomic_read_ptr(allocator->areas_fixed));
        KOS_atomic_write_ptr(allocator->areas_fixed, (void *)new_area);

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

        KOS_atomic_write_ptr(new_area->next, KOS_atomic_read_ptr(*areas));
        KOS_atomic_write_ptr(*areas, (void *)new_area);

        _KOS_spin_unlock(&allocator->lock);
    }

    return KOS_SUCCESS;
}

int _KOS_alloc_init(KOS_CONTEXT *ctx)
{
    struct _KOS_ALLOCATOR *allocator = &ctx->allocator;

    static const uint8_t de_bruijn_bit_pos[32] = {
        0,   1, 28,  2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17,  4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18,  6, 11,  5, 10, 9
    };

    memset(allocator, 0, sizeof(*allocator));

    memcpy(allocator->de_bruijn_bit_pos, de_bruijn_bit_pos, sizeof(de_bruijn_bit_pos));

    allocator->str_oom_id = KOS_VOID;

    return _alloc_areas(allocator);
}

void _KOS_alloc_destroy(KOS_CONTEXT *ctx)
{
    void *objects = KOS_atomic_read_ptr(ctx->allocator.buffers);

    while (objects) {
        void *del = objects;
        objects   = *(void **)objects;
        _KOS_free(del);
    }
}

void *_KOS_alloc_object_internal(KOS_FRAME                frame,
                                 enum _KOS_AREA_ELEM_SIZE elem_size_pot,
                                 int                      elem_size)
{
    const enum _KOS_AREA_TYPE    alloc_mode = _KOS_alloc_get_mode(frame);
    struct _KOS_ALLOCATOR *const allocator  = frame->allocator;
    KOS_ATOMIC(void *)          *area_ptr;
    void                        *ret        = 0;

    if (_KOS_seq_fail()) {
        KOS_raise_exception(frame, allocator->str_oom_id);
        return 0;
    }

    assert((int)elem_size_pot >= 3 && (int)elem_size_pot <= 7);
    assert(elem_size <= (1 << (int)elem_size_pot));
    assert(alloc_mode != KOS_AREA_FREE);
    assert((int)elem_size_pot < 7 || alloc_mode == KOS_AREA_FIXED);

    /* TODO count FIXED, STACK separately */
    KOS_PERF_CNT_ARRAY(alloc_object, elem_size_pot);

    if (alloc_mode == KOS_AREA_FIXED)
        area_ptr = &allocator->areas_fixed;
    else
        area_ptr = &allocator->areas[elem_size_pot - 3];

    for (;;) {

        int error;

        if (alloc_mode == KOS_AREA_FIXED) {

            struct _KOS_FIXED_AREA *const area = (struct _KOS_FIXED_AREA *)
                    KOS_atomic_read_ptr(*area_ptr);

            if (area) {

                const int32_t aligned_size = (elem_size + 15) & ~15;

                const int32_t offs = KOS_atomic_add_i32(area->first_free_offs, aligned_size);

                if (offs + elem_size <= _KOS_AREA_SIZE) {
                    ret = (void *)((uint8_t *)area + offs);
                    break;
                }

                KOS_atomic_add_i32(area->first_free_offs, -aligned_size);
            }
        }
        else {

            struct _KOS_AREA *const area = (struct _KOS_AREA *)
                    KOS_atomic_read_ptr(*area_ptr);

            if (area) {

                const uint32_t num_chunks   = NUM_CHUNKS(elem_size_pot);
                const uint32_t bitmap_elems = BITMAP_ELEMS(num_chunks);

                for (;;) {

                    const uint32_t        lookup_offs = KOS_atomic_read_u32(area->free_lookup_offs);
                    KOS_ATOMIC(uint32_t) *bits_ptr;
                    uint32_t              bits;

                    if (lookup_offs >= bitmap_elems)
                        break; /* No more free blocks, allocate new area */

                    bits_ptr = &area->bitmap[lookup_offs];

                    bits = KOS_atomic_read_u32(*bits_ptr);

                    if (bits != ~0U) {
                        const uint32_t new_bits = bits | (bits + 1U);
                        uint32_t       offs;
                        uintptr_t      ptr;

                        if ( ! KOS_atomic_cas_u32(*bits_ptr, bits, new_bits))
                            /* Failed to grab this object, try another one */
                            continue;

                        offs = allocator->de_bruijn_bit_pos[((~bits & new_bits) * 0x077CB531U) >> 27];

                        offs = (lookup_offs << 5) + offs;

                        if (offs >= num_chunks)
                            break; /* No more free blocks, allocate new area */

                        ptr = ((uintptr_t)&area->bitmap[bitmap_elems] + 15) & ~(uintptr_t)15;

                        ret = (void *)(ptr + (offs << (int)elem_size_pot));
                        assert((uint8_t *)ret - (uint8_t *)area < _KOS_AREA_SIZE - 16);
                        break;
                    }

                    /* Try next 32-block group */
                    KOS_atomic_cas_u32(area->free_lookup_offs, lookup_offs, lookup_offs + 4U);
                }

                if (ret)
                    break;
            }
        }

        error = _alloc_area(allocator, alloc_mode, elem_size_pot);

        if (error) {
            KOS_raise_exception(frame, allocator->str_oom_id);
            return 0;
        }
    }

    return ret;
}

static void *_alloc_buffer(struct _KOS_ALLOCATOR *allocator, size_t size)
{
    uint8_t *obj = (uint8_t *)_KOS_malloc(size + sizeof(uint64_t) + 0x10);

    if (obj) {
        void **ptr = (void **)obj;

        for (;;) {
            void *next = KOS_atomic_read_ptr(allocator->buffers);
            *ptr       = next;
            if (KOS_atomic_cas_ptr(allocator->buffers, next, (void *)obj))
                break;
        }

        obj += sizeof(void *);
        obj += 0x10 - ((int)(uintptr_t)obj & 0xF);
    }

    return (void *)obj;
}

void *_KOS_alloc_buffer(KOS_FRAME frame, size_t size)
{
    void *buf = _alloc_buffer(frame->allocator, size);

    if (buf) {
        KOS_PERF_CNT(alloc_buffer);
        KOS_PERF_ADD(alloc_buffer_total, (uint32_t)size);
    }
    else
        KOS_raise_exception(frame, frame->allocator->str_oom_id);

    return buf;
}

void _KOS_free_buffer(KOS_FRAME frame, void *ptr, size_t size)
{
    /* TODO put on freed list */
}

void _KOS_alloc_set_mode(KOS_FRAME frame, enum _KOS_AREA_TYPE alloc_mode)
{
    assert(alloc_mode != KOS_AREA_FREE);
    frame->alloc_mode = (uint8_t)alloc_mode;
}

enum _KOS_AREA_TYPE _KOS_alloc_get_mode(KOS_FRAME frame)
{
    return (enum _KOS_AREA_TYPE)frame->alloc_mode;
}

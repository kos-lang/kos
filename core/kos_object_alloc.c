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

typedef struct _KOS_SLOT_PLACEHOLDER {
    uint8_t _dummy[1 << _KOS_OBJ_ALIGN_BITS];
} _KOS_SLOT;

struct _KOS_PAGE_HEADER {
    KOS_ATOMIC(_KOS_PAGE *) next;
    KOS_ATOMIC(_KOS_SLOT *) first_free_slot;
};

#define _KOS_PAGE_HDR_SIZE  (sizeof(struct _KOS_PAGE_HEADER))
#define _KOS_SLOTS_PER_PAGE (((_KOS_PAGE_SIZE - _KOS_PAGE_HDR_SIZE) << 2) / \
                             ((1 << (_KOS_OBJ_ALIGN_BITS + 2)) + 1))
#define _KOS_BITMAP_SIZE    (((_KOS_SLOTS_PER_PAGE + 15) & ~15) >> 2)
#define _KOS_BITMAP_OFFS    ((_KOS_PAGE_HDR_SIZE + 3) & ~3)
#define _KOS_SLOTS_OFFS     (_KOS_PAGE_SIZE - (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS))

static void _list_push(KOS_ATOMIC(void *) *list, void *new_ptr)
{
    for (;;) {
        void *next = KOS_atomic_read_ptr(*list);

        KOS_atomic_write_ptr(*(KOS_ATOMIC(void *) *)new_ptr, next);

        if (KOS_atomic_cas_ptr(*list, next, new_ptr))
            break;
    }
}

static void *_list_pop(KOS_ATOMIC(void *) *list)
{
    void *item;

    for (;;) {
        void *next;

        item = KOS_atomic_read_ptr(*list);

        if ( ! item)
            break;

        next = KOS_atomic_read_ptr(*(KOS_ATOMIC(void *) *)item);

        if (KOS_atomic_cas_ptr(*list, item, next))
            break;
    }

    return item;
}

int _KOS_alloc_init(KOS_CONTEXT *ctx)
{
    struct _KOS_ALLOCATOR *allocator = &ctx->allocator;

    memset(allocator, 0, sizeof(*allocator));

    allocator->str_oom_id = TO_SMALL_INT(0);

    assert(_KOS_BITMAP_OFFS + _KOS_BITMAP_SIZE <= _KOS_SLOTS_OFFS);
    assert( ! (_KOS_SLOTS_OFFS & 7U));
    assert(_KOS_SLOTS_OFFS + (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS) == _KOS_PAGE_SIZE);

    return KOS_SUCCESS;
}

void _KOS_alloc_destroy(KOS_CONTEXT *ctx)
{
    for (;;) {
        void *pool = _list_pop(&ctx->allocator.pools);

        if ( ! pool)
            break;

        _KOS_free(pool);
    }
}

static int _alloc_pool(struct _KOS_ALLOCATOR *allocator)
{
    uint8_t *const pool = (uint8_t *)_KOS_malloc(_KOS_POOL_SIZE);
    uint8_t       *begin;
    uint8_t       *page;

    if ( ! pool)
        return KOS_ERROR_OUT_OF_MEMORY;

    _list_push(&allocator->pools, pool);

    begin = (uint8_t *)(((uintptr_t)pool + _KOS_PAGE_SIZE) & ~(uintptr_t)(_KOS_PAGE_SIZE - 1));

    page = begin + _KOS_POOL_SIZE - _KOS_PAGE_SIZE;

    /* TODO reuse unused portions of the pool */

    while (page > begin) {
        page -= _KOS_PAGE_SIZE;
        assert( ! ((uintptr_t)page & (uintptr_t)(_KOS_PAGE_SIZE - 1)));
        _list_push((KOS_ATOMIC(void *) *)&allocator->free_pages, (void *)page);
        KOS_PERF_CNT(alloc_new_page);
    }

    return KOS_SUCCESS;
}

static _KOS_PAGE *_alloc_page(struct _KOS_ALLOCATOR *allocator)
{
    _KOS_PAGE *page;

    for (;;) {
        page = (_KOS_PAGE *)_list_pop((KOS_ATOMIC(void *) *)&allocator->free_pages);

        if (page)
            break;

        if (_alloc_pool(allocator))
            return 0;
    }

    KOS_atomic_write_ptr(page->first_free_slot,
                         (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS));

    assert( ! ((uintptr_t)KOS_atomic_read_ptr(page->first_free_slot) & ((1 << _KOS_OBJ_ALIGN_BITS) - 1)));

    KOS_PERF_CNT(alloc_free_page);

    return page;
}

static void *_alloc_bytes_from_page(_KOS_PAGE *page, uint32_t size)
{
    _KOS_SLOT *const slots_begin = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS);
    _KOS_SLOT *const slots_end   = slots_begin + _KOS_SLOTS_PER_PAGE;
    const uint32_t   alloc_size  = (size + sizeof(_KOS_SLOT) - 1) >> _KOS_OBJ_ALIGN_BITS;

    for (;;) {
        _KOS_SLOT *const slot = (_KOS_SLOT *)KOS_atomic_read_ptr(page->first_free_slot);
        _KOS_SLOT *const next = slot + alloc_size;

        if (next <= slots_end) {
            if (KOS_atomic_cas_ptr(page->first_free_slot, slot, next))
                return slot;
        }
        else
            break;
    }

    return 0;
}

static void *_alloc_huge_object(KOS_FRAME            frame,
                                enum KOS_ALLOC_HINT  alloc_hint,
                                enum KOS_OBJECT_TYPE object_type,
                                uint32_t             size)
{
    KOS_OBJ_HEADER *hdr;
    void          **ptr = (void **)_KOS_malloc(size + 2 * sizeof(void *));

    if ( ! ptr) {
        KOS_raise_exception(frame, frame->allocator->str_oom_id);
        return 0;
    }

    hdr = (KOS_OBJ_HEADER *)(ptr + 2);
    assert( ! ((uintptr_t)hdr & 7U));

    hdr->type       = (uint8_t)object_type;
    hdr->alloc_size = size;

    _list_push(&frame->allocator->pools, (void *)ptr);

    KOS_PERF_CNT(alloc_huge_object);

    return (void *)hdr;
}

static void *_alloc_object(KOS_FRAME            frame,
                           enum KOS_ALLOC_HINT  alloc_hint,
                           enum KOS_OBJECT_TYPE object_type,
                           uint32_t             size)
{
    struct _KOS_ALLOCATOR *allocator = frame->allocator;

    for (;;) {
        _KOS_PAGE *page = (_KOS_PAGE *)KOS_atomic_read_ptr(allocator->active_pages);

        while (page) {
            KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)
                    _alloc_bytes_from_page(page, size);

            if (hdr) {

                /* TODO sort active pages by remaining size in descending order */

                hdr->type       = (uint8_t)object_type;
                hdr->alloc_size = size;

                KOS_PERF_CNT(alloc_object);

                return hdr;
            }

            page = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next);
        }

        page = _alloc_page(allocator);

        if (page)
            _list_push((KOS_ATOMIC(void *) *)&allocator->active_pages, (void *)page);
        else
            break;
    }

    KOS_raise_exception(frame, frame->allocator->str_oom_id);
    return 0;
}

void *_KOS_alloc_object(KOS_FRAME            frame,
                        enum KOS_ALLOC_HINT  alloc_hint,
                        enum KOS_OBJECT_TYPE object_type,
                        uint32_t             size)
{
    if (size > _KOS_MAX_SMALL_OBJ_SIZE)
    {
        return _alloc_huge_object(frame, alloc_hint, object_type, size);
    }
    else
    {
        return _alloc_object(frame, alloc_hint, object_type, size);
    }
}

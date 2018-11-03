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

#include "kos_heap.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum _GC_STATE {
    GC_INACTIVE,
    GC_INIT
};

struct _KOS_POOL_HEADER {
    _KOS_POOL *next;        /* Pointer to next pool header                    */
    void      *memory;      /* Pointer to the allocated memory                */
    void      *usable_ptr;  /* Pointer to usable region of memory in the pool */
    uint32_t   alloc_size;  /* Number of allocated bytes                      */
    uint32_t   usable_size; /* Size of the usable region                      */
};

struct _KOS_WASTE_HEADER {
    _KOS_WASTE *next;
    uint32_t    size;
};

typedef struct _KOS_SLOT_PLACEHOLDER {
    uint8_t _dummy[1 << _KOS_OBJ_ALIGN_BITS];
} _KOS_SLOT;

struct _KOS_PAGE_HEADER {
    _KOS_PAGE           *next;
    uint32_t             num_slots;       /* Total number of slots in this page */
    KOS_ATOMIC(uint32_t) num_allocated;   /* Number of slots allocated          */
    KOS_ATOMIC(uint32_t) num_used;        /* Number of slots used, only for GC  */
};

#define _KOS_PAGE_HDR_SIZE  (sizeof(struct _KOS_PAGE_HEADER))
#define _KOS_SLOTS_PER_PAGE (((_KOS_PAGE_SIZE - _KOS_PAGE_HDR_SIZE) << 2) / \
                             ((1U << (_KOS_OBJ_ALIGN_BITS + 2)) + 1U))
#define _KOS_BITMAP_SIZE    (((_KOS_SLOTS_PER_PAGE + 15U) & ~15U) >> 2)
#define _KOS_BITMAP_OFFS    ((_KOS_PAGE_HDR_SIZE + 3U) & ~3U)
#define _KOS_SLOTS_OFFS     (_KOS_PAGE_SIZE - (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS))

#ifdef __cplusplus

template<typename T>
void _list_push(T*& list, T* value)
{
    value->next = list;
    list        = value;
}

template<typename T>
T* _list_pop(T*& list)
{
    T* const ret = list;

    if (ret) {
        T* const next = ret->next;

        list = next;
    }

    return ret;
}

#define PUSH_LIST(list, value) _list_push(list, value)

#define POP_LIST(list) _list_pop(list)

#else

static void _list_push(void **list_ptr, void *value, void **next_ptr)
{
    *next_ptr = *list_ptr;
    *list_ptr = value;
}

static void *_list_pop(void **list_ptr)
{
    void *ret = *list_ptr;

    if (ret) {
        void *next = *(void **)ret;

        *list_ptr = next;
    }

    return ret;
}

#define PUSH_LIST(list, value) _list_push((void **)&(list), (void *)(value), (void **)&(value)->next)

#define POP_LIST(list) _list_pop((void **)&(list))

#endif

static struct _KOS_HEAP *_get_heap(KOS_CONTEXT ctx)
{
    return &ctx->inst->heap;
}

int _KOS_heap_init(KOS_INSTANCE *inst)
{
    struct _KOS_HEAP *heap = &inst->heap;

    KOS_atomic_write_u32(heap->gc_state, 0);
    heap->heap_size      = 0;
    heap->used_size      = 0;
    heap->gc_threshold   = _KOS_GC_STEP;
    heap->free_pages     = 0;
    heap->non_full_pages = 0;
    heap->full_pages     = 0;
    heap->pools          = 0;
    heap->pool_headers   = 0;
    heap->waste          = 0;

    assert(_KOS_BITMAP_OFFS + _KOS_BITMAP_SIZE <= _KOS_SLOTS_OFFS);
    assert( ! (_KOS_SLOTS_OFFS & 7U));
    assert(_KOS_SLOTS_OFFS + (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS) == _KOS_PAGE_SIZE);

    return _KOS_create_mutex(&heap->mutex);
}

void _KOS_heap_destroy(KOS_INSTANCE *inst)
{
    for (;;) {
        _KOS_POOL *pool = (_KOS_POOL *)POP_LIST(inst->heap.pools);
        void      *memory;

        if ( ! pool)
            break;

        memory = pool->memory;
        _KOS_free(memory);

        if (pool != memory)
            _KOS_free(pool);
    }

    _KOS_destroy_mutex(&inst->heap.mutex);
}

static void _register_wasted_region(struct _KOS_HEAP *heap,
                                    void             *ptr,
                                    uint32_t          size)
{
    if (size >= sizeof(_KOS_WASTE)) {

        _KOS_WASTE *waste = (_KOS_WASTE *)ptr;

        waste->size = size;

        PUSH_LIST(heap->waste, waste);
    }

    heap->used_size += size;
}

static _KOS_POOL *_alloc_pool(struct _KOS_HEAP *heap,
                              uint32_t          alloc_size)
{
    _KOS_POOL *pool_hdr;
    uint8_t   *pool;
    uint8_t   *begin;
    uint32_t   waste_at_front;

    if (heap->heap_size + alloc_size > _KOS_MAX_HEAP_SIZE)
        return 0;

    pool = (uint8_t *)_KOS_malloc(alloc_size);

    if ( ! pool)
        return 0;

    heap->heap_size += alloc_size;

    begin = (uint8_t *)KOS_align_up((uintptr_t)pool, (uintptr_t)_KOS_PAGE_SIZE);

    waste_at_front = (uint32_t)(begin - pool);

    if (waste_at_front < sizeof(_KOS_POOL)) {
        pool_hdr = (_KOS_POOL *)_KOS_malloc(sizeof(struct _KOS_POOL_HEADER));

        if ( ! pool_hdr) {
            _KOS_free(pool);
            return 0;
        }
    }
    else {

        uint8_t *waste;
        uint32_t waste_size;

        assert(waste_at_front >= sizeof(_KOS_POOL));

        pool_hdr   = (_KOS_POOL *)pool;
        waste      = pool + sizeof(_KOS_POOL);
        waste_size = waste_at_front - (uint32_t)sizeof(_KOS_POOL);

        _register_wasted_region(heap, waste, waste_size);
    }

    pool_hdr->memory      = pool;
    pool_hdr->alloc_size  = alloc_size;
    pool_hdr->usable_ptr  = begin;
    pool_hdr->usable_size = (uint32_t)((pool + alloc_size) - begin);

    PUSH_LIST(heap->pools, pool_hdr);

    return pool_hdr;
}

static int _alloc_page_pool(struct _KOS_HEAP *heap)
{
    _KOS_POOL *pool_hdr  = _alloc_pool(heap, _KOS_POOL_SIZE);
    _KOS_PAGE *next_page = 0;
    uint8_t   *begin;
    uint8_t   *usable_end;
    uint8_t   *page_bytes;
    uint32_t   page_size;

    if ( ! pool_hdr)
        return KOS_ERROR_OUT_OF_MEMORY;

    begin      = (uint8_t *)pool_hdr->usable_ptr;
    usable_end = begin + pool_hdr->usable_size;
    page_bytes = (uint8_t *)((uintptr_t)usable_end & ~(uintptr_t)(_KOS_PAGE_SIZE - 1U));
    page_size  = (uint32_t)(usable_end - page_bytes);

    if (page_size > _KOS_SLOTS_OFFS + (_KOS_PAGE_SIZE >> 3))
        page_bytes += page_size;
    else {

        uint32_t waste_size = (uint32_t)(usable_end - page_bytes);

        pool_hdr->usable_size -= waste_size;

        _register_wasted_region(heap, page_bytes, waste_size);

        page_size = _KOS_PAGE_SIZE;
    }

    assert(heap->free_pages == 0);

    while (page_bytes > begin) {

        _KOS_PAGE *page;

        page_bytes -= page_size;
        assert( ! ((uintptr_t)page_bytes & (uintptr_t)(_KOS_PAGE_SIZE - 1)));

        page = (_KOS_PAGE *)page_bytes;

        page->num_slots = (page_size - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;

        KOS_atomic_write_u32(page->num_allocated, 0);

        page->next = next_page;

        KOS_PERF_CNT(alloc_new_page);

        page_size = _KOS_PAGE_SIZE;

        next_page = page;
    }

    heap->free_pages = next_page;

#ifndef NDEBUG
    {
        _KOS_PAGE *page      = heap->free_pages;
        uint32_t   num_pages = 0;

        next_page = page;

        while (page) {

            page_size = page->num_slots == _KOS_SLOTS_PER_PAGE
                      ? _KOS_PAGE_SIZE
                      : _KOS_SLOTS_OFFS + (page->num_slots << _KOS_OBJ_ALIGN_BITS);

            assert(page->num_slots <= _KOS_SLOTS_PER_PAGE);
            assert(page->num_slots >= (_KOS_PAGE_SIZE >> (3 + _KOS_OBJ_ALIGN_BITS)));
            assert(KOS_atomic_read_u32(page->num_allocated) == 0);

            assert(page == next_page);

            assert((uintptr_t)page >= (uintptr_t)pool_hdr->usable_ptr);

            assert((uintptr_t)page + page_size <=
                   (uintptr_t)pool_hdr->usable_ptr + pool_hdr->usable_size);

            next_page = (_KOS_PAGE *)((uint8_t *)page + page_size);

            page = page->next;

            ++num_pages;
        }

        assert(num_pages == _KOS_POOL_SIZE / _KOS_PAGE_SIZE ||
               num_pages == (_KOS_POOL_SIZE / _KOS_PAGE_SIZE) - 1U);
    }
#endif

    return KOS_SUCCESS;
}

static _KOS_PAGE *_alloc_page(struct _KOS_HEAP *heap)
{
    _KOS_PAGE *page = heap->free_pages;

    if ( ! page) {

        if (_alloc_page_pool(heap))
            return 0;

        page = heap->free_pages;

        assert(page);
    }

    assert(page->num_slots <= _KOS_SLOTS_PER_PAGE);
    assert(KOS_atomic_read_u32(page->num_allocated) == 0);
    assert(page->next == 0 || page < page->next);

    heap->free_pages = page->next;

    KOS_PERF_CNT(alloc_free_page);

    return page;
}

#ifndef NDEBUG
int _KOS_heap_lend_page(KOS_CONTEXT ctx,
                        void       *buffer,
                        size_t      size)
{
    const uintptr_t   buf_ptr      = (uintptr_t)buffer;
    const uintptr_t   good_buf_ptr = KOS_align_up(buf_ptr, (uintptr_t)_KOS_PAGE_SIZE);
    const uintptr_t   reserved     = good_buf_ptr - buf_ptr + _KOS_SLOTS_OFFS
                                     + (1U << _KOS_OBJ_ALIGN_BITS);
    struct _KOS_HEAP *heap         = _get_heap(ctx);
    int               lent         = 0;

    _KOS_lock_mutex(&heap->mutex);

    if (reserved <= size) {

        _KOS_PAGE  *page      = (_KOS_PAGE *)good_buf_ptr;
        _KOS_PAGE **insert_at = &heap->free_pages;

        page->num_slots = (uint32_t)(size - reserved) >> _KOS_OBJ_ALIGN_BITS;
        KOS_atomic_write_u32(page->num_allocated, 0);

        while (*insert_at && page > *insert_at)
            insert_at = &(*insert_at)->next;

        page->next = *insert_at;
        *insert_at = page;

        lent = 1;
    }

    _KOS_unlock_mutex(&heap->mutex);

    return lent;
}
#endif

static void _try_collect_garbage(KOS_CONTEXT ctx)
{
    struct _KOS_HEAP *const heap = _get_heap(ctx);

    if (heap->used_size > heap->gc_threshold &&
        ! (ctx->inst->flags & KOS_INST_MANUAL_GC)) {

        _KOS_unlock_mutex(&heap->mutex);

        KOS_collect_garbage(ctx, 0);

        _KOS_lock_mutex(&heap->mutex);
    }
}

static void *_alloc_slots_from_page(_KOS_PAGE *page, uint32_t num_slots)
{
    const uint32_t total_slots   = page->num_slots;
    const uint32_t num_allocated = KOS_atomic_read_u32(page->num_allocated);
    uint32_t       new_num_slots = num_allocated + num_slots;
    _KOS_SLOT     *slot          = 0;

    assert(num_slots > 0);

    if (new_num_slots <= total_slots) {

        slot = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS) + num_allocated;

        assert(slot == (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS + (num_allocated << _KOS_OBJ_ALIGN_BITS)));

        KOS_atomic_write_u32(page->num_allocated, new_num_slots);
    }

    return slot;
}

static KOS_OBJ_HEADER *_alloc_object_from_page(_KOS_PAGE           *page,
                                               enum KOS_OBJECT_TYPE object_type,
                                               uint32_t             num_slots)
{
    KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)_alloc_slots_from_page(page, num_slots);

    if (hdr) {
        hdr->alloc_size = TO_SMALL_INT(num_slots << _KOS_OBJ_ALIGN_BITS);
        hdr->type       = (uint8_t)object_type;

        KOS_PERF_CNT(alloc_object);
    }

    return hdr;
}

void *_KOS_heap_early_alloc(KOS_INSTANCE        *inst,
                            KOS_CONTEXT          ctx,
                            enum KOS_OBJECT_TYPE object_type,
                            uint32_t             size)
{
    const uint32_t num_slots = (size + sizeof(_KOS_SLOT) - 1) >> _KOS_OBJ_ALIGN_BITS;

    if ( ! ctx->cur_page) {

        _KOS_lock_mutex(&inst->heap.mutex);

        ctx->cur_page = _alloc_page(&inst->heap);

        _KOS_unlock_mutex(&inst->heap.mutex);

        if ( ! ctx->cur_page)
            return 0;
    }

    return _alloc_object_from_page(ctx->cur_page, object_type, num_slots);
}

static uint32_t _full_page_size(_KOS_PAGE *page)
{
    return _KOS_SLOTS_OFFS + (page->num_slots << _KOS_OBJ_ALIGN_BITS);
}

static uint32_t _non_full_page_size(_KOS_PAGE *page)
{
    return _KOS_SLOTS_OFFS + (KOS_atomic_read_u32(page->num_allocated) << _KOS_OBJ_ALIGN_BITS);
}

void _KOS_heap_release_thread_page(KOS_CONTEXT ctx)
{
    if (ctx->cur_page) {

        struct _KOS_HEAP *const heap = &ctx->inst->heap;

        _KOS_lock_mutex(&heap->mutex);

        PUSH_LIST(heap->non_full_pages, ctx->cur_page);

        heap->used_size += _non_full_page_size(ctx->cur_page);

        _KOS_unlock_mutex(&heap->mutex);

        ctx->cur_page = 0;
    }
}

static KOS_OBJ_HEADER *_setup_huge_object_in_page(struct _KOS_HEAP    *heap,
                                                  _KOS_PAGE           *page,
                                                  enum KOS_OBJECT_TYPE object_type,
                                                  uint32_t             size)
{
    KOS_OBJ_HEADER *hdr = (KOS_OBJ_HEADER *)((uint8_t *)page + _KOS_SLOTS_OFFS);

    assert( ! ((uintptr_t)hdr & 7U));

    hdr->alloc_size = TO_SMALL_INT(KOS_align_up(size, 1U << _KOS_OBJ_ALIGN_BITS));
    hdr->type       = (uint8_t)object_type;

    KOS_PERF_CNT(alloc_huge_object);

    PUSH_LIST(heap->full_pages, page);

    assert(page->num_slots > _KOS_SLOTS_PER_PAGE);

    heap->used_size += _full_page_size(page);

    KOS_atomic_write_u32(page->num_allocated, page->num_slots);

    return hdr;
}

static void *_alloc_huge_object(KOS_CONTEXT          ctx,
                                enum KOS_OBJECT_TYPE object_type,
                                uint32_t             size)
{
    struct _KOS_HEAP *heap = _get_heap(ctx);
    KOS_OBJ_HEADER   *hdr  = 0;
    _KOS_PAGE       **page_ptr;
    _KOS_PAGE        *page;

    _KOS_lock_mutex(&heap->mutex);

    _try_collect_garbage(ctx);

    page_ptr = &heap->free_pages;
    page     = *page_ptr;

    while (page) {

        _KOS_PAGE **next_ptr  = page_ptr;
        _KOS_PAGE  *next_page = page;
        uint32_t    accum     = 0;

        do {

            if (page != next_page)
                break;

            next_ptr = &page->next;

            if (page->num_slots < _KOS_SLOTS_PER_PAGE) {
                page = *next_ptr;
                break;
            }

            accum += _KOS_PAGE_SIZE;

            if (accum >= size + _KOS_SLOTS_OFFS) {

                const uint32_t num_slots = (accum - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;

                page      = *page_ptr;
                next_page = *next_ptr;

#ifndef NDEBUG
                {
                    _KOS_PAGE *test_page = page;
                    _KOS_PAGE *expected  = page;
                    uint32_t   num_pages = 0;

                    while (test_page != next_page) {

                        assert(test_page == expected);

                        expected = (_KOS_PAGE *)((uint8_t *)test_page + _KOS_PAGE_SIZE);

                        assert(test_page >= page);
                        assert( ! next_page || test_page < next_page);

                        assert(test_page->num_slots == _KOS_SLOTS_PER_PAGE);
                        assert(KOS_atomic_read_u32(test_page->num_allocated) == 0);

                        ++num_pages;

                        test_page = test_page->next;
                    }

                    assert(num_pages * _KOS_PAGE_SIZE == accum);
                }
#endif
                *page_ptr = next_page;

                page->num_slots = num_slots;

                hdr = _setup_huge_object_in_page(heap, page, object_type, size);

                assert((uint8_t *)hdr + size <= (uint8_t *)page + accum);

                page = 0;
                break;
            }

            next_page = (_KOS_PAGE *)((uintptr_t)page + _KOS_PAGE_SIZE);
            page      = *next_ptr;

        } while (page);

        page_ptr = next_ptr;
    }

    if ( ! hdr) {

        _KOS_POOL *pool = _alloc_pool(heap, size + _KOS_SLOTS_OFFS + _KOS_PAGE_SIZE);

        if (pool) {
            page = (_KOS_PAGE *)pool->usable_ptr;

            /* TODO register tail as wasted */

            page->num_slots = (pool->usable_size - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;

            assert((page->num_slots << _KOS_OBJ_ALIGN_BITS) >= size);

            hdr = _setup_huge_object_in_page(heap,
                                             page,
                                             object_type,
                                             size);

            assert((uint8_t *)hdr + size <= (uint8_t *)pool->usable_ptr + pool->usable_size);
        }
        else
            KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
    }

    _KOS_unlock_mutex(&heap->mutex);

    return (void *)hdr;
}

static int _is_page_full(_KOS_PAGE *page)
{
    return KOS_atomic_read_u32(page->num_allocated) == page->num_slots;
}

static void *_alloc_object(KOS_CONTEXT          ctx,
                           enum KOS_OBJECT_TYPE object_type,
                           uint32_t             size)
{
    _KOS_PAGE        *page       = ctx->cur_page;
    const uint32_t    num_slots  = (size + sizeof(_KOS_SLOT) - 1) >> _KOS_OBJ_ALIGN_BITS;
    unsigned          seek_depth = _KOS_MAX_PAGE_SEEK;
    struct _KOS_HEAP *heap;
    _KOS_PAGE       **page_ptr;
    KOS_OBJ_HEADER   *hdr        = 0;

    KOS_instance_validate(ctx);

    /* Fast path: allocate from a page held by this thread */
    if (page) {

        hdr = _alloc_object_from_page(page, object_type, num_slots);

        if (hdr)
            return hdr;
    }

    /* Slow path: find a non-full page in the heap which has enough room or
     * allocate a new page. */

    heap = _get_heap(ctx);

    _KOS_lock_mutex(&heap->mutex);

    /* TODO if in GC, unlock mutex and help GC */
#if 0
    if (_is_gc_active(heap)) {

        _KOS_unlock_mutex(&heap->mutex);

        _help_gc(ctx);

        _KOS_lock_mutex(&heap->mutex);
    }
#endif

    /* Check if any of the non-full pages contains enough space */

    page_ptr = &heap->non_full_pages;

    while (seek_depth--) {

        _KOS_PAGE *old_page = *page_ptr;
        uint32_t   page_size;

        if ( ! old_page)
            break;

        page_size = _non_full_page_size(old_page);

        hdr = _alloc_object_from_page(old_page, object_type, num_slots);

        if (hdr) {
            if (_is_page_full(old_page)) {
                *page_ptr = old_page->next;
                PUSH_LIST(heap->full_pages, old_page);
                heap->used_size += _full_page_size(old_page) - page_size;
            }

            break;
        }

        page_ptr = &old_page->next;
    }

    if ( ! hdr) {

        /* Release thread's current page */
        if (page) {

            if (_is_page_full(page)) {
                PUSH_LIST(heap->full_pages, page);
                heap->used_size += _full_page_size(page);
            }
            else {
                PUSH_LIST(heap->non_full_pages, page);
                heap->used_size += _non_full_page_size(page);
            }

            ctx->cur_page = 0;
        }

        _try_collect_garbage(ctx);

        /* Allocate a new page */
        page = _alloc_page(heap);

        /* If page capacity is too small, find page which is big enough */
        if (page && page->num_slots < num_slots) {

            _KOS_PAGE *pages_too_small = page;

            page->next = 0;

            for (;;) {

                page = _alloc_page(heap);

                if ( ! page || page->num_slots >= num_slots)
                    break;

                assert(page != pages_too_small);

                page->next = pages_too_small;

                pages_too_small = page;
            }

            while (pages_too_small) {

                _KOS_PAGE  *next      = pages_too_small->next;
                _KOS_PAGE **insert_at = &heap->free_pages;
                _KOS_PAGE  *next_free = *insert_at;

                while (next_free && pages_too_small > next_free) {

                    insert_at = &next_free->next;
                    next_free = *insert_at;
                }

                assert(pages_too_small != next_free);
                assert(pages_too_small < next_free || ! next_free);

                pages_too_small->next = next_free;

                *insert_at = pages_too_small;

                pages_too_small = next;
            }
        }

        if (page) {

            assert(page->num_slots >= num_slots);

            ctx->cur_page = page;

            hdr = _alloc_object_from_page(page, object_type, num_slots);

            assert(hdr);
        }
    }

    if ( ! hdr)
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));

    _KOS_unlock_mutex(&heap->mutex);

    return hdr;
}

void *_KOS_alloc_object(KOS_CONTEXT          ctx,
                        enum KOS_OBJECT_TYPE object_type,
                        uint32_t             size)
{
    if (size > (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS))
    {
        return _alloc_huge_object(ctx, object_type, size);
    }
    else
    {
        return _alloc_object(ctx, object_type, size);
    }
}

void *_KOS_alloc_object_page(KOS_CONTEXT          ctx,
                             enum KOS_OBJECT_TYPE object_type)
{
    return _alloc_object(ctx, object_type, _KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS);
}

static void _release_current_page(KOS_CONTEXT ctx)
{
    _KOS_PAGE *page = ctx->cur_page;

    if (page) {

        struct _KOS_HEAP *heap = _get_heap(ctx);

        _KOS_lock_mutex(&heap->mutex);

        PUSH_LIST(heap->non_full_pages, page);

        heap->used_size += _non_full_page_size(page);

        _KOS_unlock_mutex(&heap->mutex);

        ctx->cur_page = 0;
    }
}

static void _release_current_page_locked(KOS_CONTEXT ctx)
{
    _KOS_PAGE *page = ctx->cur_page;

    if (page) {

        struct _KOS_HEAP *heap = _get_heap(ctx);

        PUSH_LIST(heap->non_full_pages, page);

        heap->used_size += _non_full_page_size(page);

        ctx->cur_page = 0;
    }
}

static void _stop_the_world(KOS_CONTEXT ctx)
{
    /* TODO make all threads enter _help_gc() */
}

enum _KOS_MARK_STATE {
    WHITE     = 0,
    GRAY      = 1,
    BLACK     = 2,
    COLORMASK = 3
};

static void _set_marking_in_pages(_KOS_PAGE           *page,
                                  enum _KOS_MARK_STATE state)
{
    const int mask = (int)state * 0x55;

    while (page) {

        uint32_t *const bitmap = (uint32_t *)((uint8_t *)page + _KOS_BITMAP_OFFS);

        memset(bitmap, mask, _KOS_BITMAP_SIZE);

        KOS_atomic_write_u32(page->num_used, 0);

        page = page->next;
    }
}

static void _clear_marking(struct _KOS_HEAP *heap)
{
    _set_marking_in_pages(heap->non_full_pages, WHITE);
    _set_marking_in_pages(heap->full_pages,     WHITE);
    _set_marking_in_pages(heap->free_pages,     GRAY);
}

struct _KOS_MARK_LOC {
    KOS_ATOMIC(uint32_t) *bitmap;
    uint32_t              mask_idx;
};

static struct _KOS_MARK_LOC _get_mark_location(KOS_OBJ_ID obj_id)
{
    const uintptr_t offs_in_page  = (uintptr_t)obj_id & (uintptr_t)(_KOS_PAGE_SIZE - 1);
    const uint32_t  slot_idx      = (uint32_t)(offs_in_page - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;

    const uintptr_t page_addr     = (uintptr_t)obj_id & ~(uintptr_t)(_KOS_PAGE_SIZE - 1);
    uint32_t *const bitmap        = (uint32_t *)(page_addr + _KOS_BITMAP_OFFS) + (slot_idx >> 4);

    struct _KOS_MARK_LOC mark_loc = { 0, 0 };

    mark_loc.bitmap   = (KOS_ATOMIC(uint32_t) *)bitmap;
    mark_loc.mask_idx = (slot_idx & 0xFU) * 2;

    return mark_loc;
}

static void _advance_marking(struct _KOS_MARK_LOC *mark_loc,
                             uint32_t              num_slots)
{
    const uint32_t mask_idx = mark_loc->mask_idx + num_slots * 2U;

    mark_loc->bitmap   += mask_idx >> 5;
    mark_loc->mask_idx =  mask_idx & 0x1FU;
}

static uint32_t _get_marking(const struct _KOS_MARK_LOC *mark_loc)
{
    const uint32_t marking = KOS_atomic_read_u32(*(mark_loc->bitmap));

    return (marking >> mark_loc->mask_idx) & COLORMASK;
}

static int _set_mark_state_loc(struct _KOS_MARK_LOC mark_loc,
                               enum _KOS_MARK_STATE state)
{
    const uint32_t mask = (uint32_t)state << mark_loc.mask_idx;

    uint32_t value = KOS_atomic_read_u32(*mark_loc.bitmap);

    while ( ! (value & mask)) {

        if (KOS_atomic_cas_u32(*mark_loc.bitmap, value, value | mask))
            return 1;

        value = KOS_atomic_read_u32(*mark_loc.bitmap);
    }

    return 0;
}

static int _set_mark_state(KOS_OBJ_ID           obj_id,
                           enum _KOS_MARK_STATE state)
{
    assert(((uintptr_t)obj_id & 0xFFFFFFFFU) != 0xDDDDDDDDU);

    /* TODO can we get rid of IS_BAD_PTR ? */
    if (IS_HEAP_OBJECT(obj_id) && ! IS_BAD_PTR(obj_id)) {

        const struct _KOS_MARK_LOC mark_loc = _get_mark_location(obj_id);

        return _set_mark_state_loc(mark_loc, state);
    }
    else
        return 0;
}

static void _mark_children_gray(KOS_OBJ_ID obj_id)
{
    switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            /* fall through */
        case OBJ_FLOAT:
            /* fall through */
        case OBJ_OPAQUE:
            /* fall through */
        case OBJ_BUFFER_STORAGE:
            break;

        case OBJ_STRING:
            if (OBJPTR(STRING, obj_id)->header.flags & KOS_STRING_REF)
                _set_mark_state(OBJPTR(STRING, obj_id)->ref.obj_id, GRAY);
            break;

        default:
            assert(READ_OBJ_TYPE(obj_id) == OBJ_OBJECT);
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id)->props), GRAY);
            _set_mark_state(OBJPTR(OBJECT, obj_id)->prototype, GRAY);
            break;

        case OBJ_ARRAY:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(ARRAY, obj_id)->data), GRAY);
            break;

        case OBJ_BUFFER:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(BUFFER, obj_id)->data), GRAY);
            break;

        case OBJ_FUNCTION:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->closures, GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->defaults, GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->generator_stack_frame, GRAY);
            break;

        case OBJ_CLASS:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->prototype), GRAY);
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->props), GRAY);
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(CLASS, obj_id)->closures, GRAY);
            _set_mark_state(OBJPTR(CLASS, obj_id)->defaults, GRAY);
            break;

        case OBJ_OBJECT_STORAGE:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT_STORAGE, obj_id)->new_prop_table), GRAY);
            {
                KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id)->items[0];
                KOS_PITEM *end  = item + OBJPTR(OBJECT_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item) {
                    _set_mark_state(KOS_atomic_read_obj(item->key), GRAY);
                    _set_mark_state(KOS_atomic_read_obj(item->value), GRAY);
                }
            }
            break;

        case OBJ_ARRAY_STORAGE:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(ARRAY_STORAGE, obj_id)->next), GRAY);
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(ARRAY_STORAGE, obj_id)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(ARRAY_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item)
                    _set_mark_state(KOS_atomic_read_obj(*item), GRAY);
            }
            break;

        case OBJ_DYNAMIC_PROP:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->getter, GRAY);
            _set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->setter, GRAY);
            break;

        case OBJ_OBJECT_WALK:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->obj,         GRAY);
            _set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->key_table,   GRAY);
            _set_mark_state(KOS_atomic_read_obj(
                            OBJPTR(OBJECT_WALK, obj_id)->last_key),   GRAY);
            _set_mark_state(KOS_atomic_read_obj(
                            OBJPTR(OBJECT_WALK, obj_id)->last_value), GRAY);
            break;

        case OBJ_MODULE:
            /* TODO lock gc during module setup */
            _set_mark_state(OBJPTR(MODULE, obj_id)->name,         GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->path,         GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->constants,    GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->global_names, GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->globals,      GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->module_names, GRAY);
            break;

        case OBJ_STACK:
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(STACK, obj_id)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(STACK, obj_id)->size;
                for ( ; item < end; ++item)
                    _set_mark_state(KOS_atomic_read_obj(*item), GRAY);
            }
            break;

        case OBJ_LOCAL_REFS:
            {
                KOS_OBJ_ID **ref = &OBJPTR(LOCAL_REFS, obj_id)->refs[0];
                KOS_OBJ_ID **end = ref + OBJPTR(LOCAL_REFS, obj_id)->header.num_tracked;

                _set_mark_state(OBJPTR(LOCAL_REFS, obj_id)->next, GRAY);

                for ( ; ref < end; ++ref)
                    _set_mark_state(**ref, GRAY);
            }
            break;
    }
}

static int _mark_object_black(KOS_OBJ_ID obj_id)
{
    int marked = 0;

    if (IS_HEAP_OBJECT(obj_id)) {

        assert( ! IS_BAD_PTR(obj_id));

        marked = _set_mark_state(obj_id, BLACK);

        _mark_children_gray(obj_id);
    }

    return marked;
}

static uint32_t _get_num_active_slots(_KOS_PAGE *page)
{
    /* For a huge object, only match the beginning of the page */
    return (page->num_slots > _KOS_SLOTS_PER_PAGE)
           ? 1U
           : KOS_atomic_read_u32(page->num_allocated);
}

static int _gray_to_black_in_pages(_KOS_PAGE *page)
{
    int marked = 0;

    for ( ; page; page = page->next) {

        uint32_t num_slots_used = 0;

        struct _KOS_MARK_LOC mark_loc = { 0, 0 };

        _KOS_SLOT *ptr = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS);
        _KOS_SLOT *end = ptr + _get_num_active_slots(page);

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + _KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t        size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t        slots = size >> _KOS_OBJ_ALIGN_BITS;
            const uint32_t        color = _get_marking(&mark_loc);

            if (color == GRAY) {
                marked         += _set_mark_state_loc(mark_loc, BLACK);
                num_slots_used += slots;

                _mark_children_gray((KOS_OBJ_ID)((intptr_t)hdr + 1));
            }
            else if (color)
                /* TODO mark children gray if it's already black? */
                num_slots_used += slots;

            _advance_marking(&mark_loc, slots);

            ptr = (_KOS_SLOT *)((uint8_t *)ptr + size);
        }

        assert(num_slots_used >= KOS_atomic_read_u32(page->num_used));

        KOS_atomic_write_u32(page->num_used, num_slots_used);
    }

    return marked;
}

static int _gray_to_black(struct _KOS_HEAP *heap)
{
    int marked = 0;

    marked += _gray_to_black_in_pages(heap->non_full_pages);
    marked += _gray_to_black_in_pages(heap->full_pages);

    return marked;
}

static void _mark_from_thread_context(KOS_CONTEXT ctx)
{
    uint32_t i;

    if ( ! IS_BAD_PTR(ctx->exception))
        _mark_object_black(ctx->exception);
    if ( ! IS_BAD_PTR(ctx->retval))
        _mark_object_black(ctx->retval);
    if ( ! IS_BAD_PTR(ctx->stack))
        _mark_object_black(ctx->stack);
    if ( ! IS_BAD_PTR(ctx->local_refs))
        _mark_object_black(ctx->local_refs);

    for (i = 0; i < ctx->tmp_ref_count; ++i) {
        KOS_OBJ_ID *const obj_id_ptr = ctx->tmp_refs[i];
        if (obj_id_ptr) {
            const KOS_OBJ_ID obj_id = *obj_id_ptr;
            if ( ! IS_BAD_PTR(obj_id))
                _mark_object_black(obj_id);
        }
    }
}

static void _mark_roots(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    {
        int i;

        for (i = 0; i < KOS_STR_NUM; ++i)
            _mark_object_black(inst->common_strings[i]);
    }

    _mark_object_black(inst->prototypes.object_proto);
    _mark_object_black(inst->prototypes.number_proto);
    _mark_object_black(inst->prototypes.integer_proto);
    _mark_object_black(inst->prototypes.float_proto);
    _mark_object_black(inst->prototypes.string_proto);
    _mark_object_black(inst->prototypes.boolean_proto);
    _mark_object_black(inst->prototypes.array_proto);
    _mark_object_black(inst->prototypes.buffer_proto);
    _mark_object_black(inst->prototypes.function_proto);
    _mark_object_black(inst->prototypes.class_proto);
    _mark_object_black(inst->prototypes.generator_proto);
    _mark_object_black(inst->prototypes.exception_proto);
    _mark_object_black(inst->prototypes.generator_end_proto);
    _mark_object_black(inst->prototypes.thread_proto);

    _mark_object_black(inst->modules.init_module);
    _mark_object_black(inst->modules.search_paths);
    _mark_object_black(inst->modules.module_names);
    _mark_object_black(inst->modules.modules);
    _mark_object_black(inst->modules.module_inits);

    _mark_object_black(inst->args);

    /* TODO go over all threads */
    _mark_from_thread_context(ctx);
}

static void _get_flat_list(_KOS_PAGE *page, _KOS_PAGE ***begin, _KOS_PAGE ***end)
{
    uint8_t *const ptr       = (uint8_t *)page + _KOS_BITMAP_OFFS;
    const uint32_t num_slots = KOS_min(page->num_slots, (uint32_t)_KOS_SLOTS_PER_PAGE);

    *begin = (_KOS_PAGE **)ptr;

    if (end)
        *end = (_KOS_PAGE **)(ptr + _KOS_BITMAP_SIZE + (num_slots << _KOS_OBJ_ALIGN_BITS));
}

static void _get_flat_page_list(struct _KOS_HEAP *heap,
                                _KOS_PAGE       **list,
                                _KOS_PAGE       **pages)
{
    _KOS_PAGE  *page  = *pages;
    _KOS_PAGE  *first = *pages;
    _KOS_PAGE **dest;
    _KOS_PAGE **begin;
    _KOS_PAGE **end;

    assert(page);

    _get_flat_list(first, &begin, &end);

    dest = begin;

    while (page && dest < end) {

        _KOS_PAGE *next = page->next;
        unsigned   size;
        unsigned   num_pages;

        assert(((uintptr_t)page & (_KOS_PAGE_SIZE - 1U)) == 0U);

        if (page->num_slots <= _KOS_SLOTS_PER_PAGE) {
            *(dest++) = page;
            page = next;
            continue;
        }

        size = (unsigned)(_KOS_SLOTS_OFFS + (page->num_slots << _KOS_OBJ_ALIGN_BITS));

        num_pages = ((size - 1U) >> _KOS_PAGE_BITS) + 1U;

        if ((intptr_t)num_pages > end - dest)
            break;

        while (size) {

            const unsigned this_size = KOS_min(size, _KOS_PAGE_SIZE);

            if (this_size >= (_KOS_SLOTS_OFFS + (_KOS_PAGE_SIZE >> 3))) {

                page->num_slots = (this_size - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;
                assert(page->num_slots <= _KOS_SLOTS_PER_PAGE);

                *(dest++) = page;

                page = (_KOS_PAGE *)((uintptr_t)page + this_size);
            }
            else
                _register_wasted_region(heap, (void *)page, this_size);

            size -= this_size;
        }

        page = next;
    }

    KOS_atomic_write_u32(first->num_allocated, (unsigned)(dest - begin));

    first->next = *list;
    *list       = first;

    *pages = page;
}

static int _sort_compare(const void *a, const void *b)
{
    _KOS_PAGE *const pa = *(_KOS_PAGE *const *)a;
    _KOS_PAGE *const pb = *(_KOS_PAGE *const *)b;

    return (pa < pb) ? -1 : (pa > pb) ?  1 : 0;
}

static void _sort_flat_page_list(_KOS_PAGE *list)
{
    _KOS_PAGE **begin;

    _get_flat_list(list, &begin, 0);

    qsort(begin, KOS_atomic_read_u32(list->num_allocated), sizeof(void *), _sort_compare);
}

static unsigned _push_sorted_list(struct _KOS_HEAP *heap, _KOS_PAGE *list)
{
    _KOS_PAGE **begin;
    _KOS_PAGE **end;
    _KOS_PAGE **insert_at;
    _KOS_PAGE  *page_at;
    unsigned    num_pages = 0;

    _get_flat_list(list, &begin, 0);

    end = begin + KOS_atomic_read_u32(list->num_allocated);

    insert_at = &heap->free_pages;
    page_at   = *insert_at;

    while (begin < end) {

        _KOS_PAGE *page = *(begin++);

        KOS_atomic_write_u32(page->num_allocated, 0);

#ifndef NDEBUG
        if (page != list)
            memset(((uint8_t *)page) + _KOS_BITMAP_OFFS,
                   0xDDU,
                   (_KOS_SLOTS_OFFS - _KOS_BITMAP_OFFS) + (page->num_slots << _KOS_OBJ_ALIGN_BITS));
#endif
        while (page_at && page > page_at) {

            insert_at = &page_at->next;
            page_at   = *insert_at;
        }

        page->next = page_at;
        *insert_at = page;
        insert_at  = &page->next;

        assert(begin + 1 >= end || *(begin + 1) > page);

        ++num_pages;
    }

#ifndef NDEBUG
    memset(((uint8_t *)list) + _KOS_BITMAP_OFFS,
           0xDDU,
           (_KOS_SLOTS_OFFS - _KOS_BITMAP_OFFS) + (list->num_slots << _KOS_OBJ_ALIGN_BITS));
#endif

    return num_pages;
}

static void _reclaim_free_pages(struct _KOS_HEAP     *heap,
                                _KOS_PAGE            *free_pages,
                                struct _KOS_GC_STATS *stats)
{
    _KOS_PAGE *lists = 0;

    if ( ! free_pages)
        return;

    do
        _get_flat_page_list(heap, &lists, &free_pages);
    while (free_pages);

    free_pages = lists;

    while (free_pages) {
        _sort_flat_page_list(free_pages);
        free_pages = free_pages->next;
    }

    while (lists) {

        _KOS_PAGE *next      = lists->next;
        unsigned   num_pages = _push_sorted_list(heap, lists);

        if (stats)
            stats->num_pages_freed += num_pages;

        lists = next;
    }
}

static int _evacuate_object(KOS_CONTEXT     ctx,
                            KOS_OBJ_HEADER *hdr,
                            uint32_t        size)
{
    int             error   = KOS_SUCCESS;
    KOS_OBJ_HEADER *new_obj;

    assert(size <= (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS));

    new_obj = (KOS_OBJ_HEADER *)_alloc_object(ctx,
                                              (enum KOS_OBJECT_TYPE)hdr->type,
                                              size);

    if (new_obj) {
        memcpy(new_obj, hdr, size);

        hdr->alloc_size = (KOS_OBJ_ID)((intptr_t)new_obj + 1);
    }
    else
        error = KOS_ERROR_EXCEPTION;

    return error;
}

static void _update_child_ptr(KOS_OBJ_ID *obj_id_ptr)
{
    KOS_OBJ_ID obj_id = *obj_id_ptr;

    if (IS_HEAP_OBJECT(obj_id) && ! IS_BAD_PTR(obj_id)) {

        /* TODO use relaxed atomic loads/stores ??? */

        KOS_OBJ_ID new_obj = ((KOS_OBJ_HEADER *)((intptr_t)obj_id - 1))->alloc_size;

        /* Objects in pages retained keep their size in their size field */
        if (IS_HEAP_OBJECT(new_obj)) {
            *obj_id_ptr = new_obj;
            assert(READ_OBJ_TYPE(obj_id) == READ_OBJ_TYPE(new_obj));
        }
    }
}

static void _update_child_ptrs(KOS_OBJ_HEADER *hdr)
{
    switch (hdr->type) {

        case OBJ_INTEGER:
            /* fall through */
        case OBJ_FLOAT:
            /* fall through */
        case OBJ_OPAQUE:
            /* fall through */
        case OBJ_BUFFER_STORAGE:
            break;

        case OBJ_STRING:
            if (((KOS_STRING *)hdr)->header.flags & KOS_STRING_REF)
                _update_child_ptr(&((KOS_STRING *)hdr)->ref.obj_id);
            break;

        default:
            assert(hdr->type == OBJ_OBJECT);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->props);
            _update_child_ptr(&((KOS_OBJECT *)hdr)->prototype);
            break;

        case OBJ_ARRAY:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_ARRAY *)hdr)->data);
            break;

        case OBJ_BUFFER:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_BUFFER *)hdr)->data);
            break;

        case OBJ_OBJECT_STORAGE:
            assert(IS_BAD_PTR(KOS_atomic_read_obj(((KOS_OBJECT_STORAGE *)hdr)->new_prop_table)));
            {
                KOS_PITEM *item = &((KOS_OBJECT_STORAGE *)hdr)->items[0];
                KOS_PITEM *end  = item + ((KOS_OBJECT_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item) {
                    _update_child_ptr((KOS_OBJ_ID *)&item->key);
                    _update_child_ptr((KOS_OBJ_ID *)&item->value);
                }
            }
            break;

        case OBJ_ARRAY_STORAGE:
            assert(IS_BAD_PTR(KOS_atomic_read_obj(((KOS_ARRAY_STORAGE *)hdr)->next)));
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_ARRAY_STORAGE *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_ARRAY_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item)
                    _update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_FUNCTION:
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->closures);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->defaults);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->generator_stack_frame);
            break;

        case OBJ_CLASS:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->prototype);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->props);
            _update_child_ptr(&((KOS_CLASS *)hdr)->closures);
            _update_child_ptr(&((KOS_CLASS *)hdr)->defaults);
            break;

        case OBJ_DYNAMIC_PROP:
            _update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->getter);
            _update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->setter);
            break;

        case OBJ_OBJECT_WALK:
            _update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->obj);
            _update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->key_table);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_WALK *)hdr)->last_key);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_WALK *)hdr)->last_value);
            break;

        case OBJ_MODULE:
            _update_child_ptr(&((KOS_MODULE *)hdr)->name);
            _update_child_ptr(&((KOS_MODULE *)hdr)->path);
            _update_child_ptr(&((KOS_MODULE *)hdr)->constants);
            _update_child_ptr(&((KOS_MODULE *)hdr)->global_names);
            _update_child_ptr(&((KOS_MODULE *)hdr)->globals);
            _update_child_ptr(&((KOS_MODULE *)hdr)->module_names);
            break;

        case OBJ_STACK:
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_STACK *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_STACK *)hdr)->size;
                for ( ; item < end; ++item)
                    _update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_LOCAL_REFS:
            {
                KOS_OBJ_ID **ref = &((KOS_LOCAL_REFS *)hdr)->refs[0];
                KOS_OBJ_ID **end = ref + ((KOS_LOCAL_REFS *)hdr)->header.num_tracked;

                _update_child_ptr(&((KOS_LOCAL_REFS *)hdr)->next);

                for ( ; ref < end; ++ref)
                    _update_child_ptr(*ref);
            }
            break;
    }
}

static void _update_after_evacuation(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst             = ctx->inst;
    struct _KOS_HEAP   *heap             = &inst->heap;
    _KOS_PAGE          *page             = heap->full_pages;
    int                 non_full_checked = 0;

    if ( ! page) {
        page = heap->non_full_pages;
        non_full_checked = 1;
    }

    /* TODO add way to go over all pages */
    while (page) {

        uint8_t       *ptr = (uint8_t *)page + _KOS_SLOTS_OFFS;
        uint8_t *const end = ptr + (_get_num_active_slots(page) << _KOS_OBJ_ALIGN_BITS);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size = (uint32_t)GET_SMALL_INT(hdr->alloc_size);

            _update_child_ptrs(hdr);

            ptr += size;
        }

        page = page->next;

        if ( ! page && ! non_full_checked) {
            page = heap->non_full_pages;
            non_full_checked = 1;
        }
    }

    /* Update object pointers in instance */

    {
        int i;

        for (i = 0; i < KOS_STR_NUM; ++i)
            _update_child_ptr(&inst->common_strings[i]);
    }

    _update_child_ptr(&inst->prototypes.object_proto);
    _update_child_ptr(&inst->prototypes.number_proto);
    _update_child_ptr(&inst->prototypes.integer_proto);
    _update_child_ptr(&inst->prototypes.float_proto);
    _update_child_ptr(&inst->prototypes.string_proto);
    _update_child_ptr(&inst->prototypes.boolean_proto);
    _update_child_ptr(&inst->prototypes.array_proto);
    _update_child_ptr(&inst->prototypes.buffer_proto);
    _update_child_ptr(&inst->prototypes.function_proto);
    _update_child_ptr(&inst->prototypes.class_proto);
    _update_child_ptr(&inst->prototypes.generator_proto);
    _update_child_ptr(&inst->prototypes.exception_proto);
    _update_child_ptr(&inst->prototypes.generator_end_proto);
    _update_child_ptr(&inst->prototypes.thread_proto);

    _update_child_ptr(&inst->modules.init_module);
    _update_child_ptr(&inst->modules.search_paths);
    _update_child_ptr(&inst->modules.module_names);
    _update_child_ptr(&inst->modules.modules);
    _update_child_ptr(&inst->modules.module_inits);

    _update_child_ptr(&inst->args);

    /* Update object pointers in thread contexts */

    {
        ctx = &inst->threads.main_thread;

        _KOS_lock_mutex(&inst->threads.mutex);

        while (ctx) {

            uint32_t i;

            _update_child_ptr(&ctx->exception);
            _update_child_ptr(&ctx->retval);
            _update_child_ptr(&ctx->stack);
            _update_child_ptr(&ctx->local_refs);

            for (i = 0; i < ctx->tmp_ref_count; ++i) {
                KOS_OBJ_ID *const obj_id_ptr = ctx->tmp_refs[i];
                if (obj_id_ptr)
                    _update_child_ptr(obj_id_ptr);
            }

            ctx = ctx->next;
        }

        _KOS_unlock_mutex(&inst->threads.mutex);
    }
}

static int _evacuate(KOS_CONTEXT           ctx,
                     _KOS_PAGE           **free_pages,
                     struct _KOS_GC_STATS *out_stats)
{
    struct _KOS_HEAP *heap           = _get_heap(ctx);
    int               error          = KOS_SUCCESS;
    _KOS_PAGE        *page           = heap->full_pages;
    _KOS_PAGE        *non_full_pages = heap->non_full_pages;
    _KOS_PAGE        *next;
    KOS_OBJ_ID        exception      = KOS_get_exception(ctx);
    int               non_full_turn  = 0;

    struct _KOS_GC_STATS stats = { 0, 0, 0, 0, 0, 0, 0, 0 };

    KOS_clear_exception(ctx);

    heap->full_pages     = 0;
    heap->non_full_pages = 0;

    if ( ! page) {
        non_full_turn = 1;
        page = non_full_pages;
        non_full_pages = 0;
    }

    for ( ; page; page = next) {

        struct _KOS_MARK_LOC mark_loc = { 0, 0 };

        _KOS_SLOT     *ptr            = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS);
        _KOS_SLOT     *end            = ptr + _get_num_active_slots(page);
#ifndef NDEBUG
        _KOS_SLOT     *page_end       = ptr + KOS_atomic_read_u32(page->num_allocated);
#endif
        const uint32_t num_slots_used = KOS_atomic_read_u32(page->num_used);

        heap->used_size -= non_full_turn ? _non_full_page_size(page) : _full_page_size(page);

        next = page->next;

        if ( ! next && non_full_pages) {
            next = non_full_pages;
            non_full_pages = 0;
            non_full_turn = 1;
        }

        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        if (num_slots_used >= (_KOS_SLOTS_PER_PAGE * _KOS_MIGRATION_THRESH) / 100U) {
            PUSH_LIST(heap->full_pages, page);
            heap->used_size += _full_page_size(page);
            ++stats.num_pages_kept;
            stats.size_kept += num_slots_used << _KOS_OBJ_ALIGN_BITS;
            continue;
        }

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + _KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t  color = _get_marking(&mark_loc);
            int             evac  = 0;

            assert(size > 0U);
            assert(color != GRAY);
            assert(size <= (uint8_t *)page_end - (uint8_t *)ptr);

            if (color) {
                if (_evacuate_object(ctx, hdr, size)) {

                    KOS_clear_exception(ctx);

                    _release_current_page_locked(ctx);

                    _update_after_evacuation(ctx);

                    /* TODO this will not work, pages can't be reused until
                     *      the end of GC, because evacuated object refs
                     *      must be updated in other objects. */
                    _reclaim_free_pages(heap, *free_pages, &stats);
                    *free_pages = 0;

                    error = _evacuate_object(ctx, hdr, size);

                    if (error) {
                        uint8_t       *begin     = (uint8_t *)page + _KOS_SLOTS_OFFS;
                        const uint32_t evac_size = (uint32_t)((uint8_t *)ptr - begin);

                        hdr = (KOS_OBJ_HEADER *)begin;
                        hdr->alloc_size = TO_SMALL_INT(evac_size);

                        /* TODO page failed mid-evacuation, alloc_sizes are corrupted */

                        do {
                            next = page->next;
                            PUSH_LIST(heap->full_pages, page);
                            heap->used_size += _full_page_size(page);
                            page = next;
                        } while (page);

                        goto _error;
                    }

                }
                ++stats.num_objs_evacuated;
                stats.size_evacuated += size;
                evac = 1;
            }
            else if (hdr->type == OBJ_OBJECT) {

                KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

                if (obj->finalize) {

                    obj->finalize(ctx, KOS_atomic_read_ptr(obj->priv));

                    ++stats.num_objs_finalized;
                }
            }

            if ( ! evac) {
                ++stats.num_objs_freed;
                stats.size_freed += size;
            }

            _advance_marking(&mark_loc, size >> _KOS_OBJ_ALIGN_BITS);

            ptr = (_KOS_SLOT *)((uint8_t *)ptr + size);
        }

        PUSH_LIST(*free_pages, page);
    }

_error:
    if (out_stats)
        *out_stats = stats;

    _release_current_page_locked(ctx);

    if ( ! IS_BAD_PTR(exception))
        ctx->exception = exception;

    return error;
}

static void _update_gc_threshold(struct _KOS_HEAP *heap)
{
    heap->gc_threshold = heap->used_size + _KOS_GC_STEP;
}

static int _help_gc(KOS_CONTEXT ctx)
{
    struct _KOS_HEAP *heap = _get_heap(ctx);

    while (KOS_atomic_read_u32(heap->gc_state) != GC_INACTIVE)
        /* TODO actually help garbage collector */
        _KOS_yield();

    return KOS_SUCCESS;
}

int KOS_collect_garbage(KOS_CONTEXT           ctx,
                        struct _KOS_GC_STATS *stats)
{
    int               error      = KOS_SUCCESS;
    struct _KOS_HEAP *heap       = _get_heap(ctx);
    _KOS_PAGE        *free_pages = 0;

    if ( ! KOS_atomic_cas_u32(heap->gc_state, GC_INACTIVE, GC_INIT))
        return _help_gc(ctx);

    _release_current_page(ctx);

    _clear_marking(heap);

    _mark_roots(ctx);

    _stop_the_world(ctx); /* Remaining threads enter _help_gc() */

    /* TODO mark frames in remaining threads */

    while (_gray_to_black(heap));

    error = _evacuate(ctx, &free_pages, stats);

    _update_after_evacuation(ctx);

    _reclaim_free_pages(heap, free_pages, stats);

    _update_gc_threshold(heap);

    KOS_atomic_release_barrier();

    KOS_atomic_write_u32(heap->gc_state, GC_INACTIVE);

    if ( ! error && KOS_is_exception_pending(ctx))
        error = KOS_ERROR_EXCEPTION;

    return error;
}

void _KOS_track_refs(KOS_CONTEXT ctx, int num_entries, ...)
{
    va_list  args;
    uint32_t i;
    uint32_t end;

    assert(num_entries > 0);
    assert((size_t)(ctx->tmp_ref_count + num_entries) <=
           sizeof(ctx->tmp_refs) / sizeof(ctx->tmp_refs[0]));

    i   = ctx->tmp_ref_count;
    end = i + num_entries;

    va_start(args, num_entries);

    do {
        ctx->tmp_refs[i] = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
        ++i;
    } while (i < end);

    ctx->tmp_ref_count = end;

    va_end(args);
}

void _KOS_untrack_refs(KOS_CONTEXT ctx, int num_entries)
{
    assert(num_entries > 0 && (unsigned)num_entries <= ctx->tmp_ref_count);

    ctx->tmp_ref_count -= num_entries;
}

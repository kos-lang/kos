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
#include "../inc/kos_atomic.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system.h"
#include "kos_threads_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum GC_STATE_E {
    GC_INACTIVE,
    GC_LOCKED,
    GC_INIT
};

struct KOS_POOL_HEADER_S {
    KOS_POOL *next;        /* Pointer to next pool header                    */
    void     *memory;      /* Pointer to the allocated memory                */
    void     *usable_ptr;  /* Pointer to usable region of memory in the pool */
    uint32_t  alloc_size;  /* Number of allocated bytes                      */
    uint32_t  usable_size; /* Size of the usable region                      */
};

struct KOS_WASTE_HEADER_S {
    KOS_WASTE *next;
    uint32_t   size;
};

typedef struct KOS_SLOT_PLACEHOLDER_S {
    uint8_t _dummy[1 << KOS_OBJ_ALIGN_BITS];
} KOS_SLOT;

struct KOS_PAGE_HEADER_S {
    KOS_PAGE            *next;
    uint32_t             num_slots;       /* Total number of slots in this page */
    KOS_ATOMIC(uint32_t) num_allocated;   /* Number of slots allocated          */
    KOS_ATOMIC(uint32_t) num_used;        /* Number of slots used, only for GC  */
};

#define KOS_PAGE_HDR_SIZE  (sizeof(KOS_PAGE))
#define KOS_SLOTS_PER_PAGE (((KOS_PAGE_SIZE - KOS_PAGE_HDR_SIZE) << 2) / \
                            ((1U << (KOS_OBJ_ALIGN_BITS + 2)) + 1U))
#define KOS_BITMAP_SIZE    (((KOS_SLOTS_PER_PAGE + 15U) & ~15U) >> 2)
#define KOS_BITMAP_OFFS    ((KOS_PAGE_HDR_SIZE + 3U) & ~3U)
#define KOS_SLOTS_OFFS     (KOS_PAGE_SIZE - (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))

#ifdef CONFIG_MAD_GC
#define KOS_MAX_LOCKED_PAGES 128

struct KOS_LOCKED_PAGE_S {
    KOS_PAGE *page;
    uint32_t  num_slots;
};

struct KOS_LOCKED_PAGES_S {
    struct KOS_LOCKED_PAGES_S *next;
    uint32_t                   num_pages;
    struct KOS_LOCKED_PAGE_S   pages[KOS_MAX_LOCKED_PAGES];
};
#endif

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

#ifdef __cplusplus
static inline KOS_HEAP *_get_heap(KOS_CONTEXT ctx)
{
    return &ctx->inst->heap;
}
#else
#define _get_heap(ctx) (&(ctx)->inst->heap)
#endif

int kos_heap_init(KOS_INSTANCE *inst)
{
    KOS_HEAP *heap = &inst->heap;

    KOS_atomic_write_u32(heap->gc_state, 0);
    heap->heap_size      = 0;
    heap->used_size      = 0;
    heap->gc_threshold   = KOS_GC_STEP;
    heap->free_pages     = 0;
    heap->non_full_pages = 0;
    heap->full_pages     = 0;
    heap->pools          = 0;
    heap->pool_headers   = 0;
    heap->waste          = 0;

#ifdef CONFIG_MAD_GC
    heap->locked_pages_first = 0;
    heap->locked_pages_last  = 0;
#endif

    assert(KOS_BITMAP_OFFS + KOS_BITMAP_SIZE <= KOS_SLOTS_OFFS);
    assert( ! (KOS_SLOTS_OFFS & 7U));
    assert(KOS_SLOTS_OFFS + (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) == KOS_PAGE_SIZE);

    return kos_create_mutex(&heap->mutex);
}

static uint32_t get_num_active_slots(KOS_PAGE *page)
{
    /* For a huge object, only match the beginning of the page */
    return (page->num_slots > KOS_SLOTS_PER_PAGE)
           ? 1U
           : KOS_atomic_read_u32(page->num_allocated);
}

static void finalize_objects(KOS_CONTEXT ctx,
                             KOS_HEAP   *heap)
{
    KOS_PAGE *page           = heap->full_pages;
    KOS_PAGE *non_full_pages = heap->non_full_pages;
    KOS_PAGE *next;

    heap->full_pages     = 0;
    heap->non_full_pages = 0;

    if ( ! page) {
        page = non_full_pages;
        non_full_pages = 0;
    }

    for ( ; page; page = next) {

        KOS_SLOT *ptr      = (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS);
        KOS_SLOT *end      = ptr + get_num_active_slots(page);
#ifndef NDEBUG
        KOS_SLOT *page_end = ptr + KOS_atomic_read_u32(page->num_allocated);
#endif

        next = page->next;

        if ( ! next && non_full_pages) {
            next = non_full_pages;
            non_full_pages = 0;
        }

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size = (uint32_t)GET_SMALL_INT(hdr->alloc_size);

            assert(size > 0U);
            assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

            if (hdr->type == OBJ_OBJECT) {

                KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

                if (obj->finalize)
                    obj->finalize(ctx, KOS_atomic_read_obj(obj->priv));
            }

            ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
        }
    }
}

void kos_heap_destroy(KOS_INSTANCE *inst)
{
    assert(inst->threads.main_thread.prev == 0);
    assert(inst->threads.main_thread.next == 0);

    kos_heap_release_thread_page(&inst->threads.main_thread);

#ifdef CONFIG_MAD_GC
    {
        struct KOS_LOCKED_PAGES_S *locked_pages = inst->heap.locked_pages_first;

        while (locked_pages) {

            uint32_t                   i;
            struct KOS_LOCKED_PAGES_S *del;

            for (i = 0 ; i < locked_pages->num_pages; i++) {

                KOS_PAGE *page      = locked_pages->pages[i].page;
                uint32_t  num_slots = locked_pages->pages[i].num_slots;

                if (num_slots == KOS_SLOTS_PER_PAGE)
                    kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE);
            }

            del = locked_pages;
            locked_pages = locked_pages->next;
            kos_free(del);
        }

        inst->heap.locked_pages_first = 0;
        inst->heap.locked_pages_last  = 0;
    }
#endif

    finalize_objects(&inst->threads.main_thread, &inst->heap);

    for (;;) {
        KOS_POOL *pool = (KOS_POOL *)POP_LIST(inst->heap.pools);
        void     *memory;

        if ( ! pool)
            break;

        memory = pool->memory;
        kos_free(memory);

        if (pool != memory)
            kos_free(pool);
    }

    kos_destroy_mutex(&inst->heap.mutex);
}

static void _register_wasted_region(KOS_HEAP *heap,
                                    void     *ptr,
                                    uint32_t  size)
{
    if (size >= sizeof(KOS_WASTE)) {

        KOS_WASTE *waste = (KOS_WASTE *)ptr;

        waste->size = size;

        PUSH_LIST(heap->waste, waste);
    }

    heap->used_size += size;
}

static KOS_POOL *_alloc_pool(KOS_HEAP *heap,
                             uint32_t  alloc_size)
{
    KOS_POOL *pool_hdr;
    uint8_t  *pool;
    uint8_t  *begin;
    uint32_t  waste_at_front;

    if (heap->heap_size + alloc_size > KOS_MAX_HEAP_SIZE)
        return 0;

    pool = (uint8_t *)kos_malloc(alloc_size);

    if ( ! pool)
        return 0;

    heap->heap_size += alloc_size;

    begin = (uint8_t *)KOS_align_up((uintptr_t)pool, (uintptr_t)KOS_PAGE_SIZE);

    waste_at_front = (uint32_t)(begin - pool);

    if (waste_at_front < sizeof(KOS_POOL)) {
        pool_hdr = (KOS_POOL *)kos_malloc(sizeof(KOS_POOL));

        if ( ! pool_hdr) {
            kos_free(pool);
            return 0;
        }
    }
    else {

        uint8_t *waste;
        uint32_t waste_size;

        assert(waste_at_front >= sizeof(KOS_POOL));

        pool_hdr   = (KOS_POOL *)pool;
        waste      = pool + sizeof(KOS_POOL);
        waste_size = waste_at_front - (uint32_t)sizeof(KOS_POOL);

        _register_wasted_region(heap, waste, waste_size);
    }

    pool_hdr->memory      = pool;
    pool_hdr->alloc_size  = alloc_size;
    pool_hdr->usable_ptr  = begin;
    pool_hdr->usable_size = (uint32_t)((pool + alloc_size) - begin);

    PUSH_LIST(heap->pools, pool_hdr);

    return pool_hdr;
}

static int _alloc_page_pool(KOS_HEAP *heap)
{
    KOS_POOL *pool_hdr  = _alloc_pool(heap, KOS_POOL_SIZE);
    KOS_PAGE *next_page = 0;
    uint8_t  *begin;
    uint8_t  *usable_end;
    uint8_t  *page_bytes;
    uint32_t  page_size;

    if ( ! pool_hdr)
        return KOS_ERROR_OUT_OF_MEMORY;

    begin      = (uint8_t *)pool_hdr->usable_ptr;
    usable_end = begin + pool_hdr->usable_size;
    page_bytes = (uint8_t *)((uintptr_t)usable_end & ~(uintptr_t)(KOS_PAGE_SIZE - 1U));
    page_size  = (uint32_t)(usable_end - page_bytes);

    if (page_size > KOS_SLOTS_OFFS + (KOS_PAGE_SIZE >> 3))
        page_bytes += page_size;
    else {

        uint32_t waste_size = (uint32_t)(usable_end - page_bytes);

        pool_hdr->usable_size -= waste_size;

        _register_wasted_region(heap, page_bytes, waste_size);

        page_size = KOS_PAGE_SIZE;
    }

    assert(heap->free_pages == 0);

    while (page_bytes > begin) {

        KOS_PAGE *page;

        page_bytes -= page_size;
        assert( ! ((uintptr_t)page_bytes & (uintptr_t)(KOS_PAGE_SIZE - 1)));

        page = (KOS_PAGE *)page_bytes;

        page->num_slots = (page_size - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

        KOS_atomic_write_u32(page->num_allocated, 0);

        page->next = next_page;

        KOS_PERF_CNT(alloc_new_page);

        page_size = KOS_PAGE_SIZE;

        next_page = page;
    }

    heap->free_pages = next_page;

#ifndef NDEBUG
    {
        KOS_PAGE *page      = heap->free_pages;
        uint32_t  num_pages = 0;

        next_page = page;

        while (page) {

            page_size = page->num_slots == KOS_SLOTS_PER_PAGE
                      ? KOS_PAGE_SIZE
                      : KOS_SLOTS_OFFS + (page->num_slots << KOS_OBJ_ALIGN_BITS);

            assert(page->num_slots <= KOS_SLOTS_PER_PAGE);
            assert(page->num_slots >= (KOS_PAGE_SIZE >> (3 + KOS_OBJ_ALIGN_BITS)));
            assert(KOS_atomic_read_u32(page->num_allocated) == 0);

            assert(page == next_page);

            assert((uintptr_t)page >= (uintptr_t)pool_hdr->usable_ptr);

            assert((uintptr_t)page + page_size <=
                   (uintptr_t)pool_hdr->usable_ptr + pool_hdr->usable_size);

            next_page = (KOS_PAGE *)((uint8_t *)page + page_size);

            page = page->next;

            ++num_pages;
        }

        assert(num_pages == KOS_POOL_SIZE / KOS_PAGE_SIZE ||
               num_pages == (KOS_POOL_SIZE / KOS_PAGE_SIZE) - 1U);
    }
#endif

    return KOS_SUCCESS;
}

static KOS_PAGE *_alloc_page(KOS_HEAP *heap)
{
    KOS_PAGE *page = heap->free_pages;

    if ( ! page) {

        if (_alloc_page_pool(heap))
            return 0;

        page = heap->free_pages;

        assert(page);
    }

    assert(page->num_slots <= KOS_SLOTS_PER_PAGE);
    assert(KOS_atomic_read_u32(page->num_allocated) == 0);
    assert(page->next == 0 || page < page->next);

    heap->free_pages = page->next;

    KOS_PERF_CNT(alloc_free_page);

    return page;
}

#ifndef NDEBUG
int kos_heap_lend_page(KOS_CONTEXT ctx,
                       void       *buffer,
                       size_t      size)
{
    const uintptr_t buf_ptr      = (uintptr_t)buffer;
    const uintptr_t good_buf_ptr = KOS_align_up(buf_ptr, (uintptr_t)KOS_PAGE_SIZE);
    const uintptr_t reserved     = good_buf_ptr - buf_ptr + KOS_SLOTS_OFFS
                                   + (1U << KOS_OBJ_ALIGN_BITS);
    KOS_HEAP       *heap         = _get_heap(ctx);
    int             lent         = 0;

    kos_lock_mutex(&heap->mutex);

    if (reserved <= size) {

        KOS_PAGE  *page      = (KOS_PAGE *)good_buf_ptr;
        KOS_PAGE **insert_at = &heap->free_pages;

        page->num_slots = (uint32_t)(size - reserved) >> KOS_OBJ_ALIGN_BITS;
        KOS_atomic_write_u32(page->num_allocated, 0);

        while (*insert_at && page > *insert_at)
            insert_at = &(*insert_at)->next;

        page->next = *insert_at;
        *insert_at = page;

        lent = 1;
    }

    kos_unlock_mutex(&heap->mutex);

    return lent;
}
#endif

static int is_recursive_collection(KOS_HEAP *heap)
{
    /* TODO improve this, check on different thread */
    return KOS_atomic_read_u32(heap->gc_state) != GC_INACTIVE;
}

static void _try_collect_garbage(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = _get_heap(ctx);

    /* Don't try to collect garbage when the garbage collector is running */
    if (is_recursive_collection(heap))
        return;

#ifdef CONFIG_MAD_GC
    if ( ! (ctx->inst->flags & KOS_INST_MANUAL_GC))
#else
    if (heap->used_size > heap->gc_threshold &&
        ! (ctx->inst->flags & KOS_INST_MANUAL_GC))
#endif
    {

        kos_unlock_mutex(&heap->mutex);

        KOS_collect_garbage(ctx, 0);

        kos_lock_mutex(&heap->mutex);
    }
}

static void *_alloc_slots_from_page(KOS_PAGE *page, uint32_t num_slots)
{
    const uint32_t total_slots   = page->num_slots;
    const uint32_t num_allocated = KOS_atomic_read_u32(page->num_allocated);
    uint32_t       new_num_slots = num_allocated + num_slots;
    KOS_SLOT      *slot          = 0;

    assert(num_slots > 0);

    if (new_num_slots <= total_slots) {

        slot = (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS) + num_allocated;

        assert(slot == (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS + (num_allocated << KOS_OBJ_ALIGN_BITS)));

        KOS_atomic_write_u32(page->num_allocated, new_num_slots);
    }

    return slot;
}

static KOS_OBJ_HEADER *_alloc_object_from_page(KOS_PAGE *page,
                                               KOS_TYPE  object_type,
                                               uint32_t  num_slots)
{
    KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)_alloc_slots_from_page(page, num_slots);

    if (hdr) {
        hdr->alloc_size = TO_SMALL_INT(num_slots << KOS_OBJ_ALIGN_BITS);
        hdr->type       = (uint8_t)object_type;

        KOS_PERF_CNT(alloc_object);
    }

    return hdr;
}

void *kos_heap_early_alloc(KOS_INSTANCE *inst,
                           KOS_CONTEXT   ctx,
                           KOS_TYPE      object_type,
                           uint32_t      size)
{
    const uint32_t num_slots = (size + sizeof(KOS_SLOT) - 1) >> KOS_OBJ_ALIGN_BITS;

    if ( ! ctx->cur_page) {

        kos_lock_mutex(&inst->heap.mutex);

        ctx->cur_page = _alloc_page(&inst->heap);

        kos_unlock_mutex(&inst->heap.mutex);

        if ( ! ctx->cur_page)
            return 0;
    }

    return _alloc_object_from_page(ctx->cur_page, object_type, num_slots);
}

static uint32_t _full_page_size(KOS_PAGE *page)
{
    return KOS_SLOTS_OFFS + (page->num_slots << KOS_OBJ_ALIGN_BITS);
}

static uint32_t _non_full_page_size(KOS_PAGE *page)
{
    return KOS_SLOTS_OFFS + (KOS_atomic_read_u32(page->num_allocated) << KOS_OBJ_ALIGN_BITS);
}

void kos_heap_release_thread_page(KOS_CONTEXT ctx)
{
    if (ctx->cur_page) {

        KOS_HEAP *const heap = &ctx->inst->heap;

        kos_lock_mutex(&heap->mutex);

        PUSH_LIST(heap->non_full_pages, ctx->cur_page);

        heap->used_size += _non_full_page_size(ctx->cur_page);

        kos_unlock_mutex(&heap->mutex);

        ctx->cur_page = 0;
    }
}

static KOS_OBJ_HEADER *_setup_huge_object_in_page(KOS_HEAP *heap,
                                                  KOS_PAGE *page,
                                                  KOS_TYPE  object_type,
                                                  uint32_t  size)
{
    KOS_OBJ_HEADER *hdr = (KOS_OBJ_HEADER *)((uint8_t *)page + KOS_SLOTS_OFFS);

    assert( ! ((uintptr_t)hdr & 7U));

    hdr->alloc_size = TO_SMALL_INT(KOS_align_up(size, 1U << KOS_OBJ_ALIGN_BITS));
    hdr->type       = (uint8_t)object_type;

    KOS_PERF_CNT(alloc_huge_object);

    PUSH_LIST(heap->full_pages, page);

    assert(page->num_slots > KOS_SLOTS_PER_PAGE);

    heap->used_size += _full_page_size(page);

    KOS_atomic_write_u32(page->num_allocated, page->num_slots);

    return hdr;
}

static void *_alloc_huge_object(KOS_CONTEXT ctx,
                                KOS_TYPE    object_type,
                                uint32_t    size)
{
    KOS_HEAP       *heap = _get_heap(ctx);
    KOS_OBJ_HEADER *hdr  = 0;
    KOS_PAGE      **page_ptr;
    KOS_PAGE       *page;

    kos_lock_mutex(&heap->mutex);

    _try_collect_garbage(ctx);

    page_ptr = &heap->free_pages;
    page     = *page_ptr;

    while (page) {

        KOS_PAGE **next_ptr  = page_ptr;
        KOS_PAGE  *next_page = page;
        uint32_t   accum     = 0;

        do {

            if (page != next_page)
                break;

            next_ptr = &page->next;

            if (page->num_slots < KOS_SLOTS_PER_PAGE) {
                page = *next_ptr;
                break;
            }

            accum += KOS_PAGE_SIZE;

            if (accum >= size + KOS_SLOTS_OFFS) {

                const uint32_t num_slots = (accum - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

                page      = *page_ptr;
                next_page = *next_ptr;

#ifndef NDEBUG
                {
                    KOS_PAGE *test_page = page;
                    KOS_PAGE *expected  = page;
                    uint32_t  num_pages = 0;

                    while (test_page != next_page) {

                        assert(test_page == expected);

                        expected = (KOS_PAGE *)((uint8_t *)test_page + KOS_PAGE_SIZE);

                        assert(test_page >= page);
                        assert( ! next_page || test_page < next_page);

                        assert(test_page->num_slots == KOS_SLOTS_PER_PAGE);
                        assert(KOS_atomic_read_u32(test_page->num_allocated) == 0);

                        ++num_pages;

                        test_page = test_page->next;
                    }

                    assert(num_pages * KOS_PAGE_SIZE == accum);
                }
#endif
                *page_ptr = next_page;

                page->num_slots = num_slots;

                hdr = _setup_huge_object_in_page(heap, page, object_type, size);

                assert((uint8_t *)hdr + size <= (uint8_t *)page + accum);

                page = 0;
                break;
            }

            next_page = (KOS_PAGE *)((uintptr_t)page + KOS_PAGE_SIZE);
            page      = *next_ptr;

        } while (page);

        page_ptr = next_ptr;
    }

    if ( ! hdr) {

        KOS_POOL *pool = _alloc_pool(heap, size + KOS_SLOTS_OFFS + KOS_PAGE_SIZE);

        if (pool) {
            page = (KOS_PAGE *)pool->usable_ptr;

            /* TODO register tail as wasted */

            page->num_slots = (pool->usable_size - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

            assert((page->num_slots << KOS_OBJ_ALIGN_BITS) >= size);

            hdr = _setup_huge_object_in_page(heap,
                                             page,
                                             object_type,
                                             size);

            assert((uint8_t *)hdr + size <= (uint8_t *)pool->usable_ptr + pool->usable_size);
        }
        else
            KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
    }

    kos_unlock_mutex(&heap->mutex);

    return (void *)hdr;
}

static int _is_page_full(KOS_PAGE *page)
{
    return KOS_atomic_read_u32(page->num_allocated) == page->num_slots;
}

static void *_alloc_object(KOS_CONTEXT ctx,
                           KOS_TYPE    object_type,
                           uint32_t    size)
{
    KOS_PAGE       *page       = ctx->cur_page;
    const uint32_t  num_slots  = (size + sizeof(KOS_SLOT) - 1) >> KOS_OBJ_ALIGN_BITS;
    unsigned        seek_depth = KOS_MAX_PAGE_SEEK;
    KOS_HEAP       *heap;
    KOS_PAGE      **page_ptr;
    KOS_OBJ_HEADER *hdr        = 0;

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

    kos_lock_mutex(&heap->mutex);

    /* TODO if in GC, unlock mutex and help GC, unless the current thread is
     * already participating in GC */
#if 0
    if (_is_gc_active(heap)) {

        kos_unlock_mutex(&heap->mutex);

        _help_gc(ctx);

        kos_lock_mutex(&heap->mutex);
    }
#endif

    /* Check if any of the non-full pages contains enough space */

    page_ptr = &heap->non_full_pages;

    while (seek_depth--) {

        KOS_PAGE *old_page = *page_ptr;
        uint32_t  page_size;

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

            KOS_PAGE *pages_too_small = page;

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

                KOS_PAGE  *next      = pages_too_small->next;
                KOS_PAGE **insert_at = &heap->free_pages;
                KOS_PAGE  *next_free = *insert_at;

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

    kos_unlock_mutex(&heap->mutex);

    if ( ! hdr)
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));

    return hdr;
}

void *kos_alloc_object(KOS_CONTEXT ctx,
                       KOS_TYPE    object_type,
                       uint32_t    size)
{
    kos_trigger_mad_gc(ctx);

    if (size > (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))
    {
        return _alloc_huge_object(ctx, object_type, size);
    }
    else
    {
        return _alloc_object(ctx, object_type, size);
    }
}

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type)
{
    return _alloc_object(ctx, object_type, KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS);
}

static void _release_current_page(KOS_CONTEXT ctx)
{
    KOS_PAGE *page = ctx->cur_page;

    if (page) {

        KOS_HEAP *heap = _get_heap(ctx);

        kos_lock_mutex(&heap->mutex);

        PUSH_LIST(heap->non_full_pages, page);

        heap->used_size += _non_full_page_size(page);

        kos_unlock_mutex(&heap->mutex);

        ctx->cur_page = 0;
    }
}

static void _release_current_page_locked(KOS_CONTEXT ctx)
{
    KOS_PAGE *page = ctx->cur_page;

    if (page) {

        KOS_HEAP *heap = _get_heap(ctx);

        PUSH_LIST(heap->non_full_pages, page);

        heap->used_size += _non_full_page_size(page);

        ctx->cur_page = 0;
    }
}

static void _stop_the_world(KOS_CONTEXT ctx)
{
    /* TODO make all threads enter _help_gc() */
}

enum KOS_MARK_STATE_E {
    WHITE     = 0,
    GRAY      = 1,
    BLACK     = 2,
    COLORMASK = 3
};

static void _set_marking_in_pages(KOS_PAGE             *page,
                                  enum KOS_MARK_STATE_E state)
{
    const int mask = (int)state * 0x55;

    while (page) {

        uint32_t *const bitmap = (uint32_t *)((uint8_t *)page + KOS_BITMAP_OFFS);

        memset(bitmap, mask, KOS_BITMAP_SIZE);

        KOS_atomic_write_u32(page->num_used, 0);

        page = page->next;
    }
}

static void _clear_marking(KOS_HEAP *heap)
{
    _set_marking_in_pages(heap->non_full_pages, WHITE);
    _set_marking_in_pages(heap->full_pages,     WHITE);
    _set_marking_in_pages(heap->free_pages,     GRAY);
}

struct KOS_MARK_LOC_S {
    KOS_ATOMIC(uint32_t) *bitmap;
    uint32_t              mask_idx;
};

static struct KOS_MARK_LOC_S _get_mark_location(KOS_OBJ_ID obj_id)
{
    const uintptr_t offs_in_page  = (uintptr_t)obj_id & (uintptr_t)(KOS_PAGE_SIZE - 1);
    const uint32_t  slot_idx      = (uint32_t)(offs_in_page - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

    const uintptr_t page_addr     = (uintptr_t)obj_id & ~(uintptr_t)(KOS_PAGE_SIZE - 1);
    uint32_t *const bitmap        = (uint32_t *)(page_addr + KOS_BITMAP_OFFS) + (slot_idx >> 4);

    struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

    mark_loc.bitmap   = (KOS_ATOMIC(uint32_t) *)bitmap;
    mark_loc.mask_idx = (slot_idx & 0xFU) * 2;

    return mark_loc;
}

static void _advance_marking(struct KOS_MARK_LOC_S *mark_loc,
                             uint32_t               num_slots)
{
    const uint32_t mask_idx = mark_loc->mask_idx + num_slots * 2U;

    mark_loc->bitmap   += mask_idx >> 5;
    mark_loc->mask_idx =  mask_idx & 0x1FU;
}

static uint32_t _get_marking(const struct KOS_MARK_LOC_S *mark_loc)
{
    const uint32_t marking = KOS_atomic_read_u32(*(mark_loc->bitmap));

    return (marking >> mark_loc->mask_idx) & COLORMASK;
}

static int _set_mark_state_loc(struct KOS_MARK_LOC_S mark_loc,
                               enum KOS_MARK_STATE_E state)
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

static int _set_mark_state(KOS_OBJ_ID            obj_id,
                           enum KOS_MARK_STATE_E state)
{
    assert(((uintptr_t)obj_id & 0xFFFFFFFFU) != 0xDDDDDDDDU);

    /* TODO can we get rid of IS_BAD_PTR ? */
    if (IS_HEAP_OBJECT(obj_id) && ! IS_BAD_PTR(obj_id)) {

        const struct KOS_MARK_LOC_S mark_loc = _get_mark_location(obj_id);

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
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id)->priv), GRAY);
            break;

        case OBJ_ARRAY:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(ARRAY, obj_id)->data), GRAY);
            break;

        case OBJ_BUFFER:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(BUFFER, obj_id)->data), GRAY);
            break;

        case OBJ_FUNCTION:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->module,   GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->closures, GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->defaults, GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->generator_stack_frame, GRAY);
            break;

        case OBJ_CLASS:
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->prototype), GRAY);
            _set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->props), GRAY);
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(CLASS, obj_id)->module,   GRAY);
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

        case OBJ_THREAD:
            {
                _set_mark_state(OBJPTR(THREAD, obj_id)->thread_func, GRAY);
                _set_mark_state(OBJPTR(THREAD, obj_id)->this_obj,    GRAY);
                _set_mark_state(OBJPTR(THREAD, obj_id)->args_obj,    GRAY);
                _set_mark_state(OBJPTR(THREAD, obj_id)->retval,      GRAY);
                _set_mark_state(OBJPTR(THREAD, obj_id)->exception,   GRAY);
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

static int _gray_to_black_in_pages(KOS_PAGE *page)
{
    int marked = 0;

    for ( ; page; page = page->next) {

        uint32_t num_slots_used = 0;

        struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

        KOS_SLOT *ptr = (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS);
        KOS_SLOT *end = ptr + get_num_active_slots(page);

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t        size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t        slots = size >> KOS_OBJ_ALIGN_BITS;
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

            ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
        }

        assert(num_slots_used >= KOS_atomic_read_u32(page->num_used));

        KOS_atomic_write_u32(page->num_used, num_slots_used);
    }

    return marked;
}

static int _gray_to_black(KOS_HEAP *heap)
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

    for (i = 0; i < ctx->helper_ref_count; ++i) {
        KOS_OBJ_ID *const obj_id_ptr = ctx->helper_refs[i];
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

static void _get_flat_list(KOS_PAGE *page, KOS_PAGE ***begin, KOS_PAGE ***end)
{
    uint8_t *const ptr       = (uint8_t *)page + KOS_BITMAP_OFFS;
    const uint32_t num_slots = KOS_min(page->num_slots, (uint32_t)KOS_SLOTS_PER_PAGE);

    *begin = (KOS_PAGE **)ptr;

    if (end)
        *end = (KOS_PAGE **)(ptr + KOS_BITMAP_SIZE + (num_slots << KOS_OBJ_ALIGN_BITS));
}

static void _get_flat_page_list(KOS_HEAP  *heap,
                                KOS_PAGE **list,
                                KOS_PAGE **pages)
{
    KOS_PAGE  *page  = *pages;
    KOS_PAGE  *first = *pages;
    KOS_PAGE **dest;
    KOS_PAGE **begin;
    KOS_PAGE **end;

    assert(page);

    _get_flat_list(first, &begin, &end);

    dest = begin;

    while (page && dest < end) {

        KOS_PAGE *next = page->next;
        unsigned  size;
        unsigned  num_pages;

        assert(((uintptr_t)page & (KOS_PAGE_SIZE - 1U)) == 0U);

        if (page->num_slots <= KOS_SLOTS_PER_PAGE) {
            *(dest++) = page;
            page = next;
            continue;
        }

        size = (unsigned)(KOS_SLOTS_OFFS + (page->num_slots << KOS_OBJ_ALIGN_BITS));

        num_pages = ((size - 1U) >> KOS_PAGE_BITS) + 1U;

        if ((intptr_t)num_pages > end - dest)
            break;

        while (size) {

            const unsigned this_size = KOS_min(size, KOS_PAGE_SIZE);

            if (this_size >= (KOS_SLOTS_OFFS + (KOS_PAGE_SIZE >> 3))) {

                page->num_slots = (this_size - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;
                assert(page->num_slots <= KOS_SLOTS_PER_PAGE);

                *(dest++) = page;

                page = (KOS_PAGE *)((uintptr_t)page + this_size);
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
    KOS_PAGE *const pa = *(KOS_PAGE *const *)a;
    KOS_PAGE *const pb = *(KOS_PAGE *const *)b;

    return (pa < pb) ? -1 : (pa > pb) ?  1 : 0;
}

static void _sort_flat_page_list(KOS_PAGE *list)
{
    KOS_PAGE **begin;

    _get_flat_list(list, &begin, 0);

    qsort(begin, KOS_atomic_read_u32(list->num_allocated), sizeof(void *), _sort_compare);
}

#ifdef CONFIG_MAD_GC
static void _lock_pages(KOS_HEAP *heap, KOS_PAGE *pages)
{
    while (pages) {

        KOS_PAGE *page      = pages;
        uint32_t  num_slots = page->num_slots;
        uint32_t  i;

        pages = page->next;

        /* Only lock heap pages which are fully aligned with OS pages */
        if (num_slots == KOS_SLOTS_PER_PAGE) {
            if (kos_mem_protect(page, KOS_PAGE_SIZE, KOS_NO_ACCESS)) {
                assert(0);
                fprintf(stderr, "Failed to lock region at %p size %u\n",
                        (void *)page, KOS_PAGE_SIZE);
                exit(1);
            }
        }

        if ( ! heap->locked_pages_last || heap->locked_pages_last->num_pages == KOS_MAX_LOCKED_PAGES) {

            struct KOS_LOCKED_PAGES_S *locked_pages =
                (struct KOS_LOCKED_PAGES_S *)kos_malloc(sizeof(struct KOS_LOCKED_PAGES_S));

            if ( ! locked_pages) {
                fprintf(stderr, "Failed to allocate memory to store locked pages\n");
                exit(1);
            }

            locked_pages->num_pages = 0;
            locked_pages->next = 0;
            if (heap->locked_pages_last)
                heap->locked_pages_last->next = locked_pages;
            heap->locked_pages_last = locked_pages;
            if ( ! heap->locked_pages_first)
                heap->locked_pages_first = locked_pages;
        }

        i = heap->locked_pages_last->num_pages++;

        heap->locked_pages_last->pages[i].page      = page;
        heap->locked_pages_last->pages[i].num_slots = num_slots;
    }
}

static void unlock_pages(KOS_HEAP *heap)
{
    struct KOS_LOCKED_PAGES_S *locked_pages = heap->locked_pages_first;
    KOS_PAGE                 **insert_at    = &heap->free_pages;

    while (locked_pages) {
        struct KOS_LOCKED_PAGES_S *cur_locked_page_list = locked_pages;

        uint32_t i;

        locked_pages = locked_pages->next;

        for (i = 0; i < cur_locked_page_list->num_pages; ++i) {
            const uint32_t  num_slots = cur_locked_page_list->pages[i].num_slots;
            KOS_PAGE *const page      = cur_locked_page_list->pages[i].page;
            KOS_PAGE       *cur;

            if (num_slots == KOS_SLOTS_PER_PAGE) {
                if (kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE)) {
                    assert(0);
                    fprintf(stderr, "Failed to unlock region at %p size %u\n",
                            (void *)page, KOS_PAGE_SIZE);
                    exit(1);
                }
            }

            /* Put page on free list */
            cur = *insert_at;

            if (page < cur) {
                insert_at = &heap->free_pages;
                cur       = heap->free_pages;
            }

            while (cur && page > cur) {
                insert_at = &cur->next;
                cur       = cur->next;
            }

            assert(page < cur || ! cur);

            page->next = cur;
            *insert_at = page;
            if (cur)
                insert_at = &page->next;
        }

        kos_free(cur_locked_page_list);
    }

    heap->locked_pages_first = 0;
    heap->locked_pages_last  = 0;
}
#endif

static unsigned _push_sorted_list(KOS_HEAP *heap, KOS_PAGE *list)
{
    KOS_PAGE **begin;
    KOS_PAGE **end;
    KOS_PAGE **insert_at;
    KOS_PAGE  *page_at;
    unsigned   num_pages = 0;
#ifdef CONFIG_MAD_GC
    KOS_PAGE  *to_lock = 0;
#endif

    _get_flat_list(list, &begin, 0);

    end = begin + KOS_atomic_read_u32(list->num_allocated);

#ifdef CONFIG_MAD_GC
    insert_at = &to_lock;
#else
    insert_at = &heap->free_pages;
#endif
    page_at   = *insert_at;

    while (begin < end) {

        KOS_PAGE *page = *(begin++);

        KOS_atomic_write_u32(page->num_allocated, 0);

#ifndef NDEBUG
        if (page != list)
            memset(((uint8_t *)page) + KOS_BITMAP_OFFS,
                   0xDDU,
                   (KOS_SLOTS_OFFS - KOS_BITMAP_OFFS) + (page->num_slots << KOS_OBJ_ALIGN_BITS));
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
    memset(((uint8_t *)list) + KOS_BITMAP_OFFS,
           0xDDU,
           (KOS_SLOTS_OFFS - KOS_BITMAP_OFFS) + (list->num_slots << KOS_OBJ_ALIGN_BITS));
#endif

#ifdef CONFIG_MAD_GC
    _lock_pages(heap, to_lock);
#endif

    return num_pages;
}

static void _reclaim_free_pages(KOS_HEAP              *heap,
                                KOS_PAGE              *free_pages,
                                struct KOS_GC_STATS_S *stats)
{
    KOS_PAGE *lists = 0;

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

        KOS_PAGE *next      = lists->next;
        unsigned  num_pages = _push_sorted_list(heap, lists);

        if (stats)
            stats->num_pages_freed += num_pages;

        lists = next;
    }
}

static int _evacuate_object(KOS_CONTEXT     ctx,
                            KOS_OBJ_HEADER *hdr,
                            uint32_t        size)
{
    int             error = KOS_SUCCESS;
    const KOS_TYPE  type  = (KOS_TYPE)hdr->type;
    KOS_OBJ_HEADER *new_obj;

#ifdef CONFIG_MAD_GC
    if (size > (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))
        new_obj = (KOS_OBJ_HEADER *)_alloc_huge_object(ctx, type, size);
    else
#endif
    {
        assert(size <= (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS));
        new_obj = (KOS_OBJ_HEADER *)_alloc_object(ctx, type, size);
    }

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
            if (((KOS_STRING *)hdr)->header.flags & KOS_STRING_REF) {
                const uint8_t* old_data_ptr = (const uint8_t *)((KOS_STRING *)hdr)->ref.data_ptr;
                KOS_OBJ_ID     old_ref_obj  = ((KOS_STRING *)hdr)->ref.obj_id;
                KOS_OBJ_ID     new_ref_obj  = old_ref_obj;
                intptr_t       delta;

                assert(OBJPTR(STRING, old_ref_obj)->header.flags & KOS_STRING_LOCAL);

                _update_child_ptr(&new_ref_obj);

                delta = (intptr_t)new_ref_obj - (intptr_t)old_ref_obj;

                ((KOS_STRING *)hdr)->ref.obj_id   = new_ref_obj;
                ((KOS_STRING *)hdr)->ref.data_ptr = old_data_ptr + delta;
            }
            break;

        default:
            assert(hdr->type == OBJ_OBJECT);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->props);
            _update_child_ptr(&((KOS_OBJECT *)hdr)->prototype);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->priv);
            break;

        case OBJ_ARRAY:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_ARRAY *)hdr)->data);
            break;

        case OBJ_BUFFER:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_BUFFER *)hdr)->data);
            break;

        case OBJ_OBJECT_STORAGE:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_STORAGE *)hdr)->new_prop_table);
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
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_ARRAY_STORAGE *)hdr)->next);
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_ARRAY_STORAGE *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_ARRAY_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item)
                    _update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_FUNCTION:
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->module);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->closures);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->defaults);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->generator_stack_frame);
            break;

        case OBJ_CLASS:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->prototype);
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->props);
            _update_child_ptr(&((KOS_CLASS *)hdr)->module);
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

        case OBJ_THREAD:
            {
                _update_child_ptr(&((KOS_THREAD *)hdr)->thread_func);
                _update_child_ptr(&((KOS_THREAD *)hdr)->this_obj);
                _update_child_ptr(&((KOS_THREAD *)hdr)->args_obj);
                _update_child_ptr(&((KOS_THREAD *)hdr)->retval);
                _update_child_ptr(&((KOS_THREAD *)hdr)->exception);
            }
            break;
    }
}

static void _update_after_evacuation(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst             = ctx->inst;
    KOS_HEAP           *heap             = &inst->heap;
    KOS_PAGE           *page             = heap->full_pages;
    int                 non_full_checked = 0;

    if ( ! page) {
        page = heap->non_full_pages;
        non_full_checked = 1;
    }

    /* TODO add way to go over all pages */
    while (page) {

        uint8_t       *ptr = (uint8_t *)page + KOS_SLOTS_OFFS;
        uint8_t *const end = ptr + (get_num_active_slots(page) << KOS_OBJ_ALIGN_BITS);

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

        kos_lock_mutex(&inst->threads.mutex);

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

            for (i = 0; i < ctx->helper_ref_count; ++i) {
                KOS_OBJ_ID *const obj_id_ptr = ctx->helper_refs[i];
                if (obj_id_ptr)
                    _update_child_ptr(obj_id_ptr);
            }

            ctx = ctx->next;
        }

        kos_unlock_mutex(&inst->threads.mutex);
    }
}

static int _evacuate(KOS_CONTEXT            ctx,
                     KOS_PAGE             **free_pages,
                     struct KOS_GC_STATS_S *out_stats)
{
    KOS_HEAP   *heap           = _get_heap(ctx);
    int         error          = KOS_SUCCESS;
    KOS_PAGE   *page           = heap->full_pages;
    KOS_PAGE   *non_full_pages = heap->non_full_pages;
    KOS_PAGE   *next;
    KOS_OBJ_ID  exception      = KOS_get_exception(ctx);
    int         non_full_turn  = 0;

    struct KOS_GC_STATS_S stats = { 0, 0, 0, 0, 0, 0, 0, 0 };

    KOS_clear_exception(ctx);

    heap->full_pages     = 0;
    heap->non_full_pages = 0;

    if ( ! page) {
        non_full_turn = 1;
        page = non_full_pages;
        non_full_pages = 0;
    }

    for ( ; page; page = next) {

        struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

        unsigned  num_evac = 0;
        KOS_SLOT *ptr      = (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS);
        KOS_SLOT *end      = ptr + get_num_active_slots(page);
#ifndef NDEBUG
        KOS_SLOT *page_end = ptr + KOS_atomic_read_u32(page->num_allocated);
#endif
#ifndef CONFIG_MAD_GC
        const uint32_t num_slots_used = KOS_atomic_read_u32(page->num_used);
#endif

        heap->used_size -= non_full_turn ? _non_full_page_size(page) : _full_page_size(page);

        next = page->next;

        if ( ! next && non_full_pages) {
            next = non_full_pages;
            non_full_pages = 0;
            non_full_turn = 1;
        }

#ifndef CONFIG_MAD_GC
        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        if (num_slots_used >= (KOS_SLOTS_PER_PAGE * KOS_MIGRATION_THRESH) / 100U) {
            PUSH_LIST(heap->full_pages, page);
            heap->used_size += _full_page_size(page);
            ++stats.num_pages_kept;
            stats.size_kept += num_slots_used << KOS_OBJ_ALIGN_BITS;
            continue;
        }
#endif

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t  color = _get_marking(&mark_loc);

            assert(size > 0U);
            assert(color != GRAY);
            assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

            if (color) {
                if (_evacuate_object(ctx, hdr, size)) {

                    KOS_clear_exception(ctx);

                    _release_current_page_locked(ctx);

#ifdef CONFIG_MAD_GC
                    unlock_pages(heap);
#else
                    /* TODO find all free pages and make them available; a free
                     * page which had no objects to evacuate is marked with
                     * num_allocated == 0 */
                    assert(0);
#endif
                    error = _evacuate_object(ctx, hdr, size);

                    if (error) {
                        /* TODO failed mid-evacuation, therefore we have to do the
                         * following:
                         * 1) Put back the remaining pages on the full or half-full
                         *    list.
                         * 2) Update pointers after evacuation.
                         * 3) Set size of the first object to the size of
                         *    the area that was evacuated successfully and set
                         *    the type to OBJ_OPAQUE - this will make sure this
                         *    area remains reserved until the next evacuation.
                         */

                        assert(0);

                        goto cleanup;
                    }

                }
                ++num_evac;
                stats.size_evacuated += size;
            }
            else {
                if (hdr->type == OBJ_OBJECT) {

                    KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

                    if (obj->finalize) {

                        obj->finalize(ctx, KOS_atomic_read_obj(obj->priv));

                        ++stats.num_objs_finalized;
                    }
                }

                ++stats.num_objs_freed;
                stats.size_freed += size;
            }

            _advance_marking(&mark_loc, size >> KOS_OBJ_ALIGN_BITS);

            ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
        }

        stats.num_objs_evacuated += num_evac;

        /* Mark page which has no evacuated objects, such page can be re-used
         * early before the end of evacuation when the heap is full. */
        if ( ! num_evac)
            KOS_atomic_write_u32(page->num_allocated, 0);

        PUSH_LIST(*free_pages, page);
    }

cleanup:
    if (out_stats)
        *out_stats = stats;

    _release_current_page_locked(ctx);

    if ( ! IS_BAD_PTR(exception))
        ctx->exception = exception;

    return error;
}

static void _update_gc_threshold(KOS_HEAP *heap)
{
    heap->gc_threshold = heap->used_size + KOS_GC_STEP;
}

static int _help_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = _get_heap(ctx);

    while (KOS_atomic_read_u32(heap->gc_state) != GC_INACTIVE)
        /* TODO actually help garbage collector */
        kos_yield();

    return KOS_SUCCESS;
}

#ifdef CONFIG_MAD_GC
void kos_trigger_mad_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = _get_heap(ctx);

    /* Don't try to collect garbage when the garbage collector is running */
    if (is_recursive_collection(heap))
        return;

    kos_lock_mutex(&heap->mutex);
    _try_collect_garbage(ctx);
    kos_unlock_mutex(&heap->mutex);
}
#endif

void kos_lock_gc(KOS_INSTANCE *inst)
{
    while ( ! KOS_atomic_cas_u32(inst->heap.gc_state, GC_INACTIVE, GC_LOCKED))
        kos_yield();
}

void kos_unlock_gc(KOS_INSTANCE *inst)
{
    assert(KOS_atomic_read_u32(inst->heap.gc_state) == GC_LOCKED);

    KOS_atomic_write_u32(inst->heap.gc_state, GC_INACTIVE);

    KOS_atomic_release_barrier();
}

int KOS_collect_garbage(KOS_CONTEXT            ctx,
                        struct KOS_GC_STATS_S *stats)
{
    int       error      = KOS_SUCCESS;
    KOS_HEAP *heap       = _get_heap(ctx);
    KOS_PAGE *free_pages = 0;

    kos_lock_mutex(&ctx->inst->threads.mutex);

    /* TODO multiple threads are not brought up yet */
    if (ctx->prev || ctx->next) {
        kos_unlock_mutex(&ctx->inst->threads.mutex);
        return KOS_SUCCESS;
    }

    kos_unlock_mutex(&ctx->inst->threads.mutex);

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

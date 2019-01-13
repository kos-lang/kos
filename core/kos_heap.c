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

#if 0
#define gc_trace(x) printf x
#else
#define gc_trace(x) do {} while (0)
#endif

enum GC_STATE_E {
    GC_INACTIVE,

    /* ctx->gc_state */
    GC_SUSPENDED,
    GC_ENGAGED,

    /* heap->gc_state */
    GC_INIT,
    GC_MARK,
    GC_EVACUATE,
    GC_UPDATE
};

struct KOS_POOL_HEADER_S {
    KOS_POOL *next;        /* Pointer to next pool header     */
    void     *memory;      /* Pointer to the allocated memory */
    uint32_t  alloc_size;  /* Number of allocated bytes       */
};

typedef struct KOS_SLOT_PLACEHOLDER_S {
    uint8_t dummy[1 << KOS_OBJ_ALIGN_BITS];
} KOS_SLOT;

static void help_gc(KOS_CONTEXT ctx);

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

namespace {
    template<typename T>
    void list_push(T*& list, T* value)
    {
        value->next = list;
        list        = value;
    }

    template<typename T>
    T* list_pop(T*& list)
    {
        T* const ret = list;

        if (ret) {
            T* const next = ret->next;

            list = next;
        }

        return ret;
    }
}

#define PUSH_LIST(list, value) list_push(list, value)

#define POP_LIST(list) list_pop(list)

#else

static void list_push(void **list_ptr, void *value, void **next_ptr)
{
    *next_ptr = *list_ptr;
    *list_ptr = value;
}

static void *list_pop(void **list_ptr)
{
    void *ret = *list_ptr;

    if (ret) {
        void *next = *(void **)ret;

        *list_ptr = next;
    }

    return ret;
}

#define PUSH_LIST(list, value) list_push((void **)&(list), (void *)(value), (void **)&(value)->next)

#define POP_LIST(list) list_pop((void **)&(list))

#endif

static void clear_marking_in_pages(KOS_PAGE *page);

#ifdef __cplusplus
static inline KOS_HEAP *get_heap(KOS_CONTEXT ctx)
{
    return &ctx->inst->heap;
}
#else
#define get_heap(ctx) (&(ctx)->inst->heap)
#endif

int kos_heap_init(KOS_INSTANCE *inst)
{
    KOS_HEAP *heap = &inst->heap;

    KOS_atomic_write_u32(heap->gc_state, GC_INACTIVE);
    heap->heap_size      = 0;
    heap->used_size      = 0;
    heap->gc_threshold   = KOS_GC_STEP;
    heap->free_pages     = 0;
    heap->non_full_pages = 0;
    heap->full_pages     = 0;
    heap->pools          = 0;
    heap->pool_headers   = 0;

    KOS_atomic_write_ptr(heap->gray_pages,   (KOS_PAGE *)0);
    KOS_atomic_write_ptr(heap->update_pages, (KOS_PAGE *)0);
    KOS_atomic_write_u32(heap->gray_marked,  0U);
    KOS_atomic_write_u32(heap->walk_stage,   0U);
    KOS_atomic_write_u32(heap->walk_active,  0U);
    KOS_atomic_write_u32(heap->gc_cycles,    0U);

#ifdef CONFIG_MAD_GC
    heap->locked_pages_first = 0;
    heap->locked_pages_last  = 0;
#endif

    assert(KOS_BITMAP_OFFS + KOS_BITMAP_SIZE <= KOS_SLOTS_OFFS);
    assert( ! (KOS_SLOTS_OFFS & 7U));
    assert(KOS_SLOTS_OFFS + (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) == KOS_PAGE_SIZE);

    return kos_create_mutex(&heap->mutex);
}

#if !defined(NDEBUG) || defined(CONFIG_MAD_GC)
int kos_gc_active(KOS_CONTEXT ctx)
{
    return KOS_atomic_read_u32(get_heap(ctx)->gc_state) >= GC_MARK;
}
#endif

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

            for (i = 0; i < locked_pages->num_pages; i++) {

                KOS_PAGE *page      = locked_pages->pages[i].page;
                uint32_t  num_slots = locked_pages->pages[i].num_slots;

                assert(num_slots == KOS_SLOTS_PER_PAGE);
                if (page)
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
        kos_free_aligned(memory);

        kos_free(pool);
    }

    kos_destroy_mutex(&inst->heap.mutex);
}

#ifdef CONFIG_MAD_GC
static KOS_PAGE *unlock_one_page(KOS_HEAP *heap)
{
    struct KOS_LOCKED_PAGES_S *locked_pages = heap->locked_pages_first;

    while (locked_pages) {

        uint32_t i;

        for (i = 0; i < locked_pages->num_pages; i++) {

            KOS_PAGE *page = locked_pages->pages[i].page;

            assert(locked_pages->pages[i].num_slots == KOS_SLOTS_PER_PAGE);

            if (page) {
                locked_pages->pages[i].page = 0;

                kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE);

                assert(page->num_slots == KOS_SLOTS_PER_PAGE);
                page->next = 0;

                return page;
            }
        }
    }

    return 0;
}
#endif

enum KOS_WALK_STAGE_E {
    WALK_NON_FULL,
    WALK_FULL,
    WALK_DONE
};

static KOS_PAGE *get_pages_head(KOS_HEAP *heap)
{
    KOS_atomic_write_u32(heap->walk_active, 0U);

    if (heap->non_full_pages) {
        KOS_atomic_write_u32(heap->walk_stage, WALK_NON_FULL);
        return heap->non_full_pages;
    }

    KOS_atomic_write_u32(heap->walk_stage, WALK_FULL);
    return heap->full_pages;
}

static KOS_PAGE *get_next_page(KOS_HEAP               *heap,
                               KOS_ATOMIC(KOS_PAGE *) *page_ptr,
                               KOS_PAGE               *prev_page)
{
    KOS_PAGE *page = 0;

    for (;;) {

        enum KOS_WALK_STAGE_E stage;
        KOS_PAGE             *next;

        page = (KOS_PAGE *)KOS_atomic_read_ptr(*page_ptr);

        if ( ! page)
            break;

        next = page->next;

        if (next) {
            if (KOS_atomic_cas_ptr(*page_ptr, page, next))
                break;
            else
                continue;
        }

        kos_lock_mutex(&heap->mutex);

        stage = (enum KOS_WALK_STAGE_E)KOS_atomic_read_u32(heap->walk_stage);

        if (page != (KOS_PAGE *)KOS_atomic_read_ptr(*page_ptr)) {
            kos_unlock_mutex(&heap->mutex);
            continue;
        }

        if (stage == WALK_NON_FULL && heap->full_pages) {
            KOS_atomic_write_ptr(*page_ptr, heap->full_pages);
            KOS_atomic_write_u32(heap->walk_stage, WALK_FULL);

            kos_unlock_mutex(&heap->mutex);
            break;
        }
        else if (stage != WALK_DONE) {
            KOS_atomic_write_ptr(*page_ptr, (KOS_PAGE *)0);
            KOS_atomic_write_u32(heap->walk_stage, WALK_DONE);

            kos_unlock_mutex(&heap->mutex);
            break;
        }

        kos_unlock_mutex(&heap->mutex);
    }

    if (page && ! prev_page)
        KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t) *)&heap->walk_active, 1);

    if (prev_page && ! page)
        KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t) *)&heap->walk_active, -1);

    return page;
}

static void wait_for_walk_end(KOS_HEAP *heap)
{
    KOS_atomic_release_barrier();
    while (KOS_atomic_read_u32(*(KOS_ATOMIC(uint32_t) *)&heap->walk_active)) {
        kos_yield();
        KOS_atomic_acquire_barrier();
    }
}

static KOS_POOL *alloc_pool(KOS_HEAP *heap,
                            uint32_t  alloc_size)
{
    KOS_POOL *pool_hdr;
    uint8_t  *pool;

    if (heap->heap_size + alloc_size > KOS_MAX_HEAP_SIZE)
        return 0;

    pool = (uint8_t *)kos_malloc_aligned(alloc_size, (size_t)KOS_PAGE_SIZE);

    if ( ! pool)
        return 0;

    heap->heap_size += alloc_size;

    assert(KOS_align_up((uintptr_t)pool, (uintptr_t)KOS_PAGE_SIZE) == (uintptr_t)pool);

    pool_hdr = (KOS_POOL *)kos_malloc(sizeof(KOS_POOL));

    if ( ! pool_hdr) {
        kos_free_aligned(pool);
        return 0;
    }

    pool_hdr->memory     = pool;
    pool_hdr->alloc_size = alloc_size;

    PUSH_LIST(heap->pools, pool_hdr);

    return pool_hdr;
}

static int alloc_page_pool(KOS_HEAP *heap)
{
    KOS_POOL *pool_hdr  = alloc_pool(heap, KOS_POOL_SIZE);
    KOS_PAGE *next_page = 0;
    uint8_t  *page_bytes;

    if ( ! pool_hdr)
        return KOS_ERROR_OUT_OF_MEMORY;

    assert((uintptr_t)pool_hdr->memory % (uintptr_t)KOS_PAGE_SIZE == 0U);
    assert(pool_hdr->alloc_size % KOS_PAGE_SIZE == 0U);

    page_bytes = (uint8_t *)pool_hdr->memory + pool_hdr->alloc_size;

    assert(heap->free_pages == 0);

    while (page_bytes > (uint8_t *)pool_hdr->memory) {

        KOS_PAGE *page;

        page_bytes -= KOS_PAGE_SIZE;
        assert( ! ((uintptr_t)page_bytes & (uintptr_t)(KOS_PAGE_SIZE - 1)));

        page = (KOS_PAGE *)page_bytes;

        page->num_slots = (KOS_PAGE_SIZE - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

        KOS_atomic_write_u32(page->num_allocated, 0);

        page->next = next_page;

        KOS_PERF_CNT(alloc_new_page);

        next_page = page;
    }

    heap->free_pages = next_page;

#ifndef NDEBUG
    {
        KOS_PAGE *page      = heap->free_pages;
        uint32_t  num_pages = 0;

        next_page = page;

        while (page) {

            assert(page->num_slots == KOS_SLOTS_PER_PAGE);
            assert(KOS_atomic_read_u32(page->num_allocated) == 0);

            assert(page == next_page);

            assert((uintptr_t)page >= (uintptr_t)pool_hdr->memory);

            assert((uintptr_t)page + KOS_PAGE_SIZE <=
                   (uintptr_t)pool_hdr->memory + pool_hdr->alloc_size);

            next_page = (KOS_PAGE *)((uint8_t *)page + KOS_PAGE_SIZE);

            page = page->next;

            ++num_pages;
        }

        assert(num_pages == KOS_POOL_SIZE / KOS_PAGE_SIZE ||
               num_pages == (KOS_POOL_SIZE / KOS_PAGE_SIZE) - 1U);
    }
#endif

    return KOS_SUCCESS;
}

static KOS_PAGE *alloc_page(KOS_HEAP *heap)
{
    KOS_PAGE *page = heap->free_pages;

#ifdef CONFIG_MAD_GC
    if ( ! page && KOS_atomic_read_u32(heap->gc_state) == GC_INACTIVE &&
        (heap->heap_size + KOS_POOL_SIZE > KOS_MAX_HEAP_SIZE))

        page = unlock_one_page(heap);
#endif

    if ( ! page) {

        if (alloc_page_pool(heap))
            return 0;

        page = heap->free_pages;

        assert(page);
    }

    gc_trace(("alloc page %p\n", (void *)page));

    assert(page->num_slots == KOS_SLOTS_PER_PAGE);
    assert(KOS_atomic_read_u32(page->num_allocated) == 0);
    assert(page->next == 0 || page < page->next);

    heap->free_pages = page->next;

    KOS_PERF_CNT(alloc_free_page);

    KOS_atomic_write_u32(page->num_used, 0U);
    page->next = 0;
    return page;
}

static void try_collect_garbage(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);

    /* Don't try to collect garbage when the garbage collector is running */
    if (KOS_atomic_read_u32(ctx->gc_state) != GC_INACTIVE)
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

static void *alloc_slots_from_page(KOS_PAGE *page, uint32_t num_slots)
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

static KOS_OBJ_HEADER *alloc_object_from_page(KOS_PAGE *page,
                                              KOS_TYPE  object_type,
                                              uint32_t  num_slots)
{
    KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)alloc_slots_from_page(page, num_slots);

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

        ctx->cur_page = alloc_page(&inst->heap);

        kos_unlock_mutex(&inst->heap.mutex);

        if ( ! ctx->cur_page)
            return 0;
    }

    return alloc_object_from_page(ctx->cur_page, object_type, num_slots);
}

static uint32_t full_page_size(KOS_PAGE *page)
{
    return KOS_SLOTS_OFFS + (page->num_slots << KOS_OBJ_ALIGN_BITS);
}

static uint32_t non_full_page_size(KOS_PAGE *page)
{
    return KOS_SLOTS_OFFS + (KOS_atomic_read_u32(page->num_allocated) << KOS_OBJ_ALIGN_BITS);
}

static void release_current_page_locked(KOS_CONTEXT ctx)
{
    KOS_PAGE *page = ctx->cur_page;

    if (page) {

        KOS_HEAP *heap = get_heap(ctx);

        if (KOS_atomic_read_u32(heap->gc_state) != GC_INACTIVE) {
            page->next = 0;
            clear_marking_in_pages(page);
            KOS_atomic_release_barrier();
        }

        gc_trace(("release cur page %p ctx=%p\n", (void *)page, (void *)ctx));

        PUSH_LIST(heap->non_full_pages, page);

        heap->used_size += non_full_page_size(page);

        ctx->cur_page = 0;
    }
}

void kos_heap_release_thread_page(KOS_CONTEXT ctx)
{
    if (ctx->cur_page) {

        KOS_HEAP *const heap = get_heap(ctx);

        kos_lock_mutex(&heap->mutex);

        release_current_page_locked(ctx);

        kos_unlock_mutex(&heap->mutex);
    }
}

#ifndef NDEBUG
static void verify_heap_used_size(KOS_HEAP *heap)
{
    uint32_t  used_size = 0;
    KOS_PAGE *page;

    kos_lock_mutex(&heap->mutex);

    for (page = heap->non_full_pages; page; page = page->next)
        used_size += non_full_page_size(page);

    for (page = heap->full_pages; page; page = page->next)
        used_size += full_page_size(page);

    assert(used_size == heap->used_size);

    kos_unlock_mutex(&heap->mutex);
}
#else
#define verify_heap_used_size(heap) ((void)0)
#endif

static KOS_OBJ_HEADER *setup_huge_object_in_page(KOS_HEAP *heap,
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

    heap->used_size += full_page_size(page);

    KOS_atomic_write_u32(page->num_allocated, page->num_slots);

    return hdr;
}

/* TODO Allocate huge object at the end of the page, use beginning of the page
 *      for allocating smaller objects ! */
static void *alloc_huge_object(KOS_CONTEXT ctx,
                               KOS_TYPE    object_type,
                               uint32_t    size)
{
    KOS_HEAP       *heap = get_heap(ctx);
    KOS_OBJ_HEADER *hdr  = 0;
    KOS_PAGE      **page_ptr;
    KOS_PAGE       *page;

    assert(KOS_atomic_read_u32(ctx->gc_state) != GC_SUSPENDED);

    kos_lock_mutex(&heap->mutex);

    try_collect_garbage(ctx);

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

            assert(page->num_slots == KOS_SLOTS_PER_PAGE);

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

                hdr = setup_huge_object_in_page(heap, page, object_type, size);

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

        KOS_POOL *pool = alloc_pool(heap, KOS_align_up(size + (uint32_t)KOS_SLOTS_OFFS, (uint32_t)KOS_PAGE_SIZE));

        if (pool) {
            page = (KOS_PAGE *)pool->memory;

            page->num_slots = (pool->alloc_size - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

            assert((page->num_slots << KOS_OBJ_ALIGN_BITS) >= size);

            hdr = setup_huge_object_in_page(heap,
                                            page,
                                            object_type,
                                            size);

            assert((uint8_t *)hdr + size <= (uint8_t *)pool->memory + pool->alloc_size);
        }
        else {
            kos_unlock_mutex(&heap->mutex);
            KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
            kos_lock_mutex(&heap->mutex);
        }
    }

    kos_unlock_mutex(&heap->mutex);

    return (void *)hdr;
}

static int is_page_full(KOS_PAGE *page)
{
    return KOS_atomic_read_u32(page->num_allocated) == page->num_slots;
}

static void *alloc_object(KOS_CONTEXT ctx,
                          KOS_TYPE    object_type,
                          uint32_t    size)
{
    KOS_PAGE       *page       = ctx->cur_page;
    const uint32_t  num_slots  = (size + sizeof(KOS_SLOT) - 1) >> KOS_OBJ_ALIGN_BITS;
    unsigned        seek_depth = KOS_MAX_PAGE_SEEK;
    KOS_HEAP       *heap;
    KOS_PAGE      **page_ptr;
    KOS_OBJ_HEADER *hdr        = 0;

    assert(KOS_atomic_read_u32(ctx->gc_state) != GC_SUSPENDED);

    KOS_instance_validate(ctx);

    /* Fast path: allocate from a page held by this thread */
    if (page) {

        hdr = alloc_object_from_page(page, object_type, num_slots);

        if (hdr)
            return hdr;
    }

    /* Slow path: find a non-full page in the heap which has enough room or
     * allocate a new page. */

    heap = get_heap(ctx);

    kos_lock_mutex(&heap->mutex);

    {
        const uint32_t gc_state = KOS_atomic_read_u32(heap->gc_state);

        if (gc_state != GC_INACTIVE && KOS_atomic_read_u32(ctx->gc_state) == GC_INACTIVE) {
            kos_unlock_mutex(&heap->mutex);

            help_gc(ctx);

            kos_lock_mutex(&heap->mutex);

            assert( ! ctx->cur_page);
            page = 0;
        }
    }

    /* Check if any of the non-full pages contains enough space */

    page_ptr = &heap->non_full_pages;

    while (seek_depth--) {

        KOS_PAGE *old_page = *page_ptr;
        uint32_t  page_size;

        if ( ! old_page)
            break;

        assert(old_page->num_slots == KOS_SLOTS_PER_PAGE);

        page_size = non_full_page_size(old_page);

        hdr = alloc_object_from_page(old_page, object_type, num_slots);

        if (hdr) {
            heap->used_size -= page_size;

            if (is_page_full(old_page)) {
                *page_ptr = old_page->next;
                PUSH_LIST(heap->full_pages, old_page);
                heap->used_size += full_page_size(old_page);
            }
            else if ( ! page || (page && KOS_atomic_read_u32(page->num_allocated)
                                         > KOS_atomic_read_u32(old_page->num_allocated))) {
                *page_ptr = old_page->next;

                if (page) {
                    assert(page == ctx->cur_page);
                    assert(page->num_slots == KOS_SLOTS_PER_PAGE);
                    if (is_page_full(page)) {
                        heap->used_size += full_page_size(page);
                        PUSH_LIST(heap->full_pages, page);
                    }
                    else {
                        heap->used_size += non_full_page_size(page);
                        PUSH_LIST(heap->non_full_pages, page);
                    }
                }

                KOS_atomic_write_u32(old_page->num_used, 0U);
                old_page->next = 0;
                ctx->cur_page  = old_page;
            }
            else
                heap->used_size += non_full_page_size(old_page);
            break;
        }

        page_ptr = &old_page->next;
    }

    if ( ! hdr) {

        /* Release thread's current page */
        if (page) {

            if (is_page_full(page)) {
                PUSH_LIST(heap->full_pages, page);
                heap->used_size += full_page_size(page);
            }
            else {
                PUSH_LIST(heap->non_full_pages, page);
                heap->used_size += non_full_page_size(page);
            }

            ctx->cur_page = 0;
        }

        try_collect_garbage(ctx);

        /* Allocate a new page */
        page = alloc_page(heap);

        assert( ! page || page->num_slots == KOS_SLOTS_PER_PAGE);

        if (page) {

            assert(page->num_slots >= num_slots);

            ctx->cur_page = page;

            hdr = alloc_object_from_page(page, object_type, num_slots);

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
    assert(KOS_atomic_read_u32(ctx->gc_state) == GC_INACTIVE);

    kos_trigger_mad_gc(ctx);

    if (size > (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))
        return alloc_huge_object(ctx, object_type, size);
    else
        return alloc_object(ctx, object_type, size);
}

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type)
{
    return alloc_object(ctx, object_type, KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS);
}

static void stop_the_world(KOS_INSTANCE *inst)
{
    KOS_CONTEXT ctx = &inst->threads.main_thread;

    KOS_atomic_full_barrier();

    while (ctx) {

        const enum GC_STATE_E gc_state = (enum GC_STATE_E)KOS_atomic_read_u32(ctx->gc_state);

        if (gc_state == GC_INACTIVE) {
            kos_yield();
            continue;
        }

        ctx = ctx->next;
    }
}

enum KOS_MARK_STATE_E {
    WHITE     = 0,
    GRAY      = 1,
    BLACK     = 2,
    COLORMASK = 3
};

static void clear_marking_in_pages(KOS_PAGE *page)
{
    while (page) {

        uint32_t *const bitmap = (uint32_t *)((uint8_t *)page + KOS_BITMAP_OFFS);

        memset(bitmap, WHITE * 0x55, KOS_BITMAP_SIZE);

        KOS_atomic_write_u32(page->num_used, 0);

        page = page->next;
    }
}

static void clear_marking(KOS_HEAP *heap)
{
    clear_marking_in_pages(heap->non_full_pages);
    clear_marking_in_pages(heap->full_pages);
}

struct KOS_MARK_LOC_S {
    KOS_ATOMIC(uint32_t) *bitmap;
    uint32_t              mask_idx;
};

static struct KOS_MARK_LOC_S get_mark_location(KOS_OBJ_ID obj_id)
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

static void advance_marking(struct KOS_MARK_LOC_S *mark_loc,
                            uint32_t               num_slots)
{
    const uint32_t mask_idx = mark_loc->mask_idx + num_slots * 2U;

    mark_loc->bitmap   += mask_idx >> 5;
    mark_loc->mask_idx =  mask_idx & 0x1FU;
}

static uint32_t get_marking(const struct KOS_MARK_LOC_S *mark_loc)
{
    const uint32_t marking = KOS_atomic_read_u32(*(mark_loc->bitmap));

    return (marking >> mark_loc->mask_idx) & COLORMASK;
}

static void set_mark_state_loc(struct KOS_MARK_LOC_S mark_loc,
                               enum KOS_MARK_STATE_E state)
{
    const uint32_t mask = (uint32_t)state << mark_loc.mask_idx;

    uint32_t value = KOS_atomic_read_u32(*mark_loc.bitmap);

    while ( ! (value & mask)) {

        if (KOS_atomic_cas_u32(*mark_loc.bitmap, value, value | mask))
            break;

        value = KOS_atomic_read_u32(*mark_loc.bitmap);
    }
}

static void set_mark_state(KOS_OBJ_ID            obj_id,
                           enum KOS_MARK_STATE_E state)
{
    assert(((uintptr_t)obj_id & 0xFFFFFFFFU) != 0xDDDDDDDDU);

    /* TODO can we get rid of IS_BAD_PTR ? */
    if (IS_HEAP_OBJECT(obj_id) && ! IS_BAD_PTR(obj_id)) {

        const struct KOS_MARK_LOC_S mark_loc = get_mark_location(obj_id);

        set_mark_state_loc(mark_loc, state);
    }
}

static void mark_children_gray(KOS_OBJ_ID obj_id)
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
                set_mark_state(OBJPTR(STRING, obj_id)->ref.obj_id, GRAY);
            break;

        default:
            assert(READ_OBJ_TYPE(obj_id) == OBJ_OBJECT);
            set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id)->props), GRAY);
            set_mark_state(OBJPTR(OBJECT, obj_id)->prototype, GRAY);
            set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id)->priv), GRAY);
            break;

        case OBJ_ARRAY:
            set_mark_state(KOS_atomic_read_obj(OBJPTR(ARRAY, obj_id)->data), GRAY);
            break;

        case OBJ_BUFFER:
            set_mark_state(KOS_atomic_read_obj(OBJPTR(BUFFER, obj_id)->data), GRAY);
            break;

        case OBJ_FUNCTION:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(FUNCTION, obj_id)->module,   GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->closures, GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->defaults, GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->generator_stack_frame, GRAY);
            break;

        case OBJ_CLASS:
            set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->prototype), GRAY);
            set_mark_state(KOS_atomic_read_obj(OBJPTR(CLASS, obj_id)->props), GRAY);
            /* TODO make these atomic */
            set_mark_state(OBJPTR(CLASS, obj_id)->module,   GRAY);
            set_mark_state(OBJPTR(CLASS, obj_id)->closures, GRAY);
            set_mark_state(OBJPTR(CLASS, obj_id)->defaults, GRAY);
            break;

        case OBJ_OBJECT_STORAGE:
            set_mark_state(KOS_atomic_read_obj(OBJPTR(OBJECT_STORAGE, obj_id)->new_prop_table), GRAY);
            {
                KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id)->items[0];
                KOS_PITEM *end  = item + OBJPTR(OBJECT_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item) {
                    set_mark_state(KOS_atomic_read_obj(item->key), GRAY);
                    set_mark_state(KOS_atomic_read_obj(item->value), GRAY);
                }
            }
            break;

        case OBJ_ARRAY_STORAGE:
            set_mark_state(KOS_atomic_read_obj(OBJPTR(ARRAY_STORAGE, obj_id)->next), GRAY);
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(ARRAY_STORAGE, obj_id)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(ARRAY_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item)
                    set_mark_state(KOS_atomic_read_obj(*item), GRAY);
            }
            break;

        case OBJ_DYNAMIC_PROP:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->getter, GRAY);
            set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->setter, GRAY);
            break;

        case OBJ_OBJECT_WALK:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->obj,         GRAY);
            set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->key_table,   GRAY);
            set_mark_state(KOS_atomic_read_obj(
                           OBJPTR(OBJECT_WALK, obj_id)->last_key),   GRAY);
            set_mark_state(KOS_atomic_read_obj(
                           OBJPTR(OBJECT_WALK, obj_id)->last_value), GRAY);
            break;

        case OBJ_MODULE:
            /* TODO lock gc during module setup */
            set_mark_state(OBJPTR(MODULE, obj_id)->name,         GRAY);
            set_mark_state(OBJPTR(MODULE, obj_id)->path,         GRAY);
            set_mark_state(OBJPTR(MODULE, obj_id)->constants,    GRAY);
            set_mark_state(OBJPTR(MODULE, obj_id)->global_names, GRAY);
            set_mark_state(OBJPTR(MODULE, obj_id)->globals,      GRAY);
            set_mark_state(OBJPTR(MODULE, obj_id)->module_names, GRAY);
            break;

        case OBJ_STACK:
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(STACK, obj_id)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(STACK, obj_id)->size;
                for ( ; item < end; ++item)
                    set_mark_state(KOS_atomic_read_obj(*item), GRAY);
            }
            break;

        case OBJ_LOCAL_REFS:
            {
                KOS_OBJ_ID **ref = &OBJPTR(LOCAL_REFS, obj_id)->refs[0];
                KOS_OBJ_ID **end = ref + OBJPTR(LOCAL_REFS, obj_id)->header.num_tracked;

                set_mark_state(OBJPTR(LOCAL_REFS, obj_id)->next, GRAY);

                for ( ; ref < end; ++ref)
                    set_mark_state(**ref, GRAY);
            }
            break;

        case OBJ_THREAD:
            {
                set_mark_state(OBJPTR(THREAD, obj_id)->thread_func, GRAY);
                set_mark_state(OBJPTR(THREAD, obj_id)->this_obj,    GRAY);
                set_mark_state(OBJPTR(THREAD, obj_id)->args_obj,    GRAY);
                set_mark_state(OBJPTR(THREAD, obj_id)->retval,      GRAY);
                set_mark_state(OBJPTR(THREAD, obj_id)->exception,   GRAY);
            }
            break;
    }
}

static void mark_object_black(KOS_OBJ_ID obj_id)
{
    if (IS_HEAP_OBJECT(obj_id)) {

        assert( ! IS_BAD_PTR(obj_id));

        set_mark_state(obj_id, BLACK);

        mark_children_gray(obj_id);
    }
}

static void gray_to_black_in_pages(KOS_HEAP *heap)
{
    int32_t   marked = 0;
    KOS_PAGE *page   = get_next_page(heap, &heap->gray_pages, 0);

    for ( ; page; page = get_next_page(heap, &heap->gray_pages, page)) {

        uint32_t num_slots_used = 0;

        struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

        KOS_SLOT *ptr = (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS);
        KOS_SLOT *end = ptr + get_num_active_slots(page);

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t        size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t        slots = size >> KOS_OBJ_ALIGN_BITS;
            const uint32_t        color = get_marking(&mark_loc);

            if (color == GRAY) {
                set_mark_state_loc(mark_loc, BLACK);
                num_slots_used += slots;
                marked = 1;

                mark_children_gray((KOS_OBJ_ID)((intptr_t)hdr + 1));
            }
            else if (color)
                num_slots_used += slots;

            advance_marking(&mark_loc, slots);

            ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
        }

        assert(num_slots_used >= KOS_atomic_read_u32(page->num_used));

        KOS_atomic_write_u32(page->num_used, num_slots_used);
    }

    KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t) *)&heap->gray_marked, marked);
}

static uint32_t gray_to_black(KOS_HEAP *heap)
{
    KOS_atomic_write_u32(heap->gray_marked, 0);
    KOS_atomic_write_ptr(heap->gray_pages,  get_pages_head(heap));

    gray_to_black_in_pages(heap);

    wait_for_walk_end(heap);

    return KOS_atomic_read_u32(heap->gray_marked);
}

static void mark_roots_in_context(KOS_CONTEXT ctx)
{
    uint32_t i;

    assert(KOS_atomic_read_u32(ctx->gc_state) != GC_INACTIVE);

    if ( ! IS_BAD_PTR(ctx->exception))
        mark_object_black(ctx->exception);
    if ( ! IS_BAD_PTR(ctx->retval))
        mark_object_black(ctx->retval);
    if ( ! IS_BAD_PTR(ctx->stack))
        mark_object_black(ctx->stack);
    if ( ! IS_BAD_PTR(ctx->local_refs))
        mark_object_black(ctx->local_refs);

    for (i = 0; i < ctx->tmp_ref_count; ++i) {
        KOS_OBJ_ID *const obj_id_ptr = ctx->tmp_refs[i];
        if (obj_id_ptr) {
            const KOS_OBJ_ID obj_id = *obj_id_ptr;
            if ( ! IS_BAD_PTR(obj_id))
                mark_object_black(obj_id);
        }
    }

    for (i = 0; i < ctx->helper_ref_count; ++i) {
        KOS_OBJ_ID *const obj_id_ptr = ctx->helper_refs[i];
        if (obj_id_ptr) {
            const KOS_OBJ_ID obj_id = *obj_id_ptr;
            if ( ! IS_BAD_PTR(obj_id))
                mark_object_black(obj_id);
        }
    }
}

static void mark_roots(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    {
        int i;

        for (i = 0; i < KOS_STR_NUM; ++i)
            mark_object_black(inst->common_strings[i]);
    }

    mark_object_black(inst->prototypes.object_proto);
    mark_object_black(inst->prototypes.number_proto);
    mark_object_black(inst->prototypes.integer_proto);
    mark_object_black(inst->prototypes.float_proto);
    mark_object_black(inst->prototypes.string_proto);
    mark_object_black(inst->prototypes.boolean_proto);
    mark_object_black(inst->prototypes.array_proto);
    mark_object_black(inst->prototypes.buffer_proto);
    mark_object_black(inst->prototypes.function_proto);
    mark_object_black(inst->prototypes.class_proto);
    mark_object_black(inst->prototypes.generator_proto);
    mark_object_black(inst->prototypes.exception_proto);
    mark_object_black(inst->prototypes.generator_end_proto);
    mark_object_black(inst->prototypes.thread_proto);

    mark_object_black(inst->modules.init_module);
    mark_object_black(inst->modules.search_paths);
    mark_object_black(inst->modules.module_names);
    mark_object_black(inst->modules.modules);
    mark_object_black(inst->modules.module_inits);

    mark_object_black(inst->args);

    ctx = &inst->threads.main_thread;

    for ( ; ctx; ctx = ctx->next)
        mark_roots_in_context(ctx);
}

static void get_flat_list(KOS_PAGE *page, KOS_PAGE ***begin, KOS_PAGE ***end)
{
    uint8_t *const ptr       = (uint8_t *)page + KOS_BITMAP_OFFS;
    const uint32_t num_slots = KOS_min(page->num_slots, (uint32_t)KOS_SLOTS_PER_PAGE);

    *begin = (KOS_PAGE **)ptr;

    if (end)
        *end = (KOS_PAGE **)(ptr + KOS_BITMAP_SIZE + (num_slots << KOS_OBJ_ALIGN_BITS));
}

static void get_flat_page_list(KOS_HEAP  *heap,
                               KOS_PAGE **list,
                               KOS_PAGE **pages)
{
    KOS_PAGE  *page  = *pages;
    KOS_PAGE  *first = *pages;
    KOS_PAGE **dest;
    KOS_PAGE **begin;
    KOS_PAGE **end;

    assert(page);

    get_flat_list(first, &begin, &end);

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

            assert(this_size >= (KOS_SLOTS_OFFS + (KOS_PAGE_SIZE >> 3)));

            page->num_slots = (this_size - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;
            assert(page->num_slots == KOS_SLOTS_PER_PAGE);

            *(dest++) = page;

            page = (KOS_PAGE *)((uintptr_t)page + this_size);

            size -= this_size;
        }

        page = next;
    }

    KOS_atomic_write_u32(first->num_allocated, (unsigned)(dest - begin));

    first->next = *list;
    *list       = first;

    *pages = page;
}

static int sort_compare(const void *a, const void *b)
{
    KOS_PAGE *const pa = *(KOS_PAGE *const *)a;
    KOS_PAGE *const pb = *(KOS_PAGE *const *)b;

    return (pa < pb) ? -1 : (pa > pb) ?  1 : 0;
}

static void sort_flat_page_list(KOS_PAGE *list)
{
    KOS_PAGE **begin;

    get_flat_list(list, &begin, 0);

    qsort(begin, KOS_atomic_read_u32(list->num_allocated), sizeof(void *), sort_compare);
}

#ifdef CONFIG_MAD_GC
static void lock_pages(KOS_HEAP *heap, KOS_PAGE *pages)
{
    while (pages) {

        KOS_PAGE *page      = pages;
        uint32_t  num_slots = page->num_slots;
        uint32_t  i;

        pages = page->next;

        /* Only lock heap pages which are fully aligned with OS pages */
        assert(num_slots == KOS_SLOTS_PER_PAGE);
        kos_mem_protect(page, KOS_PAGE_SIZE, KOS_NO_ACCESS);

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

            if ( ! page)
                continue;

            assert(num_slots == KOS_SLOTS_PER_PAGE);
            kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE);

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

static unsigned push_sorted_list(KOS_HEAP *heap, KOS_PAGE *list)
{
    KOS_PAGE **begin;
    KOS_PAGE **end;
    KOS_PAGE **insert_at;
    KOS_PAGE  *page_at;
    unsigned   num_pages = 0;
#ifdef CONFIG_MAD_GC
    KOS_PAGE  *to_lock = 0;
#endif

    get_flat_list(list, &begin, 0);

    end = begin + KOS_atomic_read_u32(list->num_allocated);

#ifdef CONFIG_MAD_GC
    insert_at = &to_lock;
#else
    insert_at = &heap->free_pages;
#endif
    page_at   = *insert_at;

    while (begin < end) {

        KOS_PAGE *page = *(begin++);

        assert(page->num_slots == KOS_SLOTS_PER_PAGE);

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
    lock_pages(heap, to_lock);
#endif

    return num_pages;
}

static void reclaim_free_pages(KOS_HEAP     *heap,
                               KOS_PAGE     *free_pages,
                               KOS_GC_STATS *stats)
{
    KOS_PAGE *lists = 0;

    if ( ! free_pages)
        return;

    do
        get_flat_page_list(heap, &lists, &free_pages);
    while (free_pages);

    free_pages = lists;

    while (free_pages) {
        sort_flat_page_list(free_pages);
        free_pages = free_pages->next;
    }

    while (lists) {

        KOS_PAGE *next      = lists->next;
        unsigned  num_pages = push_sorted_list(heap, lists);

        stats->num_pages_freed += num_pages;

        lists = next;
    }
}

static int evacuate_object(KOS_CONTEXT     ctx,
                           KOS_OBJ_HEADER *hdr,
                           uint32_t        size)
{
    int             error = KOS_SUCCESS;
    const KOS_TYPE  type  = (KOS_TYPE)hdr->type;
    KOS_OBJ_HEADER *new_obj;

#ifdef CONFIG_MAD_GC
    if (size > (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))
        new_obj = (KOS_OBJ_HEADER *)alloc_huge_object(ctx, type, size);
    else
#endif
    {
        assert(size <= (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS));
        new_obj = (KOS_OBJ_HEADER *)alloc_object(ctx, type, size);
    }

    if (new_obj) {
        memcpy(new_obj, hdr, size);

        hdr->alloc_size = (KOS_OBJ_ID)((intptr_t)new_obj + 1);
    }
    else
        error = KOS_ERROR_EXCEPTION;

    return error;
}

static void update_child_ptr(KOS_OBJ_ID *obj_id_ptr)
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

static void update_child_ptrs(KOS_OBJ_HEADER *hdr)
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

                update_child_ptr(&new_ref_obj);

                delta = (intptr_t)new_ref_obj - (intptr_t)old_ref_obj;

                ((KOS_STRING *)hdr)->ref.obj_id   = new_ref_obj;
                ((KOS_STRING *)hdr)->ref.data_ptr = old_data_ptr + delta;
            }
            break;

        default:
            assert(hdr->type == OBJ_OBJECT);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->props);
            update_child_ptr(&((KOS_OBJECT *)hdr)->prototype);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->priv);
            break;

        case OBJ_ARRAY:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ARRAY *)hdr)->data);
            break;

        case OBJ_BUFFER:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_BUFFER *)hdr)->data);
            break;

        case OBJ_OBJECT_STORAGE:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_STORAGE *)hdr)->new_prop_table);
            {
                KOS_PITEM *item = &((KOS_OBJECT_STORAGE *)hdr)->items[0];
                KOS_PITEM *end  = item + ((KOS_OBJECT_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item) {
                    update_child_ptr((KOS_OBJ_ID *)&item->key);
                    update_child_ptr((KOS_OBJ_ID *)&item->value);
                }
            }
            break;

        case OBJ_ARRAY_STORAGE:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ARRAY_STORAGE *)hdr)->next);
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_ARRAY_STORAGE *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_ARRAY_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item)
                    update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_FUNCTION:
            update_child_ptr(&((KOS_FUNCTION *)hdr)->module);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->closures);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->defaults);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->generator_stack_frame);
            break;

        case OBJ_CLASS:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->prototype);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->props);
            update_child_ptr(&((KOS_CLASS *)hdr)->module);
            update_child_ptr(&((KOS_CLASS *)hdr)->closures);
            update_child_ptr(&((KOS_CLASS *)hdr)->defaults);
            break;

        case OBJ_DYNAMIC_PROP:
            update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->getter);
            update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->setter);
            break;

        case OBJ_OBJECT_WALK:
            update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->obj);
            update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->key_table);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_WALK *)hdr)->last_key);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT_WALK *)hdr)->last_value);
            break;

        case OBJ_MODULE:
            update_child_ptr(&((KOS_MODULE *)hdr)->name);
            update_child_ptr(&((KOS_MODULE *)hdr)->path);
            update_child_ptr(&((KOS_MODULE *)hdr)->constants);
            update_child_ptr(&((KOS_MODULE *)hdr)->global_names);
            update_child_ptr(&((KOS_MODULE *)hdr)->globals);
            update_child_ptr(&((KOS_MODULE *)hdr)->module_names);
            break;

        case OBJ_STACK:
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_STACK *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_STACK *)hdr)->size;
                for ( ; item < end; ++item)
                    update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_LOCAL_REFS:
            {
                KOS_OBJ_ID **ref = &((KOS_LOCAL_REFS *)hdr)->refs[0];
                KOS_OBJ_ID **end = ref + ((KOS_LOCAL_REFS *)hdr)->header.num_tracked;

                update_child_ptr(&((KOS_LOCAL_REFS *)hdr)->next);

                for ( ; ref < end; ++ref)
                    update_child_ptr(*ref);
            }
            break;

        case OBJ_THREAD:
            {
                update_child_ptr(&((KOS_THREAD *)hdr)->thread_func);
                update_child_ptr(&((KOS_THREAD *)hdr)->this_obj);
                update_child_ptr(&((KOS_THREAD *)hdr)->args_obj);
                update_child_ptr(&((KOS_THREAD *)hdr)->retval);
                update_child_ptr(&((KOS_THREAD *)hdr)->exception);
            }
            break;
    }
}

static void update_pages_after_evacuation(KOS_HEAP *heap)
{
    KOS_PAGE *page = get_next_page(heap, &heap->update_pages, 0);

    for ( ; page; page = get_next_page(heap, &heap->update_pages, page)) {

        uint8_t       *ptr = (uint8_t *)page + KOS_SLOTS_OFFS;
        uint8_t *const end = ptr + (get_num_active_slots(page) << KOS_OBJ_ALIGN_BITS);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);

            update_child_ptrs(hdr);

            ptr += size;
        }
    }
}

static void update_after_evacuation(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;
    KOS_HEAP           *heap = &inst->heap;

    KOS_atomic_write_ptr(heap->update_pages, get_pages_head(heap));

    assert( ! ctx->cur_page);

    update_pages_after_evacuation(heap);

    wait_for_walk_end(heap);

    /* Update object pointers in instance */

    kos_lock_mutex(&inst->threads.mutex);

    {
        int i;

        for (i = 0; i < KOS_STR_NUM; ++i)
            update_child_ptr(&inst->common_strings[i]);
    }

    update_child_ptr(&inst->prototypes.object_proto);
    update_child_ptr(&inst->prototypes.number_proto);
    update_child_ptr(&inst->prototypes.integer_proto);
    update_child_ptr(&inst->prototypes.float_proto);
    update_child_ptr(&inst->prototypes.string_proto);
    update_child_ptr(&inst->prototypes.boolean_proto);
    update_child_ptr(&inst->prototypes.array_proto);
    update_child_ptr(&inst->prototypes.buffer_proto);
    update_child_ptr(&inst->prototypes.function_proto);
    update_child_ptr(&inst->prototypes.class_proto);
    update_child_ptr(&inst->prototypes.generator_proto);
    update_child_ptr(&inst->prototypes.exception_proto);
    update_child_ptr(&inst->prototypes.generator_end_proto);
    update_child_ptr(&inst->prototypes.thread_proto);

    update_child_ptr(&inst->modules.init_module);
    update_child_ptr(&inst->modules.search_paths);
    update_child_ptr(&inst->modules.module_names);
    update_child_ptr(&inst->modules.modules);
    update_child_ptr(&inst->modules.module_inits);

    update_child_ptr(&inst->args);

    /* Update object pointers in thread contexts */

    ctx = &inst->threads.main_thread;

    while (ctx) {

        uint32_t i;

        assert(KOS_atomic_read_u32(ctx->gc_state) != GC_INACTIVE);

        update_child_ptr(&ctx->exception);
        update_child_ptr(&ctx->retval);
        update_child_ptr(&ctx->stack);
        update_child_ptr(&ctx->local_refs);

        for (i = 0; i < ctx->tmp_ref_count; ++i) {
            KOS_OBJ_ID *const obj_id_ptr = ctx->tmp_refs[i];
            if (obj_id_ptr)
                update_child_ptr(obj_id_ptr);
        }

        for (i = 0; i < ctx->helper_ref_count; ++i) {
            KOS_OBJ_ID *const obj_id_ptr = ctx->helper_refs[i];
            if (obj_id_ptr)
                update_child_ptr(obj_id_ptr);
        }

        ctx = ctx->next;
    }

    kos_unlock_mutex(&inst->threads.mutex);
}

static void finalize_object(KOS_CONTEXT     ctx,
                            KOS_OBJ_HEADER *hdr,
                            KOS_GC_STATS   *stats)
{
    if (hdr->type == OBJ_OBJECT) {

        KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

        if (obj->finalize) {

            obj->finalize(ctx, KOS_atomic_read_obj(obj->priv));

            ++stats->num_objs_finalized;
        }
    }
}

static int evacuate(KOS_CONTEXT   ctx,
                    KOS_PAGE    **free_pages,
                    KOS_GC_STATS *out_stats)
{
    KOS_HEAP   *heap           = get_heap(ctx);
    int         error          = KOS_SUCCESS;
    KOS_PAGE   *page           = heap->full_pages;
    KOS_PAGE   *non_full_pages = heap->non_full_pages;
    KOS_PAGE   *next;
    KOS_OBJ_ID  exception      = KOS_get_exception(ctx);
    int         non_full_turn  = 0;

    KOS_GC_STATS stats = *out_stats;

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
#if !defined(NDEBUG) || !defined(CONFIG_MAD_GC)
        const uint32_t num_allocated = KOS_atomic_read_u32(page->num_allocated);
#endif
#ifndef NDEBUG
        KOS_SLOT *page_end = ptr + num_allocated;
#endif
#ifndef CONFIG_MAD_GC
        const uint32_t num_slots_used = KOS_atomic_read_u32(page->num_used);
#endif

        heap->used_size -= non_full_turn ? non_full_page_size(page) : full_page_size(page);

        next = page->next;

        if ( ! next && non_full_pages) {
            next = non_full_pages;
            non_full_pages = 0;
            non_full_turn = 1;
        }

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

#ifndef CONFIG_MAD_GC
        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        #define UNUSED_SLOTS ((KOS_SLOTS_PER_PAGE * (100U - KOS_MIGRATION_THRESH)) / 100U)
        if (num_allocated - num_slots_used < UNUSED_SLOTS || num_slots_used > KOS_SLOTS_PER_PAGE) {

            gc_trace(("GC ctx=%p retain page %p\n", (void *)ctx, (void *)page));

            /* Mark unused objects as opaque so they don't participate in
             * pointer update after evacuation. */
            if (num_slots_used < num_allocated) {
                while (ptr < end) {

                    KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
                    const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
                    const uint32_t  color = get_marking(&mark_loc);

                    assert(size > 0U);
                    assert(color != GRAY);
                    assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

                    if ( ! color) {
                        finalize_object(ctx, hdr, &stats);
                        hdr->type = OBJ_OPAQUE;
                    }

                    advance_marking(&mark_loc, size >> KOS_OBJ_ALIGN_BITS);

                    ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
                }
            }

            if (is_page_full(page)) {
                PUSH_LIST(heap->full_pages, page);
                heap->used_size += full_page_size(page);
            }
            else {
                PUSH_LIST(heap->non_full_pages, page);
                heap->used_size += non_full_page_size(page);
            }
            ++stats.num_pages_kept;
            stats.size_kept += num_slots_used << KOS_OBJ_ALIGN_BITS;
            continue;
        }
#endif
        gc_trace(("GC ctx=%p -- evac page %p\n", (void *)ctx, (void *)page));

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t  color = get_marking(&mark_loc);

            assert(size > 0U);
            assert(color != GRAY);
            assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

            if (color) {
                if (evacuate_object(ctx, hdr, size)) {

                    KOS_clear_exception(ctx);

                    release_current_page_locked(ctx);

#ifdef CONFIG_MAD_GC
                    unlock_pages(heap);
#else
                    /* TODO find all free pages and make them available; a free
                     * page which had no objects to evacuate is marked with
                     * num_allocated == 0 */
                    assert(0);
#endif
                    error = evacuate_object(ctx, hdr, size);

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
                finalize_object(ctx, hdr, &stats);

                ++stats.num_objs_freed;
                stats.size_freed += size;
            }

            advance_marking(&mark_loc, size >> KOS_OBJ_ALIGN_BITS);

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
    *out_stats = stats;

    release_current_page_locked(ctx);

    if ( ! IS_BAD_PTR(exception))
        ctx->exception = exception;

    return error;
}

static void update_gc_threshold(KOS_HEAP *heap)
{
    heap->gc_threshold = heap->used_size + KOS_GC_STEP;
}

static void help_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap    = get_heap(ctx);
    int             engaged = 0;
    enum GC_STATE_E gc_state;

    gc_state = (enum GC_STATE_E)KOS_atomic_read_u32(heap->gc_state);

    if (gc_state == GC_INACTIVE)
        return;

    kos_heap_release_thread_page(ctx);

    for (;;) {

        assert( ! ctx->cur_page);

        switch (gc_state) {

            case GC_INACTIVE:
                kos_lock_mutex(&ctx->inst->threads.mutex);

                gc_state = (enum GC_STATE_E)KOS_atomic_read_u32(heap->gc_state);

                if (gc_state == GC_INACTIVE && engaged)
                    KOS_atomic_write_u32(ctx->gc_state, GC_INACTIVE);

                kos_unlock_mutex(&ctx->inst->threads.mutex);

                if (gc_state == GC_INACTIVE)
                    return;
                break;

#if 0
            case GC_MARK:
                gray_to_black_in_pages(heap);
                break;

            /* TODO help with evac */

            case GC_UPDATE:
                update_pages_after_evacuation(heap);
                break;
#endif

            default:
                if ( ! engaged) {
                    KOS_atomic_release_barrier();
                    KOS_atomic_write_u32(ctx->gc_state, GC_ENGAGED);
                    engaged = 1;
                }
                kos_yield();
                break;
        }

        KOS_atomic_acquire_barrier();

        gc_state = (enum GC_STATE_E)KOS_atomic_read_u32(heap->gc_state);
    }
}

void KOS_help_gc(KOS_CONTEXT ctx)
{
    assert(KOS_atomic_read_u32(ctx->gc_state) == GC_INACTIVE);

    if (KOS_atomic_read_u32(get_heap(ctx)->gc_state) >= GC_INIT)
        help_gc(ctx);
}

#ifdef CONFIG_MAD_GC
void kos_trigger_mad_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);

    /* Don't try to collect garbage when the garbage collector is running */
    if (KOS_atomic_read_u32(ctx->gc_state) != GC_INACTIVE)
        return;

    kos_lock_mutex(&heap->mutex);
    try_collect_garbage(ctx);
    kos_unlock_mutex(&heap->mutex);
}
#endif

void KOS_suspend_context(KOS_CONTEXT ctx)
{
    kos_heap_release_thread_page(ctx);
    KOS_atomic_write_u32(ctx->gc_state, GC_SUSPENDED);
}

void KOS_resume_context(KOS_CONTEXT ctx)
{
    enum GC_STATE_E gc_state;

    assert(KOS_atomic_read_u32(ctx->gc_state) == GC_SUSPENDED);

    kos_lock_mutex(&ctx->inst->threads.mutex);

    KOS_atomic_write_u32(ctx->gc_state, GC_INACTIVE);

    KOS_atomic_full_barrier();

    gc_state = (enum GC_STATE_E)KOS_atomic_read_u32(get_heap(ctx)->gc_state);

    kos_unlock_mutex(&ctx->inst->threads.mutex);

    if (gc_state >= GC_INIT)
        help_gc(ctx);
}

int KOS_collect_garbage(KOS_CONTEXT   ctx,
                        KOS_GC_STATS *out_stats)
{
    uint64_t     time_0;
    uint64_t     time_1;
    KOS_HEAP    *heap            = get_heap(ctx);
    KOS_PAGE    *free_pages      = 0;
    KOS_GC_STATS stats           = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                     0U, 0U, 0U, 0U };
    unsigned     num_gray_passes = 0U;
    int          error           = KOS_SUCCESS;

    /***********************************************************************/
    /* Phase 1: Initialize GC and clear marking */

    time_0 = kos_get_time_us();

    stats.initial_heap_size = heap->heap_size;
    stats.initial_used_size = heap->used_size;

    if ( ! KOS_atomic_cas_u32(heap->gc_state, GC_INACTIVE, GC_INIT)) {

        KOS_help_gc(ctx);

        return KOS_SUCCESS;
    }

    gc_trace(("GC ctx=%p begin cycle %u\n", (void *)ctx, KOS_atomic_read_u32(heap->gc_cycles)));

    KOS_atomic_write_u32(heap->gc_cycles, KOS_atomic_read_u32(heap->gc_cycles) + 1U);

    KOS_atomic_write_u32(ctx->gc_state, GC_ENGAGED);

    kos_heap_release_thread_page(ctx);

    verify_heap_used_size(heap);

    clear_marking(heap);

    /***********************************************************************/
    /* Phase 2: Perform marking */

    kos_lock_mutex(&ctx->inst->threads.mutex);

    stop_the_world(ctx->inst); /* Remaining threads enter help_gc() */

    mark_roots(ctx);

    KOS_atomic_release_barrier();
    KOS_atomic_write_u32(heap->gc_state, GC_MARK);

    kos_unlock_mutex(&ctx->inst->threads.mutex);

    do ++num_gray_passes;
    while (gray_to_black(heap));

    /***********************************************************************/
    /* Phase 3: Evacuate */

    KOS_atomic_release_barrier();
    KOS_atomic_write_u32(heap->gc_state, GC_EVACUATE);

    error = evacuate(ctx, &free_pages, &stats);

    KOS_atomic_release_barrier();
    KOS_atomic_write_u32(heap->gc_state, GC_UPDATE);

    update_after_evacuation(ctx);

    /***********************************************************************/
    /* Phase 4: Reclaim free pages */

    reclaim_free_pages(heap, free_pages, &stats);

    /***********************************************************************/
    /* Phase 5: Finish GC */

    update_gc_threshold(heap);

    stats.num_gray_passes = num_gray_passes;
    stats.heap_size       = heap->heap_size;
    stats.used_size       = heap->used_size;

    gc_trace(("GC ctx=%p end cycle\n", (void *)ctx));

    KOS_atomic_write_u32(ctx->gc_state, GC_INACTIVE);

    KOS_atomic_release_barrier();
    KOS_atomic_write_u32(heap->gc_state, GC_INACTIVE);

    if ( ! error && KOS_is_exception_pending(ctx))
        error = KOS_ERROR_EXCEPTION;

    time_1 = kos_get_time_us();

    stats.time_us = (unsigned)(time_1 - time_0);

    if (ctx->inst->flags & KOS_INST_DEBUG) {
        printf("GC used/total [B] %x/%x -> %x/%x : time %u us : gray passes %u\n",
               stats.initial_used_size, stats.initial_heap_size,
               stats.used_size, stats.heap_size,
               stats.time_us, stats.num_gray_passes);
    }

    verify_heap_used_size(heap);

    if (out_stats)
        *out_stats = stats;

    return error;
}

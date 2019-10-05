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
#include "kos_const_strings.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system.h"
#include "kos_threads_internal.h"
#include "kos_try.h"
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

static void update_child_ptr(KOS_OBJ_ID *obj_id_ptr);

static void help_gc(KOS_CONTEXT ctx);

#ifdef CONFIG_MAD_GC
#define KOS_MAX_LOCKED_PAGES 256

struct KOS_LOCKED_PAGES_S {
    struct KOS_LOCKED_PAGES_S *next;
    uint32_t                   num_pages;
    KOS_PAGE                  *pages[KOS_MAX_LOCKED_PAGES];
};
#endif

static int is_page_full(KOS_PAGE *page)
{
    return KOS_atomic_read_relaxed_u32(page->num_allocated) == KOS_SLOTS_PER_PAGE;
}

static uint32_t used_page_size(KOS_PAGE *page)
{
    return KOS_SLOTS_OFFS + (KOS_atomic_read_relaxed_u32(page->num_allocated) << KOS_OBJ_ALIGN_BITS);
}

static void push_page_back(KOS_PAGE_LIST *list, KOS_PAGE *page)
{
    page->next = 0;

    if (list->tail)
        list->tail->next = page;
    else {
        assert( ! list->head);
        list->head = page;
    }

    list->tail = page;
}

static void push_page(KOS_PAGE_LIST *list, KOS_PAGE *page)
{
    /* Non-full pages are put in front, full pages go in the back */
    if (is_page_full(page))
        push_page_back(list, page);

    else {
        KOS_PAGE *old_head = list->head;

        page->next = old_head;
        list->head = page;

        if ( ! old_head)
            list->tail = page;
    }
}

#ifndef NDEBUG
KOS_TYPE kos_get_object_type_gc_safe(KOS_OBJ_ID obj)
{
    KOS_OBJ_HEADER *hdr;

    if ( ! kos_is_heap_object(obj))
        return GET_OBJ_TYPE(obj);

    hdr = (KOS_OBJ_HEADER *)((intptr_t)obj - 1);

    if ( ! kos_is_heap_object(hdr->size_and_type))
        return kos_get_object_type(*hdr);

    hdr = (KOS_OBJ_HEADER *)((intptr_t)hdr->size_and_type - 1);

    return kos_get_object_type(*hdr);
}
#endif

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

    KOS_atomic_write_release_u32(inst->threads.main_thread.gc_state, GC_INACTIVE);
    KOS_atomic_write_relaxed_u32(heap->gc_state, GC_INACTIVE);
    heap->heap_size       = 0;
    heap->used_heap_size  = 0;
    heap->malloc_size     = 0;
    heap->max_heap_size   = KOS_MAX_HEAP_SIZE;
    heap->max_malloc_size = KOS_MAX_HEAP_SIZE;
    heap->gc_threshold    = KOS_GC_STEP;
    heap->free_pages      = 0;
    heap->used_pages.head = 0;
    heap->used_pages.tail = 0;
    heap->pools           = 0;

    KOS_atomic_write_relaxed_ptr(heap->gray_pages,   (KOS_PAGE *)0);
    KOS_atomic_write_relaxed_ptr(heap->update_pages, (KOS_PAGE *)0);
    KOS_atomic_write_relaxed_u32(heap->gray_marked,  0U);
    KOS_atomic_write_relaxed_u32(heap->walk_stage,   0U);
    KOS_atomic_write_relaxed_u32(heap->walk_active,  0U);
    KOS_atomic_write_relaxed_u32(heap->gc_cycles,    0U);

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
    return KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE ||
           KOS_atomic_read_relaxed_u32(get_heap(ctx)->gc_state) >= GC_MARK;
}
#endif

static void finalize_object(KOS_CONTEXT     ctx,
                            KOS_OBJ_HEADER *hdr,
                            KOS_GC_STATS   *stats)
{
    const KOS_TYPE type = kos_get_object_type(*hdr);

    switch (type) {

        case OBJ_OBJECT: {

            KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

            if (obj->finalize) {

                obj->finalize(ctx, KOS_atomic_read_relaxed_ptr(obj->priv));

                assert( ! KOS_is_exception_pending(ctx));

                obj->finalize = 0;
                KOS_atomic_write_relaxed_ptr(obj->priv, (void *)0);

                ++stats->num_objs_finalized;
            }
            break;
        }

        case OBJ_HUGE_TRACKER: {

            KOS_HUGE_TRACKER *obj = (KOS_HUGE_TRACKER *)hdr;

            if (obj->data) {

                gc_trace(("free huge %p\n", (void *)obj->data));

                kos_free_aligned(obj->data);

                get_heap(ctx)->malloc_size -= obj->size;

                stats->size_freed += obj->size;

                obj->data   = 0;
                obj->object = KOS_BADPTR;
                obj->size   = 0;
            }
            break;
        }

        default:
            break;
    }
}

static KOS_SLOT *get_slots(KOS_PAGE *page)
{
    return (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS);
}

static void finalize_objects(KOS_CONTEXT ctx,
                             KOS_HEAP   *heap)
{
    KOS_PAGE    *page     = heap->used_pages.head;
    KOS_GC_STATS gc_stats = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                              0U, 0U, 0U, 0U, 0U, 0U };

    for ( ; page; page = page->next) {

        KOS_SLOT *ptr = get_slots(page);
        KOS_SLOT *end = ptr + KOS_atomic_read_relaxed_u32(page->num_allocated);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size = kos_get_object_size(*hdr);

            assert(size > 0U);
            assert(size <= (size_t)((uint8_t *)page - (uint8_t *)ptr));

            finalize_object(ctx, hdr, &gc_stats);

            ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
        }
    }
}

void kos_heap_destroy(KOS_INSTANCE *inst)
{
    assert(inst->threads.main_thread.prev == 0);
    assert(inst->threads.main_thread.next == 0);

    /* Disable GC */
    inst->flags |= KOS_INST_MANUAL_GC;

    kos_heap_release_thread_page(&inst->threads.main_thread);

#ifdef CONFIG_MAD_GC
    {
        struct KOS_LOCKED_PAGES_S *locked_pages = inst->heap.locked_pages_first;

        while (locked_pages) {

            uint32_t                   i;
            struct KOS_LOCKED_PAGES_S *del;

            for (i = 0; i < locked_pages->num_pages; i++) {

                KOS_PAGE *const page = locked_pages->pages[i];
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
        void     *memory;
        KOS_POOL *pool = inst->heap.pools;

        if ( ! pool)
            break;

        inst->heap.pools = pool->next;

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

            KOS_PAGE *page = locked_pages->pages[i];

            if (page) {
                locked_pages->pages[i] = 0;

                kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE);

                page->next = 0;

                return page;
            }
        }

        locked_pages = locked_pages->next;
    }

    return 0;
}
#endif

enum KOS_WALK_STAGE_E {
    WALK_DONE,
    WALK_ACTIVE
};

static KOS_PAGE *get_next_page(KOS_HEAP               *heap,
                               KOS_ATOMIC(KOS_PAGE *) *page_ptr)
{
    KOS_PAGE *page = 0;

    for (;;) {

        enum KOS_WALK_STAGE_E stage;
        KOS_PAGE             *next;

        page = (KOS_PAGE *)KOS_atomic_read_relaxed_ptr(*page_ptr);

        if ( ! page)
            break;

        next = page->next;

        if (next) {
            if (KOS_atomic_cas_weak_ptr(*page_ptr, page, next))
                break;
            else
                continue;
        }

        /* TODO try to replace all this below with atomic_write(walk_stage, WALK_DONE) */
        kos_lock_mutex(&heap->mutex);

        stage = (enum KOS_WALK_STAGE_E)KOS_atomic_read_relaxed_u32(heap->walk_stage);

        if (page != (KOS_PAGE *)KOS_atomic_read_relaxed_ptr(*page_ptr)) {
            kos_unlock_mutex(&heap->mutex);
            continue;
        }

        if (stage == WALK_ACTIVE) {
            KOS_atomic_write_relaxed_ptr(*page_ptr, (KOS_PAGE *)0);
            KOS_atomic_write_relaxed_u32(heap->walk_stage, WALK_DONE);

            kos_unlock_mutex(&heap->mutex);
            break;
        }

        kos_unlock_mutex(&heap->mutex);
    }

    return page;
}

static KOS_PAGE *begin_page_walk(KOS_HEAP               *heap,
                                 KOS_ATOMIC(KOS_PAGE *) *page_ptr)
{
    KOS_atomic_add_u32(heap->walk_active, 1);

    return get_next_page(heap, page_ptr);
}

static void end_page_walk(KOS_HEAP *heap)
{
    assert(KOS_atomic_read_relaxed_u32(heap->walk_active) > 0U);

    KOS_atomic_add_u32(heap->walk_active, (uint32_t)-1);
}

static void wait_for_walk_end(KOS_HEAP *heap)
{
    while (KOS_atomic_read_acquire_u32(heap->walk_active))
        kos_yield();
}

static KOS_POOL *alloc_pool(KOS_HEAP *heap,
                            uint32_t  alloc_size)
{
    KOS_POOL *pool_hdr;
    uint8_t  *pool;

    if (heap->heap_size + alloc_size > heap->max_heap_size)
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

    pool_hdr->next = heap->pools;
    heap->pools    = pool_hdr;

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

        KOS_atomic_write_relaxed_u32(page->num_allocated, 0);

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

            assert(KOS_atomic_read_relaxed_u32(page->num_allocated) == 0);

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
    if ( ! page && (heap->heap_size + KOS_POOL_SIZE > heap->max_heap_size))
        page = unlock_one_page(heap);
#endif

    if ( ! page) {

        if (alloc_page_pool(heap))
            return 0;

        page = heap->free_pages;

        assert(page);
    }

    gc_trace(("alloc page %p\n", (void *)page));

    assert(KOS_atomic_read_relaxed_u32(page->num_allocated) == 0);

    heap->free_pages = page->next;

    KOS_PERF_CNT(alloc_free_page);

    KOS_atomic_write_relaxed_u32(page->num_used, 0U);
    page->next = 0;
    return page;
}

static int collect_garbage_last_resort_locked(KOS_CONTEXT ctx, KOS_GC_STATS *stats)
{
    KOS_HEAP *const heap = get_heap(ctx);
    int             error;

    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        return KOS_SUCCESS;

    kos_unlock_mutex(&heap->mutex);

    error = KOS_collect_garbage(ctx, stats);

    kos_lock_mutex(&heap->mutex);

    return error;
}

static int try_collect_garbage(KOS_CONTEXT ctx)
{
#ifndef CONFIG_MAD_GC
    KOS_HEAP *const heap  = get_heap(ctx);
#endif
    int             error = KOS_SUCCESS;

    /* Don't try to collect garbage when the garbage collector is running */
    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        return KOS_SUCCESS;

    if (ctx->inst->flags & KOS_INST_MANUAL_GC)
        return error;

#ifndef CONFIG_MAD_GC
    if (heap->used_heap_size + heap->malloc_size > heap->gc_threshold ||
        heap->malloc_size > heap->max_malloc_size)
#endif
    {
        KOS_GC_STATS stats;

        error = collect_garbage_last_resort_locked(ctx, &stats);

        if ( ! error && stats.heap_size)
            error = KOS_SUCCESS_RETURN;
    }

    return error;
}

static void *alloc_slots_from_page(KOS_PAGE *page, uint32_t num_slots)
{
    const uint32_t num_allocated = KOS_atomic_read_relaxed_u32(page->num_allocated);
    uint32_t       new_num_slots = num_allocated + num_slots;
    KOS_SLOT      *slot          = 0;

    assert(num_slots > 0);

    if (new_num_slots <= KOS_SLOTS_PER_PAGE) {

        slot = get_slots(page) + num_allocated;

        assert(slot == (KOS_SLOT *)((uint8_t *)page + KOS_SLOTS_OFFS + (num_allocated << KOS_OBJ_ALIGN_BITS)));

        KOS_atomic_write_relaxed_u32(page->num_allocated, new_num_slots);
    }

    return slot;
}

static KOS_OBJ_HEADER *alloc_object_from_page(KOS_PAGE *page,
                                              KOS_TYPE  object_type,
                                              uint32_t  num_slots)
{
    KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)alloc_slots_from_page(page, num_slots);

    if (hdr) {
        kos_set_object_type_size(*hdr,
                                 object_type,
                                 num_slots << KOS_OBJ_ALIGN_BITS);

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

static void push_page_with_objects(KOS_HEAP *heap, KOS_PAGE *page)
{
    push_page(&heap->used_pages, page);
    heap->used_heap_size += used_page_size(page);
}

static void release_current_page_locked(KOS_CONTEXT ctx)
{
    KOS_PAGE *page = ctx->cur_page;

    if (page) {

        KOS_HEAP *heap = get_heap(ctx);

        assert(KOS_atomic_read_relaxed_u32(heap->gc_state) == GC_INACTIVE ||
               KOS_atomic_read_relaxed_u32(ctx->gc_state)  == GC_ENGAGED ||
               KOS_atomic_read_relaxed_u32(heap->gc_state) == GC_INIT);

        assert(KOS_atomic_read_relaxed_u32(page->num_used) == 0U);

        gc_trace(("release cur page %p ctx=%p\n", (void *)page, (void *)ctx));

        push_page_with_objects(heap, page);

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

    for (page = heap->used_pages.head; page; page = page->next)
        used_size += used_page_size(page);

    assert(used_size == heap->used_heap_size);

    kos_unlock_mutex(&heap->mutex);
}
#else
#define verify_heap_used_size(heap) ((void)0)
#endif

static void *alloc_object(KOS_CONTEXT ctx,
                          KOS_TYPE    object_type,
                          uint32_t    size)
{
    KOS_PAGE       *page       = ctx->cur_page;
    const uint32_t  num_slots  = (size + sizeof(KOS_SLOT) - 1) >> KOS_OBJ_ALIGN_BITS;
    unsigned        seek_depth = KOS_MAX_PAGE_SEEK;
    KOS_HEAP       *heap;
    KOS_PAGE      **page_ptr;
    KOS_PAGE       *old_page;
    KOS_PAGE       *prev_page;
    KOS_OBJ_HEADER *hdr        = 0;

    assert(num_slots <= KOS_SLOTS_PER_PAGE);
    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_SUSPENDED);

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
        const uint32_t gc_state = KOS_atomic_read_relaxed_u32(heap->gc_state);

        if (gc_state != GC_INACTIVE && KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE) {

            kos_unlock_mutex(&heap->mutex);
            help_gc(ctx);
            kos_lock_mutex(&heap->mutex);

            assert( ! ctx->cur_page);
            page = 0;
        }
    }

    /* Check if any of the non-full page contains enough space */

    page_ptr  = &heap->used_pages.head;
    prev_page = 0;

    for ( ; seek_depth--; prev_page = old_page, page_ptr = &old_page->next) {

        uint32_t num_allocated;

        old_page = *page_ptr;

        if ( ! old_page)
            break;

        num_allocated = KOS_atomic_read_relaxed_u32(old_page->num_allocated);

        if (num_allocated == KOS_SLOTS_PER_PAGE)
            break;

        if (num_slots > KOS_SLOTS_PER_PAGE - num_allocated)
            continue;

        hdr = alloc_object_from_page(old_page, object_type, num_slots);

        if ( ! hdr)
            continue;

        heap->used_heap_size += num_slots << KOS_OBJ_ALIGN_BITS;

        /* Move full page to the back of the list */
        if (is_page_full(old_page)) {
            if (old_page->next) {
                *page_ptr = old_page->next;
                push_page(&heap->used_pages, old_page);
            }
        }
        /* If the page is still not full and has more room than current page,
         * then use it as current page */
        else if ( ! page || (page && KOS_atomic_read_relaxed_u32(page->num_allocated)
                                     > KOS_atomic_read_relaxed_u32(old_page->num_allocated))) {

            KOS_PAGE *next_page = old_page->next;

            *page_ptr = next_page;

            if ( ! next_page)
                heap->used_pages.tail = prev_page;

            heap->used_heap_size -= used_page_size(old_page);

            if (page) {
                assert(page == ctx->cur_page);
                push_page_with_objects(heap, page);
            }

            KOS_atomic_write_relaxed_u32(old_page->num_used, 0U);
            old_page->next = 0;
            ctx->cur_page  = old_page;
        }

        break;
    }

    if ( ! hdr) {

        int error;

        release_current_page_locked(ctx);

        error = try_collect_garbage(ctx);

        if (error && error != KOS_SUCCESS_RETURN) {
            KOS_clear_exception(ctx);
            kos_unlock_mutex(&heap->mutex);
            return 0;
        }

        /* Allocate a new page */
        page = alloc_page(heap);

        /* If failed, collect garbage and try again */
        if ( ! page && ! error) {
            error = collect_garbage_last_resort_locked(ctx, 0);

            if (error)
                KOS_clear_exception(ctx);

            page = alloc_page(heap);
        }

        if (page) {

            ctx->cur_page = page;

            hdr = alloc_object_from_page(page, object_type, num_slots);

            assert(hdr);
        }
    }

    kos_unlock_mutex(&heap->mutex);

    return hdr;
}

static void *alloc_huge_object(KOS_CONTEXT ctx,
                               KOS_TYPE    object_type,
                               uint32_t    size)
{
    KOS_OBJ_HEADER   *hdr = 0;
    KOS_HUGE_TRACKER *tracker;
    KOS_OBJ_ID        tracker_obj;
    KOS_HEAP         *heap;
    intptr_t          ptrval;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_SUSPENDED);

    size += KOS_OBJ_TRACK_BIT;

    heap = get_heap(ctx);

    tracker = (KOS_HUGE_TRACKER *)alloc_object(ctx, OBJ_HUGE_TRACKER, sizeof(KOS_HUGE_TRACKER));

    if ( ! tracker)
        return 0;

    tracker->data   = 0;
    tracker->object = KOS_BADPTR;
    tracker->size   = 0;

    tracker_obj = OBJID(HUGE_TRACKER, tracker);

    kos_track_refs(ctx, 1, &tracker_obj);

    kos_lock_mutex(&heap->mutex);

    if (heap->malloc_size + size > heap->max_malloc_size) {

        const int error = collect_garbage_last_resort_locked(ctx, 0);

        if (error)
            KOS_clear_exception(ctx);

        if (heap->malloc_size + size > heap->max_malloc_size)
            goto cleanup;
    }

    ptrval = (intptr_t)kos_malloc_aligned(size, 32);

    if ( ! ptrval)
        goto cleanup;

    gc_trace(("alloc huge %p\n", (void *)ptrval));

    OBJPTR(HUGE_TRACKER, tracker_obj)->size = size;
    OBJPTR(HUGE_TRACKER, tracker_obj)->data = (void *)ptrval;

    hdr = (KOS_OBJ_HEADER *)(ptrval + KOS_OBJ_TRACK_BIT);

    *(KOS_OBJ_ID *)((intptr_t)hdr - sizeof(KOS_OBJ_ID)) = tracker_obj;

    kos_set_object_type_size(*hdr, object_type, size - KOS_OBJ_TRACK_BIT);

    OBJPTR(HUGE_TRACKER, tracker_obj)->object = (KOS_OBJ_ID)((intptr_t)hdr + 1);

    heap->malloc_size += size;

    assert(kos_is_heap_object(tracker_obj));
    assert(kos_is_tracked_object((KOS_OBJ_ID)((intptr_t)hdr + 1)));
    assert( ! kos_is_heap_object((KOS_OBJ_ID)((intptr_t)hdr + 1)));

    KOS_PERF_CNT(alloc_huge_object);

cleanup:
    kos_unlock_mutex(&heap->mutex);

    kos_untrack_refs(ctx, 1);

    return hdr;
}

void *kos_alloc_object(KOS_CONTEXT ctx,
                       KOS_TYPE    object_type,
                       uint32_t    size)
{
    void *obj;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    if (kos_trigger_mad_gc(ctx))
        return 0;

    if (kos_seq_fail()) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return 0;
    }

    if (size > KOS_MAX_HEAP_OBJ_SIZE)
        obj = alloc_huge_object(ctx, object_type, size);
    else
        obj = alloc_object(ctx, object_type, size);

    if ( ! obj)
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

#ifdef CONFIG_PERF
    if (size <= 32)
        KOS_PERF_CNT(alloc_object_size[0]);
    else if (size <= 128)
        KOS_PERF_CNT(alloc_object_size[1]);
    else if (size <= 512)
        KOS_PERF_CNT(alloc_object_size[2]);
    else
        KOS_PERF_CNT(alloc_object_size[3]);
#endif

    return obj;
}

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type)
{
    /* TODO actually allocate the page and set up the object in it */
    void *obj = alloc_object(ctx, object_type, KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS);

    if ( ! obj)
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    return obj;
}

#if 0
static void print_heap(KOS_HEAP *heap)
{
    unsigned  used_pages = 0;
    unsigned  free_pages = 0;
    KOS_PAGE *page       = heap->used_pages.head;

    for ( ; page; page = page->next) {
        ++used_pages;
        printf("used page %p, %u/%u slots allocated, %u slots used\n",
               (void *)page,
               KOS_atomic_read_relaxed_u32(page->num_allocated),
               (unsigned)KOS_SLOTS_PER_PAGE,
               KOS_atomic_read_relaxed_u32(page->num_used));
    }

    page = heap->free_pages;

    for ( ; page; page = page->next) {
        ++free_pages;
        printf("free page %p\n", (void *)page);
    }

    printf("total %u pages used, %u pages free\n", used_pages, free_pages);
}
#endif

static void stop_the_world(KOS_INSTANCE *inst)
{
    KOS_CONTEXT ctx = &inst->threads.main_thread;

    KOS_atomic_full_barrier();

    while (ctx) {

        const enum GC_STATE_E gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(ctx->gc_state);

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

static void set_marking_in_pages(KOS_PAGE             *page,
                                 enum KOS_MARK_STATE_E state,
                                 uint32_t              num_used)
{
    while (page) {

        uint32_t *const bitmap = (uint32_t *)((uint8_t *)page + KOS_BITMAP_OFFS);

        memset(bitmap, state * 0x55, KOS_BITMAP_SIZE);

        KOS_atomic_write_relaxed_u32(page->num_used, num_used);

        page = page->next;
    }
}

static void clear_marking(KOS_HEAP *heap)
{
    set_marking_in_pages(heap->used_pages.head, WHITE, 0);
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
    const uint32_t marking = KOS_atomic_read_relaxed_u32(*(mark_loc->bitmap));

    return (marking >> mark_loc->mask_idx) & COLORMASK;
}

static void set_mark_state_loc(struct KOS_MARK_LOC_S mark_loc,
                               enum KOS_MARK_STATE_E state)
{
    const uint32_t mask = (uint32_t)state << mark_loc.mask_idx;

    uint32_t value = KOS_atomic_read_relaxed_u32(*mark_loc.bitmap);

    while ( ! (value & mask)) {

        if (KOS_atomic_cas_weak_u32(*mark_loc.bitmap, value, value | mask))
            break;

        value = KOS_atomic_read_relaxed_u32(*mark_loc.bitmap);
    }
}

static void clear_mark_state(KOS_OBJ_ID obj_id)
{
    struct KOS_MARK_LOC_S mark_lock = get_mark_location(obj_id);

    const uint32_t mask = ~((uint32_t)COLORMASK << mark_lock.mask_idx);

    const uint32_t value = KOS_atomic_read_relaxed_u32(*mark_lock.bitmap);

    KOS_atomic_write_relaxed_u32(*mark_lock.bitmap, value & mask);
}

static void set_mark_state(KOS_OBJ_ID            obj_id,
                           enum KOS_MARK_STATE_E state)
{
    assert(((uintptr_t)obj_id & 0xFFFFFFFFU) != 0xDDDDDDDDU);

    /* TODO can we get rid of IS_BAD_PTR ? */
    if (kos_is_tracked_object(obj_id) && ! IS_BAD_PTR(obj_id)) {

        struct KOS_MARK_LOC_S mark_loc;

        if (kos_is_heap_object(obj_id))

            mark_loc = get_mark_location(obj_id);

        else {

            const KOS_OBJ_ID back_ref = *(KOS_OBJ_ID *)((intptr_t)obj_id - 1 - sizeof(KOS_OBJ_ID));

            assert(kos_is_heap_object(back_ref));

            assert(READ_OBJ_TYPE(back_ref) == OBJ_HUGE_TRACKER);

            mark_loc = get_mark_location(back_ref);
        }

        set_mark_state_loc(mark_loc, state);
    }
}

/* TODO Explore returning the number of objects turned to gray.
 *      This could help get rid of the last gray-to-black pass. */
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
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT, obj_id)->props), GRAY);
            set_mark_state(OBJPTR(OBJECT, obj_id)->prototype, GRAY);
            break;

        case OBJ_ARRAY:
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY, obj_id)->data), GRAY);
            break;

        case OBJ_BUFFER:
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data), GRAY);
            break;

        case OBJ_FUNCTION:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(FUNCTION, obj_id)->module,   GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->closures, GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->defaults, GRAY);
            set_mark_state(OBJPTR(FUNCTION, obj_id)->generator_stack_frame, GRAY);
            break;

        case OBJ_CLASS:
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->prototype), GRAY);
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->props), GRAY);
            /* TODO make these atomic */
            set_mark_state(OBJPTR(CLASS, obj_id)->module,   GRAY);
            set_mark_state(OBJPTR(CLASS, obj_id)->closures, GRAY);
            set_mark_state(OBJPTR(CLASS, obj_id)->defaults, GRAY);
            break;

        case OBJ_OBJECT_STORAGE: {
            KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id)->items[0];
            KOS_PITEM *end  = item + OBJPTR(OBJECT_STORAGE, obj_id)->capacity;
            for ( ; item < end; ++item) {
                set_mark_state(KOS_atomic_read_relaxed_obj(item->key), GRAY);
                set_mark_state(KOS_atomic_read_relaxed_obj(item->value), GRAY);
            }

            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, obj_id)->new_prop_table), GRAY);
            break;
        }

        case OBJ_ARRAY_STORAGE: {
            KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(ARRAY_STORAGE, obj_id)->buf[0];
            KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(ARRAY_STORAGE, obj_id)->capacity;
            for ( ; item < end; ++item)
                set_mark_state(KOS_atomic_read_relaxed_obj(*item), GRAY);

            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, obj_id)->next), GRAY);
            break;
        }

        case OBJ_DYNAMIC_PROP:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->getter, GRAY);
            set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->setter, GRAY);
            break;

        case OBJ_OBJECT_WALK:
            /* TODO make these atomic */
            set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->obj,         GRAY);
            set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->key_table,   GRAY);
            set_mark_state(KOS_atomic_read_relaxed_obj(
                           OBJPTR(OBJECT_WALK, obj_id)->last_key),   GRAY);
            set_mark_state(KOS_atomic_read_relaxed_obj(
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

        case OBJ_STACK: {
            KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(STACK, obj_id)->buf[0];
            KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(STACK, obj_id)->size;
            for ( ; item < end; ++item)
                set_mark_state(KOS_atomic_read_relaxed_obj(*item), GRAY);
            break;
        }

        case OBJ_LOCAL_REFS: {
            KOS_OBJ_ID **ref = &OBJPTR(LOCAL_REFS, obj_id)->refs[0];
            KOS_OBJ_ID **end = ref + OBJPTR(LOCAL_REFS, obj_id)->num_tracked;

            set_mark_state(OBJPTR(LOCAL_REFS, obj_id)->next, GRAY);

            for ( ; ref < end; ++ref)
                set_mark_state(**ref, GRAY);
            break;
        }

        case OBJ_HUGE_TRACKER: {
            KOS_OBJ_ID object = OBJPTR(HUGE_TRACKER, obj_id)->object;

            if ( ! IS_BAD_PTR(object)) {
                assert(kos_is_tracked_object(object));
                assert( ! kos_is_heap_object(object));
                mark_children_gray(object);
            }
            break;
        }
    }
}

static void mark_object_black(KOS_OBJ_ID obj_id)
{
    if (kos_is_tracked_object(obj_id)) {

        assert( ! IS_BAD_PTR(obj_id));

        set_mark_state(obj_id, BLACK);

        mark_children_gray(obj_id);
    }
}

static void gray_to_black_in_pages(KOS_HEAP *heap)
{
    int32_t   marked = 0;
    KOS_PAGE *page   = begin_page_walk(heap, &heap->gray_pages);

    for ( ; page; page = get_next_page(heap, &heap->gray_pages)) {

        uint32_t num_slots_used = 0;

        struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

        KOS_SLOT *ptr = get_slots(page);
        KOS_SLOT *end = ptr + KOS_atomic_read_relaxed_u32(page->num_allocated);

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t        size  = kos_get_object_size(*hdr);
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

        assert(num_slots_used <= KOS_atomic_read_relaxed_u32(page->num_allocated));
        assert(num_slots_used >= KOS_atomic_read_relaxed_u32(page->num_used));

        KOS_atomic_write_relaxed_u32(page->num_used, num_slots_used);
    }

    KOS_atomic_add_u32(heap->gray_marked, marked);

    end_page_walk(heap);
}

static uint32_t gray_to_black(KOS_HEAP *heap)
{
    KOS_atomic_write_relaxed_u32(heap->gray_marked, 0);
    KOS_atomic_write_relaxed_ptr(heap->gray_pages,  heap->used_pages.head);
    KOS_atomic_write_relaxed_u32(heap->walk_stage,  WALK_ACTIVE);

    gray_to_black_in_pages(heap);

    wait_for_walk_end(heap);

    return KOS_atomic_read_relaxed_u32(heap->gray_marked);
}

static void mark_roots_in_context(KOS_CONTEXT ctx)
{
    uint32_t i;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE);

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

static void mark_roots_in_threads(KOS_INSTANCE *inst)
{
    uint32_t       i;
    const uint32_t max_threads = inst->threads.max_threads;

    for (i = 0; i < max_threads; i++)
    {
        KOS_THREAD *thread = (KOS_THREAD *)KOS_atomic_read_relaxed_ptr(inst->threads.threads[i]);

        if ( ! thread)
            continue;

        set_mark_state(thread->thread_func, GRAY);
        set_mark_state(thread->this_obj,    GRAY);
        set_mark_state(thread->args_obj,    GRAY);
        set_mark_state(thread->retval,      GRAY);
        set_mark_state(thread->exception,   GRAY);
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

    mark_roots_in_threads(inst);

    mark_object_black(inst->args);

    ctx = &inst->threads.main_thread;

    for ( ; ctx; ctx = ctx->next)
        mark_roots_in_context(ctx);
}

#ifdef CONFIG_MAD_GC
static void lock_pages(KOS_HEAP *heap, KOS_PAGE *pages)
{
    while (pages) {

        KOS_PAGE *page = pages;
        uint32_t  i;

        pages = page->next;

        /* Only lock heap pages which are fully aligned with OS pages */
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

        heap->locked_pages_last->pages[i] = page;
    }
}
#endif

#ifdef NDEBUG
#define debug_fill_slots(page) ((void)0)
#else
static void debug_fill_slots(KOS_PAGE* page)
{
    memset(((uint8_t *)page) + KOS_BITMAP_OFFS,
           0xDDU,
           KOS_PAGE_SIZE - KOS_BITMAP_OFFS);
}
#endif

static void reclaim_free_pages(KOS_HEAP      *heap,
                               KOS_PAGE_LIST *free_pages,
                               KOS_GC_STATS  *stats)
{
    KOS_PAGE *tail     = 0;
    unsigned  num_free = 1;

    tail = free_pages->head;

    if ( ! tail)
        return;

    for (;;) {
        KOS_PAGE *next;

        debug_fill_slots(tail);

        KOS_atomic_write_relaxed_u32(tail->num_allocated, 0);

        next = tail->next;
        if ( ! next)
            break;

        ++num_free;
        tail = next;
    }

#ifdef CONFIG_MAD_GC
    lock_pages(heap, free_pages->head);
#else
    tail->next       = heap->free_pages;
    heap->free_pages = free_pages->head;
#endif

    free_pages->head = 0;
    free_pages->tail = 0;

    stats->num_pages_freed += num_free;
}

#ifdef CONFIG_MAD_GC
static unsigned unlock_pages(KOS_HEAP      *heap,
                             KOS_PAGE_LIST *free_pages,
                             KOS_GC_STATS  *stats)
{
    struct KOS_LOCKED_PAGES_S *locked_pages = heap->locked_pages_first;
    KOS_PAGE                 **insert_at    = &heap->free_pages;
    unsigned                   num_unlocked = 0U;

    while (locked_pages) {
        struct KOS_LOCKED_PAGES_S *cur_locked_page_list = locked_pages;

        uint32_t i;

        locked_pages = locked_pages->next;

        for (i = 0; i < cur_locked_page_list->num_pages; ++i) {
            KOS_PAGE *const page = cur_locked_page_list->pages[i];
            KOS_PAGE       *cur;

            if ( ! page)
                continue;

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
            ++num_unlocked;
        }

        kos_free(cur_locked_page_list);
    }

    heap->locked_pages_first = 0;
    heap->locked_pages_last  = 0;

    return num_unlocked;
}
#else
static unsigned unlock_pages(KOS_HEAP      *heap,
                             KOS_PAGE_LIST *free_pages,
                             KOS_GC_STATS  *stats)
{
    KOS_PAGE    **page_ptr     = &free_pages->head;
    KOS_PAGE     *page         = *page_ptr;
    KOS_PAGE     *prev         = 0;
    KOS_PAGE_LIST reclaimed    = { 0, 0 };
    unsigned      num_unlocked = 0U;

    /* Find all free pages which had no evacuated objects and make them available */
    while (page) {

        if (KOS_atomic_read_relaxed_u32(page->num_allocated)) {
            page_ptr = &page->next;
            prev     = page;
            page     = *page_ptr;
        }
        else {
            KOS_PAGE *next = page->next;
            *page_ptr      = next;
            if ( ! next)
                free_pages->tail = prev;
            push_page_back(&reclaimed, page);
            page = next;
            ++num_unlocked;
        }
    }

    reclaim_free_pages(heap, &reclaimed, stats);

    return num_unlocked;
}
#endif

static int evacuate_object(KOS_CONTEXT     ctx,
                           KOS_OBJ_HEADER *hdr,
                           uint32_t        size)
{
    int             error = KOS_SUCCESS;
    const KOS_TYPE  type  = kos_get_object_type(*hdr);
    KOS_OBJ_HEADER *new_obj;

    new_obj = (KOS_OBJ_HEADER *)alloc_object(ctx, type, size);

    if (new_obj) {
        memcpy(new_obj, hdr, size);

        hdr->size_and_type = (KOS_OBJ_ID)((intptr_t)new_obj + 1);

#ifdef CONFIG_PERF
        if (size <= 32)
            KOS_PERF_CNT(evac_object_size[0]);
        else if (size <= 128)
            KOS_PERF_CNT(evac_object_size[1]);
        else if (size <= 512)
            KOS_PERF_CNT(evac_object_size[2]);
        else
            KOS_PERF_CNT(evac_object_size[3]);
#endif
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void update_child_ptr(KOS_OBJ_ID *obj_id_ptr)
{
    KOS_OBJ_ID obj_id = *obj_id_ptr;

    if (kos_is_heap_object(obj_id) && ! IS_BAD_PTR(obj_id)) {

        /* TODO use relaxed atomic loads/stores ??? */

        KOS_OBJ_ID new_obj = ((KOS_OBJ_HEADER *)((intptr_t)obj_id - 1))->size_and_type;

#ifdef CONFIG_MAD_GC
        const struct KOS_MARK_LOC_S mark_loc = get_mark_location(obj_id);
        const uint32_t              color    = get_marking(&mark_loc);

        assert(color & BLACK);
        assert(kos_is_heap_object(new_obj));
#endif

        /* Objects in pages retained keep their size in their size field */
        if (kos_is_heap_object(new_obj))
            *obj_id_ptr = new_obj;
    }
}

static void update_child_ptrs(KOS_OBJ_HEADER *hdr)
{
    switch (kos_get_object_type(*hdr)) {

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

                update_child_ptr(&new_ref_obj);

                delta = (intptr_t)new_ref_obj - (intptr_t)old_ref_obj;

                ((KOS_STRING *)hdr)->ref.obj_id   = new_ref_obj;
                ((KOS_STRING *)hdr)->ref.data_ptr = old_data_ptr + delta;
            }
            break;

        default:
            assert(kos_get_object_type(*hdr) == OBJ_OBJECT);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_OBJECT *)hdr)->props);
            update_child_ptr(&((KOS_OBJECT *)hdr)->prototype);
            break;

        case OBJ_HUGE_TRACKER: {
            KOS_HUGE_TRACKER *const tracker = (KOS_HUGE_TRACKER *)hdr;
            const KOS_OBJ_ID        object  = tracker->object;
            if ( ! IS_BAD_PTR(object)) {

                KOS_OBJ_ID *back_ref_ptr = (KOS_OBJ_ID *)((intptr_t)object - 1 - sizeof(KOS_OBJ_ID));

                assert(tracker->data);

                *back_ref_ptr = OBJID(HUGE_TRACKER, tracker);

                update_child_ptrs((KOS_OBJ_HEADER *)((intptr_t)object - 1));
            }
            break;
        }

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
                KOS_OBJ_ID **end = ref + ((KOS_LOCAL_REFS *)hdr)->num_tracked;

                update_child_ptr(&((KOS_LOCAL_REFS *)hdr)->next);

                for ( ; ref < end; ++ref)
                    update_child_ptr(*ref);
            }
            break;
    }
}

static void update_page_after_evacuation(KOS_PAGE *page, uint32_t offset)
{
    uint8_t       *ptr = (uint8_t *)get_slots(page);
    uint8_t *const end = ptr + (KOS_atomic_read_relaxed_u32(page->num_allocated) << KOS_OBJ_ALIGN_BITS);

    ptr += offset;

    while (ptr < end) {

        KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)ptr;
        const uint32_t  size = kos_get_object_size(*hdr);

        update_child_ptrs(hdr);

        ptr += size;
    }
}

struct KOS_INCOMPLETE_S {
    KOS_PAGE *page;
    uint32_t  offset;
};

static void update_incomplete_page(KOS_HEAP                *heap,
                                   struct KOS_INCOMPLETE_S *incomplete)
{
    KOS_OBJ_HEADER *hdr;

    if ( ! incomplete->page)
        return;

    update_page_after_evacuation(incomplete->page, incomplete->offset);

    hdr = (KOS_OBJ_HEADER *)get_slots(incomplete->page);

    /* Mark all objects/slots that have been processed on this page so far
     * as one contiguous region and clear mark bits for them, so that
     * these objects will not be processed again by a subsequent evacuation
     * pass. */
    kos_set_object_type_size(*hdr, OBJ_OPAQUE, incomplete->offset);
    clear_mark_state((KOS_OBJ_ID)((uintptr_t)hdr + 1));

    push_page_with_objects(heap, incomplete->page);
}

static int save_incomplete_page(KOS_OBJ_HEADER          *hdr,
                                KOS_PAGE                *page,
                                struct KOS_INCOMPLETE_S *incomplete)
{
    KOS_OBJ_HEADER *first_hdr = (KOS_OBJ_HEADER *)get_slots(page);

    if (first_hdr == hdr)
        return 0;

    /* Pass the incomplete page to update_incomplete_page() */
    incomplete->page   = page;
    incomplete->offset = (uint32_t)((uintptr_t)hdr - (uintptr_t)first_hdr);

    return 1;
}

static void update_pages_after_evacuation(KOS_HEAP *heap)
{
    KOS_PAGE *page = begin_page_walk(heap, &heap->update_pages);

    for ( ; page; page = get_next_page(heap, &heap->update_pages))
        update_page_after_evacuation(page, 0);

    end_page_walk(heap);
}

static void update_threads_after_evacuation(KOS_INSTANCE *inst)
{
    uint32_t       i;
    const uint32_t max_threads = inst->threads.max_threads;

    if ( ! inst->threads.threads)
        return;

    for (i = 0; i < max_threads; i++)
    {
        KOS_THREAD *thread = inst->threads.threads[i];

        if ( ! thread)
            continue;

        update_child_ptr(&thread->thread_func);
        update_child_ptr(&thread->this_obj);
        update_child_ptr(&thread->args_obj);
        update_child_ptr((KOS_OBJ_ID *)&thread->retval);
        update_child_ptr((KOS_OBJ_ID *)&thread->exception);
    }
}

static void update_after_evacuation(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;
    KOS_HEAP           *heap = &inst->heap;

    KOS_atomic_write_relaxed_ptr(heap->update_pages, heap->used_pages.head);
    KOS_atomic_write_relaxed_u32(heap->walk_stage,   WALK_ACTIVE);

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
    update_child_ptr((KOS_OBJ_ID *)&inst->threads.threads);

    update_child_ptr(&inst->args);

    update_threads_after_evacuation(inst);

    /* Update object pointers in thread contexts */

    ctx = &inst->threads.main_thread;

    while (ctx) {

        uint32_t i;

        assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE);
        assert(!ctx->cur_page);

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

#define PAGE_ALREADY_EVACED 0xF00DBA5E

static int evacuate(KOS_CONTEXT              ctx,
                    KOS_PAGE_LIST           *free_pages,
                    KOS_GC_STATS            *out_stats,
                    struct KOS_INCOMPLETE_S *incomplete)
{
    KOS_HEAP    *heap  = get_heap(ctx);
    int          error = KOS_SUCCESS;
    KOS_PAGE    *page  = heap->used_pages.head;
    KOS_PAGE    *next;
    KOS_GC_STATS stats = *out_stats;

    assert( ! KOS_is_exception_pending(ctx));

    heap->used_pages.head = 0;
    heap->used_pages.tail = 0;

    for ( ; page; page = next) {

        struct KOS_MARK_LOC_S mark_loc = { 0, 0 };

        unsigned       num_evac       = 0;
        const uint32_t num_allocated  = KOS_atomic_read_relaxed_u32(page->num_allocated);
        const uint32_t num_slots_used = KOS_atomic_read_relaxed_u32(page->num_used);
        KOS_SLOT      *ptr            = get_slots(page);
        KOS_SLOT      *end            = ptr + num_allocated;
#ifndef NDEBUG
        KOS_SLOT      *page_end       = ptr + num_allocated;
#endif

        heap->used_heap_size -= used_page_size(page);

        next = page->next;

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);

        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        #define UNUSED_SLOTS ((KOS_SLOTS_PER_PAGE * (100U - KOS_MIGRATION_THRESH)) / 100U)

        if (num_slots_used == PAGE_ALREADY_EVACED
#ifndef CONFIG_MAD_GC
            || (num_allocated - num_slots_used < UNUSED_SLOTS)
#endif
            ) {

            gc_trace(("GC ctx=%p retain page %p\n", (void *)ctx, (void *)page));

            /* Mark unused objects as opaque so they don't participate in
             * pointer update after evacuation. */
            if (num_slots_used < num_allocated) {
                while (ptr < end) {

                    KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
                    const uint32_t  size  = kos_get_object_size(*hdr);
                    const uint32_t  color = get_marking(&mark_loc);

                    assert(size > 0U);
                    assert(color != GRAY);
                    assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

                    if ( ! color) {
                        finalize_object(ctx, hdr, &stats);
                        kos_set_object_type(*hdr, OBJ_OPAQUE);
                    }

                    advance_marking(&mark_loc, size >> KOS_OBJ_ALIGN_BITS);

                    ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
                }
            }

            push_page_with_objects(heap, page);

            if (num_slots_used != PAGE_ALREADY_EVACED) {
                ++stats.num_pages_kept;
                stats.size_kept += num_slots_used << KOS_OBJ_ALIGN_BITS;
            }
            continue;
        }

        gc_trace(("GC ctx=%p -- evac page %p\n", (void *)ctx, (void *)page));

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = kos_get_object_size(*hdr);
            const uint32_t  color = get_marking(&mark_loc);

            assert(size > 0U);
            assert(color != GRAY);
            assert(size <= (size_t)((uint8_t *)page_end - (uint8_t *)ptr));

            if (color) {
                error = evacuate_object(ctx, hdr, size);

                if (error) {

                    assert( ! KOS_is_exception_pending(ctx));

                    /* First attempt to recover:
                     * Try to release any pages which had no objects evacuated
                     * and put them in the free pool. */

                    release_current_page_locked(ctx);

                    if (unlock_pages(heap, free_pages, &stats)) {
                        error = evacuate_object(ctx, hdr, size);

                        if (ctx->inst->flags & KOS_INST_VERBOSE) {
                            if (error)
                                printf("GC is memory constrained\n");
                            else
                                printf("GC is memory constrained, but recovered\n");
                        }
                    }

                    if (error) {

                        assert( ! ctx->cur_page);

                        set_marking_in_pages(heap->used_pages.head, BLACK, PAGE_ALREADY_EVACED);

                        if ( ! save_incomplete_page(hdr, page, incomplete))
                            push_page_with_objects(heap, page);

                        /* Put back the remaining pages on the heap. */
                        for (page = next; page; page = next) {
                            next = page->next;
                            push_page(&heap->used_pages, page);
                        }

                        stats.num_objs_evacuated += num_evac;

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
        if ( ! num_evac) {
            KOS_atomic_write_relaxed_u32(page->num_allocated, 0);

            debug_fill_slots(page);
        }

        push_page_back(free_pages, page);
    }

cleanup:
    *out_stats = stats;

    release_current_page_locked(ctx);

    return error;
}

static void update_gc_threshold(KOS_HEAP *heap)
{
    heap->gc_threshold = heap->used_heap_size + heap->malloc_size + KOS_GC_STEP;
}

static void help_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap    = get_heap(ctx);
    int             engaged = KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_ENGAGED;
    enum GC_STATE_E gc_state;

    gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(heap->gc_state);

    if (gc_state == GC_INACTIVE && ! engaged)
        return;

    kos_heap_release_thread_page(ctx);

    for (;;) {

        assert( ! ctx->cur_page);

        switch (gc_state) {

            case GC_INACTIVE:
                kos_lock_mutex(&ctx->inst->threads.mutex);

                gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(heap->gc_state);

                if (gc_state == GC_INACTIVE && engaged)
                    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_INACTIVE);

                kos_unlock_mutex(&ctx->inst->threads.mutex);

                if (gc_state == GC_INACTIVE)
                    return;
                break;

            case GC_MARK:
                gray_to_black_in_pages(heap);
                break;

            /* TODO help with evac */

            case GC_UPDATE:
                update_pages_after_evacuation(heap);
                break;

            default:
                if ( ! engaged) {
                    KOS_atomic_write_release_u32(ctx->gc_state, GC_ENGAGED);
                    engaged = 1;
                }
                kos_yield();
                break;
        }

        gc_state = (enum GC_STATE_E)KOS_atomic_read_acquire_u32(heap->gc_state);
    }
}

void KOS_help_gc(KOS_CONTEXT ctx)
{
    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    if (KOS_atomic_read_relaxed_u32(get_heap(ctx)->gc_state) >= GC_INIT)
        help_gc(ctx);
}

#ifdef CONFIG_MAD_GC
int kos_trigger_mad_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);
    int             error;

    /* Don't try to collect garbage when the garbage collector is running */
    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        return KOS_SUCCESS;

    kos_lock_mutex(&heap->mutex);
    error = try_collect_garbage(ctx);
    kos_unlock_mutex(&heap->mutex);

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

    return error;
}
#endif

void KOS_suspend_context(KOS_CONTEXT ctx)
{
    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    kos_heap_release_thread_page(ctx);
    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_SUSPENDED);
}

int KOS_resume_context(KOS_CONTEXT ctx)
{
    enum GC_STATE_E gc_state;
    int             error = KOS_SUCCESS;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_SUSPENDED);

    kos_lock_mutex(&ctx->inst->threads.mutex);

    gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(get_heap(ctx)->gc_state);

    KOS_atomic_write_release_u32(ctx->gc_state, gc_state == GC_INIT ? GC_INACTIVE : GC_ENGAGED);

    kos_unlock_mutex(&ctx->inst->threads.mutex);

    if (gc_state != GC_INIT)
        help_gc(ctx);
    else
        error = kos_trigger_mad_gc(ctx);

    return error;
}

int KOS_collect_garbage(KOS_CONTEXT   ctx,
                        KOS_GC_STATS *out_stats)
{
    uint64_t      time_0;
    uint64_t      time_1;
    KOS_HEAP     *heap            = get_heap(ctx);
    KOS_PAGE_LIST free_pages      = { 0, 0 };
    KOS_GC_STATS  stats           = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                      0U, 0U, 0U, 0U, 0U, 0U };
    unsigned      num_gray_passes = 0U;
    int           error           = KOS_SUCCESS;

    /***********************************************************************/
    /* Initialize GC */

    time_0 = kos_get_time_us();

    if ( ! KOS_atomic_cas_strong_u32(heap->gc_state, GC_INACTIVE, GC_INIT)) {

        KOS_help_gc(ctx);

        if (out_stats)
            *out_stats = stats;

        return KOS_SUCCESS;
    }

    stats.initial_heap_size   = heap->heap_size;
    stats.initial_malloc_size = heap->malloc_size;

    gc_trace(("GC ctx=%p begin cycle %u\n", (void *)ctx, KOS_atomic_read_relaxed_u32(heap->gc_cycles)));

    KOS_PERF_CNT(gc_cycles);

    KOS_atomic_write_relaxed_u32(heap->gc_cycles, KOS_atomic_read_relaxed_u32(heap->gc_cycles) + 1U);

    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_ENGAGED);

    kos_heap_release_thread_page(ctx);

    verify_heap_used_size(heap);

    /***********************************************************************/
    /* Phase 1: Stop all threads, clear marking and mark roots */

    kos_lock_mutex(&ctx->inst->threads.mutex);

    stop_the_world(ctx->inst); /* Remaining threads enter help_gc() */

    stats.initial_used_heap_size = heap->used_heap_size;

    clear_marking(heap);

    mark_roots(ctx);

    KOS_atomic_write_release_u32(heap->gc_state, GC_MARK);

    kos_unlock_mutex(&ctx->inst->threads.mutex);

    /***********************************************************************/
    /* Phase 2: Perform marking */

    do ++num_gray_passes;
    while (gray_to_black(heap));

    /***********************************************************************/
    /* Phase 3: Evacuate and reclaim free pages */

    do {
        uint32_t                prev_num_freed;
        struct KOS_INCOMPLETE_S incomplete = { 0, 0 };

        KOS_atomic_write_release_u32(heap->gc_state, GC_EVACUATE);

        error = evacuate(ctx, &free_pages, &stats, &incomplete);

        KOS_atomic_write_release_u32(heap->gc_state, GC_UPDATE);

        assert( ! incomplete.page || error);

        update_after_evacuation(ctx);

        update_incomplete_page(heap, &incomplete);

        prev_num_freed = stats.num_pages_freed;

        reclaim_free_pages(heap, &free_pages, &stats);

        /* If there was an error during evacuation and we cannot
         * reclaim any freed pages, throw OOM exception. */
        if (error && prev_num_freed == stats.num_pages_freed) {
            assert(error == KOS_ERROR_OUT_OF_MEMORY);

            if (ctx->inst->flags & KOS_INST_VERBOSE)
                printf("GC ran out of memory\n");

            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            error = KOS_ERROR_EXCEPTION;
            break;
        }
    } while (error);

    /***********************************************************************/
    /* Done, finish GC */

    update_gc_threshold(heap);

    stats.num_gray_passes = num_gray_passes;
    stats.heap_size       = heap->heap_size;
    stats.used_heap_size  = heap->used_heap_size;
    stats.malloc_size     = heap->malloc_size;

    verify_heap_used_size(heap);

    gc_trace(("GC ctx=%p end cycle\n", (void *)ctx));

    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_INACTIVE);

    KOS_atomic_write_release_u32(heap->gc_state, GC_INACTIVE);

    if ( ! error && KOS_is_exception_pending(ctx))
        error = KOS_ERROR_EXCEPTION;

    time_1 = kos_get_time_us();

    stats.time_us = (unsigned)(time_1 - time_0);

    if (ctx->inst->flags & KOS_INST_DEBUG) {
        printf("GC used/total [B] %x/%x -> %x/%x | malloc [B] %x -> %x : time %u us : gray passes %u\n",
               stats.initial_used_heap_size, stats.initial_heap_size,
               stats.used_heap_size, stats.heap_size,
               stats.initial_malloc_size, stats.malloc_size,
               stats.time_us, stats.num_gray_passes);
    }

    if (out_stats)
        *out_stats = stats;

    if ( ! error)
        error = kos_join_finished_threads(ctx, KOS_ONLY_DISOWNED);

    return error;
}

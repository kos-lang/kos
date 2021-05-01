/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "kos_heap.h"
#include "../inc/kos_atomic.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_string.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_math.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system_internal.h"
#include "kos_threads_internal.h"
#include "kos_try.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* For _BitScanForward() */
#ifdef _MSC_VER
#   pragma warning( push )
#   pragma warning( disable : 4255 ) /* '__slwpcb': no function prototype given: converting '()' to '(void)' */
#   pragma warning( disable : 4668 ) /* '__cplusplus' is not defined as a preprocessor macro */
#   include <intrin.h>
#   pragma warning( pop )
#endif

#if 0
#define gc_trace(x) printf x
#else
#define gc_trace(x) do {} while (0)
#endif

#define MARK_GROUP_BITS 4
#define MARK_GROUP_MASK ((1U << MARK_GROUP_BITS) - 1U)

enum WALK_THREAD_TYPE_E {
    WALK_MAIN_THREAD,
    WALK_HELPER_THREAD
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

struct KOS_MARK_GROUP_S {
    uint32_t        num_objs;
    KOS_MARK_GROUP *next;
    KOS_OBJ_ID      objs[62];
};

struct KOS_MARK_CONTEXT_S {
    KOS_MARK_GROUP *current;
    KOS_HEAP       *heap;
};

typedef struct KOS_MARK_CONTEXT_S KOS_MARK_CONTEXT;

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
    page->next = KOS_NULL;

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

static int init_mark_group_stack(KOS_MARK_GROUP_STACK *stack)
{
    size_t i;

    KOS_atomic_write_relaxed_u32(stack->slot_idx, 0U);
    KOS_atomic_write_relaxed_u32(stack->num_groups, 0U);

    assert(sizeof(stack->slots) / sizeof(stack->slots[0]) == (MARK_GROUP_MASK + 1U));

    for (i = 0; i < sizeof(stack->slots) / sizeof(stack->slots[0]); i++)
        KOS_atomic_write_relaxed_ptr(stack->slots[i], (KOS_MARK_GROUP *)KOS_NULL);

    stack->stack = KOS_NULL;

    return kos_create_mutex(&stack->mutex);
}

static void push_mark_group(KOS_MARK_GROUP_STACK *stack,
                            KOS_MARK_GROUP       *group)
{
    uint32_t       idx = KOS_atomic_read_relaxed_u32(stack->slot_idx);
    const uint32_t end = idx;

    do {
        if (KOS_atomic_cas_weak_ptr(stack->slots[idx], (KOS_MARK_GROUP *)KOS_NULL, group)) {
            (void)KOS_atomic_cas_weak_u32(stack->slot_idx, end, (idx + 1U) & MARK_GROUP_MASK);
            return;
        }

        idx = (idx + 1U) & MARK_GROUP_MASK;
    } while (idx != end);

    kos_lock_mutex(stack->mutex);

    group->next  = stack->stack;
    stack->stack = group;

    kos_unlock_mutex(stack->mutex);
}

static KOS_MARK_GROUP *pop_mark_group(KOS_MARK_GROUP_STACK *stack)
{
    KOS_MARK_GROUP *group;
    uint32_t        idx = (KOS_atomic_read_relaxed_u32(stack->slot_idx) - 1U) & MARK_GROUP_MASK;
    const uint32_t  end = idx;

    do {
        group = (KOS_MARK_GROUP *)KOS_atomic_read_relaxed_ptr(stack->slots[idx]);

        if (group && KOS_atomic_cas_weak_ptr(stack->slots[idx], group, (KOS_MARK_GROUP *)KOS_NULL)) {
            (void)KOS_atomic_cas_weak_u32(stack->slot_idx, ((end + 1U) & MARK_GROUP_MASK), idx);
            return group;
        }

        idx = (idx - 1U) & MARK_GROUP_MASK;
    } while (idx != end);

    kos_lock_mutex(stack->mutex);

    group = stack->stack;

    if (group)
        stack->stack = group->next;

    kos_unlock_mutex(stack->mutex);

    return group;
}

int kos_heap_init(KOS_INSTANCE *inst)
{
    PROF_PLOT_INIT("heap",     Memory)
    PROF_PLOT_INIT("off-heap", Memory)

    KOS_HEAP *heap = &inst->heap;
    int       error;

    heap->heap_size       = 0;
    heap->used_heap_size  = 0;
    heap->malloc_size     = 0;
    heap->max_heap_size   = KOS_MAX_HEAP_SIZE;
    heap->max_malloc_size = KOS_MAX_HEAP_SIZE;
    heap->gc_threshold    = (uint32_t)(((uint64_t)KOS_MAX_HEAP_SIZE * KOS_GC_THRESHOLD) / 100U);
    heap->free_pages      = KOS_NULL;
    heap->used_pages.head = KOS_NULL;
    heap->used_pages.tail = KOS_NULL;
    heap->pools           = KOS_NULL;
    heap->walk_threads    = 0U;
    heap->threads_to_stop = 0U;
    heap->gc_cycles       = 0U;

    KOS_atomic_write_relaxed_u32(heap->gc_state,   GC_INACTIVE);
    KOS_atomic_write_relaxed_ptr(heap->walk_pages, (KOS_PAGE *)KOS_NULL);

#ifdef CONFIG_MAD_GC
    heap->locked_pages_first = KOS_NULL;
    heap->locked_pages_last  = KOS_NULL;
#endif

    assert(KOS_BITMAP_OFFS + KOS_BITMAP_SIZE <= KOS_SLOTS_OFFS);
    assert( ! (KOS_SLOTS_OFFS & 7U));
    assert(KOS_SLOTS_OFFS + (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) == KOS_PAGE_SIZE);

    error = init_mark_group_stack(&heap->objects_to_mark);
    if (error)
        return error;

    error = init_mark_group_stack(&heap->free_mark_groups);
    if (error) {
        kos_destroy_mutex(&heap->objects_to_mark.mutex);
        return error;
    }

    error = kos_create_mutex(&heap->mutex);
    if (error) {
        kos_destroy_mutex(&heap->free_mark_groups.mutex);
        kos_destroy_mutex(&heap->objects_to_mark.mutex);
        return error;
    }

    error = kos_create_cond_var(&heap->engagement_cond);
    if (error) {
        kos_destroy_mutex(&heap->mutex);
        kos_destroy_mutex(&heap->free_mark_groups.mutex);
        kos_destroy_mutex(&heap->objects_to_mark.mutex);
        return error;
    }

    error = kos_create_cond_var(&heap->walk_cond);
    if (error) {
        kos_destroy_cond_var(&heap->engagement_cond);
        kos_destroy_mutex(&heap->mutex);
        kos_destroy_mutex(&heap->free_mark_groups.mutex);
        kos_destroy_mutex(&heap->objects_to_mark.mutex);
        return error;
    }

    error = kos_create_cond_var(&heap->helper_cond);
    if (error) {
        kos_destroy_cond_var(&heap->walk_cond);
        kos_destroy_cond_var(&heap->engagement_cond);
        kos_destroy_mutex(&heap->mutex);
        kos_destroy_mutex(&heap->free_mark_groups.mutex);
        kos_destroy_mutex(&heap->objects_to_mark.mutex);
        return error;
    }

    return KOS_SUCCESS;
}

#if defined(CONFIG_MAD_GC)
int kos_gc_active(KOS_CONTEXT ctx)
{
    int ret = 0;

    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        ret = 1;
    else {

        KOS_HEAP *const heap = get_heap(ctx);

        ret = KOS_atomic_read_relaxed_u32(heap->gc_state) >= GC_MARK;
    }

    return ret;
}
#endif

static void finalize_object(KOS_CONTEXT     ctx,
                            KOS_OBJ_HEADER *hdr,
                            KOS_GC_STATS   *stats)
{
    const KOS_TYPE type = kos_get_object_type(*hdr);

    switch (type) {

        case OBJ_OBJECT: {

            KOS_OBJECT_WITH_PRIVATE *obj = (KOS_OBJECT_WITH_PRIVATE *)hdr;

            if (obj->priv_class && obj->finalize) {

                obj->finalize(ctx, KOS_atomic_read_relaxed_ptr(obj->priv));

                assert( ! KOS_is_exception_pending(ctx));

                obj->priv_class = KOS_NULL;
                obj->finalize   = KOS_NULL;
                KOS_atomic_write_relaxed_ptr(obj->priv, (void *)KOS_NULL);

                ++stats->num_objs_finalized;
            }
            break;
        }

        case OBJ_BUFFER_STORAGE: {

            KOS_BUFFER_EXTERNAL_STORAGE *obj = (KOS_BUFFER_EXTERNAL_STORAGE *)hdr;

            if ((obj->flags & KOS_EXTERNAL_STORAGE) && obj->priv && obj->finalize) {

                obj->finalize(ctx, obj->priv);

                assert( ! KOS_is_exception_pending(ctx));

                obj->finalize = KOS_NULL;
                obj->priv     = KOS_NULL;
                obj->ptr      = KOS_NULL;

                ++stats->num_objs_finalized;
            }
            break;
        }

        case OBJ_HUGE_TRACKER: {

            KOS_HUGE_TRACKER *obj = (KOS_HUGE_TRACKER *)hdr;

            if (obj->data) {

                gc_trace(("free huge %p\n", (void *)obj->data));

                KOS_free_aligned(obj->data);

                get_heap(ctx)->malloc_size -= obj->size;

                stats->size_freed += obj->size;

                obj->data   = KOS_NULL;
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
    KOS_GC_STATS gc_stats = KOS_GC_STATS_INIT(0U);

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
    assert(inst->threads.main_thread.prev == KOS_NULL);
    assert(inst->threads.main_thread.next == KOS_NULL);

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
            KOS_free(del);
        }

        inst->heap.locked_pages_first = KOS_NULL;
        inst->heap.locked_pages_last  = KOS_NULL;
    }
#endif

    finalize_objects(&inst->threads.main_thread, &inst->heap);

    assert( ! inst->heap.objects_to_mark.stack);
    while (inst->heap.free_mark_groups.stack) {

        KOS_MARK_GROUP *const group = inst->heap.free_mark_groups.stack;

        inst->heap.free_mark_groups.stack = group->next;

        KOS_free(group);
    }
    {
        KOS_MARK_GROUP_STACK *const stack = &inst->heap.free_mark_groups;
        size_t                      i;

        for (i = 0; i < sizeof(stack->slots) / sizeof(stack->slots[0]); i++) {

            KOS_MARK_GROUP *const group = KOS_atomic_read_relaxed_ptr(stack->slots[i]);

            assert( ! KOS_atomic_read_relaxed_ptr(inst->heap.objects_to_mark.slots[i]));

            if (group)
                KOS_free(group);
        }
    }

    for (;;) {
        void     *memory;
        KOS_POOL *pool = inst->heap.pools;

        if ( ! pool)
            break;

        inst->heap.pools = pool->next;

        memory = pool->memory;
        KOS_free_aligned(memory);

        KOS_free(pool);
    }

    kos_destroy_cond_var(&inst->heap.helper_cond);
    kos_destroy_cond_var(&inst->heap.walk_cond);
    kos_destroy_cond_var(&inst->heap.engagement_cond);
    kos_destroy_mutex(&inst->heap.mutex);
    kos_destroy_mutex(&inst->heap.free_mark_groups.mutex);
    kos_destroy_mutex(&inst->heap.objects_to_mark.mutex);
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
                locked_pages->pages[i] = KOS_NULL;

                kos_mem_protect(page, KOS_PAGE_SIZE, KOS_READ_WRITE);

                page->next = KOS_NULL;

                return page;
            }
        }

        locked_pages = locked_pages->next;
    }

    return KOS_NULL;
}
#endif

static KOS_PAGE *get_next_page(KOS_HEAP *heap)
{
    KOS_PAGE *page = KOS_NULL;
    KOS_PAGE *next;

    do {
        page = (KOS_PAGE *)KOS_atomic_read_relaxed_ptr(heap->walk_pages);

        if ( ! page)
            break;

        next = page->next;

    } while ( ! KOS_atomic_cas_weak_ptr(heap->walk_pages, page, next));

    return page;
}

static void release_helper_threads(KOS_HEAP *heap)
{
    kos_broadcast_cond_var(heap->helper_cond);
}

static void begin_walk(KOS_HEAP               *heap,
                       enum WALK_THREAD_TYPE_E helper)
{
    ++heap->walk_threads;

    if ( ! helper)
        release_helper_threads(heap);

    kos_unlock_mutex(heap->mutex);
}

static void end_walk(KOS_HEAP               *heap,
                     enum WALK_THREAD_TYPE_E helper)
{
    kos_lock_mutex(heap->mutex);

    assert(heap->walk_threads > 0);

    if (--heap->walk_threads == 0) {
        if (helper)
            kos_signal_cond_var(heap->walk_cond);
    }
    else if ( ! helper) {
        do kos_wait_cond_var(heap->walk_cond, heap->mutex);
        while (heap->walk_threads);
    }
}

static KOS_POOL *alloc_pool(KOS_HEAP *heap,
                            uint32_t  alloc_size)
{
    PROF_ZONE(HEAP)

    KOS_POOL *pool_hdr;
    uint8_t  *pool;

    if (heap->heap_size + alloc_size > heap->max_heap_size)
        return KOS_NULL;

    pool = (uint8_t *)KOS_malloc_aligned(alloc_size, (size_t)KOS_PAGE_SIZE);

    if ( ! pool)
        return KOS_NULL;

    heap->heap_size += alloc_size;

    assert(KOS_align_up((uintptr_t)pool, (uintptr_t)KOS_PAGE_SIZE) == (uintptr_t)pool);

    pool_hdr = (KOS_POOL *)KOS_malloc(sizeof(KOS_POOL));

    if ( ! pool_hdr) {
        KOS_free_aligned(pool);
        return KOS_NULL;
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
    KOS_PAGE *next_page = KOS_NULL;
    uint8_t  *page_bytes;

    if ( ! pool_hdr)
        return KOS_ERROR_OUT_OF_MEMORY;

    assert((uintptr_t)pool_hdr->memory % (uintptr_t)KOS_PAGE_SIZE == 0U);
    assert(pool_hdr->alloc_size % KOS_PAGE_SIZE == 0U);

    page_bytes = (uint8_t *)pool_hdr->memory + pool_hdr->alloc_size;

    assert(heap->free_pages == KOS_NULL);

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
            return KOS_NULL;

        page = heap->free_pages;

        assert(page);
    }

    gc_trace(("alloc page %p\n", (void *)page));

    assert(KOS_atomic_read_relaxed_u32(page->num_allocated) == 0);

    heap->free_pages = page->next;

    KOS_PERF_CNT(alloc_free_page);

    page->next = KOS_NULL;
    return page;
}

static int collect_garbage_last_resort_locked(KOS_CONTEXT ctx, KOS_GC_STATS *stats)
{
    KOS_HEAP *const heap = get_heap(ctx);
    int             error;

    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        return KOS_SUCCESS;

    /* No garbage to collect, bail out early.  This can happen on init. */
    if ( ! heap->used_pages.head && ! ctx->cur_page)
        return KOS_SUCCESS;

    kos_unlock_mutex(heap->mutex);

    /* TODO add and use kos_collect_garbage_locked() */
    error = KOS_collect_garbage(ctx, stats);

    kos_lock_mutex(heap->mutex);

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
        return KOS_SUCCESS;

#ifndef CONFIG_MAD_GC
    if (heap->used_heap_size > heap->gc_threshold ||
        heap->malloc_size > heap->max_malloc_size)
#endif
    {
        KOS_GC_STATS stats = KOS_GC_STATS_INIT(0U);

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
    KOS_SLOT      *slot          = KOS_NULL;

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
               KOS_atomic_read_relaxed_u32(heap->gc_state) == GC_INIT     ||
               KOS_atomic_read_relaxed_u32(ctx->gc_state)  == GC_ENGAGED);

        gc_trace(("release cur page %p ctx=%p\n", (void *)page, (void *)ctx));

        push_page_with_objects(heap, page);

        ctx->cur_page = KOS_NULL;
    }
}

void kos_heap_release_thread_page(KOS_CONTEXT ctx)
{
    if (ctx->cur_page) {

        KOS_HEAP *const heap = get_heap(ctx);

        kos_lock_mutex(heap->mutex);

        release_current_page_locked(ctx);

        kos_unlock_mutex(heap->mutex);
    }
}

#ifndef NDEBUG
static void verify_heap_used_size(KOS_HEAP *heap)
{
    uint32_t  used_size = 0;
    KOS_PAGE *page;

    for (page = heap->used_pages.head; page; page = page->next)
        used_size += used_page_size(page);

    assert(used_size == heap->used_heap_size);
}
#else
#define verify_heap_used_size(heap) ((void)0)
#endif

static void *alloc_object(KOS_CONTEXT ctx,
                          KOS_TYPE    object_type,
                          uint32_t    size)
{
    PROF_ZONE(HEAP)

    KOS_PAGE       *page       = ctx->cur_page;
    const uint32_t  num_slots  = (size + sizeof(KOS_SLOT) - 1) >> KOS_OBJ_ALIGN_BITS;
    unsigned        seek_depth = KOS_MAX_PAGE_SEEK;
    KOS_HEAP       *heap;
    KOS_PAGE      **page_ptr;
    KOS_PAGE       *old_page;
    KOS_PAGE       *prev_page;
    KOS_OBJ_HEADER *hdr        = KOS_NULL;

    assert(num_slots <= KOS_SLOTS_PER_PAGE);
    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_SUSPENDED);

    kos_validate_context(ctx);

    /* Fast path: allocate from a page held by this thread */
    if (page) {

        hdr = alloc_object_from_page(page, object_type, num_slots);

        if (hdr)
            return hdr;
    }

    /* Slow path: find a non-full page in the heap which has enough room or
     * allocate a new page. */

    heap = get_heap(ctx);

    kos_lock_mutex(heap->mutex);

    {
        const uint32_t gc_state = KOS_atomic_read_relaxed_u32(heap->gc_state);

        if (gc_state != GC_INACTIVE && KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE) {

            help_gc(ctx);

            assert( ! ctx->cur_page);
            page = KOS_NULL;
        }
    }

    /* Check if any of the non-full pages contains enough space */

    page_ptr  = &heap->used_pages.head;
    prev_page = KOS_NULL;

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
        else if ( ! page || (KOS_atomic_read_relaxed_u32(page->num_allocated)
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

            old_page->next = KOS_NULL;
            ctx->cur_page  = old_page;
        }

        break;
    }

    if (seek_depth < KOS_MAX_PAGE_SEEK) {
        if (seek_depth) {
            KOS_PERF_CNT(non_full_seek);
        }
        else {
            KOS_PERF_CNT(non_full_seek_max);
        }
    }

    if ( ! hdr) {

        int error;

        release_current_page_locked(ctx);

        error = try_collect_garbage(ctx);

        if (error && error != KOS_SUCCESS_RETURN) {
            KOS_clear_exception(ctx);
            kos_unlock_mutex(heap->mutex);
            return KOS_NULL;
        }

        /* Allocate a new page */
        page = alloc_page(heap);

        /* If failed, collect garbage and try again */
        if ( ! page && ! error) {
            error = collect_garbage_last_resort_locked(ctx, KOS_NULL);

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

    PROF_PLOT("heap",     (int64_t)heap->used_heap_size)
    PROF_PLOT("off-heap", (int64_t)heap->malloc_size)

    kos_unlock_mutex(heap->mutex);

    return hdr;
}

static void *alloc_huge_object(KOS_CONTEXT ctx,
                               KOS_TYPE    object_type,
                               uint32_t    size)
{
    PROF_ZONE(HEAP)

    KOS_OBJ_HEADER   *hdr = KOS_NULL;
    KOS_HUGE_TRACKER *new_tracker;
    KOS_LOCAL         tracker;
    KOS_HEAP         *heap;
    intptr_t          ptrval;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_SUSPENDED);

    size += KOS_OBJ_TRACK_BIT;

    heap = get_heap(ctx);

    new_tracker = (KOS_HUGE_TRACKER *)alloc_object(ctx, OBJ_HUGE_TRACKER, sizeof(KOS_HUGE_TRACKER));

    if ( ! new_tracker)
        return KOS_NULL;

    new_tracker->data   = KOS_NULL;
    new_tracker->object = KOS_BADPTR;
    new_tracker->size   = 0;

    KOS_init_local_with(ctx, &tracker, OBJID(HUGE_TRACKER, new_tracker));

    kos_lock_mutex(heap->mutex);

    if (heap->malloc_size + size > heap->max_malloc_size) {

        const int error = collect_garbage_last_resort_locked(ctx, KOS_NULL);

        if (error)
            KOS_clear_exception(ctx);

        if (heap->malloc_size + size > heap->max_malloc_size)
            goto cleanup;
    }

    ptrval = (intptr_t)KOS_malloc_aligned(size, 32);

    if ( ! ptrval)
        goto cleanup;

    gc_trace(("alloc huge %p\n", (void *)ptrval));

    OBJPTR(HUGE_TRACKER, tracker.o)->size = size;
    OBJPTR(HUGE_TRACKER, tracker.o)->data = (void *)ptrval;

    hdr = (KOS_OBJ_HEADER *)(ptrval + KOS_OBJ_TRACK_BIT);

    *(KOS_OBJ_ID *)((intptr_t)hdr - sizeof(KOS_OBJ_ID)) = tracker.o;

    kos_set_object_type_size(*hdr, object_type, size - KOS_OBJ_TRACK_BIT);

    OBJPTR(HUGE_TRACKER, tracker.o)->object = (KOS_OBJ_ID)((intptr_t)hdr + 1);

    heap->malloc_size += size;

    assert(kos_is_heap_object(tracker.o));
    assert(kos_is_tracked_object((KOS_OBJ_ID)((intptr_t)hdr + 1)));
    assert( ! kos_is_heap_object((KOS_OBJ_ID)((intptr_t)hdr + 1)));

    KOS_PERF_CNT(alloc_huge_object);

cleanup:
    PROF_PLOT("heap",     (int64_t)heap->used_heap_size)
    PROF_PLOT("off-heap", (int64_t)heap->malloc_size)

    kos_unlock_mutex(heap->mutex);

    KOS_destroy_top_local(ctx, &tracker);

    return hdr;
}

void *kos_alloc_object(KOS_CONTEXT    ctx,
                       KOS_ALLOC_FLAG flags,
                       KOS_TYPE       object_type,
                       uint32_t       size)
{
    void *obj;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    if (kos_trigger_mad_gc(ctx))
        return KOS_NULL;

    if (kos_seq_fail()) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_NULL;
    }

    if (size > KOS_MAX_HEAP_OBJ_SIZE || flags == KOS_ALLOC_IMMOVABLE)
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
    else if (size <= 256)
        KOS_PERF_CNT(alloc_object_size[2]);
    else
        KOS_PERF_CNT(alloc_object_size[3]);
#endif

    return obj;
}

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type)
{
    void *obj = alloc_object(ctx, object_type, KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS);

    if ( ! obj)
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    return obj;
}

static void print_heap_locked(KOS_HEAP *heap, KOS_PAGE *cur_page)
{
    unsigned  used_pages = 0;
    unsigned  free_pages = 0;
    KOS_PAGE *page       = heap->used_pages.head;

    for (;;) {
        if ( ! page) {
            page = cur_page;
            if ( ! page)
                break;
        }

        ++used_pages;
        printf("%s page %p, %u/%u slots allocated\n",
               page == cur_page ? "cur" : "used",
               (void *)page,
               KOS_atomic_read_relaxed_u32(page->num_allocated),
               (unsigned)KOS_SLOTS_PER_PAGE);

        if (page == cur_page)
            break;

        page = page->next;
    }

    page = heap->free_pages;

    for ( ; page; page = page->next) {
        ++free_pages;
        printf("free page %p\n", (void *)page);
    }

    printf("total %u pages used, %u pages free, %u bytes malloc'd\n",
           used_pages, free_pages, heap->malloc_size);
}

void kos_print_heap(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);

    kos_lock_mutex(heap->mutex);

    print_heap_locked(heap, ctx->cur_page);

    kos_unlock_mutex(heap->mutex);
}

static void stop_the_world(KOS_INSTANCE *inst)
{
    KOS_HEAP *const heap = &inst->heap;

    assert( ! heap->threads_to_stop);

    for (;;) {

        KOS_CONTEXT ctx         = &inst->threads.main_thread;
        uint32_t    num_to_stop = 0U;

        kos_lock_mutex(inst->threads.ctx_mutex);

        while (ctx) {

            const enum GC_STATE_E gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(ctx->gc_state);

            if (gc_state == GC_INACTIVE)
                ++num_to_stop;
            else {
                assert( ! ctx->cur_page);
            }

            ctx = ctx->next;
        }

        kos_unlock_mutex(inst->threads.ctx_mutex);

        heap->threads_to_stop = num_to_stop;

        if ( ! num_to_stop)
            break;

        kos_wait_cond_var(heap->engagement_cond, heap->mutex);
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
                                 uint32_t              flags)
{
    while (page) {

        uint32_t *const bitmap = (uint32_t *)((uint8_t *)page + KOS_BITMAP_OFFS);

        memset(bitmap, state * 0x55, KOS_BITMAP_SIZE);

        KOS_atomic_write_relaxed_u32(page->flags, flags);

        page = page->next;
    }
}

static void clear_marking(KOS_HEAP *heap)
{
    PROF_ZONE(GC)

    set_marking_in_pages(heap->used_pages.head, WHITE, 0);

    heap->mark_error = KOS_SUCCESS;
}

struct KOS_MARK_LOC_S {
    KOS_ATOMIC(uint32_t) *bitmap;
    uint32_t              mask_idx;
};

static struct KOS_MARK_LOC_S get_mark_location(KOS_OBJ_ID obj_id)
{
    const uintptr_t offs_in_page = (uintptr_t)obj_id & (uintptr_t)(KOS_PAGE_SIZE - 1);
    const uint32_t  slot_idx     = (uint32_t)(offs_in_page - KOS_SLOTS_OFFS) >> KOS_OBJ_ALIGN_BITS;

    const uintptr_t page_addr    = (uintptr_t)obj_id & ~(uintptr_t)(KOS_PAGE_SIZE - 1);
    uint32_t *const bitmap       = (uint32_t *)(page_addr + KOS_BITMAP_OFFS) + (slot_idx >> 4);

    struct KOS_MARK_LOC_S mark_loc = { KOS_NULL, 0 };

    mark_loc.bitmap   = (KOS_ATOMIC(uint32_t) *)bitmap;
    mark_loc.mask_idx = (slot_idx & 0xFU) * 2;

    return mark_loc;
}

static KOS_ATOMIC(uint32_t) *get_bitmap(KOS_PAGE *page)
{
    return (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + KOS_BITMAP_OFFS);
}

static void advance_marking(struct KOS_MARK_LOC_S *mark_loc,
                            uint32_t               num_slots)
{
    const uint32_t mask_idx = mark_loc->mask_idx + num_slots * 2U;

    mark_loc->bitmap   += mask_idx >> 5;
    mark_loc->mask_idx =  mask_idx & 0x1FU;
}

static int find_first_set(uint32_t value)
{
#if defined(__GNUC__) && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
#   define KOS_HAS_BUILTIN_CTZ
#elif defined(__clang__)
#   if __has_builtin(__builtin_ctz)
#       define KOS_HAS_BUILTIN_CTZ
#   endif
#endif

#ifdef KOS_HAS_BUILTIN_CTZ
    return __builtin_ctz(value);
#elif defined(_MSC_VER)
    unsigned long bit;

    _BitScanForward(&bit, value);

    return (int)bit;
#else
    int bit;

    static const signed char de_brujin_bit_pos[32] =
    {
        0,   1, 28 , 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17,  4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18,  6, 11,  5, 10, 9
    };

    bit = de_brujin_bit_pos[((value & -value) * 0x077CB531U) >> 27];

    return bit;
#endif
}

static uint32_t skip_white(KOS_SLOT             **out_ptr,
                           KOS_SLOT              *end,
                           struct KOS_MARK_LOC_S *mark_loc)
{
    KOS_SLOT *ptr;
    uint32_t  value    = KOS_atomic_read_relaxed_u32(*mark_loc->bitmap);
    uint32_t  mask_idx = mark_loc->mask_idx;
    int       slot_idx;
    int       bit_idx;

    value >>= mask_idx;

    if (value & 3U)
        return value & 3U;

    ptr = *out_ptr;

    if ( ! value) {

        ptr -= mask_idx >> 1;

        mask_idx = 0;

        do {
            ++mark_loc->bitmap;
            ptr += 16U;

            if (ptr >= end)
                return 0U;

            value = KOS_atomic_read_relaxed_u32(*mark_loc->bitmap);
        } while ( ! value);
    }

    slot_idx = find_first_set(value) >> 1;
    bit_idx  = slot_idx * 2;

    mask_idx +=  bit_idx;
    ptr      +=  slot_idx;
    value    >>= bit_idx;

    value &= 3U;

    assert(value);

    mark_loc->mask_idx = mask_idx;

    *out_ptr = ptr;

    return value;
}

static uint32_t get_marking(const struct KOS_MARK_LOC_S *mark_loc)
{
    const uint32_t marking = KOS_atomic_read_relaxed_u32(*(mark_loc->bitmap));

    return (marking >> mark_loc->mask_idx) & COLORMASK;
}

static uint32_t set_mark_state_loc(struct KOS_MARK_LOC_S mark_loc,
                                   enum KOS_MARK_STATE_E state)
{
    const uint32_t mask  = (uint32_t)state << mark_loc.mask_idx;
    uint32_t       value = KOS_atomic_read_relaxed_u32(*mark_loc.bitmap);

    while ( ! (value & mask)) {

        if (KOS_atomic_cas_weak_u32(*mark_loc.bitmap, value, value | mask))
            return 1;

        value = KOS_atomic_read_relaxed_u32(*mark_loc.bitmap);
    }

    return 0;
}

static void clear_page_mark_state(KOS_PAGE *page, uint32_t size)
{
    KOS_ATOMIC(uint32_t) *bitmap = get_bitmap(page);

    uint32_t num_slots = size >> KOS_OBJ_ALIGN_BITS;

    while (num_slots >= 16U) {
        KOS_atomic_write_relaxed_u32(*bitmap, 0U);

        ++bitmap;
        num_slots -= 16U;
    }

    if (num_slots) {
        uint32_t value = KOS_atomic_read_relaxed_u32(*bitmap);

        value &= ~0U << (num_slots * 2);

        KOS_atomic_write_relaxed_u32(*bitmap, value);
    }
}

static uint32_t set_mark_state(KOS_OBJ_ID            obj_id,
                               enum KOS_MARK_STATE_E state)
{
    uint32_t marked = 0;

    assert(((uintptr_t)obj_id & 0xFFFFFFFFU) != 0xDDDDDDDDU);

    if ( ! IS_BAD_PTR(obj_id) && kos_is_tracked_object(obj_id)) {

        struct KOS_MARK_LOC_S mark_loc;

        if (kos_is_heap_object(obj_id))

            mark_loc = get_mark_location(obj_id);

        else {

            const KOS_OBJ_ID back_ref = *(KOS_OBJ_ID *)((intptr_t)obj_id - 1 - sizeof(KOS_OBJ_ID));

            assert(kos_is_heap_object(back_ref));

            assert(READ_OBJ_TYPE(back_ref) == OBJ_HUGE_TRACKER);

            mark_loc = get_mark_location(back_ref);
        }

        marked += set_mark_state_loc(mark_loc, state);
    }

    return marked;
}

static void push_scheduled(KOS_HEAP       *heap,
                           KOS_MARK_GROUP *group)
{
    KOS_PERF_CNT(mark_groups_sched);

    push_mark_group(&heap->objects_to_mark, group);
}

static int schedule_for_marking(KOS_MARK_CONTEXT *mark_ctx,
                                KOS_OBJ_ID        obj_id)
{
    KOS_HEAP *const heap    = mark_ctx->heap;
    KOS_MARK_GROUP *current = mark_ctx->current;
    uint32_t        idx;

    if ( ! current) {

        current = pop_mark_group(&heap->free_mark_groups);

        if ( ! current) {
            current = (KOS_MARK_GROUP *)KOS_malloc(sizeof(KOS_MARK_GROUP));
            if ( ! current)
                return KOS_ERROR_OUT_OF_MEMORY;

            KOS_PERF_CNT(mark_groups_alloc);
        }

        current->num_objs = 0;
        mark_ctx->current = current;
    }

    assert(current->num_objs < sizeof(current->objs) / sizeof(current->objs[0]));

    idx = current->num_objs++;

    current->objs[idx] = obj_id;

    if (idx == (sizeof(current->objs) / sizeof(current->objs[0])) - 1) {
        mark_ctx->current = KOS_NULL;
        push_scheduled(mark_ctx->heap, current);
    }

    return KOS_SUCCESS;
}

static KOS_MARK_GROUP *get_next_scheduled_mark_group(KOS_MARK_CONTEXT *mark_ctx)
{
    KOS_HEAP *const heap  = mark_ctx->heap;
    KOS_MARK_GROUP *group = pop_mark_group(&heap->objects_to_mark);

    if ( ! group) {
        group = mark_ctx->current;
        mark_ctx->current = KOS_NULL;
    }

    return group;
}

static void free_mark_group(KOS_MARK_CONTEXT *mark_ctx, KOS_MARK_GROUP *group)
{
    KOS_HEAP *const heap = mark_ctx->heap;

    push_mark_group(&heap->free_mark_groups, group);
}

static void free_all_mark_groups(KOS_MARK_CONTEXT *mark_ctx)
{
    KOS_MARK_GROUP *group = get_next_scheduled_mark_group(mark_ctx);

    for ( ; group; group = get_next_scheduled_mark_group(mark_ctx))
        free_mark_group(mark_ctx, group);
}

static int mark_object_gray(KOS_MARK_CONTEXT *mark_ctx, KOS_OBJ_ID obj_id)
{
    int error = KOS_SUCCESS;

    if (set_mark_state(obj_id, GRAY))
        error = schedule_for_marking(mark_ctx, obj_id);

    return error;
}

static int mark_object_black(KOS_MARK_CONTEXT *mark_ctx,
                             KOS_OBJ_ID        obj_id);

static void init_mark_context(KOS_MARK_CONTEXT *mark_ctx, KOS_HEAP *heap)
{
    mark_ctx->current = KOS_NULL;
    mark_ctx->heap    = heap;
}

static int perform_gray_to_black_marking(KOS_MARK_CONTEXT       *mark_ctx,
                                         enum WALK_THREAD_TYPE_E helper)
{
    KOS_MARK_GROUP *group;
    int             error = KOS_SUCCESS;

    PROF_ZONE(GC)

    begin_walk(mark_ctx->heap, helper);

    group = get_next_scheduled_mark_group(mark_ctx);

    while (group && ! error) {

        KOS_OBJ_ID       *ptr = &group->objs[0];
        KOS_OBJ_ID *const end = ptr + group->num_objs;

        while ((ptr != end) && ! error) {

            const KOS_OBJ_ID obj_id = *(ptr++);

            error = mark_object_black(mark_ctx, obj_id);
        }

        free_mark_group(mark_ctx, group);

        group = get_next_scheduled_mark_group(mark_ctx);
    }

    if (error) {
        group = mark_ctx->current;

        if (group) {
            mark_ctx->current = KOS_NULL;
            free_mark_group(mark_ctx, group);
        }
    }

    end_walk(mark_ctx->heap, helper);

    return error;
}

static int mark_children_gray(KOS_MARK_CONTEXT *mark_ctx,
                              KOS_OBJ_ID        obj_id)
{
    int error = KOS_SUCCESS;

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
                TRY(mark_object_gray(mark_ctx, OBJPTR(STRING, obj_id)->ref.obj_id));
            break;

        default:
            assert(READ_OBJ_TYPE(obj_id) == OBJ_OBJECT);
            TRY(mark_object_black(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT, obj_id)->props)));
            TRY(mark_object_gray(mark_ctx, OBJPTR(OBJECT, obj_id)->prototype));
            break;

        case OBJ_ARRAY:
            TRY(mark_object_black(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY, obj_id)->data)));
            break;

        case OBJ_BUFFER:
            set_mark_state(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data), BLACK);
            break;

        case OBJ_FUNCTION:
            /* TODO make these atomic */
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->module));
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->name));
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->closures));
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->defaults));
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->arg_map));
            TRY(mark_object_gray(mark_ctx, OBJPTR(FUNCTION, obj_id)->generator_stack_frame));
            break;

        case OBJ_CLASS:
            TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->prototype)));
            TRY(mark_object_black(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->props)));
            /* TODO make these atomic */
            TRY(mark_object_gray(mark_ctx, OBJPTR(CLASS, obj_id)->module));
            TRY(mark_object_gray(mark_ctx, OBJPTR(CLASS, obj_id)->name));
            TRY(mark_object_gray(mark_ctx, OBJPTR(CLASS, obj_id)->closures));
            TRY(mark_object_gray(mark_ctx, OBJPTR(CLASS, obj_id)->defaults));
            TRY(mark_object_gray(mark_ctx, OBJPTR(CLASS, obj_id)->arg_map));
            break;

        case OBJ_OBJECT_STORAGE: {
            KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id)->items[0];
            KOS_PITEM *end  = item + OBJPTR(OBJECT_STORAGE, obj_id)->capacity;
            for ( ; item < end; ++item) {
                TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(item->key)));
                TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(item->value)));
            }

            TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, obj_id)->new_prop_table)));
            break;
        }

        case OBJ_ARRAY_STORAGE: {
            KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(ARRAY_STORAGE, obj_id)->buf[0];
            KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(ARRAY_STORAGE, obj_id)->capacity;
            for ( ; item < end; ++item)
                TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(*item)));

            TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, obj_id)->next)));
            break;
        }

        case OBJ_DYNAMIC_PROP:
            /* TODO make these atomic */
            TRY(mark_object_black(mark_ctx, OBJPTR(DYNAMIC_PROP, obj_id)->getter));
            TRY(mark_object_black(mark_ctx, OBJPTR(DYNAMIC_PROP, obj_id)->setter));
            break;

        case OBJ_ITERATOR:
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->obj));
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->prop_obj));
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->key_table));
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->returned_keys));
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->last_key));
            TRY(mark_object_gray(mark_ctx, OBJPTR(ITERATOR, obj_id)->last_value));
            break;

        case OBJ_MODULE:
            /* TODO lock gc during module setup */
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->name));
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->path));
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->constants));
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->global_names));
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->globals));
            TRY(mark_object_black(mark_ctx, OBJPTR(MODULE, obj_id)->module_names));
            TRY(mark_object_black(mark_ctx, KOS_atomic_read_relaxed_obj(
                                            OBJPTR(MODULE, obj_id)->priv)));
            break;

        case OBJ_STACK: {
            KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(STACK, obj_id)->buf[0];
            KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(STACK, obj_id)->size;
            for ( ; item < end; ++item)
                TRY(mark_object_gray(mark_ctx, KOS_atomic_read_relaxed_obj(*item)));
            break;
        }

        case OBJ_HUGE_TRACKER: {
            KOS_OBJ_ID object = OBJPTR(HUGE_TRACKER, obj_id)->object;

            if ( ! IS_BAD_PTR(object)) {
                assert(kos_is_tracked_object(object));
                assert( ! kos_is_heap_object(object));
                TRY(mark_children_gray(mark_ctx, object));
            }
            break;
        }
    }

cleanup:
    return error;
}

static int mark_object_black(KOS_MARK_CONTEXT *mark_ctx,
                             KOS_OBJ_ID        obj_id)
{
    int error = KOS_SUCCESS;

    set_mark_state(obj_id, BLACK);

    if ( ! IS_BAD_PTR(obj_id) && kos_is_tracked_object(obj_id))
        error = mark_children_gray(mark_ctx, obj_id);

    return error;
}

static int gray_to_black(KOS_MARK_CONTEXT *mark_ctx, KOS_HEAP *heap)
{
    int error;

    assert(heap->walk_threads == 0);

    error = perform_gray_to_black_marking(mark_ctx, WALK_MAIN_THREAD);

    if ( ! error)
        error = heap->mark_error;

    return error;
}

static int mark_roots_in_context(KOS_MARK_CONTEXT *mark_ctx, KOS_CONTEXT ctx)
{
    uint32_t   error = KOS_SUCCESS;
    KOS_LOCAL *local;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE);

    TRY(mark_object_black(mark_ctx, ctx->exception));
    TRY(mark_object_black(mark_ctx, ctx->stack));

    for (local = ctx->local_list; local; local = local->next)
        TRY(mark_object_black(mark_ctx, local->o));

    for (local = (KOS_LOCAL *)ctx->ulocal_list; local; local = local->next)
        TRY(mark_object_black(mark_ctx, local->o));

cleanup:
    return error;
}

static int mark_roots_in_threads(KOS_MARK_CONTEXT *mark_ctx, KOS_INSTANCE *inst)
{
    uint32_t       i;
    uint32_t       error       = KOS_SUCCESS;
    const uint32_t max_threads = inst->threads.max_threads;

    kos_lock_mutex(inst->threads.new_mutex);

    for (i = 0; i < max_threads; i++) {

        KOS_THREAD *thread = (KOS_THREAD *)KOS_atomic_read_relaxed_ptr(inst->threads.threads[i]);

        if ( ! thread)
            continue;

        TRY(mark_object_gray(mark_ctx, thread->thread_func));
        TRY(mark_object_gray(mark_ctx, thread->this_obj));
        TRY(mark_object_gray(mark_ctx, thread->args_obj));
        TRY(mark_object_gray(mark_ctx, thread->retval));
        TRY(mark_object_gray(mark_ctx, thread->exception));
    }

cleanup:
    kos_unlock_mutex(inst->threads.new_mutex);

    return error;
}

static int mark_roots(KOS_CONTEXT ctx, KOS_MARK_CONTEXT *mark_ctx)
{
    PROF_ZONE(GC)

    int                 error = KOS_SUCCESS;
    KOS_INSTANCE *const inst  = ctx->inst;

    TRY(mark_object_black(mark_ctx, inst->prototypes.object_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.number_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.integer_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.float_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.string_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.boolean_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.array_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.buffer_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.function_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.class_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.generator_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.exception_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.generator_end_proto));
    TRY(mark_object_black(mark_ctx, inst->prototypes.thread_proto));

    TRY(mark_object_black(mark_ctx, inst->modules.init_module));
    TRY(mark_object_black(mark_ctx, inst->modules.search_paths));
    TRY(mark_object_black(mark_ctx, inst->modules.module_names));
    TRY(mark_object_black(mark_ctx, inst->modules.modules));
    TRY(mark_object_black(mark_ctx, inst->modules.module_inits));

    TRY(mark_roots_in_threads(mark_ctx, inst));

    TRY(mark_object_black(mark_ctx, inst->args));

    kos_lock_mutex(inst->threads.ctx_mutex);

    ctx = &inst->threads.main_thread;

    for ( ; ctx; ctx = ctx->next) {
        error = mark_roots_in_context(mark_ctx, ctx);
        if (error)
            break;
    }

    kos_unlock_mutex(inst->threads.ctx_mutex);

cleanup:
    return error;
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
                (struct KOS_LOCKED_PAGES_S *)KOS_malloc(sizeof(struct KOS_LOCKED_PAGES_S));

            if ( ! locked_pages) {
                fprintf(stderr, "Failed to allocate memory to store locked pages\n");
                exit(1);
            }

            locked_pages->num_pages = 0;
            locked_pages->next      = KOS_NULL;
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
#define clear_opaque(hdr) ((void)0)
#else
static void debug_fill_slots(KOS_PAGE* page)
{
    memset((uint8_t *)page + KOS_BITMAP_OFFS,
           0xDDU,
           KOS_PAGE_SIZE - KOS_BITMAP_OFFS);
}

static void clear_opaque(KOS_OBJ_HEADER *hdr)
{
    memset((uint8_t *)hdr + sizeof(KOS_OPAQUE),
           0xDDU,
           kos_get_object_size(*hdr) - sizeof(KOS_OPAQUE));
}
#endif

static void reclaim_free_pages(KOS_HEAP      *heap,
                               KOS_PAGE_LIST *free_pages,
                               KOS_GC_STATS  *stats)
{
    KOS_PAGE *tail     = KOS_NULL;
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

    free_pages->head = KOS_NULL;
    free_pages->tail = KOS_NULL;

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

        KOS_free(cur_locked_page_list);
    }

    heap->locked_pages_first = KOS_NULL;
    heap->locked_pages_last  = KOS_NULL;

    return num_unlocked;
}
#else
static unsigned unlock_pages(KOS_HEAP      *heap,
                             KOS_PAGE_LIST *free_pages,
                             KOS_GC_STATS  *stats)
{
    KOS_PAGE    **page_ptr     = &free_pages->head;
    KOS_PAGE     *page         = *page_ptr;
    KOS_PAGE     *prev         = KOS_NULL;
    KOS_PAGE_LIST reclaimed    = { KOS_NULL, KOS_NULL };
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
        else if (size <= 256)
            KOS_PERF_CNT(evac_object_size[2]);
        else
            KOS_PERF_CNT(evac_object_size[3]);
#endif
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void mark_unused_objects_opaque(KOS_CONTEXT   ctx,
                                       KOS_PAGE     *page,
                                       KOS_GC_STATS *stats)
{
    struct KOS_MARK_LOC_S mark_loc = { KOS_NULL, 0 };

    KOS_SLOT *ptr = get_slots(page);
    KOS_SLOT *end = ptr + KOS_atomic_read_relaxed_u32(page->num_allocated);

    mark_loc.bitmap = get_bitmap(page);

    while (ptr < end) {

        KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
        const uint32_t        color = get_marking(&mark_loc);
        KOS_OBJ_HEADER        st    = *hdr;
        uint32_t              size;

        if ( ! IS_SMALL_INT(st.size_and_type)) {
            const KOS_OBJ_ID new_obj = st.size_and_type;
            assert( ! IS_BAD_PTR(new_obj));
            assert(kos_is_heap_object(new_obj));
            assert(READ_OBJ_TYPE(new_obj) <= OBJ_LAST_POSSIBLE);
            st = *(KOS_OBJ_HEADER *)((intptr_t)new_obj - 1);
        }

        size = kos_get_object_size(st);

        assert(size > 0U);
        assert(color != GRAY);
        assert(size <= (size_t)((uint8_t *)end - (uint8_t *)ptr));

        if ( ! color) {
            finalize_object(ctx, hdr, stats);
            kos_set_object_type(*hdr, OBJ_OPAQUE);
            clear_opaque(hdr);
        }

        advance_marking(&mark_loc, size >> KOS_OBJ_ALIGN_BITS);

        ptr = (KOS_SLOT *)((uint8_t *)ptr + size);
    }
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
#endif

        assert(IS_SMALL_INT(new_obj) || IS_BAD_PTR(new_obj) || READ_OBJ_TYPE(new_obj) <= OBJ_LAST_POSSIBLE);

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

        case OBJ_BUFFER_STORAGE:
            if ( ! (((KOS_BUFFER_STORAGE *)hdr)->flags & KOS_EXTERNAL_STORAGE))
                ((KOS_BUFFER_STORAGE *)hdr)->ptr = &((KOS_BUFFER_STORAGE *)hdr)->buf[0];
            break;

        case OBJ_FUNCTION:
            update_child_ptr(&((KOS_FUNCTION *)hdr)->module);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->name);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->closures);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->defaults);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->arg_map);
            update_child_ptr(&((KOS_FUNCTION *)hdr)->generator_stack_frame);
            break;

        case OBJ_CLASS:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->prototype);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_CLASS *)hdr)->props);
            update_child_ptr(&((KOS_CLASS *)hdr)->module);
            update_child_ptr(&((KOS_CLASS *)hdr)->name);
            update_child_ptr(&((KOS_CLASS *)hdr)->closures);
            update_child_ptr(&((KOS_CLASS *)hdr)->defaults);
            update_child_ptr(&((KOS_CLASS *)hdr)->arg_map);
            break;

        case OBJ_DYNAMIC_PROP:
            update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->getter);
            update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->setter);
            break;

        case OBJ_ITERATOR:
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->obj);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->prop_obj);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->key_table);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->returned_keys);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->last_key);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_ITERATOR *)hdr)->last_value);
            break;

        case OBJ_MODULE:
            update_child_ptr(&((KOS_MODULE *)hdr)->name);
            update_child_ptr(&((KOS_MODULE *)hdr)->path);
            update_child_ptr(&((KOS_MODULE *)hdr)->constants);
            update_child_ptr(&((KOS_MODULE *)hdr)->global_names);
            update_child_ptr(&((KOS_MODULE *)hdr)->globals);
            update_child_ptr(&((KOS_MODULE *)hdr)->module_names);
            update_child_ptr((KOS_OBJ_ID *)&((KOS_MODULE *)hdr)->priv);
            break;

        case OBJ_STACK:
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_STACK *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_STACK *)hdr)->size;
                for ( ; item < end; ++item)
                    update_child_ptr((KOS_OBJ_ID *)item);
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
    clear_page_mark_state(incomplete->page, incomplete->offset);
    clear_opaque(hdr);

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

static void update_pages_after_evacuation(KOS_HEAP               *heap,
                                          enum WALK_THREAD_TYPE_E helper)
{
    PROF_ZONE(GC)

    if (KOS_atomic_read_relaxed_ptr(heap->walk_pages)) {

        KOS_PAGE *page;

        begin_walk(heap, helper);

        for (page = get_next_page(heap); page; page = get_next_page(heap))
            update_page_after_evacuation(page, 0);

        end_walk(heap, helper);
    }
}

static void update_threads_after_evacuation(KOS_INSTANCE *inst)
{
    uint32_t       i;
    const uint32_t max_threads = inst->threads.max_threads;

    if ( ! inst->threads.threads)
        return;

    kos_lock_mutex(inst->threads.new_mutex);

    for (i = 0; i < max_threads; i++) {

        KOS_THREAD *thread = (KOS_THREAD *)KOS_atomic_read_relaxed_ptr(inst->threads.threads[i]);

        if ( ! thread)
            continue;

        update_child_ptr(&thread->thread_func);
        update_child_ptr(&thread->this_obj);
        update_child_ptr(&thread->args_obj);
        update_child_ptr((KOS_OBJ_ID *)&thread->retval);
        update_child_ptr((KOS_OBJ_ID *)&thread->exception);
    }

    kos_unlock_mutex(inst->threads.new_mutex);
}

static void update_after_evacuation(KOS_CONTEXT ctx)
{
    PROF_ZONE(GC)

    KOS_INSTANCE *const inst = ctx->inst;
    KOS_HEAP           *heap = &inst->heap;

    assert(heap->walk_threads == 0);
    assert(KOS_atomic_read_relaxed_ptr(heap->walk_pages) == 0);

    KOS_atomic_write_relaxed_ptr(heap->walk_pages, heap->used_pages.head);

    assert( ! ctx->cur_page);

    update_pages_after_evacuation(heap, WALK_MAIN_THREAD);

    /* Update object pointers in instance */

    kos_lock_mutex(inst->threads.ctx_mutex);

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

    update_threads_after_evacuation(inst);

    /* Update object pointers in thread contexts */

    ctx = &inst->threads.main_thread;

    while (ctx) {

        KOS_LOCAL *local;

        assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE);
        assert(!ctx->cur_page);

        update_child_ptr(&ctx->exception);
        update_child_ptr(&ctx->stack);

        for (local = ctx->local_list; local; local = local->next)
            update_child_ptr(&local->o);

        for (local = (KOS_LOCAL *)ctx->ulocal_list; local; local = local->next)
            update_child_ptr(&local->o);

        ctx = ctx->next;
    }

    kos_unlock_mutex(inst->threads.ctx_mutex);
}

static uint32_t get_num_slots_used(KOS_PAGE *page, uint32_t num_allocated)
{
    uint32_t              color;
    uint32_t              num_used = 0;
    KOS_SLOT             *ptr      = get_slots(page);
    KOS_SLOT       *const end      = ptr + num_allocated;
    struct KOS_MARK_LOC_S mark_loc = { KOS_NULL, 0 };

    mark_loc.bitmap = get_bitmap(page);

    assert(num_allocated == KOS_atomic_read_relaxed_u32(page->num_allocated));

    color = skip_white(&ptr, end, &mark_loc);

    while (color) {

        KOS_OBJ_HEADER *const hdr   = (KOS_OBJ_HEADER *)ptr;
        const uint32_t        size  = kos_get_object_size(*hdr);
        const uint32_t        slots = size >> KOS_OBJ_ALIGN_BITS;

        assert(color & BLACK);
        assert((uint8_t *)ptr + size <= (uint8_t *)end);

        num_used += slots;

        advance_marking(&mark_loc, slots);

        ptr = (KOS_SLOT *)((uint8_t *)ptr + size);

        color = skip_white(&ptr, end, &mark_loc);
    }

    return num_used;
}

#define PAGE_ALREADY_EVACED 1U

static int evacuate(KOS_CONTEXT              ctx,
                    KOS_PAGE_LIST           *free_pages,
                    KOS_GC_STATS            *out_stats,
                    struct KOS_INCOMPLETE_S *incomplete)
{
    PROF_ZONE(GC)

    KOS_HEAP    *heap  = get_heap(ctx);
    int          error = KOS_SUCCESS;
    KOS_PAGE    *page  = heap->used_pages.head;
    KOS_PAGE    *next;
    KOS_GC_STATS stats = *out_stats;

    assert( ! KOS_is_exception_pending(ctx));

    heap->used_pages.head = KOS_NULL;
    heap->used_pages.tail = KOS_NULL;

    for ( ; page; page = next) {

        struct KOS_MARK_LOC_S mark_loc = { KOS_NULL, 0 };

        unsigned       num_evac       = 0;
        const uint32_t num_allocated  = KOS_atomic_read_relaxed_u32(page->num_allocated);
        const uint32_t page_flags     = KOS_atomic_read_relaxed_u32(page->flags);
        const uint32_t num_slots_used = (page_flags == PAGE_ALREADY_EVACED) ? num_allocated :
                                        get_num_slots_used(page, num_allocated);
        KOS_SLOT      *ptr            = get_slots(page);
        KOS_SLOT      *end            = ptr + num_allocated;

        heap->used_heap_size -= used_page_size(page);

        next = page->next;

        mark_loc.bitmap = get_bitmap(page);

        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        #define UNUSED_SLOTS ((KOS_SLOTS_PER_PAGE * (100U - KOS_MIGRATION_THRESH)) / 100U)

        if (
#ifdef CONFIG_MAD_GC
            page_flags == PAGE_ALREADY_EVACED
#else
            num_allocated - num_slots_used < UNUSED_SLOTS
#endif
            ) {

            gc_trace(("GC ctx=%p -- retain page %p\n", (void *)ctx, (void *)page));

            /* Mark unused objects as opaque so they don't participate in
             * pointer update after evacuation. */
            if (num_slots_used < num_allocated)
                mark_unused_objects_opaque(ctx, page, &stats);

            push_page_with_objects(heap, page);

            if (num_slots_used != PAGE_ALREADY_EVACED) {
                ++stats.num_pages_kept;
                stats.size_kept += num_slots_used << KOS_OBJ_ALIGN_BITS;
                KOS_atomic_write_relaxed_u32(page->flags, PAGE_ALREADY_EVACED);
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
            assert(size <= (size_t)((uint8_t *)end - (uint8_t *)ptr));

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

                        if (ctx->inst->flags & KOS_INST_VERBOSE)
                            printf("GC is memory constrained%s\n",
                                   error ? "" : ", but recovered");
                    }

                    if (error) {

                        assert( ! ctx->cur_page);

                        set_marking_in_pages(heap->used_pages.head, BLACK, PAGE_ALREADY_EVACED);

                        mark_unused_objects_opaque(ctx, page, &stats);

                        if ( ! save_incomplete_page(hdr, page, incomplete)) {
                            gc_trace(("GC OOM during evac\n"));
                            push_page_with_objects(heap, page);
                        }
                        else {
                            gc_trace(("GC OOM, incomplete evac, page %p, end %p, evac obj %p\n",
                                      (void *)page, (void *)end, (void *)hdr));
                        }

                        /* Put back the remaining pages on the heap. */
                        for (page = next; page; page = next) {
                            next = page->next;
                            mark_unused_objects_opaque(ctx, page, &stats);
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
            gc_trace(("GC ctx=%p -- drop page %p\n", (void *)ctx, (void *)page));

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
    if (heap->used_heap_size >= heap->gc_threshold)
        heap->gc_threshold = heap->max_heap_size;
    else
        heap->gc_threshold = heap->max_heap_size * KOS_GC_THRESHOLD / 100U;
}

static void engage_in_gc(KOS_CONTEXT ctx, enum GC_STATE_E new_state)
{
    KOS_HEAP *const heap = get_heap(ctx);

    KOS_atomic_write_relaxed_u32(ctx->gc_state, new_state);

    if (KOS_atomic_read_relaxed_u32(heap->gc_state) == GC_INIT) {
        if (--heap->threads_to_stop == 0)
            kos_signal_cond_var(heap->engagement_cond);
    }
    else {
        assert( ! heap->threads_to_stop);
    }
}

static void help_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);
    enum GC_STATE_E gc_state;

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(heap->gc_state);

    if (gc_state == GC_INACTIVE)
        return;

    PROF_ZONE(GC)

    if (ctx->cur_page) {
        assert(gc_state == GC_INIT);
    }

    release_current_page_locked(ctx);

    engage_in_gc(ctx, GC_ENGAGED);

    do {
        assert( ! ctx->cur_page);

        switch (gc_state) {

            case GC_MARK: {

                KOS_MARK_CONTEXT mark_ctx;
                int              error;

                init_mark_context(&mark_ctx, heap);
                error = perform_gray_to_black_marking(&mark_ctx, WALK_HELPER_THREAD);

                if (error)
                    heap->mark_error = error;
                break;
            }

            /* TODO help with evac */

            case GC_UPDATE:
                update_pages_after_evacuation(heap, WALK_HELPER_THREAD);
                break;

            default:
                assert(gc_state != GC_INACTIVE);
                break;
        }

        /* Wait for GC state to change or for more pages to be available for
         * processing by helper threads. */
        kos_wait_cond_var(heap->helper_cond, heap->mutex);

        gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(heap->gc_state);

    } while (gc_state != GC_INACTIVE);

    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_INACTIVE);
}

void KOS_help_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    if (KOS_atomic_read_relaxed_u32(heap->gc_state) >= GC_INIT) {

        kos_lock_mutex(heap->mutex);

        help_gc(ctx);

        kos_unlock_mutex(heap->mutex);
    }
}

#ifdef CONFIG_MAD_GC
int kos_trigger_mad_gc(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);
    int             error;

    /* Don't try to collect garbage when the garbage collector is running */
    if (KOS_atomic_read_relaxed_u32(ctx->gc_state) != GC_INACTIVE)
        return KOS_SUCCESS;

    kos_lock_mutex(heap->mutex);
    error = try_collect_garbage(ctx);
    kos_unlock_mutex(heap->mutex);

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

    return error;
}
#endif

void KOS_suspend_context(KOS_CONTEXT ctx)
{
    KOS_HEAP *const heap = get_heap(ctx);

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_INACTIVE);

    kos_lock_mutex(heap->mutex);

    release_current_page_locked(ctx);

    engage_in_gc(ctx, GC_SUSPENDED);

    kos_unlock_mutex(heap->mutex);
}

int KOS_resume_context(KOS_CONTEXT ctx)
{
    enum GC_STATE_E gc_state;
    int             error = KOS_SUCCESS;
    KOS_HEAP *const heap  = get_heap(ctx);

    assert(KOS_atomic_read_relaxed_u32(ctx->gc_state) == GC_SUSPENDED);

    kos_lock_mutex(heap->mutex);

    KOS_atomic_write_release_u32(ctx->gc_state, GC_INACTIVE);

    gc_state = (enum GC_STATE_E)KOS_atomic_read_relaxed_u32(heap->gc_state);

    if (gc_state == GC_INACTIVE) {

#ifdef CONFIG_MAD_GC

        error = try_collect_garbage(ctx);

        if (error == KOS_SUCCESS_RETURN)
            error = KOS_SUCCESS;
#endif
    }
    else
        help_gc(ctx);

    kos_unlock_mutex(heap->mutex);

    return error;
}

int KOS_collect_garbage(KOS_CONTEXT   ctx,
                        KOS_GC_STATS *out_stats)
{
    PROF_ZONE(GC)

    uint64_t         time_0;
    uint64_t         time_1;
    KOS_HEAP        *heap       = get_heap(ctx);
    KOS_MARK_CONTEXT mark_ctx;
    KOS_PAGE_LIST    free_pages = { KOS_NULL, KOS_NULL };
    KOS_GC_STATS     stats      = KOS_GC_STATS_INIT(0U);
    int              error      = KOS_SUCCESS;

    /***********************************************************************/
    /* Initialize GC */

    time_0 = KOS_get_time_us();

    kos_lock_mutex(heap->mutex);

    if (KOS_atomic_read_relaxed_u32(heap->gc_state) != GC_INACTIVE) {

        help_gc(ctx);

        kos_unlock_mutex(heap->mutex);

        if (out_stats)
            *out_stats = stats;

        return KOS_SUCCESS;
    }

    KOS_atomic_write_release_u32(heap->gc_state, GC_INIT);

    stats.initial_heap_size   = heap->heap_size;
    stats.initial_malloc_size = heap->malloc_size;

    gc_trace(("GC ctx=%p begin cycle %u\n", (void *)ctx, heap->gc_cycles));

    KOS_PERF_CNT(gc_cycles);
    PROF_FRAME_START("GC")

    ++heap->gc_cycles;

    KOS_atomic_write_relaxed_u32(ctx->gc_state, GC_ENGAGED);

    release_current_page_locked(ctx);

    verify_heap_used_size(heap);

    /***********************************************************************/
    /* Phase 1: Stop all threads, clear marking and mark roots */

    stop_the_world(ctx->inst); /* Remaining threads enter help_gc() */

    time_1             = KOS_get_time_us();
    stats.time_stop_us = (unsigned)(time_1 - time_0);
    time_0             = time_1;

    stats.initial_used_heap_size = heap->used_heap_size;

    clear_marking(heap);

    init_mark_context(&mark_ctx, heap);
    error = mark_roots(ctx, &mark_ctx);

    /***********************************************************************/
    /* Phase 2: Perform marking */

    if ( ! error) {
        KOS_atomic_write_relaxed_u32(heap->gc_state, GC_MARK);

        error = gray_to_black(&mark_ctx, heap);
    }

    assert( ! error || (error == KOS_ERROR_OUT_OF_MEMORY));

    /***********************************************************************/
    /* Phase 3: Evacuate and reclaim free pages */

    time_1             = KOS_get_time_us();
    stats.time_mark_us = (unsigned)(time_1 - time_0);
    time_0             = time_1;

    if ( ! error) {
        do {
            uint32_t                prev_num_freed;
            struct KOS_INCOMPLETE_S incomplete = { KOS_NULL, 0 };

            KOS_atomic_write_relaxed_u32(heap->gc_state, GC_EVACUATE);

            /* Evacuation performs heap allocations and will lock the mutex again */
            kos_unlock_mutex(heap->mutex);

            error = evacuate(ctx, &free_pages, &stats, &incomplete);

            kos_lock_mutex(heap->mutex);

            time_1              = KOS_get_time_us();
            stats.time_evac_us += (unsigned)(time_1 - time_0);
            time_0              = time_1;

            KOS_atomic_write_relaxed_u32(heap->gc_state, GC_UPDATE);

            assert( ! incomplete.page || error);

            update_after_evacuation(ctx);

            update_incomplete_page(heap, &incomplete);

            prev_num_freed = stats.num_pages_freed;

            reclaim_free_pages(heap, &free_pages, &stats);

            time_1                = KOS_get_time_us();
            stats.time_update_us += (unsigned)(time_1 - time_0);
            time_0                = time_1;

            /* If there was an error during evacuation and we cannot
             * reclaim any freed pages, throw OOM exception. */
            if (error && (prev_num_freed == stats.num_pages_freed)) {
                assert(error == KOS_ERROR_OUT_OF_MEMORY);
                break;
            }
        } while (error);
    }

    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        free_all_mark_groups(&mark_ctx);

        if (ctx->inst->flags & KOS_INST_VERBOSE)
            printf("GC ran out of memory\n");

        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }

    /***********************************************************************/
    /* Done, finish GC */

    update_gc_threshold(heap);

    stats.heap_size      = heap->heap_size;
    stats.used_heap_size = heap->used_heap_size;
    stats.malloc_size    = heap->malloc_size;

    verify_heap_used_size(heap);

    gc_trace(("GC ctx=%p end cycle\n", (void *)ctx));

    KOS_atomic_write_relaxed_u32(ctx->gc_state,  GC_INACTIVE);
    KOS_atomic_write_relaxed_u32(heap->gc_state, GC_INACTIVE);

    release_helper_threads(heap);

    PROF_FRAME_END("GC")
    PROF_PLOT("heap",     (int64_t)heap->used_heap_size)
    PROF_PLOT("off-heap", (int64_t)heap->malloc_size)

    kos_unlock_mutex(heap->mutex);

    if ( ! error && KOS_is_exception_pending(ctx))
        error = KOS_ERROR_EXCEPTION;

    time_1 = KOS_get_time_us();

    stats.time_finish_us = (unsigned)(time_1 - time_0);
    stats.time_total_us = (unsigned)(stats.time_stop_us +
                                     stats.time_mark_us +
                                     stats.time_evac_us +
                                     stats.time_update_us +
                                     stats.time_finish_us);

    if (ctx->inst->flags & KOS_INST_DEBUG) {
        printf("GC used/total [B] %x/%x -> %x/%x | malloc [B] %x -> %x : retained %u : time %u us\n",
               stats.initial_used_heap_size, stats.initial_heap_size,
               stats.used_heap_size, stats.heap_size,
               stats.initial_malloc_size, stats.malloc_size,
               stats.num_pages_kept, stats.time_total_us);
    }

    if (out_stats)
        *out_stats = stats;

    if ( ! error)
        error = kos_join_finished_threads(ctx, KOS_ONLY_DISOWNED);

    return error;
}

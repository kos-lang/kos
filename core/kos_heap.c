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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include <stdio.h>
#include <string.h>

enum _GC_STATE {
    GC_INACTIVE,
    GC_INIT
};

struct _KOS_POOL_HEADER {
    KOS_ATOMIC(_KOS_POOL *) next;        /* Pointer to next pool header                    */
    void                   *memory;      /* Pointer to the allocated memory                */
    void                   *usable_ptr;  /* Pointer to usable region of memory in the pool */
    uint32_t                alloc_size;  /* Number of allocated bytes                      */
    uint32_t                usable_size; /* Size of the usable region                      */
};

struct _KOS_WASTE_HEADER {
    KOS_ATOMIC(_KOS_WASTE *) next;
    uint32_t                 size;
};

typedef struct _KOS_SLOT_PLACEHOLDER {
    uint8_t _dummy[1 << _KOS_OBJ_ALIGN_BITS];
} _KOS_SLOT;

struct _KOS_PAGE_HEADER {
    KOS_ATOMIC(_KOS_PAGE *) next;
    KOS_ATOMIC(_KOS_SLOT *) first_free_slot; /* Used during object allocation      */
    uint32_t                num_slots;       /* Total number of slots in this page */
    KOS_ATOMIC(uint32_t)    num_slots_used;  /* Used during garbage collection     */
};

#define _KOS_PAGE_HDR_SIZE  (sizeof(struct _KOS_PAGE_HEADER))
#define _KOS_SLOTS_PER_PAGE (((_KOS_PAGE_SIZE - _KOS_PAGE_HDR_SIZE) << 2) / \
                             ((1U << (_KOS_OBJ_ALIGN_BITS + 2)) + 1U))
#define _KOS_BITMAP_SIZE    (((_KOS_SLOTS_PER_PAGE + 15U) & ~15U) >> 2)
#define _KOS_BITMAP_OFFS    ((_KOS_PAGE_HDR_SIZE + 3U) & ~3U)
#define _KOS_SLOTS_OFFS     (_KOS_PAGE_SIZE - (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS))

/* TODO use mutex instead of cas */

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

static struct _KOS_HEAP *_get_heap(KOS_FRAME frame)
{
    return &frame->thread_ctx->ctx->heap;
}

int _KOS_heap_init(KOS_CONTEXT *ctx)
{
    struct _KOS_HEAP *heap = &ctx->heap;

    memset(heap, 0, sizeof(*heap));

    heap->str_oom_id = TO_SMALL_INT(0);

    assert(_KOS_BITMAP_OFFS + _KOS_BITMAP_SIZE <= _KOS_SLOTS_OFFS);
    assert( ! (_KOS_SLOTS_OFFS & 7U));
    assert(_KOS_SLOTS_OFFS + (_KOS_SLOTS_PER_PAGE << _KOS_OBJ_ALIGN_BITS) == _KOS_PAGE_SIZE);

    return _KOS_create_mutex(&heap->mutex);
}

void _KOS_heap_destroy(KOS_CONTEXT *ctx)
{
    for (;;) {
        _KOS_POOL *pool = (_KOS_POOL *)_list_pop((KOS_ATOMIC(void *) *)&ctx->heap.pools);

        if ( ! pool)
            break;

        _KOS_free(pool->memory);
    }

    _KOS_destroy_mutex(&ctx->heap.mutex);
}

static void _register_wasted_region(struct _KOS_HEAP *heap,
                                    void             *ptr,
                                    uint32_t          size)
{
    if (size >= sizeof(_KOS_WASTE)) {

        _KOS_WASTE *waste = (_KOS_WASTE *)ptr;

        waste->size = size;

        _list_push((KOS_ATOMIC(void *) *)&heap->waste, (void *)waste);
    }
}

static _KOS_POOL *_get_pool_header(struct _KOS_HEAP *heap)
{
    _KOS_POOL *pool = (_KOS_POOL *)_list_pop((KOS_ATOMIC(void *) *)&heap->pool_headers);

    if ( ! pool) {

        _KOS_WASTE *discarded = 0;
        _KOS_WASTE *waste;
        _KOS_POOL  *begin;

        for (;;) {

            waste = (_KOS_WASTE *)_list_pop((KOS_ATOMIC(void *) *)&heap->waste);

            if ( ! waste)
                break;

            if (waste->size >= sizeof(_KOS_POOL) && waste->size <= 8U * sizeof(_KOS_POOL))
                break;

            KOS_atomic_write_ptr(waste->next, discarded);
            discarded = waste;
        }

        while (discarded) {
            _KOS_WASTE *to_push = discarded;
            discarded           = (_KOS_WASTE *)KOS_atomic_read_ptr(discarded->next);

            _list_push((KOS_ATOMIC(void *) *)&heap->waste, (void *)to_push);
        }

        if ( ! waste) {
            pool = (_KOS_POOL *)_KOS_malloc(8U * sizeof(_KOS_POOL));
            if ( ! pool)
                return 0;

            pool->memory      = pool;
            pool->alloc_size  = 8U * sizeof(_KOS_POOL);
            pool->usable_ptr  = 0;
            pool->usable_size = 0U;

            _list_push((KOS_ATOMIC(void *) *)&heap->pools, (void *)pool);

            waste       = (_KOS_WASTE *)(pool + 1);
            waste->size = 7U * sizeof(_KOS_POOL);
        }

        begin = (_KOS_POOL *)waste;
        pool  = begin + waste->size / sizeof(_KOS_POOL);

        for (;;) {
            --pool;
            if (pool == begin)
                break;

            _list_push((KOS_ATOMIC(void *) *)&heap->pool_headers, (void *)pool);
        }
    }

    return pool;
}

static _KOS_POOL *_alloc_pool(struct _KOS_HEAP *heap,
                              uint32_t          alloc_size)
{
    _KOS_POOL     *pool_hdr = 0;
    uint8_t *const pool     = (uint8_t *)_KOS_malloc(alloc_size);
    uint8_t       *begin;
    uint32_t       waste_at_front;

    if ( ! pool)
        return 0;

    begin = (uint8_t *)(((uintptr_t)pool + _KOS_PAGE_SIZE - 1U) & ~(uintptr_t)(_KOS_PAGE_SIZE - 1U));

    waste_at_front = (uint32_t)(begin - pool);

    if (begin == pool || waste_at_front < sizeof(_KOS_POOL)) {
        pool_hdr = _get_pool_header(heap);

        if ( ! pool_hdr) {
            _KOS_free(pool);
            return 0;
        }
    }

    if ( ! pool_hdr) {

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

    _list_push((KOS_ATOMIC(void *) *)&heap->pools, (void *)pool_hdr);

    return pool_hdr;
}

static int _alloc_page_pool(struct _KOS_HEAP *heap)
{
    _KOS_POOL *pool_hdr = _alloc_pool(heap, _KOS_POOL_SIZE);
    uint8_t   *begin;
    uint8_t   *usable_end;
    uint8_t   *page;
    uint32_t   page_size;

    if ( ! pool_hdr)
        return KOS_ERROR_OUT_OF_MEMORY;

    begin      = (uint8_t *)pool_hdr->usable_ptr;
    usable_end = begin + pool_hdr->usable_size;
    page       = (uint8_t *)((uintptr_t)usable_end & ~(uintptr_t)(_KOS_PAGE_SIZE - 1U));
    page_size  = (uint32_t)(usable_end - page);

    if (page_size > _KOS_SLOTS_OFFS + (_KOS_PAGE_SIZE >> 3))
        page += page_size;
    else {

        pool_hdr->usable_size -= page_size;

        _register_wasted_region(heap, page, page_size);

        page_size = _KOS_PAGE_SIZE;
    }

    while (page > begin) {

        page -= page_size;
        assert( ! ((uintptr_t)page & (uintptr_t)(_KOS_PAGE_SIZE - 1)));

        ((_KOS_PAGE *)page)->num_slots = (page_size - _KOS_SLOTS_OFFS) >> _KOS_OBJ_ALIGN_BITS;

        _list_push((KOS_ATOMIC(void *) *)&heap->free_pages, (void *)page);

        KOS_PERF_CNT(alloc_new_page);

        page_size = _KOS_PAGE_SIZE;
    }

    return KOS_SUCCESS;
}

static _KOS_PAGE *_alloc_page(struct _KOS_HEAP *heap)
{
    _KOS_PAGE *page;

    for (;;) {
        page = (_KOS_PAGE *)_list_pop((KOS_ATOMIC(void *) *)&heap->free_pages);

        if (page)
            break;

        if (_alloc_page_pool(heap))
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
    _KOS_SLOT *const slots_end   = slots_begin + page->num_slots;
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
    _KOS_POOL      *pool = _alloc_pool(_get_heap(frame), size + _KOS_PAGE_SIZE);

    /* TODO try to coalesce free pages */

    if ( ! pool) {
        KOS_raise_exception(frame, _get_heap(frame)->str_oom_id);
        return 0;
    }

    hdr = (KOS_OBJ_HEADER *)(pool->usable_ptr);
    assert( ! ((uintptr_t)hdr & 7U));

    hdr->alloc_size = TO_SMALL_INT(size);
    hdr->type       = (uint8_t)object_type;

    KOS_PERF_CNT(alloc_huge_object);

    return (void *)hdr;
}

static void *_alloc_object(KOS_FRAME            frame,
                           enum KOS_ALLOC_HINT  alloc_hint,
                           enum KOS_OBJECT_TYPE object_type,
                           uint32_t             size)
{
    struct _KOS_HEAP *heap     = _get_heap(frame);
    const int         smallest = size <= sizeof(KOS_INTEGER);

    for (;;) {
        _KOS_PAGE *page  = (_KOS_PAGE *)KOS_atomic_read_ptr(heap->active_pages);
        int        first = 1;

        while (page) {
            _KOS_PAGE            *next;
            KOS_OBJ_HEADER *const hdr = (KOS_OBJ_HEADER *)
                    _alloc_bytes_from_page(page, size);

            if (hdr) {

                /* TODO sort active pages by remaining size in descending order */

                hdr->alloc_size = TO_SMALL_INT(size);
                hdr->type       = (uint8_t)object_type;

                KOS_PERF_CNT(alloc_object);

                return hdr;
            }

            next = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next);

            /* Upon failure to allocate smallest object from first page, move
             * that page to the list of used pages. */
            if (smallest && first) {
                if (KOS_atomic_cas_ptr(heap->active_pages, page, next)) {
                    _list_push((KOS_ATOMIC(void *) *)&heap->full_pages, page);
                    page = next;
                    continue;
                }
            }

            page  = next;
            first = 0;
        }

        page = _alloc_page(heap);

        if (page)
            _list_push((KOS_ATOMIC(void *) *)&heap->active_pages, (void *)page);
        else
            break;
    }

    KOS_raise_exception(frame, _get_heap(frame)->str_oom_id);
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

static void _stop_the_world(KOS_FRAME frame)
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

        KOS_atomic_write_u32(page->num_slots_used, 0);

        page = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next);
    }
}

static void _clear_marking(struct _KOS_HEAP *heap)
{
    _set_marking_in_pages((_KOS_PAGE *)KOS_atomic_read_ptr(heap->active_pages), WHITE);
    _set_marking_in_pages((_KOS_PAGE *)KOS_atomic_read_ptr(heap->full_pages),   WHITE);
    _set_marking_in_pages((_KOS_PAGE *)KOS_atomic_read_ptr(heap->free_pages),   GRAY);
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
    mark_loc.mask_idx = (slot_idx & ~0xFU) * 2;

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
    if (IS_HEAP_OBJECT(obj_id)) {

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
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(OBJECT, obj_id)->props), GRAY);
            _set_mark_state(OBJPTR(OBJECT, obj_id)->prototype, GRAY);
            break;

        case OBJ_ARRAY:
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(ARRAY, obj_id)->data), GRAY);
            break;

        case OBJ_BUFFER:
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(BUFFER, obj_id)->data), GRAY);
            break;

        case OBJ_FUNCTION:
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(FUNCTION, obj_id)->prototype), GRAY);
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->closures, GRAY);
            _set_mark_state(OBJPTR(FUNCTION, obj_id)->defaults, GRAY);
            {
                KOS_FRAME frame = OBJPTR(FUNCTION, obj_id)->generator_stack_frame;
                if (frame)
                    _set_mark_state(OBJID(STACK_FRAME, frame), GRAY);
            }
            break;

        case OBJ_OBJECT_STORAGE:
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(OBJECT_STORAGE, obj_id)->new_prop_table), GRAY);
            {
                KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id)->items[0];
                KOS_PITEM *end  = item + OBJPTR(OBJECT_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item) {
                    _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(item->key), GRAY);
                    _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(item->value), GRAY);
                }
            }
            break;

        case OBJ_ARRAY_STORAGE:
            _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(ARRAY_STORAGE, obj_id)->next), GRAY);
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &OBJPTR(ARRAY_STORAGE, obj_id)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + OBJPTR(ARRAY_STORAGE, obj_id)->capacity;
                for ( ; item < end; ++item)
                    _set_mark_state((KOS_OBJ_ID)KOS_atomic_read_ptr(*item), GRAY);
            }
            break;

        case OBJ_DYNAMIC_PROP:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->getter, GRAY);
            _set_mark_state(OBJPTR(DYNAMIC_PROP, obj_id)->setter, GRAY);
            break;

        case OBJ_OBJECT_WALK:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->obj, GRAY);
            _set_mark_state(OBJPTR(OBJECT_WALK, obj_id)->key_table, GRAY);
            break;

        case OBJ_MODULE:
            /* TODO lock gc during module setup */
            _set_mark_state(OBJPTR(MODULE, obj_id)->name,         GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->path,         GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->strings,      GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->global_names, GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->globals,      GRAY);
            _set_mark_state(OBJPTR(MODULE, obj_id)->module_names, GRAY);
            break;

        case OBJ_STACK_FRAME:
            /* TODO make these atomic */
            _set_mark_state(OBJPTR(STACK_FRAME, obj_id)->parent, GRAY);
            _set_mark_state(OBJPTR(STACK_FRAME, obj_id)->module, GRAY);
            _set_mark_state(OBJPTR(STACK_FRAME, obj_id)->registers, GRAY);
            _set_mark_state(OBJPTR(STACK_FRAME, obj_id)->exception, GRAY);
            _set_mark_state(OBJPTR(STACK_FRAME, obj_id)->retval, GRAY);
            {
                KOS_OBJ_ID *item = &OBJPTR(STACK_FRAME, obj_id)->saved_frames[0];
                KOS_OBJ_ID *end  = item + KOS_MAX_SAVED_FRAMES;
                for ( ; item < end; ++item)
                    _set_mark_state(*item, GRAY);
            }
            break;
    }
}

static int _mark_object_black(KOS_OBJ_ID obj_id)
{
    int marked = 0;

    if ( ! IS_HEAP_OBJECT(obj_id))
        return 0;

    assert( ! IS_BAD_PTR(obj_id));

    marked = _set_mark_state(obj_id, BLACK);

    _mark_children_gray(obj_id);

    return marked;
}

static int _gray_to_black_in_pages(_KOS_PAGE *page)
{
    int marked = 0;

    for ( ; page; page = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next)) {

        uint32_t num_slots_used = 0;

        struct _KOS_MARK_LOC mark_loc = { 0, 0 };

        _KOS_SLOT *ptr      = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS);
        _KOS_SLOT *page_end = ptr + page->num_slots;
        _KOS_SLOT *end      = (_KOS_SLOT *)KOS_atomic_read_ptr(page->first_free_slot);

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + _KOS_BITMAP_OFFS);

        /* Lock page to prevent accidental allocation from it during processing */
        while ( ! KOS_atomic_cas_ptr(page->first_free_slot, end, page_end))
            end = (_KOS_SLOT *)KOS_atomic_read_ptr(page->first_free_slot);

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

        assert(num_slots_used >= KOS_atomic_read_u32(page->num_slots_used));

        KOS_atomic_write_u32(page->num_slots_used, num_slots_used);

        /* Unlock page */
        KOS_atomic_write_ptr(page->first_free_slot, end);
    }

    return marked;
}

static int _gray_to_black(struct _KOS_HEAP *heap)
{
    int marked = 0;

    marked += _gray_to_black_in_pages((_KOS_PAGE *)KOS_atomic_read_ptr(heap->active_pages));
    marked += _gray_to_black_in_pages((_KOS_PAGE *)KOS_atomic_read_ptr(heap->full_pages));

    return marked;
}

static void _mark_roots(KOS_FRAME frame)
{
    KOS_CONTEXT *ctx = KOS_context_from_frame(frame);

    _mark_object_black(ctx->empty_string);

    _mark_object_black(ctx->prototypes.object_proto);
    _mark_object_black(ctx->prototypes.number_proto);
    _mark_object_black(ctx->prototypes.integer_proto);
    _mark_object_black(ctx->prototypes.float_proto);
    _mark_object_black(ctx->prototypes.string_proto);
    _mark_object_black(ctx->prototypes.boolean_proto);
    _mark_object_black(ctx->prototypes.void_proto);
    _mark_object_black(ctx->prototypes.array_proto);
    _mark_object_black(ctx->prototypes.buffer_proto);
    _mark_object_black(ctx->prototypes.function_proto);
    _mark_object_black(ctx->prototypes.constructor_proto);
    _mark_object_black(ctx->prototypes.generator_proto);
    _mark_object_black(ctx->prototypes.exception_proto);
    _mark_object_black(ctx->prototypes.generator_end_proto);
    _mark_object_black(ctx->prototypes.thread_proto);

    _mark_object_black(ctx->module_search_paths);
    _mark_object_black(ctx->module_names);
    _mark_object_black(ctx->modules);

    _mark_object_black(ctx->args);

    /* TODO go over all threads */

    _mark_object_black(OBJID(STACK_FRAME, frame));
}

static void _reclaim_free_pages(struct _KOS_HEAP *heap,
                                _KOS_PAGE        *free_pages)
{
    /* TODO */
}

static int _evacuate_object(KOS_FRAME       frame,
                            KOS_OBJ_HEADER *hdr,
                            uint32_t        size)
{
    int             error   = KOS_SUCCESS;
    KOS_OBJ_HEADER *new_obj = (KOS_OBJ_HEADER *)_KOS_alloc_object(frame,
                                                                  KOS_ALLOC_DEFAULT,
                                                                  (enum KOS_OBJECT_TYPE)hdr->type,
                                                                  size);

    if (new_obj) {
        memcpy(new_obj, hdr, size);

        hdr->alloc_size = (KOS_OBJ_ID)((intptr_t)hdr + 1);
    }
    else
        error = KOS_ERROR_EXCEPTION;

    return KOS_SUCCESS;
}

static void _update_child_ptr(KOS_OBJ_ID *obj_id_ptr)
{
    KOS_OBJ_ID obj_id = *obj_id_ptr;

    if (IS_HEAP_OBJECT(obj_id)) {

        /* TODO use relaxed atomic loads/stores ??? */

        KOS_OBJ_ID new_obj = ((KOS_OBJ_HEADER *)((intptr_t)obj_id - 1))->alloc_size;

        if (IS_HEAP_OBJECT(new_obj))
            *obj_id_ptr = new_obj;
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
            assert(IS_BAD_PTR((KOS_OBJ_ID)KOS_atomic_read_ptr(((KOS_OBJECT_STORAGE *)hdr)->new_prop_table)));
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
            assert(IS_BAD_PTR((KOS_OBJ_ID)KOS_atomic_read_ptr(((KOS_ARRAY_STORAGE *)hdr)->next)));
            {
                KOS_ATOMIC(KOS_OBJ_ID) *item = &((KOS_ARRAY_STORAGE *)hdr)->buf[0];
                KOS_ATOMIC(KOS_OBJ_ID) *end  = item + ((KOS_ARRAY_STORAGE *)hdr)->capacity;
                for ( ; item < end; ++item)
                    _update_child_ptr((KOS_OBJ_ID *)item);
            }
            break;

        case OBJ_FUNCTION:
            _update_child_ptr((KOS_OBJ_ID *)&((KOS_FUNCTION *)hdr)->prototype);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->closures);
            _update_child_ptr(&((KOS_FUNCTION *)hdr)->defaults);
            {
                KOS_FRAME frame = ((KOS_FUNCTION *)hdr)->generator_stack_frame;
                if (frame) {
                    KOS_OBJ_ID obj_id = OBJID(STACK_FRAME, frame);
                    _update_child_ptr(&obj_id);
                    ((KOS_FUNCTION *)hdr)->generator_stack_frame = OBJPTR(STACK_FRAME, obj_id);
                }
            }
            break;

        case OBJ_DYNAMIC_PROP:
            _update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->getter);
            _update_child_ptr(&((KOS_DYNAMIC_PROP *)hdr)->setter);
            break;

        case OBJ_OBJECT_WALK:
            _update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->obj);
            _update_child_ptr(&((KOS_OBJECT_WALK *)hdr)->key_table);
            break;

        case OBJ_MODULE:
            _update_child_ptr(&((KOS_MODULE *)hdr)->name);
            _update_child_ptr(&((KOS_MODULE *)hdr)->path);
            _update_child_ptr(&((KOS_MODULE *)hdr)->strings);
            _update_child_ptr(&((KOS_MODULE *)hdr)->global_names);
            _update_child_ptr(&((KOS_MODULE *)hdr)->globals);
            _update_child_ptr(&((KOS_MODULE *)hdr)->module_names);
            break;

        case OBJ_STACK_FRAME:
            _update_child_ptr(&((KOS_STACK_FRAME *)hdr)->parent);
            _update_child_ptr(&((KOS_STACK_FRAME *)hdr)->module);
            _update_child_ptr(&((KOS_STACK_FRAME *)hdr)->registers);
            _update_child_ptr(&((KOS_STACK_FRAME *)hdr)->exception);
            _update_child_ptr(&((KOS_STACK_FRAME *)hdr)->retval);
            {
                KOS_OBJ_ID *item = &((KOS_STACK_FRAME *)hdr)->saved_frames[0];
                KOS_OBJ_ID *end  = item + KOS_MAX_SAVED_FRAMES;
                for ( ; item < end; ++item)
                    _update_child_ptr(item);
            }
            break;
    }
}

static void _update_after_evacuation(struct _KOS_HEAP *heap)
{
    /* Note: no need to update object ids in context, because all of them should
     * be allocated on a persistent page. */

    _KOS_PAGE *page = (_KOS_PAGE *)KOS_atomic_read_ptr(heap->full_pages);

    /* TODO add way to go over all pages */
    for ( ; page; page = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next)) {

        uint8_t       *ptr = (uint8_t *)page + _KOS_SLOTS_OFFS;
        uint8_t *const end = (uint8_t *)KOS_atomic_read_ptr(page->first_free_slot);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size = (uint32_t)GET_SMALL_INT(hdr->alloc_size);

            _update_child_ptrs(hdr);

            ptr += size;
        }
    }
}

static int _evacuate(KOS_FRAME   frame,
                     _KOS_PAGE **free_pages)
{
    struct _KOS_HEAP *heap = _get_heap(frame);

    int        error = KOS_SUCCESS;
    _KOS_PAGE *page  = (_KOS_PAGE *)KOS_atomic_swap_ptr(heap->full_pages, (_KOS_PAGE *)0);
    _KOS_PAGE *next;

    for ( ; page; page = next) {

        struct _KOS_MARK_LOC mark_loc = { 0, 0 };

        _KOS_SLOT *ptr = (_KOS_SLOT *)((uint8_t *)page + _KOS_SLOTS_OFFS);
        _KOS_SLOT *end = (_KOS_SLOT *)KOS_atomic_read_ptr(page->first_free_slot);

        next = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next);

        /* If the number of slots used reaches the threshold, then the page is
         * exempt from evacuation. */
        if (page->num_slots_used >= (_KOS_SLOTS_PER_PAGE * _KOS_MIGRATION_THRESH) / 100U) {
            _list_push((KOS_ATOMIC(void *) *)&heap->full_pages, page);
            continue;
        }

        mark_loc.bitmap = (KOS_ATOMIC(uint32_t) *)((uint8_t *)page + _KOS_BITMAP_OFFS);

        while (ptr < end) {

            KOS_OBJ_HEADER *hdr   = (KOS_OBJ_HEADER *)ptr;
            const uint32_t  size  = (uint32_t)GET_SMALL_INT(hdr->alloc_size);
            const uint32_t  color = _get_marking(&mark_loc);

            assert(color != GRAY);

            if (color) {
                if (_evacuate_object(frame, hdr, size)) {

                    KOS_clear_exception(frame);

                    _update_after_evacuation(heap);

                    _reclaim_free_pages(heap, *free_pages);

                    error = _evacuate_object(frame, hdr, size);

                    if (error) {
                        uint8_t        *begin     = (uint8_t *)page + _KOS_SLOTS_OFFS;
                        const uint32_t  evac_size = (uint32_t)((uint8_t *)ptr - begin);

                        hdr = (KOS_OBJ_HEADER *)begin;
                        hdr->alloc_size = TO_SMALL_INT(evac_size);

                        do {
                            next = (_KOS_PAGE *)KOS_atomic_read_ptr(page->next);
                            _list_push((KOS_ATOMIC(void *) *)&heap->full_pages, (void *)page);
                            page = next;
                        } while (page);

                        goto _error;
                    }
                }
            }
            else if (hdr->type == OBJ_OBJECT) {

                KOS_OBJECT *obj = (KOS_OBJECT *)hdr;

                obj->finalize(frame, KOS_atomic_read_ptr(obj->priv));
            }

            _advance_marking(&mark_loc, size >> _KOS_OBJ_ALIGN_BITS);

            ptr = (_KOS_SLOT *)((uint8_t *)ptr + size);
        }

        _list_push((KOS_ATOMIC(void *) *)free_pages, (void *)page);
    }

_error:
    return error;
}

static int _help_gc(KOS_FRAME frame)
{
    struct _KOS_HEAP *heap = _get_heap(frame);

    while (KOS_atomic_read_u32(heap->gc_state) != GC_INACTIVE)
        /* TODO actually help garbage collector */
        _KOS_yield();

    return KOS_SUCCESS;
}

int KOS_collect_garbage(KOS_FRAME frame)
{
    int               error      = KOS_SUCCESS;
    struct _KOS_HEAP *heap       = _get_heap(frame);
    _KOS_PAGE        *free_pages = 0;

    if ( ! KOS_atomic_cas_u32(heap->gc_state, GC_INACTIVE, GC_INIT))
        return _help_gc(frame);

    _clear_marking(heap);

    _mark_roots(frame);

    while (_gray_to_black(heap));

    _stop_the_world(frame); /* Remaining threads enter _help_gc() */

    error = _evacuate(frame, &free_pages);

    _update_after_evacuation(heap);

    _reclaim_free_pages(heap, free_pages);

    KOS_atomic_release_barrier();

    KOS_atomic_write_u32(heap->gc_state, GC_INACTIVE);

    return error;
}

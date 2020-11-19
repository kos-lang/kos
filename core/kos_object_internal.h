/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_OBJECT_INTERNAL_H_INCLUDED
#define KOS_OBJECT_INTERNAL_H_INCLUDED

#include "../inc/kos_atomic.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_string.h"
#include "kos_config.h"
#include "kos_heap.h"
#include "kos_red_black.h"

/*==========================================================================*/
/* Object size and type                                                     */
/*==========================================================================*/

#define KOS_OBJ_TYPE_FIELD_BITS 8U
#define KOS_OBJ_TYPE_FIELD_MASK ((1U << KOS_OBJ_TYPE_FIELD_BITS) - 1U)
#define KOS_HEAP_OBJECT_MASK ((1 << KOS_OBJ_ALIGN_BITS) - 1)
#define KOS_OBJ_TRACK_BIT 8
#define KOS_TRACKED_OBJECT_MASK (((1 << KOS_OBJ_ALIGN_BITS) - 1) ^ KOS_OBJ_TRACK_BIT)

#ifdef __cplusplus

static inline bool kos_is_heap_object(KOS_OBJ_ID obj_id)
{
    return (reinterpret_cast<intptr_t>(obj_id) & KOS_HEAP_OBJECT_MASK) == 1;
}

static inline bool kos_is_tracked_object(KOS_OBJ_ID obj_id)
{
    return (reinterpret_cast<intptr_t>(obj_id) & KOS_TRACKED_OBJECT_MASK) == 1;
}

template<typename T>
static inline void kos_set_object_size(T& header, uint32_t size)
{
    uintptr_t size_and_type = reinterpret_cast<uintptr_t>(header.size_and_type);

    size_and_type &= static_cast<uintptr_t>(KOS_OBJ_TYPE_FIELD_MASK);

    size_and_type |= static_cast<uintptr_t>(size) << KOS_OBJ_TYPE_FIELD_BITS;

    header.size_and_type = reinterpret_cast<KOS_OBJ_ID>(size_and_type);
}

template<typename T>
static inline void kos_set_object_type(T& header, KOS_TYPE type)
{
    uintptr_t size_and_type = reinterpret_cast<uintptr_t>(header.size_and_type);

    assert((static_cast<uint8_t>(type) & 1U) == 0U);

    size_and_type &= ~static_cast<uintptr_t>(KOS_OBJ_TYPE_FIELD_MASK);

    size_and_type |= static_cast<uintptr_t>(type);

    header.size_and_type = reinterpret_cast<KOS_OBJ_ID>(size_and_type);
}

template<typename T>
static inline void kos_set_object_type_size(T& header, KOS_TYPE type, uint32_t size)
{
    const uintptr_t size_and_type = (static_cast<uintptr_t>(size) << KOS_OBJ_TYPE_FIELD_BITS)
                                  | static_cast<uintptr_t>(type);
    header.size_and_type = reinterpret_cast<KOS_OBJ_ID>(size_and_type);
}

template<typename T>
static inline KOS_TYPE kos_get_object_type(T& header)
{
    return static_cast<KOS_TYPE>(
               static_cast<uint8_t>(
                   reinterpret_cast<uintptr_t>(header.size_and_type)));
}

template<typename T>
static inline uint32_t kos_get_object_size(T& header)
{
    return static_cast<uint32_t>(
               reinterpret_cast<uintptr_t>(
                   header.size_and_type) >> KOS_OBJ_TYPE_FIELD_BITS);
}

#else

#define kos_is_heap_object(obj_id) ( ((intptr_t)(obj_id) & KOS_HEAP_OBJECT_MASK) == 1)

#define kos_is_tracked_object(obj_id) ( ((intptr_t)(obj_id) & KOS_TRACKED_OBJECT_MASK) == 1)

#define kos_set_object_size(header, size) do {                                   \
    (header).size_and_type = (KOS_OBJ_ID)(                                       \
        ((uintptr_t)(header).size_and_type & (uintptr_t)KOS_OBJ_TYPE_FIELD_MASK) \
        | ((uintptr_t)(size) << KOS_OBJ_TYPE_FIELD_BITS));                       \
} while (0)

#define kos_set_object_type(header, type) do {                                    \
    (header).size_and_type = (KOS_OBJ_ID)(                                        \
        ((uintptr_t)(header).size_and_type & ~(uintptr_t)KOS_OBJ_TYPE_FIELD_MASK) \
        | ((uintptr_t)(type)));                                                   \
} while (0)

#define kos_set_object_type_size(header, type, size) do {                          \
    (header).size_and_type = (KOS_OBJ_ID)(                                         \
            ((uintptr_t)(size) << KOS_OBJ_TYPE_FIELD_BITS) | ((uintptr_t)(type))); \
} while (0)

#define kos_get_object_type(header) ((KOS_TYPE)(uint8_t)(uintptr_t)(header).size_and_type)

#define kos_get_object_size(header) ((uint32_t)((uintptr_t)(header).size_and_type >> KOS_OBJ_TYPE_FIELD_BITS))

#endif

#ifdef NDEBUG
#define GET_OBJ_TYPE_GC_SAFE(obj) OBJ_OPAQUE
#else
#define GET_OBJ_TYPE_GC_SAFE(obj) kos_get_object_type_gc_safe(obj)
KOS_TYPE kos_get_object_type_gc_safe(KOS_OBJ_ID obj);
#endif

/*==========================================================================*/
/* KOS_CONTEXT                                                              */
/*==========================================================================*/

#ifdef NDEBUG
#define kos_validate_context(ctx) ((void)0)
#else
void kos_validate_context(KOS_CONTEXT ctx);
#endif

/*==========================================================================*/
/* KOS_OBJECT                                                               */
/*==========================================================================*/

union KOS_HASH_ALIGN_U {
    KOS_ATOMIC(uint32_t) hash;
    KOS_OBJ_ID           align;
};

typedef struct KOS_PROPERTY_ITEM_S {
    KOS_ATOMIC(KOS_OBJ_ID) key;
    union KOS_HASH_ALIGN_U hash;
    KOS_ATOMIC(KOS_OBJ_ID) value;
} KOS_PITEM;

typedef struct KOS_OBJECT_STORAGE_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   capacity;
    KOS_ATOMIC(uint32_t)   num_slots_used;
    KOS_ATOMIC(uint32_t)   num_slots_open;
    KOS_ATOMIC(uint32_t)   active_copies;
    KOS_ATOMIC(KOS_OBJ_ID) new_prop_table;
    KOS_PITEM              items[1];
} KOS_OBJECT_STORAGE;

#define KOS_MIN_PROPS_CAPACITY 4U
#define KOS_MAX_PROP_REPROBES  8U
#define KOS_SPEED_GROW_BELOW   64U

void kos_init_object(KOS_OBJECT *obj, KOS_OBJ_ID prototype);

int kos_object_copy_prop_table(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id);

int kos_is_truthy(KOS_OBJ_ID obj_id);

KOS_OBJ_ID kos_new_object_walk(KOS_CONTEXT      ctx,
                               KOS_OBJ_ID       obj_id,
                               enum KOS_DEPTH_E depth);

int kos_object_walk(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  iterator_id);

/*==========================================================================*/
/* KOS_ARRAY                                                                */
/*==========================================================================*/

typedef struct KOS_ARRAY_STORAGE_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   capacity;
    KOS_ATOMIC(uint32_t)   num_slots_open;
    KOS_ATOMIC(KOS_OBJ_ID) next;
    KOS_ATOMIC(KOS_OBJ_ID) buf[1];
} KOS_ARRAY_STORAGE;

#ifdef __cplusplus

static inline KOS_ATOMIC(KOS_OBJ_ID) *kos_get_array_buffer(KOS_ARRAY *array)
{
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_acquire_obj(array->data);
    assert( ! IS_BAD_PTR(buf_obj));
    return &OBJPTR(ARRAY_STORAGE, buf_obj)->buf[0];
}

static inline KOS_OBJ_ID kos_get_array_storage(KOS_OBJ_ID obj_id)
{
    return KOS_atomic_read_acquire_obj(OBJPTR(ARRAY, obj_id)->data);
}

#else

#define kos_get_array_buffer(array) (&OBJPTR(ARRAY_STORAGE, KOS_atomic_read_acquire_obj((array)->data))->buf[0])

#define kos_get_array_storage(obj_id) (KOS_atomic_read_acquire_obj(OBJPTR(ARRAY, (obj_id))->data))

#endif

int kos_array_copy_storage(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id);

struct KOS_CONST_ARRAY_S {
    struct KOS_CONST_OBJECT_ALIGNMENT_S align;
    struct {
        uintptr_t  size_and_type;
        uint32_t   size;
        uint32_t   flags;
        KOS_OBJ_ID data;
    } object;
};

extern const struct KOS_CONST_ARRAY_S kos_empty_array;

/*==========================================================================*/
/* KOS_BUFFER                                                               */
/*==========================================================================*/

#define KOS_BUFFER_CAPACITY_ALIGN 64U

/*==========================================================================*/
/* KOS_STRING                                                               */
/*==========================================================================*/

typedef struct KOS_STRING_ITER_S {
    const uint8_t   *ptr;
    const uint8_t   *end;
    KOS_STRING_FLAGS elem_size;
} KOS_STRING_ITER;

#ifdef __cplusplus

static inline const void* kos_get_string_buffer(KOS_STRING *str)
{
    return (str->header.flags & KOS_STRING_LOCAL) ? &str->local.data[0] : str->ptr.data_ptr;
}

static inline KOS_STRING_FLAGS kos_get_string_elem_size(KOS_STRING *str)
{
    return (KOS_STRING_FLAGS)(str->header.flags & KOS_STRING_ELEM_MASK);
}

static inline bool kos_is_string_iter_end(KOS_STRING_ITER *iter)
{
    return iter->ptr >= iter->end;
}

static inline void kos_string_iter_advance(KOS_STRING_ITER *iter)
{
    iter->ptr += ((uintptr_t)1 << iter->elem_size);
}

#else

#define kos_get_string_buffer(str) (((str)->header.flags & KOS_STRING_LOCAL) ? \
                (const void *)(&(str)->local.data[0]) : \
                (const void *)((str)->ptr.data_ptr))

#define kos_get_string_elem_size(str) ((KOS_STRING_FLAGS)((str)->header.flags & KOS_STRING_ELEM_MASK))

#define kos_is_string_iter_end(iter) ((iter)->ptr >= (iter)->end)

#define kos_string_iter_advance(iter) do { (iter)->ptr += ((uintptr_t)1 << (iter)->elem_size); } while (0)

#endif

void kos_init_string_iter(KOS_STRING_ITER *iter, KOS_OBJ_ID str_id);

uint32_t kos_string_iter_peek_next_code(KOS_STRING_ITER *iter);

/*==========================================================================*/
/* KOS_STACK                                                                */
/*==========================================================================*/

/*
 * Stack frame layout on the stack, indexed from register r0:
 *     -3     function object
 *     -2     (catch_offs << 8) | catch_reg
 *     -1     current instr offset
 *     0      r0
 *     +N-1   rN-1
 *     +N     N | (ret_reg << 8) | (instr << 16)
 *
 * For constructors, 'this' is also pushed as the last (additional) register,
 * i.e. one more register than the function actually uses.  The number of
 * registers N is thus func->num_regs + 1 for non-native constructors.
 */

/* Number of entries in addition to the number of registers */
#define KOS_STACK_EXTRA 4U

struct KOS_STACK_FRAME_HDR_S {
    KOS_ATOMIC(KOS_OBJ_ID) func_obj;
    KOS_ATOMIC(KOS_OBJ_ID) catch_info;
    KOS_ATOMIC(KOS_OBJ_ID) instr_offs;
    KOS_ATOMIC(KOS_OBJ_ID) regs[1];
};

typedef struct KOS_STACK_FRAME_HDR_S KOS_STACK_FRAME;

int kos_stack_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  func_obj,
                   uint8_t     ret_reg,
                   uint8_t     instr);

void kos_stack_pop(KOS_CONTEXT ctx);

void kos_wrap_exception(KOS_CONTEXT ctx);

/*==========================================================================*/
/* KOS_FUNCTION                                                             */
/*==========================================================================*/

KOS_OBJ_ID kos_copy_function(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id);

/*==========================================================================*/
/* KOS_MODULE                                                               */
/*==========================================================================*/

struct KOS_MODULE_INIT_S {
    KOS_OBJ_HEADER   hdr;
    KOS_SHARED_LIB   lib;
    KOS_BUILTIN_INIT init;
};

KOS_OBJ_ID kos_register_module_init(KOS_CONTEXT      ctx,
                                    KOS_OBJ_ID       module_name_obj,
                                    KOS_SHARED_LIB   lib,
                                    KOS_BUILTIN_INIT init);

/*==========================================================================*/
/* KOS_HEAP                                                                 */
/*==========================================================================*/

/*
 * The heap is comprised of pools containing pages.  Each pool is an individual
 * memory allocation.
 * Page size preferably matches or is a multiple of CPU page size.
 * All pages are aligned on page size.
 *
 * Layout of a page on the heap:
 * +=============================================+
 * | header |  bitmap  |          slots          |
 * +=============================================+
 *          ^          ^
 *          |          +-- KOS_SLOTS_OFFS
 *          +-- KOS_BITMAP_OFFS
 *
 * - Page header structure (also typedef'ed as KOS_PAGE) is described below.
 * - Bitmap is used during garbage collection to determine which objects are
 *   still in use.  Bitmap contains 2 bits per slot, the 2 bits are used for
 *   marking.
 * - Slots are used for object storage.  An object occupies at least one slot,
 *   typically multiple contiguous slots.  Marking bits in the bitmap are used
 *   to store marking color for object's first slot only.
 */
struct KOS_PAGE_HEADER_S {
    KOS_PAGE            *next;
    KOS_ATOMIC(uint32_t) num_allocated;   /* Number of slots allocated */
    KOS_ATOMIC(uint32_t) flags;           /* Flags for GC              */

    /* TODO
     * - Distinguish between old pages and new page.  New objects should never
     *   be allocated in old pages, objects can only be moved from new pages
     *   to old pages.  Old pages can just be put on full_pages list.  is_page_full()
     *   will need to be adjusted to detect an old page and treat it as full,
     *   except for when allocating space for evacuated objects during GC.
     */
};

#define KOS_PAGE_HDR_SIZE  (sizeof(KOS_PAGE))
#define KOS_SLOTS_PER_PAGE (((KOS_PAGE_SIZE - KOS_PAGE_HDR_SIZE) << 2) / \
                            ((1U << (KOS_OBJ_ALIGN_BITS + 2)) + 1U))
#define KOS_BITMAP_SIZE    (((KOS_SLOTS_PER_PAGE + 15U) & ~15U) >> 2)
#define KOS_BITMAP_OFFS    ((KOS_PAGE_HDR_SIZE + 3U) & ~3U)
#define KOS_SLOTS_OFFS     (KOS_PAGE_SIZE - (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS))

#ifdef KOS_CPP11
static_assert(KOS_BITMAP_OFFS >= KOS_PAGE_HDR_SIZE, "Unexpected bitmap offset");
static_assert(KOS_SLOTS_PER_PAGE * 2 <= KOS_BITMAP_SIZE * 8, "Unexpected bitmap size");
static_assert(KOS_SLOTS_OFFS >= KOS_BITMAP_OFFS + KOS_BITMAP_SIZE, "Unexpected slots offset");
static_assert(KOS_SLOTS_OFFS + (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) <= KOS_PAGE_SIZE, "Unexpected slots offset");
static_assert(KOS_PAGE_SIZE >= KOS_BITMAP_OFFS + KOS_BITMAP_SIZE + (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS), "Unexpected number of slots");
static_assert(KOS_PAGE_SIZE - KOS_BITMAP_OFFS - KOS_BITMAP_SIZE - (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) < (1 << KOS_OBJ_ALIGN_BITS), "Wasted space in pages");
#endif

#endif

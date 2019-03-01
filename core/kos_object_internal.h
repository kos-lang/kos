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

#ifndef KOS_OBJECT_INTERNAL_H_INCLUDED
#define KOS_OBJECT_INTERNAL_H_INCLUDED

#include "../inc/kos_atomic.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_string.h"
#include "kos_heap.h"
#include "kos_red_black.h"

/*==========================================================================*/
/* Object size and type                                                     */
/*==========================================================================*/

#ifdef __cplusplus

template<typename T>
static inline void kos_set_object_size(T& header, uint32_t size)
{
    uintptr_t size_and_type = reinterpret_cast<uintptr_t>(header.size_and_type);

    size_and_type &= static_cast<uintptr_t>(0xFFU);

    size_and_type |= static_cast<uintptr_t>(size) << 8;

    header.size_and_type = reinterpret_cast<KOS_OBJ_ID>(size_and_type);
}

template<typename T>
static inline void kos_set_object_type(T& header, KOS_TYPE type)
{
    uintptr_t size_and_type = reinterpret_cast<uintptr_t>(header.size_and_type);

    assert((static_cast<uint8_t>(type) & 1U) == 0U);

    size_and_type &= ~static_cast<uintptr_t>(0xFFU);

    size_and_type |= static_cast<uintptr_t>(type);

    header.size_and_type = reinterpret_cast<KOS_OBJ_ID>(size_and_type);
}

template<typename T>
static inline void kos_set_object_type_size(T& header, KOS_TYPE type, uint32_t size)
{
    const uintptr_t size_and_type = (static_cast<uintptr_t>(size) << 8)
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
                   header.size_and_type) >> 8);
}

#else

#define kos_set_object_size(header, size) do {                 \
    (header).size_and_type = (KOS_OBJ_ID)(                     \
        ((uintptr_t)(header).size_and_type & (uintptr_t)0xFFU) \
        | ((uintptr_t)(size) << 8));                           \
} while (0)

#define kos_set_object_type(header, type) do {                  \
    (header).size_and_type = (KOS_OBJ_ID)(                      \
        ((uintptr_t)(header).size_and_type & ~(uintptr_t)0xFFU) \
        | ((uintptr_t)(type)));                                 \
} while (0)

#define kos_set_object_type_size(header, type, size) do {    \
    (header).size_and_type = (KOS_OBJ_ID)(                   \
            ((uintptr_t)(size) << 8) | ((uintptr_t)(type))); \
} while (0)

#define kos_get_object_type(header) ((KOS_TYPE)(uint8_t)(uintptr_t)(header).size_and_type)

#define kos_get_object_size(header) ((uint32_t)((uintptr_t)(header).size_and_type) >> 8)

#endif

#ifdef NDEBUG
#define GET_OBJ_TYPE_GC_SAFE(obj) OBJ_OPAQUE
#else
#define GET_OBJ_TYPE_GC_SAFE(obj) kos_get_object_type_gc_safe(obj)
KOS_TYPE kos_get_object_type_gc_safe(KOS_OBJ_ID obj);
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

/*==========================================================================*/
/* KOS_BUFFER                                                               */
/*==========================================================================*/

#define KOS_BUFFER_CAPACITY_ALIGN 64U

/*==========================================================================*/
/* KOS_STRING                                                               */
/*==========================================================================*/

#ifdef __cplusplus

static inline const void* kos_get_string_buffer(KOS_STRING *str)
{
    return (str->header.flags & KOS_STRING_LOCAL) ? &str->local.data[0] : str->ptr.data_ptr;
}

static inline KOS_STRING_FLAGS kos_get_string_elem_size(KOS_STRING *str)
{
    return (KOS_STRING_FLAGS)(str->header.flags & KOS_STRING_ELEM_MASK);
}

#else

#define kos_get_string_buffer(str) (((str)->header.flags & KOS_STRING_LOCAL) ? \
                (const void *)(&(str)->local.data[0]) : \
                (const void *)((str)->ptr.data_ptr))

#define kos_get_string_elem_size(str) ((KOS_STRING_FLAGS)((str)->header.flags & KOS_STRING_ELEM_MASK))

#endif

int kos_append_cstr(KOS_CONTEXT          ctx,
                    struct KOS_VECTOR_S *cstr_vec,
                    const char          *str,
                    size_t               len);

/*==========================================================================*/
/* KOS_STACK                                                                */
/*==========================================================================*/

#define KOS_STACK_EXTRA 4U

int kos_stack_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  func_obj);

void kos_stack_pop(KOS_CONTEXT ctx);

void kos_wrap_exception(KOS_CONTEXT ctx);

/*==========================================================================*/
/* KOS_MODULE                                                               */
/*==========================================================================*/

struct KOS_MODULE_INIT_S {
    KOS_OBJ_HEADER   hdr;
    KOS_BUILTIN_INIT init;
};

KOS_OBJ_ID kos_module_import(KOS_CONTEXT ctx,
                             const char *module_name, /* Module name or path, ASCII or UTF-8    */
                             unsigned    name_size,   /* Length of module name or path in bytes */
                             int         is_path,     /* Module name can be a path              */
                             const char *data,        /* Module data or 0 if load from file     */
                             unsigned    data_size,   /* Data length if data is not 0           */
                             int        *out_module_idx);

/*==========================================================================*/
/* KOS_HEAP                                                                 */
/*==========================================================================*/

/*
 * The heap is comprised of pools containing pages.  Each pool is an individual
 * memory allocation.
 * Page size preferably matches CPU page size.
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
    uint32_t             num_slots;       /* Total number of slots in this page */
    KOS_ATOMIC(uint32_t) num_allocated;   /* Number of slots allocated          */
    KOS_ATOMIC(uint32_t) num_used;        /* Number of slots used, only for GC  */

    /* TODO
     * - Distinguish between number of slots that the page has and the number of
     *   slots usable for allocating small objects.  This will enable allocating
     *   a big object far in the page and using the rest of the page for small
     *   objects.
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

#define KOS_LOOK_FURTHER 255

#ifdef CONFIG_MAD_GC
#define TRACK_ONE_REF 0x1234
#else
#define TRACK_ONE_REF 1
#endif

typedef struct KOS_LOCAL_REFS_S {
    KOS_OBJ_HEADER header;
    KOS_OBJ_ID     next;
    uint8_t        num_tracked;
    uint8_t        prev_scope;
    KOS_OBJ_ID    *refs[64 - 3];
} KOS_LOCAL_REFS;

void kos_track_refs(KOS_CONTEXT ctx, int num_entries, ...);

void kos_untrack_refs(KOS_CONTEXT ctx, int num_entries);

#endif

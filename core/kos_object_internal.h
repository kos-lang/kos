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
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_obj(array->data);
    assert( ! IS_BAD_PTR(buf_obj));
    return &OBJPTR(ARRAY_STORAGE, buf_obj)->buf[0];
}

static inline KOS_OBJ_ID kos_get_array_storage(KOS_OBJ_ID obj_id)
{
    return KOS_atomic_read_obj(OBJPTR(ARRAY, obj_id)->data);
}

#else

#define kos_get_array_buffer(array) (&OBJPTR(ARRAY_STORAGE, KOS_atomic_read_obj((array)->data))->buf[0])

#define kos_get_array_storage(obj_id) (KOS_atomic_read_obj(OBJPTR(ARRAY, (obj_id))->data))

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
                             const char *data,        /* Module data or 0 if load from file     */
                             unsigned    data_size,   /* Data length if data is not 0           */
                             int        *out_module_idx);

/*==========================================================================*/
/* KOS_HEAP                                                                 */
/*==========================================================================*/

#define KOS_LOOK_FURTHER 255

typedef struct KOS_LOCAL_REFS_HEADER_S {
    KOS_OBJ_ID alloc_size;
    uint8_t    type;
    uint8_t    num_tracked;
    uint8_t    prev_scope;
} KOS_LOCAL_REFS_HEADER;

typedef struct KOS_LOCAL_REFS_S {
    KOS_LOCAL_REFS_HEADER header;
    KOS_OBJ_ID            next;
    KOS_OBJ_ID           *refs[64 - 3];
} KOS_LOCAL_REFS;

#ifndef NDEBUG
int kos_heap_lend_page(KOS_CONTEXT ctx,
                       void       *buffer,
                       size_t      size);
#endif

void kos_lock_gc(KOS_INSTANCE *inst);

void kos_unlock_gc(KOS_INSTANCE *inst);

void kos_track_refs(KOS_CONTEXT ctx, int num_entries, ...);

void kos_untrack_refs(KOS_CONTEXT ctx, int num_entries);

#endif

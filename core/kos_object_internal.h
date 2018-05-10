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

#ifndef __KOS_OBJECT_INTERNAL_H
#define __KOS_OBJECT_INTERNAL_H

#include "../inc/kos_context.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "kos_heap.h"
#include "kos_red_black.h"

/*==========================================================================*/
/* KOS_OBJECT                                                               */
/*==========================================================================*/

union _KOS_HASH_ALIGN {
    KOS_ATOMIC(uint32_t) hash;
    KOS_OBJ_ID           align;
};

typedef struct _KOS_PROPERTY_ITEM {
    KOS_ATOMIC(KOS_OBJ_ID) key;
    union _KOS_HASH_ALIGN  hash;
    KOS_ATOMIC(KOS_OBJ_ID) value;
} KOS_PITEM;

typedef struct _KOS_OBJECT_STORAGE {
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

void _KOS_init_object(KOS_OBJECT *obj, KOS_OBJ_ID prototype);

int _KOS_object_copy_prop_table(KOS_FRAME  frame,
                                KOS_OBJ_ID obj_id);

int _KOS_is_truthy(KOS_OBJ_ID obj_id);

/*==========================================================================*/
/* KOS_ARRAY                                                                */
/*==========================================================================*/

typedef struct _KOS_ARRAY_STORAGE {
    KOS_OBJ_HEADER         header;
    uint32_t               capacity;
    KOS_ATOMIC(uint32_t)   num_slots_open;
    KOS_ATOMIC(KOS_OBJ_ID) next;
    KOS_ATOMIC(KOS_OBJ_ID) buf[1];
} KOS_ARRAY_STORAGE;

#ifdef __cplusplus

static inline KOS_ATOMIC(KOS_OBJ_ID) *_KOS_get_array_buffer(KOS_ARRAY *array)
{
    const KOS_OBJ_ID buf_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(array->data);
    assert( ! IS_BAD_PTR(buf_obj));
    return &OBJPTR(ARRAY_STORAGE, buf_obj)->buf[0];
}

#else

#define _KOS_get_array_buffer(array) (&OBJPTR(ARRAY_STORAGE, (KOS_OBJ_ID)KOS_atomic_read_ptr((array)->data))->buf[0])

#endif

int _KOS_array_copy_storage(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id);

/*==========================================================================*/
/* KOS_BUFFER                                                               */
/*==========================================================================*/

#define KOS_BUFFER_CAPACITY_ALIGN 64U

/*==========================================================================*/
/* KOS_STRING                                                               */
/*==========================================================================*/

#ifdef __cplusplus

static inline const void* _KOS_get_string_buffer(KOS_STRING *str)
{
    return (str->header.flags & KOS_STRING_LOCAL) ? &str->local.data[0] : str->ptr.data_ptr;
}

static inline enum _KOS_STRING_FLAGS _KOS_get_string_elem_size(KOS_STRING *str)
{
    return (enum _KOS_STRING_FLAGS)(str->header.flags & KOS_STRING_ELEM_MASK);
}

#else

#define _KOS_get_string_buffer(str) (((str)->header.flags & KOS_STRING_LOCAL) ? \
                (const void *)(&(str)->local.data[0]) : \
                (const void *)((str)->ptr.data_ptr))

#define _KOS_get_string_elem_size(str) ((enum _KOS_STRING_FLAGS)((str)->header.flags & KOS_STRING_ELEM_MASK))

#endif

/*==========================================================================*/
/* KOS_FRAME                                                                */
/*==========================================================================*/

int _KOS_init_stack_frame(KOS_FRAME           frame,
                          KOS_THREAD_CONTEXT *thread_ctx,
                          KOS_MODULE         *module,
                          uint32_t            instr_offs);

KOS_FRAME _KOS_stack_frame_push(KOS_FRAME   frame,
                                KOS_MODULE *module,
                                uint32_t    instr_offs,
                                KOS_OBJ_ID  regs);

#define _KOS_stack_frame_push_func(frame, func, regs)         \
        _KOS_stack_frame_push((frame),                        \
                              OBJPTR(MODULE, (func)->module), \
                              (func)->instr_offs,             \
                              (regs))

void _KOS_wrap_exception(KOS_FRAME frame);

/*==========================================================================*/
/* KOS_MODULE                                                               */
/*==========================================================================*/

struct _KOS_MODULE_INIT {
    struct _KOS_RED_BLACK_NODE rb_tree_node;
    KOS_OBJ_ID                 name;
    KOS_BUILTIN_INIT           init;
};

KOS_OBJ_ID _KOS_module_import(KOS_FRAME   frame,
                              const char *name,      /* Module name or path, ASCII or UTF-8    */
                              unsigned    name_size, /* Length of module name or path in bytes */
                              const char *data,      /* Module data or 0 if load from file     */
                              unsigned    data_size, /* Data length if data is not 0           */
                              int        *module_idx);

#endif

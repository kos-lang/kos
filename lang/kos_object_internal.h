/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#include "kos_threads.h"
#include "../inc/kos_context.h"
#include "../inc/kos_string.h"
#include "kos_red_black.h"

/*==========================================================================*/
/* KOS_OBJECT                                                               */
/*==========================================================================*/

union _KOS_HASH_ALIGN {
    KOS_ATOMIC(uint32_t) hash;
    KOS_OBJ_PTR          align;
};

typedef struct _KOS_PROPERTY_ITEM {
    KOS_ATOMIC(KOS_OBJ_PTR) key;
    union _KOS_HASH_ALIGN   hash;
    KOS_ATOMIC(KOS_OBJ_PTR) value;
} KOS_PITEM;

struct _KOS_PROPERTY_BUF;
typedef struct _KOS_PROPERTY_BUF *KOS_PBUF_PTR;

struct _KOS_PROPERTY_BUF {
    /* Keeping capacity here is suboptimal and hurts performance, because
       the capacity will often be held in another cache line.  Find a better
       way some day to keep the capacity in the object. */
    uint32_t                 capacity;
    KOS_ATOMIC(uint32_t)     num_slots_used;
    KOS_ATOMIC(uint32_t)     active_copies;
    KOS_ATOMIC(uint32_t)     all_salvaged;
    KOS_ATOMIC(KOS_PBUF_PTR) new_prop_table;
    KOS_PITEM                items[1];
};

#define KOS_MIN_PROPS_CAPACITY 4U
#define KOS_MAX_PROP_REPROBES  8U
#define KOS_SPEED_GROW_BELOW   64U

#ifdef __cplusplus
static inline void _KOS_init_properties(struct _KOS_PROPERTIES *properties)
{
    KOS_atomic_write_ptr(properties->props, (void *)0);
}
#else
#define _KOS_init_properties(properties) KOS_atomic_write_ptr((properties)->props, 0)
#endif

void _KOS_init_object(KOS_OBJECT *obj, KOS_OBJ_PTR prototype);

#ifdef CONFIG_OBJECT_STATS
struct _KOS_OBJECT_STATS {
    uint32_t num_successful_resizes;
    uint32_t num_failed_resizes;
    uint32_t num_successful_writes;
    uint32_t num_failed_writes;
    uint32_t num_successful_reads;
    uint32_t num_failed_reads;
};

struct _KOS_OBJECT_STATS _KOS_get_object_stats();
#endif

int _KOS_object_copy_prop_table(KOS_CONTEXT *ctx,
                                KOS_OBJ_PTR  obj);

int _KOS_is_truthy(KOS_OBJ_PTR obj);

/*==========================================================================*/
/* KOS_ARRAY                                                                */
/*==========================================================================*/

int _KOS_init_array(KOS_CONTEXT *ctx, KOS_ARRAY *array, unsigned capacity);

#define KOS_MIN_ARRAY_CAPACITY  4U
#define KOS_ARRAY_CAPACITY_STEP 4096U

struct _KOS_ARRAY_BUFFER {
    KOS_ATOMIC(KOS_OBJ_PTR) buf[1];
};

#ifdef __cplusplus

static inline KOS_ATOMIC(KOS_OBJ_PTR) *_KOS_get_array_buffer(KOS_ARRAY *array)
{
    struct _KOS_ARRAY_BUFFER *const buf = (struct _KOS_ARRAY_BUFFER *)KOS_atomic_read_ptr(array->buffer);
    return &buf->buf[0];
}

#else

#define _KOS_get_array_buffer(array) (&((struct _KOS_ARRAY_BUFFER *)KOS_atomic_read_ptr((array)->buffer))->buf[0])

#endif

/*==========================================================================*/
/* KOS_BUFFER                                                               */
/*==========================================================================*/

#define KOS_BUFFER_CAPACITY_ALIGN 64U

struct _KOS_BUFFER_DATA {
    uint32_t capacity;
    uint32_t _align;
    uint8_t  buf[1];
};

/*==========================================================================*/
/* KOS_STRING                                                               */
/*==========================================================================*/

#ifdef __cplusplus

static inline const void* _KOS_get_string_buffer(KOS_STRING *str)
{
    assert(str->type >= OBJ_STRING_8 && str->type <= OBJ_STRING_32);
    return (str->flags == KOS_STRING_LOCAL) ? &str->data.buf : str->data.ptr;
}

#else

#define _KOS_get_string_buffer(str) ((const void *)((str)->flags == KOS_STRING_LOCAL ? &(str)->data.buf : (str)->data.ptr))

#endif

/*==========================================================================*/
/* KOS_STACK_FRAME                                                          */
/*==========================================================================*/

void _KOS_stack_frame_init(KOS_STACK_FRAME *frame);

KOS_STACK_FRAME *_KOS_stack_frame_push(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  module,
                                       uint32_t     instr_offs,
                                       uint32_t     num_regs);

KOS_STACK_FRAME *_KOS_stack_frame_push_func(KOS_CONTEXT  *ctx,
                                            KOS_FUNCTION *func);

void _KOS_wrap_exception(KOS_CONTEXT     *ctx,
                         KOS_STACK_FRAME *stack_frame);

/*==========================================================================*/
/* KOS_MODULE                                                               */
/*==========================================================================*/

struct _KOS_MODULE_INIT {
    struct _KOS_RED_BLACK_NODE rb_tree_node;
    KOS_OBJ_PTR                name;
    KOS_BUILTIN_INIT           init;
};

enum _KOS_MODULE_REQUIRED {
    KOS_MODULE_OPTIONAL,
    KOS_MODULE_MANDATORY
};

KOS_OBJ_PTR _KOS_module_import(KOS_CONTEXT              *ctx,
                               const char               *module, /* Module name or path, ASCII or UTF-8    */
                               unsigned                  length, /* Length of module name or path in bytes */
                               enum _KOS_MODULE_REQUIRED required,
                               int                      *module_idx);

#endif

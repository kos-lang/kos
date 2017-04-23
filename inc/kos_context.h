/*
 * Copyright (c) 2014-2017 Chris Dragan
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

#ifndef __KOS_CONTEXT_H
#define __KOS_CONTEXT_H

#include "kos_object_base.h"
#include "kos_thread_primitives.h"
#include <stddef.h>

struct _KOS_MODULE_LOAD_CHAIN;
struct _KOS_RED_BLACK_NODE;

#if 1

struct _KOS_ALLOCATOR {
    KOS_ATOMIC(void *) objects;
};

#else

struct _KOS_AREA;

struct _KOS_FIXED_AREA;

struct _KOS_FREE_AREA;

struct _KOS_ALLOCATOR {
    KOS_ATOMIC(uint32_t) lock;
    KOS_ATOMIC(void *)  *areas_free;
    KOS_ATOMIC(void *)  *areas_fixed;
    /* TODO add areas_stack */
    KOS_ATOMIC(void *)  *areas[4]; /* By element size: 8, 16, 32, 64 */
};

#endif

enum _KOS_YIELD_STATE {
    KOS_CANNOT_YIELD = 0x1000U, /* indicates a regular function */
    KOS_CAN_YIELD    = 0x2000U  /* indicates a generator        */
};

enum _KOS_CATCH_STATE {
    KOS_NO_CATCH = 0x7FFFFFFFU
};

struct _KOS_STACK_FRAME {
    struct _KOS_ALLOCATOR   *allocator;
    uint8_t                  alloc_mode;
    uint8_t                  catch_reg;
    uint16_t                 yield_reg;    /* index of the yield register */
    uint32_t                 catch_offs;
    struct _KOS_STACK_FRAME *parent;
    struct _KOS_MODULE      *module;
    KOS_OBJ_ID               registers;
    KOS_OBJ_ID               exception;
    KOS_OBJ_ID               retval;
    uint32_t                 instr_offs;
};

struct _KOS_THREAD_ROOT {
    struct _KOS_STACK_FRAME frame;
};

typedef struct _KOS_THREAD_ROOT KOS_THREAD_ROOT;

enum _KOS_CONTEXT_FLAGS {
    KOS_CTX_NO_FLAGS = 0,
    KOS_CTX_VERBOSE  = 1,
    KOS_CTX_DEBUG    = 2
};

struct _KOS_CONTEXT {
    uint32_t                       flags;

    struct _KOS_ALLOCATOR          allocator;

    KOS_OBJ_ID                     empty_string;

    KOS_OBJ_ID                     object_prototype;
    KOS_OBJ_ID                     number_prototype;
    KOS_OBJ_ID                     integer_prototype;
    KOS_OBJ_ID                     float_prototype;
    KOS_OBJ_ID                     string_prototype;
    KOS_OBJ_ID                     boolean_prototype;
    KOS_OBJ_ID                     void_prototype;
    KOS_OBJ_ID                     array_prototype;
    KOS_OBJ_ID                     buffer_prototype;
    KOS_OBJ_ID                     function_prototype;

    KOS_ATOMIC(void *)             prototypes;
    KOS_ATOMIC(uint32_t)           prototypes_lock;

    _KOS_TLS_KEY                   thread_key;

    KOS_OBJ_ID                     module_search_paths;
    KOS_OBJ_ID                     module_names;
    KOS_OBJ_ID                     modules;

    KOS_MODULE                     init_module;
    KOS_THREAD_ROOT                main_thread;

    struct _KOS_RED_BLACK_NODE    *module_inits;
    struct _KOS_MODULE_LOAD_CHAIN *module_load_chain;
};

#ifdef __cplusplus
static inline KOS_CONTEXT *KOS_context_from_frame(KOS_FRAME frame)
{
    assert(frame);
    assert(frame->module);
    assert(frame->module->context);
    return frame->module->context;
}
#else
#define KOS_context_from_frame(frame) ((frame)->module->context)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int KOS_context_init(KOS_CONTEXT *ctx,
                     KOS_FRAME   *frame);

void KOS_context_destroy(KOS_CONTEXT *ctx);

int KOS_context_add_path(KOS_FRAME   frame,
                         const char *module_search_path);

typedef int (*KOS_BUILTIN_INIT)(KOS_FRAME frame);

int KOS_context_register_builtin(KOS_FRAME        frame,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init);

int KOS_context_register_thread(KOS_CONTEXT *ctx, KOS_THREAD_ROOT *thread_root);

KOS_OBJ_ID KOS_context_get_cstring(KOS_FRAME   frame,
                                   const char *cstr);

#ifdef NDEBUG
#define KOS_context_validate(frame) ((void)0)
#else
void KOS_context_validate(KOS_FRAME frame);
#endif

void KOS_raise_exception(KOS_FRAME  frame,
                         KOS_OBJ_ID exception_obj);

void KOS_raise_exception_cstring(KOS_FRAME   frame,
                                 const char *cstr);

void KOS_clear_exception(KOS_FRAME frame);

int KOS_is_exception_pending(KOS_FRAME frame);

KOS_OBJ_ID KOS_get_exception(KOS_FRAME frame);

KOS_OBJ_ID KOS_format_exception(KOS_FRAME  frame,
                                KOS_OBJ_ID exception);

/* TODO find a better place */
KOS_OBJ_ID KOS_get_file_name(KOS_FRAME  frame,
                             KOS_OBJ_ID full_path);

/* TODO find a better place */
int KOS_get_integer(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int64_t   *ret);

KOS_OBJ_ID KOS_gen_prototype(KOS_FRAME   frame,
                             const void *ptr);

KOS_OBJ_ID KOS_call_function(KOS_FRAME  frame,
                             KOS_OBJ_ID func_obj,
                             KOS_OBJ_ID this_obj,
                             KOS_OBJ_ID args_obj);

#ifdef __cplusplus
}
#endif

#endif

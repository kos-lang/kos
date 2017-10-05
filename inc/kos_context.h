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
#include "kos_threads.h"
#include <stddef.h>

struct _KOS_MODULE_LOAD_CHAIN;
struct _KOS_RED_BLACK_NODE;

struct _KOS_ALLOCATOR {
    KOS_ATOMIC(uint32_t) lock;
    KOS_ATOMIC(void *)   areas_free;
    KOS_ATOMIC(void *)   areas_fixed;
    /* TODO add areas_stack */
    KOS_ATOMIC(void *)   areas[4]; /* By element size: 8, 16, 32, 64 */
    KOS_ATOMIC(void *)   buffers;  /* TODO buddy allocator for buffers + freed list */
    KOS_OBJ_ID           str_oom_id;
};

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
    KOS_CTX_DISASM   = 2
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
    KOS_OBJ_ID                     exception_prototype;
    KOS_OBJ_ID                     generator_end_prototype;

    KOS_ATOMIC(void *)             prototypes;
    KOS_ATOMIC(uint32_t)           prototypes_lock;

    _KOS_TLS_KEY                   thread_key;

    KOS_OBJ_ID                     module_search_paths;
    KOS_OBJ_ID                     module_names;
    KOS_OBJ_ID                     modules;

    KOS_OBJ_ID                     args;

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

int KOS_context_set_args(KOS_FRAME    frame,
                         int          argc,
                         const char **argv);

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

void KOS_raise_generator_end(KOS_FRAME frame);

/* TODO move to utils */
KOS_OBJ_ID KOS_get_file_name(KOS_FRAME  frame,
                             KOS_OBJ_ID full_path);

/* TODO move to utils */
int KOS_get_integer(KOS_FRAME  frame,
                    KOS_OBJ_ID obj_id,
                    int64_t   *ret);

KOS_OBJ_ID KOS_gen_prototype(KOS_FRAME   frame,
                             const void *ptr);

enum _KOS_CALL_FLAVOR {
    KOS_CALL_FUNCTION,
    KOS_APPLY_FUNCTION
};

KOS_OBJ_ID _KOS_call_function(KOS_FRAME             frame,
                              KOS_OBJ_ID            func_obj,
                              KOS_OBJ_ID            this_obj,
                              KOS_OBJ_ID            args_obj,
                              enum _KOS_CALL_FLAVOR call_flavor);

#define KOS_call_function(frame, func_obj, this_obj, args_obj) \
    _KOS_call_function((frame), (func_obj), (this_obj), (args_obj), KOS_CALL_FUNCTION)

#define KOS_apply_function(frame, func_obj, this_obj, args_obj) \
    _KOS_call_function((frame), (func_obj), (this_obj), (args_obj), KOS_APPLY_FUNCTION)

#ifdef __cplusplus
}
#endif

#endif

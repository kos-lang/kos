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

#ifndef __KOS_CONTEXT_H
#define __KOS_CONTEXT_H

#include "kos_object_base.h"
#include "kos_threads.h"
#include <stddef.h>

struct _KOS_MODULE_LOAD_CHAIN;
struct _KOS_RED_BLACK_NODE;
struct _KOS_PAGE_HEADER;
struct _KOS_POOL_HEADER;
struct _KOS_WASTE_HEADER;

typedef struct _KOS_PAGE_HEADER  _KOS_PAGE;
typedef struct _KOS_POOL_HEADER  _KOS_POOL;
typedef struct _KOS_WASTE_HEADER _KOS_WASTE;

struct _KOS_HEAP {
    _KOS_MUTEX           mutex;
    KOS_ATOMIC(uint32_t) gc_state;
    uint32_t             heap_size;         /* total amount of memory owned by the heap */
    _KOS_PAGE           *free_pages;        /* pages which are currently unused         */
    _KOS_PAGE           *non_full_pages;    /* pages in which new objects are allocated */
    _KOS_PAGE           *full_pages;        /* pages which have no room for new objects */
    _KOS_POOL           *pools;             /* allocated memory - page pools            */
    _KOS_POOL           *pool_headers;      /* list of pool headers for new pools       */
    _KOS_WASTE          *waste;             /* unused memory from pool allocations      */
    KOS_OBJ_ID           str_oom_id;
};

/* Stored on the stack as catch offset */
enum _KOS_CATCH_STATE {
    KOS_NO_CATCH = 0x1FFFFF
};

/* Stack header flags */
enum _KOS_STACK_HEADER_FLAGS {
    KOS_NORMAL_STACK    = 0U,
    KOS_REENTRANT_STACK = 1U,   /* Stack of a generator or closure      */
    KOS_CAN_YIELD       = 2U    /* Indicates that a generator can yield */
};

typedef struct _KOS_STACK_HEADER {
    KOS_OBJ_ID  alloc_size;
    uint8_t     type;
    uint8_t     flags;
    uint8_t     yield_reg; /* In a generator stack, this is the index of the yield register */
} KOS_STACK_HEADER;

/* Stack management:
 * - If this is not the root stack object, the first element on the stack
 *   is the object id of the previous stack object.
 * - Each stack frame on the stack is either an object id of the reentrant
 *   or closure stack object or a local stack frame.
 *
 * Local stack frame:
 * - function object id
 * - catch_offs / catch reg
 * - instr_offs
 * - registers
 *   ...
 * - number of registers (small int)
 */

typedef struct _KOS_STACK {
    KOS_STACK_HEADER       header;
    uint32_t               capacity;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(KOS_OBJ_ID) buf[1]; /* Actual stack */
} KOS_STACK;

struct _KOS_THREAD_CONTEXT {
    KOS_THREAD_CONTEXT  *next;     /* List of thread roots in context */
    KOS_THREAD_CONTEXT  *prev;

    struct _KOS_CONTEXT *ctx;
    _KOS_PAGE           *cur_page;
    KOS_OBJ_ID           exception;
    KOS_OBJ_ID           retval;
    KOS_OBJ_REF         *obj_refs;
    KOS_OBJ_ID           stack;    /* Topmost container for registers & stack frames */
    uint32_t             regs_idx; /* Index of first register in current frame       */
    uint32_t             stack_depth;
};

struct _KOS_PROTOTYPES {
    KOS_OBJ_ID object_proto;
    KOS_OBJ_ID number_proto;
    KOS_OBJ_ID integer_proto;
    KOS_OBJ_ID float_proto;
    KOS_OBJ_ID string_proto;
    KOS_OBJ_ID boolean_proto;
    KOS_OBJ_ID void_proto;
    KOS_OBJ_ID array_proto;
    KOS_OBJ_ID buffer_proto;
    KOS_OBJ_ID function_proto;
    KOS_OBJ_ID class_proto;
    KOS_OBJ_ID generator_proto;
    KOS_OBJ_ID exception_proto;
    KOS_OBJ_ID generator_end_proto;
    KOS_OBJ_ID thread_proto;
};

struct _KOS_MODULE_MGMT {
    KOS_OBJ_ID search_paths;
    KOS_OBJ_ID module_names;
    KOS_OBJ_ID modules;
    KOS_OBJ_ID init_module;

    struct _KOS_RED_BLACK_NODE    *module_inits;
    struct _KOS_MODULE_LOAD_CHAIN *load_chain;
};

struct _KOS_THREAD_MGMT {
    _KOS_TLS_KEY       thread_key;
    KOS_THREAD_CONTEXT main_thread;
    _KOS_MUTEX         mutex;
};

enum _KOS_CONTEXT_FLAGS {
    KOS_CTX_NO_FLAGS = 0,
    KOS_CTX_VERBOSE  = 1,
    KOS_CTX_DISASM   = 2
};

struct _KOS_CONTEXT {
    uint32_t                flags;
    struct _KOS_HEAP        heap;
    KOS_OBJ_ID              empty_string;
    KOS_OBJ_ID              args;
    struct _KOS_PROTOTYPES  prototypes;
    struct _KOS_MODULE_MGMT modules;
    struct _KOS_THREAD_MGMT threads;
};

#ifdef __cplusplus
static inline bool KOS_is_exception_pending(KOS_FRAME frame)
{
    return ! IS_BAD_PTR(frame->exception);
}

static inline KOS_OBJ_ID KOS_get_exception(KOS_FRAME frame)
{
    return frame->exception;
}

static inline void KOS_clear_exception(KOS_FRAME frame)
{
    frame->exception = KOS_BADPTR;
}
#else
#define KOS_is_exception_pending(frame) (!IS_BAD_PTR((frame)->exception))
#define KOS_get_exception(frame) ((frame)->exception)
#define KOS_clear_exception(frame) (void)((frame)->exception = KOS_BADPTR)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int KOS_context_init(KOS_CONTEXT *ctx,
                     KOS_FRAME   *out_frame);

void KOS_context_destroy(KOS_CONTEXT *ctx);

int KOS_context_add_path(KOS_FRAME   frame,
                         const char *module_search_path);

int KOS_context_add_default_path(KOS_FRAME   frame,
                                 const char *argv0);

int KOS_context_set_args(KOS_FRAME    frame,
                         int          argc,
                         const char **argv);

typedef int (*KOS_BUILTIN_INIT)(KOS_FRAME frame, KOS_OBJ_ID module);

int KOS_context_register_builtin(KOS_FRAME        frame,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init);

int KOS_context_register_thread(KOS_CONTEXT        *ctx,
                                KOS_THREAD_CONTEXT *thread_ctx);

void KOS_context_unregister_thread(KOS_CONTEXT        *ctx,
                                   KOS_THREAD_CONTEXT *thread_ctx);

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

KOS_OBJ_ID KOS_format_exception(KOS_FRAME  frame,
                                KOS_OBJ_ID exception);

void KOS_raise_generator_end(KOS_FRAME frame);

enum _KOS_CALL_FLAVOR {
    KOS_CALL_FUNCTION,
    KOS_CALL_GENERATOR,
    KOS_APPLY_FUNCTION
};

KOS_OBJ_ID _KOS_call_function(KOS_FRAME             frame,
                              KOS_OBJ_ID            func_obj,
                              KOS_OBJ_ID            this_obj,
                              KOS_OBJ_ID            args_obj,
                              enum _KOS_CALL_FLAVOR call_flavor);

#define KOS_call_function(frame, func_obj, this_obj, args_obj) \
    _KOS_call_function((frame), (func_obj), (this_obj), (args_obj), KOS_CALL_FUNCTION)

#define KOS_call_generator(frame, func_obj, this_obj, args_obj) \
    _KOS_call_function((frame), (func_obj), (this_obj), (args_obj), KOS_CALL_GENERATOR)

#define KOS_apply_function(frame, func_obj, this_obj, args_obj) \
    _KOS_call_function((frame), (func_obj), (this_obj), (args_obj), KOS_APPLY_FUNCTION)

void KOS_track_ref(KOS_FRAME    frame,
                   KOS_OBJ_REF *ref);

void KOS_untrack_ref(KOS_FRAME    frame,
                     KOS_OBJ_REF *ref);

struct _KOS_GC_STATS {
    unsigned num_objs_evacuated;
    unsigned num_objs_freed;
    unsigned num_objs_finalized;
    unsigned num_pages_kept;
    unsigned num_pages_freed;
    unsigned size_evacuated;
    unsigned size_freed;
    unsigned size_kept;
};

int KOS_collect_garbage(KOS_FRAME             frame,
                        struct _KOS_GC_STATS *stats);

#ifdef __cplusplus
}
#endif

#endif

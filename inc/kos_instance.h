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

#ifndef KOS_INSTANCE_H_INCLUDED
#define KOS_INSTANCE_H_INCLUDED

#include "kos_object_base.h"
#include "kos_threads.h"
#include <stddef.h>

struct KOS_MODULE_LOAD_CHAIN_S;
struct KOS_PAGE_HEADER_S;
struct KOS_POOL_HEADER_S;

typedef struct KOS_PAGE_HEADER_S       KOS_PAGE;
typedef struct KOS_POOL_HEADER_S       KOS_POOL;
typedef struct KOS_MODULE_LOAD_CHAIN_S KOS_MODULE_LOAD_CHAIN;

typedef struct KOS_HEAP_S {
    KOS_MUTEX            mutex;
    KOS_ATOMIC(uint32_t) gc_state;
    uint32_t             heap_size;      /* Total amount of memory owned by the heap */
    uint32_t             used_size;      /* Size used in full_ and non_full_pages    */
    uint32_t             gc_threshold;   /* Next used size that triggers GC          */
    KOS_PAGE            *free_pages;     /* Pages which are currently unused         */
    KOS_PAGE            *non_full_pages; /* Pages in which new objects are allocated */
    KOS_PAGE            *full_pages;     /* Pages which have no room for new objects */
    KOS_POOL            *pools;          /* Allocated memory - page pools            */
    KOS_POOL            *pool_headers;   /* List of pool headers for new pools       */

#ifdef CONFIG_MAD_GC
    struct KOS_LOCKED_PAGES_S *locked_pages_first;
    struct KOS_LOCKED_PAGES_S *locked_pages_last;
#endif
} KOS_HEAP;

/* Stored on the stack as catch offset */
enum KOS_CATCH_STATE_E {
    KOS_NO_CATCH = 0x1FFFFF
};

/* Stack header flags */
enum KOS_STACK_HEADER_FLAGS_E {
    KOS_NORMAL_STACK    = 0U,
    KOS_REENTRANT_STACK = 1U,   /* Stack of a generator or closure      */
    KOS_CAN_YIELD       = 2U    /* Indicates that a generator can yield */
};

typedef struct KOS_STACK_HEADER_S {
    KOS_OBJ_ID alloc_size;
    uint8_t    type;
    uint8_t    flags;
    uint8_t    yield_reg; /* In a generator stack, this is the index of the yield register */
} KOS_STACK_HEADER;

/* Stack management:
 * - If this is not the root stack object, the first element on the stack
 *   is the object id of the previous stack object.
 * - Each stack frame on the stack is either an object id of the reentrant
 *   or closure stack object or a local stack frame.
 *
 * Local stack frame:
 * +0 function object id
 * +1 catch_offs / catch reg
 * +2 instr_offs
 * +3 registers
 *    ...
 * +N number of registers (small int)
 */

typedef struct KOS_STACK_S {
    KOS_STACK_HEADER       header;
    uint32_t               capacity;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(KOS_OBJ_ID) buf[1]; /* Actual stack */
} KOS_STACK;

#define KOS_MAX_LOCALS 16

struct KOS_THREAD_CONTEXT_S {
    KOS_CONTEXT   next;     /* List of thread roots in instance */
    KOS_CONTEXT   prev;

    KOS_INSTANCE *inst;
    KOS_PAGE     *cur_page;
    KOS_OBJ_ID    thread_obj;
    KOS_OBJ_ID    exception;
    KOS_OBJ_ID    retval;
    KOS_OBJ_ID    stack;        /* Topmost container for registers & stack frames */
    uint32_t      regs_idx;     /* Index of first register in current frame       */
    uint32_t      stack_depth;
    KOS_OBJ_ID    local_refs;   /* Object id refs on user's local stack           */
    KOS_OBJ_ID   *tmp_refs[12]; /* Object id refs during object creation          */
    KOS_OBJ_ID   *helper_refs[KOS_MAX_LOCALS]; /* Helper when pushing locals stack*/
    uint32_t      tmp_ref_count;
    uint32_t      helper_ref_count;
};

struct KOS_PROTOTYPES_S {
    KOS_OBJ_ID object_proto;
    KOS_OBJ_ID number_proto;
    KOS_OBJ_ID integer_proto;
    KOS_OBJ_ID float_proto;
    KOS_OBJ_ID string_proto;
    KOS_OBJ_ID boolean_proto;
    KOS_OBJ_ID array_proto;
    KOS_OBJ_ID buffer_proto;
    KOS_OBJ_ID function_proto;
    KOS_OBJ_ID class_proto;
    KOS_OBJ_ID generator_proto;
    KOS_OBJ_ID exception_proto;
    KOS_OBJ_ID generator_end_proto;
    KOS_OBJ_ID thread_proto;
};

struct KOS_MODULE_MGMT_S {
    KOS_OBJ_ID search_paths;
    KOS_OBJ_ID module_names;
    KOS_OBJ_ID modules;
    KOS_OBJ_ID init_module;  /* Initial module for top-level stack frame */
    KOS_OBJ_ID module_inits; /* Registered built-in module initializers  */

    KOS_MODULE_LOAD_CHAIN *load_chain;
};

struct KOS_THREAD_MGMT_S {
    KOS_TLS_KEY                 thread_key;
    struct KOS_THREAD_CONTEXT_S main_thread;
    KOS_MUTEX                   mutex;
};

enum KOS_STR_E {
    KOS_STR_EMPTY,
    KOS_STR_OUT_OF_MEMORY,

    KOS_STR_ARGS,
    KOS_STR_ARRAY,
    KOS_STR_BACKTRACE,
    KOS_STR_BOOLEAN,
    KOS_STR_BUFFER,
    KOS_STR_CLASS,
    KOS_STR_FALSE,
    KOS_STR_FILE,
    KOS_STR_FLOAT,
    KOS_STR_FUNCTION,
    KOS_STR_GLOBAL,
    KOS_STR_INTEGER,
    KOS_STR_LINE,
    KOS_STR_LOCALS,
    KOS_STR_MODULE,
    KOS_STR_OBJECT,
    KOS_STR_OFFSET,
    KOS_STR_PROTOTYPE,
    KOS_STR_QUOTE_MARK,
    KOS_STR_RESULT,
    KOS_STR_SLICE,
    KOS_STR_STRING,
    KOS_STR_TRUE,
    KOS_STR_VALUE,
    KOS_STR_VOID,
    KOS_STR_XBUILTINX,

    KOS_STR_NUM /* number of pre-allocated strings */
};

enum KOS_INSTANCE_FLAGS_E {
    KOS_INST_NO_FLAGS  = 0,
    KOS_INST_VERBOSE   = 1,
    KOS_INST_DEBUG     = 2,
    KOS_INST_DISASM    = 4,
    KOS_INST_MANUAL_GC = 8
};

struct KOS_INSTANCE_S {
    uint32_t                 flags;
    KOS_HEAP                 heap;
    KOS_OBJ_ID               common_strings[KOS_STR_NUM]; /* For KOS_get_string() */
    KOS_OBJ_ID               args;
    struct KOS_PROTOTYPES_S  prototypes;
    struct KOS_MODULE_MGMT_S modules;
    struct KOS_THREAD_MGMT_S threads;
};

#ifdef __cplusplus
static inline bool KOS_is_exception_pending(KOS_CONTEXT ctx)
{
    return ! IS_BAD_PTR(ctx->exception);
}

static inline KOS_OBJ_ID KOS_get_exception(KOS_CONTEXT ctx)
{
    return ctx->exception;
}

static inline void KOS_clear_exception(KOS_CONTEXT ctx)
{
    ctx->exception = KOS_BADPTR;
}
#else
#define KOS_is_exception_pending(ctx) (!IS_BAD_PTR((ctx)->exception))
#define KOS_get_exception(ctx) ((ctx)->exception)
#define KOS_clear_exception(ctx) (void)((ctx)->exception = KOS_BADPTR)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int KOS_instance_init(KOS_INSTANCE *inst,
                      uint32_t      flags,
                      KOS_CONTEXT  *out_ctx);

void KOS_instance_destroy(KOS_INSTANCE *inst);

int KOS_instance_add_path(KOS_CONTEXT ctx,
                          const char *module_search_path);

int KOS_instance_add_default_path(KOS_CONTEXT ctx,
                                  const char *argv0);

int KOS_instance_set_args(KOS_CONTEXT  ctx,
                          int          argc,
                          const char **argv);

typedef int (*KOS_BUILTIN_INIT)(KOS_CONTEXT ctx, KOS_OBJ_ID module);

int KOS_instance_register_builtin(KOS_CONTEXT      ctx,
                                  const char      *module,
                                  KOS_BUILTIN_INIT init);

int KOS_instance_register_thread(KOS_INSTANCE *inst,
                                 KOS_CONTEXT   ctx);

void KOS_instance_unregister_thread(KOS_INSTANCE *inst,
                                    KOS_CONTEXT   ctx);

KOS_OBJ_ID KOS_get_string(KOS_CONTEXT    ctx,
                          enum KOS_STR_E str_id);

#ifdef NDEBUG
#define KOS_instance_validate(ctx) ((void)0)
#else
void KOS_instance_validate(KOS_CONTEXT ctx);
#endif

void KOS_raise_exception(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  exception_obj);

void KOS_raise_exception_cstring(KOS_CONTEXT ctx,
                                 const char *cstr);

KOS_OBJ_ID KOS_format_exception(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  exception);

void KOS_raise_generator_end(KOS_CONTEXT ctx);

enum KOS_CALL_FLAVOR_E {
    KOS_CALL_FUNCTION,
    KOS_CALL_GENERATOR,
    KOS_APPLY_FUNCTION
};

KOS_OBJ_ID kos_call_function(KOS_CONTEXT            ctx,
                             KOS_OBJ_ID             func_obj,
                             KOS_OBJ_ID             this_obj,
                             KOS_OBJ_ID             args_obj,
                             enum KOS_CALL_FLAVOR_E call_flavor);

#define KOS_call_function(ctx, func_obj, this_obj, args_obj) \
    kos_call_function((ctx), (func_obj), (this_obj), (args_obj), KOS_CALL_FUNCTION)

#define KOS_call_generator(ctx, func_obj, this_obj, args_obj) \
    kos_call_function((ctx), (func_obj), (this_obj), (args_obj), KOS_CALL_GENERATOR)

#define KOS_apply_function(ctx, func_obj, this_obj, args_obj) \
    kos_call_function((ctx), (func_obj), (this_obj), (args_obj), KOS_APPLY_FUNCTION)

int KOS_push_local_scope(KOS_CONTEXT ctx, KOS_OBJ_ID *prev_scope);

void KOS_pop_local_scope(KOS_CONTEXT ctx, KOS_OBJ_ID *prev_scope);

int KOS_push_locals(KOS_CONTEXT ctx, int* push_status, int num_entries, ...);

void KOS_pop_locals(KOS_CONTEXT ctx, int push_status);

typedef struct KOS_GC_STATS_S {
    unsigned num_objs_evacuated;
    unsigned num_objs_freed;
    unsigned num_objs_finalized;
    unsigned num_pages_kept;
    unsigned num_pages_freed;
    unsigned size_evacuated;
    unsigned size_freed;
    unsigned size_kept;
    unsigned num_gray_passes;
    unsigned initial_heap_size;
    unsigned initial_used_size;
    unsigned heap_size;
    unsigned used_size;
    unsigned time_ms;
} KOS_GC_STATS;

int KOS_collect_garbage(KOS_CONTEXT   ctx,
                        KOS_GC_STATS *out_stats);

#ifdef __cplusplus
}
#endif

#endif

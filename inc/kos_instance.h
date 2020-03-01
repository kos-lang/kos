/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#include "kos_entity.h"
#include "kos_threads.h"
#include <stddef.h>

struct KOS_MODULE_LOAD_CHAIN_S;
struct KOS_PAGE_HEADER_S;
struct KOS_POOL_HEADER_S;

typedef struct KOS_PAGE_HEADER_S       KOS_PAGE;
typedef struct KOS_POOL_HEADER_S       KOS_POOL;
typedef struct KOS_MODULE_LOAD_CHAIN_S KOS_MODULE_LOAD_CHAIN;

typedef struct KOS_PAGE_LIST_S {
    KOS_PAGE *head;
    KOS_PAGE *tail;
} KOS_PAGE_LIST;

typedef struct KOS_HEAP_S {
    KOS_MUTEX              mutex;
    uint32_t               gc_state;       /* Says what the GC is doing                      */
    uint32_t               heap_size;      /* Total num bytes allocated for the heap         */
    uint32_t               used_heap_size; /* Num bytes allocated for objects on heap        */
    uint32_t               malloc_size;    /* Num bytes allocated for objs with malloc       */
    uint32_t               max_heap_size;  /* Maximum allowed heap size                      */
    uint32_t               max_malloc_size;/* Maximum allowed bytes allocated with malloc    */
    uint32_t               gc_threshold;   /* Next value of used_heap_size which triggers GC */
    KOS_PAGE              *free_pages;     /* Pages which are currently unused               */
    KOS_PAGE_LIST          used_pages;     /* Pages which contain objects                    */
    KOS_POOL              *pools;          /* Allocated memory for heap, in page pools       */

    KOS_ATOMIC(KOS_PAGE *) walk_pages;     /* Multi-threaded page marking/updating           */
    KOS_ATOMIC(uint32_t)   gray_marked;    /* Number of objects marked                       */
    uint32_t               walk_threads;   /* Number of threads helping with page walking    */
    uint32_t               threads_to_stop;/* Number of threads on which GC is waiting       */
    uint32_t               gc_cycles;      /* Number of GC cycles started                    */

    KOS_COND_VAR           engagement_cond;
    KOS_COND_VAR           walk_cond;
    KOS_COND_VAR           helper_cond;

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
enum KOS_STACK_FLAGS_E {
    KOS_NORMAL_STACK    = 0U,
    KOS_REENTRANT_STACK = 1U,   /* Stack of a generator or closure      */
    KOS_CAN_YIELD       = 2U    /* Indicates that a generator can yield */
};

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
    KOS_OBJ_HEADER         header;
    uint32_t               capacity;
    KOS_ATOMIC(uint32_t)   size;
    uint8_t                flags;
    uint8_t                yield_reg; /* In a generator stack, this is the index of the yield register */
    KOS_ATOMIC(KOS_OBJ_ID) buf[1]; /* Actual stack */
} KOS_STACK;

typedef struct KOS_LOCAL_S {
    struct KOS_LOCAL_S *next;
    KOS_OBJ_ID          o;
} KOS_LOCAL;

struct KOS_THREAD_CONTEXT_S {
    KOS_CONTEXT          next;     /* List of thread roots in instance */
    KOS_CONTEXT          prev;
    KOS_ATOMIC(uint32_t) gc_state;

    KOS_INSTANCE *inst;
    KOS_PAGE     *cur_page;
    KOS_OBJ_ID    thread_obj;
    KOS_OBJ_ID    exception;
    KOS_OBJ_ID    retval;
    KOS_OBJ_ID    stack;        /* Topmost container for registers & stack frames */
    uint32_t      regs_idx;     /* Index of first register in current frame       */
    uint32_t      stack_depth;
    KOS_LOCAL    *local_list;
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
    KOS_OBJ_ID search_paths; /* Paths where new modules are loaded from   */
    KOS_OBJ_ID module_names; /* Object which maps module names to indices */
    KOS_OBJ_ID modules;      /* Array of loaded modules                   */
    KOS_OBJ_ID init_module;  /* Initial module for top-level stack frame  */
    KOS_OBJ_ID module_inits; /* Registered built-in module initializers   */

    KOS_MODULE_LOAD_CHAIN *load_chain;
};

struct KOS_THREAD_MGMT_S {
    KOS_TLS_KEY                 thread_key;  /* TLS key for current context ptr */
    struct KOS_THREAD_CONTEXT_S main_thread; /* Main thread's context           */
    KOS_MUTEX                   ctx_mutex;   /* Mutex for registering contexts  */
    KOS_MUTEX                   new_mutex;   /* Mutex for creating threads      */
    KOS_ATOMIC(KOS_THREAD *)   *threads;     /* Array of thread objects         */
    KOS_ATOMIC(uint32_t)        num_threads; /* Number of used thread slots     */
    uint32_t                    max_threads; /* Maximum number of threads       */
    uint32_t                    can_create;  /* Spawning new threads is allowed */
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

void KOS_init_local_with(KOS_CONTEXT ctx, KOS_LOCAL *local, KOS_OBJ_ID obj_id);

#define KOS_init_local(ctx, local) KOS_init_local_with((ctx), (local), KOS_BADPTR)

void KOS_init_locals(KOS_CONTEXT ctx, int num_locals, ...);

KOS_OBJ_ID KOS_destroy_local(KOS_CONTEXT ctx, KOS_LOCAL *local);

KOS_OBJ_ID KOS_destroy_locals(KOS_CONTEXT ctx, KOS_LOCAL *first, KOS_LOCAL *last);

KOS_OBJ_ID KOS_destroy_top_local(KOS_CONTEXT ctx, KOS_LOCAL *local);

KOS_OBJ_ID KOS_destroy_top_locals(KOS_CONTEXT ctx, KOS_LOCAL *first, KOS_LOCAL *last);

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
    unsigned initial_used_heap_size;
    unsigned initial_malloc_size;
    unsigned heap_size;
    unsigned used_heap_size;
    unsigned malloc_size;
    unsigned time_us;
} KOS_GC_STATS;

#define KOS_GC_STATS_INIT(val) \
    { (val), (val), (val), (val), (val), (val), (val), (val), (val), (val), \
      (val), (val), (val), (val), (val), (val) }

int KOS_collect_garbage(KOS_CONTEXT   ctx,
                        KOS_GC_STATS *out_stats);

void KOS_help_gc(KOS_CONTEXT ctx);

void KOS_suspend_context(KOS_CONTEXT ctx);

int KOS_resume_context(KOS_CONTEXT ctx);

#ifdef __cplusplus
}
#endif

#endif

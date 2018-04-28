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
    _KOS_MUTEX               mutex;
    KOS_ATOMIC(uint32_t)     gc_state;
    KOS_ATOMIC(_KOS_PAGE *)  active_pages;      /* pages in which new objects are allocated */
    KOS_ATOMIC(_KOS_PAGE *)  free_pages;        /* pages which are currently unused         */
    KOS_ATOMIC(_KOS_PAGE *)  full_pages;        /* pages which have no room for new objects */
    KOS_ATOMIC(void *)       huge_objects;      /* objects which don't fit in page size     */
    KOS_ATOMIC(_KOS_POOL *)  pools;             /* allocated memory - page pools            */
    KOS_ATOMIC(_KOS_POOL *)  pool_headers;      /* list of pool headers for new pools       */
    KOS_ATOMIC(_KOS_WASTE *) waste;             /* unused memory from pool allocations      */
    KOS_OBJ_ID               str_oom_id;
};

enum _KOS_YIELD_STATE {
    KOS_CAN_YIELD    = 0x1U,  /* indicates a generator        */
    KOS_REGS_BOUND   = 0x2U   /* registers bound to a closure */
};

enum _KOS_CATCH_STATE {
    KOS_NO_CATCH = 0x7FFFFFFFU
};

#define KOS_MAX_SAVED_FRAMES 16

struct _KOS_SF_HDR {
    KOS_OBJ_ID alloc_size;
    uint8_t    type;
    uint8_t    catch_reg;
    uint8_t    yield_reg; /* index of the yield register */
    uint8_t    flags;
};

typedef struct _KOS_STACK_FRAME {
    struct _KOS_SF_HDR          header;
    struct _KOS_THREAD_CONTEXT *thread_ctx;
    uint32_t                    catch_offs;
    uint32_t                    instr_offs;
    uint8_t                     num_saved_frames;
    KOS_OBJ_ID                  parent;
    KOS_OBJ_ID                  module;
    KOS_OBJ_ID                  registers;
    KOS_OBJ_ID                  exception;
    KOS_OBJ_ID                  retval;
    KOS_OBJ_ID                  saved_frames[KOS_MAX_SAVED_FRAMES];
} KOS_STACK_FRAME;

struct _KOS_THREAD_CONTEXT {
    struct _KOS_THREAD_CONTEXT *next; /* Add list of thread roots in context */
    struct _KOS_THREAD_CONTEXT *prev;
    struct _KOS_STACK_FRAME     frame; /* TODO allocate root frame on heap */
    struct _KOS_CONTEXT        *ctx;
    _KOS_PAGE                  *cur_page;
};

typedef struct _KOS_THREAD_CONTEXT KOS_THREAD_CONTEXT;

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

enum _KOS_CONTEXT_FLAGS {
    KOS_CTX_NO_FLAGS = 0,
    KOS_CTX_VERBOSE  = 1,
    KOS_CTX_DISASM   = 2
};

struct _KOS_CONTEXT {
    uint32_t                       flags;

    struct _KOS_HEAP               heap;

    KOS_OBJ_ID                     empty_string;

    struct _KOS_PROTOTYPES         prototypes;

    _KOS_TLS_KEY                   thread_key;

    /* TODO gather all module-related members in a structure */
    KOS_OBJ_ID                     module_search_paths;
    KOS_OBJ_ID                     module_names;
    KOS_OBJ_ID                     modules;

    KOS_OBJ_ID                     args;

    KOS_MODULE                     init_module;
    KOS_THREAD_CONTEXT             main_thread;

    struct _KOS_RED_BLACK_NODE    *module_inits;
    struct _KOS_MODULE_LOAD_CHAIN *module_load_chain;
};

#ifdef __cplusplus
static inline KOS_CONTEXT *KOS_context_from_frame(KOS_FRAME frame)
{
    assert(frame);
    assert(frame->thread_ctx);
    return frame->thread_ctx->ctx;
}
#else
#define KOS_context_from_frame(frame) ((frame)->thread_ctx->ctx)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int KOS_context_init(KOS_CONTEXT *ctx,
                     KOS_FRAME   *frame);

void KOS_context_destroy(KOS_CONTEXT *ctx);

int KOS_context_add_path(KOS_FRAME   frame,
                         const char *module_search_path);

int KOS_context_add_default_path(KOS_FRAME   frame,
                                 const char *argv0);

int KOS_context_set_args(KOS_FRAME    frame,
                         int          argc,
                         const char **argv);

typedef int (*KOS_BUILTIN_INIT)(KOS_FRAME frame);

int KOS_context_register_builtin(KOS_FRAME        frame,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init);

int KOS_context_register_thread(KOS_CONTEXT        *ctx,
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

#ifdef __cplusplus
}
#endif

#endif

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

enum _KOS_CONTEXT_FLAGS {
    KOS_CTX_NO_FLAGS = 0,
    KOS_CTX_VERBOSE  = 1,
    KOS_CTX_DEBUG    = 2
};

struct _KOS_ALLOC_DEBUG {
    KOS_ATOMIC(void *) objects;
};

#ifndef CONFIG_ALLOCATOR
#define CONFIG_ALLOCATOR 0xDEB
#endif

struct _KOS_THREAD_ROOT {
    KOS_STACK_FRAME frame;
};

typedef struct _KOS_THREAD_ROOT KOS_THREAD_ROOT;

struct _KOS_CONTEXT {
    uint32_t                       flags;

    KOS_OBJECT                     object_prototype;
    KOS_OBJECT                     number_prototype;
    KOS_OBJECT                     integer_prototype;
    KOS_OBJECT                     float_prototype;
    KOS_OBJECT                     string_prototype;
    KOS_OBJECT                     boolean_prototype;
    KOS_OBJECT                     void_prototype;
    KOS_OBJECT                     array_prototype;
    KOS_OBJECT                     buffer_prototype;
    KOS_OBJECT                     function_prototype;

    KOS_ATOMIC(void *)             prototypes;
    KOS_ATOMIC(uint32_t)           prototypes_lock;

    _KOS_TLS_KEY                   thread_key;

    KOS_ARRAY                      module_search_paths;
    KOS_OBJECT                     module_names;
    KOS_ARRAY                      modules;

    KOS_MODULE                     init_module;
    KOS_THREAD_ROOT                main_thread;

    struct _KOS_RED_BLACK_NODE    *module_inits;
    struct _KOS_MODULE_LOAD_CHAIN *module_load_chain;

#if CONFIG_ALLOCATOR == 0xDEB
    struct _KOS_ALLOC_DEBUG        allocator;
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

int KOS_context_init(KOS_CONTEXT      *ctx,
                     KOS_STACK_FRAME **frame);

void KOS_context_destroy(KOS_CONTEXT *ctx);

int KOS_context_add_path(KOS_STACK_FRAME *frame,
                         const char      *module_search_path);

typedef int (*KOS_BUILTIN_INIT)(KOS_STACK_FRAME *frame);

int KOS_context_register_builtin(KOS_STACK_FRAME *frame,
                                 const char      *module,
                                 KOS_BUILTIN_INIT init);

int KOS_context_register_thread(KOS_CONTEXT *ctx, KOS_THREAD_ROOT *thread_root);

#ifdef NDEBUG
#define KOS_context_validate(frame) ((void)0)
#else
void KOS_context_validate(KOS_STACK_FRAME *frame);
#endif

void KOS_raise_exception(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      exception_obj);

void KOS_clear_exception(KOS_STACK_FRAME *frame);

int KOS_is_exception_pending(KOS_STACK_FRAME *frame);

KOS_OBJ_PTR KOS_get_exception(KOS_STACK_FRAME *frame);

KOS_OBJ_PTR KOS_format_exception(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      exception);

/* TODO find a better place */
KOS_OBJ_PTR KOS_get_file_name(KOS_STACK_FRAME *frame,
                              KOS_OBJ_PTR      full_path);

/* TODO find a better place */
int KOS_get_integer(KOS_STACK_FRAME *frame,
                    KOS_OBJ_PTR      obj,
                    int64_t         *ret);

KOS_OBJ_PTR KOS_gen_prototype(KOS_STACK_FRAME *frame,
                              const void      *ptr);

KOS_OBJ_PTR KOS_call_function(KOS_STACK_FRAME *frame,
                              KOS_OBJ_PTR      func_obj,
                              KOS_OBJ_PTR      this_obj,
                              KOS_OBJ_PTR      args_obj);

#ifdef __cplusplus
}
#endif

#endif

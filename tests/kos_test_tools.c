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

#include "kos_test_tools.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../core/kos_malloc.h"
#include "../core/kos_memory.h"
#include "../core/kos_system.h"

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
#   include <pthread.h>
#endif

static const char str_err_join_self[] = "thread cannot join itself";
static const char str_err_thread[]    = "failed to create thread";

#ifdef _WIN32

struct THREAD_S {
    HANDLE          thread_handle;
    DWORD           thread_id;
    KOS_INSTANCE   *inst;
    THREAD_PROC proc;
    void           *cookie;
    KOS_OBJ_ID      exception;
};

static DWORD WINAPI _thread_proc(LPVOID thread_obj)
{
    struct KOS_THREAD_CONTEXT_S thread_ctx;
    DWORD                       ret        = 0;
    int                         unregister = 0;

    if (KOS_instance_register_thread(((THREAD)thread_obj)->inst, &thread_ctx) == KOS_SUCCESS) {
        ((THREAD)thread_obj)->proc(&thread_ctx, ((THREAD)thread_obj)->cookie);
        unregister = 1;
    }

    if (KOS_is_exception_pending(&thread_ctx)) {
        ((THREAD)thread_obj)->exception = KOS_get_exception(&thread_ctx);
        ret = 1;
    }

    if (unregister)
        KOS_instance_unregister_thread(((THREAD)thread_obj)->inst, &thread_ctx);

    return ret;
}

int create_thread(KOS_CONTEXT ctx,
                  THREAD_PROC proc,
                  void       *cookie,
                  THREAD     *thread)
{
    int    error      = KOS_SUCCESS;
    THREAD new_thread = (THREAD)kos_malloc(sizeof(struct THREAD_S));

    if (new_thread) {
        int pushed = 0;

        new_thread->thread_id = 0;
        new_thread->inst      = ctx->inst;
        new_thread->proc      = proc;
        new_thread->cookie    = cookie;
        new_thread->exception = KOS_BADPTR;

        error = KOS_push_locals(ctx, &pushed, 1, &new_thread->exception);
        if (error)
            return error;

        new_thread->thread_handle = CreateThread(0,
                                                 0,
                                                 _thread_proc,
                                                 new_thread,
                                                 0,
                                                 &new_thread->thread_id);

        if (!new_thread->thread_handle) {
            kos_free(new_thread);
            KOS_raise_exception_cstring(ctx, str_err_thread);
            error = KOS_ERROR_EXCEPTION;
        }
        else
            *thread = new_thread;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

int join_thread(KOS_CONTEXT ctx,
                THREAD      thread)
{
    int error = KOS_SUCCESS;

    if (thread) {
        if (GetCurrentThreadId() == thread->thread_id) {
            KOS_raise_exception_cstring(ctx, str_err_join_self);
            return KOS_ERROR_EXCEPTION;
        }

        KOS_suspend_context(ctx);

        WaitForSingleObject(thread->thread_handle, INFINITE);
        CloseHandle(thread->thread_handle);

        KOS_resume_context(ctx);

        if ( ! IS_BAD_PTR(thread->exception)) {
            KOS_raise_exception(ctx, thread->exception);
            error = KOS_ERROR_EXCEPTION;
        }

        KOS_pop_locals(ctx, 1);

        kos_free(thread);
    }

    return error;
}

#else

struct THREAD_S {
    pthread_t     thread_handle;
    KOS_INSTANCE *inst;
    THREAD_PROC   proc;
    void         *cookie;
    KOS_OBJ_ID    exception;
};

static void *_thread_proc(void *thread_obj)
{
    struct KOS_THREAD_CONTEXT_S thread_ctx;
    int                         unregister = 0;

    if (KOS_instance_register_thread(((THREAD)thread_obj)->inst, &thread_ctx) == KOS_SUCCESS) {
        ((THREAD)thread_obj)->proc(&thread_ctx, ((THREAD)thread_obj)->cookie);
        unregister = 1;
    }

    if (KOS_is_exception_pending(&thread_ctx))
        ((THREAD)thread_obj)->exception = KOS_get_exception(&thread_ctx);

    if (unregister)
        KOS_instance_unregister_thread(((THREAD)thread_obj)->inst, &thread_ctx);

    return 0;
}

int create_thread(KOS_CONTEXT ctx,
                  THREAD_PROC proc,
                  void       *cookie,
                  THREAD     *thread)
{
    int    error      = KOS_SUCCESS;
    THREAD new_thread = (THREAD)kos_malloc(sizeof(struct THREAD_S));

    if (new_thread) {
        int pushed = 0;

        new_thread->inst      = ctx->inst;
        new_thread->proc      = proc;
        new_thread->cookie    = cookie;
        new_thread->exception = KOS_BADPTR;

        error = KOS_push_locals(ctx, &pushed, 1, &new_thread->exception);

        if (error) {
            kos_free(new_thread);
            return error;
        }

        if (pthread_create(&new_thread->thread_handle, 0, _thread_proc, new_thread)) {
            kos_free(new_thread);
            KOS_raise_exception_cstring(ctx, str_err_thread);
            error = KOS_ERROR_EXCEPTION;
        }
        else
            *thread = new_thread;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

int join_thread(KOS_CONTEXT ctx,
                THREAD      thread)
{
    int error = KOS_SUCCESS;

    if (thread) {
        void *ret = 0;

        if (pthread_equal(pthread_self(), thread->thread_handle)) {
            KOS_raise_exception_cstring(ctx, str_err_join_self);
            return KOS_ERROR_EXCEPTION;
        }

        KOS_suspend_context(ctx);

        pthread_join(thread->thread_handle, &ret);

        KOS_resume_context(ctx);

        if ( ! IS_BAD_PTR(thread->exception)) {
            KOS_raise_exception(ctx, thread->exception);
            error = KOS_ERROR_EXCEPTION;
        }

        KOS_pop_locals(ctx, 1);

        kos_free(thread);
    }

    return error;
}

#endif

int get_num_cpus(void)
{
    int        num_cpus = 2; /* By default behave as if there were 2 CPUs */
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    if (kos_get_env("TEST_CPUS", &cstr) == KOS_SUCCESS) {
        num_cpus = (int)strtol(cstr.buffer, 0, 10);
        if (num_cpus < 1) {
            kos_vector_destroy(&cstr);
            printf("Failed: Invalid value in TEST_CPUS env var!\n");
            exit(1);
        }
    }

    kos_vector_destroy(&cstr);

    return num_cpus;
}

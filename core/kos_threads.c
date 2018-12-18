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

#include "kos_threads_internal.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_try.h"
#include <assert.h>

#ifndef _WIN32
#   include <sched.h>
#endif

static const char str_err_join_self[] = "thread cannot join itself";
static const char str_err_thread[]    = "failed to create thread";

#ifdef KOS_GCC_ATOMIC_EXTENSION
int kos_atomic_cas_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int kos_atomic_cas_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif

void kos_atomic_move_ptr(KOS_ATOMIC(void *) *dest,
                         KOS_ATOMIC(void *) *src,
                         unsigned            ptr_count)
{
    if (src == dest)
        return;

    if (src < dest && src + ptr_count > dest) {
        KOS_ATOMIC(void *) *const end = src;

        src  += ptr_count - 1;
        dest += ptr_count - 1;

        do {
            KOS_atomic_write_ptr(*dest, KOS_atomic_read_ptr(*src));
            --src;
            --dest;
        }
        while (src >= end);
    }
    else {
        KOS_ATOMIC(void *) *const end = src + ptr_count;

        do {
            KOS_atomic_write_ptr(*dest, KOS_atomic_read_ptr(*src));
            ++src;
            ++dest;
        }
        while (src < end);
    }
}

void kos_spin_lock(KOS_ATOMIC(uint32_t) *lock)
{
    while (KOS_atomic_swap_u32(*lock, 1))
        kos_yield();
}

void kos_spin_unlock(KOS_ATOMIC(uint32_t) *lock)
{
#ifdef NDEBUG
    KOS_atomic_swap_u32(*lock, 0);
#else
    const uint32_t old = KOS_atomic_swap_u32(*lock, 0);
    assert(old == 1);
#endif
}

void kos_yield(void)
{
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

static KOS_OBJ_ID alloc_thread(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  thread_func,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_THREAD *thread;
    int         pushed = 0;

    if (KOS_push_locals(ctx, &pushed, 3, &thread_func, &this_obj, &args_obj))
        return KOS_BADPTR;

    thread = (KOS_THREAD *)kos_alloc_object(ctx, OBJ_THREAD, sizeof(KOS_THREAD));

    if (thread) {
        assert(thread->header.type == OBJ_THREAD);
        thread->inst        = ctx->inst;
        thread->thread_func = thread_func;
        thread->this_obj    = this_obj;
        thread->args_obj    = args_obj;
        thread->retval      = KOS_BADPTR;
        thread->exception   = KOS_BADPTR;
#ifdef _WIN32
        thread->thread_handle = 0;
        thread->thread_id     = 0;
#endif
    }

    KOS_pop_locals(ctx, pushed);

    return OBJID(THREAD, thread);
}

#ifdef _WIN32

#define THREAD_PTR(void_ptr) OBJPTR(THREAD, ((THREAD_INIT *)(void_ptr))->thread_obj)

typedef struct THREAD_INITIALIZER_S {
    KOS_OBJ_ID         thread_obj;
    CRITICAL_SECTION   start_cs;
    CONDITION_VARIABLE start_cv;
    int                started;
} THREAD_INIT;

static void release_thread_parent(LPVOID thread_ptr)
{
    THREAD_INIT *init = (THREAD_INIT *)thread_ptr;

    EnterCriticalSection(&init->start_cs);
    init->started = 1;
    WakeConditionVariable(&init->start_cv);
    LeaveCriticalSection(&init->start_cs);
}

static DWORD WINAPI thread_proc(LPVOID thread_ptr)
{
    struct KOS_THREAD_CONTEXT_S thread_ctx;
    KOS_OBJ_ID                  thread_obj = KOS_BADPTR;

    {
        int pushed = 0;

        if (KOS_instance_register_thread(THREAD_PTR(thread_ptr)->inst, &thread_ctx) ||
            KOS_push_locals(&thread_ctx, &pushed, 1, &thread_obj)) {

            assert(KOS_is_exception_pending(&thread_ctx));

            THREAD_PTR(thread_ptr)->exception = KOS_get_exception(&thread_ctx);

            release_thread_parent(thread_ptr);

            return 0;
        }
    }

    thread_obj = ((THREAD_INIT *)thread_ptr)->thread_obj;

    release_thread_parent(thread_ptr);

    {
        KOS_OBJ_ID retval;

        retval = KOS_call_function(&thread_ctx,
                                   OBJPTR(THREAD, thread_obj)->thread_func,
                                   OBJPTR(THREAD, thread_obj)->this_obj,
                                   OBJPTR(THREAD, thread_obj)->args_obj);

        OBJPTR(THREAD, thread_obj)->retval = retval;

        if (IS_BAD_PTR(retval)) {
            assert(KOS_is_exception_pending(&thread_ctx));

            OBJPTR(THREAD, thread_obj)->exception = KOS_get_exception(&thread_ctx);
        }
        else {
            assert( ! KOS_is_exception_pending(&thread_ctx));

            OBJPTR(THREAD, thread_obj)->retval = retval;
        }
    }

    KOS_instance_unregister_thread(thread_ctx.inst, &thread_ctx);

    return 0;
}

KOS_OBJ_ID kos_thread_create(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  thread_func,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int         error  = KOS_SUCCESS;
    int         pushed = 0;
    THREAD_INIT init;

    InitializeCriticalSection(&init.start_cs);
    InitializeConditionVariable(&init.start_cv);

    init.started = 0;

    init.thread_obj = alloc_thread(ctx, thread_func, this_obj, args_obj);
    TRY_OBJID(init.thread_obj);

    TRY(KOS_push_locals(ctx, &pushed, 1, &init.thread_obj));

    OBJPTR(THREAD, init.thread_obj)->thread_handle = kos_seq_fail() ? 0 :
        CreateThread(0,
                     0,
                     thread_proc,
                     &init,
                     0,
                     &OBJPTR(THREAD, init.thread_obj)->thread_id);

    if ( ! OBJPTR(THREAD, init.thread_obj)->thread_handle)
        RAISE_EXCEPTION(str_err_thread);

    EnterCriticalSection(&init.start_cs);
    while ( ! init.started)
        SleepConditionVariableCS(&init.start_cv, &init.start_cs, INFINITE);
    LeaveCriticalSection(&init.start_cs);

cleanup:
    KOS_pop_locals(ctx, pushed);
    DeleteCriticalSection(&init.start_cs);

    return error ? KOS_BADPTR : init.thread_obj;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  thread)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert( ! IS_BAD_PTR(thread));

    TRY(KOS_push_locals(ctx, &pushed, 1, &thread));

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    WaitForSingleObject(OBJPTR(THREAD, thread)->thread_handle, INFINITE);
    CloseHandle(OBJPTR(THREAD, thread)->thread_handle);

    if (IS_BAD_PTR(OBJPTR(THREAD, thread)->exception)) {
        retval = OBJPTR(THREAD, thread)->retval;
        assert( ! IS_BAD_PTR(retval));
    }
    else {
        KOS_raise_exception(ctx, OBJPTR(THREAD, thread)->exception);
        error = KOS_ERROR_EXCEPTION;
    }

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error ? KOS_BADPTR : retval;
}

int kos_is_current_thread(KOS_OBJ_ID thread)
{
    assert( ! IS_BAD_PTR(thread));

    return GetCurrentThreadId() == OBJPTR(THREAD, thread)->thread_id ? 1 : 0;
}

struct KOS_MUTEX_OBJECT_S {
    CRITICAL_SECTION cs;
};

int kos_create_mutex(KOS_MUTEX *mutex)
{
    int error = KOS_SUCCESS;

    assert(mutex);
    *mutex = (KOS_MUTEX)kos_malloc(sizeof(struct KOS_MUTEX_OBJECT_S));

    if (*mutex)
        InitializeCriticalSection(&(*mutex)->cs);
    else
        error = KOS_ERROR_EXCEPTION;

    return error;
}

void kos_destroy_mutex(KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    DeleteCriticalSection(&(*mutex)->cs);

    kos_free(*mutex);
}

void kos_lock_mutex(KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    EnterCriticalSection(&(*mutex)->cs);
}

void kos_unlock_mutex(KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    LeaveCriticalSection(&(*mutex)->cs);
}

int kos_tls_create(KOS_TLS_KEY *key)
{
    int   error   = KOS_SUCCESS;
    DWORD new_key;

    if (kos_seq_fail())
        new_key = TLS_OUT_OF_INDEXES;
    else
        new_key = TlsAlloc();

    if (new_key == TLS_OUT_OF_INDEXES)
        error = KOS_ERROR_OUT_OF_MEMORY;
    else
        *key = new_key;

    return error;
}

void kos_tls_destroy(KOS_TLS_KEY key)
{
    TlsFree(key);
}

void *kos_tls_get(KOS_TLS_KEY key)
{
    return TlsGetValue(key);
}

void kos_tls_set(KOS_TLS_KEY key, void *value)
{
    TlsSetValue(key, value);
}

#else

#define THREAD_PTR(void_ptr) OBJPTR(THREAD, ((THREAD_INIT *)(void_ptr))->thread_obj)

typedef struct THREAD_INITIALIZER_S {
    KOS_OBJ_ID         thread_obj;
    pthread_mutex_t    start_mutex;
    pthread_cond_t     start_cv;
    int                started;
} THREAD_INIT;

static void release_thread_parent(void *thread_ptr)
{
    THREAD_INIT *init = (THREAD_INIT *)thread_ptr;

    pthread_mutex_lock(&init->start_mutex);
    init->started = 1;
    pthread_cond_signal(&init->start_cv);
    pthread_mutex_unlock(&init->start_mutex);
}

static void *thread_proc(void *thread_ptr)
{
    struct KOS_THREAD_CONTEXT_S thread_ctx;
    KOS_OBJ_ID                  thread_obj = KOS_BADPTR;

    {
        int pushed = 0;

        if (KOS_instance_register_thread(THREAD_PTR(thread_ptr)->inst, &thread_ctx) ||
            KOS_push_locals(&thread_ctx, &pushed, 1, &thread_obj)) {

            assert(KOS_is_exception_pending(&thread_ctx));

            THREAD_PTR(thread_ptr)->exception = KOS_get_exception(&thread_ctx);

            release_thread_parent(thread_ptr);

            return 0;
        }
    }

    thread_obj = ((THREAD_INIT *)thread_ptr)->thread_obj;

    release_thread_parent(thread_ptr);

    {
        KOS_OBJ_ID retval;

        retval = KOS_call_function(&thread_ctx,
                                   OBJPTR(THREAD, thread_obj)->thread_func,
                                   OBJPTR(THREAD, thread_obj)->this_obj,
                                   OBJPTR(THREAD, thread_obj)->args_obj);

        OBJPTR(THREAD, thread_obj)->retval = retval;

        if (IS_BAD_PTR(retval)) {
            assert(KOS_is_exception_pending(&thread_ctx));

            OBJPTR(THREAD, thread_obj)->exception = KOS_get_exception(&thread_ctx);
        }
        else {
            assert( ! KOS_is_exception_pending(&thread_ctx));

            OBJPTR(THREAD, thread_obj)->retval = retval;
        }
    }

    KOS_instance_unregister_thread(thread_ctx.inst, &thread_ctx);

    return 0;
}

KOS_OBJ_ID kos_thread_create(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  thread_func,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int         error  = KOS_SUCCESS;
    int         pushed = 0;
    THREAD_INIT init;

    pthread_mutex_init(&init.start_mutex, NULL);
    pthread_cond_init(&init.start_cv, NULL);

    init.started = 0;

    init.thread_obj = alloc_thread(ctx, thread_func, this_obj, args_obj);
    TRY_OBJID(init.thread_obj);

    TRY(KOS_push_locals(ctx, &pushed, 1, &init.thread_obj));

    if (kos_seq_fail() || pthread_create(&OBJPTR(THREAD, init.thread_obj)->thread_handle,
                                         0,
                                         thread_proc,
                                         &init))
        RAISE_EXCEPTION(str_err_thread);

    pthread_mutex_lock(&init.start_mutex);
    while ( ! init.started)
        pthread_cond_wait(&init.start_cv, &init.start_mutex);
    pthread_mutex_unlock(&init.start_mutex);

cleanup:
    KOS_pop_locals(ctx, pushed);
    pthread_cond_destroy(&init.start_cv);
    pthread_mutex_destroy(&init.start_mutex);

    return error ? KOS_BADPTR : init.thread_obj;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  thread)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert( ! IS_BAD_PTR(thread));

    TRY(KOS_push_locals(ctx, &pushed, 1, &thread));

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    pthread_join(OBJPTR(THREAD, thread)->thread_handle, NULL);

    if (IS_BAD_PTR(OBJPTR(THREAD, thread)->exception)) {
        retval = OBJPTR(THREAD, thread)->retval;
        assert( ! IS_BAD_PTR(retval));
    }
    else {
        KOS_raise_exception(ctx, OBJPTR(THREAD, thread)->exception);
        error = KOS_ERROR_EXCEPTION;
    }

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error ? KOS_BADPTR : retval;
}

int kos_is_current_thread(KOS_OBJ_ID thread)
{
    assert( ! IS_BAD_PTR(thread));

    return pthread_equal(pthread_self(), OBJPTR(THREAD, thread)->thread_handle);
}

struct KOS_MUTEX_OBJECT_S {
    pthread_mutex_t mutex;
};

int kos_create_mutex(KOS_MUTEX *mutex)
{
    int error = KOS_SUCCESS;

    assert(mutex);
    *mutex = (KOS_MUTEX)kos_malloc(sizeof(struct KOS_MUTEX_OBJECT_S));

    if (*mutex) {
        if (kos_seq_fail() || pthread_mutex_init(&(*mutex)->mutex, 0)) {
            kos_free(*mutex);
            *mutex = 0;
            error = KOS_ERROR_OUT_OF_MEMORY;
        }
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void kos_destroy_mutex(KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    pthread_mutex_destroy(&(*mutex)->mutex);

    kos_free(*mutex);
}

void kos_lock_mutex(KOS_MUTEX *mutex)
{
#ifndef NDEBUG
    int ret;
#endif

    assert(mutex && *mutex);

#ifndef NDEBUG
    ret =
#endif
    pthread_mutex_lock(&(*mutex)->mutex);

    assert(ret == 0);
}

void kos_unlock_mutex(KOS_MUTEX *mutex)
{
#ifndef NDEBUG
    int ret;
#endif

    assert(mutex && *mutex);

#ifndef NDEBUG
    ret =
#endif
    pthread_mutex_unlock(&(*mutex)->mutex);

    assert(ret == 0);
}

struct KOS_TLS_OBJECT_S {
    pthread_key_t key;
};

int kos_tls_create(KOS_TLS_KEY *key)
{
    int         error   = KOS_SUCCESS;
    KOS_TLS_KEY new_key = (KOS_TLS_KEY)kos_malloc(sizeof(struct KOS_TLS_OBJECT_S));

    if (new_key) {
        if (kos_seq_fail() || pthread_key_create(&new_key->key, 0)) {
            kos_free(new_key);
            error = KOS_ERROR_OUT_OF_MEMORY;
        }
        else
            *key = new_key;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void kos_tls_destroy(KOS_TLS_KEY key)
{
    pthread_key_delete(key->key);
    kos_free(key);
}

void *kos_tls_get(KOS_TLS_KEY key)
{
    return pthread_getspecific(key->key);
}

void kos_tls_set(KOS_TLS_KEY key, void *value)
{
    pthread_setspecific(key->key, value);
}

#endif

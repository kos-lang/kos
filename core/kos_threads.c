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

#include "../inc/kos_threads.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include <assert.h>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
#   include <pthread.h>
#   include <sched.h>
#endif

static const char str_err_join_self[] = "thread cannot join itself";
static const char str_err_thread[]    = "failed to create thread";

#ifdef KOS_GCC_ATOMIC_EXTENSION
int _KOS_atomic_cas_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int _KOS_atomic_cas_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif

void _KOS_atomic_move_ptr(KOS_ATOMIC(void *) *dest,
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

void _KOS_spin_lock(KOS_ATOMIC(uint32_t) *lock)
{
    while (KOS_atomic_swap_u32(*lock, 1))
        _KOS_yield();
}

void _KOS_spin_unlock(KOS_ATOMIC(uint32_t) *lock)
{
#ifdef NDEBUG
    KOS_atomic_swap_u32(*lock, 0);
#else
    const uint32_t old = KOS_atomic_swap_u32(*lock, 0);
    assert(old == 1);
#endif
}

void _KOS_yield(void)
{
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

#ifdef _WIN32

struct _KOS_THREAD_OBJECT {
    HANDLE           thread_handle;
    DWORD            thread_id;
    KOS_INSTANCE    *inst;
    _KOS_THREAD_PROC proc;
    void            *cookie;
    KOS_OBJ_ID       exception;
};

static DWORD WINAPI _thread_proc(LPVOID thread_obj)
{
    struct _KOS_THREAD_CONTEXT thread_ctx;
    DWORD                      ret        = 0;
    int                        unregister = 0;

    if (KOS_instance_register_thread(((_KOS_THREAD)thread_obj)->inst, &thread_ctx) == KOS_SUCCESS) {
        ((_KOS_THREAD)thread_obj)->proc(&thread_ctx, ((_KOS_THREAD)thread_obj)->cookie);
        unregister = 1;
    }

    if (KOS_is_exception_pending(&thread_ctx)) {
        ((_KOS_THREAD)thread_obj)->exception = KOS_get_exception(&thread_ctx);
        /* TODO nobody owns exception */
        ret = 1;
    }

    if (unregister)
        KOS_instance_unregister_thread(((_KOS_THREAD)thread_obj)->inst, &thread_ctx);

    return ret;
}

int _KOS_thread_create(KOS_CONTEXT      ctx,
                       _KOS_THREAD_PROC proc,
                       void            *cookie,
                       _KOS_THREAD     *thread)
{
    int         error      = KOS_SUCCESS;
    _KOS_THREAD new_thread = (_KOS_THREAD)_KOS_malloc(sizeof(struct _KOS_THREAD_OBJECT));

    if (new_thread) {
        new_thread->thread_id = 0;
        new_thread->inst      = ctx->inst;
        new_thread->proc      = proc;
        new_thread->cookie    = cookie;
        new_thread->exception = KOS_BADPTR;

        if (_KOS_seq_fail())
            new_thread->thread_handle = 0;
        else
            new_thread->thread_handle = CreateThread(0,
                                                     0,
                                                     _thread_proc,
                                                     new_thread,
                                                     0,
                                                     &new_thread->thread_id);

        if (!new_thread->thread_handle) {
            _KOS_free(new_thread);
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

int _KOS_thread_join(KOS_CONTEXT ctx,
                     _KOS_THREAD thread)
{
    int error = KOS_SUCCESS;

    if (thread) {
        if (_KOS_is_current_thread(thread)) {
            KOS_raise_exception_cstring(ctx, str_err_join_self);
            return KOS_ERROR_EXCEPTION;
        }

        WaitForSingleObject(thread->thread_handle, INFINITE);
        CloseHandle(thread->thread_handle);

        if ( ! IS_BAD_PTR(thread->exception)) {
            KOS_raise_exception(ctx, thread->exception);
            error = KOS_ERROR_EXCEPTION;
        }

        _KOS_free(thread);
    }

    return error;
}

int _KOS_is_current_thread(_KOS_THREAD thread)
{
    assert(thread);

    return GetCurrentThreadId() == thread->thread_id ? 1 : 0;
}

struct _KOS_MUTEX_OBJECT {
    CRITICAL_SECTION cs;
};

int _KOS_create_mutex(_KOS_MUTEX *mutex)
{
    int error = KOS_SUCCESS;

    assert(mutex);
    *mutex = (_KOS_MUTEX)_KOS_malloc(sizeof(struct _KOS_MUTEX_OBJECT));

    if (*mutex)
        InitializeCriticalSection(&(*mutex)->cs);
    else
        error = KOS_ERROR_EXCEPTION;

    return error;
}

void _KOS_destroy_mutex(_KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    DeleteCriticalSection(&(*mutex)->cs);

    _KOS_free(*mutex);
}

void _KOS_lock_mutex(_KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    EnterCriticalSection(&(*mutex)->cs);
}

void _KOS_unlock_mutex(_KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    LeaveCriticalSection(&(*mutex)->cs);
}

int _KOS_tls_create(_KOS_TLS_KEY *key)
{
    int   error   = KOS_SUCCESS;
    DWORD new_key;

    if (_KOS_seq_fail())
        new_key = TLS_OUT_OF_INDEXES;
    else
        new_key = TlsAlloc();

    if (new_key == TLS_OUT_OF_INDEXES)
        error = KOS_ERROR_OUT_OF_MEMORY;
    else
        *key = new_key;

    return error;
}

void _KOS_tls_destroy(_KOS_TLS_KEY key)
{
    TlsFree(key);
}

void *_KOS_tls_get(_KOS_TLS_KEY key)
{
    return TlsGetValue(key);
}

void _KOS_tls_set(_KOS_TLS_KEY key, void *value)
{
    TlsSetValue(key, value);
}

#else

struct _KOS_THREAD_OBJECT {
    pthread_t        thread_handle;
    KOS_INSTANCE    *inst;
    _KOS_THREAD_PROC proc;
    void            *cookie;
};

static void *_thread_proc(void *thread_obj)
{
    struct _KOS_THREAD_CONTEXT thread_ctx;
    void                      *ret        = 0;
    int                        unregister = 0;

    if (KOS_instance_register_thread(((_KOS_THREAD)thread_obj)->inst, &thread_ctx) == KOS_SUCCESS) {
        ((_KOS_THREAD)thread_obj)->proc(&thread_ctx, ((_KOS_THREAD)thread_obj)->cookie);
        unregister = 1;
    }

    if (KOS_is_exception_pending(&thread_ctx))
        ret = (void *)KOS_get_exception(&thread_ctx);
        /* TODO nobody owns exception */

    if (unregister)
        KOS_instance_unregister_thread(((_KOS_THREAD)thread_obj)->inst, &thread_ctx);

    return ret;
}

int _KOS_thread_create(KOS_CONTEXT      ctx,
                       _KOS_THREAD_PROC proc,
                       void            *cookie,
                       _KOS_THREAD     *thread)
{
    int         error      = KOS_SUCCESS;
    _KOS_THREAD new_thread = (_KOS_THREAD)_KOS_malloc(sizeof(struct _KOS_THREAD_OBJECT));

    if (new_thread) {
        new_thread->inst   = ctx->inst;
        new_thread->proc   = proc;
        new_thread->cookie = cookie;

        if (_KOS_seq_fail() || pthread_create(&new_thread->thread_handle, 0, _thread_proc, new_thread)) {
            _KOS_free(new_thread);
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

int _KOS_thread_join(KOS_CONTEXT ctx,
                     _KOS_THREAD thread)
{
    int error = KOS_SUCCESS;

    if (thread) {
        void *ret  = 0;

        if (_KOS_is_current_thread(thread)) {
            KOS_raise_exception_cstring(ctx, str_err_join_self);
            return KOS_ERROR_EXCEPTION;
        }

        pthread_join(thread->thread_handle, &ret);

        if (ret) {
            KOS_raise_exception(ctx, (KOS_OBJ_ID)ret);
            error = KOS_ERROR_EXCEPTION;
        }

        _KOS_free(thread);
    }

    return error;
}

int _KOS_is_current_thread(_KOS_THREAD thread)
{
    assert(thread);

    return pthread_equal(pthread_self(), thread->thread_handle);
}

struct _KOS_MUTEX_OBJECT {
    pthread_mutex_t mutex;
};

int _KOS_create_mutex(_KOS_MUTEX *mutex)
{
    int error = KOS_SUCCESS;

    assert(mutex);
    *mutex = (_KOS_MUTEX)_KOS_malloc(sizeof(struct _KOS_MUTEX_OBJECT));

    if (*mutex) {
        if (_KOS_seq_fail() || pthread_mutex_init(&(*mutex)->mutex, 0)) {
            _KOS_free(*mutex);
            *mutex = 0;
            error = KOS_ERROR_OUT_OF_MEMORY;
        }
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void _KOS_destroy_mutex(_KOS_MUTEX *mutex)
{
    assert(mutex && *mutex);

    pthread_mutex_destroy(&(*mutex)->mutex);

    _KOS_free(*mutex);
}

void _KOS_lock_mutex(_KOS_MUTEX *mutex)
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

void _KOS_unlock_mutex(_KOS_MUTEX *mutex)
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

struct _KOS_TLS_OBJECT {
    pthread_key_t key;
};

int _KOS_tls_create(_KOS_TLS_KEY *key)
{
    int          error   = KOS_SUCCESS;
    _KOS_TLS_KEY new_key = (_KOS_TLS_KEY)_KOS_malloc(sizeof(struct _KOS_TLS_OBJECT));

    if (new_key) {
        if (_KOS_seq_fail() || pthread_key_create(&new_key->key, 0)) {
            _KOS_free(new_key);
            error = KOS_ERROR_OUT_OF_MEMORY;
        }
        else
            *key = new_key;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void _KOS_tls_destroy(_KOS_TLS_KEY key)
{
    pthread_key_delete(key->key);
    _KOS_free(key);
}

void *_KOS_tls_get(_KOS_TLS_KEY key)
{
    return pthread_getspecific(key->key);
}

void _KOS_tls_set(_KOS_TLS_KEY key, void *value)
{
    pthread_setspecific(key->key, value);
}

#endif

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

#include "kos_threads.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
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

static const char str_err_thread[] = "failed to create thread";

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
    KOS_CONTEXT     *ctx;
    _KOS_THREAD_PROC proc;
    void            *cookie;
    KOS_OBJ_ID       exception;
};

static DWORD WINAPI _thread_proc(LPVOID thread_obj)
{
    KOS_THREAD_ROOT thread_root;

    if (KOS_context_register_thread(((_KOS_THREAD)thread_obj)->ctx, &thread_root) == KOS_SUCCESS)
        ((_KOS_THREAD)thread_obj)->proc(&thread_root.frame, ((_KOS_THREAD)thread_obj)->cookie);

    if (KOS_is_exception_pending(&thread_root.frame)) {
        ((_KOS_THREAD)thread_obj)->exception = KOS_get_exception(&thread_root.frame);
        return 1;
    }

    return 0;
}

int _KOS_thread_create(struct _KOS_STACK_FRAME *frame,
                       _KOS_THREAD_PROC         proc,
                       void                    *cookie,
                       _KOS_THREAD             *thread)
{
    int         error      = KOS_SUCCESS;
    _KOS_THREAD new_thread = (_KOS_THREAD)_KOS_malloc(sizeof(struct _KOS_THREAD_OBJECT));

    if (new_thread) {
        new_thread->ctx       = KOS_context_from_frame(frame);
        new_thread->proc      = proc;
        new_thread->cookie    = cookie;
        new_thread->exception = KOS_BADPTR;

        new_thread->thread_handle = CreateThread(0, 0, _thread_proc, new_thread, 0, 0);

        if (!new_thread->thread_handle) {
            _KOS_free(new_thread);
            KOS_raise_exception_cstring(frame, str_err_thread);
            error = KOS_ERROR_EXCEPTION;
        }
        else
            *thread = new_thread;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void _KOS_thread_join(struct _KOS_STACK_FRAME *frame,
                      _KOS_THREAD              thread)
{
    if (thread) {
        WaitForSingleObject(thread->thread_handle, INFINITE);
        CloseHandle(thread->thread_handle);

        if ( ! IS_BAD_PTR(thread->exception))
            KOS_raise_exception(frame, thread->exception);

        _KOS_free(thread);
    }
}

int _KOS_tls_create(_KOS_TLS_KEY *key)
{
    int         error   = KOS_SUCCESS;
    const DWORD new_key = TlsAlloc();

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
    KOS_CONTEXT     *ctx;
    _KOS_THREAD_PROC proc;
    void            *cookie;
};

static void *_thread_proc(void *thread_obj)
{
    KOS_THREAD_ROOT thread_root;

    if (KOS_context_register_thread(((_KOS_THREAD)thread_obj)->ctx, &thread_root) == KOS_SUCCESS)
        ((_KOS_THREAD)thread_obj)->proc(&thread_root.frame, ((_KOS_THREAD)thread_obj)->cookie);

    if (KOS_is_exception_pending(&thread_root.frame))
        return (void *)KOS_get_exception(&thread_root.frame);

    return 0;
}

int _KOS_thread_create(struct _KOS_STACK_FRAME *frame,
                       _KOS_THREAD_PROC         proc,
                       void                    *cookie,
                       _KOS_THREAD             *thread)
{
    int         error      = KOS_SUCCESS;
    _KOS_THREAD new_thread = (_KOS_THREAD)_KOS_malloc(sizeof(struct _KOS_THREAD_OBJECT));

    if (new_thread) {
        new_thread->ctx    = KOS_context_from_frame(frame);
        new_thread->proc   = proc;
        new_thread->cookie = cookie;

        if (pthread_create(&new_thread->thread_handle, 0, _thread_proc, new_thread)) {
            _KOS_free(new_thread);
            KOS_raise_exception_cstring(frame, str_err_thread);
            error = KOS_ERROR_EXCEPTION;
        }
        else
            *thread = new_thread;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

void _KOS_thread_join(struct _KOS_STACK_FRAME *frame,
                      _KOS_THREAD              thread)
{
    if (thread) {
        void *ret = 0;
        pthread_join(thread->thread_handle, &ret);

        if (ret)
            KOS_raise_exception(frame, (KOS_OBJ_ID)ret);

        _KOS_free(thread);
    }
}

struct _KOS_TLS_OBJECT {
    pthread_key_t key;
};

int _KOS_tls_create(_KOS_TLS_KEY *key)
{
    int          error   = KOS_SUCCESS;
    _KOS_TLS_KEY new_key = (_KOS_TLS_KEY)_KOS_malloc(sizeof(struct _KOS_TLS_OBJECT));

    if (new_key) {
        if (pthread_key_create(&new_key->key, 0)) {
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

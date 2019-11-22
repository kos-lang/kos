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

#include "kos_threads_internal.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_utils.h"
#include "kos_const_strings.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include <assert.h>

static const char str_err_join_self[] = "thread cannot join itself";
static const char str_err_thread[]    = "failed to create thread";

#if defined(CONFIG_THREADS) && (CONFIG_THREADS == 0)
uint32_t kos_atomic_swap_u32(uint32_t volatile *dest, uint32_t value)
{
    const uint32_t tmp = *dest;
    *dest = value;
    return tmp;
}

void *kos_atomic_swap_ptr(void *volatile *dest, void *value)
{
    void *const tmp = *dest;
    *dest = value;
    return tmp;
}
#elif defined(KOS_GCC_ATOMIC_EXTENSION)
int kos_atomic_cas_strong_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

int kos_atomic_cas_weak_u32(uint32_t volatile *dest, uint32_t oldv, uint32_t newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

int kos_atomic_cas_strong_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

int kos_atomic_cas_weak_ptr(void *volatile *dest, void *oldv, void *newv)
{
    return __atomic_compare_exchange_n(dest, &oldv, newv, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
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
            KOS_atomic_write_relaxed_ptr(*dest, KOS_atomic_read_relaxed_ptr(*src));
            --src;
            --dest;
        }
        while (src >= end);
    }
    else {
        KOS_ATOMIC(void *) *const end = src + ptr_count;

        do {
            KOS_atomic_write_relaxed_ptr(*dest, KOS_atomic_read_relaxed_ptr(*src));
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

#define KOS_NO_THREAD_IDX ~0U

static KOS_THREAD *alloc_thread(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  thread_func,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_INSTANCE  *inst        = ctx->inst;
    KOS_THREAD    *thread      = 0;
    const uint32_t max_threads = inst->threads.max_threads;
    uint32_t       i;

    thread = (KOS_THREAD *)kos_malloc(sizeof(KOS_THREAD));

    if ( ! thread) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return 0;
    }

    thread->inst        = inst;
    thread->thread_func = thread_func;
    thread->this_obj    = this_obj;
    thread->args_obj    = args_obj;
    thread->retval      = KOS_BADPTR;
    thread->exception   = KOS_BADPTR;
    thread->flags       = 0;
    thread->ref_count   = 1;
#ifdef _WIN32
    thread->thread_handle = 0;
    thread->thread_id     = 0;
#endif

    kos_lock_mutex(&inst->threads.new_mutex);

    if ( ! inst->threads.can_create)
        i = max_threads;

    else {
        for (i = 0; i < max_threads; i++) {

            KOS_atomic_write_release_u32(thread->thread_idx, i);

            if ( ! KOS_atomic_read_relaxed_ptr(inst->threads.threads[i])) {

                KOS_atomic_write_relaxed_ptr(inst->threads.threads[i], thread);

                KOS_atomic_add_u32(inst->threads.num_threads, 1);
                break;
            }

            KOS_atomic_write_relaxed_u32(thread->thread_idx, KOS_NO_THREAD_IDX);
        }
    }

    kos_unlock_mutex(&inst->threads.new_mutex);

    if (i >= max_threads) {

        KOS_DECLARE_STATIC_CONST_STRING(str_too_many_threads, "too many threads");

        KOS_raise_exception(ctx, KOS_CONST_ID(str_too_many_threads));

        kos_free(thread);
        thread = 0;
    }

    return thread;
}

static void set_thread_flags(KOS_THREAD *thread, uint32_t new_flags)
{
    assert(new_flags);

    for (;;) {
        const uint32_t flags = KOS_atomic_read_relaxed_u32(thread->flags);

        assert((flags & new_flags) == 0);

        if (KOS_atomic_cas_weak_u32(thread->flags, flags, flags | new_flags))
            break;
    }
}

static void release_thread(KOS_THREAD *thread)
{
    KOS_INSTANCE  *inst       = thread->inst;
    const uint32_t thread_idx = KOS_atomic_swap_u32(thread->thread_idx, KOS_NO_THREAD_IDX);

    if (thread_idx != KOS_NO_THREAD_IDX) {
        KOS_atomic_write_relaxed_ptr(inst->threads.threads[thread_idx], (KOS_THREAD *)0);

        KOS_atomic_add_u32(inst->threads.num_threads, (uint32_t)-1);
    }

    if (KOS_atomic_add_u32(thread->ref_count, (uint32_t)-1) == 1)
        kos_free(thread);
}

void kos_thread_add_ref(KOS_THREAD *thread)
{
    KOS_atomic_add_u32(thread->ref_count, 1);
}

void kos_thread_disown(KOS_THREAD *thread)
{
    set_thread_flags(thread, KOS_THREAD_DISOWNED);

    if (KOS_atomic_add_u32(thread->ref_count, (uint32_t)-1) == 1)
        kos_free(thread);
}

static void set_thread_exception(KOS_CONTEXT ctx, KOS_THREAD *thread)
{
    assert(KOS_is_exception_pending(ctx));

    KOS_atomic_write_relaxed_ptr(thread->exception, KOS_get_exception(ctx));
}

int kos_join_finished_threads(KOS_CONTEXT                      ctx,
                              enum KOS_THREAD_RELEASE_ACTION_E join_all)
{
    int            error        = KOS_SUCCESS;
    KOS_INSTANCE  *inst         = ctx->inst;
    const uint32_t max_threads  = inst->threads.max_threads;
    KOS_OBJ_ID     exception    = KOS_BADPTR;
    int            num_tracked  = 0;
    int            num_pending  = 0;
    uint32_t       num_finished = 0;
    int            join_rest    = 0;
    uint32_t       i            = 0;

    if (join_all) {
        kos_lock_mutex(&inst->threads.new_mutex);

        inst->threads.can_create = 0U;

        kos_unlock_mutex(&inst->threads.new_mutex);
    }

    if ( ! KOS_atomic_read_relaxed_u32(inst->threads.num_threads))
        return KOS_SUCCESS;

    while (i < max_threads) {

        KOS_THREAD *thread = 0;

        if (i == 0) {
            num_pending  = 0;
            num_finished = 0;
        }

        thread = (KOS_THREAD *)KOS_atomic_read_relaxed_ptr(inst->threads.threads[i]);

        if (thread) {

            const uint32_t flags = KOS_atomic_read_relaxed_u32(thread->flags);

            if (flags == (KOS_THREAD_DISOWNED | KOS_THREAD_FINISHED) ||
                     (join_all && flags == KOS_THREAD_DISOWNED) || join_rest) {

                if (KOS_atomic_cas_strong_u32(thread->flags, flags, flags | KOS_THREAD_JOINING)) {

                    const KOS_OBJ_ID retval = kos_thread_join(ctx, thread);

                    if (IS_BAD_PTR(retval)) {
                        assert(KOS_is_exception_pending(ctx));

                        if ( ! join_all) {
                            error = KOS_ERROR_EXCEPTION;
                            break;
                        }

                        /* We are joining all threads, so print the previous exception and continue */
                        if (IS_BAD_PTR(exception)) {
                            kos_track_refs(ctx, TRACK_ONE_REF, &exception);

                            exception = KOS_get_exception(ctx);
                            KOS_clear_exception(ctx);

                            assert(num_tracked == 0);
                            num_tracked = 1;
                        }
                        else {
                            const KOS_OBJ_ID prev_exception = exception;

                            exception = KOS_get_exception(ctx);
                            KOS_clear_exception(ctx);

                            KOS_raise_exception(ctx, prev_exception);
                            KOS_print_exception(ctx, KOS_STDERR);
                        }
                    }
                }
                else
                    ++num_pending;
            }
            else if ((flags & (KOS_THREAD_JOINED | KOS_THREAD_DISOWNED)) == 0) {
                if (flags == KOS_THREAD_FINISHED)
                    ++num_finished;
                ++num_pending;
            }
        }

        i++;

        if (join_all && i == max_threads && num_pending) {
            i = 0;

            KOS_suspend_context(ctx);
            kos_yield();
            KOS_resume_context(ctx);

            if (num_finished == KOS_atomic_read_relaxed_u32(inst->threads.num_threads))
                join_rest = 1;
        }
    }

    if (num_tracked)
        kos_untrack_refs(ctx, num_tracked);

    if ( ! error && ! IS_BAD_PTR(exception)) {
        KOS_raise_exception(ctx, exception);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

#ifdef _WIN32
static DWORD WINAPI thread_proc(LPVOID thread_ptr)
#else
static void *thread_proc(void *thread_ptr)
#endif
{
    struct KOS_THREAD_CONTEXT_S thread_ctx;
    KOS_THREAD                 *thread = (KOS_THREAD *)thread_ptr;
    KOS_OBJ_ID                  retval;

    if (KOS_instance_register_thread(thread->inst, &thread_ctx)) {

        set_thread_exception(&thread_ctx, thread);

        set_thread_flags(thread, KOS_THREAD_FINISHED);

        return 0;
    }

    retval = KOS_call_function(&thread_ctx,
                               thread->thread_func,
                               thread->this_obj,
                               thread->args_obj);

    if (IS_BAD_PTR(retval))
        set_thread_exception(&thread_ctx, thread);
    else {
        assert( ! KOS_is_exception_pending(&thread_ctx));

        thread->retval = retval;
    }

    set_thread_flags(thread, KOS_THREAD_FINISHED);

    KOS_instance_unregister_thread(thread_ctx.inst, &thread_ctx);

    return 0;
}

#ifdef _WIN32

KOS_THREAD *kos_thread_create(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  thread_func,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    KOS_THREAD *thread = alloc_thread(ctx, thread_func, this_obj, args_obj);

    if ( ! thread)
        return 0;

    thread->thread_handle = kos_seq_fail() ? 0 :
        CreateThread(0,
                     0,
                     thread_proc,
                     thread,
                     0,
                     &thread->thread_id);

    if ( ! thread->thread_handle) {
        KOS_raise_exception_cstring(ctx, str_err_thread);

        release_thread(thread);

        thread = 0;
    }

    return thread;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_THREAD *thread)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert(thread);

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    KOS_suspend_context(ctx);

    WaitForSingleObject(thread->thread_handle, INFINITE);
    CloseHandle(thread->thread_handle);

    error = KOS_resume_context(ctx);

    if ( ! error) {
        if (IS_BAD_PTR(thread->exception)) {
            retval = thread->retval;
            assert( ! IS_BAD_PTR(retval));
        }
        else {
            KOS_raise_exception(ctx, thread->exception);
            error = KOS_ERROR_EXCEPTION;
        }
    }

    set_thread_flags(thread, KOS_THREAD_JOINED);

    release_thread(thread);

cleanup:
    return error ? KOS_BADPTR : retval;
}

int kos_is_current_thread(KOS_THREAD *thread)
{
    assert(thread);

    return GetCurrentThreadId() == thread->thread_id ? 1 : 0;
}

struct KOS_MUTEX_OBJECT_S {
    CRITICAL_SECTION cs;
};

int kos_create_mutex(KOS_MUTEX *mutex)
{
    int error = KOS_SUCCESS;

    assert(mutex);
    *mutex = (KOS_MUTEX)kos_malloc(sizeof(struct KOS_MUTEX_OBJECT_S));

    if (*mutex) {
        __try {
            InitializeCriticalSection(&(*mutex)->cs);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            error = KOS_ERROR_EXCEPTION;
        }

    }
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

    __try {
        EnterCriticalSection(&(*mutex)->cs);
    }
    __except (EXCEPTION_CONTINUE_SEARCH) {
    }
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

KOS_THREAD *kos_thread_create(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  thread_func,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    KOS_THREAD *thread = alloc_thread(ctx, thread_func, this_obj, args_obj);

    if ( ! thread)
        return 0;

    if (kos_seq_fail() || pthread_create(&thread->thread_handle,
                                         0,
                                         thread_proc,
                                         thread)) {

        KOS_raise_exception_cstring(ctx, str_err_thread);

        release_thread(thread);

        thread = 0;
    }

    return thread;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_THREAD *thread)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert(thread);

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    KOS_suspend_context(ctx);

    pthread_join(thread->thread_handle, NULL);

    error = KOS_resume_context(ctx);

    if ( ! error) {
        if (IS_BAD_PTR(thread->exception)) {
            retval = thread->retval;
            assert( ! IS_BAD_PTR(retval));
        }
        else {
            KOS_raise_exception(ctx, thread->exception);
            error = KOS_ERROR_EXCEPTION;
        }
    }

    set_thread_flags(thread, KOS_THREAD_JOINED);

    release_thread(thread);

cleanup:
    return error ? KOS_BADPTR : retval;
}

int kos_is_current_thread(KOS_THREAD *thread)
{
    assert(thread);

    return pthread_equal(pthread_self(), thread->thread_handle);
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

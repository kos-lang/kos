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
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include <assert.h>

static const char str_err_join_self[] = "thread cannot join itself";
static const char str_err_thread[]    = "failed to create thread";

#ifdef KOS_GCC_ATOMIC_EXTENSION
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

static KOS_OBJ_ID alloc_thread(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  thread_func,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_INSTANCE *inst       = ctx->inst;
    KOS_THREAD   *thread;
    KOS_OBJ_ID    thread_obj = KOS_BADPTR;
    int           pushed     = 0;
    int           error      = KOS_SUCCESS;
    int           i;

    if (KOS_push_locals(ctx, &pushed, 4, &thread_func, &this_obj, &args_obj, &thread_obj))
        return KOS_BADPTR;

    thread = (KOS_THREAD *)kos_alloc_object(ctx, OBJ_THREAD, sizeof(KOS_THREAD));

    if ( ! thread) {
        KOS_pop_locals(ctx, pushed);
        return KOS_BADPTR;
    }

    assert(kos_get_object_type(thread->header) == OBJ_THREAD);

    thread->inst        = inst;
    thread->thread_func = thread_func;
    thread->this_obj    = this_obj;
    thread->args_obj    = args_obj;
    thread->retval      = KOS_BADPTR;
    thread->exception   = KOS_BADPTR;
    thread->thread_idx  = ~0U;
    thread->flags       = 0;
    thread_obj          = OBJID(THREAD, thread);
#ifdef _WIN32
    thread->thread_handle = 0;
    thread->thread_id     = 0;
#endif

    if (IS_BAD_PTR(KOS_atomic_read_relaxed_obj(inst->threads.threads))) {
        KOS_OBJ_ID new_threads = KOS_new_array(ctx, 8);

        if (IS_BAD_PTR(new_threads))
            error = KOS_ERROR_EXCEPTION;
        else
            /* Attempt to write the new thread array, ignore failure */
            (void)KOS_atomic_cas_strong_ptr(inst->threads.threads, KOS_BADPTR, new_threads);
    }

    for (i = 0; ! error; ) {
        const int size        = (int)KOS_get_array_size(inst->threads.threads);
        const int num_threads = (int)KOS_atomic_read_relaxed_u32(inst->threads.num_threads);

        if (num_threads >= size) {
            error = KOS_array_resize(ctx, inst->threads.threads, size * 2U);
            if (error)
                break;
        }

        for ( ; i < size; ++i) {
            const KOS_OBJ_ID cur = KOS_array_read(ctx, inst->threads.threads, i);

            if (cur == KOS_VOID)
                break;
        }

        if (i < size) {

            KOS_OBJ_ID prev;

            OBJPTR(THREAD, thread_obj)->thread_idx = (uint32_t)i;

            prev = KOS_array_cas(ctx, inst->threads.threads, i, KOS_VOID, thread_obj);

            if (prev == KOS_VOID) {
                KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t) *)&inst->threads.num_threads, 1);
                break;
            }

            OBJPTR(THREAD, thread_obj)->thread_idx = ~0U;

            if (IS_BAD_PTR(prev))
                error = KOS_ERROR_EXCEPTION;
        }

        ++i;

        if (i >= size)
            i = 0;
    }

    KOS_pop_locals(ctx, pushed);

    if ( ! error) {
        assert(OBJPTR(THREAD, thread_obj)->thread_idx != ~0U);
    }
    else {
        assert(OBJPTR(THREAD, thread_obj)->thread_idx == ~0U);
    }

    return error ? KOS_BADPTR : thread_obj;
}

static void set_thread_flags(KOS_OBJ_ID thread, uint32_t new_flags)
{
    assert(new_flags);

    for (;;) {
        const uint32_t flags = KOS_atomic_read_relaxed_u32(OBJPTR(THREAD, thread)->flags);

        assert((flags & new_flags) == 0);

        if (KOS_atomic_cas_weak_u32(OBJPTR(THREAD, thread)->flags, flags, flags | new_flags))
            break;
    }
}

static void release_thread(KOS_CONTEXT ctx, KOS_OBJ_ID thread)
{
    KOS_INSTANCE *inst = ctx->inst;
    uint32_t      thread_idx;

    assert(GET_OBJ_TYPE(thread) == OBJ_THREAD);

    thread_idx = OBJPTR(THREAD, thread)->thread_idx;

    assert(thread_idx != ~0U);

    OBJPTR(THREAD, thread)->thread_idx = ~0U;

    set_thread_flags(thread, KOS_THREAD_JOINED);

    (void)KOS_array_write(ctx, inst->threads.threads, thread_idx, KOS_VOID);

    KOS_atomic_add_i32(*(KOS_ATOMIC(int32_t) *)&inst->threads.num_threads, -1);
}

void kos_thread_disown(KOS_OBJ_ID thread)
{
    set_thread_flags(thread, KOS_THREAD_DISOWNED);
}

typedef struct THREAD_INITIALIZER_S {
    KOS_INSTANCE *inst;
    uint32_t      thread_idx;
} THREAD_INIT;

static KOS_OBJ_ID get_thread_obj(void *init_ptr)
{
    KOS_INSTANCE *const inst  = ((THREAD_INIT *)init_ptr)->inst;
    const uint32_t      idx   = ((THREAD_INIT *)init_ptr)->thread_idx;
    KOS_OBJ_ID          array = KOS_atomic_read_relaxed_obj(inst->threads.threads);
    KOS_OBJ_ID          thread_obj;

    assert( ! IS_BAD_PTR(array));
    assert(idx != ~0U);

    array = kos_get_array_storage(array);
    assert(GET_OBJ_TYPE(array) == OBJ_ARRAY_STORAGE);

    thread_obj = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, array)->buf[idx]);

    assert(GET_OBJ_TYPE(thread_obj) == OBJ_THREAD);

    return thread_obj;
}

static void set_thread_exception(KOS_CONTEXT ctx, KOS_OBJ_ID thread)
{
    assert(GET_OBJ_TYPE(thread) == OBJ_THREAD);

    assert(KOS_is_exception_pending(ctx));

    KOS_atomic_write_relaxed_ptr(OBJPTR(THREAD, thread)->exception, KOS_get_exception(ctx));
}

int kos_join_finished_threads(KOS_CONTEXT                      ctx,
                              enum KOS_THREAD_RELEASE_ACTION_E action)
{
    int           error        = KOS_SUCCESS;
    KOS_INSTANCE *inst         = ctx->inst;
    uint32_t      num_threads  = IS_BAD_PTR(inst->threads.threads) ? 0U : KOS_get_array_size(inst->threads.threads);
    KOS_OBJ_ID    thread       = KOS_BADPTR;
    KOS_OBJ_ID    exception    = KOS_BADPTR;
    int           num_tracked  = 1;
    int           num_pending  = 0;
    uint32_t      num_finished = 0;
    int           join_rest    = 0;
    uint32_t      i            = 0;

    kos_track_refs(ctx, TRACK_ONE_REF, &thread);

    while (i < num_threads) {

        if (i == 0) {
            num_pending  = 0;
            num_finished = 0;
        }

        thread = KOS_array_read(ctx, inst->threads.threads, i);

        assert( ! IS_BAD_PTR(thread));

        if (thread != KOS_VOID) {

            const uint32_t flags = KOS_atomic_read_relaxed_u32(OBJPTR(THREAD, thread)->flags);

            if (flags == (KOS_THREAD_DISOWNED | KOS_THREAD_FINISHED) ||
                     (action && flags == KOS_THREAD_DISOWNED) || join_rest) {

                if (KOS_atomic_cas_strong_u32(OBJPTR(THREAD, thread)->flags, flags, flags | KOS_THREAD_JOINING)) {

                    const KOS_OBJ_ID retval = kos_thread_join(ctx, thread);

                    if (IS_BAD_PTR(retval)) {
                        assert(KOS_is_exception_pending(ctx));

                        if ( ! action) {
                            error = KOS_ERROR_EXCEPTION;
                            break;
                        }

                        /* We are joining all threads, so print the previous exception and continue */
                        if (IS_BAD_PTR(exception)) {
                            kos_track_refs(ctx, TRACK_ONE_REF, &exception);

                            exception = KOS_get_exception(ctx);
                            KOS_clear_exception(ctx);

                            assert(num_tracked == 1);
                            num_tracked = 2;
                        }
                        else {
                            const KOS_OBJ_ID prev_exception = exception;

                            exception = KOS_get_exception(ctx);
                            KOS_clear_exception(ctx);

                            KOS_raise_exception(ctx, prev_exception);
                            KOS_print_exception(ctx);
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

        if (action && i == num_threads && num_pending) {
            i = 0;
            kos_yield();
            KOS_help_gc(ctx);
            num_threads = IS_BAD_PTR(inst->threads.threads) ? 0U : KOS_get_array_size(inst->threads.threads);
            if (num_finished == KOS_atomic_read_relaxed_u32(inst->threads.num_threads))
                join_rest = 1;
        }
    }

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
    KOS_OBJ_ID                  thread_obj = KOS_BADPTR;

    if (KOS_instance_register_thread(((THREAD_INIT *)thread_ptr)->inst, &thread_ctx)) {

        thread_obj = get_thread_obj(thread_ptr);

        set_thread_exception(&thread_ctx, thread_obj);

        set_thread_flags(thread_obj, KOS_THREAD_FINISHED);

        kos_free(thread_ptr);

        return 0;
    }

    {
        int pushed = 0;

        if (KOS_push_locals(&thread_ctx, &pushed, 1, &thread_obj)) {

            thread_obj = get_thread_obj(thread_ptr);

            set_thread_exception(&thread_ctx, thread_obj);

            set_thread_flags(thread_obj, KOS_THREAD_FINISHED);

            kos_free(thread_ptr);

            return 0;
        }
    }

    thread_obj = get_thread_obj(thread_ptr);

    kos_free(thread_ptr);

    {
        KOS_OBJ_ID retval;

        retval = KOS_call_function(&thread_ctx,
                                   OBJPTR(THREAD, thread_obj)->thread_func,
                                   OBJPTR(THREAD, thread_obj)->this_obj,
                                   OBJPTR(THREAD, thread_obj)->args_obj);

        if (IS_BAD_PTR(retval))
            set_thread_exception(&thread_ctx, thread_obj);
        else {
            assert( ! KOS_is_exception_pending(&thread_ctx));

            OBJPTR(THREAD, thread_obj)->retval = retval;
        }
    }

    set_thread_flags(thread_obj, KOS_THREAD_FINISHED);

    KOS_instance_unregister_thread(thread_ctx.inst, &thread_ctx);

    return 0;
}

#ifdef _WIN32

#define THREAD_PTR(void_ptr) OBJPTR(THREAD, ((THREAD_INIT *)(void_ptr))->thread_obj)

KOS_OBJ_ID kos_thread_create(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  thread_func,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int          error      = KOS_SUCCESS;
    KOS_OBJ_ID   thread_obj = KOS_BADPTR;
    THREAD_INIT *init;

    init = (THREAD_INIT *)kos_malloc(sizeof(THREAD_INIT));

    if ( ! init) {
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    init->inst = ctx->inst;

    thread_obj = alloc_thread(ctx, thread_func, this_obj, args_obj);
    TRY_OBJID(thread_obj);

    init->thread_idx = OBJPTR(THREAD, thread_obj)->thread_idx;

    OBJPTR(THREAD, thread_obj)->thread_handle = kos_seq_fail() ? 0 :
        CreateThread(0,
                     0,
                     thread_proc,
                     init,
                     0,
                     &OBJPTR(THREAD, thread_obj)->thread_id);

    if ( ! OBJPTR(THREAD, thread_obj)->thread_handle)
        RAISE_EXCEPTION(str_err_thread);

cleanup:
    if (error) {
        kos_free(init);

        if ( ! IS_BAD_PTR(thread_obj))
            release_thread(ctx, thread_obj);

        thread_obj = KOS_BADPTR;
    }

    return thread_obj;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  thread)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID retval = KOS_BADPTR;
    HANDLE     handle;

    assert( ! IS_BAD_PTR(thread));

    kos_track_refs(ctx, TRACK_ONE_REF, &thread);

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    handle = OBJPTR(THREAD, thread)->thread_handle;

    KOS_suspend_context(ctx);

    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);

    error = KOS_resume_context(ctx);

    if ( ! error) {
        if (IS_BAD_PTR(OBJPTR(THREAD, thread)->exception)) {
            retval = OBJPTR(THREAD, thread)->retval;
            assert( ! IS_BAD_PTR(retval));
        }
        else {
            KOS_raise_exception(ctx, OBJPTR(THREAD, thread)->exception);
            error = KOS_ERROR_EXCEPTION;
        }
    }

    release_thread(ctx, thread);

cleanup:
    kos_untrack_refs(ctx, 1);

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

KOS_OBJ_ID kos_thread_create(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  thread_func,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int          error      = KOS_SUCCESS;
    KOS_OBJ_ID   thread_obj = KOS_BADPTR;
    THREAD_INIT *init;

    init = (THREAD_INIT *)kos_malloc(sizeof(THREAD_INIT));

    if ( ! init) {
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    init->inst = ctx->inst;

    thread_obj = alloc_thread(ctx, thread_func, this_obj, args_obj);
    TRY_OBJID(thread_obj);

    init->thread_idx = OBJPTR(THREAD, thread_obj)->thread_idx;

    if (kos_seq_fail() || pthread_create(&OBJPTR(THREAD, thread_obj)->thread_handle,
                                         0,
                                         thread_proc,
                                         init))
        RAISE_EXCEPTION(str_err_thread);

cleanup:
    if (error) {
        kos_free(init);

        if ( ! IS_BAD_PTR(thread_obj))
            release_thread(ctx, thread_obj);

        thread_obj = KOS_BADPTR;
    }

    return thread_obj;
}

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  thread)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID retval = KOS_BADPTR;
    pthread_t  handle;

    assert( ! IS_BAD_PTR(thread));

#ifdef CONFIG_MAD_GC
    kos_track_refs(ctx, 0x1234, &thread);
#else
    kos_track_refs(ctx, 1, &thread);
#endif

    if (kos_is_current_thread(thread))
        RAISE_EXCEPTION(str_err_join_self);

    handle = OBJPTR(THREAD, thread)->thread_handle;

    KOS_suspend_context(ctx);

    pthread_join(handle, NULL);

    error = KOS_resume_context(ctx);

    if ( ! error) {
        if (IS_BAD_PTR(OBJPTR(THREAD, thread)->exception)) {
            retval = OBJPTR(THREAD, thread)->retval;
            assert( ! IS_BAD_PTR(retval));
        }
        else {
            KOS_raise_exception(ctx, OBJPTR(THREAD, thread)->exception);
            error = KOS_ERROR_EXCEPTION;
        }
    }

    release_thread(ctx, thread);

cleanup:
    kos_untrack_refs(ctx, 1);

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

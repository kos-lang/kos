/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_threads.h"
#include "../inc/kos_utils.h"
#include "../core/kos_debug.h"
#include "../core/kos_misc.h"
#include "../core/kos_try.h"

#define KOS_MAX_SEM 0x7FFFFFFF

KOS_DECLARE_STATIC_CONST_STRING(str_count,               "count");
KOS_DECLARE_STATIC_CONST_STRING(str_err_bad_module,      "failed to get private data from module thread");
KOS_DECLARE_STATIC_CONST_STRING(str_err_cond_var_failed, "failed to create a condition variable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_count_too_small, "count argument is less than 1");
KOS_DECLARE_STATIC_CONST_STRING(str_err_count_too_large, "count argument exceeds 0x7FFFFFFF");
KOS_DECLARE_STATIC_CONST_STRING(str_err_mutex_failed,    "failed to create a mutex");
KOS_DECLARE_STATIC_CONST_STRING(str_err_init_too_large,  "init argument exceeds 0x7FFFFFFF");
KOS_DECLARE_STATIC_CONST_STRING(str_err_init_too_small,  "init argument is less than 0");
KOS_DECLARE_STATIC_CONST_STRING(str_init,                "init");

KOS_DECLARE_PRIVATE_CLASS(mutex_priv_class);

static void mutex_finalize(KOS_CONTEXT ctx,
                           void       *priv)
{
    if (priv)
        kos_destroy_mutex((KOS_MUTEX *)&priv);
}

/* @item threads mutex()
 *
 *     mutex()
 *
 * Mutex object class.
 *
 * Mutex objects are best used with the `with` statement.
 */
static KOS_OBJ_ID mutex_ctor(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL  mutex;
    KOS_OBJ_ID proto;
    KOS_MUTEX  mutex_obj = KOS_NULL;
    int        error     = KOS_SUCCESS;

    KOS_init_local(ctx, &mutex);

    error = kos_create_mutex(&mutex_obj);

    if (error)
        RAISE_EXCEPTION_STR(str_err_mutex_failed);

    proto = KOS_get_module(ctx);
    TRY_OBJID(proto);

    proto = KOS_atomic_read_relaxed_obj(OBJPTR(MODULE, proto)->priv);
    if (IS_BAD_PTR(proto) || kos_seq_fail())
        RAISE_EXCEPTION_STR(str_err_bad_module);

    proto = KOS_array_read(ctx, proto, 0);
    TRY_OBJID(proto);

    mutex.o = KOS_new_object_with_private(ctx,
                                          proto,
                                          &mutex_priv_class,
                                          mutex_finalize);
    TRY_OBJID(mutex.o);

    KOS_object_set_private_ptr(mutex.o, mutex_obj);
    mutex_obj = KOS_NULL;

cleanup:
    if (mutex_obj)
        kos_destroy_mutex(&mutex_obj);

    mutex.o = KOS_destroy_top_local(ctx, &mutex);

    return error ? KOS_BADPTR : mutex.o;
}

/* @item threads mutex.prototype.acquire()
 *
 *     mutex.prototype.acquire()
 *
 * Locks the mutex object.
 *
 * If the mutex is already locked by another thread, this function will wait
 * until it is unlocked.
 *
 * Returns `this` mutex object.
 */
static KOS_OBJ_ID mutex_acquire(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL mutex;
    KOS_MUTEX mutex_obj;

    KOS_init_local_with(ctx, &mutex, this_obj);

    mutex_obj = (KOS_MUTEX)KOS_object_get_private(mutex.o, &mutex_priv_class);

    if (mutex_obj) {
        KOS_suspend_context(ctx);

        kos_lock_mutex(mutex_obj);

        KOS_resume_context(ctx);
    }

    return KOS_destroy_top_local(ctx, &mutex);
}

/* @item threads mutex.prototype.release()
 *
 *     mutex.prototype.release()
 *
 * Unlocks the mutex object, if it is held by the current thread.
 *
 * If the mutex is not held by the current thread, this function does nothing.
 *
 * Returns `this` mutex object.
 */
static KOS_OBJ_ID mutex_release(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    const KOS_MUTEX mutex_obj = (KOS_MUTEX)KOS_object_get_private(this_obj, &mutex_priv_class);

    if (mutex_obj)
        kos_unlock_mutex(mutex_obj);

    return this_obj;
}

typedef struct KOS_SEMAPHORE_S {
    KOS_MUTEX            mutex;
    KOS_COND_VAR         cond_var;
    KOS_ATOMIC(uint32_t) value;
} KOS_SEMAPHORE;

KOS_DECLARE_PRIVATE_CLASS(semaphore_priv_class);

static void semaphore_finalize(KOS_CONTEXT ctx,
                               void       *priv)
{
    if (priv) {
        KOS_SEMAPHORE *const sem = (KOS_SEMAPHORE *)priv;

        if (sem->mutex)
            kos_destroy_mutex(&sem->mutex);

        if (sem->cond_var)
            kos_destroy_cond_var(&sem->cond_var);

        KOS_free(sem);
    }
}

static const KOS_CONVERT sem_args[2] = {
    KOS_DEFINE_OPTIONAL_ARG(str_init, TO_SMALL_INT(0)),
    KOS_DEFINE_TAIL_ARG()
};

/* @item threads semaphore()
 *
 *     semaphore(init = 0)
 *
 * Semaphore object class.
 *
 * A semaphore is an integer number which can be incremented (release)
 * or decremented (acquire).  If an `acquire()` function is called on
 * a semaphore which has a zero-value, the function will block until
 * another thread increments the semaphore.
 *
 * `init` is the initial integer value for the new semaphore object.
 *
 * Semaphore objects can be used with the `with` statement.
 */
static KOS_OBJ_ID semaphore_ctor(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL      semaphore;
    KOS_OBJ_ID     proto;
    KOS_SEMAPHORE *sem   = KOS_NULL;
    int            error = KOS_SUCCESS;

    assert(KOS_get_array_size(args_obj) >= 1);

    KOS_init_local(ctx, &semaphore);

    sem = (KOS_SEMAPHORE *)KOS_malloc(sizeof(KOS_SEMAPHORE));

    if ( ! sem) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        goto cleanup;
    }

    sem->mutex    = KOS_NULL;
    sem->cond_var = KOS_NULL;

    {
        int64_t value64;

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &value64));

        if (value64 < 0)
            RAISE_EXCEPTION_STR(str_err_init_too_small);

        if (value64 > KOS_MAX_SEM)
            RAISE_EXCEPTION_STR(str_err_init_too_large);

        KOS_atomic_write_relaxed_u32(sem->value, (uint32_t)value64);
    }

    error = kos_create_mutex(&sem->mutex);

    if (error)
        RAISE_EXCEPTION_STR(str_err_mutex_failed);

    error = kos_create_cond_var(&sem->cond_var);

    if (error)
        RAISE_EXCEPTION_STR(str_err_cond_var_failed);

    proto = KOS_get_module(ctx);
    TRY_OBJID(proto);

    proto = KOS_atomic_read_relaxed_obj(OBJPTR(MODULE, proto)->priv);
    if (IS_BAD_PTR(proto) || kos_seq_fail())
        RAISE_EXCEPTION_STR(str_err_bad_module);

    proto = KOS_array_read(ctx, proto, 1);
    TRY_OBJID(proto);

    semaphore.o = KOS_new_object_with_private(ctx,
                                              proto,
                                              &semaphore_priv_class,
                                              semaphore_finalize);
    TRY_OBJID(semaphore.o);

    KOS_object_set_private_ptr(semaphore.o, sem);
    sem = KOS_NULL;

cleanup:
    if (sem)
        semaphore_finalize(ctx, sem);

    semaphore.o = KOS_destroy_top_local(ctx, &semaphore);

    return error ? KOS_BADPTR : semaphore.o;
}

static const KOS_CONVERT count_arg[2] = {
    { KOS_CONST_ID(str_count), TO_SMALL_INT(1), 0, 0, KOS_NATIVE_INT64 },
    KOS_DEFINE_TAIL_ARG()
};

static int get_count_arg(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  args_obj,
                         uint32_t   *count)
{
    int64_t count64 = 0;
    int     error;

    error = KOS_extract_native_from_array(ctx, args_obj, "argument", count_arg, KOS_NULL, &count64);
    if (error)
        return error;

    if (count64 < 1) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_count_too_small));
        return KOS_ERROR_EXCEPTION;
    }

    if (count64 > KOS_MAX_SEM) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_count_too_large));
        return KOS_ERROR_EXCEPTION;
    }

    *count = (uint32_t)(uint64_t)count64;

    return KOS_SUCCESS;
}

/* @item threads semaphore.prototype.acquire()
 *
 *     semaphore.prototype.acquire(count = 1)
 *
 * Subtracts `count` from the semaphore value.
 *
 * `count` defaults to 1.  If `count` is less than 1 or greater than 0x7FFFFFFF
 * throws an exception.
 *
 * If the semaphore value is already 0, blocks until another thread increments it,
 * then performs the decrement operation.  This is repeated until the value has
 * been decremented `count` times.  The decrement operation is non-atomic meaning
 * that if two threads are trying to acquire with `count > 1`, each of them could
 * decrement the value by 1 multiple times.
 *
 * Returns `this` semaphore object.
 */
static KOS_OBJ_ID semaphore_acquire(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL      semaphore;
    KOS_SEMAPHORE *sem   = KOS_NULL;
    uint32_t       count = 0;

    KOS_init_local_with(ctx, &semaphore, this_obj);

    sem = (KOS_SEMAPHORE *)KOS_object_get_private(semaphore.o, &semaphore_priv_class);

    if (get_count_arg(ctx, args_obj, &count)) {
        KOS_destroy_top_local(ctx, &semaphore);
        return KOS_BADPTR;
    }

    if (sem) {

        int suspended = 0;;

        do {
            const uint32_t old_value = KOS_atomic_read_relaxed_u32(sem->value);
            const uint32_t dec_value = (old_value > count) ? count : old_value;

            if ( ! dec_value) {

                if ( ! suspended) {
                    KOS_suspend_context(ctx);

                    kos_lock_mutex(sem->mutex);

                    suspended = 1;
                }

                kos_wait_cond_var(sem->cond_var, sem->mutex);

                continue;
            }

            if ( ! KOS_atomic_cas_weak_u32(sem->value, old_value, old_value - dec_value))
                continue;

            count -= dec_value;
        } while (count);

        if (suspended) {
            kos_unlock_mutex(sem->mutex);

            KOS_resume_context(ctx);
        }
    }

    return KOS_destroy_top_local(ctx, &semaphore);
}

/* @item threads semaphore.prototype.release()
 *
 *     semaphore.prototype.release()
 *
 * Increments the semaphore value and signals other threads that may be
 * waiting on `acquire()`.
 *
 * Returns `this` semaphore object.
 */
static KOS_OBJ_ID semaphore_release(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL      semaphore;
    KOS_SEMAPHORE *sem   = KOS_NULL;
    uint32_t       count = 0;

    KOS_init_local_with(ctx, &semaphore, this_obj);

    sem = (KOS_SEMAPHORE *)KOS_object_get_private(semaphore.o, &semaphore_priv_class);

    if (get_count_arg(ctx, args_obj, &count)) {
        KOS_destroy_top_local(ctx, &semaphore);
        return KOS_BADPTR;
    }

    if (sem) {
        uint32_t old_value;

        do {
            uint32_t max_inc;

            old_value = KOS_atomic_read_relaxed_u32(sem->value);
            max_inc   = (uint32_t)KOS_MAX_SEM - old_value;

            if (count > max_inc) {
                KOS_destroy_top_local(ctx, &semaphore);

                KOS_raise_printf(ctx, "semaphore value %u cannot be increased by %u",
                                 old_value, count);
                return KOS_BADPTR;
            }

        } while ( ! KOS_atomic_cas_weak_u32(sem->value, old_value, old_value + count));

        kos_signal_cond_var(sem->cond_var);
    }

    return KOS_destroy_top_local(ctx, &semaphore);
}

static KOS_OBJ_ID semaphore_value(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_SEMAPHORE *const sem = (KOS_SEMAPHORE *)KOS_object_get_private(this_obj, &semaphore_priv_class);

    if (sem) {
        const uint32_t value = KOS_atomic_read_relaxed_u32(sem->value);

        return KOS_new_int(ctx, (int64_t)value);
    }

    return KOS_VOID;
}

int kos_module_threads_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL priv;
    KOS_LOCAL mutex_proto;
    KOS_LOCAL semaphore_proto;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_locals(    ctx, 3, &priv, &mutex_proto, &semaphore_proto);

    priv.o = KOS_new_array(ctx, 2);
    TRY_OBJID(priv.o);

    KOS_atomic_write_relaxed_ptr(OBJPTR(MODULE, module.o)->priv, priv.o);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,                    "mutex",     mutex_ctor,        KOS_NULL, &mutex_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, mutex_proto.o,     "acquire",   mutex_acquire,     KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, mutex_proto.o,     "release",   mutex_release,     KOS_NULL);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,                    "semaphore", semaphore_ctor,    sem_args, &semaphore_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, semaphore_proto.o, "acquire",   semaphore_acquire, count_arg);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, semaphore_proto.o, "release",   semaphore_release, count_arg);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, semaphore_proto.o, "value",     semaphore_value,   KOS_NULL);

    TRY(KOS_array_write(ctx, priv.o, 0, mutex_proto.o));
    TRY(KOS_array_write(ctx, priv.o, 1, semaphore_proto.o));

cleanup:
    KOS_destroy_top_locals(ctx, &priv, &module);

    return error;
}

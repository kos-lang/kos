/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_THREADS_H_INCLUDED
#define KOS_THREADS_H_INCLUDED

#include "kos_entity.h"

typedef struct KOS_MUTEX_OBJECT_S    *KOS_MUTEX;
typedef struct KOS_COND_VAR_OBJECT_S *KOS_COND_VAR;

#ifdef _WIN32
typedef uint32_t KOS_TLS_KEY;
#else
typedef struct KOS_TLS_OBJECT_S *KOS_TLS_KEY;
#endif

typedef struct KOS_THREAD_S KOS_THREAD;

void kos_yield(void);

KOS_THREAD *kos_thread_create(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  thread_func,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj);

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_THREAD *thread);

void kos_thread_add_ref(KOS_THREAD *thread);

void kos_thread_disown(KOS_THREAD *thread);

int kos_is_current_thread(KOS_THREAD *thread);

int kos_create_mutex(KOS_MUTEX *mutex);
void kos_destroy_mutex(KOS_MUTEX *mutex);
void kos_lock_mutex(KOS_MUTEX mutex);
void kos_unlock_mutex(KOS_MUTEX mutex);

int kos_create_cond_var(KOS_COND_VAR *cond_var);
void kos_destroy_cond_var(KOS_COND_VAR *cond_var);
void kos_signal_cond_var(KOS_COND_VAR cond_var);
void kos_broadcast_cond_var(KOS_COND_VAR cond_var);
void kos_wait_cond_var(KOS_COND_VAR cond_var, KOS_MUTEX mutex);

int   kos_tls_create(KOS_TLS_KEY *key);
void  kos_tls_destroy(KOS_TLS_KEY key);
void *kos_tls_get(KOS_TLS_KEY key);
void  kos_tls_set(KOS_TLS_KEY key, void *value);

#endif

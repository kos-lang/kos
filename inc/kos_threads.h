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

#ifndef KOS_THREADS_H_INCLUDED
#define KOS_THREADS_H_INCLUDED

#include "kos_object_base.h"

struct KOS_MUTEX_OBJECT_S;
typedef struct KOS_MUTEX_OBJECT_S *KOS_MUTEX;

#ifdef _WIN32
typedef uint32_t KOS_TLS_KEY;
#else
struct KOS_TLS_OBJECT_S;
typedef struct KOS_TLS_OBJECT_S *KOS_TLS_KEY;
#endif

void kos_yield(void);

KOS_OBJ_ID kos_thread_create(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  thread_func,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj);

KOS_OBJ_ID kos_thread_join(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  thread);

int kos_is_current_thread(KOS_OBJ_ID thread);

int kos_create_mutex(KOS_MUTEX *mutex);
void kos_destroy_mutex(KOS_MUTEX *mutex);
void kos_lock_mutex(KOS_MUTEX *mutex);
void kos_unlock_mutex(KOS_MUTEX *mutex);

int   kos_tls_create(KOS_TLS_KEY *key);
void  kos_tls_destroy(KOS_TLS_KEY key);
void *kos_tls_get(KOS_TLS_KEY key);
void  kos_tls_set(KOS_TLS_KEY key, void *value);

#endif

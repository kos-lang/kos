/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_THREAD_INTERNAL_H_INCLUDED
#define KOS_THREAD_INTERNAL_H_INCLUDED

#include "../inc/kos_threads.h"

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

struct KOS_THREAD_S {
    KOS_INSTANCE          *inst;
    KOS_OBJ_ID             thread_func;
    KOS_OBJ_ID             this_obj;
    KOS_OBJ_ID             args_obj;
    KOS_ATOMIC(KOS_OBJ_ID) retval;
    KOS_ATOMIC(KOS_OBJ_ID) exception;
#ifdef _WIN32
    HANDLE                 thread_handle;
    DWORD                  thread_id;
#else
    pthread_t              thread_handle;
#endif
    KOS_ATOMIC(uint32_t)   thread_idx; /* Index to the threads array in instance */
    KOS_ATOMIC(uint32_t)   flags;
    KOS_ATOMIC(uint32_t)   ref_count;
};

enum KOS_THREAD_FLAGS_E {
    KOS_THREAD_DISOWNED = 1,
    KOS_THREAD_FINISHED = 2,
    KOS_THREAD_JOINING  = 4,
    KOS_THREAD_JOINED   = 8
};

enum KOS_THREAD_RELEASE_ACTION_E {
    KOS_ONLY_DISOWNED,
    KOS_JOIN_ALL
};

int kos_join_finished_threads(KOS_CONTEXT                      ctx,
                              enum KOS_THREAD_RELEASE_ACTION_E action);

#endif

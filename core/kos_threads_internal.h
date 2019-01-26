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

typedef struct KOS_THREAD_S {
    KOS_OBJ_HEADER         header;
    KOS_INSTANCE          *inst;
    KOS_OBJ_ID             thread_func;
    KOS_OBJ_ID             this_obj;
    KOS_OBJ_ID             args_obj;
    KOS_OBJ_ID             retval;
    KOS_ATOMIC(KOS_OBJ_ID) exception;
#ifdef _WIN32
    HANDLE                 thread_handle;
    DWORD                  thread_id;
#else
    pthread_t              thread_handle;
#endif
    unsigned               thread_idx; /* Index to the threads array in instance */
} KOS_THREAD;

#endif

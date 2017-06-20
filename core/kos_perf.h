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

#ifndef __KOS_PERF_H
#define __KOS_PERF_H

#include "../inc/kos_threads.h"

#ifdef CONFIG_PERF
#   define KOS_PERF_CNT(stat)            KOS_atomic_add_i32(_kos_perf.stat, 1)
#   define KOS_PERF_CNT_ARRAY(stat, idx) KOS_atomic_add_i32(_kos_perf.stat[(idx)-3], 1)
#   define KOS_PERF_ADD(stat, num)       KOS_atomic_add_i32(_kos_perf.stat, (num))

struct _KOS_PERF {
    KOS_ATOMIC(uint32_t) object_get_success;
    KOS_ATOMIC(uint32_t) object_get_fail;
    KOS_ATOMIC(uint32_t) object_set_success;
    KOS_ATOMIC(uint32_t) object_set_fail;
    KOS_ATOMIC(uint32_t) object_delete_success;
    KOS_ATOMIC(uint32_t) object_delete_fail;
    KOS_ATOMIC(uint32_t) object_resize_success;
    KOS_ATOMIC(uint32_t) object_resize_fail;
    KOS_ATOMIC(uint32_t) object_salvage_success;
    KOS_ATOMIC(uint32_t) object_salvage_fail;

    KOS_ATOMIC(uint32_t) array_salvage_success;
    KOS_ATOMIC(uint32_t) array_salvage_fail;

    KOS_ATOMIC(uint32_t) alloc_object[5];
    KOS_ATOMIC(uint32_t) alloc_buffer;
    KOS_ATOMIC(uint32_t) alloc_buffer_total;
};

extern struct _KOS_PERF _kos_perf;

#else
#   define KOS_PERF_CNT(stat)            (void)0
#   define KOS_PERF_CNT_ARRAY(stat, idx) (void)0
#   define KOS_PERF_ADD(stat, num)       (void)0
#endif

#endif

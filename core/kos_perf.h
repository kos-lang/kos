/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_PERF_H_INCLUDED
#define KOS_PERF_H_INCLUDED

#include "../inc/kos_atomic.h"

#if defined(CONFIG_PERF) && defined(KOS_NO_64BIT_ATOMICS)
#   undef CONFIG_PERF
#endif

#ifdef CONFIG_PERF
#   define KOS_PERF_CNT(stat)            KOS_atomic_add_u64(kos_perf.stat, 1)
#   define KOS_PERF_CNT_ARRAY(stat, idx) KOS_atomic_add_u64(kos_perf.stat[idx], 1)
#   define KOS_PERF_ADD(stat, num)       KOS_atomic_add_u64(kos_perf.stat, (num))

struct KOS_PERF_S {
    KOS_ATOMIC(uint64_t) object_get_success;
    KOS_ATOMIC(uint64_t) object_get_fail;
    KOS_ATOMIC(uint64_t) object_set_success;
    KOS_ATOMIC(uint64_t) object_set_fail;
    KOS_ATOMIC(uint64_t) object_delete_success;
    KOS_ATOMIC(uint64_t) object_delete_fail;
    KOS_ATOMIC(uint64_t) object_resize_success;
    KOS_ATOMIC(uint64_t) object_resize_fail;
    KOS_ATOMIC(uint64_t) object_salvage_success;
    KOS_ATOMIC(uint64_t) object_salvage_fail;
    KOS_ATOMIC(uint64_t) object_collision[4];

    KOS_ATOMIC(uint64_t) array_salvage_success;
    KOS_ATOMIC(uint64_t) array_salvage_fail;

    KOS_ATOMIC(uint64_t) alloc_object;
    KOS_ATOMIC(uint64_t) alloc_huge_object;
    KOS_ATOMIC(uint64_t) non_full_seek;
    KOS_ATOMIC(uint64_t) non_full_seek_max;
    KOS_ATOMIC(uint64_t) alloc_new_page;
    KOS_ATOMIC(uint64_t) alloc_free_page;
    KOS_ATOMIC(uint64_t) gc_cycles;

    KOS_ATOMIC(uint64_t) alloc_object_size[4];
    KOS_ATOMIC(uint64_t) evac_object_size[4];

    KOS_ATOMIC(uint64_t) instructions;
};

extern struct KOS_PERF_S kos_perf;

#else
#   define KOS_PERF_CNT(stat)            (void)0
#   define KOS_PERF_CNT_ARRAY(stat, idx) (void)0
#   define KOS_PERF_ADD(stat, num)       (void)0
#endif

#endif

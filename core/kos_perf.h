/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_PERF_H_INCLUDED
#define KOS_PERF_H_INCLUDED

#include "../inc/kos_atomic.h"

#if defined(CONFIG_PERF) && defined(KOS_NO_64BIT_ATOMICS)
#   undef CONFIG_PERF
#endif

#if defined(__cplusplus) && defined(TRACY_ENABLE)
#   include "Tracy.hpp"
#   define PROF_FRAME_START(name)        FrameMarkStart(name)
#   define PROF_FRAME_END(name)          FrameMarkEnd(name)
#   define PROF_MALLOC(ptr, size)        TracyAlloc(ptr, size)
#   define PROF_FREE(ptr)                TracyFree(ptr)
#   define PROF_ZONE(color)              ZoneScopedC(PROF_ ## color)
#   define PROF_ZONE_N(color, name)      ZoneScopedNC(#name, PROF_ ## color)
#   define PROF_ZONE_NAME(name, len)     ZoneName(name, len)
#   define PROF_PLOT(name, value)        TracyPlot(name, value)
#   define PROF_PLOT_INIT(name, type)    TracyPlotConfig(name, tracy::PlotFormatType::type)
#   define PROF_PARSER                   0x81A2A4
#   define PROF_COMPILER                 0xA48281
#   define PROF_MODULE                   0x9192A5
#   define PROF_INSTR                    0x12E213
#   define PROF_VM                       0xE3E362
#   define PROF_HEAP                     0x080881
#   define PROF_GC                       0x1520DA
#   define KOS_PERF_CNT(stat)            (void)0
#   define KOS_PERF_CNT_ARRAY(stat, idx) (void)0
#   define KOS_PERF_ADD(stat, num)       (void)0
#elif defined(CONFIG_PERF)
#   define PROF_FRAME_START(name)
#   define PROF_FRAME_END(name)
#   define PROF_MALLOC(ptr, size)
#   define PROF_FREE(ptr)
#   define PROF_ZONE(color)
#   define PROF_ZONE_N(color, name)
#   define PROF_ZONE_NAME(name, len)
#   define PROF_PLOT(name, value)
#   define PROF_PLOT_INIT(name, type)
#   define KOS_PERF_CNT(stat)            KOS_atomic_add_u64(kos_perf.stat, 1)
#   define KOS_PERF_CNT_ARRAY(stat, idx) KOS_atomic_add_u64(kos_perf.stat[idx], 1)
#   define KOS_PERF_ADD(stat, num)       KOS_atomic_add_u64(kos_perf.stat, (num))

struct KOS_PERF_S {
    KOS_ATOMIC(uint64_t) object_key_identical;
    KOS_ATOMIC(uint64_t) object_key_diff_hash;
    KOS_ATOMIC(uint64_t) object_key_compare_success;
    KOS_ATOMIC(uint64_t) object_key_compare_fail;
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
    KOS_ATOMIC(uint64_t) mark_groups_alloc;
    KOS_ATOMIC(uint64_t) mark_groups_sched;

    KOS_ATOMIC(uint64_t) alloc_object_size[4];
    KOS_ATOMIC(uint64_t) evac_object_size[4];

    KOS_ATOMIC(uint64_t) instructions;
};

extern struct KOS_PERF_S kos_perf;

#else
#   define PROF_FRAME_START(name)
#   define PROF_FRAME_END(name)
#   define PROF_MALLOC(ptr, size)
#   define PROF_FREE(ptr)
#   define PROF_ZONE(color)
#   define PROF_ZONE_N(color, name)
#   define PROF_ZONE_NAME(name, len)
#   define PROF_PLOT(name, value)
#   define PROF_PLOT_INIT(name, type)
#   define KOS_PERF_CNT(stat)            (void)0
#   define KOS_PERF_CNT_ARRAY(stat, idx) (void)0
#   define KOS_PERF_ADD(stat, num)       (void)0
#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_OBJECT_ALLOC_H_INCLUDED
#define KOS_OBJECT_ALLOC_H_INCLUDED

#include "../inc/kos_entity.h"
#include <stddef.h>

typedef enum KOS_ALLOC_FLAG_E {
    KOS_ALLOC_MOVABLE,
    KOS_ALLOC_IMMOVABLE
} KOS_ALLOC_FLAG;

enum GC_STATE_E {
    GC_INACTIVE,

    /* ctx->gc_state */
    GC_SUSPENDED,
    GC_ENGAGED,

    /* heap->gc_state */
    GC_INIT,
    GC_MARK,
    GC_EVACUATE,
    GC_UPDATE
};

int   kos_heap_init(KOS_INSTANCE *inst);

void  kos_heap_destroy(KOS_INSTANCE *inst);

void *kos_alloc_object(KOS_CONTEXT    ctx,
                       KOS_ALLOC_FLAG flags,
                       KOS_TYPE       object_type,
                       uint32_t       size);

void *kos_alloc_object_page(KOS_CONTEXT ctx,
                            KOS_TYPE    object_type);

void kos_heap_release_thread_page(KOS_CONTEXT ctx);

void kos_print_heap(KOS_CONTEXT ctx);

#ifdef CONFIG_MAD_GC
int kos_trigger_mad_gc(KOS_CONTEXT ctx);
#else
#define kos_trigger_mad_gc(ctx) 0
#endif

#if defined(CONFIG_MAD_GC)
int kos_gc_active(KOS_CONTEXT ctx);
#endif

#endif

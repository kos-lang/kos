/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_MEMORY_H_INCLUDED
#define KOS_MEMORY_H_INCLUDED

#include "kos_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Variable-size allocator without ability to free items */

struct KOS_MEMPOOL_S {
    size_t free_size;
    void  *next_free;
    void  *buffers;
};

typedef struct KOS_MEMPOOL_S KOS_MEMPOOL;

KOS_API void  KOS_mempool_init(KOS_MEMPOOL *mempool);
KOS_API void  KOS_mempool_init_small(KOS_MEMPOOL *mempool, size_t initial_size);
KOS_API void  KOS_mempool_destroy(KOS_MEMPOOL *mempool);
KOS_API void *KOS_mempool_alloc(KOS_MEMPOOL *mempool, size_t size);

/* Dynamic array of bytes */

struct KOS_VECTOR_S {
    char  *buffer;
    size_t size;
    size_t capacity;
    double local_buffer_[2];
};

typedef struct KOS_VECTOR_S KOS_VECTOR;

KOS_API void KOS_vector_init(KOS_VECTOR *vector);
KOS_API void KOS_vector_destroy(KOS_VECTOR *vector);
KOS_API int  KOS_vector_reserve(KOS_VECTOR *vector, size_t capacity);
KOS_API int  KOS_vector_resize(KOS_VECTOR *vector, size_t size);
KOS_API int  KOS_vector_concat(KOS_VECTOR *dest, KOS_VECTOR *src);

#ifdef __cplusplus
}
#endif

#endif

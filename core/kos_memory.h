/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_MEMORY_H_INCLUDED
#define KOS_MEMORY_H_INCLUDED

#include <stddef.h>

/* Variable-size allocator without ability to free items */

struct KOS_MEMPOOL_S {
    size_t free_size;
    void  *buffers;
};

void  kos_mempool_init(struct KOS_MEMPOOL_S *mempool);
void  kos_mempool_destroy(struct KOS_MEMPOOL_S *mempool);
void *kos_mempool_alloc(struct KOS_MEMPOOL_S *mempool, size_t size);

/* Dynamic array of bytes */

struct KOS_VECTOR_S {
    char  *buffer;
    size_t size;
    size_t capacity;
    double local_buffer_[2];
};

typedef struct KOS_VECTOR_S KOS_VECTOR;

void kos_vector_init(KOS_VECTOR *vector);
void kos_vector_destroy(KOS_VECTOR *vector);
int  kos_vector_reserve(KOS_VECTOR *vector, size_t capacity);
int  kos_vector_resize(KOS_VECTOR *vector, size_t size);
int  kos_vector_concat(KOS_VECTOR *dest, KOS_VECTOR *src);

#endif

/*
 * Copyright (c) 2014-2020 Chris Dragan
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
    double _local_buffer[2];
};

typedef struct KOS_VECTOR_S KOS_VECTOR;

void kos_vector_init(KOS_VECTOR *vector);
void kos_vector_destroy(KOS_VECTOR *vector);
int  kos_vector_reserve(KOS_VECTOR *vector, size_t capacity);
int  kos_vector_resize(KOS_VECTOR *vector, size_t size);
int  kos_vector_concat(KOS_VECTOR *dest, KOS_VECTOR *src);
int  kos_vector_append_cstr(KOS_VECTOR *dest, const char* cstr);

#endif

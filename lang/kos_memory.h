/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#ifndef __KOS_MEMORY_H
#define __KOS_MEMORY_H

#include <stddef.h>

/* Variable-size allocator without ability to free items */

struct _KOS_MEMPOOL {
    size_t free_size;
    void  *buffers;
};

void  _KOS_mempool_init(struct _KOS_MEMPOOL *mempool);
void  _KOS_mempool_destroy(struct _KOS_MEMPOOL *mempool);
void *_KOS_mempool_alloc(struct _KOS_MEMPOOL *mempool, size_t size);

/* Dynamic array of bytes */

struct _KOS_VECTOR {
    char  *buffer;
    size_t size;
    size_t capacity;
};

void _KOS_vector_init(struct _KOS_VECTOR *vector);
void _KOS_vector_destroy(struct _KOS_VECTOR *vector);
int  _KOS_vector_reserve(struct _KOS_VECTOR *vector, size_t capacity);
int  _KOS_vector_resize(struct _KOS_VECTOR *vector, size_t size);

#endif

/*
 * Copyright (c) 2014-2018 Chris Dragan
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

#include "kos_memory.h"
#include "../inc/kos_error.h"
#include "../inc/kos_threads.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include <assert.h>
#include <string.h>

struct _KOS_MEMPOOL_BUFFER {
    struct _KOS_MEMPOOL_BUFFER *next;
    uint8_t                     buf[8];
};

void _KOS_mempool_init(struct _KOS_MEMPOOL *mempool)
{
    mempool->free_size = 0;
    mempool->buffers   = 0;
}

void _KOS_mempool_destroy(struct _KOS_MEMPOOL *mempool)
{
    struct _KOS_MEMPOOL_BUFFER *buf = (struct _KOS_MEMPOOL_BUFFER *)(mempool->buffers);

    while (buf) {
        struct _KOS_MEMPOOL_BUFFER *to_free = buf;

        buf = buf->next;

        _KOS_free(to_free);
    }
}

static void *_alloc_large(struct _KOS_MEMPOOL *mempool, size_t size)
{
    const size_t alloc_size = size + sizeof(struct _KOS_MEMPOOL_BUFFER);

    struct _KOS_MEMPOOL_BUFFER *buf = (struct _KOS_MEMPOOL_BUFFER *)_KOS_malloc(alloc_size);

    uint8_t *obj = 0;

    if (buf) {

        if (mempool->buffers) {

            struct _KOS_MEMPOOL_BUFFER *top  = (struct _KOS_MEMPOOL_BUFFER *)mempool->buffers;
            struct _KOS_MEMPOOL_BUFFER *next = top->next;

            buf->next = next;
            top->next = buf;
        }
        else {
            buf->next          = 0;
            mempool->buffers   = buf;
            mempool->free_size = 0;
        }

        obj = (uint8_t *)buf + sizeof(struct _KOS_MEMPOOL_BUFFER);
    }

    return obj;
}

void *_KOS_mempool_alloc(struct _KOS_MEMPOOL *mempool, size_t size)
{
    uint8_t                    *obj = 0;
    struct _KOS_MEMPOOL_BUFFER *buf = 0;

    size = (size + 7U) & ~7U;

    if (size > mempool->free_size) {

        /* Special handling for unusually large allocation requests */
        if (size > (_KOS_BUF_ALLOC_SIZE / 16U))
            return _alloc_large(mempool, size);

        buf = (struct _KOS_MEMPOOL_BUFFER *)_KOS_malloc(_KOS_BUF_ALLOC_SIZE);

        if (buf) {
            buf->next          = (struct _KOS_MEMPOOL_BUFFER *)(mempool->buffers);
            mempool->buffers   = buf;
            mempool->free_size = _KOS_BUF_ALLOC_SIZE - sizeof(struct _KOS_MEMPOOL_BUFFER);
        }
    }
    else if ( ! _KOS_seq_fail())
        buf = (struct _KOS_MEMPOOL_BUFFER *)(mempool->buffers);

    if (buf) {
        assert(size <= mempool->free_size);

        obj = ((uint8_t *)buf) + _KOS_BUF_ALLOC_SIZE - mempool->free_size;

        mempool->free_size -= size;
    }

    return obj;
}

void _KOS_vector_init(struct _KOS_VECTOR *vector)
{
    vector->buffer   = (char *)(void *)&vector->_local_buffer;
    vector->size     = 0;
    vector->capacity = sizeof(vector->_local_buffer);
}

void _KOS_vector_destroy(struct _KOS_VECTOR *vector)
{
    if (vector->capacity > sizeof(vector->_local_buffer)) {
        _KOS_free(vector->buffer);

        _KOS_vector_init(vector);
    }
}

int _KOS_vector_reserve(struct _KOS_VECTOR *vector, size_t capacity)
{
    int error = KOS_SUCCESS;

    if (capacity > vector->capacity) {

        char *const new_buf = (char *)_KOS_malloc(capacity);

        if (new_buf) {

            if (vector->size)
                memcpy(new_buf, vector->buffer, vector->size);

            if (vector->capacity > sizeof(vector->_local_buffer))
                _KOS_free(vector->buffer);

            vector->buffer = new_buf;

            vector->capacity = capacity;
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    return error;
}

int _KOS_vector_resize(struct _KOS_VECTOR *vector, size_t size)
{
    int error = KOS_SUCCESS;

    if (size > vector->capacity) {
        size_t       new_capacity;
        const size_t delta = KOS_max(KOS_min(vector->capacity, (size_t)_KOS_VEC_MAX_INC_SIZE),
                                     (size_t)64);

        assert(vector->capacity);

        if (vector->capacity + delta > size)
            new_capacity = vector->capacity + delta;
        else
            new_capacity = size;

        error = _KOS_vector_reserve(vector, new_capacity);
    }

    if (!error)
        vector->size = size;

    return error;
}

int _KOS_vector_concat(struct _KOS_VECTOR *dest, struct _KOS_VECTOR *src)
{
    int error = KOS_SUCCESS;

    if (src->size) {

        const size_t pos = dest->size;

        error = _KOS_vector_resize(dest, dest->size + src->size);
        if ( ! error)
            memcpy(dest->buffer + pos, src->buffer, src->size);
    }

    return error;
}

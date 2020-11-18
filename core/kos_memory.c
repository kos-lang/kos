/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_memory.h"
#include "../inc/kos_atomic.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_math.h"
#include <assert.h>
#include <string.h>

struct MEMPOOL_BUF {
    struct MEMPOOL_BUF *next;
    uint8_t             buf[8];
};

void KOS_mempool_init(struct KOS_MEMPOOL_S *mempool)
{
    mempool->free_size = 0;
    mempool->buffers   = 0;
}

void KOS_mempool_destroy(struct KOS_MEMPOOL_S *mempool)
{
    struct MEMPOOL_BUF *buf = (struct MEMPOOL_BUF *)(mempool->buffers);

    while (buf) {
        struct MEMPOOL_BUF *to_free = buf;

        buf = buf->next;

        KOS_free(to_free);
    }
}

static void *alloc_large(struct KOS_MEMPOOL_S *mempool, size_t size)
{
    const size_t alloc_size = size + sizeof(struct MEMPOOL_BUF);

    struct MEMPOOL_BUF *buf = (struct MEMPOOL_BUF *)KOS_malloc(alloc_size);

    uint8_t *obj = 0;

    if (buf) {

        if (mempool->buffers) {

            struct MEMPOOL_BUF *top  = (struct MEMPOOL_BUF *)mempool->buffers;
            struct MEMPOOL_BUF *next = top->next;

            buf->next = next;
            top->next = buf;
        }
        else {
            buf->next          = 0;
            mempool->buffers   = buf;
            mempool->free_size = 0;
        }

        obj = (uint8_t *)buf + sizeof(struct MEMPOOL_BUF);
    }

    return obj;
}

void *KOS_mempool_alloc(struct KOS_MEMPOOL_S *mempool, size_t size)
{
    uint8_t            *obj = 0;
    struct MEMPOOL_BUF *buf = 0;

    size = (size + 7U) & ~7U;

    if (size > mempool->free_size) {

        /* Special handling for unusually large allocation requests */
        if (size > (KOS_BUF_ALLOC_SIZE / 16U))
            return alloc_large(mempool, size);

        buf = (struct MEMPOOL_BUF *)KOS_malloc(KOS_BUF_ALLOC_SIZE);

        if (buf) {
            buf->next          = (struct MEMPOOL_BUF *)(mempool->buffers);
            mempool->buffers   = buf;
            mempool->free_size = KOS_BUF_ALLOC_SIZE - sizeof(struct MEMPOOL_BUF);
        }
    }
    else if ( ! kos_seq_fail())
        buf = (struct MEMPOOL_BUF *)(mempool->buffers);

    if (buf) {
        assert(size <= mempool->free_size);

        obj = ((uint8_t *)buf) + KOS_BUF_ALLOC_SIZE - mempool->free_size;

        mempool->free_size -= size;
    }

    return obj;
}

void KOS_vector_init(KOS_VECTOR *vector)
{
    vector->buffer   = (char *)(void *)&vector->local_buffer_;
    vector->size     = 0;
    vector->capacity = sizeof(vector->local_buffer_);
}

void KOS_vector_destroy(KOS_VECTOR *vector)
{
    if (vector->capacity > sizeof(vector->local_buffer_)) {
        KOS_free(vector->buffer);

        KOS_vector_init(vector);
    }
}

int KOS_vector_reserve(KOS_VECTOR *vector, size_t capacity)
{
    int error = KOS_SUCCESS;

    if (capacity > vector->capacity) {

        const int in_place = vector->capacity <= sizeof(vector->local_buffer_);

        char *const new_buf = in_place ? (char *)KOS_malloc(capacity)
                                       : (char *)KOS_realloc(vector->buffer, capacity);

        if (new_buf) {
            if (in_place && vector->size)
                memcpy(new_buf, vector->buffer, vector->size);

            vector->buffer   = new_buf;
            vector->capacity = capacity;
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    return error;
}

int KOS_vector_resize(KOS_VECTOR *vector, size_t size)
{
    int error = KOS_SUCCESS;

    if (size > vector->capacity) {
        size_t       new_capacity;
        const size_t delta = KOS_max(KOS_min(vector->capacity, (size_t)KOS_VEC_MAX_INC_SIZE),
                                     (size_t)64);

        assert(vector->capacity);

        if (vector->capacity + delta > size)
            new_capacity = vector->capacity + delta;
        else
            new_capacity = size;

        error = KOS_vector_reserve(vector, new_capacity);
    }

    if (!error)
        vector->size = size;

    return error;
}

int KOS_vector_concat(KOS_VECTOR *dest, KOS_VECTOR *src)
{
    int error = KOS_SUCCESS;

    if (src->size) {

        const size_t pos = dest->size;

        error = KOS_vector_resize(dest, dest->size + src->size);
        if ( ! error)
            memcpy(dest->buffer + pos, src->buffer, src->size);
    }

    return error;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../inc/kos_malloc.h"
#include "kos_debug.h"
#include "kos_perf.h"
#include <stdlib.h>
#ifdef _WIN32
#   include <malloc.h>
#endif

void *KOS_malloc(size_t size)
{
    void *ptr;

    if (kos_seq_fail())
        return KOS_NULL;

    ptr = malloc(size);

    PROF_MALLOC(ptr, size)

    return ptr;
}

void *KOS_realloc(void *ptr, size_t size)
{
    if (kos_seq_fail())
        return KOS_NULL;

    PROF_FREE(ptr)

    ptr = realloc(ptr, size);

    PROF_MALLOC(ptr, size)

    return ptr;
}

void KOS_free(void *ptr)
{
    PROF_FREE(ptr)

    free(ptr);
}

#ifdef _WIN32
void *KOS_malloc_aligned(size_t size, size_t alignment)
{
    void *ptr;

    if (kos_seq_fail())
        return KOS_NULL;

    ptr = _aligned_malloc(size, alignment);

    PROF_MALLOC(ptr, size)

    return ptr;
}

void KOS_free_aligned(void *ptr)
{
    PROF_FREE(ptr)

    _aligned_free(ptr);
}

#else

void *KOS_malloc_aligned(size_t size, size_t alignment)
{
    void *ptr = KOS_NULL;

    if (kos_seq_fail())
        return KOS_NULL;

    if (posix_memalign(&ptr, alignment, size))
        return KOS_NULL;

    PROF_MALLOC(ptr, size)

    return ptr;
}

void KOS_free_aligned(void *ptr)
{
    PROF_FREE(ptr)

    free(ptr);
}
#endif

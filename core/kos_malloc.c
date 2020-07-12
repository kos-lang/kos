/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_malloc.h"
#include "kos_debug.h"
#include "kos_perf.h"
#include <stdlib.h>
#ifdef _WIN32
#   include <malloc.h>
#endif

void *kos_malloc(size_t size)
{
    void *ptr;

    if (kos_seq_fail())
        return 0;

    ptr = malloc(size);

    PROF_MALLOC(ptr, size);

    return ptr;
}

void *kos_realloc(void *ptr, size_t size)
{
    if (kos_seq_fail())
        return 0;

    PROF_FREE(ptr);

    ptr = realloc(ptr, size);

    PROF_MALLOC(ptr, size);

    return ptr;
}

void kos_free(void *ptr)
{
    PROF_FREE(ptr);

    free(ptr);
}

#ifdef _WIN32
void *kos_malloc_aligned(size_t size, size_t alignment)
{
    void *ptr;

    if (kos_seq_fail())
        return 0;

    ptr = _aligned_malloc(size, alignment);

    PROF_MALLOC(ptr, size);

    return ptr;
}

void kos_free_aligned(void *ptr)
{
    PROF_FREE(ptr);

    _aligned_free(ptr);
}

#else

void *kos_malloc_aligned(size_t size, size_t alignment)
{
    void *ptr = 0;

    if (kos_seq_fail())
        return 0;

    if (posix_memalign(&ptr, alignment, size))
        return 0;

    PROF_MALLOC(ptr, size);

    return ptr;
}

void kos_free_aligned(void *ptr)
{
    PROF_FREE(ptr);

    free(ptr);
}
#endif

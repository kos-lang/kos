/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_MALLOC_H_INCLUDED
#define KOS_MALLOC_H_INCLUDED

#include <stddef.h>

void *kos_malloc(size_t size);
void *kos_realloc(void *ptr, size_t size);
void  kos_free(void *ptr);
void *kos_malloc_aligned(size_t size, size_t alignment);
void  kos_free_aligned(void *ptr);

#endif

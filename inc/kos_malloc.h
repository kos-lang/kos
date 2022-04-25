/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_MALLOC_H_INCLUDED
#define KOS_MALLOC_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include "kos_api.h"
#include <stddef.h>

KOS_API void *KOS_malloc(size_t size);
KOS_API void *KOS_realloc(void *ptr, size_t size);
KOS_API void  KOS_free(void *ptr);
KOS_API void *KOS_malloc_aligned(size_t size, size_t alignment);
KOS_API void  KOS_free_aligned(void *ptr);

#ifdef __cplusplus
}
#endif

#endif

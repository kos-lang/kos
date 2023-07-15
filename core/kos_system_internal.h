/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_SYSTEM_INTERNAL_H_INCLUDED
#define KOS_SYSTEM_INTERNAL_H_INCLUDED

#include "../inc/kos_system.h"

#ifndef _WIN32
#   include <sys/stat.h>
#endif

struct KOS_VECTOR_S;

#ifdef _WIN32
#define KOS_FOPEN_CLOEXEC
#else
#define KOS_FOPEN_CLOEXEC "e"
int kos_unix_open(const char *filename, int flags);
#endif

int kos_executable_path(struct KOS_VECTOR_S *buf);

enum KOS_PROTECT_E {
    KOS_NO_ACCESS,
    KOS_READ_WRITE
};

int kos_mem_protect(void *ptr, unsigned size, enum KOS_PROTECT_E protect);

#ifndef _WIN32
KOS_OBJ_ID kos_stat(KOS_CONTEXT ctx, struct stat *st);
#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_MOD_IO_H_INCLUDED
#define KOS_MOD_IO_H_INCLUDED

#include "../inc/kos_entity.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
typedef void *KOS_FILE_HANDLE; /* HANDLE */
#else
typedef FILE *KOS_FILE_HANDLE;
#endif

KOS_API
KOS_FILE_HANDLE KOS_io_get_file(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  file_obj);

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_MOD_IO_H_INCLUDED
#define KOS_MOD_IO_H_INCLUDED

#include "../inc/kos_entity.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_os_make_file_object(KOS_CONTEXT ctx,
                                   KOS_OBJ_ID  io_module_obj,
                                   FILE       *file);

KOS_API
FILE *KOS_os_get_file(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  file_obj);

#ifdef __cplusplus
}
#endif

#endif

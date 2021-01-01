/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_MODULES_INIT_H_INCLUDED
#define KOS_MODULES_INIT_H_INCLUDED

#include "kos_api.h"

struct KOS_THREAD_CONTEXT_S;

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
int KOS_modules_init(struct KOS_THREAD_CONTEXT_S *ctx);

#ifdef __cplusplus
}
#endif

#endif

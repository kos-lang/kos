/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_MODULES_INIT_H_INCLUDED
#define KOS_MODULES_INIT_H_INCLUDED

struct KOS_THREAD_CONTEXT_S;

#ifdef __cplusplus
extern "C" {
#endif

int KOS_modules_init(struct KOS_THREAD_CONTEXT_S *ctx);

#ifdef __cplusplus
}
#endif

#endif

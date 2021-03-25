/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_DEBUG_H_INCLUDED
#define KOS_DEBUG_H_INCLUDED

#if (defined(CONFIG_SEQFAIL) || defined(CONFIG_FUZZ)) && !defined(KOS_PUBLIC_API)

int  kos_seq_fail(void);
void kos_set_seq_point(int seq_point);

#else

#define kos_seq_fail() 0
#define kos_set_seq_point(x) ((void)0)

#endif

#endif

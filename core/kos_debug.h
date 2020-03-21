/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_DEBUG_H_INCLUDED
#define KOS_DEBUG_H_INCLUDED

#ifdef CONFIG_SEQFAIL

int kos_seq_fail(void);

#else

#define kos_seq_fail() 0

#endif

#endif

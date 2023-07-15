/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_DEBUG_H_INCLUDED
#define KOS_DEBUG_H_INCLUDED

#include "../inc/kos_atomic.h"

#if (defined(CONFIG_SEQFAIL) || defined(CONFIG_FUZZ)) && !defined(KOS_PUBLIC_API)

int  kos_seq_fail(void);
void kos_set_seq_point(int seq_point);

#else

#define kos_seq_fail() 0
#define kos_set_seq_point(x) ((void)0)

#endif

#if defined(CONFIG_FUZZ) && !defined(KOS_PUBLIC_API)
extern KOS_ATOMIC(uint32_t) kos_fuzz_instructions;
#   define MAX_FUZZ_INSTR 1024U
#   define KOS_INSTR_FUZZ_LIMIT() do {                                         \
        if (KOS_atomic_add_u32(kos_fuzz_instructions, 1U) >= MAX_FUZZ_INSTR) { \
            KOS_DECLARE_STATIC_CONST_STRING(str_err_cnt, "too many instr");    \
            RAISE_EXCEPTION_STR(str_err_cnt);                                  \
        }                                                                      \
    } while (0)
#else
#   define KOS_INSTR_FUZZ_LIMIT() ((void)0)
#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_CONST_STRINGS_H_INCLUDED
#define KOS_CONST_STRINGS_H_INCLUDED

#include "../inc/kos_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

KOS_API extern struct KOS_CONST_STRING_S KOS_str_empty;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_backtrace;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_file;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_function;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_line;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_offset;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_out_of_memory;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_value;
KOS_API extern struct KOS_CONST_STRING_S KOS_str_void;

#ifdef __cplusplus
}
#endif

#define KOS_STR_EMPTY         KOS_CONST_ID(KOS_str_empty)
#define KOS_STR_BACKTRACE     KOS_CONST_ID(KOS_str_backtrace)
#define KOS_STR_FILE          KOS_CONST_ID(KOS_str_file)
#define KOS_STR_FUNCTION      KOS_CONST_ID(KOS_str_function)
#define KOS_STR_LINE          KOS_CONST_ID(KOS_str_line)
#define KOS_STR_OFFSET        KOS_CONST_ID(KOS_str_offset)
#define KOS_STR_OUT_OF_MEMORY KOS_CONST_ID(KOS_str_out_of_memory)
#define KOS_STR_VALUE         KOS_CONST_ID(KOS_str_value)
#define KOS_STR_VOID          KOS_CONST_ID(KOS_str_void)

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_CONST_STRINGS_H_INCLUDED
#define KOS_CONST_STRINGS_H_INCLUDED

#include "../inc/kos_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct KOS_CONST_STRING_S KOS_str_empty;
extern struct KOS_CONST_STRING_S KOS_str_backtrace;
extern struct KOS_CONST_STRING_S KOS_str_file;
extern struct KOS_CONST_STRING_S KOS_str_function;
extern struct KOS_CONST_STRING_S KOS_str_line;
extern struct KOS_CONST_STRING_S KOS_str_offset;
extern struct KOS_CONST_STRING_S KOS_str_out_of_memory;
extern struct KOS_CONST_STRING_S KOS_str_value;
extern struct KOS_CONST_STRING_S KOS_str_void;
extern struct KOS_CONST_STRING_S KOS_str_xbuiltinx;

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
#define KOS_STR_XBUILTINX     KOS_CONST_ID(KOS_str_xbuiltinx)

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_const_strings.h"

#ifdef __cplusplus
extern "C" {
#endif

KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING_WITH_LENGTH(KOS_str_empty, 0, KOS_NULL);

KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_file,          "file");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_backtrace,     "backtrace");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_function,      "function");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_line,          "line");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_offset,        "offset");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_out_of_memory, "out of memory");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_value,         "value");
KOS_API_VAR_DEF KOS_DECLARE_CONST_STRING(KOS_str_void,          "void");

#ifdef __cplusplus
}
#endif

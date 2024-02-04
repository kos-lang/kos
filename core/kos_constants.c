/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

KOS_DECLARE_CONST_STRING_WITH_LENGTH(KOS_str_empty, 0, KOS_NULL);

KOS_DECLARE_CONST_STRING(KOS_str_file,          "file");
KOS_DECLARE_CONST_STRING(KOS_str_backtrace,     "backtrace");
KOS_DECLARE_CONST_STRING(KOS_str_function,      "function");
KOS_DECLARE_CONST_STRING(KOS_str_line,          "line");
KOS_DECLARE_CONST_STRING(KOS_str_offset,        "offset");
KOS_DECLARE_CONST_STRING(KOS_str_out_of_memory, "out of memory");
KOS_DECLARE_CONST_STRING(KOS_str_value,         "value");
KOS_DECLARE_CONST_STRING(KOS_str_void,          "void");

DECLARE_CONST_OBJECT(KOS_void,  OBJ_VOID,    0);
DECLARE_CONST_OBJECT(KOS_false, OBJ_BOOLEAN, 0);
DECLARE_CONST_OBJECT(KOS_true,  OBJ_BOOLEAN, 1);

KOS_DECLARE_EMPTY_CONST_ARRAY(KOS_empty_array)

#ifdef __cplusplus
}
#endif

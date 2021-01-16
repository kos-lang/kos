/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_constants.h"

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

KOS_API_VAR_DEF DECLARE_CONST_OBJECT(KOS_void,  OBJ_VOID,    0);
KOS_API_VAR_DEF DECLARE_CONST_OBJECT(KOS_false, OBJ_BOOLEAN, 0);
KOS_API_VAR_DEF DECLARE_CONST_OBJECT(KOS_true,  OBJ_BOOLEAN, 1);
KOS_API_VAR_DEF KOS_DECLARE_ALIGNED(32, const struct KOS_CONST_ARRAY_S KOS_empty_array) =
    { { { 0, 0 } }, { OBJ_ARRAY, 0, KOS_READ_ONLY, KOS_BADPTR } };

#ifdef __cplusplus
}
#endif

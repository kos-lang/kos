/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_CONST_STRINGS_H_INCLUDED
#define KOS_CONST_STRINGS_H_INCLUDED

#include "kos_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

KOS_API extern struct KOS_CONST_STRING_S       KOS_str_empty;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_backtrace;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_file;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_function;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_line;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_offset;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_out_of_memory;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_value;
KOS_API extern struct KOS_CONST_STRING_S       KOS_str_void;
KOS_API extern const struct KOS_CONST_OBJECT_S KOS_void;
KOS_API extern const struct KOS_CONST_OBJECT_S KOS_false;
KOS_API extern const struct KOS_CONST_OBJECT_S KOS_true;
KOS_API extern const struct KOS_CONST_ARRAY_S  KOS_empty_array;

#ifdef __cplusplus
}
#endif

#define KOS_DECLARE_EMPTY_CONST_ARRAY(name)                        \
    KOS_DECLARE_ALIGNED(32, const struct KOS_CONST_ARRAY_S name) = \
        { { { 0, 0 } }, { OBJ_ARRAY, 0, KOS_READ_ONLY, KOS_BADPTR } };

#define KOS_STR_EMPTY         KOS_CONST_ID(KOS_str_empty)
#define KOS_STR_BACKTRACE     KOS_CONST_ID(KOS_str_backtrace)
#define KOS_STR_FILE          KOS_CONST_ID(KOS_str_file)
#define KOS_STR_FUNCTION      KOS_CONST_ID(KOS_str_function)
#define KOS_STR_LINE          KOS_CONST_ID(KOS_str_line)
#define KOS_STR_OFFSET        KOS_CONST_ID(KOS_str_offset)
#define KOS_STR_OUT_OF_MEMORY KOS_CONST_ID(KOS_str_out_of_memory)
#define KOS_STR_VALUE         KOS_CONST_ID(KOS_str_value)
#define KOS_STR_VOID          KOS_CONST_ID(KOS_str_void)
#define KOS_VOID              KOS_CONST_ID(KOS_void)
#define KOS_FALSE             KOS_CONST_ID(KOS_false)
#define KOS_TRUE              KOS_CONST_ID(KOS_true)
#define KOS_EMPTY_ARRAY       KOS_CONST_ID(KOS_empty_array)

#define KOS_BOOL(v) ( (v) ? KOS_TRUE : KOS_FALSE )

#ifdef __cplusplus

static inline bool KOS_get_bool(KOS_OBJ_ID obj_id)
{
    assert(obj_id == KOS_TRUE || obj_id == KOS_FALSE);
    return obj_id == KOS_TRUE;
}

#else

#define KOS_get_bool(obj_id) ((obj_id) == KOS_TRUE)

#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_UTILS_H_INCLUDED
#define KOS_UTILS_H_INCLUDED

#include "kos_entity.h"
#include <stddef.h>
#include <stdint.h>

enum KOS_NUMERIC_TYPE_E {
    KOS_NON_NUMERIC,
    KOS_INTEGER_VALUE,
    KOS_FLOAT_VALUE
};

typedef union KOS_NUMERIC_VALUE_U {
    int64_t i;
    double  d;
} KOS_NUMERIC_VALUE;

typedef struct KOS_NUMERIC_S {
    enum KOS_NUMERIC_TYPE_E type;
    KOS_NUMERIC_VALUE       u;
} KOS_NUMERIC;

struct KOS_MEMPOOL_S;

struct KOS_VECTOR_S;

typedef enum KOS_COMPARE_RESULT_E {
    KOS_EQUAL,
    KOS_LESS_THAN,
    KOS_GREATER_THAN,
    KOS_INDETERMINATE
} KOS_COMPARE_RESULT;

enum KOS_VOID_INDEX_E {
    KOS_VOID_INDEX_INVALID,
    KOS_VOID_INDEX_IS_BEGIN,
    KOS_VOID_INDEX_IS_END
};

enum KOS_CONVERT_TYPE_E {
    KOS_NATIVE_INVALID,
    KOS_NATIVE_SKIP,
    KOS_NATIVE_UINT8,
    KOS_NATIVE_UINT16,
    KOS_NATIVE_UINT32,
    KOS_NATIVE_UINT64,
    KOS_NATIVE_INT8,
    KOS_NATIVE_INT16,
    KOS_NATIVE_INT32,
    KOS_NATIVE_INT64,
    KOS_NATIVE_SIZE,
    KOS_NATIVE_ENUM,
    KOS_NATIVE_BOOL8,
    KOS_NATIVE_BOOL32,
    KOS_NATIVE_FLOAT,
    KOS_NATIVE_DOUBLE,
    KOS_NATIVE_STRING,
    KOS_NATIVE_STRING_PTR,
    KOS_NATIVE_BUFFER
};

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
const char *KOS_get_type_name(KOS_TYPE type);

KOS_API
int KOS_get_numeric_arg(KOS_CONTEXT  ctx,
                        KOS_OBJ_ID   args_obj,
                        int          idx,
                        KOS_NUMERIC *numeric);

KOS_API
KOS_NUMERIC KOS_get_numeric(KOS_OBJ_ID obj_id);

KOS_API
int KOS_get_integer(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t    *ret);

KOS_API
int KOS_get_index_arg(KOS_CONTEXT           ctx,
                      KOS_OBJ_ID            args_obj,
                      int                   arg_idx,
                      int                   begin_pos,
                      int                   end_pos,
                      enum KOS_VOID_INDEX_E void_index,
                      int                  *found_pos);

KOS_API
int KOS_extract_native_value(KOS_CONTEXT           ctx,
                             KOS_OBJ_ID            value_id,
                             const KOS_CONVERT    *convert,
                             struct KOS_MEMPOOL_S *alloc,
                             void                 *value_ptr);

KOS_API
int KOS_extract_native_from_array(KOS_CONTEXT           ctx,
                                  KOS_OBJ_ID            array_id,
                                  const char           *element_name,
                                  const KOS_CONVERT    *convert,
                                  struct KOS_MEMPOOL_S *alloc,
                                  ...);

KOS_API
int KOS_extract_native_from_iterable(KOS_CONTEXT           ctx,
                                     KOS_OBJ_ID            iterable_id,
                                     const KOS_CONVERT    *convert,
                                     struct KOS_MEMPOOL_S *alloc,
                                     ...);

KOS_API
int KOS_extract_native_from_object(KOS_CONTEXT           ctx,
                                   KOS_OBJ_ID            object_id,
                                   const KOS_CONVERT    *convert,
                                   struct KOS_MEMPOOL_S *alloc,
                                   ...);

KOS_API
int KOS_extract_native_struct_from_object(KOS_CONTEXT           ctx,
                                          KOS_OBJ_ID            object_id,
                                          const KOS_CONVERT    *convert,
                                          struct KOS_MEMPOOL_S *alloc,
                                          void                 *struct_ptr);

KOS_API
KOS_OBJ_ID KOS_new_from_native(KOS_CONTEXT        ctx,
                               const KOS_CONVERT *convert,
                               const void        *value_ptr);

KOS_API
int KOS_set_properties_from_native(KOS_CONTEXT        ctx,
                                   KOS_OBJ_ID         object_id,
                                   const KOS_CONVERT *convert,
                                   const void        *struct_ptr);

enum KOS_PRINT_WHERE_E {
    KOS_STDOUT,
    KOS_STDERR
};

KOS_API
void KOS_print_exception(KOS_CONTEXT ctx, enum KOS_PRINT_WHERE_E print_where);

typedef enum KOS_QUOTE_STR_E {
    KOS_DONT_QUOTE,
    KOS_QUOTE_STRINGS
} KOS_QUOTE_STR;

KOS_API
int KOS_object_to_string_or_cstr_vec(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_QUOTE_STR        quote_str,
                                     KOS_OBJ_ID          *str,
                                     struct KOS_VECTOR_S *cstr_vec);

KOS_API
KOS_OBJ_ID KOS_object_to_string(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  obj);

KOS_API
int KOS_print_to_cstr_vec(KOS_CONTEXT          ctx,
                          KOS_OBJ_ID           array,
                          KOS_QUOTE_STR        quote_str,
                          struct KOS_VECTOR_S *cstr_vec,
                          const char          *sep,
                          unsigned             sep_len);

KOS_API
int KOS_append_cstr(KOS_CONTEXT          ctx,
                    struct KOS_VECTOR_S *cstr_vec,
                    const char          *str,
                    size_t               len);

KOS_API
int KOS_array_push_expand(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  array,
                          KOS_OBJ_ID  value);

KOS_API
KOS_COMPARE_RESULT KOS_compare(KOS_OBJ_ID a,
                               KOS_OBJ_ID b);

KOS_API
KOS_OBJ_ID KOS_get_file_name(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  full_path);

KOS_API
int KOS_is_generator(KOS_OBJ_ID fun_obj, KOS_FUNCTION_STATE *fun_state);

#ifdef __GNUC__
#define KOS_CHECK_FORMAT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define KOS_CHECK_FORMAT(fmt_idx, arg_idx)
#endif

KOS_API
KOS_OBJ_ID KOS_string_printf(KOS_CONTEXT ctx,
                             const char *format,
                             ...) KOS_CHECK_FORMAT(2, 3);

KOS_API
void KOS_raise_printf(KOS_CONTEXT ctx,
                      const char *format,
                      ...) KOS_CHECK_FORMAT(2, 3);

KOS_API
void KOS_raise_errno(KOS_CONTEXT ctx, const char *prefix);

KOS_API
void KOS_raise_errno_value(KOS_CONTEXT ctx, const char *prefix, int error_value);

#ifdef _WIN32
KOS_API
void KOS_raise_last_error(KOS_CONTEXT ctx, const char *prefix, unsigned error_value);
#endif

KOS_API
int64_t KOS_fix_index(int64_t idx, unsigned length);

#ifdef __cplusplus
}
#endif

#endif

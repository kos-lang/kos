/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_UTILS_H_INCLUDED
#define KOS_UTILS_H_INCLUDED

#include "kos_entity.h"
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

struct KOS_VECTOR_S;

typedef enum KOS_COMPARE_RESULT_E {
    KOS_EQUAL,
    KOS_LESS_THAN,
    KOS_GREATER_THAN,
    KOS_INDETERMINATE
} KOS_COMPARE_RESULT;

#ifdef __cplusplus
extern "C" {
#endif

int KOS_get_numeric_arg(KOS_CONTEXT  ctx,
                        KOS_OBJ_ID   args_obj,
                        int          idx,
                        KOS_NUMERIC *numeric);

int KOS_get_integer(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t    *ret);

enum KOS_PRINT_WHERE_E {
    KOS_STDOUT,
    KOS_STDERR
};

void KOS_print_exception(KOS_CONTEXT ctx, enum KOS_PRINT_WHERE_E print_where);

typedef enum KOS_QUOTE_STR_E {
    KOS_DONT_QUOTE,
    KOS_QUOTE_STRINGS
} KOS_QUOTE_STR;

int KOS_object_to_string_or_cstr_vec(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_QUOTE_STR        quote_str,
                                     KOS_OBJ_ID          *str,
                                     struct KOS_VECTOR_S *cstr_vec);

KOS_OBJ_ID KOS_object_to_string(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  obj);

int KOS_print_to_cstr_vec(KOS_CONTEXT          ctx,
                          KOS_OBJ_ID           array,
                          KOS_QUOTE_STR        quote_str,
                          struct KOS_VECTOR_S *cstr_vec,
                          const char          *sep,
                          unsigned             sep_len);

int KOS_array_push_expand(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  array,
                          KOS_OBJ_ID  value);

KOS_COMPARE_RESULT KOS_compare(KOS_OBJ_ID a,
                               KOS_OBJ_ID b);

KOS_OBJ_ID KOS_get_file_name(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  full_path);

int KOS_is_generator(KOS_OBJ_ID fun_obj, KOS_FUNCTION_STATE *fun_state);

#ifdef __GNUC__
#define KOS_CHECK_FORMAT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define KOS_CHECK_FORMAT(fmt_idx, arg_idx)
#endif

KOS_OBJ_ID KOS_string_printf(KOS_CONTEXT ctx,
                             const char *format,
                             ...) KOS_CHECK_FORMAT(2, 3);

void KOS_raise_printf(KOS_CONTEXT ctx,
                      const char *format,
                      ...) KOS_CHECK_FORMAT(2, 3);

#ifdef __cplusplus
}
#endif

#endif

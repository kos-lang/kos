/*
 * Copyright (c) 2014-2018 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef KOS_UTILS_H_INCLUDED
#define KOS_UTILS_H_INCLUDED

#include "kos_object_base.h"
#include <stdint.h>

enum _KOS_NUMERIC_TYPE {
    KOS_NON_NUMERIC,
    KOS_INTEGER_VALUE,
    KOS_FLOAT_VALUE
};

union _KOS_NUMERIC_VALUE {
    int64_t i;
    double  d;
};

struct _KOS_NUMERIC {
    enum _KOS_NUMERIC_TYPE   type;
    union _KOS_NUMERIC_VALUE u;
};

typedef struct _KOS_NUMERIC KOS_NUMERIC;

struct KOS_VECTOR_S;

enum _KOS_COMPARE_RESULT {
    KOS_EQUAL,
    KOS_LESS_THAN,
    KOS_GREATER_THAN,
    KOS_INDETERMINATE
};

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

void KOS_print_exception(KOS_CONTEXT ctx);

enum _KOS_QUOTE_STR {
    KOS_DONT_QUOTE,
    KOS_QUOTE_STRINGS
};

int KOS_object_to_string_or_cstr_vec(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     enum _KOS_QUOTE_STR  quote_str,
                                     KOS_OBJ_ID          *str,
                                     struct KOS_VECTOR_S *cstr_vec);

KOS_OBJ_ID KOS_object_to_string(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  obj);

int KOS_print_to_cstr_vec(KOS_CONTEXT          ctx,
                          KOS_OBJ_ID           array,
                          enum _KOS_QUOTE_STR  quote_str,
                          struct KOS_VECTOR_S *cstr_vec,
                          const char          *sep,
                          unsigned             sep_len);

int KOS_array_push_expand(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  array,
                          KOS_OBJ_ID  value);

enum _KOS_COMPARE_RESULT KOS_compare(KOS_OBJ_ID a,
                                     KOS_OBJ_ID b);

KOS_OBJ_ID KOS_get_file_name(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  full_path);

#ifdef __cplusplus
}
#endif

#endif

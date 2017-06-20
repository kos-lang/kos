/*
 * Copyright (c) 2014-2017 Chris Dragan
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

#ifndef __KOS_UTILS_H
#define __KOS_UTILS_H

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

struct _KOS_VECTOR;

#ifdef __cplusplus
extern "C" {
#endif

int KOS_get_numeric_arg(KOS_FRAME    frame,
                        KOS_OBJ_ID   args_obj,
                        int          idx,
                        KOS_NUMERIC *numeric);

void KOS_print_exception(KOS_FRAME frame);

enum _KOS_QUOTE_STR {
    KOS_DONT_QUOTE,
    KOS_QUOTE_STRINGS
};

int KOS_object_to_string_or_cstr_vec(KOS_FRAME           frame,
                                     KOS_OBJ_ID          obj,
                                     enum _KOS_QUOTE_STR quote_str,
                                     KOS_OBJ_ID         *str,
                                     struct _KOS_VECTOR *cstr_vec);

KOS_OBJ_ID KOS_object_to_string(KOS_FRAME  frame,
                                KOS_OBJ_ID obj);

int KOS_print_to_cstr_vec(KOS_FRAME           frame,
                          KOS_OBJ_ID          array,
                          enum _KOS_QUOTE_STR quote_str,
                          struct _KOS_VECTOR *cstr_vec,
                          const char         *sep,
                          unsigned            sep_len);

#ifdef __cplusplus
}
#endif

#endif

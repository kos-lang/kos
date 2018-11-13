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

#ifndef __KOS_STRING_H
#define __KOS_STRING_H

#include "kos_object_base.h"
#include <assert.h>
#include <stddef.h>

struct _KOS_VECTOR;

#ifdef __cplusplus

static inline unsigned KOS_get_string_length(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
    return OBJPTR(STRING, obj_id)->header.length;
}

#else

#define KOS_get_string_length(obj_id) (OBJPTR(STRING, (obj_id))->header.length)

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_cstring(KOS_CONTEXT ctx,
                           const char *utf8_str);

KOS_OBJ_ID KOS_new_string(KOS_CONTEXT ctx,
                          const char *utf8_str,
                          unsigned    length);

KOS_OBJ_ID KOS_new_string_esc(KOS_CONTEXT ctx,
                              const char *utf8_str,
                              unsigned    length);

KOS_OBJ_ID KOS_new_const_ascii_cstring(KOS_CONTEXT ctx,
                                       const char *ascii_str);

KOS_OBJ_ID KOS_new_const_ascii_string(KOS_CONTEXT ctx,
                                      const char *ascii_str,
                                      unsigned    length);

KOS_OBJ_ID KOS_new_const_string(KOS_CONTEXT            ctx,
                                const void            *str_data,
                                unsigned               length,
                                enum _KOS_STRING_FLAGS elem_size);

KOS_OBJ_ID KOS_new_string_from_codes(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  codes);

unsigned KOS_string_to_utf8(KOS_OBJ_ID obj_id,
                            void      *buf,
                            unsigned   buf_size);

int KOS_string_to_cstr_vec(KOS_CONTEXT         ctx,
                           KOS_OBJ_ID          obj_id,
                           struct _KOS_VECTOR *str_vec);

uint32_t KOS_string_get_hash(KOS_OBJ_ID obj_id);

KOS_OBJ_ID KOS_string_add_n(KOS_CONTEXT ctx,
                            KOS_OBJ_ID *str_id_array,
                            unsigned    num_strings);

KOS_OBJ_ID KOS_string_add(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  str_array_id);

KOS_OBJ_ID KOS_string_slice(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            int64_t     begin,
                            int64_t     end);

KOS_OBJ_ID KOS_string_get_char(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id,
                               int         idx);

unsigned KOS_string_get_char_code(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id,
                                  int         idx);

int KOS_string_compare(KOS_OBJ_ID obj_id_a,
                       KOS_OBJ_ID obj_id_b);

int KOS_string_compare_slice(KOS_OBJ_ID obj_id_a,
                             int64_t    a_begin,
                             int64_t    a_end,
                             KOS_OBJ_ID obj_id_b,
                             int64_t    b_begin,
                             int64_t    b_end);

enum _KOS_FIND_DIR {
    KOS_FIND_FORWARD,
    KOS_FIND_REVERSE
};

/* *pos contains starting search position on input and found position on output.
 * If pattern is not found, returns KOS_SUCCESS and sets *pos to -1. */
int KOS_string_find(KOS_CONTEXT        ctx,
                    KOS_OBJ_ID         obj_id_text,
                    KOS_OBJ_ID         obj_id_pattern,
                    enum _KOS_FIND_DIR reverse,
                    int               *pos);

enum _KOS_SCAN_INCLUDE {
    KOS_SCAN_EXCLUDE,
    KOS_SCAN_INCLUDE
};

/* *pos contains starting search position on input and found position on output.
 * If pattern is not found, returns KOS_SUCCESS and sets *pos to -1. */
int KOS_string_scan(KOS_CONTEXT            ctx,
                    KOS_OBJ_ID             obj_id_text,
                    KOS_OBJ_ID             obj_id_pattern,
                    enum _KOS_FIND_DIR     reverse,
                    enum _KOS_SCAN_INCLUDE include,
                    int                   *pos);

KOS_OBJ_ID KOS_string_reverse(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id);

KOS_OBJ_ID KOS_string_repeat(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id,
                             unsigned    num_repeat);

#ifdef __cplusplus
}
#endif

#endif

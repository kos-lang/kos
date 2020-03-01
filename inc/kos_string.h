/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#ifndef KOS_STRING_H_INCLUDED
#define KOS_STRING_H_INCLUDED

#include "kos_entity.h"
#include <assert.h>
#include <stddef.h>

struct KOS_VECTOR_S;
struct KOS_LOCAL_S;

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

KOS_OBJ_ID KOS_new_const_string(KOS_CONTEXT      ctx,
                                const void      *str_data,
                                unsigned         length,
                                KOS_STRING_FLAGS elem_size);

KOS_OBJ_ID KOS_new_string_from_codes(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  codes);

KOS_OBJ_ID KOS_new_string_from_buffer(KOS_CONTEXT ctx,
                                      KOS_OBJ_ID  utf8_buf,
                                      unsigned    begin,
                                      unsigned    end);

unsigned KOS_string_to_utf8(KOS_OBJ_ID obj_id,
                            void      *buf,
                            unsigned   buf_size);

int KOS_string_to_cstr_vec(KOS_CONTEXT          ctx,
                           KOS_OBJ_ID           obj_id,
                           struct KOS_VECTOR_S *str_vec);

uint32_t KOS_string_get_hash(KOS_OBJ_ID obj_id);

KOS_OBJ_ID KOS_string_add_n(KOS_CONTEXT         ctx,
                            struct KOS_LOCAL_S *str_array,
                            unsigned            num_strings);

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

enum KOS_FIND_DIR_E {
    KOS_FIND_FORWARD,
    KOS_FIND_REVERSE
};

/* *pos contains starting search position on input and found position on output.
 * If pattern is not found, returns KOS_SUCCESS and sets *pos to -1. */
int KOS_string_find(KOS_CONTEXT         ctx,
                    KOS_OBJ_ID          obj_id_text,
                    KOS_OBJ_ID          obj_id_pattern,
                    enum KOS_FIND_DIR_E reverse,
                    int                *pos);

enum KOS_SCAN_INCLUDE_E {
    KOS_SCAN_EXCLUDE,
    KOS_SCAN_INCLUDE
};

/* *pos contains starting search position on input and found position on output.
 * If pattern is not found, returns KOS_SUCCESS and sets *pos to -1. */
int KOS_string_scan(KOS_CONTEXT             ctx,
                    KOS_OBJ_ID              obj_id_text,
                    KOS_OBJ_ID              obj_id_pattern,
                    enum KOS_FIND_DIR_E     reverse,
                    enum KOS_SCAN_INCLUDE_E include,
                    int                    *pos);

KOS_OBJ_ID KOS_string_reverse(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id);

KOS_OBJ_ID KOS_string_repeat(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id,
                             unsigned    num_repeat);

KOS_OBJ_ID KOS_string_lowercase(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id);

KOS_OBJ_ID KOS_string_uppercase(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id);

#ifdef __cplusplus
}
#endif

#endif

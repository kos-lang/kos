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
    return OBJPTR(STRING, obj_id)->length;
}

#else

#define KOS_get_string_length(obj_id) (OBJPTR(STRING, (obj_id))->length)

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_cstring(KOS_FRAME   frame,
                           const char *utf8_str);

KOS_OBJ_ID KOS_new_string(KOS_FRAME   frame,
                          const char *utf8_str,
                          unsigned    length);

KOS_OBJ_ID KOS_new_string_esc(KOS_FRAME   frame,
                              const char *utf8_str,
                              unsigned    length);

KOS_OBJ_ID KOS_new_const_ascii_cstring(KOS_FRAME   frame,
                                       const char *ascii_str);

KOS_OBJ_ID KOS_new_const_ascii_string(KOS_FRAME   frame,
                                      const char *ascii_str,
                                      unsigned    length);

KOS_OBJ_ID KOS_new_const_string(KOS_FRAME                  frame,
                                const void                *str,
                                unsigned                   length,
                                enum _KOS_STRING_ELEM_SIZE elem_size);

KOS_OBJ_ID KOS_new_string_from_codes(KOS_FRAME  frame,
                                     KOS_OBJ_ID codes);

unsigned KOS_string_to_utf8(KOS_OBJ_ID obj_id,
                            void      *buf,
                            unsigned   buf_size);

int KOS_string_to_cstr_vec(KOS_FRAME           frame,
                           KOS_OBJ_ID          obj_id,
                           struct _KOS_VECTOR *str_vec);

KOS_OBJ_ID KOS_string_add(KOS_FRAME  frame,
                          KOS_OBJ_ID obj_id_a,
                          KOS_OBJ_ID obj_id_b);

KOS_OBJ_ID KOS_string_add_many(KOS_FRAME               frame,
                               KOS_ATOMIC(KOS_OBJ_ID) *obj_id_array,
                               unsigned                num_strings);

KOS_OBJ_ID KOS_string_slice(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id,
                            int64_t    idx_a,
                            int64_t    idx_b);

KOS_OBJ_ID KOS_string_get_char(KOS_FRAME  frame,
                               KOS_OBJ_ID obj_id,
                               int        idx);

unsigned KOS_string_get_char_code(KOS_FRAME  frame,
                                  KOS_OBJ_ID obj_id,
                                  int        idx);

int KOS_string_compare(KOS_OBJ_ID obj_id_a,
                       KOS_OBJ_ID obj_id_b);

uint32_t KOS_string_get_hash(KOS_OBJ_ID obj_id);

#ifdef __cplusplus
}
#endif

#endif

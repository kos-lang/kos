/*
 * Copyright (c) 2014-2016 Chris Dragan
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

static inline unsigned KOS_get_string_length(KOS_OBJ_PTR objptr)
{
    assert( ! IS_SMALL_INT(objptr) && ! IS_BAD_PTR(objptr));
    assert(IS_STRING_OBJ(objptr));
    return OBJPTR(KOS_STRING, objptr)->length;
}

#else

#define KOS_get_string_length(objptr) (OBJPTR(KOS_STRING, (objptr))->length)

#endif

#ifdef KOS_CPP11

#define KOS_ASCII_STRING(var, s) KOS_STRING var(s, sizeof(s)-1)

#define KOS_init_const_ascii_string(obj, s) _KOS_init_const_ascii_string(obj, s, sizeof(s)-1)

static inline void _KOS_init_const_ascii_string(KOS_STRING* obj, const char* s, size_t len)
{
    obj->type     = OBJ_STRING_8;
    obj->flags    = KOS_STRING_PTR;
    obj->length   = static_cast<uint16_t>(len);
    obj->hash     = 0;
    obj->data.ptr = s;
}

#else

#define KOS_ASCII_STRING(var, s) KOS_STRING var = { OBJ_STRING_8, KOS_STRING_PTR, sizeof(s)-1, 0, { s } }

#define KOS_init_const_ascii_string(obj, s) do { \
    (obj)->type     = OBJ_STRING_8;                \
    (obj)->flags    = KOS_STRING_PTR;              \
    (obj)->length   = (uint32_t)(sizeof(s)-1);     \
    (obj)->hash     = 0;                           \
    (obj)->data.ptr = s;                           \
}                                                \
while (0)

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_PTR KOS_new_cstring(KOS_CONTEXT *ctx,
                            const char  *utf8_str);

KOS_OBJ_PTR KOS_new_string(KOS_CONTEXT *ctx,
                           const char  *utf8_str,
                           unsigned     length);

KOS_OBJ_PTR KOS_new_const_ascii_cstring(KOS_CONTEXT *ctx,
                                        const char  *ascii_str);

KOS_OBJ_PTR KOS_new_const_ascii_string(KOS_CONTEXT *ctx,
                                       const char  *ascii_str,
                                       unsigned     length);

KOS_OBJ_PTR KOS_new_const_string(KOS_CONTEXT         *ctx,
                                 const void          *str,
                                 unsigned             length,
                                 enum KOS_OBJECT_TYPE type);

unsigned KOS_string_to_utf8(KOS_OBJ_PTR objptr,
                            void       *buf,
                            unsigned    buf_size);

int KOS_string_to_cstr_vec(KOS_CONTEXT        *ctx,
                           KOS_OBJ_PTR         objptr,
                           struct _KOS_VECTOR *str_vec);

KOS_OBJ_PTR KOS_string_add(KOS_CONTEXT *ctx,
                           KOS_OBJ_PTR  objptr_a,
                           KOS_OBJ_PTR  objptr_b);

KOS_OBJ_PTR KOS_string_add_many(KOS_CONTEXT             *ctx,
                                KOS_ATOMIC(KOS_OBJ_PTR) *objptr_array,
                                unsigned                 num_strings);

KOS_OBJ_PTR KOS_string_slice(KOS_CONTEXT *ctx,
                             KOS_OBJ_PTR  objptr,
                             int64_t      idx_a,
                             int64_t      idx_b);

KOS_OBJ_PTR KOS_string_get_char(KOS_CONTEXT *ctx,
                                KOS_OBJ_PTR  objptr,
                                int          idx);

unsigned KOS_string_get_char_code(KOS_CONTEXT *ctx,
                                  KOS_OBJ_PTR  objptr,
                                  int          idx);

int KOS_string_compare(KOS_OBJ_PTR objptr_a,
                       KOS_OBJ_PTR objptr_b);

uint32_t KOS_string_get_hash(KOS_OBJ_PTR objptr);

KOS_OBJ_PTR KOS_object_to_string(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  obj);

#ifdef __cplusplus
}
#endif

#endif

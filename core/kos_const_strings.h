/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#ifndef KOS_CONST_STRINGS_H_INCLUDED
#define KOS_CONST_STRINGS_H_INCLUDED

#include "../inc/kos_object_base.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct KOS_CONST_STRING_S KOS_str_empty;
extern struct KOS_CONST_STRING_S KOS_str_out_of_memory;

#ifdef __cplusplus
}
#endif

#define KOS_STR_EMPTY         KOS_CONST_ID(KOS_str_empty)
#define KOS_STR_OUT_OF_MEMORY KOS_CONST_ID(KOS_str_out_of_memory)

#endif
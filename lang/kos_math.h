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

#ifndef __KOS_MATH_H
#define __KOS_MATH_H

#ifdef __cplusplus

#include <assert.h>

template<typename T>
static inline T KOS_min(T a, T b)
{
    return (a < b) ? a : b;
}

template<typename T>
static inline T KOS_max(T a, T b)
{
    return (a > b) ? a : b;
}

template<typename T>
static inline bool KOS_is_power_of_2(T step)
{
    while (step && ! ((step & 1))) {
        step >>= 1;
    }
    return step == 1;
}

template<typename T>
static inline T KOS_align_up(T value, T step)
{
    assert(KOS_is_power_of_2(step));
    return (value + step - 1) & ~(step - 1);
}

#else

#define KOS_min(a, b)             ((a) < (b) ? (a) : (b))
#define KOS_max(a, b)             ((a) > (b) ? (a) : (b))
#define KOS_align_up(value, step) (((value) + (step) - 1) & ~((step) - 1))

#endif

#endif

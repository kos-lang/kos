/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_MATH_H_INCLUDED
#define KOS_MATH_H_INCLUDED

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
    if (step == 0)
        return false;
    if (step == 1)
        return true;
    const T mask = ~((step - 1) << 1);
    return (step & mask) == 0;
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

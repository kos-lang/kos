/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_DEFS_H_INCLUDED
#define KOS_DEFS_H_INCLUDED

/* Detect C++11 support */
#if defined(__cplusplus)
#   if ! defined(KOS_CPP11)
#       if __cplusplus >= 201103L
#           define KOS_CPP11 1
#       elif defined(_MSC_VER) && _MSC_VER >= 1900
#           define KOS_CPP11 1
#       endif
#   endif
#else
#   if defined(KOS_CPP11)
#       undef KOS_CPP11
#   endif
#endif

#ifdef KOS_CPP11
#   define KOS_NULL nullptr
#else
#   define KOS_NULL 0
#endif

#endif

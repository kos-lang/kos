/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef MODULE_DEF
#error "MODULE_DEF is not defined"
#endif

#ifdef KOS_EXTERNAL_MODULES
#   define EXTERN_DEF(module, flags)
#else
#   define EXTERN_DEF(module, flags) MODULE_DEF(module, flags)
#endif

MODULE_DEF(base,     KOS_MODULE_NEEDS_KOS_SOURCE)
MODULE_DEF(datetime, KOS_MODULE_NEEDS_KOS_SOURCE)
MODULE_DEF(fs,       0)
MODULE_DEF(io,       KOS_MODULE_NEEDS_KOS_SOURCE)
MODULE_DEF(kos,      KOS_MODULE_NEEDS_KOS_SOURCE)
EXTERN_DEF(math,     KOS_MODULE_NEEDS_KOS_SOURCE)
EXTERN_DEF(os,       0)
MODULE_DEF(random,   KOS_MODULE_NEEDS_KOS_SOURCE)
EXTERN_DEF(re,       KOS_MODULE_NEEDS_KOS_SOURCE)
MODULE_DEF(threads,  KOS_MODULE_NEEDS_KOS_SOURCE)

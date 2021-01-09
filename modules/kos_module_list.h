/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef MODULE_DEF
#error "MODULE_DEF is not defined"
#endif

#ifdef KOS_EXTERNAL_MODULES
#   define EXTERN_DEF(x)
#else
#   define EXTERN_DEF(x) MODULE_DEF(x)
#endif

MODULE_DEF(base)
MODULE_DEF(datetime)
MODULE_DEF(fs)
MODULE_DEF(gc)
MODULE_DEF(io)
MODULE_DEF(kos)
EXTERN_DEF(math)
EXTERN_DEF(os)
MODULE_DEF(random)
EXTERN_DEF(re)

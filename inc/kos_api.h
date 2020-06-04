/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_API_H_INCLUDED
#define KOS_API_H_INCLUDED

#ifdef _WIN32
#   define KOS_EXPORT_SYMBOL __declspec(dllexport)
#   define KOS_IMPORT_SYMBOL __declspec(dllimport)
#elif defined(__GNUC__)
#   define KOS_EXPORT_SYMBOL __attribute__((visibility("default")))
#   define KOS_IMPORT_SYMBOL
#else
#   define KOS_EXPORT_SYMBOL
#   define KOS_IMPORT_SYMBOL
#endif

#ifdef KOS_PUBLIC_API
#   define KOS_API KOS_IMPORT_SYMBOL
#else
#   define KOS_API KOS_EXPORT_SYMBOL
#endif

#if !defined(_WIN32) || !defined(KOS_PUBLIC_API)
#   define KOS_API_VAR_DEF
#else
#   define KOS_API_VAR_DEF KOS_API
#endif

#endif

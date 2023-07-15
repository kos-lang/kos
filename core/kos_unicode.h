/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_UNICODE_H_INCLUDED
#define KOS_UNICODE_H_INCLUDED

#include <stdint.h>

#ifdef CONFIG_HAS_UNICODE

int      kos_unicode_compare(uint32_t a, uint32_t b);
int      kos_unicode_icompare(uint32_t a, uint32_t b);
uint32_t kos_unicode_to_upper(uint32_t c);
uint32_t kos_unicode_to_lower(uint32_t c);

#else

#include <wchar.h>
#include <wctype.h>

#if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)) || defined(__cplusplus)
#define INLINE inline
#else
#define INLINE
#endif

static INLINE int kos_unicode_compare(uint32_t a, uint32_t b)
{
    wchar_t wa[4] = { 0 };
    wchar_t wb[4] = { 0 };
    wa[0] = (wchar_t)a;
    wb[0] = (wchar_t)b;
    return wcscoll(wa, wb);
}

#define kos_unicode_to_upper(c) ((uint32_t)towupper(c))

#define kos_unicode_to_lower(c) ((uint32_t)towlower(c))

#define kos_unicode_icompare(a, b) (kos_unicode_compare(kos_unicode_to_lower(a), kos_unicode_to_lower(b)))

#endif

#endif

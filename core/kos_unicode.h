/*
 * Copyright (c) 2014-2020 Chris Dragan
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

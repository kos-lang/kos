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

#ifndef __KOS_UTF8_H
#define __KOS_UTF8_H

#include <stdint.h>

enum _KOS_ESCAPE_TYPE {
    KOS_ET_INVALID     = 0,
    KOS_ET_INTERPOLATE = 40,
    KOS_ET_HEX         = 120
};

extern const char _KOS_escape_sequence_map[256];

enum _KOS_UTF8_ESCAPE {
    KOS_UTF8_NO_ESCAPE,
    KOS_UTF8_WITH_ESCAPE
};

unsigned _KOS_utf8_get_len(const char           *str,
                           unsigned              length,
                           enum _KOS_UTF8_ESCAPE escape,
                           uint32_t             *max_code);

int _KOS_utf8_decode_8(const char           *str,
                       unsigned              length,
                       enum _KOS_UTF8_ESCAPE escape,
                       uint8_t              *out);

int _KOS_utf8_decode_16(const char           *str,
                        unsigned              length,
                        enum _KOS_UTF8_ESCAPE escape,
                        uint16_t             *out);

int _KOS_utf8_decode_32(const char           *str,
                        unsigned              length,
                        enum _KOS_UTF8_ESCAPE escape,
                        uint32_t             *out);

unsigned _KOS_utf8_calc_buf_size_8(const uint8_t *buf, unsigned length);

unsigned _KOS_utf8_calc_buf_size_16(const uint16_t *buf, unsigned length);

unsigned _KOS_utf8_calc_buf_size_32(const uint32_t *buf, unsigned length);

void _KOS_utf8_encode_8(const uint8_t *str,
                        unsigned       length,
                        uint8_t        *out);

void _KOS_utf8_encode_16(const uint16_t *str,
                         unsigned        length,
                         uint8_t         *out);

void _KOS_utf8_encode_32(const uint32_t *str,
                         unsigned        length,
                         uint8_t         *out);

#endif

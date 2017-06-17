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

#include "kos_utf8.h"
#include "../inc/kos_error.h"
#include <assert.h>

static const uint8_t _utf8_len[256] = {
    /* 0 .. 127 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 128 .. 191 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 .. 223 */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 224 .. 239 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 240 .. 247 */
    4, 4, 4, 4, 4, 4, 4, 4,
    /* 148 .. 255 */
    0, 0, 0, 0, 0, 0, 0, 0
};

const char _KOS_escape_sequence_map[256] = {
    /* 0..15 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 16..31 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 32..47 */
    0, 0, '"', 0, 0, 0, 0, 0, KOS_ET_INTERPOLATE, 0, 0, 0, 0, 0, 0, 0,
    /* 48..63 */
    '0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 64..79 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 80..95 */
    0, 0, 0, 0, 0, 0, 0, 0, KOS_ET_HEX, 0, 0, 0, '\\', 0, 0, 0,
    /* 96..111 */
    0, 0, 0, 0, 0, 0, '\f', 0, 0, 0, 0, 0, 0, 0, '\n', 0,
    /* 112..127 */
    0, 0, '\r', 0, '\t', 'u', '\v', 0, KOS_ET_HEX, 0, 0, 0, 0, 0, 0, 0,
    /* 128..255 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char _hex_map[256] = {
    /* 0..15 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 16..31 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 32..47 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 48..63 */
     0,  1,  2,  3,  4,  5,  6,  7,  8, 16, 16, 16, 16, 16, 16, 16,
    /* 64..79 */
    16, 10, 11, 12, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 80..95 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 96..111 */
    16, 10, 11, 12, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 112..127 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 128..255 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
};

#define KOS_INVALID_ESC (~0U)
#define KOS_NOT_ESC     (~0U - 1U)

static uint32_t _parse_escape_sequence(const char **str_ptr,
                                       const char  *end)
{
    const char *str  = *str_ptr;
    uint32_t    code = KOS_INVALID_ESC;

    if (str < end) {
        const char esc_type = _KOS_escape_sequence_map[(uint8_t)*(str++)];

        if (esc_type == KOS_ET_HEX) {
            if (str + 2 <= end) {
                if (*str == '{' && str[1] != '}') {
                    uint32_t gather = 0;
                    for (++str; ; ) {
                        const char c = *(str++);
                        uint32_t   v;
                        if (c == '}') {
                            code = gather;
                            break;
                        }
                        if (str >= end)
                            break;
                        v = (uint32_t)_hex_map[(uint8_t)c];
                        if (v > 15)
                            break;
                        gather = (gather << 4) | v;
                        if (gather > 0x00FFFFFFU)
                            break;
                    }
                }
                else {
                    const unsigned hi = (uint8_t)_hex_map[(uint8_t)*(str++)];
                    const unsigned lo = (uint8_t)_hex_map[(uint8_t)*(str++)];
                    if (lo < 16 && hi < 16)
                        code = (hi << 4) | lo;
                }
            }
        }
        else if (esc_type == '0')
            code = 0;
        else if (esc_type == KOS_ET_INVALID || esc_type == KOS_ET_INTERPOLATE) {
            --str;
            code = KOS_NOT_ESC;
        }
        else
            code = (uint8_t)esc_type;
    }

    *str_ptr = str;

    return code;
}

unsigned _KOS_utf8_get_len(const char           *str,
                           unsigned              length,
                           enum _KOS_UTF8_ESCAPE escape,
                           uint32_t             *max_code)
{
    const char *const end   = str + length;
    unsigned          count = 0;
    uint32_t          max_c = 0;

    for ( ; str < end; ++count) {
        const uint8_t c = (uint8_t)*(str++);
        uint32_t      code;

        if (c < 0x80)
            code = c;
        else {

            unsigned code_len = _utf8_len[c];

            if (code_len == 0 || str + code_len - 1 > end) {
                count = ~0U;
                break;
            }

            code = c & ((0x80U >> code_len) - 1);
            for (--code_len; code_len; --code_len) {
                const uint32_t next_c = (uint8_t)*(str++);
                code = (code << 6) | (next_c & 0x3FU);
            }

            if (code > max_c)
                max_c = code;
        }

        if (escape && code == '\\') {
            code = _parse_escape_sequence(&str, end);

            if (code == KOS_INVALID_ESC) {
                count = ~0U;
                break;
            }

            if (code != KOS_NOT_ESC && code > max_c)
                max_c = code;
        }
    }

    if (str != end)
        count = ~0U;

    *max_code = max_c;

    return count;
}

int _KOS_utf8_decode_8(const char *str, unsigned length, enum _KOS_UTF8_ESCAPE escape, uint8_t *out)
{
    const char *const end   = str + length;
    int               error = KOS_SUCCESS;

    while (str < end) {
        uint8_t c = (uint8_t)*(str++);

        if (c > 0x7F) {
            int code_len = _utf8_len[c];

            c &= (0x80U >> code_len) - 1;
            --code_len;

            do {
                const uint8_t next = (uint8_t)*(str++);
                if ((next & 0xC0U) != 0x80U) {
                    error = KOS_ERROR_INVALID_UTF8_CHARACTER;
                    str   = end;
                    break;
                }
                c = (c << 6) | (next & 0x3FU);
            }
            while (--code_len);
        }

        if (escape && c == '\\') {
            const uint32_t c32 = _parse_escape_sequence(&str, end);
            if (c32 != KOS_NOT_ESC) {
                assert(c32 < 0x100U);
                c = (uint8_t)c32;
            }
        }

        *(out++) = c;
    }

    return error;
}

int _KOS_utf8_decode_16(const char *str, unsigned length, enum _KOS_UTF8_ESCAPE escape, uint16_t *out)
{
    const char *const end   = str + length;
    int               error = KOS_SUCCESS;

    while (str < end) {
        uint16_t c = (uint8_t)*(str++);

        if (c > 0x7F) {
            int code_len = _utf8_len[c];

            c &= (0x80U >> code_len) - 1;
            --code_len;

            do {
                const uint16_t next = (uint8_t)*(str++);
                if ((next & 0xC0U) != 0x80U) {
                    error = KOS_ERROR_INVALID_UTF8_CHARACTER;
                    str   = end;
                    break;
                }
                c = (c << 6) | (next & 0x3FU);
            }
            while (--code_len);
        }

        if (escape && c == '\\') {
            const uint32_t c32 = _parse_escape_sequence(&str, end);
            if (c32 != KOS_NOT_ESC) {
                assert(c32 < 0x10000U);
                c = (uint16_t)c32;
            }
        }

        *(out++) = c;
    }

    return error;
}

int _KOS_utf8_decode_32(const char *str, unsigned length, enum _KOS_UTF8_ESCAPE escape, uint32_t *out)
{
    const char *const end   = str + length;
    int               error = KOS_SUCCESS;

    while (str < end) {
        uint32_t c = (uint8_t)*(str++);

        if (c > 0x7F) {
            int code_len = _utf8_len[c];

            c &= (0x80U >> code_len) - 1;
            --code_len;

            do {
                const uint32_t next = (uint8_t)*(str++);
                if ((next & 0xC0U) != 0x80U) {
                    error = KOS_ERROR_INVALID_UTF8_CHARACTER;
                    str   = end;
                    break;
                }
                c = (c << 6) | (next & 0x3F);
            }
            while (--code_len);
        }

        if (escape && c == '\\') {
            const uint32_t c32 = _parse_escape_sequence(&str, end);
            if (c32 != KOS_NOT_ESC) {
                assert(c32 != KOS_INVALID_ESC);
                c = c32;
            }
        }

        *(out++) = c;
    }

    return error;
}

unsigned _KOS_utf8_calc_buf_size_8(const uint8_t *buf, unsigned length)
{
    const uint8_t *pend = buf + length;
    unsigned       size = 0;

    while (buf != pend) {
        const uint8_t code = *(buf++);
        size += (code & 0x80U) ? 2 : 1;
    }

    return size;
}

unsigned _KOS_utf8_calc_buf_size_16(const uint16_t *buf, unsigned length)
{
    const uint16_t *pend = buf + length;
    unsigned        size = 0;

    while (buf != pend) {
        const uint16_t code = *(buf++);
        if (code < (1U << 7))
            ++size;
        else if (code < (1U << (5+6)))
            size += 2;
        else
            size += 3;
    }

    return size;
}

unsigned _KOS_utf8_calc_buf_size_32(const uint32_t *buf, unsigned length)
{
    const uint32_t *pend = buf + length;
    unsigned        size = 0;

    while (buf != pend) {
        const uint32_t code = *(buf++);
        if (code < (1U << 7))
            ++size;
        else if (code < (1U << (5+6)))
            size += 2;
        else if (code < (1U << (4+6+6)))
            size += 3;
        else if (code < (1U << (3+6+6+6)))
            size += 4;
        else {
            size = ~0U;
            break;
        }
    }

    return size;
}

void _KOS_utf8_encode_8(const uint8_t *str, unsigned length, uint8_t *out)
{
    const uint8_t *end = str + length;

    while (str != end) {
        const uint8_t code = *(str++);
        if ((code & 0x80U)) {
            *(out++) = (uint8_t)(0xC0U | (code >> 6));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
        else
            *(out++) = (uint8_t)code;
    }
}

void _KOS_utf8_encode_16(const uint16_t *str, unsigned length, uint8_t *out)
{
    const uint16_t *end = str + length;

    while (str != end) {
        const uint16_t code = *(str++);
        if (code < (1U << 7))
            *(out++) = (uint8_t)code;
        else if (code < (1U << (5+6))) {
            *(out++) = (uint8_t)(0xC0U | (code >> 6));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
        else {
            *(out++) = (uint8_t)(0xE0U | (code >> (6+6)));
            *(out++) = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
    }
}

void _KOS_utf8_encode_32(const uint32_t *str, unsigned length, uint8_t *out)
{
    const uint32_t *end = str + length;

    while (str != end) {
        const uint32_t code = *(str++);
        if (code < (1U << 7))
            *(out++) = (uint8_t)code;
        else if (code < (1U << (5+6))) {
            *(out++) = (uint8_t)(0xC0U | (code >> 6));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
        else if (code < (1U << (4+6+6))) {
            *(out++) = (uint8_t)(0xE0U | (code >> (6+6)));
            *(out++) = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
        else {
            *(out++) = (uint8_t)(0xF0U | (code >> (6+6+6)));
            *(out++) = (uint8_t)(0x80U | ((code >> (6+6)) & 0x3FU));
            *(out++) = (uint8_t)(0x80U | ((code >> 6) & 0x3FU));
            *(out++) = (uint8_t)(0x80U | (code & 0x3FU));
        }
    }
}

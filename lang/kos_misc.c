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

#include "kos_misc.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>

int _KOS_is_integer(const char *begin,
                    const char *end)
{
    int ret = 1;

    for ( ; begin < end; begin++) {
        const char c = *begin;
        if (c == 'x' || c == 'X' || c == 'b' || c == 'B')
            break;
        if (c == '.' || c == 'e' || c == 'E') {
            ret = 0;
            break;
        }
    }

    return ret;
}

int _KOS_parse_int(const char *begin,
                   const char *end,
                   int64_t    *value)
{
    int         error = KOS_SUCCESS;
    const char *s;
    const int   minus = (begin < end && *begin == '-') ? 1 : 0;
    int         radix = 10;
    int         idx;

    if (minus)
        ++begin;
    else if (begin < end && *begin == '+')
        ++begin;

    if (begin + 2 < end && begin[0] == '0') {

        const char t = begin[1];

        if (t == 'x' || t == 'X')
            radix = 16;
        else if (t == 'b' || t == 'B')
            radix = 2;
    }

    if (radix == 16) {

        idx = 2 + minus;

        begin += 2;

        for (s = begin; s < end; ++s) {
            const char c = *s;
            if ((c < '0' || c > '9') &&
                (c < 'a' || c > 'f') &&
                (c < 'A' || c > 'F'))
                break;
        }
    }
    else if (radix == 2) {

        idx = 4 + minus;

        begin += 2;

        for (s = begin; s < end; ++s) {
            const char c = *s;
            if (c != '0' && c != '1')
                break;
        }
    }
    else {

        idx = minus;

        for (s = begin; s < end; ++s) {
            const char c = *s;
            if (c < '0' || c > '9')
                break;
        }
    }

    if (s != end || begin == end)
        error = KOS_ERROR_INTEGER_EXPECTED;

    else {

        uint64_t       v      = 0;
        const uint64_t vmax[] = { ~(uint64_t)0U/20U,      /* 9223372036854775807 / 10 */
                                  ~(uint64_t)0U/20U,      /* 9223372036854775808 / 10 */
                                  ~(uint64_t)0U>>4,       /* 0xFFFFFFFFFFFFFFFF >> 4 */
                                  ~(uint64_t)0U>>4,       /* 0xFFFFFFFFFFFFFFFF >> 4 */
                                  ~(uint64_t)0U>>1,       /* 0x7FFFFFFFFFFFFFFF */
                                  ~(uint64_t)0U>>1 };     /* 0x7FFFFFFFFFFFFFFF */
        const unsigned dmax[] = { 7U, 8U, 15U, 15U, 1U, 1U };

        for (s = begin; s < end; ++s) {

            const char c = *s;
            const unsigned digit = (unsigned)((c <= '9') ? (c - '0') :
                                              (c < 'a')  ? (c - 'A' + 10) : (c - 'a' + 10));
            if (v > vmax[idx]) {
                error = KOS_ERROR_INTEGER_EXPECTED;
                break;
            }
            else if (v == vmax[idx]) {
                if (digit > dmax[idx]) {
                    error = KOS_ERROR_INTEGER_EXPECTED;
                    break;
                }
            }
            v = v * radix + digit;
        }

        if (!error)
            *value = minus ? -(int64_t)v : (int64_t)v;
    }

    return error;
}

static void _multiply_by_10(uint64_t *mantissa, int *exponent)
{
    const uint64_t high  = 5 * (*mantissa >> 3);
    const unsigned low   = 5 * ((unsigned)*mantissa & 7U);
    const unsigned carry = (low >> 2) & 1;

    *mantissa = high + (low >> 3) + carry;
    *exponent += 4;
}

static void _divide_by_10(uint64_t *mantissa, int *exponent)
{
    *mantissa /= 5;
    --*exponent;
}

static void _renormalize(uint64_t *mantissa, int *exponent)
{
    while ( ! ((*mantissa & ((uint64_t)1U << 63) )) ) {
        *mantissa <<= 1;
        --*exponent;
    }
}

int _KOS_parse_double(const char *begin,
                      const char *end,
                      double     *value)
{
    int      error             = KOS_SUCCESS;
    int      sign              = 0;
    int      exponent          = 0;
    uint64_t mantissa          = 0;
    int      decimal_exponent  = 0;
    int      had_decimal_point = 0;

    assert(begin <= end);

    if (begin == end)
        TRY(KOS_ERROR_INVALID_NUMBER);

    /* Parse sign */
    if (*begin == '-') {
        sign = 1;
        ++begin;
    }

    if (begin == end)
        TRY(KOS_ERROR_INVALID_NUMBER);

    /* Discard leading zeroes */
    while (begin < end && *begin == '0')
        ++begin;

    /* Parse leading decimal point */
    if (begin < end && *begin == '.') {

        decimal_exponent  = -1;
        had_decimal_point = 1;

        ++begin;

        /* Skip zeroes */
        while (begin < end && *begin == '0') {
            ++begin;
            --decimal_exponent;
        }
    }

    if (begin < end) {

        char c = *(begin++);

        if (c == 'e' || c == 'E')
            --begin;
        else {

            /* Position the first digit in mantissa */
            if (c < '0' || c > '9')
                TRY(KOS_ERROR_INVALID_NUMBER);

            mantissa = (unsigned)(c - '0');
            if (mantissa > 7)
                exponent += 3;
            else if (mantissa > 3)
                exponent += 2;
            else if (mantissa > 1)
                exponent += 1;

            mantissa <<= 63 - exponent;
        }

        /* Parse consecutive digits */
        while (begin < end   &&
               *begin != 'e' &&
               *begin != 'E') {

            c = *(begin++);

            /* Parse decimal point */
            if (c == '.') {
                assert(!had_decimal_point);
                had_decimal_point = 1;
            }
            else {

                uint64_t digit;

                if (c < '0' || c > '9')
                    TRY(KOS_ERROR_INVALID_NUMBER);

                /* Parse digit */
                digit = (unsigned)(c - '0');

                _multiply_by_10(&mantissa, &exponent);

                if (exponent > 63)
                    TRY(KOS_ERROR_TOO_MANY_DIGITS);

                mantissa += digit << (63 - exponent);

                if (had_decimal_point)
                    --decimal_exponent;

                _renormalize(&mantissa, &exponent);
            }
        }

        /* Parse decimal exponent */
        if (begin < end) {

            int64_t e = 0;

            assert(*begin == 'e' || *begin == 'E');
            ++begin;

            if (begin == end)
                TRY(KOS_ERROR_INVALID_EXPONENT);

            error = _KOS_parse_int(begin, end, &e);

            if (error)
                TRY(KOS_ERROR_INVALID_EXPONENT);

            if (e > 308 || e < -324)
                TRY(KOS_ERROR_EXPONENT_OUT_OF_RANGE);

            decimal_exponent += (int)e;
        }

        /* Ignore decimal exponent if mantissa contains all zeroes */
        if ( ! mantissa)
            decimal_exponent = 0;

        /* Apply decimal exponent */
        while (decimal_exponent) {

            if (decimal_exponent < 0) {
                _divide_by_10(&mantissa, &exponent);
                ++decimal_exponent;
            }
            else {
                _multiply_by_10(&mantissa, &exponent);
                --decimal_exponent;
            }

            _renormalize(&mantissa, &exponent);
        }
    }

    if (exponent > 0x3FF)
        TRY(KOS_ERROR_NUMBER_TOO_BIG);

    /* Adjust exponent for denormalized numbers */
    while (exponent < -0x3FF) {

        if (mantissa) {
            mantissa >>= 1;
            ++exponent;
        }
        else
            exponent = -0x3FF;
    }

    if (exponent == -0x3FF)
        mantissa >>= 1;

    if (mantissa == 0)
        exponent = -0x3FF;

    /* Round mantissa to nearest */
    if ((mantissa & 0x7FFU) >= 0x400U) {

        mantissa = (mantissa | 0x3FFU) + 1;

        /* Cope with carry after rounding */
        if (mantissa == 0) {
            mantissa = (uint64_t)1U << 63;
            ++exponent;
        }
        /* Renormalize number after carry */
        else if ((mantissa & ((uint64_t)1U << 63)) != 0 && exponent == -0x3FF)
            ++exponent;
    }

    *(uint64_t*)value = ((uint64_t)sign << 63)
                      | ((uint64_t)(exponent + 0x3FF) << 52)
                      | ((mantissa >> 11) & (((uint64_t)1U << 52)-1));

_error:
    return error;
}

int _KOS_parse_numeric(const char          *begin,
                       const char          *end,
                       struct _KOS_NUMERIC *value)
{
    int error;

    value->type = KOS_NON_NUMERIC;

    if (_KOS_is_integer(begin, end)) {
        error = _KOS_parse_int(begin, end, &value->u.i);
        if ( ! error)
            value->type = KOS_INTEGER_VALUE;
    }
    else {
        error = _KOS_parse_double(begin, end, &value->u.d);
        if ( ! error)
            value->type = KOS_FLOAT_VALUE;
    }

    return error;
}

uint64_t _KOS_double_to_uint64_t(double value)
{
    union DOUBLE_TO_UINT64 {
        double   d;
        uint64_t u;
    } conv;
    conv.d = value;
    return conv.u;
}

uint32_t _KOS_float_to_uint32_t(float value)
{
    union FLOAT_TO_UINT32 {
        float    f;
        uint32_t u;
    } conv;
    conv.f = value;
    return conv.u;
}

void _KOS_rng_init(struct KOS_RNG *rng)
{
    int i;

    srand((unsigned)time(0));

    rng->seed[0] = 0;
    rng->seed[1] = 0;

    for (i = 0; i < 16; i++) {
        const int sel   = i >> 3;
        const int shift = (i & 7) << 3;
        rng->seed[sel] |= (uint64_t)(rand() & 0xFFU) << shift;
    }
}

uint64_t _KOS_rng_random(struct KOS_RNG *rng)
{
    /* xorshift+ algorithm */
    uint64_t       x = rng->seed[0];
    const uint64_t y = rng->seed[1];

    rng->seed[0] = y;
    x           ^= x << 23;
    x            = x ^ y ^ (x >> 17) ^ (y >> 26);
    rng->seed[1] = x;
    return x + y;
}

int64_t _KOS_fix_index(int64_t idx, unsigned length)
{
    if (idx < 0)
        idx += length;

    if (idx < 0)
        idx = 0;
    else if (idx > (int64_t)length)
        idx = length;

    return idx;
}

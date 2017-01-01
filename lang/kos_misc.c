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

#include "kos_misc.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   include <wincrypt.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'fopen/getenv': This function may be unsafe */
#   pragma comment( lib, "advapi32.lib" )
#else
#   include <fcntl.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif

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
                (c < 'A' || c > 'F') &&
                (c != '_'))
                break;
        }
    }
    else if (radix == 2) {

        idx = 4 + minus;

        begin += 2;

        for (s = begin; s < end; ++s) {
            const char c = *s;
            if (c != '0' && c != '1' && c != '_')
                break;
        }
    }
    else {

        idx = minus;

        for (s = begin; s < end; ++s) {
            const char c = *s;
            if ((c < '0' || c > '9') && c != '_')
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

            if (c == '_')
                continue;

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

        if ( ! error) {
            if (minus)
                *value = (int64_t)((uint64_t)0U - v);
            else
                *value = (int64_t)v;
        }
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
        RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

    /* Parse sign */
    if (*begin == '-') {
        sign = 1;
        ++begin;
    }

    if (begin == end)
        RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

    /* Discard leading zeroes and underscores */
    while (begin < end && (*begin == '0' || *begin == '_'))
        ++begin;

    /* Parse leading decimal point */
    if (begin < end && *begin == '.') {

        decimal_exponent  = -1;
        had_decimal_point = 1;

        ++begin;

        /* Skip zeroes and underscores */
        while (begin < end && (*begin == '0' || *begin == '_')) {
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
                RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

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

            if (c == '_')
                continue;

            /* Parse decimal point */
            if (c == '.') {
                assert(!had_decimal_point);
                had_decimal_point = 1;
            }
            else {

                uint64_t digit;

                if (c < '0' || c > '9')
                    RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

                /* Parse digit */
                digit = (unsigned)(c - '0');

                _multiply_by_10(&mantissa, &exponent);

                if (exponent > 63)
                    RAISE_ERROR(KOS_ERROR_TOO_MANY_DIGITS);

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
                RAISE_ERROR(KOS_ERROR_INVALID_EXPONENT);

            error = _KOS_parse_int(begin, end, &e);

            if (error)
                RAISE_ERROR(KOS_ERROR_INVALID_EXPONENT);

            if (e > 308 || e < -324)
                RAISE_ERROR(KOS_ERROR_EXPONENT_OUT_OF_RANGE);

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
        RAISE_ERROR(KOS_ERROR_NUMBER_TOO_BIG);

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

void _KOS_get_entropy_fallback(uint8_t *bytes)
{
    const uint32_t multiplier = 0x8088405U;
    uint32_t       state      = (uint32_t)time(0);
    uint8_t *const end        = bytes + 32;

    /* Trivial LCG to init the state from time */
    for ( ; bytes < end; bytes++) {

        state = state * multiplier + 1;

        *bytes = (uint8_t)(state >> 24);
    }
}

#ifdef _WIN32
static void _get_entropy(uint8_t *bytes)
{
    HCRYPTPROV crypt_prov;

    if (CryptAcquireContext(&crypt_prov, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {

        if ( ! CryptGenRandom(crypt_prov, 32, bytes))
            _KOS_get_entropy_fallback(bytes);

        CryptReleaseContext(crypt_prov, 0);
    }
    else
        _KOS_get_entropy_fallback(bytes);
}
#else
static void _get_entropy(uint8_t *bytes)
{
    const int fd = open("/dev/urandom", O_RDONLY);

    if (fd == -1)
        _KOS_get_entropy_fallback(bytes);

    else {

        const ssize_t num_read = read(fd, bytes, 32);

        if (num_read != 32)
            _KOS_get_entropy_fallback(bytes);

        close(fd);
    }
}
#endif

/* The PCG XSH RR 32 algorithm by Melissa O'Neill, http://www.pcg-random.org */
static uint32_t _pcg_random(struct KOS_RNG_PCG32 *pcg)
{
    uint32_t xorshifted;
    int      rot;

    const uint64_t state = pcg->state;

    const uint64_t multiplier = ((uint64_t)0x5851F42DU << 32) | (uint64_t)0x4C957F2D;

    pcg->state = state * multiplier + pcg->stream;

    xorshifted = (uint32_t)(((state >> 18U) ^ state) >> 27U);
    rot        = (int)(state >> 59U);

    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static void _pcg_init(struct KOS_RNG_PCG32 *pcg,
                      uint64_t              init_state,
                      uint64_t              init_stream)
{
    pcg->stream = (init_stream << 1) | 1U;
    pcg->state  = pcg->stream + init_state;
    _pcg_random(pcg);
}

void _KOS_rng_init_seed(struct KOS_RNG *rng, uint64_t seed)
{
    _pcg_init(&rng->pcg[0], seed & 0xFFFFU, (seed >> 16) & 0xFFFFU);
    _pcg_init(&rng->pcg[1], (seed >> 32) & 0xFFFFU, (seed >> 48) & 0xFFFFU);
}

void _KOS_rng_init(struct KOS_RNG *rng)
{
    uint64_t entropy[4];
    _get_entropy((uint8_t *)&entropy);

    _pcg_init(&rng->pcg[0], entropy[0], entropy[1]);
    _pcg_init(&rng->pcg[1], entropy[2], entropy[3]);
}

uint64_t _KOS_rng_random(struct KOS_RNG *rng)
{
    const uint64_t low  = _pcg_random(&rng->pcg[0]);
    const uint64_t high = _pcg_random(&rng->pcg[1]);
    return (high << 32U) | low;
}

uint64_t _KOS_rng_random_range(struct KOS_RNG *rng, uint64_t max_value)
{
    if (max_value == (uint64_t)(int64_t)-1)
        return _KOS_rng_random(rng);

    if (max_value == (uint32_t)(int32_t)-1)
        return _pcg_random(&rng->pcg[0]);

    if (max_value <= (uint32_t)(int32_t)-1) {

        const uint32_t mask      = (uint32_t)max_value + 1U;
        const uint32_t threshold = (uint32_t)-(int32_t)mask % mask;
        int            sel       = 0;

        for (;; sel ^= 1) {
            const uint32_t r = _pcg_random(&rng->pcg[sel]);
            if (r >= threshold)
                return r % mask;
        }
    }
    else {

        const uint64_t mask      = max_value + 1U;
        const uint64_t threshold = (uint64_t)-(int64_t)mask % mask;
        int            sel       = 0;

        for (;; sel ^= 1) {
            const uint64_t r = _KOS_rng_random(rng);
            if (r >= threshold)
                return r % mask;
        }
    }
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

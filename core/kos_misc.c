/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "kos_misc.h"
#include "../inc/kos_error.h"
#include "kos_math.h"
#include "kos_system_internal.h"
#include "kos_try.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

union DOUBLE_TO_UINT64 {
    double   d;
    uint64_t u;
};

int kos_is_integer(const char *begin,
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

int kos_parse_int(const char *begin,
                  const char *end,
                  int64_t    *value)
{
    int         error = KOS_SUCCESS;
    const char *s;
    const int   minus = ((begin < end) && (*begin == '-')) ? 1 : 0;
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
                *value = -(int64_t)v;
            else
                *value = (int64_t)v;
        }
    }

    return error;
}

static uint64_t shift_digit(unsigned digit, int exponent)
{
    const int shift = 59 - exponent;
    if (shift >= 0)
        return (uint64_t)digit << shift;
    else if (shift >= -4)
        return (uint64_t)digit >> -shift;
    else
        return 0;
}

/*
 * How rounding works:
 * - L is the least significant bit of mantissa
 * - G is the guard bit
 * - R is the round bit
 * - S is the sticky bit
 *
 * If LGRS = x0xx -> round down
 * If LGRS = 0100 -> round down
 * If LGRS = 1100 -> round up
 * If LGRS = x101 -> round up
 * If LGRS = x11x -> round up
 */

static void multiply_by_10_and_add(uint64_t *mantissa, int *exponent, unsigned digit)
{
    const uint64_t high1 = 5 * (*mantissa >> 3);
    const uint32_t low   = 5 * ((uint32_t)*mantissa & 7U);
    const uint64_t high  = high1 + (low >> 3) + shift_digit(digit, *exponent);

    if (high & ((uint64_t)1U << 63)) {
        const uint32_t lgrs_mask = 0xFU;
        const uint32_t g_mask    = 0x4U;
        const uint32_t carry     = (low & lgrs_mask) != g_mask ? (low & g_mask) : 0U;

        *mantissa = high + (carry >> 2);
        *exponent += 4;
    }
    else if (high & ((uint64_t)1U << 62)) {
        const uint32_t lgr_mask = 0x7U;
        const uint32_t g_mask   = 0x2U;
        const uint32_t carry    = (low & lgr_mask) != g_mask ? (low & g_mask) : 0U;

        *mantissa = (high << 1) + ((low >> 2) & 1U) + (carry >> 1);
        *exponent += 3;
    }
    else if (high & ((uint64_t)1U << 61)) {
        const uint32_t lg    = low & 0x3U;
        const uint32_t carry = (lg == 3U) ? 1U : 0U;

        *mantissa = (high << 2) + ((low >> 1) & 3U) + carry;
        *exponent += 2;
    }
    else {
        const uint32_t carry = low & 1U;

        *mantissa = (high << 3) + (low & 7U) + carry;
        *exponent += 1;
    }
}

static void divide_by_10(uint64_t *mantissa, int *exponent)
{
    const uint64_t high = (*mantissa & ~(uint64_t)0xFFFFFFFFU) / 5;
    const uint64_t low  = (*mantissa << 32) / 5 + (high & 0xFFFFFFFFU);

    assert( ! (high >> 62));

    if (high & ((uint64_t)1U << 61)) {
        const uint32_t lgrs_mask = 0x7FFFFFFFU;
        const uint32_t g_mask    = 0x20000000U;
        const uint32_t carry     = ((uint32_t)low & lgrs_mask) != g_mask ?
                                   ((uint32_t)low & g_mask) : 0U;

        *mantissa = (high << 2) + ((low + carry) >> 30);
        *exponent -= 3;
    }
    else {
        const uint32_t lgrs_mask = 0x3FFFFFFFU;
        const uint32_t g_mask    = 0x10000000U;
        const uint32_t carry     = ((uint32_t)low & lgrs_mask) != g_mask ?
                                   ((uint32_t)low & g_mask) : 0U;

        *mantissa = (high << 3) + ((low + carry) >> 29);
        *exponent -= 4;
    }

    assert(*mantissa & ((uint64_t)1U << 63));
}

int kos_parse_double(const char *begin,
                     const char *end,
                     double     *value)
{
    int      error             = KOS_SUCCESS;
    int      sign              = 0;
    int      exponent          = 0;
    uint64_t mantissa          = 0;
    int      decimal_exponent  = 0;
    int      num_digits        = 0;
    int      dot_pos           = -1;

    assert(begin <= end);

    if (begin == end)
        RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

    /* Parse sign */
    if (*begin == '-') {
        sign = 1;
        ++begin;
    }
    else if (*begin == '+')
        ++begin;

    if (begin == end)
        RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);

    /* Discard leading zeroes and underscores */
    while (begin < end) {

        const char c = *(begin++);

        if (c == '_')
            continue;

        if (c == '0') {
            if (dot_pos >= 0)
                --decimal_exponent;
            continue;
        }

        if (c == '.') {
            dot_pos = 0;
            continue;
        }

        --begin;
        break;
    }

    if (begin < end) {

        const char *first_digit = begin;
        const char *exponent_pos;

        /* Find decimal exponent */
        while (begin < end) {

            const char c = *(begin++);

            if (c >= '0' && c <= '9') {
                ++num_digits;
                continue;
            }

            if (c == 'e' || c == 'E') {
                --begin;
                break;
            }

            if (c == '_')
                continue;

            if (c == '.') {
                if (dot_pos != -1)
                    RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);
                dot_pos = num_digits;
                continue;
            }

            RAISE_ERROR(KOS_ERROR_INVALID_NUMBER);
        }

        exponent_pos = begin;

        /* Parse decimal exponent */
        if (begin < end) {

            int64_t e = 0;

            assert(*begin == 'e' || *begin == 'E');
            ++begin;

            if (begin == end)
                RAISE_ERROR(KOS_ERROR_INVALID_EXPONENT);

            error = kos_parse_int(begin, end, &e);

            if (error)
                RAISE_ERROR(KOS_ERROR_INVALID_EXPONENT);

            if (e > 308 || e < -324)
                RAISE_ERROR(KOS_ERROR_EXPONENT_OUT_OF_RANGE);

            decimal_exponent += (int)e;
        }

        begin = first_digit;
        end   = exponent_pos;

        /* Adjust decimal exponent based on the number of digits to consume */
        if (dot_pos >= 0)
            decimal_exponent += dot_pos - num_digits;
    }

    if (num_digits) {

        int i_digit = 0;

        /* Parse consecutive digits */
        while (begin < end) {

            unsigned digit;
            int      lost_precision;

            char c = *(begin++);

            if (c == '_' || c == '.')
                continue;

            assert(c >= '0' && c <= '9');

            /* Place first digit */
            if ( ! mantissa) {
                mantissa = (unsigned)(c - '0');
                if (mantissa > 7)
                    exponent += 3;
                else if (mantissa > 3)
                    exponent += 2;
                else if (mantissa > 1)
                    exponent += 1;

                mantissa <<= 63 - exponent;

                ++i_digit;
                continue;
            }

            /* Parse digit */
            digit = (unsigned)(c - '0');

            /* Detect loss of precision, ignore further digits */
            lost_precision = (exponent > 53) ? 1 : 0;

            /* If we lost precision, round the last digit to nearest */
            if (lost_precision)
                digit = (digit >= 5) ? 10U : 5U;

            multiply_by_10_and_add(&mantissa, &exponent, digit);

            ++i_digit;

            /* Upon loss of precision, stop parsing further digits */
            if (lost_precision) {
                decimal_exponent += num_digits - i_digit;
                break;
            }
        }

        /* Ignore decimal exponent if mantissa contains all zeroes */
        if ( ! mantissa)
            decimal_exponent = 0;

        /* Apply decimal exponent */
        if (decimal_exponent < 0) {
            while (decimal_exponent) {
                divide_by_10(&mantissa, &exponent);
                ++decimal_exponent;
            }
        }
        else if (decimal_exponent > 0) {
            while (decimal_exponent) {
                multiply_by_10_and_add(&mantissa, &exponent, 0);
                --decimal_exponent;
            }
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
    if (mantissa) {
        const uint32_t lgrs_mask = 0xFFFU;
        const uint32_t g_mask    = 0x400U;
        const uint32_t low       = (uint32_t)mantissa;
        const uint32_t carry     = (low & lgrs_mask) != g_mask ? (low & g_mask) : 0U;

        mantissa = (mantissa >> 11) + (carry >> 10);

        /* Renormalize number after carry */
        if (mantissa & ((uint64_t)1U << 53)) {
            mantissa >>= 1;
            ++exponent;
        }
    }

    {
        union DOUBLE_TO_UINT64 conv;

        conv.u = ((uint64_t)sign << 63)
               | ((uint64_t)(exponent + 0x3FFU) << 52)
               | (mantissa & (((uint64_t)1U << 52)-1U));

        *value = conv.d;
    }

cleanup:
    return error;
}

int kos_parse_numeric(const char  *begin,
                      const char  *end,
                      KOS_NUMERIC *value)
{
    int error;

    value->type = KOS_NON_NUMERIC;

    if (kos_is_integer(begin, end)) {
        error = kos_parse_int(begin, end, &value->u.i);
        if ( ! error)
            value->type = KOS_INTEGER_VALUE;
    }
    else {
        error = kos_parse_double(begin, end, &value->u.d);
        if ( ! error)
            value->type = KOS_FLOAT_VALUE;
    }

    return error;
}

uint64_t kos_double_to_uint64_t(double value)
{
    union DOUBLE_TO_UINT64 conv;
    conv.d = value;
    return conv.u;
}

uint32_t kos_float_to_uint32_t(float value)
{
    union FLOAT_TO_UINT32 {
        float    f;
        uint32_t u;
    } conv;
    conv.f = value;
    return conv.u;
}

unsigned kos_print_float(char *buf, unsigned size, double value)
{
    char *end;

    union DOUBLE_TO_UINT64 conv;

    conv.d = value;

    if (((conv.u >> 52) & 0x7FFU) == 0x7FFU) {
        if (conv.u << 12) {
            static const char nan[] = "nan";

            assert(sizeof(nan) - 1 <= size);
            memcpy(buf, nan, sizeof(nan) - 1);
            end = buf + sizeof(nan) - 1;
        }
        else {
            static const char infinity[] = "-infinity";
            const size_t      offs       = value < 0 ? 0 : 1;
            const size_t      str_size   = sizeof(infinity) - offs - 1;

            assert(str_size <= size);
            memcpy(buf, infinity + offs, str_size);
            end = buf + str_size;
        }
    }
    else {
        const int len = snprintf(buf, size, "%.15f", value);

        if (len < 0)
            return 0U;

        for (end = buf + KOS_min((unsigned)len, size - 1) - 1; end > buf && *end == '0'; --end);
        if (*end == '.')
            ++end;
        if (*end)
            ++end;
    }

    return (unsigned)(end - buf);
}

void kos_get_entropy_fallback(uint8_t *bytes, unsigned size)
{
    const uint32_t multiplier = 0x8088405U;
    uint32_t       state      = ((uint32_t)time(0) << 1) | 1U;
    unsigned       i;

    /* Trivial LCG to init the state from time */

    for (i = 0; i < 4; i++)
        state = state * multiplier + 1;

    for (i = 0; i < size; i++) {

        state = state * multiplier + 1;

        bytes[i] = (uint8_t)(state >> 23);
    }
}

#ifdef _WIN32
static void get_entropy(uint8_t *bytes, unsigned size)
{
    HCRYPTPROV crypt_prov;

    if (CryptAcquireContext(&crypt_prov, KOS_NULL, KOS_NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {

        if ( ! CryptGenRandom(crypt_prov, size, bytes))
            kos_get_entropy_fallback(bytes, size);

        CryptReleaseContext(crypt_prov, 0);
    }
    else
        kos_get_entropy_fallback(bytes, size);
}
#else
static void get_entropy(uint8_t *bytes, unsigned size)
{
    const int fd = kos_unix_open("/dev/urandom", O_RDONLY);

    if (fd == -1)
        kos_get_entropy_fallback(bytes, size);

    else {

        const ssize_t num_read = read(fd, bytes, size);

        if ((unsigned)num_read != size)
            kos_get_entropy_fallback(bytes, size);

        close(fd);
    }
}
#endif

/* The PCG XSH RR 32 algorithm by Melissa O'Neill, http://www.pcg-random.org */
static uint32_t pcg_random(struct KOS_RNG_PCG32 *pcg)
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

static void pcg_init(struct KOS_RNG_PCG32 *pcg,
                     uint64_t              init_state,
                     uint64_t              init_stream)
{
    pcg->stream = (init_stream << 1) | 1U;
    pcg->state  = pcg->stream + init_state;
    pcg_random(pcg);
}

void kos_rng_init_seed(struct KOS_RNG *rng, uint64_t seed)
{
    struct KOS_RNG_PCG32 init_pcg;
    int                  ipcg;

    pcg_init(&init_pcg, seed, ~seed);

    for (ipcg = 0; ipcg < 2; ipcg++) {

        uint32_t new_seed[4];
        int      iseed;

        for (iseed = 0; iseed < 4; iseed++)
            new_seed[iseed] = pcg_random(&init_pcg);

        pcg_init(&rng->pcg[ipcg],
                 ((uint64_t)new_seed[0] << 32) | new_seed[1],
                 ((uint64_t)new_seed[2] << 32) | new_seed[3]);
    }
}

void kos_rng_init(struct KOS_RNG *rng)
{
    uint64_t entropy[4];
    get_entropy((uint8_t *)&entropy, sizeof(entropy));

    pcg_init(&rng->pcg[0], entropy[0], entropy[1]);
    pcg_init(&rng->pcg[1], entropy[2], entropy[3]);
}

uint64_t kos_rng_random(struct KOS_RNG *rng)
{
    const uint64_t low  = pcg_random(&rng->pcg[0]);
    const uint64_t high = pcg_random(&rng->pcg[1]);
    return (high << 32U) | low;
}

uint64_t kos_rng_random_range(struct KOS_RNG *rng, uint64_t max_value)
{
    if (max_value == (uint64_t)(int64_t)-1)
        return kos_rng_random(rng);

    if (max_value == (uint32_t)(int32_t)-1)
        return pcg_random(&rng->pcg[0]);

    if (max_value < (uint32_t)(int32_t)-1) {

        const uint32_t mask      = (uint32_t)max_value + 1U;
        const uint32_t threshold = (~mask + 1U) % mask;
        int            sel       = 0;

        for (;; sel ^= 1) {
            const uint32_t r = pcg_random(&rng->pcg[sel]);
            if (r >= threshold)
                return r % mask;
        }
    }
    else {

        const uint64_t mask      = max_value + 1U;
        const uint64_t threshold = (~mask + 1U) % mask;

        for (;;) {
            const uint64_t r = kos_rng_random(rng);
            if (r >= threshold)
                return r % mask;
        }
    }
}

KOS_IMM kos_load_uimm(const uint8_t *bytecode)
{
    const uint8_t *cur  = bytecode;
    KOS_IMM        imm;
    int            bits = 0;
    uint8_t        byte;

    imm.value.uv = 0;

    do {
        byte = *(cur++);

        assert((uint64_t)imm.value.uv + ((uint64_t)(byte & 0x7Fu) << bits) <= (uint64_t)0xFFFFFFFFu);

        imm.value.uv += (uint32_t)(byte & 0x7Fu) << bits;
        bits         += 7;
    } while (byte > 0x7Fu);

    imm.size = (int)(cur - bytecode);
    assert(imm.size < 5);

    return imm;
}

KOS_IMM kos_load_simm(const uint8_t *bytecode)
{
    KOS_IMM imm = kos_load_uimm(bytecode);

    const uint32_t sign = imm.value.uv;

    imm.value.sv >>= 1;

    if (sign & 1u)
        imm.value.uv = ~imm.value.uv;

    return imm;
}

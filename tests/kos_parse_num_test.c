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

#include "../core/kos_misc.h"
#include "../inc/kos_error.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int status = 0;

static int reference = 0;

static void test_int(const char *str, uint32_t hi, uint32_t lo, int error)
{
    int64_t   v   = 0;
    const int ret = _KOS_parse_int(str, str+strlen(str), &v);

    if (ret != error) {
        printf("Failed: %s - error %d, expected %d\n", str, ret, error);
        status = 1;
    }
    else if (ret == 0) {

        const uint32_t v_hi = (uint32_t)(v >> 32);
        const uint32_t v_lo = (uint32_t)v;
        if (v_hi != hi || v_lo != lo) {
            printf("Failed: %s - value 0x%08X%08X, expected 0x%08X%08X\n",
                   str, v_hi, v_lo, hi, lo);
            status = 1;
        }
    }
}

static void test_double(const char *str, uint32_t high, uint32_t low, int error)
{
    double value = 0;
    int    ret   = _KOS_parse_double(str, str+strlen(str), &value);

    if (ret != error) {
        printf("Failed: %s - error %d, expected %d\n", str, ret, error);
        status = 1;
    }
    else if (ret == 0) {

        const uint64_t uval      = _KOS_double_to_uint64_t(value);
        const uint32_t uval_low  = (uint32_t)uval;
        const uint32_t uval_high = (uint32_t)(uval >> 32);

        if (low != uval_low || high != uval_high) {
            printf("Failed: %s - value 0x%08X%08X, expected 0x%08X%08X\n",
                   str, uval_high, uval_low, high, low);
            status = 1;
        }

        if (reference) {
            char        *endptr = 0;
            const double conv_d = strtod(str, &endptr);

            if (endptr != str + strlen(str)) {
                printf("Failed: %s - strtod returned error\n", str);
                status = 1;
            }
            else {
                const uint64_t conv_u = _KOS_double_to_uint64_t(conv_d);
                if (conv_u != uval) {
                    printf("Failed: %s - value 0x%08X%08X, strtod 0x%08X%08X\n",
                           str, uval_high, uval_low,
                           (uint32_t)(conv_u >> 32), (uint32_t)conv_u);
                    status = 1;
                }
            }
        }
    }
}

static void test_random_double(void)
{
    char           str[32];
    struct KOS_RNG rng;
    int            i_test;

    _KOS_rng_init(&rng);

    for (i_test = 0; i_test < 10240; i_test++) {

        char*    pos        = str;
        unsigned num_digits = 0;
        unsigned dot_pos    = ~0U;
        unsigned i_digit;
        int      ret;
        double   actual;
        double   expected;
        uint64_t actual_u;
        uint64_t expected_u;
        char*    endptr;

        if (_KOS_rng_random_range(&rng, 1U))
            *(pos++) = '-';

        num_digits = (unsigned)_KOS_rng_random_range(&rng, 23U) + 1U;

        if (_KOS_rng_random_range(&rng, 4U))
            dot_pos = (unsigned)_KOS_rng_random_range(&rng, num_digits - 2U);

        for (i_digit = 0; i_digit < num_digits; i_digit++) {

            const char digit = '0' + (char)(unsigned)_KOS_rng_random_range(&rng, 9U);

            *(pos++) = digit;

            if (i_digit == dot_pos)
                *(pos++) = '.';
        }

        if (_KOS_rng_random_range(&rng, 4U)) {

            *(pos++) = _KOS_rng_random_range(&rng, 1U) ? 'e' : 'E';

            if (_KOS_rng_random_range(&rng, 1U))
                *(pos++) = '-';
            else if (_KOS_rng_random_range(&rng, 1U))
                *(pos++) = '+';

            num_digits = (unsigned)((&str[0] + sizeof(str)) - pos) - 1U;
            if (num_digits > 3)
                num_digits = 3;
            assert(num_digits > 0);

            if (num_digits > 1U)
                num_digits = 1U + (unsigned)_KOS_rng_random_range(&rng, num_digits - 1U);
            else
                num_digits = 1U;
            assert(pos + num_digits + 1 <= &str[0] + sizeof(str));

            for (i_digit = 0; i_digit < num_digits; i_digit++) {

                unsigned max_digit = 9U;

                if (i_digit == 0 && num_digits == 3)
                    max_digit = 2U;

                *(pos++) = '0' + (char)(unsigned)_KOS_rng_random_range(&rng, max_digit);
            }
        }

        *pos = '\0';

        ret = _KOS_parse_double(str, pos, &actual);

        expected = strtod(str, &endptr);

        if (ret != KOS_SUCCESS) {
            printf("Failed: %s parse failed with error %d\n", str, ret);
            status = 1;
            continue;
        }

        if (endptr != pos) {
            printf("Failed: %s failed to parse with strtod\n", str);
            status = 1;
            continue;
        }

        actual_u   = _KOS_double_to_uint64_t(actual);
        expected_u = _KOS_double_to_uint64_t(expected);

        if (actual_u != expected_u) {
            char           diff_str[32];
            const uint64_t diff = expected_u - actual_u;

            if (diff == 1U) {
                diff_str[0] = '1';
                diff_str[1] = '\0';
            }
            else if ((diff + 1U) == 0U) {
                diff_str[0] = '-';
                diff_str[1] = '1';
                diff_str[2] = '\0';
            }
            else
                snprintf(diff_str, sizeof(diff_str), "0x%08X%08X", (uint32_t)(diff >> 32), (uint32_t)diff);

            printf("Failed: %32s - (%s) value 0x%08X%08X, expected 0x%08X%08X\n",
                   str, diff_str, (uint32_t)(actual_u >> 32), (uint32_t)actual_u,
                   (uint32_t)(expected_u >> 32), (uint32_t)expected_u);

            status = 1;
        }
    }
}

int main(int argc, char *argv[])
{
    int random = 0;

    if (argc == 2) {
        if (strcmp(argv[1], "-reference") == 0)
            reference = 1;

        else if (strcmp(argv[1], "-random") == 0) {
            reference = 1;
            random    = 1;
        }
    }

    /* Integers */
    test_int("0",                              0, 0x00000000U, KOS_SUCCESS);
    test_int("-0",                             0, 0x00000000U, KOS_SUCCESS);
    test_int("+0",                             0, 0x00000000U, KOS_SUCCESS);
    test_int("1",                              0, 0x00000001U, KOS_SUCCESS);
    test_int("-1",                   0xFFFFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_int("+10",                            0, 0x0000000AU, KOS_SUCCESS);
    test_int("2147483647",                     0, 0x7FFFFFFFU, KOS_SUCCESS);
    test_int("-2147483648",          0xFFFFFFFFU, 0x80000000U, KOS_SUCCESS);
    test_int("0x7FFFFFFF",                     0, 0x7FFFFFFFU, KOS_SUCCESS);
    test_int("-0x7FFFFFFF",          0xFFFFFFFFU, 0x80000001U, KOS_SUCCESS);
    test_int("-0x80000000",          0xFFFFFFFFU, 0x80000000U, KOS_SUCCESS);
    test_int("-0x80000001",          0xFFFFFFFFU, 0x7FFFFFFFU, KOS_SUCCESS);
    test_int("0x80000000",                     0, 0x80000000U, KOS_SUCCESS);
    test_int("2147483648",                     0, 0x80000000U, KOS_SUCCESS);
    test_int("-2147483649",          0xFFFFFFFFU, 0x7FFFFFFFU, KOS_SUCCESS);
    test_int("9223372036854775807",  0x7FFFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_int("-9223372036854775808", 0x80000000U,           0, KOS_SUCCESS);
    test_int("0x8000000000000000",   0x80000000U,           0, KOS_SUCCESS);
    test_int("-0x8000000000000000",  0x80000000U,           0, KOS_SUCCESS);
    test_int("0xFFFFFFFFFFFFFFFF",   0xFFFFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_int("-0xFFFFFFFFFFFFFFFF",            0,           1, KOS_SUCCESS);
    test_int("____1___2___",                   0,        0xCU, KOS_SUCCESS);
    test_int("0X__A___",                       0,        0xAU, KOS_SUCCESS);
    test_int("0B___1__0__",                    0,           2, KOS_SUCCESS);
    test_int("-",                              0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("--",                             0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("--1",                            0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("1-",                             0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("+",                              0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("++",                             0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("++1",                            0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("1+",                             0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("0x10000000000000000",            0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("0x10000000000000001",            0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("-0x10000000000000000",           0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("9223372036854775808",            0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("-9223372036854775809",           0,           0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("0x12G",                          0,           0, KOS_ERROR_INTEGER_EXPECTED);

    test_int("0b0111111111111111111111111111111111111111111111111111111111111111",   0x7FFFFFFFU, ~0U, KOS_SUCCESS);
    test_int("0b1000000000000000000000000000000000000000000000000000000000000000",   0x80000000U,  0U, KOS_SUCCESS);
    test_int("-0B1000000000000000000000000000000000000000000000000000000000000000",  0x80000000U,  0U, KOS_SUCCESS);
    test_int("0b01111111111111111111111111111111111111111111111111111111111111111",  0xFFFFFFFFU, ~0U, KOS_SUCCESS);
    test_int("-0b01111111111111111111111111111111111111111111111111111111111111111",           0,  1U, KOS_SUCCESS);
    test_int("0b10000000000000000000000000000000000000000000000000000000000000000",            0,   0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("-0b10000000000000000000000000000000000000000000000000000000000000000",           0,   0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("0b12",                                                                           0,   0, KOS_ERROR_INTEGER_EXPECTED);

    /* Zero */
    test_double("0",                       0x00000000U, 0x00000000U, KOS_SUCCESS);
    test_double("0.",                      0x00000000U, 0x00000000U, KOS_SUCCESS);
    test_double("0.0",                     0x00000000U, 0x00000000U, KOS_SUCCESS);
    test_double("-0",                      0x80000000U, 0x00000000U, KOS_SUCCESS);
    test_double("0e0",                     0x00000000U, 0x00000000U, KOS_SUCCESS);

    /* One */
    test_double("1",                       0x3FF00000U, 0x00000000U, KOS_SUCCESS);
    test_double("0001.0000",               0x3FF00000U, 0x00000000U, KOS_SUCCESS);

    /* Powers of two */
    test_double("2",                       0x40000000U, 0x00000000U, KOS_SUCCESS);
    test_double(".5",                      0x3FE00000U, 0x00000000U, KOS_SUCCESS);
    test_double(".25",                     0x3FD00000U, 0x00000000U, KOS_SUCCESS);
    test_double("4503599627370496",        0x43300000U, 0x00000000U, KOS_SUCCESS);
    test_double("9007199254740992",        0x43400000U, 0x00000000U, KOS_SUCCESS);
    test_double("18014398509481984",       0x43500000U, 0x00000000U, KOS_SUCCESS);
    test_double("36028797018963968",       0x43600000U, 0x00000000U, KOS_SUCCESS);
    test_double("9223372036854775808",     0x43E00000U, 0x00000000U, KOS_SUCCESS);
    test_double("18446744073709551616",    0x43F00000U, 0x00000000U, KOS_SUCCESS);

    /* Simple numbers */
    test_double("3",                       0x40080000U, 0x00000000U, KOS_SUCCESS);
    test_double("-6",                      0xC0180000U, 0x00000000U, KOS_SUCCESS);

    /* One third */
    test_double("0.3333333333333333",      0x3FD55555U, 0x55555555U, KOS_SUCCESS);
    test_double("0.33333333333333333",     0x3FD55555U, 0x55555555U, KOS_SUCCESS);
    test_double("0.33333333333333334",     0x3FD55555U, 0x55555555U, KOS_SUCCESS);
    test_double("0.33333333333333335",     0x3FD55555U, 0x55555556U, KOS_SUCCESS);

    /* Almost one */
    test_double("0.999999999999999",       0x3FEFFFFFU, 0xFFFFFFF7U, KOS_SUCCESS);
    test_double("0.9999999999999998",      0x3FEFFFFFU, 0xFFFFFFFEU, KOS_SUCCESS);
    test_double("0.9999999999999999",      0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.99999999999999990",     0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.99999999999999994",     0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.999999999999999944",    0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.9999999999999999444",   0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.99999999999999994444",  0x3FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("0.999999999999999946",    0x3FF00000U, 0x00000000U, KOS_SUCCESS);
    test_double("0.99999999999999995",     0x3FF00000U, 0x00000000U, KOS_SUCCESS);
    test_double("0.99999999999999996",     0x3FF00000U, 0x00000000U, KOS_SUCCESS);

    /* Next number after one */
    test_double("1.0000000000000002",      0x3FF00000U, 0x00000001U, KOS_SUCCESS);
    test_double("1.0000000000000004",      0x3FF00000U, 0x00000002U, KOS_SUCCESS);

    /* Pi, E */
    test_double("3.14159265358979323",     0x400921FBU, 0x54442D18U, KOS_SUCCESS);
    test_double("2.71828182845904523",     0x4005BF0AU, 0x8B145769U, KOS_SUCCESS);

    /* Simple exponents */
    test_double("1e2",                     0x40590000U, 0x00000000U, KOS_SUCCESS);
    test_double("1e-2",                    0x3F847AE1U, 0x47AE147BU, KOS_SUCCESS);
    test_double("1e10",                    0x4202a05fU, 0x20000000U, KOS_SUCCESS);

    /* Next number after zero (smallest non-zero number) */
    test_double("4.9406564584124654e-324", 0x00000000U, 0x00000001U, KOS_SUCCESS);
    test_double("1e-323",                  0x00000000U, 0x00000002U, KOS_SUCCESS);
    test_double("1e-322",                  0x00000000U, 0x00000014U, KOS_SUCCESS);

    /* Largest denormalized number */
    test_double("2.2250738585072009e-308", 0x000FFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);

    /* Smallest normalized number */
    test_double("2.2250738585072014e-308", 0x00100000U, 0x00000000U, KOS_SUCCESS);

    /* Large numbers */
    test_double("100000000000000000",      0x43763457U, 0x85D8A000U, KOS_SUCCESS);
    test_double("1e17",                    0x43763457U, 0x85D8A000U, KOS_SUCCESS);

    /* Largest number */
    test_double("1.7976931348623157e308",  0x7FEFFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);

    /* Misc numbers */
    test_double("1234567890123456e-228",   0x13BA9972U, 0x7A3301A5U, KOS_SUCCESS);
    test_double("1234567890123456e-322",   0x00363199U, 0x16D6784AU, KOS_SUCCESS);
    test_double("123456789e-322",          0x00000000U, 0x94F08F0CU, KOS_SUCCESS);
    test_double("123456789012345e-322",    0x0008E0A3U, 0xA2BC3011U, KOS_SUCCESS);
    test_double("123456789012345678e-322", 0x00A156BFU, 0x99D78DFDU, KOS_SUCCESS);
    test_double("539e-4",                  0x3FAB98C7U, 0xE28240B8U, KOS_SUCCESS);
    test_double("93e-5",                   0x3F4E7967U, 0xCAEA747EU, KOS_SUCCESS);
    test_double("11e-12",                  0x3DA83073U, 0x119F21D8U, KOS_SUCCESS);
    test_double("3e-19",                   0x3C1622D6U, 0xFBC91E01U, KOS_SUCCESS);
    test_double("3e-20",                   0x3BE1B578U, 0xC96DB19BU, KOS_SUCCESS);
    test_double("4503599627370495",        0x432FFFFFU, 0xFFFFFFFEU, KOS_SUCCESS);
    test_double("9007199254740991",        0x433FFFFFU, 0xFFFFFFFFU, KOS_SUCCESS);
    test_double("9223372036854775807",     0x43E00000U, 0x00000000U, KOS_SUCCESS);
    test_double("9223372036854775807.000", 0x43E00000U, 0x00000000U, KOS_SUCCESS);
    test_double("9223372036854775807.0e0", 0x43E00000U, 0x00000000U, KOS_SUCCESS);
    test_double("-0.00000E0",              0x80000000U, 0x00000000U, KOS_SUCCESS);
    test_double("00009223372036854775808000000.00000000", 0x451E8480U, 0x00000000U, KOS_SUCCESS);
    test_double("-830997868037328000251.946", 0xC4468634U, 0xBF150FEFU, KOS_SUCCESS);
    test_double("205012068.401531294",     0x41A87078U, 0xC8CD9583U, KOS_SUCCESS);
    test_double("26153245263757307e49",    0x4D83DE00U, 0x5BD620DFU, KOS_SUCCESS);
    test_double("9e0306",                  0x7FA9A202U, 0x8368022EU, KOS_SUCCESS);
    test_double("1e-324",                  0x00000000U, 0x00000000U, KOS_SUCCESS);
    /*
    test_double("8e-111",                  0x29133D40U, 0x32C2C7F5U, KOS_SUCCESS);
    test_double("207499759360.469947e-2",  0x41DEEB7CU, 0xD666B365U, KOS_SUCCESS);
    */

    /* Formatting errors */
    test_double("1e1A",                              0,           0, KOS_ERROR_INVALID_EXPONENT);
    test_double("1e309",                             0,           0, KOS_ERROR_EXPONENT_OUT_OF_RANGE);
    test_double("1e-325",                            0,           0, KOS_ERROR_EXPONENT_OUT_OF_RANGE);
    test_double("9999999999999999999e308",           0,           0, KOS_ERROR_NUMBER_TOO_BIG);

    if (random)
        test_random_double();

    return status;
}

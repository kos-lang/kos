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
#include <string.h>
#include <stdio.h>

static int status = 0;

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
    }
}

int main(void)
{
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

    test_int("0b0111111111111111111111111111111111111111111111111111111111111111",   0x7FFFFFFFU, ~0U, KOS_SUCCESS);
    test_int("0b1000000000000000000000000000000000000000000000000000000000000000",   0x80000000U,  0U, KOS_SUCCESS);
    test_int("-0B1000000000000000000000000000000000000000000000000000000000000000",  0x80000000U,  0U, KOS_SUCCESS);
    test_int("0b01111111111111111111111111111111111111111111111111111111111111111",  0xFFFFFFFFU, ~0U, KOS_SUCCESS);
    test_int("-0b01111111111111111111111111111111111111111111111111111111111111111",           0,  1U, KOS_SUCCESS);
    test_int("0b10000000000000000000000000000000000000000000000000000000000000000",            0,   0, KOS_ERROR_INTEGER_EXPECTED);
    test_int("-0b10000000000000000000000000000000000000000000000000000000000000000",           0,   0, KOS_ERROR_INTEGER_EXPECTED);

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
    test_double("18446744073709551616",    0x43600000U, 0x00000000U, KOS_ERROR_TOO_MANY_DIGITS);

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
    test_double("0.99999999999999994444",  0x3FEFFFFFU, 0xFFFFFFFFU, KOS_ERROR_TOO_MANY_DIGITS);
    /*
    test_double("0.999999999999999946",    0x3FF00000U, 0x00000000U, KOS_SUCCESS);
    */
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
    test_double("-0.00000E0",              0x80000000U, 0x00000000U, KOS_SUCCESS);

    return status;
}

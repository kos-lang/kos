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

#include "../lang/kos_misc.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

int main()
{
    {
        int      i;
        unsigned sum      = 0;
        unsigned num_diff = 0;
        uint8_t  bytes[32];

        memset(bytes, 0, sizeof(bytes));

        _KOS_get_entropy_fallback(bytes);

        for (i = 0; i < (int)sizeof(bytes); i++) {

            sum += bytes[i];

            if (i > 0 && bytes[i] != bytes[i-1])
                num_diff++;
        }

        sum /= sizeof(bytes);

        TEST(num_diff > sizeof(bytes)/2);
        TEST(sum > 0 && sum < 255);
    }

    {
        struct KOS_RNG rng;
        uint64_t       value = 0;
        int            i;

        _KOS_rng_init(&rng);

        for (i = 0; i < 0x10000; i++)
            value += _KOS_rng_random(&rng) >> 16;

        value >>= 56;

        TEST(value >= 0x70U);
        TEST(value <= 0x8FU);
    }

    return 0;
}

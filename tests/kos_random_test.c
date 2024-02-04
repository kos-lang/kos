/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../core/kos_misc.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

int main(void)
{
    struct KOS_RNG rng;

    kos_rng_init(&rng);

    {
        int      i;
        unsigned sum      = 0;
        unsigned num_diff = 0;
        uint8_t  bytes[32];

        memset(bytes, 0, sizeof(bytes));

        kos_get_entropy_fallback(bytes, sizeof(bytes));

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
        uint64_t       value = 0;
        const uint64_t max_value = (uint64_t)(int64_t)-1;
        int            i;

        for (i = 0; i < 0x10000; i++)
            value += kos_rng_random_range(&rng, max_value) >> 16;

        value >>= 56;

        TEST(value >= 0x70U);
        TEST(value <= 0x8FU);
    }

    {
        int i;
        const uint64_t max_1 = (uint32_t)(int32_t)-1;
        const uint64_t max_2 = max_1 + 42U;

        for (i = 0; i < 0x10; i++) {
            const uint64_t value = kos_rng_random_range(&rng, 14U);
            TEST(value <= 14U);
        }

        for (i = 0; i < 0x1000; i++) {
            const uint64_t value = kos_rng_random_range(&rng, max_1);
            TEST(value <= max_1);
        }

        for (i = 0; i < 0x1000; i++) {
            const uint64_t value = kos_rng_random_range(&rng, max_2);
            TEST(value <= max_2);
        }
    }

    return 0;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../core/kos_utf8.h"
#include "../core/kos_system.h"
#include <stdio.h>
#include <stdlib.h>

struct TEST_STRING {
    const char *str;
    unsigned    length;
    unsigned    num_code_points;
    uint32_t    max_code;
};

static const struct TEST_STRING strings[] = {
    { "", 0, 0, 0 },
    { "this is a test of a long string", 31, 31, 't' },
    { ".\xC4\x88..XXXX12345678", 17, 16, 0x108U }
};

int main(int argc, char *argv[])
{
    const int64_t start_time = kos_get_time_us();
    int           i;
    int           num_loops = argc > 1 ? 10000000 : 1;

    for (i = 0; i < num_loops; i++) {

        size_t j;

        for (j = 0; j < sizeof(strings) / sizeof(strings[0]); j++) {
            uint32_t       max_code = 0U;
            const unsigned len      = kos_utf8_get_len(strings[j].str,
                                                       strings[j].length,
                                                       KOS_UTF8_WITH_ESCAPE,
                                                       &max_code);

            if (len != strings[j].num_code_points) {
                fprintf(stderr, "Error: Invalid length returned: %u (expected %u)\n",
                        len, strings[j].num_code_points);
                return EXIT_FAILURE;
            }

            if ((strings[j].max_code <  0x100U && max_code >= 0x100U) ||
                (strings[j].max_code >= 0x100U && max_code != strings[j].max_code)) {

                fprintf(stderr, "Error: Invalid max code returned: 0x%x (expected 0x%x)\n",
                        max_code, strings[j].max_code);
                return EXIT_FAILURE;
            }
        }
    }

    if (argc > 1) {
        const int64_t duration = kos_get_time_us() - start_time;
        printf("%u us\n", (unsigned)duration);
    }
    return EXIT_SUCCESS;
}

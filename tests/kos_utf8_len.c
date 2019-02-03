/*
 * Copyright (c) 2014-2019 Chris Dragan
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

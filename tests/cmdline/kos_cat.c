/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#   pragma warning( disable : 4996 ) /* 'fopen': This function may be unsafe */
#endif

static void cat(FILE *file)
{
    static uint8_t buf[1024 * 1024];

    size_t num_read;

    do {

        num_read = fread(&buf[0], 1, sizeof(buf), file);

        if (num_read)
            fwrite(&buf[0], 1, num_read, stdout);
    } while (num_read == sizeof(buf));

    if (ferror(file)) {
        perror(0);

        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    int i;

    if (argc < 2)
        cat(stdin);

    for (i = 1; i < argc; i++) {
        FILE *const file = fopen(argv[i], "rb");

        if ( ! file) {
            perror(0);
            return EXIT_FAILURE;
        }

        cat(file);

        fclose(file);
    }

    return EXIT_SUCCESS;
}

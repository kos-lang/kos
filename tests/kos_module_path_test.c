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

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_string.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int          error     = KOS_SUCCESS;
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    int          inst_ok   = 0;
    uint32_t     num_paths = 0;
    KOS_OBJ_ID   path_str;
    KOS_VECTOR   cstr;

    kos_vector_init(&cstr);

    TRY(KOS_instance_init(&inst, 0, &ctx));

    inst_ok = 1;

    TRY(KOS_modules_init(ctx));

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments passed to the test, expected 1\n");
        RAISE_ERROR(KOS_ERROR_NOT_FOUND);
    }

    TRY(KOS_instance_add_default_path(ctx, argv[1]));

    num_paths = KOS_get_array_size(inst.modules.search_paths);

    if (num_paths != 1) {
        fprintf(stderr, "Error: %u paths added\n", (unsigned)num_paths);
        RAISE_ERROR(KOS_ERROR_NOT_FOUND);
    }

    path_str = KOS_array_read(ctx, inst.modules.search_paths, 0);
    TRY_OBJID(path_str);

    TRY(KOS_string_to_cstr_vec(ctx, path_str, &cstr));

    printf("%s\n", cstr.buffer);

cleanup:
    if (inst_ok)
        KOS_instance_destroy(&inst);

    kos_vector_destroy(&cstr);

    if (error)
        fprintf(stderr, "Error: Failed with error code %d\n", error);

    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}

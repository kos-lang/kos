/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_utils.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int          error;
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    static const char base[] = "base";

    error = KOS_instance_init(&inst, 0, &ctx);

    if ( ! error) {

        error = KOS_instance_add_default_path(ctx, 0);

        if ( ! error)
            error = KOS_modules_init(ctx);

        if ( ! error)
            error = KOS_load_module_from_memory(ctx,
                                                base,
                                                sizeof(base) - 1,
                                                (const char *)data,
                                                (unsigned)size);

        if (error == KOS_ERROR_EXCEPTION)
            KOS_print_exception(ctx, KOS_STDERR);
        else if (error) {
            assert(error == KOS_ERROR_OUT_OF_MEMORY);
            fprintf(stderr, "Out of memory\n");
        }

        KOS_instance_destroy(&inst);
    }
    return 0;
}

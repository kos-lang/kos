/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
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

        if ( ! error) {
            const KOS_OBJ_ID module_id = KOS_load_module_from_memory(ctx,
                                                                     base,
                                                                     sizeof(base) - 1,
                                                                     (const char *)data,
                                                                     (unsigned)size);

            if (IS_BAD_PTR(module_id))
                error = KOS_ERROR_EXCEPTION;
            else {
                const KOS_OBJ_ID ret = KOS_run_module(ctx, module_id);
                if (IS_BAD_PTR(ret))
                    error = KOS_ERROR_EXCEPTION;
            }
        }

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

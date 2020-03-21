/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    static const char base[] = "base.kos";

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    TEST(KOS_load_module_from_memory(ctx, base, sizeof(base) - 1, 0, 0) != KOS_SUCCESS);

    KOS_instance_destroy(&inst);

    return 0;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../core/kos_heap.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "kos_test_tools.h"

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    kos_print_heap(ctx);

    KOS_instance_destroy(&inst);

    return 0;
}

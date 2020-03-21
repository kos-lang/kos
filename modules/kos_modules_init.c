/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_modules_init.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../core/kos_try.h"

#define MODULE_DEF(module) int kos_module_##module##_init(KOS_CONTEXT ctx, KOS_OBJ_ID module);
#include "kos_module_list.h"
#undef MODULE_DEF

int KOS_modules_init(struct KOS_THREAD_CONTEXT_S *ctx)
{
    int error = KOS_SUCCESS;
#define MODULE_DEF(module) TRY(KOS_instance_register_builtin(ctx, #module, kos_module_##module##_init));
#include "kos_module_list.h"
#undef MODULE_DEF
cleanup:
    return error;
}

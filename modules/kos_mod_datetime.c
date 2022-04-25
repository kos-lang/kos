/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_system.h"
#include "../core/kos_try.h"

static const char str_err_cannot_get_time[] = "failed to get system time";

/* @item datetime now()
 *
 *     now()
 *
 * Returns current time, in microseconds since the Epoch.
 *
 */
static KOS_OBJ_ID now(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  this_obj,
                      KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID    ret     = KOS_BADPTR;
    const int64_t time_us = KOS_get_time_us();

    if (time_us)
        ret = KOS_new_int(ctx, time_us);
    else
        KOS_raise_exception_cstring(ctx, str_err_cannot_get_time);

    return ret;
}

int kos_module_datetime_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_FUNCTION(ctx, module.o, "now", now, KOS_NULL);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

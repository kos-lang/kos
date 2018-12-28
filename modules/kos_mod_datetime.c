/*
 * Copyright (c) 2014-2018 Chris Dragan
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

#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../core/kos_try.h"
#ifdef _WIN32
    /* TODO */
#else
#   include <sys/time.h>
#endif

static const char str_err_cannot_get_time[] = "failed to get system time";

/* @item datetime now()
 *
 *     now()
 *
 * Returns current time, in milliseconds since Epoch, in UTC.
 *
 */
static KOS_OBJ_ID now(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  this_obj,
                      KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

#ifdef _WIN32
    /* TODO */
#else
    struct timeval  tv;
    struct timezone tz;

    if (!gettimeofday(&tv, &tz)) {

        int64_t time_ms = (int64_t)tv.tv_sec * 1000;

        time_ms += (int64_t)tv.tv_usec / 1000;

        ret = KOS_new_int(ctx, time_ms);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_cannot_get_time);
#endif

    return ret;
}

int kos_module_datetime_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int error  = KOS_SUCCESS;
    int pushed = 0;

    TRY(KOS_push_locals(ctx, &pushed, 1, &module));

    TRY_ADD_FUNCTION(ctx, module, "now", now, 0);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}
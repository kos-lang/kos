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

#include "kos_test_tools.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../core/kos_malloc.h"
#include "../core/kos_memory.h"
#include "../core/kos_system.h"
#include "../core/kos_try.h"

int create_thread(KOS_CONTEXT          ctx,
                  KOS_FUNCTION_HANDLER proc,
                  void                *cookie,
                  KOS_OBJ_ID          *thread)
{
    int        error    = KOS_SUCCESS;
    int        pushed   = 0;
    KOS_OBJ_ID func_obj = KOS_BADPTR;
    KOS_OBJ_ID args_obj = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 1, &func_obj));

    func_obj = KOS_new_builtin_function(ctx, proc, 0);
    TRY_OBJID(func_obj);

    args_obj = KOS_new_array(ctx, 0);
    TRY_OBJID(args_obj);

    *thread = kos_thread_create(ctx, func_obj, (KOS_OBJ_ID)cookie, args_obj);

    TRY_OBJID(*thread);

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error;
}

int join_thread(KOS_CONTEXT ctx,
                KOS_OBJ_ID  thread)
{
    const KOS_OBJ_ID retval = kos_thread_join(ctx, thread);

    return IS_BAD_PTR(retval) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

int get_num_cpus(void)
{
    int        num_cpus = 2; /* By default behave as if there were 2 CPUs */
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    if (kos_get_env("TEST_CPUS", &cstr) == KOS_SUCCESS) {
        num_cpus = (int)strtol(cstr.buffer, 0, 10);
        if (num_cpus < 1) {
            kos_vector_destroy(&cstr);
            printf("Failed: Invalid value in TEST_CPUS env var!\n");
            exit(1);
        }
    }

    kos_vector_destroy(&cstr);

    return num_cpus;
}

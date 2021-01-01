/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "kos_test_tools.h"
#include "../inc/kos_array.h"
#include "../inc/kos_const_strings.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_memory.h"
#include "../core/kos_config.h"
#include "../core/kos_system.h"
#include "../core/kos_try.h"

int create_thread(KOS_CONTEXT          ctx,
                  KOS_FUNCTION_HANDLER proc,
                  void                *cookie,
                  KOS_THREAD         **thread)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID args_obj = KOS_BADPTR;
    KOS_LOCAL  func;

    KOS_init_local(ctx, &func);

    func.o = KOS_new_builtin_function(ctx, KOS_STR_EMPTY, proc, 0);
    TRY_OBJID(func.o);

    args_obj = KOS_new_array(ctx, 0);
    TRY_OBJID(args_obj);

    *thread = kos_thread_create(ctx, func.o, (KOS_OBJ_ID)cookie, args_obj);

    if ( ! *thread)
        error = KOS_ERROR_EXCEPTION;

cleanup:
    KOS_destroy_top_local(ctx, &func);

    return error;
}

int join_thread(KOS_CONTEXT ctx,
                KOS_THREAD *thread)
{
    const KOS_OBJ_ID retval = kos_thread_join(ctx, thread);

    return IS_BAD_PTR(retval) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

int get_num_cpus(void)
{
    int        num_cpus = 2; /* By default behave as if there were 2 CPUs */
    KOS_VECTOR cstr;

    KOS_vector_init(&cstr);

    if (kos_get_env("TEST_CPUS", &cstr) == KOS_SUCCESS) {
        num_cpus = (int)strtol(cstr.buffer, 0, 10);
        if (num_cpus < 1) {
            KOS_vector_destroy(&cstr);
            printf("Failed: Invalid value in TEST_CPUS env var!\n");
            exit(1);
        }
    }

    KOS_vector_destroy(&cstr);

    /* Don't try to create more threads than the max number of threads supported */
    if ((unsigned)num_cpus > KOS_MAX_THREADS)
        num_cpus = KOS_MAX_THREADS;

    return num_cpus;
}

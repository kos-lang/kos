/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_threads.h"
#include "kos_test_tools.h"

typedef struct INIT_DATA_S {
    KOS_ATOMIC(uint32_t) go;
} INIT_DATA;

static KOS_OBJ_ID test_thread_func(KOS_CONTEXT ctx,
                                   KOS_OBJ_ID  this_obj,
                                   KOS_OBJ_ID  args_obj)
{
    int        error;
    INIT_DATA *go = (INIT_DATA *)this_obj;

    while ( ! KOS_atomic_read_relaxed_u32(go->go))
        kos_yield(); /* TODO use condition var */

    error = KOS_collect_garbage(ctx, 0);

    return error ? KOS_BADPTR : KOS_VOID;
}

int main(void)
{
    const int num_loops   = 1024;
    const int num_cpus    = get_num_cpus();
    const int num_threads = num_cpus < 2 ? 2 : num_cpus - 1;
    int       i_loop;

    for (i_loop = 0; i_loop < num_loops; i_loop++) {
        KOS_INSTANCE inst;
        KOS_CONTEXT  ctx;
        int          i_thread;
        INIT_DATA    go;

        KOS_atomic_write_relaxed_u32(go.go, 0U);

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        for (i_thread = 0; i_thread < num_threads; i_thread++) {

            KOS_THREAD *thread = 0;

            TEST(create_thread(ctx, test_thread_func, &go, &thread) == KOS_SUCCESS);

            kos_thread_add_ref(thread);
            kos_thread_disown(thread);
        }

        KOS_atomic_write_relaxed_u32(go.go, 1U);

        KOS_instance_destroy(&inst);
    }

    return 0;
}

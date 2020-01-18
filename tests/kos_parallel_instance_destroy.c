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
        KOS_OBJ_ID   prev_locals;
        int          i_thread;
        INIT_DATA    go;

        KOS_atomic_write_relaxed_u32(go.go, 0U);

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

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

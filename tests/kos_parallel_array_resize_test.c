/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_system.h"
#include "kos_test_tools.h"

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_LOCAL            obj;
    int                  num_idcs;
    KOS_ATOMIC(uint32_t) stage;
    KOS_ATOMIC(uint32_t) done;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA    *test;
    int                  first_idx;
    KOS_ATOMIC(uint32_t) num_loops;
};

static int run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
{
    struct TEST_DATA *test  = data->test;
    uint32_t          stage = 0;

    for (;;) {

        KOS_OBJ_ID object;
        int        idx;
        const int  first_idx = data->first_idx;
        const int  end_idx   = first_idx + test->num_idcs;

        for (;;) {
            const uint32_t cur_stage = KOS_atomic_read_relaxed_u32(test->stage);

            if (cur_stage > stage) {
                stage = cur_stage;
                break;
            }

            KOS_help_gc(ctx);
        }
        if (stage == ~0U)
            break;

        object = test->obj.o;

        for (idx = first_idx; idx < end_idx; idx++) {
            const KOS_OBJ_ID value = TO_SMALL_INT(idx);

            TEST(KOS_array_write(ctx, object, idx, value) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (idx = end_idx; idx > first_idx; idx--) {
            const KOS_OBJ_ID expected = TO_SMALL_INT(idx-1);
            const KOS_OBJ_ID actual   = KOS_array_read(ctx, object, idx-1);
            const KOS_OBJ_ID new_val  = TO_SMALL_INT(-(idx-1));

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_array_write(ctx, object, idx-1, new_val) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (idx = first_idx; idx < end_idx; idx++) {
            const KOS_OBJ_ID expected = TO_SMALL_INT(-idx);
            const KOS_OBJ_ID actual   = KOS_array_read(ctx, object, idx);

            TEST_NO_EXCEPTION();
            TEST(actual == expected);
        }

        KOS_atomic_add_i32(test->done,      1);
        KOS_atomic_add_i32(data->num_loops, 1);
    }

    return 0;
}

static KOS_OBJ_ID test_thread_func(KOS_CONTEXT ctx,
                                   KOS_OBJ_ID  this_obj,
                                   KOS_OBJ_ID  args_obj)
{
    struct THREAD_DATA *test  = (struct THREAD_DATA *)this_obj;

    if (run_test(ctx, test)) {
        KOS_atomic_add_i32(test->test->done, 1);
        KOS_atomic_add_i32(test->test->error, 1);
    }

    return KOS_is_exception_pending(ctx) ? KOS_BADPTR : KOS_VOID;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    const int    num_cpus = get_num_cpus();

    TEST(KOS_instance_init(&inst, 0, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* This test overwrites array indices from multiple threads while the array
     * storage is being reallocated. */
    {
        const int           num_loops       = 1000 / (num_cpus > 100 ? 100 : num_cpus);
        const int           max_idcs_per_th = 100;
        KOS_VECTOR          mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        KOS_THREAD        **threads         = 0;
        int                 num_threads     = 0;
        int                 num_idcs;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads;

        num_idcs = num_threads * max_idcs_per_th;

        KOS_init_local(ctx, &data.obj);

        data.obj.o = KOS_new_array(ctx, (uint32_t)num_idcs);
        TEST( ! IS_BAD_PTR(data.obj.o));

        KOS_vector_init(&mem_buf);
        TEST(KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (KOS_THREAD **)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test      = &data;
            thread_cookies[i].first_idx = i * max_idcs_per_th;
            thread_cookies[i].num_loops = 0;
        }

        data.inst     = &inst;
        data.num_idcs = max_idcs_per_th;
        data.stage    = 0U;
        data.done     = 0U;
        data.error    = KOS_SUCCESS;

        for (i = 0; i < num_threads; i++)
            TEST(create_thread(ctx, test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            KOS_atomic_add_i32(data.stage, 1);

            do {
                TEST(kos_array_copy_storage(ctx, data.obj.o) == KOS_SUCCESS);
                KOS_suspend_context(ctx);
                kos_yield();
                KOS_resume_context(ctx);
            } while (KOS_atomic_read_relaxed_u32(data.done) != (uint32_t)num_threads);

            KOS_atomic_write_relaxed_u32(data.done, 0U);

            TEST( ! data.error);

            for (i = 0; i < num_idcs; i++) {
                KOS_OBJ_ID value = KOS_array_read(ctx, data.obj.o, i);
                TEST_NO_EXCEPTION();
                TEST(value == TO_SMALL_INT(-i));
            }
        }

        for (i = 0; i < num_threads; i++)
            KOS_atomic_write_relaxed_u32(data.stage, ~0U);
        KOS_atomic_full_barrier();

        for (i = 0; i < num_threads; i++) {
            join_thread(ctx, threads[i]);
            TEST_NO_EXCEPTION();
            TEST(KOS_atomic_read_relaxed_u32(thread_cookies[i].num_loops) == (uint32_t)num_loops);
        }

        KOS_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

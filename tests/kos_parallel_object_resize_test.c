/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_object.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_system.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_LOCAL            object;
    KOS_LOCAL           *prop_names;
    int                  num_props;
    KOS_ATOMIC(uint32_t) stage;
    KOS_ATOMIC(uint32_t) done;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA    *test;
    int                  first_prop;
    KOS_ATOMIC(uint32_t) num_loops;
};

static int run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
{
    struct TEST_DATA *test  = data->test;
    uint32_t          stage = 0;

    for (;;) {

        int        i_prop;
        const int  first_prop = data->first_prop;
        const int  end_prop   = first_prop + test->num_props;

        for (;;) {
            const uint32_t cur_stage = (uint32_t)KOS_atomic_add_i32(test->stage, 0);

            if (cur_stage > stage) {
                stage = cur_stage;
                break;
            }

            KOS_suspend_context(ctx);
            kos_yield();
            KOS_resume_context(ctx);
        }
        if (stage == ~0U)
            break;

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop].o;
            const KOS_OBJ_ID value = TO_SMALL_INT(i_prop);

            TEST(KOS_set_property(ctx, test->object.o, key, value) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop-1].o;
            const KOS_OBJ_ID expected = TO_SMALL_INT(i_prop-1);
            const KOS_OBJ_ID actual   = KOS_get_property(ctx, test->object.o, key);
            const KOS_OBJ_ID new_val  = TO_SMALL_INT(-(i_prop-1));

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_set_property(ctx, test->object.o, key, new_val) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop].o;
            const KOS_OBJ_ID expected = TO_SMALL_INT(-i_prop);
            const KOS_OBJ_ID actual   = KOS_get_property(ctx, test->object.o, key);

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_delete_property(ctx, test->object.o, key) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop-1].o;
            const KOS_OBJ_ID value = KOS_get_property(ctx, test->object.o, key);

            TEST(IS_BAD_PTR(value));
            TEST_EXCEPTION();
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

#ifdef CONFIG_MAD_GC
    /* Mad GC needs a bigger heap */
    inst.heap.max_heap_size *= 2U;
#endif

    /************************************************************************/
    /* This test writes and deletes unique properties from multiple threads, checking for consistency */
    {
#ifdef CONFIG_MAD_GC
        const int           num_loops        = 1;
#else
        const int           num_loops        = 500 / (num_cpus > 100 ? 100 : num_cpus);
#endif
        const int           max_props_per_th = 100;
        KOS_VECTOR          mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        KOS_THREAD        **threads          = 0;
        int                 num_threads      = 0;
        int                 num_props;
        KOS_LOCAL          *props;
        struct KOS_RNG      rng;
        int                 i_loop;
        int                 i;

        KOS_init_local(ctx, &data.object);

        data.object.o = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(data.object.o));

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads;

        num_props = num_threads * max_props_per_th;

        kos_rng_init(&rng);

        KOS_vector_init(&mem_buf);
        TEST(KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_LOCAL)
            ) == KOS_SUCCESS);
        props          = (KOS_LOCAL *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (KOS_THREAD **)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test       = &data;
            thread_cookies[i].first_prop = i * max_props_per_th;
            thread_cookies[i].num_loops  = 0;
        }

        for (i = 0; i < num_props; i++) {
            char         buf[9];
            unsigned     k;
            int          idx_gen = i;
            const size_t len = sizeof(buf) - 1;

            for (k = 0; k < len / 2; k++)
                buf[k] = (char)(kos_rng_random_range(&rng, 0x7EU - 0x20U) + 0x20U);
            for (; k < len; k++) {
                if ((k > len / 2) && ! idx_gen)
                    break;
                buf[k] = (char)((idx_gen & 0x3FU) + 0x30U);
                idx_gen >>= 6;
            }
            buf[k] = '\0';

            KOS_init_local(ctx, &props[i]);

            props[i].o = KOS_new_cstring(ctx, buf);
            TEST( ! IS_BAD_PTR(props[i].o));
        }

        data.inst       = &inst;
        data.prop_names = props;
        data.num_props  = max_props_per_th;
        data.stage      = 0U;
        data.done       = 0U;
        data.error      = KOS_SUCCESS;

        for (i = 0; i < num_threads; i++)
            TEST(create_thread(ctx, test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            /* Limit number of copies made to avoid running out of memory */
            int copies_left = 1000;

            KOS_atomic_add_i32(data.stage, 1);

            do {
                if (copies_left-- > 0)
                    TEST(kos_object_copy_prop_table(ctx, data.object.o) == KOS_SUCCESS);
                KOS_suspend_context(ctx);
                kos_yield();
                KOS_resume_context(ctx);
            } while (KOS_atomic_read_relaxed_u32(data.done) != (uint32_t)num_threads);

            KOS_atomic_write_relaxed_u32(data.done, 0U);

            TEST( ! data.error);

            for (i = 0; i < num_props; i++) {
                KOS_OBJ_ID value = KOS_get_property(ctx, data.object.o, props[i].o);
                TEST(IS_BAD_PTR(value));
                TEST_EXCEPTION();
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

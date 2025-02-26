/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_system.h"
#include "../inc/kos_threads.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"
#include <time.h>

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_LOCAL            object;
    KOS_LOCAL           *prop_names;
    size_t               num_props;
    size_t               num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    unsigned          rand_init;
};

static int write_props_inner(KOS_CONTEXT       ctx,
                             struct TEST_DATA *test,
                             unsigned          rand_init)
{
    size_t   i;
    unsigned n = rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = test->prop_names[n % test->num_props].o;
        KOS_OBJ_ID value = TO_SMALL_INT((int)((n % 32) - 16));
        if (!((n & 0xF00U)))
            TEST(KOS_delete_property(ctx, test->object.o, key) == KOS_SUCCESS);
        else
            TEST(KOS_set_property(ctx, test->object.o, key, value) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        n = n * 0x8088405 + 1;
    }

    return 0;
}

static KOS_OBJ_ID write_props(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)this_obj;

    while (!KOS_atomic_read_relaxed_u32(test->test->go)) {
        KOS_suspend_context(ctx);
        kos_yield();
        KOS_resume_context(ctx);
    }

    if (write_props_inner(ctx, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);

    return KOS_is_exception_pending(ctx) ? KOS_BADPTR : KOS_VOID;
}

static int read_props_inner(KOS_CONTEXT       ctx,
                            struct TEST_DATA *test,
                            unsigned          rand_init)
{
    size_t   i;
    unsigned n = rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = test->prop_names[n % test->num_props].o;
        KOS_OBJ_ID value = KOS_get_property(ctx, test->object.o, key);
        if (IS_BAD_PTR(value)) {
            TEST(KOS_is_exception_pending(ctx));
            KOS_clear_exception(ctx);
        }
        else {
            TEST(!KOS_is_exception_pending(ctx));
            TEST(IS_SMALL_INT(value));
            TEST(GET_SMALL_INT(value) >= -16 && GET_SMALL_INT(value) < 16);
        }

        n = n * 0x8088405 + 1;
    }

    return 0;
}

static KOS_OBJ_ID read_props(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)this_obj;

    while (!KOS_atomic_read_relaxed_u32(test->test->go)) {
        KOS_suspend_context(ctx);
        kos_yield();
        KOS_resume_context(ctx);
    }

    if (read_props_inner(ctx, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);

    return KOS_is_exception_pending(ctx) ? KOS_BADPTR : KOS_VOID;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    const int    num_cpus = get_num_cpus();

    TEST(KOS_instance_init(&inst, 0, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* This test grows objects from multiple threads, causing lots of collisions */
    {
#ifdef CONFIG_MAD_GC
        const int           num_loops   = 50;
#else
        const int           num_loops   = 100;
#endif
        const int           num_props   = 128;
        KOS_VECTOR          mem_buf;
        KOS_THREAD        **threads     = 0;
        int                 num_threads = 0;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        struct KOS_RNG      rng;
        KOS_LOCAL          *props;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads; /* The main thread participates */

        KOS_vector_init(&mem_buf);
        TEST(KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_LOCAL)
            ) == KOS_SUCCESS);
        props          = (KOS_LOCAL *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (KOS_THREAD **)(thread_cookies + num_threads);

        kos_rng_init(&rng);

        for (i = 0; i < num_props; i++) {
            char     buf[3];
            unsigned k;

            for (k = 0; k < sizeof(buf); k++)
                buf[k] = (char)kos_rng_random_range(&rng, 127U);

            KOS_init_local(ctx, &props[i]);

            props[i].o = KOS_new_string(ctx, buf, sizeof(buf));
            TEST( ! IS_BAD_PTR(props[i].o));
        }

        data.inst       = &inst;
        data.prop_names = props;
        data.num_props  = num_props;
        data.num_loops  = num_props * 2U / (unsigned)num_threads;
        data.error      = KOS_SUCCESS;

        for (i_loop = 0; i_loop < num_loops; i_loop++) {

            data.go = 0;

            KOS_init_local(ctx, &data.object);

            data.object.o = KOS_new_object(ctx);
            TEST(!IS_BAD_PTR(data.object.o));

            for (i = 0; i < num_threads; i++) {
                thread_cookies[i].test      = &data;
                thread_cookies[i].rand_init = (unsigned)kos_rng_random_range(&rng, 0xFFFFFFFFU);
                TEST(create_thread(ctx, ((i & 7)) ? write_props : read_props, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);
            }

            i = (int)kos_rng_random_range(&rng, 0x7FFFFFFF);
            KOS_atomic_write_relaxed_u32(data.go, 1);
            TEST(write_props_inner(ctx, &data, (unsigned)i) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            for (i = num_threads - 1; i >= 0; i--) {
                join_thread(ctx, threads[i]);
                TEST_NO_EXCEPTION();
            }

            TEST(data.error == KOS_SUCCESS);

            for (i = 0; i < num_props; i++) {
                KOS_OBJ_ID value = KOS_get_property(ctx, data.object.o, props[i].o);
                if (IS_BAD_PTR(value))
                    TEST_EXCEPTION();
                else {
                    TEST_NO_EXCEPTION();
                    TEST(IS_SMALL_INT(value));
                    TEST(GET_SMALL_INT(value) >= -16 && GET_SMALL_INT(value) < 16);
                }
            }

            KOS_destroy_top_local(ctx, &data.object);

            TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);
        }

        KOS_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

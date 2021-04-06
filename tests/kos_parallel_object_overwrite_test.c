/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
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

/* These strings cause lots of collisions in the hash table */
KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(key0, 1, "\x00");
KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(key1, 1, "\x80");
KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(key2, 1, "\x01");
KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(key3, 1, "\x80");

static KOS_OBJ_ID props[4] = {
    KOS_CONST_ID(key0),
    KOS_CONST_ID(key1),
    KOS_CONST_ID(key2),
    KOS_CONST_ID(key3)
};

static const uint32_t num_props = (uint32_t)(sizeof(props) / sizeof(props[0]));

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_LOCAL            object;
    size_t               num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    int               rand_init;
};

static int write_props_inner(KOS_CONTEXT       ctx,
                             struct TEST_DATA *test,
                             int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = props[n % num_props];
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
                            int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = props[n % num_props];
        KOS_OBJ_ID value = KOS_get_property(ctx, test->object.o, key);
        if (IS_BAD_PTR(value))
            TEST_EXCEPTION();
        else {
            TEST_NO_EXCEPTION();
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

    struct KOS_RNG rng;

    kos_rng_init(&rng);

    TEST(KOS_instance_init(&inst, 0, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* This test performs multiple reads and writes at the same locations in the property table */
    {
        KOS_VECTOR          mem_buf;
        KOS_THREAD        **threads     = 0;
        const int           num_threads = num_cpus > 2 ? num_cpus - 1 : num_cpus;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        int                 i;

        KOS_init_local(ctx, &data.object);

        data.object.o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(data.object.o));

        KOS_vector_init(&mem_buf);
        TEST(KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (KOS_THREAD **)(thread_cookies + num_threads);

        data.inst       = &inst;
#ifdef CONFIG_MAD_GC
        data.num_loops  = 10000;
#else
        data.num_loops  = 1000000;
#endif
        data.go         = 0;
        data.error      = KOS_SUCCESS;

        data.num_loops /= (num_threads >> 2) + 1;

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test      = &data;
            thread_cookies[i].rand_init = (int)kos_rng_random_range(&rng, 0xFFFFFFFFU);
            TEST(create_thread(ctx, ((i & 1)) ? write_props : read_props, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);
        }

        i = (int)kos_rng_random_range(&rng, 0xFFFFFFFFU);
        KOS_atomic_write_relaxed_u32(data.go, 1);
        TEST(write_props_inner(ctx, &data, i) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < num_threads; i++) {
            join_thread(ctx, threads[i]);
            TEST_NO_EXCEPTION();
        }

        KOS_vector_destroy(&mem_buf);

        TEST(data.error == KOS_SUCCESS);

        for (i = 0; i < (int)num_props; i++) {
            KOS_OBJ_ID value = KOS_get_property(ctx, data.object.o, props[i]);
            if (IS_BAD_PTR(value)) {
                TEST(KOS_is_exception_pending(ctx));
                KOS_clear_exception(ctx);
            }
            else {
                TEST(!KOS_is_exception_pending(ctx));
                TEST(IS_SMALL_INT(value));
                TEST(GET_SMALL_INT(value) >= -16 && GET_SMALL_INT(value) < 16);
            }
        }
    }

    KOS_instance_destroy(&inst);

    return 0;
}

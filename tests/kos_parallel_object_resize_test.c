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

#include "../inc/kos_object.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_OBJ_ID           object;
    KOS_OBJ_ID          *prop_names;
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

static int _run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
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

            KOS_help_gc(ctx);
            kos_yield();
        }
        if (stage == ~0U)
            break;

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop];
            const KOS_OBJ_ID value = TO_SMALL_INT(i_prop);

            TEST(KOS_set_property(ctx, test->object, key, value) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop-1];
            const KOS_OBJ_ID expected = TO_SMALL_INT(i_prop-1);
            const KOS_OBJ_ID actual   = KOS_get_property(ctx, test->object, key);
            const KOS_OBJ_ID new_val  = TO_SMALL_INT(-(i_prop-1));

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_set_property(ctx, test->object, key, new_val) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop];
            const KOS_OBJ_ID expected = TO_SMALL_INT(-i_prop);
            const KOS_OBJ_ID actual   = KOS_get_property(ctx, test->object, key);

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_delete_property(ctx, test->object, key) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop-1];
            const KOS_OBJ_ID value = KOS_get_property(ctx, test->object, key);

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

    if (_run_test(ctx, test)) {
        KOS_atomic_add_i32(test->test->done, 1);
        KOS_atomic_add_i32(test->test->error, 1);
    }

    return KOS_VOID;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    const int    num_cpus = get_num_cpus();

    TEST(KOS_instance_init(&inst, 0, &ctx) == KOS_SUCCESS);

#ifdef CONFIG_MAD_GC
    /* Mad GC needs a bigger heap */
    inst.heap.max_size *= 2U;
#endif

    /************************************************************************/
    /* This test writes and deletes unique properties from multiple threads, checking for consistency */
    {
#ifdef CONFIG_MAD_GC
        const int           num_loops        = 1;
#else
        const int           num_loops        = 1000 / (num_cpus > 100 ? 100 : num_cpus);
#endif
        const int           max_props_per_th = 100;
        KOS_VECTOR          mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        KOS_OBJ_ID         *threads          = 0;
        int                 num_threads      = 0;
        int                 num_props;
        KOS_OBJ_ID         *props;
        struct KOS_RNG      rng;
        int                 i_loop;
        int                 i;

        data.object = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(data.object));

        {
            int pushed = 0;
            TEST(KOS_push_locals(ctx, &pushed, 1, &data.object) == KOS_SUCCESS);
        }

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads;

        num_props = num_threads * max_props_per_th;

        kos_rng_init(&rng);

        kos_vector_init(&mem_buf);
        TEST(kos_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_OBJ_ID)
            ) == KOS_SUCCESS);
        props          = (KOS_OBJ_ID *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (KOS_OBJ_ID *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test       = &data;
            thread_cookies[i].first_prop = i * max_props_per_th;
            thread_cookies[i].num_loops  = 0;
        }

        for (i = 0; i < num_props; i++) {
            char     buf[8];
            unsigned k;
            int      pushed = 0;

            for (k = 0; k < sizeof(buf); k++) {
                if (k + 4U < sizeof(buf))
                    buf[k] = (char)kos_rng_random_range(&rng, 127U);
                else
                    buf[k] = (char)((i >> ((sizeof(buf) - 1 - k) * 7) & 0x7F));
            }

            props[i] = KOS_new_string(ctx, buf, sizeof(buf));
            TEST( ! IS_BAD_PTR(props[i]));

            TEST(KOS_push_locals(ctx, &pushed, 1, &props[i]) == KOS_SUCCESS);
        }

        data.inst       = &inst;
        data.prop_names = props;
        data.num_props  = max_props_per_th;
        data.stage      = 0U;
        data.done       = 0U;
        data.error      = KOS_SUCCESS;

        for (i = 0; i < num_threads; i++) {
            int pushed = 0;
            TEST(create_thread(ctx, test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);
            TEST(KOS_push_locals(ctx, &pushed, 1, &threads[i]) == KOS_SUCCESS);
        }

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            /* Limit number of copies made to avoid running out of memory */
            int copies_left = 1000;

            KOS_atomic_add_i32(data.stage, 1);

            do {
                if (copies_left-- > 0)
                    TEST(kos_object_copy_prop_table(ctx, data.object) == KOS_SUCCESS);
                kos_yield();
            } while (KOS_atomic_read_relaxed_u32(data.done) != (uint32_t)num_threads);

            KOS_atomic_write_relaxed_u32(data.done, 0U);

            TEST( ! data.error);

            for (i = 0; i < num_props; i++) {
                KOS_OBJ_ID value = KOS_get_property(ctx, data.object, props[i]);
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

        kos_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

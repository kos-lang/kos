/*
 * Copyright (c) 2014-2017 Chris Dragan
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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../core/kos_file.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_threads.h"
#include "kos_parallel.h"

struct TEST_DATA {
    KOS_CONTEXT         *ctx;
    KOS_OBJ_ID           object;
    KOS_OBJ_ID          *prop_names;
    int                  num_props;
    int                  num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    int               first_prop;
    struct KOS_RNG    rng;
};

static int _run_test(KOS_FRAME frame, struct THREAD_DATA *data)
{
    struct TEST_DATA *test       = data->test;
    const int         first_prop = data->first_prop;
    int               i;

    while ( ! KOS_atomic_read_u32(test->go))
        KOS_atomic_full_barrier();

    for (i = 0; i < test->num_loops; i++) {
        const int rnd_num   = (int)_KOS_rng_random_range(&data->rng, ((unsigned)test->num_props / 4U) - 1U);
        const int num_props = (3 * test->num_props / 4) + rnd_num;
        const int end_prop  = first_prop + num_props;
        int       i_prop;

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop];
            const KOS_OBJ_ID value = TO_SMALL_INT(i_prop);

            TEST(KOS_set_property(frame, test->object, key, value) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop-1];
            const KOS_OBJ_ID expected = TO_SMALL_INT(i_prop-1);
            const KOS_OBJ_ID actual   = KOS_get_property(frame, test->object, key);
            const KOS_OBJ_ID new_val  = TO_SMALL_INT(-(i_prop-1));

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_set_property(frame, test->object, key, new_val) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key      = test->prop_names[i_prop];
            const KOS_OBJ_ID expected = TO_SMALL_INT(-i_prop);
            const KOS_OBJ_ID actual   = KOS_get_property(frame, test->object, key);

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_delete_property(frame, test->object, key) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key   = test->prop_names[i_prop-1];
            const KOS_OBJ_ID value = KOS_get_property(frame, test->object, key);

            TEST(IS_BAD_PTR(value));
            TEST_EXCEPTION();
        }
    }

    return 0;
}

static void _test_thread_func(KOS_FRAME frame,
                              void     *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;

    if (_run_test(frame, test))
        KOS_atomic_add_i32(test->test->error, 1);
}

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;
    const int   num_cpus = _get_num_cpus();

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* This test writes and deletes unique properties from multiple threads, checking for consistency */
    {
        const int           num_loops        = 1024;
        const int           num_thread_loops = 3;
        const int           max_props_per_th = 16;
        struct _KOS_VECTOR  mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        _KOS_THREAD        *threads          = 0;
        int                 num_threads      = 0;
        int                 num_props;
        KOS_OBJ_ID         *props;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;
        if (num_threads < 2)
            num_threads = 2;

        num_props = num_threads * max_props_per_th;

        _KOS_vector_init(&mem_buf);
        TEST(_KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_OBJ_ID)
            ) == KOS_SUCCESS);
        props          = (KOS_OBJ_ID *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            _KOS_rng_init(&thread_cookies[i].rng);
            thread_cookies[i].test       = &data;
            thread_cookies[i].first_prop = i * max_props_per_th;
        }

        for (i = 0; i < num_props; i++) {
            char     buf[8];
            unsigned k;

            for (k = 0; k < sizeof(buf); k++)
                if (k + 4U < sizeof(buf))
                    buf[k] = (char)_KOS_rng_random_range(&thread_cookies->rng, 127U);
                else
                    buf[k] = (char)((i >> ((sizeof(buf) - 1 - k) * 7) & 0x7F));

            props[i] = KOS_new_string(frame, buf, sizeof(buf));
            TEST( ! IS_BAD_PTR(props[i]));
        }

        data.ctx        = &ctx;
        data.prop_names = props;
        data.num_props  = max_props_per_th;
        data.num_loops  = num_thread_loops;
        data.error      = KOS_SUCCESS;

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            KOS_OBJ_ID o = KOS_new_object(frame);
            data.object  = o;
            data.go      = 0;

            TEST(!IS_BAD_PTR(o));

            /* Start with 1, because 0 is for the main thread, which participates */
            for (i = 1; i < num_threads; i++)
                TEST(_KOS_thread_create(frame, _test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

            KOS_atomic_write_u32(data.go, 1);
            TEST(_run_test(frame, thread_cookies) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            for (i = 1; i < num_threads; i++) {
                _KOS_thread_join(frame, threads[i]);
                TEST_NO_EXCEPTION();
            }

            TEST(data.error == KOS_SUCCESS);

            for (i = 0; i < num_props; i++) {
                KOS_OBJ_ID value = KOS_get_property(frame, o, props[i]);
                TEST(IS_BAD_PTR(value));
                TEST_EXCEPTION();
            }
        }

        _KOS_vector_destroy(&mem_buf);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

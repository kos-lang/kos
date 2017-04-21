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
    KOS_STRING          *prop_names;
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

static int _run_test(KOS_STACK_FRAME *frame, struct THREAD_DATA *data)
{
    struct TEST_DATA *test  = data->test;
    uint32_t          stage = 0;

    for (;;) {

        KOS_OBJ_ID object;
        int        i_prop;
        const int  first_prop = data->first_prop;
        const int  end_prop   = first_prop + test->num_props;

        for (;;) {
            const uint32_t cur_stage = (uint32_t)KOS_atomic_add_i32(test->stage, 0);

            if (cur_stage > stage) {
                stage = cur_stage;
                break;
            }

            _KOS_yield();
        }
        if (stage == ~0U)
            break;

        object = test->object;

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key   = OBJID(STRING, &test->prop_names[i_prop]);
            const KOS_OBJ_ID value = TO_SMALL_INT(i_prop);

            TEST(KOS_set_property(frame, object, key, value) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key      = OBJID(STRING, &test->prop_names[i_prop-1]);
            const KOS_OBJ_ID expected = TO_SMALL_INT(i_prop-1);
            const KOS_OBJ_ID actual   = KOS_get_property(frame, object, key);
            const KOS_OBJ_ID new_val  = TO_SMALL_INT(-(i_prop-1));

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_set_property(frame, object, key, new_val) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = first_prop; i_prop < end_prop; i_prop++) {
            const KOS_OBJ_ID key      = OBJID(STRING, &test->prop_names[i_prop]);
            const KOS_OBJ_ID expected = TO_SMALL_INT(-i_prop);
            const KOS_OBJ_ID actual   = KOS_get_property(frame, object, key);

            TEST_NO_EXCEPTION();
            TEST(actual == expected);

            TEST(KOS_delete_property(frame, object, key) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i_prop = end_prop; i_prop > first_prop; i_prop--) {
            const KOS_OBJ_ID key   = OBJID(STRING, &test->prop_names[i_prop-1]);
            const KOS_OBJ_ID value = KOS_get_property(frame, object, key);

            TEST(IS_BAD_PTR(value));
            TEST_EXCEPTION();
        }

        KOS_atomic_add_i32(test->done,      1);
        KOS_atomic_add_i32(data->num_loops, 1);
    }

    return 0;
}

static void _test_thread_func(KOS_STACK_FRAME *frame,
                              void            *cookie)
{
    struct THREAD_DATA *test  = (struct THREAD_DATA *)cookie;

    if (_run_test(frame, test)) {
        KOS_atomic_add_i32(test->test->done, 1);
        KOS_atomic_add_i32(test->test->error, 1);
    }
}

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;
    const int        num_cpus = _get_num_cpus();

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* This test writes and deletes unique properties from multiple threads, checking for consistency */
    {
        const int           num_loops        = 1000 / (num_cpus > 100 ? 100 : num_cpus);
        const int           max_props_per_th = 100;
        struct _KOS_VECTOR  mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        _KOS_THREAD        *threads          = 0;
        int                 num_threads      = 0;
        int                 num_props;
        KOS_STRING         *props;
        struct KOS_RNG      rng;
        KOS_OBJ_ID          obj              = KOS_new_object(frame);
        int                 i_loop;
        int                 i;

        TEST( ! IS_BAD_PTR(obj));

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads;

        num_props = num_threads * max_props_per_th;

        _KOS_rng_init(&rng);

        _KOS_vector_init(&mem_buf);
        TEST(_KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_STRING)
            ) == KOS_SUCCESS);
        props          = (KOS_STRING *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test       = &data;
            thread_cookies[i].first_prop = i * max_props_per_th;
            thread_cookies[i].num_loops  = 0;
        }

        for (i = 0; i < num_props; i++) {
            unsigned k;

            KOS_STRING *str = props + i;
            str->elem_size  = KOS_STRING_ELEM_8;
            str->flags      = KOS_STRING_LOCAL;
            str->length     = 8U;
            str->hash       = 0;

            for (k = 0; k < str->length; k++)
                if (k + 4U < str->length)
                    str->data.buf[k] = (char)_KOS_rng_random_range(&rng, 255U);
                else
                    str->data.buf[k] = (char)(i >> ((str->length - 1 - k) * 8));
        }

        data.ctx        = &ctx;
        data.object     = obj;
        data.prop_names = props;
        data.num_props  = max_props_per_th;
        data.stage      = 0U;
        data.done       = 0U;
        data.error      = KOS_SUCCESS;

        for (i = 0; i < num_threads; i++)
            TEST(_KOS_thread_create(&ctx, _test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            /* Limit number of copies made to avoid running out of memory */
            int copies_left = 1000;

            KOS_atomic_add_i32(data.stage, 1);

            do {
                if (copies_left-- > 0)
                    TEST(_KOS_object_copy_prop_table(frame, obj) == KOS_SUCCESS);
                _KOS_yield();
            } while (KOS_atomic_read_u32(data.done) != (uint32_t)num_threads);

            KOS_atomic_write_u32(data.done, 0U);

            TEST( ! data.error);

            for (i = 0; i < num_props; i++) {
                KOS_OBJ_ID value = KOS_get_property(frame, obj, OBJID(STRING, &props[i]));
                TEST(IS_BAD_PTR(value));
                TEST_EXCEPTION();
            }
        }

        for (i = 0; i < num_threads; i++)
            KOS_atomic_write_u32(data.stage, ~0U);
        KOS_atomic_full_barrier();

        for (i = 0; i < num_threads; i++) {
            _KOS_thread_join(frame, threads[i]);
            TEST_NO_EXCEPTION();
            TEST(KOS_atomic_read_u32(thread_cookies[i].num_loops) == (uint32_t)num_loops);
        }

        _KOS_vector_destroy(&mem_buf);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

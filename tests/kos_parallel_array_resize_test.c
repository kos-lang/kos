/*
 * Copyright (c) 2014-2018 Chris Dragan
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

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_parallel.h"

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_OBJ_ID           object;
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

static int _run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
{
    struct TEST_DATA *test  = data->test;
    uint32_t          stage = 0;

    for (;;) {

        KOS_OBJ_ID object;
        int        idx;
        const int  first_idx = data->first_idx;
        const int  end_idx   = first_idx + test->num_idcs;

        for (;;) {
            const uint32_t cur_stage = KOS_atomic_read_u32(test->stage);

            if (cur_stage > stage) {
                stage = cur_stage;
                break;
            }

            kos_yield();
        }
        if (stage == ~0U)
            break;

        object = test->object;

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

static void _test_thread_func(KOS_CONTEXT ctx,
                              void       *cookie)
{
    struct THREAD_DATA *test  = (struct THREAD_DATA *)cookie;

    if (_run_test(ctx, test)) {
        KOS_atomic_add_i32(test->test->done, 1);
        KOS_atomic_add_i32(test->test->error, 1);
    }
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    const int    num_cpus = _get_num_cpus();

    TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* This test overwrites array indices from multiple threads while the array
     * storage is being reallocated. */
    {
        const int           num_loops       = 1000 / (num_cpus > 100 ? 100 : num_cpus);
        const int           max_idcs_per_th = 100;
        struct _KOS_VECTOR  mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        _KOS_THREAD        *threads         = 0;
        int                 num_threads     = 0;
        int                 num_idcs;
        KOS_OBJ_ID          obj;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads;

        num_idcs = num_threads * max_idcs_per_th;

        obj = KOS_new_array(ctx, (uint32_t)num_idcs);
        TEST( ! IS_BAD_PTR(obj));

        kos_vector_init(&mem_buf);
        TEST(kos_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test      = &data;
            thread_cookies[i].first_idx = i * max_idcs_per_th;
            thread_cookies[i].num_loops = 0;
        }

        data.inst     = &inst;
        data.object   = obj;
        data.num_idcs = max_idcs_per_th;
        data.stage    = 0U;
        data.done     = 0U;
        data.error    = KOS_SUCCESS;

        for (i = 0; i < num_threads; i++)
            TEST(kos_thread_create(ctx, _test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            KOS_atomic_add_i32(data.stage, 1);

            do {
                TEST(kos_array_copy_storage(ctx, obj) == KOS_SUCCESS);
                kos_yield();
            } while (KOS_atomic_read_u32(data.done) != (uint32_t)num_threads);

            KOS_atomic_write_u32(data.done, 0U);

            TEST( ! data.error);

            for (i = 0; i < num_idcs; i++) {
                KOS_OBJ_ID value = KOS_array_read(ctx, obj, i);
                TEST_NO_EXCEPTION();
                TEST(value == TO_SMALL_INT(-i));
            }
        }

        for (i = 0; i < num_threads; i++)
            KOS_atomic_write_u32(data.stage, ~0U);
        KOS_atomic_full_barrier();

        for (i = 0; i < num_threads; i++) {
            kos_thread_join(ctx, threads[i]);
            TEST_NO_EXCEPTION();
            TEST(KOS_atomic_read_u32(thread_cookies[i].num_loops) == (uint32_t)num_loops);
        }

        kos_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

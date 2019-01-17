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

#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"
#include <memory.h>

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_OBJ_ID           buf;
    int                  num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    struct KOS_RNG    rng;
    uint8_t           id;
};

static int _run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
{
    struct TEST_DATA *test = data->test;
    int               i;

    while ( ! KOS_atomic_read_u32(test->go)) {
        KOS_help_gc(ctx);
        kos_yield();
    }

    for (i = 0; i < test->num_loops; i++) {
        const unsigned action = (unsigned)kos_rng_random_range(&data->rng, 4);

        switch (action) {

            case 0: {
                TEST(KOS_buffer_fill(ctx, test->buf, -8, -4, data->id) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            case 1: {
                TEST(KOS_buffer_copy(ctx, test->buf, -8, test->buf, 0, 8) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            default: {
                const unsigned delta = 64;
                uint8_t *b = KOS_buffer_make_room(ctx, test->buf, delta);
                TEST(b != 0);
                TEST_NO_EXCEPTION();
                memset(b, data->id, delta);
                break;
            }
        }
    }

    return 0;
}

static void _test_thread_func(KOS_CONTEXT ctx,
                              void       *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;

    if (_run_test(ctx, test))
        KOS_atomic_add_i32(test->test->error, 1);
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
    /* This test performs buffer make_room, fill and copy from multiple threads */
    {
        const int           num_loops        = 128;
        const int           num_thread_loops = 32;
        KOS_VECTOR          mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        THREAD             *threads          = 0;
        KOS_OBJ_ID          prev_locals      = KOS_BADPTR;
        int                 num_threads      = 0;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;
        if (num_threads < 2)
            num_threads = 2;

        kos_vector_init(&mem_buf);
        TEST(kos_vector_resize(&mem_buf,
                num_threads * (sizeof(THREAD) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (THREAD *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            kos_rng_init(&thread_cookies[i].rng);
            thread_cookies[i].test = &data;
            thread_cookies[i].id   = (uint8_t)(unsigned)((i & 0x1F) + 0x80);
            TEST((thread_cookies[i].id >= 0x80U) && (thread_cookies[i].id <= 0x9FU));
        }

        data.inst      = &inst;
        data.num_loops = num_thread_loops;
        data.error     = KOS_SUCCESS;
        data.buf       = KOS_BADPTR;

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);
        {
            int pushed = 0;
            TEST(KOS_push_locals(ctx, &pushed, 1, &data.buf) == KOS_SUCCESS);
        }

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            const unsigned size = 64;
            data.buf            = KOS_new_buffer(ctx, size);
            data.go             = 0;

            TEST(!IS_BAD_PTR(data.buf));

            /* Fill buffer with expected data */
            {
                uint8_t       *b   = KOS_buffer_data(data.buf);
                uint8_t *const end = b + size;

                i = 0;
                while (b < end)
                    *(b++) = (uint8_t)i++;
            }

            /* Start with 1, because 0 is for the main thread, which participates */
            for (i = 1; i < num_threads; i++)
                TEST(create_thread(ctx, _test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

            KOS_atomic_write_u32(data.go, 1);
            TEST(_run_test(ctx, thread_cookies) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            for (i = 1; i < num_threads; i++) {
                join_thread(ctx, threads[i]);
                TEST_NO_EXCEPTION();
            }

            TEST(data.error == KOS_SUCCESS);

            /* Check buffer contents */
            {
                const size_t   endsize = KOS_get_buffer_size(data.buf);
                uint8_t       *b       = KOS_buffer_data(data.buf);
                uint8_t *const end     = b + endsize;

                for (i = 0 ; b < end; i++, b++) {
                    const uint8_t v = *b;
                    if ((unsigned)i < size - 8U)
                        TEST((int)v == i);
                    else if ((unsigned)i < size)
                        TEST(((int)v == i) || (v < 8U) || ((v >= 0x80U) && (v <= 0x9FU)));
/*
    NOTE: This test is racy in nature.  In particular, one thread can be filling
    the buffer after KOS_buffer_make_room() returned and not be finished, while
    another thread resizes the buffer (capacity) again, which makes the first
    thread write to the old buffer and the new buffer retains garbage.
    For this reason, the check below does not make sense.
                    else
                        TEST((v < 8U) || ((v >= 0x80U) && (v <= 0x9FU)));
*/
                }
            }

            TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);
        }

        kos_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

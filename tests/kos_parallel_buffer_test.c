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

#include "../inc/kos_buffer.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../core/kos_file.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_threads.h"
#include "kos_parallel.h"
#include <memory.h>

struct TEST_DATA {
    KOS_CONTEXT         *ctx;
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

static int _run_test(KOS_STACK_FRAME *frame, struct THREAD_DATA *data)
{
    struct TEST_DATA *test = data->test;
    int               i;

    while ( ! KOS_atomic_read_u32(test->go))
        KOS_atomic_full_barrier();

    for (i = 0; i < test->num_loops; i++) {
        const unsigned action = (unsigned)_KOS_rng_random_range(&data->rng, 4);

        switch (action) {

            case 0: {
                TEST(KOS_buffer_fill(frame, test->buf, -8, -4, data->id) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            case 1: {
                TEST(KOS_buffer_copy(frame, test->buf, -8, test->buf, 0, 8) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            default: {
                const unsigned delta = 64;
                uint8_t *b = KOS_buffer_make_room(frame, test->buf, delta);
                TEST(b != 0);
                TEST_NO_EXCEPTION();
                memset(b, data->id, delta);
                break;
            }
        }
    }

    return 0;
}

static void _test_thread_func(KOS_STACK_FRAME *frame,
                              void            *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;

    if (_run_test(frame, test))
        KOS_atomic_add_i32(test->test->error, 1);
}

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;
    const int        num_cpus = _get_num_cpus();

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* This test performs buffer make_room, fill and copy from multiple threads */
    {
        const int           num_loops        = 128;
        const int           num_thread_loops = 32;
        struct _KOS_VECTOR  mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        _KOS_THREAD        *threads          = 0;
        int                 num_threads      = 0;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;
        if (num_threads < 2)
            num_threads = 2;

        _KOS_vector_init(&mem_buf);
        TEST(_KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            _KOS_rng_init(&thread_cookies[i].rng);
            thread_cookies[i].test = &data;
            thread_cookies[i].id   = (uint8_t)(unsigned)((i & 0x1F) + 0x80);
            TEST((thread_cookies[i].id >= 0x80U) && (thread_cookies[i].id <= 0x9FU));
        }

        data.ctx        = &ctx;
        data.num_loops  = num_thread_loops;
        data.error      = KOS_SUCCESS;

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            const unsigned size = 64;
            KOS_OBJ_ID     buf  = KOS_new_buffer(frame, size);
            data.buf            = buf;
            data.go             = 0;

            TEST(!IS_BAD_PTR(buf));

            /* Fill buffer with expected data */
            {
                uint8_t       *b   = KOS_buffer_data(buf);
                uint8_t *const end = b + size;

                i = 0;
                while (b < end)
                    *(b++) = (uint8_t)i++;
            }

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

            /* Check buffer contents */
            {
                const size_t   endsize = KOS_get_buffer_size(buf);
                uint8_t       *b       = KOS_buffer_data(buf);
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
        }

        _KOS_vector_destroy(&mem_buf);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

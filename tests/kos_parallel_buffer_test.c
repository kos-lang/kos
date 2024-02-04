/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_string.h"
#include "../inc/kos_system.h"
#include "../inc/kos_threads.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"
#include <memory.h>

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_LOCAL            buf;
    int                  num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    struct KOS_RNG    rng;
    uint8_t           id;
};

static int run_test(KOS_CONTEXT ctx, struct THREAD_DATA *data)
{
    struct TEST_DATA *test = data->test;
    int               i;

    while ( ! KOS_atomic_read_relaxed_u32(test->go)) {
        KOS_suspend_context(ctx);
        kos_yield();
        KOS_resume_context(ctx);
    }

    for (i = 0; i < test->num_loops; i++) {
        const unsigned action = (unsigned)kos_rng_random_range(&data->rng, 4);

        switch (action) {

            case 0: {
                TEST(KOS_buffer_fill(ctx, test->buf.o, -8, -4, data->id) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            case 1: {
                TEST(KOS_buffer_copy(ctx, test->buf.o, -8, test->buf.o, 0, 8) == KOS_SUCCESS);
                TEST_NO_EXCEPTION();
                break;
            }

            default: {
                const unsigned delta = 64;
                uint8_t *b = KOS_buffer_make_room(ctx, test->buf.o, delta);
                TEST(b != 0);
                TEST_NO_EXCEPTION();
                memset(b, data->id, delta);
                break;
            }
        }
    }

    return 0;
}

static KOS_OBJ_ID test_thread_func(KOS_CONTEXT ctx,
                                   KOS_OBJ_ID  this_obj,
                                   KOS_OBJ_ID  args_obj)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)this_obj;

    if (run_test(ctx, test))
        KOS_atomic_add_i32(test->test->error, 1);

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
    /* This test performs buffer make_room, fill and copy from multiple threads */
    {
        const int           num_loops        = 128;
        const int           num_thread_loops = 32;
        KOS_VECTOR          mem_buf;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        KOS_THREAD        **threads          = 0;
        int                 num_threads      = 0;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;
        if (num_threads < 2)
            num_threads = 2;

        KOS_vector_init(&mem_buf);
        TEST(KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(KOS_OBJ_ID) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (KOS_THREAD **)(thread_cookies + num_threads);

        for (i = 0; i < num_threads; i++) {
            kos_rng_init(&thread_cookies[i].rng);
            thread_cookies[i].test = &data;
            thread_cookies[i].id   = (uint8_t)(unsigned)((i & 0x1F) + 0x80);
            TEST((thread_cookies[i].id >= 0x80U) && (thread_cookies[i].id <= 0x9FU));
        }

        data.inst      = &inst;
        data.num_loops = num_thread_loops;
        data.error     = KOS_SUCCESS;

        KOS_init_local(ctx, &data.buf);

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            const unsigned size = 64;
            data.buf.o          = KOS_new_buffer(ctx, size);
            data.go             = 0;

            TEST(!IS_BAD_PTR(data.buf.o));

            /* Fill buffer with expected data */
            {
                uint8_t       *b   = KOS_buffer_data_volatile(ctx, data.buf.o);
                uint8_t *const end = b + size;

                TEST(b);

                i = 0;
                while (b < end)
                    *(b++) = (uint8_t)i++;
            }

            /* Start with 1, because 0 is for the main thread, which participates */
            for (i = 1; i < num_threads; i++)
                TEST(create_thread(ctx, test_thread_func, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);

            KOS_atomic_write_relaxed_u32(data.go, 1);
            TEST(run_test(ctx, thread_cookies) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            for (i = num_threads - 1; i > 0; i--) {
                join_thread(ctx, threads[i]);
                TEST_NO_EXCEPTION();
            }

            TEST(data.error == KOS_SUCCESS);

            /* Check buffer contents */
            {
                const size_t   endsize = KOS_get_buffer_size(data.buf.o);
                uint8_t       *b       = KOS_buffer_data_volatile(ctx, data.buf.o);
                uint8_t *const end     = b + endsize;

                TEST(b);

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

        KOS_vector_destroy(&mem_buf);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

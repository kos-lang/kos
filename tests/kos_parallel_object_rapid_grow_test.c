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
#include "../lang/kos_file.h"
#include "../lang/kos_memory.h"
#include "../lang/kos_object_internal.h"
#include "../lang/kos_threads.h"
#include "kos_parallel.h"
#include <time.h>

struct TEST_DATA {
    KOS_CONTEXT         *ctx;
    KOS_OBJ_PTR          object;
    KOS_STRING          *prop_names;
    size_t               num_props;
    size_t               num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    int               rand_init;
};

static int _write_props_inner(KOS_STACK_FRAME  *frame,
                              struct TEST_DATA *test,
                              int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_PTR key   = TO_OBJPTR(&test->prop_names[n % test->num_props]);
        KOS_OBJ_PTR value = TO_SMALL_INT((int)((n % 32) - 16));
        if (!((n & 0xF00U)))
            TEST(KOS_delete_property(frame, test->object, key) == KOS_SUCCESS);
        else
            TEST(KOS_set_property(frame, test->object, key, value) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        n = n * 0x8088405 + 1;
    }

    return 0;
}

static void _write_props(KOS_STACK_FRAME *frame,
                         void            *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;
    while (!KOS_atomic_read_u32(test->test->go))
        KOS_atomic_full_barrier();
    if (_write_props_inner(frame, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);
}

static int _read_props_inner(KOS_STACK_FRAME  *frame,
                             struct TEST_DATA *test,
                             int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_PTR key   = TO_OBJPTR(&test->prop_names[n % test->num_props]);
        KOS_OBJ_PTR value = KOS_get_property(frame, test->object, key);
        if (IS_BAD_PTR(value)) {
            TEST(KOS_is_exception_pending(frame));
            KOS_clear_exception(frame);
        }
        else {
            TEST(!KOS_is_exception_pending(frame));
            TEST(IS_SMALL_INT(value));
            TEST(GET_SMALL_INT(value) >= -16 && GET_SMALL_INT(value) < 16);
        }

        n = n * 0x8088405 + 1;
    }

    return 0;
}

static void _read_props(KOS_STACK_FRAME *frame,
                        void            *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;
    while (!KOS_atomic_read_u32(test->test->go))
        KOS_atomic_full_barrier();
    if (_read_props_inner(frame, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);
}

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;
    const int        num_cpus = _get_num_cpus();

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* This test grows objects from multiple threads, causing lots of collisions */
    {
        const int           num_loops   = 1000;
        const int           num_props   = 128;
        struct _KOS_VECTOR  mem_buf;
        _KOS_THREAD        *threads     = 0;
        int                 num_threads = 0;
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        KOS_STRING         *props;
        int                 i_loop;
        int                 i;

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads; /* The main thread participates */

        _KOS_vector_init(&mem_buf);
        TEST(_KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
                + num_props * sizeof(KOS_STRING)
            ) == KOS_SUCCESS);
        props          = (KOS_STRING *)mem_buf.buffer;
        thread_cookies = (struct THREAD_DATA *)(props + num_props);
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        srand((unsigned)time(0));

        for (i = 0; i < num_props; i++) {
            unsigned k;

            KOS_STRING *str = props + i;
            str->type       = OBJ_STRING_8;
            str->flags      = KOS_STRING_LOCAL;
            str->length     = 3U;
            str->hash       = 0;

            for (k = 0; k < str->length; k++)
                str->data.buf[k] = (char)rand();
        }

        data.ctx        = &ctx;
        data.prop_names = props;
        data.num_props  = num_props;
        data.num_loops  = num_props * 2U / (unsigned)num_threads;
        data.error      = KOS_SUCCESS;

        for (i_loop = 0; i_loop < num_loops; i_loop++) {
            KOS_OBJ_PTR o = KOS_new_object(frame);
            data.object   = o;
            data.go       = 0;

            TEST(!IS_BAD_PTR(o));

            for (i = 0; i < num_threads; i++) {
                thread_cookies[i].test      = &data;
                thread_cookies[i].rand_init = rand();
                TEST(_KOS_thread_create(&ctx, ((i & 7)) ? _write_props : _read_props, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);
            }

            i = rand();
            KOS_atomic_write_u32(data.go, 1);
            TEST(_write_props_inner(frame, &data, i) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            for (i = 0; i < num_threads; i++) {
                _KOS_thread_join(frame, threads[i]);
                TEST_NO_EXCEPTION();
            }

            TEST(data.error == KOS_SUCCESS);

            for (i = 0; i < (int)(sizeof(props)/sizeof(props[0])); i++) {
                KOS_OBJ_PTR value = KOS_get_property(frame, o, TO_OBJPTR(&props[i]));
                if (IS_BAD_PTR(value))
                    TEST_EXCEPTION();
                else {
                    TEST_NO_EXCEPTION();
                    TEST(IS_SMALL_INT(value));
                    TEST(GET_SMALL_INT(value) >= -16 && GET_SMALL_INT(value) < 16);
                }
            }
        }

        _KOS_vector_destroy(&mem_buf);
    }

    KOS_context_destroy(&ctx);

#ifdef CONFIG_OBJECT_STATS
    {
        const struct _KOS_OBJECT_STATS stats = _KOS_get_object_stats();
        printf("num_successful_resizes: %u\n", (unsigned)stats.num_successful_resizes);
        printf("num_failed_resizes:     %u\n", (unsigned)stats.num_failed_resizes);
        printf("num_successful_writes:  %u\n", (unsigned)stats.num_successful_writes);
        printf("num_failed_writes:      %u\n", (unsigned)stats.num_failed_writes);
        printf("num_successful_reads:   %u\n", (unsigned)stats.num_successful_reads);
        printf("num_failed_reads:       %u\n", (unsigned)stats.num_failed_reads);
    }
#endif

    return 0;
}

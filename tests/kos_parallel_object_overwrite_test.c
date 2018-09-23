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

#include "../inc/kos_object.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_object_internal.h"
#include "kos_parallel.h"
#include <time.h>

struct TEST_DATA {
    KOS_INSTANCE        *inst;
    KOS_OBJ_ID           object;
    KOS_OBJ_ID          *prop_names;
    size_t               num_props;
    size_t               num_loops;
    KOS_ATOMIC(uint32_t) go;
    KOS_ATOMIC(uint32_t) error;
};

struct THREAD_DATA {
    struct TEST_DATA *test;
    int               rand_init;
};

static int _write_props_inner(KOS_CONTEXT       ctx,
                              struct TEST_DATA *test,
                              int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = test->prop_names[n % test->num_props];
        KOS_OBJ_ID value = TO_SMALL_INT((int)((n % 32) - 16));
        if (!((n & 0xF00U)))
            TEST(KOS_delete_property(ctx, test->object, key) == KOS_SUCCESS);
        else
            TEST(KOS_set_property(ctx, test->object, key, value) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        n = n * 0x8088405 + 1;
    }

    return 0;
}

static void _write_props(KOS_CONTEXT ctx,
                         void       *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;
    while (!KOS_atomic_read_u32(test->test->go))
        KOS_atomic_full_barrier();
    if (_write_props_inner(ctx, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);
}

static int _read_props_inner(KOS_CONTEXT       ctx,
                             struct TEST_DATA *test,
                             int               rand_init)
{
    size_t   i;
    unsigned n = (unsigned)rand_init;

    for (i = 0; i < test->num_loops; i++) {
        KOS_OBJ_ID key   = test->prop_names[n % test->num_props];
        KOS_OBJ_ID value = KOS_get_property(ctx, test->object, key);
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

static void _read_props(KOS_CONTEXT ctx,
                        void       *cookie)
{
    struct THREAD_DATA *test = (struct THREAD_DATA *)cookie;
    while (!KOS_atomic_read_u32(test->test->go))
        KOS_atomic_full_barrier();
    if (_read_props_inner(ctx, test->test, test->rand_init))
        KOS_atomic_add_i32(test->test->error, 1);
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    const int    num_cpus = _get_num_cpus();

    TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* This test performs multiple reads and writes at the same locations in the property table */
    {
        struct _KOS_VECTOR  mem_buf;
        _KOS_THREAD        *threads     = 0;
        int                 num_threads = 0;
        KOS_OBJ_ID          o           = KOS_new_object(ctx);
        struct THREAD_DATA *thread_cookies;
        struct TEST_DATA    data;
        int                 i;

        /* These strings cause lots of collisions in the hash table */
        KOS_OBJ_ID props[4];
        props[0] = KOS_new_const_ascii_string(ctx, "\x00", 1);
        props[1] = KOS_new_const_ascii_string(ctx, "\x80", 1);
        props[2] = KOS_new_const_ascii_string(ctx, "\x01", 1);
        props[3] = KOS_new_const_ascii_string(ctx, "\x80", 1);

        TEST(!IS_BAD_PTR(o));

        num_threads = num_cpus;

        if (num_threads > 2)
            --num_threads; /* The main thread participates */

        _KOS_vector_init(&mem_buf);
        TEST(_KOS_vector_resize(&mem_buf,
                num_threads * (sizeof(_KOS_THREAD) + sizeof(struct THREAD_DATA))
            ) == KOS_SUCCESS);
        thread_cookies = (struct THREAD_DATA *)mem_buf.buffer;
        threads        = (_KOS_THREAD *)(thread_cookies + num_threads);

        data.inst       = &inst;
        data.object     = o;
        data.prop_names = props;
        data.num_props  = sizeof(props)/sizeof(props[0]);
        data.num_loops  = 1000000;
        data.go         = 0;
        data.error      = KOS_SUCCESS;

        srand((unsigned)time(0));

        for (i = 0; i < num_threads; i++) {
            thread_cookies[i].test      = &data;
            thread_cookies[i].rand_init = rand();
            TEST(_KOS_thread_create(ctx, ((i & 1)) ? _write_props : _read_props, &thread_cookies[i], &threads[i]) == KOS_SUCCESS);
        }

        i = rand();
        KOS_atomic_write_u32(data.go, 1);
        TEST(_write_props_inner(ctx, &data, i) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < num_threads; i++) {
            _KOS_thread_join(ctx, threads[i]);
            TEST_NO_EXCEPTION();
        }

        _KOS_vector_destroy(&mem_buf);

        TEST(data.error == KOS_SUCCESS);

        for (i = 0; i < (int)(sizeof(props)/sizeof(props[0])); i++) {
            KOS_OBJ_ID value = KOS_get_property(ctx, o, props[i]);
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

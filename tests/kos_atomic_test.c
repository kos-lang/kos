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

#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_threads.h"
#include <stdint.h>
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

static void *_ptr(int value)
{
    const uintptr_t hi = (value >> 8) & 0xFFU;
    const uintptr_t lo = value & 0xFFU;
    const uintptr_t v  = (hi << (sizeof(uintptr_t) * 8 - 8)) | lo;
    return (void *)v;
}

struct _THREAD_DATA {
    KOS_ATOMIC(uint32_t) lock;
    KOS_OBJ_ID           value;
};

static void _thread(KOS_CONTEXT ctx,
                    void       *cookie)
{
    struct _THREAD_DATA *thread_data = (struct _THREAD_DATA *)cookie;

    KOS_raise_exception(ctx, TO_SMALL_INT(GET_SMALL_INT(thread_data->value) + 1));

    _KOS_spin_unlock(&thread_data->lock);
}

int main(void)
{
    {
        KOS_ATOMIC(uint32_t) value;
        value = 0xCAFE;
        TEST(KOS_atomic_read_u32(value) == 0xCAFE);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0xBEEF;
        KOS_atomic_write_u32(value, 0xFEED);
        TEST(KOS_atomic_read_u32(value) == 0xFEED);
    }

    {
        KOS_ATOMIC(void *) value;
        value = _ptr(0xCAFE);
        TEST(KOS_atomic_read_ptr(value) == _ptr(0xCAFE));
    }

    {
        KOS_ATOMIC(void *) value;
        value = _ptr(0xBEEF);
        KOS_atomic_write_ptr(value, _ptr(0xFEED));
        TEST(KOS_atomic_read_ptr(value) == _ptr(0xFEED));
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0xBEAD;
        TEST( ! KOS_atomic_cas_u32(value, 0xC0DE, 0xFEED));
        TEST(KOS_atomic_read_u32(value) == 0xBEAD);
        TEST(KOS_atomic_cas_u32(value, 0xBEAD, 0xFEED));
        TEST(KOS_atomic_read_u32(value) == 0xFEED);
    }

    {
        KOS_ATOMIC(void *) value;
        value = _ptr(0xBEAD);
        TEST( ! KOS_atomic_cas_ptr(value, _ptr(0xC0DE), _ptr(0xFEED)));
        TEST(KOS_atomic_read_ptr(value) == _ptr(0xBEAD));
        TEST(KOS_atomic_cas_ptr(value, _ptr(0xBEAD), _ptr(0xFEED)));
        TEST(KOS_atomic_read_ptr(value) == _ptr(0xFEED));
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0xF00D;
        TEST(KOS_atomic_add_i32(value, 2) == 0xF00D);
        TEST(KOS_atomic_read_u32(value) == 0xF00F);
        TEST(KOS_atomic_add_i32(value, 0xEE0-2) == 0xF00F);
        TEST(KOS_atomic_read_u32(value) == 0xFEED);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0xCAFE;
        TEST(KOS_atomic_swap_u32(value, 0xBEEF) == 0xCAFE);
        TEST(KOS_atomic_read_u32(value) == 0xBEEF);
        TEST(KOS_atomic_swap_u32(value, 0xF00D) == 0xBEEF);
        TEST(KOS_atomic_read_u32(value) ==  0xF00D);
    }

    /* Test if compare-and-swap is strong */
    {
        KOS_ATOMIC(uint32_t) value;
        int                  i;

        value = 0xBEEFU;

        for (i = 0; i < 1024; i++) {
            uint32_t oldv = (i & 1) ? 0xFACEU : 0xBEEFU;
            uint32_t newv = (i & 1) ? 0xBEEFU : 0xFACEU;

            TEST(KOS_atomic_cas_u32(value, oldv, newv));
            TEST(KOS_atomic_read_u32(value) == newv);
        }
    }

    /* Test if compare-and-swap is strong */
    {
        KOS_ATOMIC(void *) value;
        int                i;

        value = (void *)0;

        for (i = 0; i < 1024; i++) {
            void *oldv = (i & 1) ? (void *)&value : (void *)0;
            void *newv = (i & 1) ? (void *)0      : (void *)&value;

            TEST(KOS_atomic_cas_ptr(value, oldv, newv));
            TEST(KOS_atomic_read_ptr(value) == newv);
        }
    }

    /* Basic thread test */
    {
        KOS_INSTANCE        inst;
        KOS_CONTEXT         ctx;
        struct _THREAD_DATA thread_data;
        _KOS_THREAD         thread;

        thread_data.lock  = 0;
        thread_data.value = TO_SMALL_INT(44);

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        _KOS_spin_lock(&thread_data.lock);

        TEST(_KOS_thread_create(ctx, _thread, &thread_data, &thread) == KOS_SUCCESS);

        _KOS_spin_lock(&thread_data.lock);

        _KOS_thread_join(ctx, thread);

        _KOS_spin_unlock(&thread_data.lock);

        TEST(KOS_is_exception_pending(ctx));

        TEST(KOS_get_exception(ctx) == TO_SMALL_INT(45));

        KOS_instance_destroy(&inst);
    }

    return 0;
}

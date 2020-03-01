/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#include "../inc/kos_atomic.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include <stdint.h>
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

static void *ptrv(uint32_t hi32, uint32_t lo32)
{
    const uintptr_t hi = hi32;
    const uintptr_t lo = lo32;
    const uintptr_t v  = (hi << (sizeof(uintptr_t) * 8 - sizeof(uint32_t))) ^ lo;
    return (void *)v;
}

static uint64_t val64(uint32_t hi32, uint32_t lo32)
{
    const uint64_t hi = hi32;
    const uint64_t lo = lo32;
    return (hi << 32) + lo;
}

int main(void)
{
    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x87654321U);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        TEST(KOS_atomic_read_acquire_u32(value) == 0x87654321U);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        KOS_atomic_write_relaxed_u32(value, 0x12345678U);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x12345678U);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        KOS_atomic_write_release_u32(value, 0x12345678U);
        TEST(KOS_atomic_read_acquire_u32(value) == 0x12345678U);
    }

    {
        KOS_ATOMIC(void *) value;
        value = ptrv(0xFEDCBA98U, 0x76543210U);
        TEST(KOS_atomic_read_relaxed_ptr(value) == ptrv(0xFEDCBA98U, 0x76543210U));
    }

    {
        KOS_ATOMIC(void *) value;
        value = ptrv(0xFEDCBA98U, 0x76543210U);
        TEST(KOS_atomic_read_acquire_ptr(value) == ptrv(0xFEDCBA98U, 0x76543210U));
    }

    {
        KOS_ATOMIC(void *) value;
        value = ptrv(0xFEDCBA98U, 0x76543210U);
        KOS_atomic_write_relaxed_ptr(value, ptrv(0xF00DFACEU, 0xBEADC0DE));
        TEST(KOS_atomic_read_relaxed_ptr(value) == ptrv(0xF00DFACEU, 0xBEADC0DE));
    }

    {
        KOS_ATOMIC(void *) value;
        value = ptrv(0xFEDCBA98U, 0x76543210U);
        KOS_atomic_write_release_ptr(value, ptrv(0xF00DFACEU, 0xBEADC0DE));
        TEST(KOS_atomic_read_acquire_ptr(value) == ptrv(0xF00DFACEU, 0xBEADC0DE));
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        TEST( ! KOS_atomic_cas_strong_u32(value, 0xC0DE, 0xFEED));
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x87654321U);
        TEST(KOS_atomic_cas_strong_u32(value, 0x87654321U, 0xFEED));
        TEST(KOS_atomic_read_relaxed_u32(value) == 0xFEED);
    }

    {
        KOS_ATOMIC(void *) value;
        value = ptrv(0xFEDCBA98U, 0x76543210U);
        TEST( ! KOS_atomic_cas_strong_ptr(value, ptrv(1, 2), ptrv(3, 4)));
        TEST(KOS_atomic_read_relaxed_ptr(value) == ptrv(0xFEDCBA98U, 0x76543210U));
        TEST(KOS_atomic_cas_strong_ptr(value, ptrv(0xFEDCBA98U, 0x76543210U), ptrv(0xF00DFACEU, 0xBEADC0DE)));
        TEST(KOS_atomic_read_relaxed_ptr(value) == ptrv(0xF00DFACEU, 0xBEADC0DE));
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        TEST(KOS_atomic_add_u32(value, 2) == 0x87654321U);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x87654323U);
        TEST(KOS_atomic_add_u32(value, 0x120-2) == 0x87654323U);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x87654441U);
    }

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x17654321U;
        TEST(KOS_atomic_add_i32(value, 2) == 0x17654321);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x17654323U);
        TEST(KOS_atomic_add_i32(value, 0x120-2) == 0x17654323);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x17654441U);
    }

#ifndef KOS_NO_64BIT_ATOMICS
    {
        KOS_ATOMIC(uint64_t) value;
        value = val64(0xFEDCBA98U, 0x76543210U);
        TEST(KOS_atomic_read_relaxed_u64(value) == val64(0xFEDCBA98U, 0x76543210U));
        TEST(KOS_atomic_add_u64(value, 2) == val64(0xFEDCBA98U, 0x76543210U));
        TEST(KOS_atomic_read_relaxed_u64(value) == val64(0xFEDCBA98U, 0x76543212U));
        TEST(KOS_atomic_add_u64(value, 1) == val64(0xFEDCBA98U, 0x76543212U));
        TEST(KOS_atomic_read_relaxed_u64(value) == val64(0xFEDCBA98U, 0x76543213U));
    }
#endif

    {
        KOS_ATOMIC(uint32_t) value;
        value = 0x87654321U;
        TEST(KOS_atomic_swap_u32(value, 0x12345678U) == 0x87654321U);
        TEST(KOS_atomic_read_relaxed_u32(value) == 0x12345678U);
        TEST(KOS_atomic_swap_u32(value, 0x89ABCDEFU) == 0x12345678U);
        TEST(KOS_atomic_read_relaxed_u32(value) ==  0x89ABCDEFU);
    }

    /* Test if compare-and-swap is strong */
    {
        KOS_ATOMIC(uint32_t) value;
        int                  i;

        value = 0x87654321U;

        for (i = 0; i < 1024; i++) {
            uint32_t oldv = (i & 1) ? 0x89ABCDEFU : 0x87654321U;
            uint32_t newv = (i & 1) ? 0x87654321U : 0x89ABCDEFU;

            TEST(KOS_atomic_cas_strong_u32(value, oldv, newv));
            TEST(KOS_atomic_read_relaxed_u32(value) == newv);
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

            TEST(KOS_atomic_cas_strong_ptr(value, oldv, newv));
            TEST(KOS_atomic_read_relaxed_ptr(value) == newv);
        }
    }

    return 0;
}

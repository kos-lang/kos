/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

int main(void)
{
    KOS_INSTANCE      inst;
    KOS_CONTEXT       ctx;
    static const char cstr[] = "str";
    KOS_OBJ_ID        str;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    str = KOS_new_const_ascii_cstring(ctx, cstr);
    TEST(!IS_BAD_PTR(str));

    /************************************************************************/
    /* Cannot invoke buffer functions on non-buffer objects */
    {
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 1);

        TEST(KOS_buffer_reserve(ctx, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, str, 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, str, 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(ctx, TO_SMALL_INT(1), 1U) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(ctx, str, 1U) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(ctx, TO_SMALL_INT(1), 1, 2, 3U) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(ctx, str, 1, 2, 3U) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(ctx, TO_SMALL_INT(1), 0, buf, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(ctx, str, 0, buf, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(ctx, buf, 0, TO_SMALL_INT(1), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(ctx, buf, 0, str, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_slice(ctx, TO_SMALL_INT(1), 1, 2) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_buffer_slice(ctx, str, 1, 2) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Allocate non-zero buffer size */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 128);
        TEST(!IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 128);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 128; i++)
            data[0] = (uint8_t)i;
    }

    /************************************************************************/
    /* Reserve/resize */
    {
        uint8_t *data;

        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 0);
        TEST(!IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 0);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, buf, 100) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 100);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Reserve/resize */
    {
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 0) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(ctx, buf, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(ctx, buf, 128) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(ctx, buf, 64) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_resize(ctx, buf, 16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_resize(ctx, buf, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 5);
    }

    /************************************************************************/
    /* KOS_buffer_fill */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_fill(ctx, buf, -100, 100, 64) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, buf, 128) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 128);

        TEST(KOS_buffer_fill(ctx, buf, 0, -1, 0x55) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 127; i++)
            TEST(data[i] == 0x55);

        TEST(KOS_buffer_resize(ctx, buf, 90) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 90);

        TEST(KOS_buffer_resize(ctx, buf, 512) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 512);

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 90; i++)
            TEST(data[i] == 0x55);

        TEST(KOS_buffer_fill(ctx, buf, -500, 50, 0xAA) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 12; i++)
            TEST(data[i] == 0x55);

        for (i = 12; i < 50; i++)
            TEST(data[i] == 0xAA);

        for (i = 50; i < 90; i++)
            TEST(data[i] == 0x55);
    }

    /************************************************************************/
    /* KOS_buffer_make_room */
    {
        uint8_t *data;

        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        data = KOS_buffer_make_room(ctx, buf, 2);
        TEST(data);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 2);

        data[0] = 0x51;
        data[1] = 0x52;

        data = KOS_buffer_make_room(ctx, buf, 1);
        TEST(data);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 3);

        data[0] = 0x40;

        data = KOS_buffer_make_room(ctx, buf, 0xFFFFFFFDU);
        TEST(data == 0);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 3);

        data = KOS_buffer_data_volatile(ctx, buf);
        TEST(data);
        TEST_NO_EXCEPTION();
        TEST(data[0] == 0x51);
        TEST(data[1] == 0x52);
        TEST(data[2] == 0x40);
    }

    /************************************************************************/
    /* KOS_buffer_copy */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_ID buf1 = KOS_new_buffer(ctx, 10);
        KOS_OBJ_ID buf2;
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 10);

        buf2 = KOS_new_buffer(ctx, 5);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 5);

        TEST(KOS_buffer_fill(ctx, buf1, 0, 10, 1) == KOS_SUCCESS);
        TEST(KOS_buffer_fill(ctx, buf2, 0, 5, 2) == KOS_SUCCESS);

        TEST(KOS_buffer_copy(ctx, buf1, 2, buf2, -4, 4) == KOS_SUCCESS);

        data = KOS_buffer_data_volatile(ctx, buf1);
        TEST(data);
        TEST_NO_EXCEPTION();
        for (i = 0; i < 2; i++)
            TEST(data[i] == 1);
        for (i = 2; i < 5; i++)
            TEST(data[i] == 2);
        for (i = 5; i < 10; i++)
            TEST(data[i] == 1);

        TEST(KOS_buffer_copy(ctx, buf1, -2, buf2, -100, 100) == KOS_SUCCESS);

        data = KOS_buffer_data_volatile(ctx, buf1);
        TEST(data);
        TEST_NO_EXCEPTION();
        for (i = 0; i < 2; i++)
            TEST(data[i] == 1);
        for (i = 2; i < 5; i++)
            TEST(data[i] == 2);
        for (i = 5; i < 8; i++)
            TEST(data[i] == 1);
        for (i = 8; i < 10; i++)
            TEST(data[i] == 2);

        data = KOS_buffer_data_volatile(ctx, buf2);
        TEST(data);
        TEST_NO_EXCEPTION();
        for (i = 0; i < 5; i++)
            TEST(data[i] == 2);

        for (i = 0; i < 5; i++)
            data[i] = (uint8_t)i;

        TEST(KOS_buffer_copy(ctx, buf2, 0, buf2, -3, 100) == KOS_SUCCESS);

        TEST(data[0] == 2);
        TEST(data[1] == 3);
        TEST(data[2] == 4);
        TEST(data[3] == 3);
        TEST(data[4] == 4);

        for (i = 0; i < 5; i++)
            data[i] = (uint8_t)i;

        TEST(KOS_buffer_copy(ctx, buf2, -2, buf2, 0, 100) == KOS_SUCCESS);

        TEST(data[0] == 0);
        TEST(data[1] == 1);
        TEST(data[2] == 2);
        TEST(data[3] == 0);
        TEST(data[4] == 1);
    }

    /************************************************************************/
    /* KOS_buffer_slice */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_ID buf1 = KOS_new_buffer(ctx, 10);
        KOS_OBJ_ID buf2;
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 10);

        data = KOS_buffer_data_volatile(ctx, buf1);
        TEST(data);
        TEST_NO_EXCEPTION();
        for (i = 0; i < 10; i++)
            data[i] = (uint8_t)i;

        buf2 = KOS_buffer_slice(ctx, buf1, 5, -5);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 0);

        buf2 = KOS_buffer_slice(ctx, buf1, -4, 1000);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 4);

        data = KOS_buffer_data_volatile(ctx, buf2);
        TEST(data);
        TEST_NO_EXCEPTION();

        TEST(data[0] == 6);
        TEST(data[1] == 7);
        TEST(data[2] == 8);
        TEST(data[3] == 9);

        buf2 = KOS_buffer_slice(ctx, buf1, 5, -6);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 0);

        buf1 = KOS_buffer_slice(ctx, buf2, 5, -6);
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 0);
    }

    /************************************************************************/
    /* Read-only buffer of size 0 */
    {
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST(GET_OBJ_TYPE(buf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_lock_object(ctx, buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 1024) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_resize(ctx, buf, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, buf, 1024) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_make_room(ctx, buf, 0) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(ctx, buf, 16) == 0);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_data(ctx, buf) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data_volatile(ctx, buf) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(ctx, buf, 0, 1, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Read-only buffer of size 16 */
    {
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 16);
        TEST( ! IS_BAD_PTR(buf));
        TEST(GET_OBJ_TYPE(buf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        {
            int            i;
            uint8_t *const data = KOS_buffer_data_volatile(ctx, buf);

            TEST(data);
            TEST_NO_EXCEPTION();
            for (i = 0; i < 16; i++)
                data[i] = (uint8_t)i;
        }

        TEST(KOS_lock_object(ctx, buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_reserve(ctx, buf, 16) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(ctx, buf, 1024) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_resize(ctx, buf, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_resize(ctx, buf, 16) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(ctx, buf, 1024) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_make_room(ctx, buf, 0) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(ctx, buf, 16) == 0);
        TEST_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_data(ctx, buf) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data_volatile(ctx, buf) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(ctx, buf, 0, 16, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        {
            int                  i;
            const uint8_t *const data = KOS_buffer_data_const(buf);
            TEST(data);
            for (i = 0; i < 16; i++)
                TEST(data[i] == (uint8_t)i);
        }
    }

    /************************************************************************/
    /* Copy a read-only buffer */
    {
        KOS_OBJ_ID newbuf;
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 16);
        TEST( ! IS_BAD_PTR(buf));
        TEST(GET_OBJ_TYPE(buf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        {
            int            i;
            uint8_t *const data = KOS_buffer_data_volatile(ctx, buf);
            TEST(data);
            TEST_NO_EXCEPTION();
            for (i = 0; i < 16; i++)
                data[i] = (uint8_t)(i + 50);
        }

        TEST(KOS_lock_object(ctx, buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        newbuf = KOS_new_buffer(ctx, 8);
        TEST( ! IS_BAD_PTR(newbuf));
        TEST(GET_OBJ_TYPE(newbuf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(newbuf) == 8);

        TEST(KOS_buffer_fill(ctx, newbuf, 0, 8, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_copy(ctx, newbuf, 1, buf, 2, 8) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        {
            const uint8_t *const data = KOS_buffer_data_const(newbuf);

            TEST(data);
            TEST(KOS_get_buffer_size(newbuf) == 8);
            TEST(data[0] == 1);
            TEST(data[1] == 52);
            TEST(data[2] == 53);
            TEST(data[3] == 54);
            TEST(data[4] == 55);
            TEST(data[5] == 56);
            TEST(data[6] == 57);
            TEST(data[7] == 1);
        }

        {
            int                  i;
            const uint8_t *const data = KOS_buffer_data_const(buf);

            TEST(data);
            TEST(KOS_get_buffer_size(buf) == 16);
            for (i = 0; i < 16; i++)
                TEST(data[i] == (uint8_t)(i + 50));
        }
    }

    /************************************************************************/
    /* Slice a read-only buffer */
    {
        KOS_OBJ_ID newbuf;
        KOS_OBJ_ID buf = KOS_new_buffer(ctx, 16);
        TEST( ! IS_BAD_PTR(buf));
        TEST(GET_OBJ_TYPE(buf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        {
            int            i;
            uint8_t *const data = KOS_buffer_data_volatile(ctx, buf);
            TEST(data);
            TEST_NO_EXCEPTION();
            for (i = 0; i < 16; i++)
                data[i] = (uint8_t)(i + 50);
        }

        TEST(KOS_lock_object(ctx, buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        newbuf = KOS_buffer_slice(ctx, buf, 1, 9);
        TEST( ! IS_BAD_PTR(newbuf));
        TEST(GET_OBJ_TYPE(newbuf) == OBJ_BUFFER);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(newbuf) == 8);

        {
            int                  i;
            const uint8_t *const data = KOS_buffer_data_const(newbuf);

            TEST(data);
            for (i = 0; i < 8; i++)
                TEST(data[i] == (uint8_t)(i + 51));
        }

        {
            uint8_t *data = KOS_buffer_data_volatile(ctx, newbuf);

            TEST(data);
            TEST_NO_EXCEPTION();

            data[1] = 8;
            data[3] = 9;

            TEST(KOS_compare(buf, newbuf));
            TEST_NO_EXCEPTION();

            TEST(KOS_buffer_fill(ctx, newbuf, 0, 8, 5) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        {
            int                  i;
            const uint8_t *const data = KOS_buffer_data_const(buf);

            TEST(data);
            TEST(KOS_get_buffer_size(buf) == 16);
            for (i = 0; i < 16; i++)
                TEST(data[i] == (uint8_t)(i + 50));
        }
    }

    KOS_instance_destroy(&inst);

    return 0;
}

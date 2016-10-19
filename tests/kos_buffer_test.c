/*
 * Copyright (c) 2014-2016 Chris Dragan
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
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* Cannot invoke buffer functions on non-buffer objects */
    {
        KOS_ASCII_STRING(str, "str");
        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 1);

        TEST(KOS_buffer_reserve(frame, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(frame, TO_OBJPTR(&str), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(frame, TO_OBJPTR(0), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(frame, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(frame, TO_OBJPTR(&str), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(frame, TO_OBJPTR(0), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data(frame, TO_SMALL_INT(1)) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data(frame, TO_OBJPTR(&str)) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data(frame, TO_OBJPTR(0)) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(frame, TO_SMALL_INT(1), 1U) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(frame, TO_OBJPTR(&str), 1U) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_make_room(frame, TO_OBJPTR(0), 1U) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(frame, TO_SMALL_INT(1), 1, 2, 3U) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(frame, TO_OBJPTR(&str), 1, 2, 3U) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_fill(frame, TO_OBJPTR(0), 1, 2, 3U) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, TO_SMALL_INT(1), 0, buf, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, TO_OBJPTR(&str), 0, buf, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, TO_OBJPTR(0), 0, buf, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, buf, 0, TO_SMALL_INT(1), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, buf, 0, TO_OBJPTR(&str), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_copy(frame, buf, 0, TO_OBJPTR(0), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_slice(frame, TO_SMALL_INT(1), 1, 2) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_buffer_slice(frame, TO_OBJPTR(&str), 1, 2) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_buffer_slice(frame, TO_OBJPTR(0), 1, 2) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Allocate non-zero buffer size */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 128);
        TEST(!IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 128);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);

        for (i = 0; i < 128; i++)
            data[0] = (uint8_t)i;
    }

    /************************************************************************/
    /* Reserve/resize */
    {
        uint8_t *data;

        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 0);
        TEST(!IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 0);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(frame, buf, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 0);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_resize(frame, buf, 100) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_buffer_size(buf) == 100);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);
        TEST(data != 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Reserve/resize */
    {
        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_reserve(frame, buf, 0) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(frame, buf, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(frame, buf, 128) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_reserve(frame, buf, 64) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 0);

        TEST(KOS_buffer_resize(frame, buf, 16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 16);

        TEST(KOS_buffer_resize(frame, buf, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 5);
    }

    /************************************************************************/
    /* KOS_buffer_fill */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_fill(frame, buf, -100, 100, 64) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_buffer_resize(frame, buf, 128) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 128);

        TEST(KOS_buffer_fill(frame, buf, 0, -1, 0x55) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);
        TEST(data);

        for (i = 0; i < 127; i++)
            TEST(data[i] == 0x55);
        TEST(data[127] == 0);

        TEST(KOS_buffer_resize(frame, buf, 90) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 90);

        TEST(KOS_buffer_resize(frame, buf, 512) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 512);

        data = KOS_buffer_data(frame, buf);
        TEST(data);

        for (i = 0; i < 90; i++)
            TEST(data[i] == 0x55);

        TEST(KOS_buffer_fill(frame, buf, -500, 50, 0xAA) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        data = KOS_buffer_data(frame, buf);
        TEST(data);

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

        KOS_OBJ_PTR buf = KOS_new_buffer(frame, 0);
        TEST( ! IS_BAD_PTR(buf));
        TEST_NO_EXCEPTION();

        data = KOS_buffer_make_room(frame, buf, 2);
        TEST(data);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 2);

        data[0] = 0x51;
        data[1] = 0x52;

        data = KOS_buffer_make_room(frame, buf, 1);
        TEST(data);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf) == 3);

        data[0] = 0x40;

        data = KOS_buffer_data(frame, buf);
        TEST(data[0] == 0x51);
        TEST(data[1] == 0x52);
        TEST(data[2] == 0x40);
    }

    /************************************************************************/
    /* KOS_buffer_copy */
    {
        uint8_t *data;
        int      i;

        KOS_OBJ_PTR buf1 = KOS_new_buffer(frame, 10);
        KOS_OBJ_PTR buf2;
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 10);

        buf2 = KOS_new_buffer(frame, 5);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 5);

        TEST(KOS_buffer_fill(frame, buf1, 0, 10, 1) == KOS_SUCCESS);
        TEST(KOS_buffer_fill(frame, buf2, 0, 5, 2) == KOS_SUCCESS);

        TEST(KOS_buffer_copy(frame, buf1, 2, buf2, -4, 4) == KOS_SUCCESS);

        data = KOS_buffer_data(frame, buf1);
        for (i = 0; i < 2; i++)
            TEST(data[i] == 1);
        for (i = 2; i < 5; i++)
            TEST(data[i] == 2);
        for (i = 5; i < 10; i++)
            TEST(data[i] == 1);

        TEST(KOS_buffer_copy(frame, buf1, -2, buf2, -100, 100) == KOS_SUCCESS);

        data = KOS_buffer_data(frame, buf1);
        for (i = 0; i < 2; i++)
            TEST(data[i] == 1);
        for (i = 2; i < 5; i++)
            TEST(data[i] == 2);
        for (i = 5; i < 8; i++)
            TEST(data[i] == 1);
        for (i = 8; i < 10; i++)
            TEST(data[i] == 2);

        data = KOS_buffer_data(frame, buf2);
        for (i = 0; i < 5; i++)
            TEST(data[i] == 2);

        for (i = 0; i < 5; i++)
            data[i] = (uint8_t)i;

        TEST(KOS_buffer_copy(frame, buf2, 0, buf2, -3, 100) == KOS_SUCCESS);

        TEST(data[0] == 2);
        TEST(data[1] == 3);
        TEST(data[2] == 4);
        TEST(data[3] == 3);
        TEST(data[4] == 4);

        for (i = 0; i < 5; i++)
            data[i] = (uint8_t)i;

        TEST(KOS_buffer_copy(frame, buf2, -2, buf2, 0, 100) == KOS_SUCCESS);

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

        KOS_OBJ_PTR buf1 = KOS_new_buffer(frame, 10);
        KOS_OBJ_PTR buf2;
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 10);

        data = KOS_buffer_data(frame, buf1);
        for (i = 0; i < 10; i++)
            data[i] = (uint8_t)i;

        buf2 = KOS_buffer_slice(frame, buf1, 5, -5);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 0);

        buf2 = KOS_buffer_slice(frame, buf1, -4, 1000);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 4);

        data = KOS_buffer_data(frame, buf2);

        TEST(data[0] == 6);
        TEST(data[1] == 7);
        TEST(data[2] == 8);
        TEST(data[3] == 9);

        buf2 = KOS_buffer_slice(frame, buf1, 5, -6);
        TEST( ! IS_BAD_PTR(buf2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf2) == 0);

        buf1 = KOS_buffer_slice(frame, buf2, 5, -6);
        TEST( ! IS_BAD_PTR(buf1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_buffer_size(buf1) == 0);
    }

    /************************************************************************/
    /* KOS_buffer_rotate */
    {
        /* TODO */
    }

    KOS_context_destroy(&ctx);

    return 0;
}

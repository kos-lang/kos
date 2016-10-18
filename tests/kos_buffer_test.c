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

        TEST(KOS_buffer_reserve(frame, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_reserve(frame, TO_OBJPTR(&str), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(frame, TO_SMALL_INT(1), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_resize(frame, TO_OBJPTR(&str), 10) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data(frame, TO_SMALL_INT(1)) == 0);
        TEST_EXCEPTION();

        TEST(KOS_buffer_data(frame, TO_OBJPTR(&str)) == 0);
        TEST_EXCEPTION();
    }

    /************************************************************************/
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

    KOS_context_destroy(&ctx);

    return 0;
}

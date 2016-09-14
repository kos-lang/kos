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

#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(&ctx)); KOS_clear_exception(&ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(&ctx))

int main(void)
{
    KOS_CONTEXT ctx;

    TEST(KOS_context_init(&ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* Cannot read from a non-array */
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_array_read(&ctx, TO_OBJPTR(0), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, TO_SMALL_INT(1), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, TO_OBJPTR(&str), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, KOS_TRUE, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, KOS_VOID, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, KOS_new_object(&ctx), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot write to a non-array */
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_array_write(&ctx, TO_OBJPTR(0), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, TO_SMALL_INT(1), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, TO_OBJPTR(&str), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, KOS_FALSE, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, KOS_VOID, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, KOS_new_object(&ctx), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot reserve on a non-array */
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_array_reserve(&ctx, TO_OBJPTR(0), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(&ctx, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(&ctx, TO_OBJPTR(&str), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(&ctx, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(&ctx, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(&ctx, KOS_new_object(&ctx), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot resize a non-array */
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_array_resize(&ctx, TO_OBJPTR(0), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, TO_OBJPTR(&str), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, KOS_new_object(&ctx), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Allocate empty array */
    {
        KOS_OBJ_PTR a = KOS_new_array(&ctx, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);

        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_array_read(&ctx, a, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, a, 0, TO_SMALL_INT(5)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array to 1 element and read it */
    {
        KOS_OBJ_PTR a = KOS_new_array(&ctx, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(&ctx, a, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 1);

        TEST(KOS_array_read(&ctx, a, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, -1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, -2) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array to 1 element and write to it */
    {
        KOS_OBJ_PTR a = KOS_new_array(&ctx, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(&ctx, a, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 1);

        TEST(KOS_array_read(&ctx, a, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(&ctx, a, 0, TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, 0) == TO_SMALL_INT(5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_PTR a = KOS_new_array(&ctx, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(&ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, 5) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, 3) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 5; i++) {
            TEST(KOS_array_read(&ctx, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(&ctx, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_PTR a = KOS_new_array(&ctx, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(&ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, 5) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, 3) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_resize(&ctx, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 10);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 10; i++) {
            TEST(KOS_array_read(&ctx, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(&ctx, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Multiple array operations */
    {
        int       i;
        int const num_items = 5;

        /* Allocate array of a particular size */
        KOS_OBJ_PTR a = KOS_new_array(&ctx, num_items);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        /* Set all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_write(&ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        /* Check all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(&ctx, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        /* Check boundary accesses */

        TEST(KOS_array_read(&ctx, a, num_items) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, -(int)num_items-1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, a, num_items, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(&ctx, a, -(int)num_items-1, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Check that reserve of a smaller capacity does not affect the array */

        TEST(KOS_array_reserve(&ctx, a, 2) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        TEST(KOS_array_read(&ctx, a, num_items-1) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Check that reserve of a larger capacity does not affect the array */

        TEST(KOS_array_reserve(&ctx, a, num_items*10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, num_items) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        /* Resize array to 10 times its size */

        TEST(KOS_array_resize(&ctx, a, num_items*10) == KOS_SUCCESS);

        TEST(KOS_get_array_size(a) == num_items*10U);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(&ctx, a, num_items) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, num_items*10-1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, -(int)num_items*10) == TO_SMALL_INT(0));
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, num_items*10) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_array_read(&ctx, a, -(int)num_items*10-1) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Iteratively add elements to the end of an array */
    {
        int       i;
        int const num_items = 1024;

        KOS_OBJ_PTR a = KOS_new_array(&ctx, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 0);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_resize(&ctx, a, (unsigned)(i+1)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_get_array_size(a) == (unsigned)(i+1));

            TEST(KOS_array_write(&ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(&ctx, a, i+1) == TO_OBJPTR(0));
            TEST_EXCEPTION();
        }

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(&ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(&ctx, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }
    }

    /************************************************************************/
    /* Slice */
    {
        KOS_OBJ_PTR a1 = KOS_new_array(&ctx, 10);
        int         i;

        for (i = 0; i < 10; i++) {
            TEST(KOS_array_write(&ctx, a1, i, TO_SMALL_INT(i*10)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        {
            KOS_OBJ_PTR a2 = KOS_array_slice(&ctx, a1, 0, 10);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(&ctx, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_PTR a2 = KOS_array_slice(&ctx, a1, 2, 8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(&ctx, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_PTR a2 = KOS_array_slice(&ctx, a1, -8, -2);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(&ctx, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_PTR a2 = KOS_array_slice(&ctx, a1, -2, -8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 0);
        }

        {
            KOS_OBJ_PTR a2 = KOS_array_slice(&ctx, a1, -20, 20);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(&ctx, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }
    }

    KOS_context_destroy(&ctx);

    return 0;
}

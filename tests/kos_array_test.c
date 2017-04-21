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

#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

int main(void)
{
    KOS_CONTEXT       ctx;
    KOS_STACK_FRAME  *frame;
    static const char cstr[] = "str";
    KOS_OBJ_ID        str;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    str = KOS_context_get_cstring(frame, cstr);

    /************************************************************************/
    /* Cannot read from a non-array */
    {
        TEST(KOS_array_read(frame, KOS_BADPTR, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, TO_SMALL_INT(1), 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, str, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, KOS_TRUE, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, KOS_VOID, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, KOS_new_object(frame), 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot write to a non-array */
    {
        TEST(KOS_array_write(frame, KOS_BADPTR, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, TO_SMALL_INT(1), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, str, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, KOS_FALSE, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, KOS_VOID, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, KOS_new_object(frame), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot reserve on a non-array */
    {
        TEST(KOS_array_reserve(frame, KOS_BADPTR, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(frame, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(frame, str, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(frame, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(frame, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(frame, KOS_new_object(frame), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot resize a non-array */
    {
        TEST(KOS_array_resize(frame, KOS_BADPTR, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, str, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, KOS_new_object(frame), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot slice a non-array */
    {
        TEST(KOS_array_slice(frame, KOS_BADPTR, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(frame, TO_SMALL_INT(1), 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(frame, str, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(frame, KOS_TRUE, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(frame, KOS_VOID, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(frame, KOS_new_object(frame), 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot insert to a non-array */
    {
        KOS_OBJ_ID array = KOS_new_array(frame, 1);

        TEST(KOS_array_insert(frame, KOS_BADPTR, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, TO_SMALL_INT(1), 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, str, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, KOS_TRUE, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, KOS_VOID, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, KOS_new_object(frame), 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, KOS_BADPTR, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, TO_SMALL_INT(1), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, str, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, KOS_TRUE, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, KOS_VOID, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(frame, array, 0, 0, KOS_new_object(frame), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot push to a non-array */
    {
        uint32_t idx = ~0U;
        TEST(KOS_array_push(frame, KOS_BADPTR, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(frame, TO_SMALL_INT(1), TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(frame, str, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(frame, KOS_TRUE, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(frame, KOS_VOID, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(frame, KOS_new_object(frame), TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);
    }

    /************************************************************************/
    /* Cannot pop from an non-array */
    {
        TEST(KOS_array_pop(frame, KOS_BADPTR) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(frame, TO_SMALL_INT(1)) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(frame, str) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(frame, KOS_TRUE) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(frame, KOS_VOID) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(frame, KOS_new_object(frame)) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Allocate empty array */
    {
        KOS_OBJ_ID a = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);

        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_array_read(frame, a, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, a, 0, TO_SMALL_INT(5)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array 100 times by 1 element and read it each time */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));

        for (i = 1; i < 101; i++) {
            TEST(KOS_array_resize(frame, a, (uint32_t)i) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_get_array_size(a) == (uint32_t)i);

            TEST(KOS_array_read(frame, a, 0) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, i-1) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, -1) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, -i) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, i) == KOS_BADPTR);
            TEST_EXCEPTION();

            TEST(KOS_array_read(frame, a, -i-1) == KOS_BADPTR);
            TEST_EXCEPTION();
        }
    }

    /************************************************************************/
    /* Resize array to 1 element and write to it */
    {
        KOS_OBJ_ID a = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(frame, a, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 1);

        TEST(KOS_array_read(frame, a, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(frame, a, 0, TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, 0) == TO_SMALL_INT(5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array to 100 elements and read them */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(frame, a, 100) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 100);

        for (i = 0; i < 100; i++) {
            TEST(KOS_array_read(frame, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, -1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, -100) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, 100) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, a, -101) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(frame, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(frame, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, 5) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, 3) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 5; i++) {
            TEST(KOS_array_read(frame, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(frame, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(frame, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(frame, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, 5) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, 3) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(frame, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 10);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 10; i++) {
            TEST(KOS_array_read(frame, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(frame, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Multiple array operations */
    {
        int       i;
        int const num_items = 5;

        /* Allocate array of a particular size */
        KOS_OBJ_ID a = KOS_new_array(frame, num_items);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        /* Set all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_write(frame, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        /* Check all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        /* Check boundary accesses */

        TEST(KOS_array_read(frame, a, num_items) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, a, -(int)num_items-1) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, a, num_items, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(frame, a, -(int)num_items-1, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Check that reserve of a smaller capacity does not affect the array */

        TEST(KOS_array_reserve(frame, a, 2) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        TEST(KOS_array_read(frame, a, num_items-1) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Check that reserve of a larger capacity does not affect the array */

        TEST(KOS_array_reserve(frame, a, num_items*10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, num_items) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Resize array to 10 times its size */

        TEST(KOS_array_resize(frame, a, num_items*10) == KOS_SUCCESS);

        TEST(KOS_get_array_size(a) == num_items*10U);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(frame, a, num_items) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, num_items*10-1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, -(int)num_items*10) == TO_SMALL_INT(0));
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(frame, a, num_items*10) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(frame, a, -(int)num_items*10-1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Iteratively add elements to the end of an array */
    {
        int       i;
        int const num_items = 1024;

        KOS_OBJ_ID a = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 0);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_resize(frame, a, (unsigned)(i+1)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_get_array_size(a) == (unsigned)(i+1));

            TEST(KOS_array_write(frame, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, i+1) == KOS_BADPTR);
            TEST_EXCEPTION();
        }

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(frame, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(frame, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }
    }

    /************************************************************************/
    /* Slice */
    {
        KOS_OBJ_ID a1 = KOS_new_array(frame, 10);
        int        i;

        for (i = 0; i < 10; i++) {
            TEST(KOS_array_write(frame, a1, i, TO_SMALL_INT(i*10)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(frame, a1, 0, 10);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(frame, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(frame, a1, 2, 8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(frame, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(frame, a1, -8, -2);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(frame, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(frame, a1, -2, -8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 0);
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(frame, a1, -20, 20);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(frame, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }
    }
    {
        KOS_OBJ_ID a;
        KOS_OBJ_ID empty = KOS_new_array(frame, 0);
        TEST( ! IS_BAD_PTR(empty));
        TEST_NO_EXCEPTION();

        a = KOS_array_slice(frame, empty, 10, 20);
        TEST_NO_EXCEPTION();
        TEST( ! IS_BAD_PTR(a));
        TEST( ! IS_SMALL_INT(a));
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);
        TEST(KOS_get_array_size(a) == 0);
    }

    /************************************************************************/
    /* Insert */
    {
        int        i;
        KOS_OBJ_ID dst;
        KOS_OBJ_ID src;

        src = KOS_new_array(frame, 10);
        TEST( ! IS_BAD_PTR(src));
        TEST(GET_OBJ_TYPE(src) == OBJ_ARRAY);
        TEST(KOS_get_array_size(src) == 10);

        for (i = 0; i < 10; i++)
            TEST(KOS_array_write(frame, src, i, TO_SMALL_INT(i)) == KOS_SUCCESS);

        dst = KOS_new_array(frame, 0);
        TEST( ! IS_BAD_PTR(dst));
        TEST(GET_OBJ_TYPE(dst) == OBJ_ARRAY);
        TEST(KOS_get_array_size(dst) == 0);

        TEST(KOS_array_insert(frame, dst, 0, 0, src, -9, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 2);
        for (i = 0; i < 2; i++)
            TEST(KOS_array_read(frame, dst, i) == TO_SMALL_INT(i+1));

        TEST(KOS_array_insert(frame, dst, 1, 0, src, 1, 0) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 2);
        TEST(KOS_array_read(frame, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(frame, dst, 1) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(frame, dst, 1, 1, src, 9, 10) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 3);
        TEST(KOS_array_read(frame, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(frame, dst, 1) == TO_SMALL_INT(9));
        TEST(KOS_array_read(frame, dst, 2) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(frame, dst, 1, 1, dst, 2, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 4);
        TEST(KOS_array_read(frame, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(frame, dst, 1) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, dst, 2) == TO_SMALL_INT(9));
        TEST(KOS_array_read(frame, dst, 3) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(frame, src, 3, 8, src, 5, 7) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 7);
        TEST(KOS_array_read(frame, src, 0) == TO_SMALL_INT(0));
        TEST(KOS_array_read(frame, src, 1) == TO_SMALL_INT(1));
        TEST(KOS_array_read(frame, src, 2) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 3) == TO_SMALL_INT(5));
        TEST(KOS_array_read(frame, src, 4) == TO_SMALL_INT(6));
        TEST(KOS_array_read(frame, src, 5) == TO_SMALL_INT(8));
        TEST(KOS_array_read(frame, src, 6) == TO_SMALL_INT(9));

        TEST(KOS_array_insert(frame, src, 0, 100, src, 2, 5) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 3);
        TEST(KOS_array_read(frame, src, 0) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 1) == TO_SMALL_INT(5));
        TEST(KOS_array_read(frame, src, 2) == TO_SMALL_INT(6));

        TEST(KOS_array_insert(frame, src, 2, 4, src, 0, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 5);
        TEST(KOS_array_read(frame, src, 0) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 1) == TO_SMALL_INT(5));
        TEST(KOS_array_read(frame, src, 2) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 3) == TO_SMALL_INT(5));
        TEST(KOS_array_read(frame, src, 4) == TO_SMALL_INT(6));

        for (i = 0; i < 5; i++)
            TEST(KOS_array_write(frame, src, i, TO_SMALL_INT(i)) == KOS_SUCCESS);

        TEST(KOS_array_insert(frame, src, 0, 2, src, 1, 4) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 6);
        TEST(KOS_array_read(frame, src, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(frame, src, 1) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 2) == TO_SMALL_INT(3));
        TEST(KOS_array_read(frame, src, 3) == TO_SMALL_INT(2));
        TEST(KOS_array_read(frame, src, 4) == TO_SMALL_INT(3));
        TEST(KOS_array_read(frame, src, 5) == TO_SMALL_INT(4));
    }

    /************************************************************************/
    /* TODO KOS_array_rotate */

    /************************************************************************/
    /* Push/pop */
    {
        uint32_t   idx = ~0U;
        KOS_OBJ_ID v;
        KOS_OBJ_ID a   = KOS_new_array(frame, 0);
        TEST(!IS_BAD_PTR(a));
        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_array_push(frame, a, TO_SMALL_INT(123), &idx) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(idx == 0);
        TEST(KOS_get_array_size(a) == 1);

        v = KOS_array_pop(frame, a);
        TEST(v == TO_SMALL_INT(123));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_array_size(a) == 0);

        v = KOS_array_pop(frame, a);
        TEST(IS_BAD_PTR(v));
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 0);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

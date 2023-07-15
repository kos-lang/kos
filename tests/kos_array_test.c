/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "kos_test_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
    /* Cannot read from a non-array */
    {
        TEST(KOS_array_read(ctx, TO_SMALL_INT(1), 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, str, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, KOS_TRUE, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, KOS_VOID, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, KOS_new_object(ctx), 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot write to a non-array */
    {
        TEST(KOS_array_write(ctx, TO_SMALL_INT(1), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, str, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, KOS_FALSE, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, KOS_VOID, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, KOS_new_object(ctx), 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot reserve on a non-array */
    {
        TEST(KOS_array_reserve(ctx, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, str, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, KOS_new_object(ctx), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot resize a non-array */
    {
        TEST(KOS_array_resize(ctx, TO_SMALL_INT(1), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, str, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, KOS_TRUE, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, KOS_VOID, 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, KOS_new_object(ctx), 128) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot slice a non-array */
    {
        TEST(KOS_array_slice(ctx, TO_SMALL_INT(1), 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(ctx, str, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(ctx, KOS_TRUE, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(ctx, KOS_VOID, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_slice(ctx, KOS_new_object(ctx), 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot insert to a non-array */
    {
        KOS_OBJ_ID array = KOS_new_array(ctx, 1);

        TEST(KOS_array_insert(ctx, TO_SMALL_INT(1), 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, str, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, KOS_TRUE, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, KOS_VOID, 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, KOS_new_object(ctx), 0, 0, array, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, array, 0, 0, TO_SMALL_INT(1), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, array, 0, 0, str, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, array, 0, 0, KOS_TRUE, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, array, 0, 0, KOS_VOID, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, array, 0, 0, KOS_new_object(ctx), 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Cannot push to a non-array */
    {
        uint32_t idx = ~0U;

        TEST(KOS_array_push(ctx, TO_SMALL_INT(1), TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(ctx, str, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(ctx, KOS_TRUE, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(ctx, KOS_VOID, TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);

        TEST(KOS_array_push(ctx, KOS_new_object(ctx), TO_SMALL_INT(42), &idx) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(idx == ~0U);
    }

    /************************************************************************/
    /* Cannot pop from an non-array */
    {
        TEST(KOS_array_pop(ctx, TO_SMALL_INT(1)) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(ctx, str) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(ctx, KOS_TRUE) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(ctx, KOS_VOID) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_pop(ctx, KOS_new_object(ctx)) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Allocate empty array */
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);

        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_array_read(ctx, a, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, TO_SMALL_INT(5)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Attempt to allocate array which is too large */
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 0x1FFFFFFEU);
        TEST(IS_BAD_PTR(a));
        TEST_EXCEPTION();
    }
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 0x7FFFFFFFU);
        TEST(IS_BAD_PTR(a));
        TEST_EXCEPTION();
    }
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 0xFFFFFFFFU);
        TEST(IS_BAD_PTR(a));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array 100 times by 1 element and read it each time */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));

        for (i = 1; i < 101; i++) {
            TEST(KOS_array_resize(ctx, a, (uint32_t)i) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_get_array_size(a) == (uint32_t)i);

            TEST(KOS_array_read(ctx, a, 0) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, i-1) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, -1) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, -i) == KOS_VOID);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, i) == KOS_BADPTR);
            TEST_EXCEPTION();

            TEST(KOS_array_read(ctx, a, -i-1) == KOS_BADPTR);
            TEST_EXCEPTION();
        }
    }

    /************************************************************************/
    /* Resize array to 1 element and write to it */
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(ctx, a, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 1);

        TEST(KOS_array_read(ctx, a, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array to 100 elements and read them */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_array_resize(ctx, a, 100) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 100);

        for (i = 0; i < 100; i++) {
            TEST(KOS_array_read(ctx, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, -1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, -100) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, 100) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, a, -101) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(ctx, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, 5) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, 3) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 5; i++) {
            TEST(KOS_array_read(ctx, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(ctx, a, 5) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Resize array down and up */
    {
        int i;

        KOS_OBJ_ID a = KOS_new_array(ctx, 5);
        TEST(!IS_BAD_PTR(a));

        TEST(KOS_get_array_size(a) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, 5) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 3) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, 3) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 10);

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        for (i = 3; i < 10; i++) {
            TEST(KOS_array_read(ctx, a, i) == KOS_VOID);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_resize(ctx, a, 10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Multiple array operations */
    {
        int       i;
        int const num_items = 5;

        /* Allocate array of a particular size */
        KOS_OBJ_ID a = KOS_new_array(ctx, num_items);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        /* Set all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        /* Check all array elements */
        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        /* Check boundary accesses */

        TEST(KOS_array_read(ctx, a, num_items) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, a, -(int)num_items-1) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, num_items, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, -(int)num_items-1, TO_SMALL_INT(100)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Check that reserve of a smaller capacity does not affect the array */

        TEST(KOS_array_reserve(ctx, a, 2) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        TEST(KOS_array_read(ctx, a, num_items-1) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Check that reserve of a larger capacity does not affect the array */

        TEST(KOS_array_reserve(ctx, a, num_items*10) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == (unsigned)num_items);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, num_items) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Resize array to 10 times its size */

        TEST(KOS_array_resize(ctx, a, num_items*10) == KOS_SUCCESS);

        TEST(KOS_get_array_size(a) == num_items*10U);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_read(ctx, a, num_items) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, num_items*10-1) == KOS_VOID);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, -(int)num_items*10) == TO_SMALL_INT(0));
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, num_items*10) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_read(ctx, a, -(int)num_items*10-1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Iteratively add elements to the end of an array */
    {
        int       i;
        int const num_items = 1024;

        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 0);

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_resize(ctx, a, (unsigned)(i+1)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_get_array_size(a) == (unsigned)(i+1));

            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, i+1) == KOS_BADPTR);
            TEST_EXCEPTION();
        }

        for (i = 0; i < num_items; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_read(ctx, a, i-num_items) == TO_SMALL_INT(i));
            TEST_NO_EXCEPTION();
        }
    }

    /************************************************************************/
    /* Slice */
    {
        KOS_OBJ_ID a1 = KOS_new_array(ctx, 10);
        int        i;

        for (i = 0; i < 10; i++) {
            TEST(KOS_array_write(ctx, a1, i, TO_SMALL_INT(i*10)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(ctx, a1, 0, 10);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(ctx, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(ctx, a1, 2, 8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(ctx, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(ctx, a1, -8, -2);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 6);

            for (i = 0; i < 6; i++) {
                TEST(KOS_array_read(ctx, a2, i) == TO_SMALL_INT((i+2)*10));
                TEST_NO_EXCEPTION();
            }
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(ctx, a1, -2, -8);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 0);
        }

        {
            KOS_OBJ_ID a2 = KOS_array_slice(ctx, a1, -20, 20);
            TEST_NO_EXCEPTION();
            TEST(!IS_BAD_PTR(a2));

            TEST(KOS_get_array_size(a2) == 10);

            for (i = 0; i < 10; i++) {
                TEST(KOS_array_read(ctx, a2, i) == TO_SMALL_INT(i*10));
                TEST_NO_EXCEPTION();
            }
        }
    }
    {
        KOS_OBJ_ID a;
        KOS_OBJ_ID empty = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(empty));
        TEST_NO_EXCEPTION();

        a = KOS_array_slice(ctx, empty, 10, 20);
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

        src = KOS_new_array(ctx, 10);
        TEST( ! IS_BAD_PTR(src));
        TEST(GET_OBJ_TYPE(src) == OBJ_ARRAY);
        TEST(KOS_get_array_size(src) == 10);

        for (i = 0; i < 10; i++)
            TEST(KOS_array_write(ctx, src, i, TO_SMALL_INT(i)) == KOS_SUCCESS);

        dst = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(dst));
        TEST(GET_OBJ_TYPE(dst) == OBJ_ARRAY);
        TEST(KOS_get_array_size(dst) == 0);

        TEST(KOS_array_insert(ctx, dst, 0, 0, src, -9, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 2);
        for (i = 0; i < 2; i++)
            TEST(KOS_array_read(ctx, dst, i) == TO_SMALL_INT(i+1));

        TEST(KOS_array_insert(ctx, dst, 1, 0, src, 1, 0) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 2);
        TEST(KOS_array_read(ctx, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(ctx, dst, 1) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(ctx, dst, 1, 1, src, 9, 10) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 3);
        TEST(KOS_array_read(ctx, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(ctx, dst, 1) == TO_SMALL_INT(9));
        TEST(KOS_array_read(ctx, dst, 2) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(ctx, dst, 1, 1, dst, 2, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(dst) == 4);
        TEST(KOS_array_read(ctx, dst, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(ctx, dst, 1) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, dst, 2) == TO_SMALL_INT(9));
        TEST(KOS_array_read(ctx, dst, 3) == TO_SMALL_INT(2));

        TEST(KOS_array_insert(ctx, src, 3, 8, src, 5, 7) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 7);
        TEST(KOS_array_read(ctx, src, 0) == TO_SMALL_INT(0));
        TEST(KOS_array_read(ctx, src, 1) == TO_SMALL_INT(1));
        TEST(KOS_array_read(ctx, src, 2) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 3) == TO_SMALL_INT(5));
        TEST(KOS_array_read(ctx, src, 4) == TO_SMALL_INT(6));
        TEST(KOS_array_read(ctx, src, 5) == TO_SMALL_INT(8));
        TEST(KOS_array_read(ctx, src, 6) == TO_SMALL_INT(9));

        TEST(KOS_array_insert(ctx, src, 0, 100, src, 2, 5) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 3);
        TEST(KOS_array_read(ctx, src, 0) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 1) == TO_SMALL_INT(5));
        TEST(KOS_array_read(ctx, src, 2) == TO_SMALL_INT(6));

        TEST(KOS_array_insert(ctx, src, 2, 4, src, 0, 3) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 5);
        TEST(KOS_array_read(ctx, src, 0) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 1) == TO_SMALL_INT(5));
        TEST(KOS_array_read(ctx, src, 2) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 3) == TO_SMALL_INT(5));
        TEST(KOS_array_read(ctx, src, 4) == TO_SMALL_INT(6));

        for (i = 0; i < 5; i++)
            TEST(KOS_array_write(ctx, src, i, TO_SMALL_INT(i)) == KOS_SUCCESS);

        TEST(KOS_array_insert(ctx, src, 0, 2, src, 1, 4) == KOS_SUCCESS);
        TEST(KOS_get_array_size(src) == 6);
        TEST(KOS_array_read(ctx, src, 0) == TO_SMALL_INT(1));
        TEST(KOS_array_read(ctx, src, 1) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 2) == TO_SMALL_INT(3));
        TEST(KOS_array_read(ctx, src, 3) == TO_SMALL_INT(2));
        TEST(KOS_array_read(ctx, src, 4) == TO_SMALL_INT(3));
        TEST(KOS_array_read(ctx, src, 5) == TO_SMALL_INT(4));
    }

    /************************************************************************/
    /* Push/pop */
    {
        uint32_t   idx = ~0U;
        KOS_OBJ_ID v;
        KOS_OBJ_ID a   = KOS_new_array(ctx, 0);
        TEST(!IS_BAD_PTR(a));
        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_array_push(ctx, a, TO_SMALL_INT(123), &idx) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(idx == 0);
        TEST(KOS_get_array_size(a) == 1);

        v = KOS_array_pop(ctx, a);
        TEST(v == TO_SMALL_INT(123));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_array_size(a) == 0);

        v = KOS_array_pop(ctx, a);
        TEST(IS_BAD_PTR(v));
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 0);
    }

    /************************************************************************/
    /* KOS_array_fill */
    {
        TEST(KOS_array_fill(ctx, KOS_VOID, 0, 0, KOS_VOID) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* Read-only array of size 0 */
    {
        KOS_OBJ_ID b;
        KOS_OBJ_ID a = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(a));
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);
        TEST(KOS_get_array_size(a) == 0);

        TEST(KOS_lock_object(ctx, a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, KOS_VOID) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, a, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_push(ctx, a, TO_SMALL_INT(42), 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, a, 0, 0, a, 0, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        b = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(b));
        TEST(GET_OBJ_TYPE(b) == OBJ_ARRAY);
        TEST(KOS_get_array_size(b) == 1);
        TEST(KOS_array_read(ctx, b, 0) == KOS_VOID);

        TEST(KOS_array_insert(ctx, a, 0, 0, b, 0, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_get_array_size(a) == 0);
        TEST(KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY, a)->data) == KOS_BADPTR);
    }

    /************************************************************************/
    /* Read-only array of size 2 */
    {
        KOS_OBJ_ID a = KOS_new_array(ctx, 2);
        TEST( ! IS_BAD_PTR(a));
        TEST(GET_OBJ_TYPE(a) == OBJ_ARRAY);
        TEST(KOS_get_array_size(a) == 2);
        TEST(KOS_array_write(ctx, a, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_array_write(ctx, a, 1, TO_SMALL_INT(20)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_lock_object(ctx, a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(10));
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, a, 1) == TO_SMALL_INT(20));
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, KOS_VOID) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, TO_SMALL_INT(10)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 1, KOS_VOID) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 1, TO_SMALL_INT(20)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 2, KOS_VOID) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, a, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_reserve(ctx, a, 64) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 1) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 2) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_resize(ctx, a, 3) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_push(ctx, a, TO_SMALL_INT(42), 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 2);

        TEST(KOS_array_insert(ctx, a, 0, 0, a, 0, 2) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 2);

        TEST(KOS_array_insert(ctx, a, 0, 0, a, 0, 0) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 2);

        TEST(KOS_array_pop(ctx, a) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 2);

        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(10));
        TEST(KOS_array_cas(ctx, a, 0, TO_SMALL_INT(10), TO_SMALL_INT(30)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_array_size(a) == 2);
        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(10));

        TEST(KOS_array_fill(ctx, a, 0, 2, KOS_TRUE) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_get_array_size(a) == 2);
        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(10));
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, a, 1) == TO_SMALL_INT(20));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* Insert read-only array into a writable array */
    {
        KOS_OBJ_ID a;
        KOS_OBJ_ID b;
        int        i;

        a = KOS_new_array(ctx, 3);
        TEST( ! IS_BAD_PTR(a));
        b = KOS_new_array(ctx, 2);
        TEST( ! IS_BAD_PTR(b));

        for (i = 0; i < 3; i++) {
            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(100 + i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        for (i = 0; i < 2; i++) {
            TEST(KOS_array_write(ctx, b, i, TO_SMALL_INT(200 + i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_lock_object(ctx, b) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(ctx, b, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_array_insert(ctx, a, 1, 2, b, 0, 2) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 4);
        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(100));
        TEST(KOS_array_read(ctx, a, 1) == TO_SMALL_INT(200));
        TEST(KOS_array_read(ctx, a, 2) == TO_SMALL_INT(201));
        TEST(KOS_array_read(ctx, a, 3) == TO_SMALL_INT(102));

        TEST(KOS_get_array_size(b) == 2);
        TEST(KOS_array_read(ctx, b, 0) == TO_SMALL_INT(200));
        TEST(KOS_array_read(ctx, b, 1) == TO_SMALL_INT(201));

        TEST(KOS_array_write(ctx, b, 1, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_array_read(ctx, b, 1) == TO_SMALL_INT(201));

        TEST(KOS_array_write(ctx, a, 1, TO_SMALL_INT(-1)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, a, 1) == TO_SMALL_INT(-1));
    }

    /************************************************************************/
    /* Slice a read-only array */
    {
        KOS_OBJ_ID a;
        KOS_OBJ_ID b;
        int        i;

        a = KOS_new_array(ctx, 10);
        TEST( ! IS_BAD_PTR(a));

        for (i = 0; i < 10; i++) {
            TEST(KOS_array_write(ctx, a, i, TO_SMALL_INT(100 + i)) == KOS_SUCCESS);
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_lock_object(ctx, a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(ctx, a, 0, TO_SMALL_INT(1)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(KOS_array_read(ctx, a, 0) == TO_SMALL_INT(100));

        b = KOS_array_slice(ctx, a, 3, -2); /* 103..107 */
        TEST( ! IS_BAD_PTR(b));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(b) == OBJ_ARRAY);
        TEST(KOS_get_array_size(b) == 5);

        for (i = 0; i < 5; i++) {
            TEST(KOS_array_read(ctx, b, i) == TO_SMALL_INT(103 + i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_array_pop(ctx, b) == TO_SMALL_INT(107));
        TEST_NO_EXCEPTION();

        TEST(KOS_array_write(ctx, b, 1, KOS_TRUE) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_array_size(a) == 10);
        for (i = 0; i < 10; i++) {
            TEST(KOS_array_read(ctx, a, i) == TO_SMALL_INT(100 + i));
            TEST_NO_EXCEPTION();
        }

        TEST(KOS_get_array_size(b) == 4);
        TEST(KOS_array_read(ctx, b, 0) == TO_SMALL_INT(103));
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, b, 1) == KOS_TRUE);
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, b, 2) == TO_SMALL_INT(105));
        TEST_NO_EXCEPTION();
        TEST(KOS_array_read(ctx, b, 3) == TO_SMALL_INT(106));
        TEST_NO_EXCEPTION();
    }

    KOS_instance_destroy(&inst);

    return 0;
}

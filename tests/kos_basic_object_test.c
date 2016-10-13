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

#include "../inc/kos_object_base.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

int main(void)
{
    const intptr_t max_small_int = GET_SMALL_INT((KOS_OBJ_PTR)(~((uintptr_t)0) >> 1));
    const intptr_t min_small_int = -max_small_int - 1;

    KOS_CONTEXT ctx;

    TEST(KOS_context_init(&ctx) == KOS_SUCCESS);

    TEST(min_small_int < 0);
    TEST(max_small_int > 0);
    TEST(GET_SMALL_INT(TO_SMALL_INT(max_small_int))   == max_small_int);
    TEST(GET_SMALL_INT(TO_SMALL_INT(max_small_int+1)) != max_small_int+1);

    /************************************************************************/
    {
        const KOS_OBJ_PTR small_int0    = TO_SMALL_INT(0);
        const KOS_OBJ_PTR small_int_min = TO_SMALL_INT(min_small_int);
        const KOS_OBJ_PTR small_int_max = TO_SMALL_INT(max_small_int);

        TEST(!IS_BAD_PTR(small_int0));
        TEST(!IS_BAD_PTR(small_int_min));
        TEST(!IS_BAD_PTR(small_int_max));

        TEST(IS_SMALL_INT(small_int0));
        TEST(IS_SMALL_INT(small_int_min));
        TEST(IS_SMALL_INT(small_int_max));

        TEST(GET_SMALL_INT(small_int0)    == 0);
        TEST(GET_SMALL_INT(small_int_min) == min_small_int);
        TEST(GET_SMALL_INT(small_int_max) == max_small_int);

        TEST(IS_NUMERIC_OBJ(small_int0));
        TEST(IS_NUMERIC_OBJ(small_int_max));

        TEST(!IS_STRING_OBJ(small_int0));
        TEST(!IS_STRING_OBJ(small_int_max));

        TEST(!IS_TYPE(OBJ_OBJECT, small_int0));
        TEST(!IS_TYPE(OBJ_OBJECT, small_int_max));
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR integer_a = KOS_new_int(&ctx, min_small_int-1);
        const KOS_OBJ_PTR integer_b = KOS_new_int(&ctx, max_small_int+1);

        TEST(!IS_BAD_PTR(integer_a));
        TEST(!IS_BAD_PTR(integer_b));

        TEST(!IS_SMALL_INT(integer_a));
        TEST(!IS_SMALL_INT(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(!IS_STRING_OBJ(integer_a));
        TEST(!IS_STRING_OBJ(integer_b));

        TEST(!IS_TYPE(OBJ_OBJECT, integer_a));
        TEST(!IS_TYPE(OBJ_OBJECT, integer_b));

        TEST(GET_OBJ_TYPE(integer_a) == OBJ_INTEGER);
        TEST(GET_OBJ_TYPE(integer_b) == OBJ_INTEGER);

        TEST(OBJPTR(KOS_INTEGER, integer_a)->number == min_small_int-1);
        TEST(OBJPTR(KOS_INTEGER, integer_b)->number == max_small_int+1);
    }

    /************************************************************************/
    {
        const intptr_t max_int = (intptr_t)(~(uintptr_t)0 >> 1);
        const intptr_t min_int = -max_int - 1;

        const KOS_OBJ_PTR integer_a = KOS_new_int(&ctx, min_int);
        const KOS_OBJ_PTR integer_b = KOS_new_int(&ctx, max_int);

        TEST(!IS_BAD_PTR(integer_a));
        TEST(!IS_BAD_PTR(integer_b));

        TEST(!IS_SMALL_INT(integer_a));
        TEST(!IS_SMALL_INT(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(!IS_STRING_OBJ(integer_a));
        TEST(!IS_STRING_OBJ(integer_b));

        TEST(!IS_TYPE(OBJ_OBJECT, integer_a));
        TEST(!IS_TYPE(OBJ_OBJECT, integer_b));

        TEST(GET_OBJ_TYPE(integer_a) == OBJ_INTEGER);
        TEST(GET_OBJ_TYPE(integer_b) == OBJ_INTEGER);

        TEST(OBJPTR(KOS_INTEGER, integer_a)->number == min_int);
        TEST(OBJPTR(KOS_INTEGER, integer_b)->number == max_int);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR number = KOS_new_float(&ctx, 1.5);

        TEST(!IS_BAD_PTR(number));

        TEST(!IS_SMALL_INT(number));

        TEST(IS_NUMERIC_OBJ(number));

        TEST(!IS_STRING_OBJ(number));

        TEST(!IS_TYPE(OBJ_OBJECT, number));

        TEST(GET_OBJ_TYPE(number) == OBJ_FLOAT);

        TEST(OBJPTR(KOS_FLOAT, number)->number == 1.5);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR boolean = KOS_TRUE;

        TEST(!IS_BAD_PTR(boolean));

        TEST(!IS_SMALL_INT(boolean));

        TEST(!IS_NUMERIC_OBJ(boolean));

        TEST(!IS_STRING_OBJ(boolean));

        TEST(!IS_TYPE(OBJ_OBJECT, boolean));

        TEST(GET_OBJ_TYPE(boolean) == OBJ_BOOLEAN);

        TEST(KOS_get_bool(boolean));
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR a_void = KOS_VOID;

        TEST(!IS_BAD_PTR(a_void));

        TEST(!IS_SMALL_INT(a_void));

        TEST(!IS_NUMERIC_OBJ(a_void));

        TEST(!IS_STRING_OBJ(a_void));

        TEST(!IS_TYPE(OBJ_OBJECT, a_void));

        TEST(GET_OBJ_TYPE(a_void) == OBJ_VOID);
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(str, "str");
        const KOS_OBJ_PTR obj = TO_OBJPTR(&str);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(IS_STRING_OBJ(obj));

        TEST(!IS_TYPE(OBJ_OBJECT, obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING_8);

        TEST(KOS_get_string_length(obj) == 3);
    }

    /************************************************************************/
    {
        const uint16_t    str[] = { 1 };
        const KOS_OBJ_PTR obj   = KOS_new_const_string(&ctx, str, sizeof(str)/2, OBJ_STRING_16);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(IS_STRING_OBJ(obj));

        TEST(!IS_TYPE(OBJ_OBJECT, obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING_16);

        TEST(KOS_get_string_length(obj) == 1);
    }

    /************************************************************************/
    {
        const uint32_t    str[] = { 2, 3 };
        const KOS_OBJ_PTR obj   = KOS_new_const_string(&ctx, str, sizeof(str)/4, OBJ_STRING_32);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(IS_STRING_OBJ(obj));

        TEST(!IS_TYPE(OBJ_OBJECT, obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING_32);

        TEST(KOS_get_string_length(obj) == 2);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR obj = KOS_new_array(&ctx, 16);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(!IS_STRING_OBJ(obj));

        TEST(!IS_TYPE(OBJ_OBJECT, obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);

        TEST(KOS_get_array_size(obj) == 16);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR obj = KOS_new_object(&ctx);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(!IS_STRING_OBJ(obj));

        TEST(IS_TYPE(OBJ_OBJECT, obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_OBJECT);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

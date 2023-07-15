/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"
#include <stdio.h>

int main(void)
{
    const intptr_t max_small_int = GET_SMALL_INT((KOS_OBJ_ID)(~((uintptr_t)2) >> 1));
    const intptr_t min_small_int = -max_small_int - 1;

    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    TEST(min_small_int < 0);
    TEST(max_small_int > 0);
    TEST(GET_SMALL_INT(TO_SMALL_INT(max_small_int))   == max_small_int);
    TEST(GET_SMALL_INT(TO_SMALL_INT(max_small_int+1)) != max_small_int+1);

    /************************************************************************/
    {
        const KOS_OBJ_ID small_int0    = TO_SMALL_INT(0);
        const KOS_OBJ_ID small_int_min = TO_SMALL_INT(min_small_int);
        const KOS_OBJ_ID small_int_max = TO_SMALL_INT(max_small_int);

        TEST(!IS_BAD_PTR(small_int0));
        TEST(!IS_BAD_PTR(small_int_min));
        TEST(!IS_BAD_PTR(small_int_max));

        TEST(IS_SMALL_INT(small_int0));
        TEST(IS_SMALL_INT(small_int_min));
        TEST(IS_SMALL_INT(small_int_max));

        TEST(GET_SMALL_INT(small_int0)    == 0);
        TEST(GET_SMALL_INT(small_int_min) == min_small_int);
        TEST(GET_SMALL_INT(small_int_max) == max_small_int);

        TEST(!kos_is_heap_object(small_int0));
        TEST(!kos_is_heap_object(small_int_min));
        TEST(!kos_is_heap_object(small_int_max));

        TEST(IS_NUMERIC_OBJ(small_int0));
        TEST(IS_NUMERIC_OBJ(small_int_max));

        TEST(GET_OBJ_TYPE(small_int0) != OBJ_STRING);
        TEST(GET_OBJ_TYPE(small_int_max) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(small_int0) != OBJ_OBJECT);
        TEST(GET_OBJ_TYPE(small_int_max) != OBJ_OBJECT);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID integer_a = KOS_new_int(ctx, min_small_int-1);
        const KOS_OBJ_ID integer_b = KOS_new_int(ctx, max_small_int+1);

        TEST(!IS_BAD_PTR(integer_a));
        TEST(!IS_BAD_PTR(integer_b));

        TEST(!IS_SMALL_INT(integer_a));
        TEST(!IS_SMALL_INT(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(GET_OBJ_TYPE(integer_a) != OBJ_STRING);
        TEST(GET_OBJ_TYPE(integer_b) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(integer_a) != OBJ_OBJECT);
        TEST(GET_OBJ_TYPE(integer_b) != OBJ_OBJECT);

        TEST(IS_SMALL_INT(integer_a) || kos_is_heap_object(integer_a));
        TEST(IS_SMALL_INT(integer_b) || kos_is_heap_object(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(IS_SMALL_INT(integer_a) || GET_OBJ_TYPE(integer_a) == OBJ_INTEGER);
        TEST(IS_SMALL_INT(integer_b) || GET_OBJ_TYPE(integer_b) == OBJ_INTEGER);

        TEST(OBJPTR(INTEGER, integer_a)->value == min_small_int-1);
        TEST(OBJPTR(INTEGER, integer_b)->value == max_small_int+1);
    }

    /************************************************************************/
    {
        const intptr_t max_int = (intptr_t)(~(uintptr_t)0 >> 1);
        const intptr_t min_int = -max_int - 1;

        const KOS_OBJ_ID integer_a = KOS_new_int(ctx, min_int);
        const KOS_OBJ_ID integer_b = KOS_new_int(ctx, max_int);

        TEST(!IS_BAD_PTR(integer_a));
        TEST(!IS_BAD_PTR(integer_b));

        TEST(!IS_SMALL_INT(integer_a));
        TEST(!IS_SMALL_INT(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(GET_OBJ_TYPE(integer_a) != OBJ_STRING);
        TEST(GET_OBJ_TYPE(integer_b) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(integer_a) != OBJ_OBJECT);
        TEST(GET_OBJ_TYPE(integer_b) != OBJ_OBJECT);

        TEST(kos_is_heap_object(integer_a));
        TEST(kos_is_heap_object(integer_b));

        TEST(IS_NUMERIC_OBJ(integer_a));
        TEST(IS_NUMERIC_OBJ(integer_b));

        TEST(IS_SMALL_INT(integer_a) || GET_OBJ_TYPE(integer_a) == OBJ_INTEGER);
        TEST(IS_SMALL_INT(integer_b) || GET_OBJ_TYPE(integer_b) == OBJ_INTEGER);

        TEST(OBJPTR(INTEGER, integer_a)->value == min_int);
        TEST(OBJPTR(INTEGER, integer_b)->value == max_int);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID number = KOS_new_float(ctx, 1.5);

        TEST(!IS_BAD_PTR(number));

        TEST(!IS_SMALL_INT(number));

        TEST(IS_NUMERIC_OBJ(number));

        TEST(GET_OBJ_TYPE(number) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(number) != OBJ_OBJECT);

        TEST(IS_NUMERIC_OBJ(number));

        TEST(kos_is_heap_object(number));

        TEST(GET_OBJ_TYPE(number) == OBJ_FLOAT);

        TEST(OBJPTR(FLOAT, number)->value == 1.5);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID boolean = KOS_TRUE;

        TEST(!IS_BAD_PTR(boolean));

        TEST(!IS_SMALL_INT(boolean));

        TEST(!IS_NUMERIC_OBJ(boolean));

        TEST(!kos_is_heap_object(boolean));

        TEST(GET_OBJ_TYPE(boolean) == OBJ_BOOLEAN);

        TEST(KOS_get_bool(boolean));
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID a_void = KOS_VOID;

        TEST(!IS_BAD_PTR(a_void));

        TEST(!IS_SMALL_INT(a_void));

        TEST(!IS_NUMERIC_OBJ(a_void));

        TEST(!kos_is_heap_object(a_void));

        TEST(GET_OBJ_TYPE(a_void) == OBJ_VOID);
    }

    /************************************************************************/
    {
        const uint8_t    str[] = { 1, 0, 3 };
        const KOS_OBJ_ID obj   = KOS_new_const_string(ctx, str, sizeof(str), KOS_STRING_ELEM_8);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(kos_is_heap_object(obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING);

        TEST(GET_OBJ_TYPE(obj) != OBJ_OBJECT);

        TEST(KOS_get_string_length(obj) == 3);
    }

    /************************************************************************/
    {
        const uint16_t   str[] = { 1 };
        const KOS_OBJ_ID obj   = KOS_new_const_string(ctx, str, sizeof(str)/2, KOS_STRING_ELEM_16);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(kos_is_heap_object(obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING);

        TEST(GET_OBJ_TYPE(obj) != OBJ_OBJECT);

        TEST(KOS_get_string_length(obj) == 1);
    }

    /************************************************************************/
    {
        const uint32_t   str[] = { 2, 3 };
        const KOS_OBJ_ID obj   = KOS_new_const_string(ctx, str, sizeof(str)/4, KOS_STRING_ELEM_32);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(kos_is_heap_object(obj));

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING);

        TEST(GET_OBJ_TYPE(obj) != OBJ_OBJECT);

        TEST(KOS_get_string_length(obj) == 2);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID obj = KOS_new_array(ctx, 16);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(kos_is_heap_object(obj));

        TEST(GET_OBJ_TYPE(obj) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(obj) != OBJ_OBJECT);

        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);

        TEST(KOS_get_array_size(obj) == 16);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID obj = KOS_new_object(ctx);

        TEST(!IS_BAD_PTR(obj));

        TEST(!IS_SMALL_INT(obj));

        TEST(!IS_NUMERIC_OBJ(obj));

        TEST(kos_is_heap_object(obj));

        TEST(GET_OBJ_TYPE(obj) != OBJ_STRING);

        TEST(GET_OBJ_TYPE(obj) == OBJ_OBJECT);

        TEST(GET_OBJ_TYPE(obj) == OBJ_OBJECT);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_xyz, "xyz");
        KOS_OBJ_ID obj_id;

        TEST(KOS_lock_object(ctx, KOS_TRUE) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_lock_object(ctx, KOS_FALSE) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_lock_object(ctx, KOS_VOID) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_lock_object(ctx, TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_lock_object(ctx, KOS_CONST_ID(str_xyz)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_int(ctx, (int64_t)0x7FFFFFFF << 32);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_INTEGER);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_float(ctx, 0.1);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_FLOAT);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_function(ctx);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj_id = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        obj_id = KOS_new_class(ctx, KOS_VOID);
        TEST( ! IS_BAD_PTR(obj_id));
        TEST(GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
        TEST(KOS_lock_object(ctx, obj_id) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    KOS_instance_destroy(&inst);

    return 0;
}

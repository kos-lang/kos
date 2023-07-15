/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "kos_test_tools.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_name, "name");

enum TEST_ENUM {
    ENUM_VAL_A,
    ENUM_VAL_B,
    ENUM_VAL_C,
    ENUM_VAL_FORCE = 0x7FFFFFFF
};

typedef struct TEST_STRUCT_S {
    int16_t  field_i16;
    uint32_t field_u32;
    uint8_t  field_u8_4[4];
} TEST_STRUCT;

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    KOS_MEMPOOL  alloc;

    KOS_mempool_init(&alloc);

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        KOS_OBJ_ID               array;
        uint8_t                  a[2] = { 1, 2 };
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_UINT8 };

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0), &conv, KOS_NULL, a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        array = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(0x70)) == KOS_SUCCESS);

        TEST(KOS_extract_native_value(ctx, array, &conv, KOS_NULL, a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a[0] == 1);
        TEST(a[1] == 2);

        TEST(KOS_array_resize(ctx, array, 2) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 1, TO_SMALL_INT(-1)) == KOS_SUCCESS);

        TEST(KOS_extract_native_value(ctx, array, &conv, KOS_NULL, a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a[0] == 0x70);
        TEST(a[1] == 2);
        a[0] = 1;

        TEST(KOS_array_write(ctx, array, 1, TO_SMALL_INT(0xF0)) == KOS_SUCCESS);

        TEST(KOS_extract_native_value(ctx, array, &conv, KOS_NULL, a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a[0] == 0x70U);
        TEST(a[1] == 0xF0U);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT8 };
        uint8_t                  a    = 1;

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x100), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0xFF), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0xFFU);
    }

    /************************************************************************/
    {
        uint16_t                 a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_UINT16 };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x10000), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0xFFFF), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0xFFFFU);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT32 };
        KOS_OBJ_ID               big;
        uint32_t                 a    = 1;

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        big = KOS_new_int(ctx, (int64_t)1 << 32);
        TEST(KOS_extract_native_value(ctx, big, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0xFFFF), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0xFFFFU);
    }

    /************************************************************************/
    {
        uint64_t                 a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_UINT64 };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == ~(uint64_t)0U);
    }

    /************************************************************************/
    {
        int8_t                   a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_INT8 };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-0x81), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x80), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-0x80), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == -0x80);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x7F), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0x7F);
    }

    /************************************************************************/
    {
        int16_t                  a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_INT16 };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-0x8001), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x8000), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-0x8000), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == -0x8000);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(0x7FFF), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0x7FFF);
    }

    /************************************************************************/
    {
        int32_t                  a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_INT32 };
        KOS_OBJ_ID               too_small;
        KOS_OBJ_ID               too_big;
        KOS_OBJ_ID               value;

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        too_small = KOS_new_int(ctx, -(int64_t)((uint64_t)1U << 31) - 1);
        TEST( ! IS_BAD_PTR(too_small));

        TEST(KOS_extract_native_value(ctx, too_small, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        too_big = KOS_new_int(ctx, (int64_t)0x7FFFFFFF + 1);
        TEST( ! IS_BAD_PTR(too_big));

        TEST(KOS_extract_native_value(ctx, too_big, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == 1);

        value = KOS_new_int(ctx, (int32_t)((uint32_t)1U << 31));
        TEST( ! IS_BAD_PTR(value));

        TEST(KOS_extract_native_value(ctx, value, &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == (int32_t)((uint32_t)1U << 31));

        value = KOS_new_int(ctx, 0x7FFFFFFF);
        TEST( ! IS_BAD_PTR(value));

        TEST(KOS_extract_native_value(ctx, value, &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 0x7FFFFFFF);
    }

    /************************************************************************/
    {
        int64_t                  a    = 1;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_INT64 };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == -1);
    }

    /************************************************************************/
    {
        enum TEST_ENUM           a    = ENUM_VAL_A;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_ENUM };
        KOS_OBJ_ID               too_big;

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(-1), &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        too_big = KOS_new_int(ctx, (int64_t)((uint64_t)1U << 31));
        TEST( ! IS_BAD_PTR(too_big));

        TEST(KOS_extract_native_value(ctx, too_big, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(a == ENUM_VAL_A);

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(ENUM_VAL_B), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == ENUM_VAL_B);
    }

    /************************************************************************/
    {
        uint8_t                  a[3] = { 20, 30, 40 };
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(uint8_t), KOS_NATIVE_BOOL8 };

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(10), &conv, KOS_NULL, &a[1]) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a[1]) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a[0] == 20);
        TEST(a[1] == 1);
        TEST(a[2] == 40);
    }

    /************************************************************************/
    {
        uint32_t                 a[3] = { 0xBEECAFE, 30, 0xBEECAFE };
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(uint32_t), KOS_NATIVE_BOOL32 };

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(10), &conv, KOS_NULL, &a[1]) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a[1]) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a[0] == 0xBEECAFE);
        TEST(a[1] == 1);
        TEST(a[2] == 0xBEECAFE);
    }

    /************************************************************************/
    {
        float                    a    = 1.0f;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_FLOAT };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(2), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST((double)a == 2.0);
    }

    /************************************************************************/
    {
        double                   a    = 1.0;
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(a), KOS_NATIVE_DOUBLE };
        KOS_OBJ_ID               num;

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &a) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, TO_SMALL_INT(2), &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 2.0);

        num = KOS_new_int(ctx, (int64_t)((uint64_t)1U << 62));
        TEST( ! IS_BAD_PTR(num));

        TEST(KOS_extract_native_value(ctx, num, &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == pow(2, 62));

        num = KOS_new_float(ctx, 8.5);
        TEST( ! IS_BAD_PTR(num));

        TEST(KOS_extract_native_value(ctx, num, &conv, KOS_NULL, &a) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(a == 8.5);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_too_long, "abcd");
        KOS_DECLARE_STATIC_CONST_STRING(str_abc,      "abc");

        char                     str[4] = { 'x', 'x', 'x', 'x' };
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(str), KOS_NATIVE_STRING };

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, KOS_NULL, &str) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, KOS_CONST_ID(str_too_long), &conv, KOS_NULL, &str) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(memcmp(str, "xxxx", 4) == 0);

        TEST(KOS_extract_native_value(ctx, KOS_CONST_ID(str_abc), &conv, KOS_NULL, &str) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(memcmp(str, "abc", 4) == 0);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_long, "this is a test of a string");

        static const KOS_CONVERT conv  = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_STRING_PTR };
        const uint32_t           src[] = { ~0U };
        const KOS_OBJ_ID         str   = KOS_new_const_string(ctx, src, 1, KOS_STRING_ELEM_32);
        char                    *buf   = KOS_NULL;

        TEST( ! IS_BAD_PTR(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 1);

        TEST(KOS_extract_native_value(ctx, KOS_TRUE, &conv, &alloc, &buf) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_extract_native_value(ctx, str, &conv, &alloc, &buf) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(buf == KOS_NULL);

        TEST(KOS_extract_native_value(ctx, KOS_CONST_ID(str_long), &conv, &alloc, &buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(buf != KOS_NULL);
        TEST(strcmp(buf, "this is a test of a string") == 0);
    }

    /************************************************************************/
    {
        uint8_t                  buf[8] = { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 };
        static const KOS_CONVERT conv   = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(buf), KOS_NATIVE_BUFFER };
        KOS_OBJ_ID               buf_obj;

        TEST(KOS_extract_native_value(ctx, KOS_CONST_ID(str_name), &conv, KOS_NULL, &buf) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        buf_obj = KOS_new_buffer(ctx, 7);
        TEST( ! IS_BAD_PTR(buf_obj));

        TEST(KOS_extract_native_value(ctx, buf_obj, &conv, KOS_NULL, &buf) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(memcmp(buf, "AAAAAAAA", 8) == 0);

        TEST(KOS_buffer_resize(ctx, buf_obj, 8) == KOS_SUCCESS);
        memcpy(KOS_buffer_data_volatile(ctx, buf_obj), "abcdefgh", 8);

        TEST(KOS_extract_native_value(ctx, buf_obj, &conv, KOS_NULL, &buf) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(memcmp(buf, "abcdefgh", 8) == 0);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[3] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(uint32_t), KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(int16_t),  KOS_NATIVE_INT16  },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID array;
        uint32_t   val_u32 = 1;
        int16_t    val_i16 = 2;

        TEST(KOS_extract_native_from_array(ctx, KOS_TRUE, "", conv, KOS_NULL, &val_u32, &val_i16) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        array = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);

        TEST(val_u32 == 1);
        TEST(val_i16 == 2);

        TEST(KOS_extract_native_from_array(ctx, array, "", conv, KOS_NULL, &val_u32, &val_i16) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(val_u32 == 10);
        TEST(val_i16 == 2);

        TEST(KOS_array_resize(ctx, array, 2) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(20)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 1, TO_SMALL_INT(30)) == KOS_SUCCESS);

        TEST(KOS_extract_native_from_array(ctx, array, "", conv, KOS_NULL, &val_u32, &val_i16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(val_u32 == 20);
        TEST(val_i16 == 30);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[3] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(uint32_t), KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(int16_t),  KOS_NATIVE_INT16  },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID array;
        uint32_t   val_u32 = 1;
        int16_t    val_i16 = 2;

        array = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);

        TEST(val_u32 == 1);
        TEST(val_i16 == 2);

        TEST(KOS_extract_native_from_iterable(ctx, array, conv, KOS_NULL, &val_u32, &val_i16) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(val_u32 == 10);
        TEST(val_i16 == 2);

        TEST(KOS_array_resize(ctx, array, 2) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(20)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 1, TO_SMALL_INT(30)) == KOS_SUCCESS);

        TEST(KOS_extract_native_from_iterable(ctx, array, conv, KOS_NULL, &val_u32, &val_i16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(val_u32 == 20);
        TEST(val_i16 == 30);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[3] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR,       0, sizeof(uint32_t), KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_name), TO_SMALL_INT(-3), 0, sizeof(int16_t),  KOS_NATIVE_INT16  },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID array;
        uint32_t   val_u32 = 1;
        int16_t    val_i16 = 2;

        array = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);

        TEST(val_u32 == 1);
        TEST(val_i16 == 2);

        TEST(KOS_extract_native_from_array(ctx, array, "", conv, KOS_NULL, &val_u32, &val_i16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(val_u32 == 10);
        TEST(val_i16 == -3);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[3] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR,       0, sizeof(uint32_t), KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_name), TO_SMALL_INT(-3), 0, sizeof(int16_t),  KOS_NATIVE_INT16  },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID array;
        uint32_t   val_u32 = 1;
        int16_t    val_i16 = 2;

        array = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);

        TEST(val_u32 == 1);
        TEST(val_i16 == 2);

        TEST(KOS_extract_native_from_iterable(ctx, array, conv, KOS_NULL, &val_u32, &val_i16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(val_u32 == 10);
        TEST(val_i16 == -3);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[2] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(uint32_t), KOS_NATIVE_UINT32 },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID obj;
        uint32_t   val_u32 = 1;

        obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));

        TEST(KOS_extract_native_from_object(ctx, obj, conv, KOS_NULL, &val_u32) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_aaa, "aaa");
        KOS_DECLARE_STATIC_CONST_STRING(str_bbb, "bbb");
        KOS_DECLARE_STATIC_CONST_STRING(str_ccc, "ccc");

        static const KOS_CONVERT conv[4] = {
            { KOS_CONST_ID(str_aaa), TO_SMALL_INT(200),  0, 0, KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_bbb), KOS_BADPTR,         0, 0, KOS_NATIVE_FLOAT  },
            { KOS_CONST_ID(str_ccc), TO_SMALL_INT(-100), 0, 0, KOS_NATIVE_INT16  },
            KOS_DEFINE_TAIL_ARG()
        };

        KOS_OBJ_ID obj;
        uint32_t   val_u32 = 1;
        float      val_f   = 2;
        int16_t    val_i16 = 3;

        obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(KOS_set_property(ctx, obj, KOS_CONST_ID(str_bbb), TO_SMALL_INT(-10)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj, KOS_CONST_ID(str_ccc), TO_SMALL_INT(-11)) == KOS_SUCCESS);

        TEST(KOS_extract_native_from_object(ctx, obj, conv, KOS_NULL, &val_u32, &val_f, &val_i16) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(val_u32       == 200);
        TEST((double)val_f == -10.0);
        TEST(val_i16       == -11);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_field_i16,  "field_i16");
        KOS_DECLARE_STATIC_CONST_STRING(str_field_u32,  "field_u32");
        KOS_DECLARE_STATIC_CONST_STRING(str_field_u8_4, "field_u8_4");

        static const KOS_CONVERT conv[4] = {
            { KOS_CONST_ID(str_field_i16),  TO_SMALL_INT(100), offsetof(TEST_STRUCT, field_i16),  sizeof(int16_t),     KOS_NATIVE_INT16  },
            { KOS_CONST_ID(str_field_u32),  TO_SMALL_INT(200), offsetof(TEST_STRUCT, field_u32),  sizeof(uint32_t),    KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_field_u8_4), KOS_BADPTR,        offsetof(TEST_STRUCT, field_u8_4), sizeof(uint8_t) * 4, KOS_NATIVE_UINT8  },
            KOS_DEFINE_TAIL_ARG()
        };

        TEST_STRUCT test_struct = { 1, 2, { 3, 4, 5, 6 } };
        KOS_OBJ_ID  obj;

        obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(KOS_set_property(ctx, obj, KOS_CONST_ID(str_field_u32), TO_SMALL_INT(10)) == KOS_SUCCESS);

        TEST(KOS_extract_native_struct_from_object(ctx, obj, conv, KOS_NULL, &test_struct) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(test_struct.field_i16     == 100);
        TEST(test_struct.field_u32     == 10);
        TEST(test_struct.field_u8_4[0] == 3);
        TEST(test_struct.field_u8_4[1] == 4);
        TEST(test_struct.field_u8_4[2] == 5);
        TEST(test_struct.field_u8_4[3] == 6);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_field_i16,  "field_i16");
        KOS_DECLARE_STATIC_CONST_STRING(str_field_u32,  "field_u32");
        KOS_DECLARE_STATIC_CONST_STRING(str_field_u8_4, "field_u8_4");

        static const KOS_CONVERT conv[4] = {
            { KOS_CONST_ID(str_field_i16),  TO_SMALL_INT(100), offsetof(TEST_STRUCT, field_i16),  sizeof(int16_t),     KOS_NATIVE_INT16  },
            { KOS_CONST_ID(str_field_u32),  TO_SMALL_INT(200), offsetof(TEST_STRUCT, field_u32),  sizeof(uint32_t),    KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_field_u8_4), KOS_BADPTR,        offsetof(TEST_STRUCT, field_u8_4), sizeof(uint8_t) * 4, KOS_NATIVE_UINT8  },
            KOS_DEFINE_TAIL_ARG()
        };

        TEST_STRUCT test_struct = { 1, 2, { 3, 4, 5, 6 } };
        KOS_OBJ_ID  obj;
        KOS_OBJ_ID  array;

        obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(KOS_set_property(ctx, obj, KOS_CONST_ID(str_field_u32), TO_SMALL_INT(10)) == KOS_SUCCESS);

        array = KOS_new_array(ctx, 4);
        TEST( ! IS_BAD_PTR(array));
        TEST(KOS_array_write(ctx, array, 0, TO_SMALL_INT(20)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 1, TO_SMALL_INT(30)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 2, TO_SMALL_INT(40)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, array, 3, TO_SMALL_INT(50)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj, KOS_CONST_ID(str_field_u8_4), array) == KOS_SUCCESS);

        TEST(KOS_extract_native_struct_from_object(ctx, obj, conv, KOS_NULL, &test_struct) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(test_struct.field_i16     == 100);
        TEST(test_struct.field_u32     == 10);
        TEST(test_struct.field_u8_4[0] == 20);
        TEST(test_struct.field_u8_4[1] == 30);
        TEST(test_struct.field_u8_4[2] == 40);
        TEST(test_struct.field_u8_4[3] == 50);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT8 };
        KOS_OBJ_ID               obj;
        const uint8_t            a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 3, KOS_NATIVE_UINT8 };
        KOS_OBJ_ID               obj;
        const uint8_t            a[3] = { 10, 11, 12 };

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 3);
        TEST(KOS_array_read(ctx, obj, 0) == TO_SMALL_INT(10));
        TEST(KOS_array_read(ctx, obj, 1) == TO_SMALL_INT(11));
        TEST(KOS_array_read(ctx, obj, 2) == TO_SMALL_INT(12));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT16 };
        KOS_OBJ_ID               obj;
        const uint16_t           a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT32 };
        KOS_OBJ_ID               obj;
        const uint32_t           a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_UINT64 };
        KOS_OBJ_ID               obj;
        const uint64_t           a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_INT8 };
        KOS_OBJ_ID               obj;
        const int8_t             a    = -10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(-10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_INT16 };
        KOS_OBJ_ID               obj;
        const int16_t            a    = -10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(-10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_INT32 };
        KOS_OBJ_ID               obj;
        const int32_t            a    = -10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(-10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_INT64 };
        KOS_OBJ_ID               obj;
        const int64_t            a    = -10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(-10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_ENUM };
        KOS_OBJ_ID               obj;
        const uint32_t           a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(obj == TO_SMALL_INT(10));
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_BOOL8 };
        KOS_OBJ_ID               obj;
        const uint8_t            a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_BOOLEAN);
        TEST(obj == KOS_TRUE);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_BOOL8 };
        KOS_OBJ_ID               obj;
        const uint8_t            a[3] = { 2, 0, 3 };

        obj = KOS_new_from_native(ctx, &conv, &a[1]);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_BOOLEAN);
        TEST(obj == KOS_FALSE);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_BOOL32 };
        KOS_OBJ_ID               obj;
        const uint32_t           a    = 10;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_BOOLEAN);
        TEST(obj == KOS_TRUE);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_BOOL32 };
        KOS_OBJ_ID               obj;
        const uint32_t           a[3] = { 0xBEECAFE, 0, 0xBEECAFE };

        obj = KOS_new_from_native(ctx, &conv, &a[1]);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_BOOLEAN);
        TEST(obj == KOS_FALSE);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_FLOAT };
        KOS_OBJ_ID               obj;
        const float              a    = 8.5f;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_FLOAT);
        TEST(OBJPTR(FLOAT, obj)->value == 8.5);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_DOUBLE };
        KOS_OBJ_ID               obj;
        const double             a    = 8.5;

        obj = KOS_new_from_native(ctx, &conv, &a);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_FLOAT);
        TEST(OBJPTR(FLOAT, obj)->value == 8.5);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_abc, "abc");

        const char               abc[6] = { 'a', 'b', 'c', 0, 0, 0 };
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, sizeof(abc), KOS_NATIVE_STRING };
        KOS_OBJ_ID               obj;

        obj = KOS_new_from_native(ctx, &conv, abc);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING);
        TEST(KOS_get_string_length(obj) == 3);
        TEST(KOS_string_compare(obj, KOS_CONST_ID(str_abc)) == 0);
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_xyz, "xyz");

        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 0, KOS_NATIVE_STRING_PTR };
        KOS_OBJ_ID               obj;
        const char        *const xyz = "xyz";

        obj = KOS_new_from_native(ctx, &conv, &xyz);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_STRING);
        TEST(KOS_get_string_length(obj) == 3);
        TEST(KOS_string_compare(obj, KOS_CONST_ID(str_xyz)) == 0);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv = { KOS_CONST_ID(str_name), KOS_BADPTR, 0, 10, KOS_NATIVE_BUFFER };
        KOS_OBJ_ID               obj;
        const uint8_t            buf[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

        obj = KOS_new_from_native(ctx, &conv, buf);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(obj) == OBJ_BUFFER);
        TEST(KOS_get_buffer_size(obj) == 10);
        TEST(memcmp(KOS_buffer_data_volatile(ctx, obj), buf, 10) == 0);
    }

    /************************************************************************/
    {
        static const KOS_CONVERT conv[3] = {
            { KOS_CONST_ID(str_name), KOS_BADPTR, offsetof(TEST_STRUCT, field_u32), sizeof(uint32_t), KOS_NATIVE_UINT32 },
            { KOS_CONST_ID(str_name), KOS_BADPTR, offsetof(TEST_STRUCT, field_i16), sizeof(int16_t),  KOS_NATIVE_SKIP   },
            KOS_DEFINE_TAIL_ARG()
        };

        TEST_STRUCT test_struct = { 1, 2, { 3, 4, 5, 6 } };
        KOS_OBJ_ID  obj;

        TEST(KOS_set_properties_from_native(ctx, KOS_VOID, conv, &test_struct) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));

        TEST(KOS_set_properties_from_native(ctx, obj, conv, &test_struct) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        obj = KOS_get_property(ctx, obj, KOS_CONST_ID(str_name));
        TEST( ! IS_BAD_PTR(obj));

        TEST(obj == TO_SMALL_INT(2));
    }

    KOS_instance_destroy(&inst);

    KOS_mempool_destroy(&alloc);

    return 0;
}

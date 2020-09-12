/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_object.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../core/kos_object_internal.h"
#include <stdio.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

/* Always returns integer 256 */
static KOS_OBJ_ID return_256(KOS_CONTEXT ctx, KOS_OBJ_ID this_obj, KOS_OBJ_ID args_obj)
{
    return TO_SMALL_INT(256);
}

/* Yields integers 256 and 512 */
static KOS_OBJ_ID yield_256_and_512(KOS_CONTEXT ctx, KOS_OBJ_ID this_obj, KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID value;

    assert(GET_OBJ_TYPE(this_obj) == OBJ_ARRAY);

    if (KOS_get_array_size(this_obj) == 0) {
        if (KOS_array_push(ctx, this_obj, TO_SMALL_INT(256), 0) != KOS_SUCCESS)
            return KOS_BADPTR;
    }

    value = KOS_array_read(ctx, this_obj, 0);
    assert(IS_SMALL_INT(value));

    if (GET_SMALL_INT(value) <= 512) {
        KOS_OBJ_ID new_value = TO_SMALL_INT(GET_SMALL_INT(value) << 1);

        if (KOS_array_write(ctx, this_obj, 0, new_value) != KOS_SUCCESS)
            return KOS_BADPTR;

        return value;
    }

    return KOS_BADPTR;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_empty, "");
        KOS_OBJ_ID iter;

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_empty), KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
        /* ---------------------------------------------------------------- */


        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_empty), KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_empty), KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_abc, "abc");
        KOS_DECLARE_STATIC_CONST_STRING(str_a, "a");
        KOS_DECLARE_STATIC_CONST_STRING(str_b, "b");
        KOS_DECLARE_STATIC_CONST_STRING(str_c, "c");
        KOS_OBJ_ID iter;

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_abc), KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_abc), KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_CONST_ID(str_abc), KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(0));
        TEST(GET_OBJ_TYPE(KOS_get_walk_value(iter)) == OBJ_STRING);
        TEST(KOS_string_compare(KOS_get_walk_value(iter), KOS_CONST_ID(str_a)) == 0);

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(1));
        TEST(GET_OBJ_TYPE(KOS_get_walk_value(iter)) == OBJ_STRING);
        TEST(KOS_string_compare(KOS_get_walk_value(iter), KOS_CONST_ID(str_b)) == 0);

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(2));
        TEST(GET_OBJ_TYPE(KOS_get_walk_value(iter)) == OBJ_STRING);
        TEST(KOS_string_compare(KOS_get_walk_value(iter), KOS_CONST_ID(str_c)) == 0);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 0);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_array(ctx, 3);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 3);
        TEST(KOS_array_write(ctx, obj, 0, TO_SMALL_INT(10)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, obj, 1, TO_SMALL_INT(20)) == KOS_SUCCESS);
        TEST(KOS_array_write(ctx, obj, 2, TO_SMALL_INT(30)) == KOS_SUCCESS);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(0));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(10));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(1));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(20));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(2));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(30));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_buffer(ctx, 0);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_BUFFER);
        TEST(KOS_get_buffer_size(obj) == 0);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
        /* ---------------------------------------------------------------- */


        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_buffer(ctx, 3);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_BUFFER);
        TEST(KOS_get_buffer_size(obj) == 3);
        KOS_buffer_data_volatile(obj)[0] = 10;
        KOS_buffer_data_volatile(obj)[1] = 20;
        KOS_buffer_data_volatile(obj)[2] = 30;

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(0));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(10));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(1));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(20));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(2));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(30));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID iter;

        iter = KOS_new_iterator(ctx, KOS_TRUE, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_TRUE, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
        /* ---------------------------------------------------------------- */


        iter = KOS_new_iterator(ctx, KOS_TRUE, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == KOS_VOID);
        TEST(KOS_get_walk_value(iter) == KOS_TRUE);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID iter;

        iter = KOS_new_iterator(ctx, KOS_VOID, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_VOID, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, KOS_VOID, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_function(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
        OBJPTR(FUNCTION, obj)->handler = return_256;

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_prototype, "prototype");
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_class(ctx, KOS_VOID);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_CLASS);
        OBJPTR(FUNCTION, obj)->handler = return_256;

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);
        TEST(KOS_string_compare(KOS_get_walk_key(iter), KOS_CONST_ID(str_prototype)) == 0);
        TEST(GET_OBJ_TYPE(KOS_get_walk_value(iter)) == OBJ_DYNAMIC_PROP);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);
        TEST(KOS_string_compare(KOS_get_walk_key(iter), KOS_CONST_ID(str_prototype)) == 0);
        TEST(GET_OBJ_TYPE(KOS_get_walk_value(iter)) == OBJ_DYNAMIC_PROP);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_function(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
        OBJPTR(FUNCTION, obj)->handler = return_256;
        KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, obj)->state, KOS_GEN_INIT);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj;
        KOS_OBJ_ID iter;

        obj = KOS_new_function(ctx);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
        OBJPTR(FUNCTION, obj)->handler = yield_256_and_512;
        KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, obj)->state, KOS_GEN_INIT);

        {
            KOS_OBJ_ID args = KOS_new_array(ctx, 0);
            TEST( ! IS_BAD_PTR(args));
            obj = KOS_call_function(ctx, obj, KOS_VOID, args);
            TEST( ! IS_BAD_PTR(obj));
            TEST(GET_OBJ_TYPE(obj) == OBJ_FUNCTION);
            TEST(KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, obj)->state) == KOS_GEN_READY);
        }

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS)
            TEST(GET_OBJ_TYPE(KOS_get_walk_key(iter)) == OBJ_STRING);

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(0));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(256));

        TEST(KOS_iterator_next(ctx, iter) == KOS_SUCCESS);
        TEST(KOS_get_walk_key(iter) == TO_SMALL_INT(1));
        TEST(KOS_get_walk_value(iter) == TO_SMALL_INT(512));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    /************************************************************************/
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_x, "x");
        KOS_DECLARE_STATIC_CONST_STRING(str_y, "y");
        KOS_DECLARE_STATIC_CONST_STRING(str_z, "z");

        KOS_OBJ_ID obj;
        KOS_OBJ_ID proto;
        KOS_OBJ_ID iter;

        proto = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(proto));
        TEST(GET_OBJ_TYPE(proto) == OBJ_OBJECT);

        obj = KOS_new_object_with_prototype(ctx, proto);
        TEST( ! IS_BAD_PTR(obj));
        TEST(GET_OBJ_TYPE(obj) == OBJ_OBJECT);
        TEST(KOS_get_prototype(ctx, obj) == proto);

        TEST(KOS_set_property(ctx, proto, KOS_CONST_ID(str_x), TO_SMALL_INT(-100)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, proto, KOS_CONST_ID(str_y), TO_SMALL_INT(-400)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj,   KOS_CONST_ID(str_y), TO_SMALL_INT(-200)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj,   KOS_CONST_ID(str_z), TO_SMALL_INT(-300)) == KOS_SUCCESS);

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_SHALLOW);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS) {
            const KOS_OBJ_ID key   = KOS_get_walk_key(iter);
            const KOS_OBJ_ID value = KOS_get_walk_value(iter);

            TEST(GET_OBJ_TYPE(key) == OBJ_STRING);
            TEST(IS_SMALL_INT(value));

            if (key == KOS_CONST_ID(str_y)) {
                TEST(value == TO_SMALL_INT(-200));
            }
            else {
                TEST(key == KOS_CONST_ID(str_z));
                TEST(value == TO_SMALL_INT(-300));
            }
        }

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_DEEP);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS) {
            const KOS_OBJ_ID key   = KOS_get_walk_key(iter);
            const KOS_OBJ_ID value = KOS_get_walk_value(iter);

            TEST(GET_OBJ_TYPE(key) == OBJ_STRING);
            TEST(IS_SMALL_INT(value));

            if (key == KOS_CONST_ID(str_x)) {
                TEST(value == TO_SMALL_INT(-100));
            }
            else if (key == KOS_CONST_ID(str_y)) {
                TEST(value == TO_SMALL_INT(-200));
            }
            else {
                TEST(key == KOS_CONST_ID(str_z));
                TEST(value == TO_SMALL_INT(-300));
            }
        }

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        /* ---------------------------------------------------------------- */

        iter = KOS_new_iterator(ctx, obj, KOS_CONTENTS);
        TEST( ! IS_BAD_PTR(iter));

        while (KOS_iterator_next(ctx, iter) == KOS_SUCCESS) {
            const KOS_OBJ_ID key   = KOS_get_walk_key(iter);
            const KOS_OBJ_ID value = KOS_get_walk_value(iter);

            TEST(GET_OBJ_TYPE(key) == OBJ_STRING);
            TEST(IS_SMALL_INT(value));

            if (key == KOS_CONST_ID(str_y)) {
                TEST(value == TO_SMALL_INT(-200));
            }
            else {
                TEST(key == KOS_CONST_ID(str_z));
                TEST(value == TO_SMALL_INT(-300));
            }
        }

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));

        TEST(KOS_iterator_next(ctx, iter) == KOS_ERROR_NOT_FOUND);
        TEST(IS_BAD_PTR(KOS_get_walk_key(iter)));
        TEST(IS_BAD_PTR(KOS_get_walk_value(iter)));
    }

    KOS_instance_destroy(&inst);

    return 0;
}

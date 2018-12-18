/*
 * Copyright (c) 2014-2018 Chris Dragan
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

#include "../inc/kos_object.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../core/kos_object_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

static int _walk_object(KOS_CONTEXT                  ctx,
                        KOS_OBJ_ID                   obj,
                        KOS_OBJ_ID                  *expected,
                        unsigned                     num_expected,
                        enum KOS_OBJECT_WALK_DEPTH_E deep)
{
    KOS_OBJ_ID walk;
    unsigned   count = 0;

    walk = KOS_new_object_walk(ctx, obj, deep);
    TEST( ! IS_BAD_PTR(walk));

    while ( ! KOS_object_walk(ctx, walk)) {

        unsigned i;

        /* Find this key and value on the expected list */
        for (i = 0; i < num_expected; i += 2) {
            if (KOS_get_walk_key(walk)   == expected[i] &&
                KOS_get_walk_value(walk) == expected[i + 1])
                break;
        }

        /* Make sure that this key and value were expected */
        TEST(i < num_expected);
        ++count;
    }

    /* Make sure that each and every key/value pair has been found exactly once */
    TEST(count * 2U == num_expected);

    return KOS_SUCCESS;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    KOS_OBJ_ID str_aaa;
    KOS_OBJ_ID str_bbb;
    KOS_OBJ_ID str_ccc;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        static const char aaa[] = "aaa";
        static const char bbb[] = "bbb";
        static const char ccc[] = "ccc";

        str_aaa = KOS_new_const_ascii_cstring(ctx, aaa);
        str_bbb = KOS_new_const_ascii_cstring(ctx, bbb);
        str_ccc = KOS_new_const_ascii_cstring(ctx, ccc);

        TEST(!IS_BAD_PTR(str_aaa));
        TEST(!IS_BAD_PTR(str_bbb));
        TEST(!IS_BAD_PTR(str_ccc));
    }

    /************************************************************************/
    {
        static const char non_existent[] = "non existent";
        KOS_OBJ_ID        na;

        const KOS_OBJ_ID o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));
        TEST(!IS_SMALL_INT(o));
        TEST(GET_OBJ_TYPE(o) == OBJ_OBJECT);

        na = KOS_new_const_ascii_cstring(ctx, non_existent);
        TEST(!IS_BAD_PTR(na));

        /* Can delete non-existent property */
        TEST(KOS_delete_property(ctx, o, na) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, na)));
        TEST_EXCEPTION();

        /* Invalid property pointer */
        TEST(KOS_delete_property(ctx, o, KOS_BADPTR) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID walk;

        const KOS_OBJ_ID o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));

        /* Set two properties */
        TEST(KOS_set_property(ctx, o, str_aaa, TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_set_property(ctx, o, str_bbb, TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Retrieve both properties */
        TEST(KOS_get_property(ctx, o, str_aaa) == TO_SMALL_INT(100));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_bbb) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve both properties by walking */
        {
            walk = KOS_new_object_walk(ctx, o, KOS_SHALLOW);
            TEST( ! IS_BAD_PTR(walk));

            TEST(KOS_object_walk(ctx, walk) == KOS_SUCCESS);
            TEST(KOS_get_walk_key(walk)   == str_aaa);
            TEST(KOS_get_walk_value(walk) == TO_SMALL_INT(100));

            TEST(KOS_object_walk(ctx, walk) == KOS_SUCCESS);
            TEST(KOS_get_walk_key(walk)   == str_bbb);
            TEST(KOS_get_walk_value(walk) == TO_SMALL_INT(200));

            TEST(KOS_object_walk(ctx, walk) == KOS_ERROR_NOT_FOUND);
            TEST(IS_BAD_PTR(KOS_get_walk_key(walk)));
            TEST(IS_BAD_PTR(KOS_get_walk_value(walk)));

            TEST(KOS_object_walk(ctx, walk) == KOS_ERROR_NOT_FOUND);
            TEST(IS_BAD_PTR(KOS_get_walk_key(walk)));
            TEST(IS_BAD_PTR(KOS_get_walk_value(walk)));
        }

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, str_ccc)));
        TEST_EXCEPTION();

        /* Delete property */
        TEST(KOS_delete_property(ctx, o, str_aaa) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve a property after it has been deleted */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, str_aaa)));
        TEST_EXCEPTION();

        /* Retrieve the remaining property */
        TEST(KOS_get_property(ctx, o, str_bbb) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve the remaining property by walking */
        {
            walk = KOS_new_object_walk(ctx, o, KOS_SHALLOW);
            TEST( ! IS_BAD_PTR(walk));

            TEST(KOS_object_walk(ctx, walk) == KOS_SUCCESS);
            TEST(KOS_get_walk_key(walk)   == str_bbb);
            TEST(KOS_get_walk_value(walk) == TO_SMALL_INT(200));

            TEST(KOS_object_walk(ctx, walk) == KOS_ERROR_NOT_FOUND);
            TEST(IS_BAD_PTR(KOS_get_walk_key(walk)));
            TEST(IS_BAD_PTR(KOS_get_walk_value(walk)));

            TEST(KOS_object_walk(ctx, walk) == KOS_ERROR_NOT_FOUND);
            TEST(IS_BAD_PTR(KOS_get_walk_key(walk)));
            TEST(IS_BAD_PTR(KOS_get_walk_value(walk)));
        }
    }

    /************************************************************************/
    {
        static const char cstr[] = "string";
        KOS_OBJ_ID        str;

        const KOS_OBJ_ID o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));

        str = KOS_new_const_ascii_cstring(ctx, cstr);
        TEST(!IS_BAD_PTR(str));

        /* Cannot set property when value is null pointer */
        TEST(KOS_set_property(ctx, o, str, KOS_BADPTR) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of null pointer */
        TEST(KOS_set_property(ctx, KOS_BADPTR, str, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a number */
        TEST(KOS_set_property(ctx, TO_SMALL_INT(123), str, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a string */
        TEST(KOS_set_property(ctx, str, str, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a boolean */
        TEST(KOS_set_property(ctx, KOS_TRUE, str, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a void */
        TEST(KOS_set_property(ctx, KOS_VOID, str, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));

        /* Cannot set property when property name is a null pointer */
        TEST(KOS_set_property(ctx, o, KOS_BADPTR, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is an object */
        TEST(KOS_set_property(ctx, o, o, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a number */
        TEST(KOS_set_property(ctx, o, TO_SMALL_INT(1), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a boolean */
        TEST(KOS_set_property(ctx, o, KOS_FALSE, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a void */
        TEST(KOS_set_property(ctx, o, KOS_VOID, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        static const char cstr[] = "string";
        KOS_OBJ_ID        str;

        const KOS_OBJ_ID o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));

        str = KOS_new_const_ascii_cstring(ctx, cstr);
        TEST(!IS_BAD_PTR(str));

        /* Can set property if name and value are correct */
        TEST(KOS_set_property(ctx, o, str, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve property of a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, KOS_BADPTR, str)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, KOS_BADPTR)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a number */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, TO_SMALL_INT(10))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a boolean */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, KOS_FALSE)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a void */
        TEST(IS_BAD_PTR(KOS_get_property(ctx, o, KOS_VOID)));
        TEST_EXCEPTION();

        /* Can retrieve correct property */
        TEST(KOS_get_property(ctx, o, str) == TO_SMALL_INT(3));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        #define NUM_PROPS 128
        KOS_OBJ_ID o;
        KOS_OBJ_ID prop_names[NUM_PROPS];
        int        have_prop[NUM_PROPS];
        int        i;

        for (i=0; i < NUM_PROPS; i++) {
            char str_num[16];
            snprintf(str_num, sizeof(str_num), "%d", i);
            prop_names[i] = KOS_new_cstring(ctx, str_num);
            have_prop[i]  = 0;
        }

        o = KOS_new_object(ctx);
        TEST(!IS_BAD_PTR(o));

        srand((unsigned)time(0));

        for (i=0; i < NUM_PROPS * 4; i++) {
            const int r   = rand();
            const int idx = r % NUM_PROPS;
            const int f   = (r / NUM_PROPS) % 3;

            switch (f) {

                case 0:
                    TEST(KOS_set_property(ctx, o, prop_names[idx], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(ctx, o, prop_names[idx]) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = 0;
                    break;

                case 2:
                    if (have_prop[idx]) {
                        TEST(KOS_get_property(ctx, o, prop_names[idx]) == TO_SMALL_INT(have_prop[idx]));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(ctx, o, prop_names[idx]) == KOS_BADPTR);
                        TEST_EXCEPTION();
                    }
                    break;

                default:
                    break;
            }
        }
    }

    /************************************************************************/
    {
        KOS_OBJ_ID base = KOS_new_object(ctx);
        KOS_OBJ_ID o    = KOS_new_object_with_prototype(ctx, base);

        TEST(!IS_BAD_PTR(base));
        TEST(!IS_BAD_PTR(o));

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(ctx, o, str_aaa) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Add properties to the prototype */
        TEST(KOS_set_property(ctx, base, str_aaa, TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, base, str_bbb, TO_SMALL_INT(2)) == KOS_SUCCESS);

        /* Can retrieve properties from prototype */
        TEST(KOS_get_property(ctx, o, str_aaa) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_bbb) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(ctx, o, str_ccc) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Set properties */
        TEST(KOS_set_property(ctx, o, str_aaa, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, o, str_ccc, TO_SMALL_INT(4)) == KOS_SUCCESS);

        /* Check all properties */
        TEST(KOS_get_property(ctx, base, str_aaa) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, base, str_bbb) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, base, str_ccc) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_aaa) == TO_SMALL_INT(3));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_bbb) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_ccc) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete some properties */
        TEST(KOS_delete_property(ctx, base, str_bbb) == KOS_SUCCESS);
        TEST(KOS_delete_property(ctx, o,    str_aaa) == KOS_SUCCESS);

        /* Check all properties again */
        TEST(KOS_get_property(ctx, base, str_aaa) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, base, str_bbb) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(ctx, base, str_ccc) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_aaa) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_bbb) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_ccc) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete more properties */
        TEST(KOS_delete_property(ctx, o, str_aaa) == KOS_SUCCESS);
        TEST(KOS_delete_property(ctx, o, str_bbb) == KOS_SUCCESS);
        TEST(KOS_delete_property(ctx, o, str_ccc) == KOS_SUCCESS);

        /* Check properties again */
        TEST(KOS_get_property(ctx, o, str_aaa) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_bbb) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(ctx, o, str_ccc) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        #define NUM_PROPS 128
        KOS_OBJ_ID o[3];
        KOS_OBJ_ID prop_names[NUM_PROPS];
        int        have_prop[3][NUM_PROPS];
        int        i;

        for (i=0; i < NUM_PROPS; i++) {
            char str_num[16];
            snprintf(str_num, sizeof(str_num), "%d", i);
            prop_names[i]   = KOS_new_cstring(ctx, str_num);
            have_prop[0][i] = 0;
            have_prop[1][i] = 0;
            have_prop[2][i] = 0;
        }

        o[0] = KOS_new_object(ctx);
        o[1] = KOS_new_object_with_prototype(ctx, o[0]);
        o[2] = KOS_new_object_with_prototype(ctx, o[1]);

        TEST(!IS_BAD_PTR(o[0]));
        TEST(!IS_BAD_PTR(o[1]));
        TEST(!IS_BAD_PTR(o[2]));

        srand((unsigned)time(0));

        for (i=0; i < NUM_PROPS * 16; i++) {
            const int r      = rand();
            const int i_prop = r % NUM_PROPS;
            const int f      = (r / NUM_PROPS) % 3;
            const int i_obj  = (r / (3*NUM_PROPS)) % 3;
            int       expect = 0;
            int       j;

            switch (f) {

                case 0:
                    TEST(KOS_set_property(ctx, o[i_obj], prop_names[i_prop], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[i_obj][i_prop] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(ctx, o[i_obj], prop_names[i_prop]) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[i_obj][i_prop] = 0;
                    break;

                case 2:
                    for (j = i_obj; j >= 0; j--) {
                        expect = have_prop[j][i_prop];
                        if (expect)
                            break;
                    }
                    if (expect) {
                        TEST(KOS_get_property(ctx, o[i_obj], prop_names[i_prop]) == TO_SMALL_INT(expect));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(ctx, o[i_obj], prop_names[i_prop]) == KOS_BADPTR);
                        TEST_EXCEPTION();
                    }
                    break;

                default:
                    break;
            }
        }
    }

    /************************************************************************/
    {
        KOS_OBJ_ID obj_a = KOS_new_object(ctx);
        KOS_OBJ_ID obj_b = KOS_new_object_with_prototype(ctx, obj_a);
        KOS_OBJ_ID obj_c = KOS_new_object_with_prototype(ctx, obj_b);
        KOS_OBJ_ID obj_d = KOS_new_object_with_prototype(ctx, obj_c);

        static const char s1[] = "1";
        static const char s2[] = "2";
        static const char s3[] = "3";
        static const char s4[] = "4";
        static const char s5[] = "5";
        static const char s6[] = "6";

        const KOS_OBJ_ID str_1 = KOS_new_const_ascii_cstring(ctx, s1);
        const KOS_OBJ_ID str_2 = KOS_new_const_ascii_cstring(ctx, s2);
        const KOS_OBJ_ID str_3 = KOS_new_const_ascii_cstring(ctx, s3);
        const KOS_OBJ_ID str_4 = KOS_new_const_ascii_cstring(ctx, s4);
        const KOS_OBJ_ID str_5 = KOS_new_const_ascii_cstring(ctx, s5);
        const KOS_OBJ_ID str_6 = KOS_new_const_ascii_cstring(ctx, s6);

        TEST(!IS_BAD_PTR(obj_a));
        TEST(!IS_BAD_PTR(obj_b));
        TEST(!IS_BAD_PTR(obj_c));
        TEST(!IS_BAD_PTR(obj_d));

        TEST(!IS_BAD_PTR(str_1));
        TEST(!IS_BAD_PTR(str_2));
        TEST(!IS_BAD_PTR(str_3));
        TEST(!IS_BAD_PTR(str_4));
        TEST(!IS_BAD_PTR(str_5));
        TEST(!IS_BAD_PTR(str_6));

        TEST(KOS_set_property(ctx, obj_a, str_1, TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_a, str_2, TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_c, str_2, TO_SMALL_INT(2)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_a, str_3, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_c, str_4, TO_SMALL_INT(4)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_a, str_5, TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_d, str_5, TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj_d, str_6, TO_SMALL_INT(6)) == KOS_SUCCESS);

        TEST(KOS_set_property(ctx, obj_a, str_6, TO_SMALL_INT(300)) == KOS_SUCCESS);
        TEST(KOS_delete_property(ctx, obj_a, str_6) == KOS_SUCCESS);

        {
            KOS_OBJ_ID expected[] = {
                KOS_BADPTR, TO_SMALL_INT(1),
                KOS_BADPTR, TO_SMALL_INT(100),
                KOS_BADPTR, TO_SMALL_INT(3),
                KOS_BADPTR, TO_SMALL_INT(200)
            };
            expected[0] = str_1;
            expected[2] = str_2;
            expected[4] = str_3;
            expected[6] = str_5;

            TEST(_walk_object(ctx, obj_a, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
            TEST(_walk_object(ctx, obj_b, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJ_ID walk;

            walk = KOS_new_object_walk(ctx, obj_b, KOS_SHALLOW);
            TEST( ! IS_BAD_PTR(walk));

            TEST(KOS_object_walk(ctx, walk) == KOS_ERROR_NOT_FOUND);
            TEST(IS_BAD_PTR(KOS_get_walk_key(walk)));
            TEST(IS_BAD_PTR(KOS_get_walk_value(walk)));
        }

        {
            KOS_OBJ_ID expected[] = {
                KOS_BADPTR, TO_SMALL_INT(2),
                KOS_BADPTR, TO_SMALL_INT(4)
            };
            expected[0] = str_2;
            expected[2] = str_4;

            TEST(_walk_object(ctx, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJ_ID expected[] = {
                KOS_BADPTR, TO_SMALL_INT(1),
                KOS_BADPTR, TO_SMALL_INT(2),
                KOS_BADPTR, TO_SMALL_INT(3),
                KOS_BADPTR, TO_SMALL_INT(4),
                KOS_BADPTR, TO_SMALL_INT(200)
            };
            expected[0] = str_1;
            expected[2] = str_2;
            expected[4] = str_3;
            expected[6] = str_4;
            expected[8] = str_5;

            TEST(_walk_object(ctx, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJ_ID expected[] = {
                KOS_BADPTR, TO_SMALL_INT(5),
                KOS_BADPTR, TO_SMALL_INT(6)
            };
            expected[0] = str_5;
            expected[2] = str_6;

            TEST(_walk_object(ctx, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJ_ID expected[] = {
                KOS_BADPTR, TO_SMALL_INT(1),
                KOS_BADPTR, TO_SMALL_INT(2),
                KOS_BADPTR, TO_SMALL_INT(3),
                KOS_BADPTR, TO_SMALL_INT(4),
                KOS_BADPTR, TO_SMALL_INT(5),
                KOS_BADPTR, TO_SMALL_INT(6)
            };
            expected[0]  = str_1;
            expected[2]  = str_2;
            expected[4]  = str_3;
            expected[6]  = str_4;
            expected[8]  = str_5;
            expected[10] = str_6;

            TEST(_walk_object(ctx, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }
    }

    /************************************************************************/
    {
        static const char abc[] = "abc";
        static const char cde[] = "cde";
        static const char efg[] = "efg";
        static const char ghi[] = "ghi";

        const KOS_OBJ_ID str_abc = KOS_new_const_ascii_cstring(ctx, abc);
        const KOS_OBJ_ID str_cde = KOS_new_const_ascii_cstring(ctx, cde);
        const KOS_OBJ_ID str_efg = KOS_new_const_ascii_cstring(ctx, efg);
        const KOS_OBJ_ID str_ghi = KOS_new_const_ascii_cstring(ctx, ghi);

        KOS_OBJ_ID obj = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj));

        TEST( ! IS_BAD_PTR(str_abc));
        TEST( ! IS_BAD_PTR(str_cde));
        TEST( ! IS_BAD_PTR(str_efg));
        TEST( ! IS_BAD_PTR(str_ghi));

        TEST(KOS_set_property(ctx, obj, str_abc, TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj, str_cde, TO_SMALL_INT(2)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj, str_efg, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(ctx, obj, str_ghi, TO_SMALL_INT(4)) == KOS_SUCCESS);

        TEST(kos_object_copy_prop_table(ctx, obj) == KOS_SUCCESS);

        TEST(KOS_get_property(ctx, obj, str_abc) == TO_SMALL_INT(1));
        TEST(KOS_get_property(ctx, obj, str_cde) == TO_SMALL_INT(2));
        TEST(KOS_get_property(ctx, obj, str_efg) == TO_SMALL_INT(3));
        TEST(KOS_get_property(ctx, obj, str_ghi) == TO_SMALL_INT(4));
    }

    KOS_instance_destroy(&inst);

    return 0;
}

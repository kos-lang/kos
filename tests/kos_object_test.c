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

#include "../inc/kos_object.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "../core/kos_object_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

static int _walk_object(KOS_FRAME                  frame,
                        KOS_OBJ_ID                 obj,
                        KOS_OBJECT_WALK_ELEM      *expected,
                        unsigned                   num_expected,
                        enum KOS_OBJECT_WALK_DEPTH deep)
{
    KOS_OBJECT_WALK      walk;
    KOS_OBJECT_WALK_ELEM elem;
    unsigned             count = 0;

    TEST(KOS_object_walk_init(frame, &walk, obj, deep) == KOS_SUCCESS);

    for (;;) {
        unsigned i;

        elem = KOS_object_walk(frame, &walk);
        if (IS_BAD_PTR(elem.key)) {
            TEST(IS_BAD_PTR(elem.value));
            break;
        }

        /* Find this key and value on the expected list */
        for (i = 0; i < num_expected; i++)
            if (elem.key == expected[i].key && elem.value == expected[i].value)
                break;

        /* Make sure that this key and value were expected */
        TEST(i < num_expected);
        ++count;
    }

    /* Make sure that each and every key/value pair has been found exactly once */
    TEST(count == num_expected);

    return KOS_SUCCESS;
}

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    {
        static const char non_existent[] = "non existent";

        const KOS_OBJ_ID o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));
        TEST(!IS_SMALL_INT(o));
        TEST(GET_OBJ_TYPE(o) == OBJ_OBJECT);

        /* Can delete non-existent property */
        TEST(KOS_delete_property(frame, o, KOS_context_get_cstring(frame, non_existent)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, KOS_context_get_cstring(frame, non_existent))));
        TEST_EXCEPTION();

        /* Invalid property pointer */
        TEST(KOS_delete_property(frame, o, KOS_BADPTR) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        static const char aaa[] = "aaa";
        static const char bbb[] = "bbb";
        static const char ccc[] = "ccc";

        KOS_OBJECT_WALK      walk;
        KOS_OBJECT_WALK_ELEM elem;

        const KOS_OBJ_ID str_aaa = KOS_context_get_cstring(frame, aaa);
        const KOS_OBJ_ID str_bbb = KOS_context_get_cstring(frame, bbb);
        const KOS_OBJ_ID str_ccc = KOS_context_get_cstring(frame, ccc);

        const KOS_OBJ_ID o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));

        /* Set two properties */
        TEST(KOS_set_property(frame, o, str_aaa, TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_set_property(frame, o, str_bbb, TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Retrieve both properties */
        TEST(KOS_get_property(frame, o, str_aaa) == TO_SMALL_INT(100));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, str_bbb) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve both properties by walking */
        {
            TEST(KOS_object_walk_init_shallow(frame, &walk, o) == KOS_SUCCESS);

            elem = KOS_object_walk(frame, &walk);
            TEST(elem.key   == str_aaa);
            TEST(elem.value == TO_SMALL_INT(100));

            elem = KOS_object_walk(frame, &walk);
            TEST(elem.key   == str_bbb);
            TEST(elem.value == TO_SMALL_INT(200));

            elem = KOS_object_walk(frame, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));

            elem = KOS_object_walk(frame, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, str_ccc)));
        TEST_EXCEPTION();

        /* Delete property */
        TEST(KOS_delete_property(frame, o, str_aaa) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve a property after it has been deleted */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa))));
        TEST_EXCEPTION();

        /* Retrieve the remaining property */
        TEST(KOS_get_property(frame, o, str_bbb) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve the remaining property by walking */
        {
            TEST(KOS_object_walk_init_shallow(frame, &walk, o) == KOS_SUCCESS);

            elem = KOS_object_walk(frame, &walk);
            TEST(elem.key   == str_bbb);
            TEST(elem.value == TO_SMALL_INT(200));

            elem = KOS_object_walk(frame, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));

            elem = KOS_object_walk(frame, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }
    }

    /************************************************************************/
    {
        static const char str[] = "string";

        const KOS_OBJ_ID o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));

        /* Cannot set property when value is null pointer */
        TEST(KOS_set_property(frame, o, KOS_context_get_cstring(frame, str), KOS_BADPTR) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of null pointer */
        TEST(KOS_set_property(frame, KOS_BADPTR, KOS_context_get_cstring(frame, str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a number */
        TEST(KOS_set_property(frame, TO_SMALL_INT(123), KOS_context_get_cstring(frame, str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a string */
        TEST(KOS_set_property(frame, KOS_context_get_cstring(frame, str), KOS_context_get_cstring(frame, str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a boolean */
        TEST(KOS_set_property(frame, KOS_TRUE, KOS_context_get_cstring(frame, str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a void */
        TEST(KOS_set_property(frame, KOS_VOID, KOS_context_get_cstring(frame, str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));

        /* Cannot set property when property name is a null pointer */
        TEST(KOS_set_property(frame, o, KOS_BADPTR, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is an object */
        TEST(KOS_set_property(frame, o, o, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a number */
        TEST(KOS_set_property(frame, o, TO_SMALL_INT(1), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a boolean */
        TEST(KOS_set_property(frame, o, KOS_FALSE, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a void */
        TEST(KOS_set_property(frame, o, KOS_VOID, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        static const char str[] = "string";

        const KOS_OBJ_ID o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));

        /* Can set property if name and value are correct */
        TEST(KOS_set_property(frame, o, KOS_context_get_cstring(frame, str), TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve property of a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(frame, KOS_BADPTR, KOS_context_get_cstring(frame, str))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, KOS_BADPTR)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a number */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, TO_SMALL_INT(10))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a boolean */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, KOS_FALSE)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a void */
        TEST(IS_BAD_PTR(KOS_get_property(frame, o, KOS_VOID)));
        TEST_EXCEPTION();

        /* Can retrieve correct property */
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, str)) == TO_SMALL_INT(3));
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
            prop_names[i] = KOS_new_cstring(frame, str_num);
            have_prop[i]  = 0;
        }

        o = KOS_new_object(frame);
        TEST(!IS_BAD_PTR(o));

        srand((unsigned)time(0));

        for (i=0; i < NUM_PROPS * 4; i++) {
            const int r   = rand();
            const int idx = r % NUM_PROPS;
            const int f   = (r / NUM_PROPS) % 3;

            switch (f) {

                case 0:
                    TEST(KOS_set_property(frame, o, prop_names[idx], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(frame, o, prop_names[idx]) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = 0;
                    break;

                case 2:
                    if (have_prop[idx]) {
                        TEST(KOS_get_property(frame, o, prop_names[idx]) == TO_SMALL_INT(have_prop[idx]));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(frame, o, prop_names[idx]) == KOS_BADPTR);
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
        KOS_OBJ_ID base = KOS_new_object(frame);
        KOS_OBJ_ID o    = KOS_new_object_with_prototype(frame, base);

        static const char aaa[] = "aaa";
        static const char bbb[] = "bbb";
        static const char ccc[] = "ccc";

        TEST(!IS_BAD_PTR(base));
        TEST(!IS_BAD_PTR(o));

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa)) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Add properties to the prototype */
        TEST(KOS_set_property(frame, base, KOS_context_get_cstring(frame, aaa), TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, base, KOS_context_get_cstring(frame, bbb), TO_SMALL_INT(2)) == KOS_SUCCESS);

        /* Can retrieve properties from prototype */
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, ccc)) == KOS_BADPTR);
        TEST_EXCEPTION();

        /* Set properties */
        TEST(KOS_set_property(frame, o, KOS_context_get_cstring(frame, aaa), TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, o, KOS_context_get_cstring(frame, ccc), TO_SMALL_INT(4)) == KOS_SUCCESS);

        /* Check all properties */
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, ccc)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(3));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, ccc)) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete some properties */
        TEST(KOS_delete_property(frame, base, KOS_context_get_cstring(frame, bbb)) == KOS_SUCCESS);
        TEST(KOS_delete_property(frame, o,    KOS_context_get_cstring(frame, aaa)) == KOS_SUCCESS);

        /* Check all properties again */
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, bbb)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(frame, base, KOS_context_get_cstring(frame, ccc)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, bbb)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, ccc)) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete more properties */
        TEST(KOS_delete_property(frame, o, KOS_context_get_cstring(frame, aaa)) == KOS_SUCCESS);
        TEST(KOS_delete_property(frame, o, KOS_context_get_cstring(frame, bbb)) == KOS_SUCCESS);
        TEST(KOS_delete_property(frame, o, KOS_context_get_cstring(frame, ccc)) == KOS_SUCCESS);

        /* Check properties again */
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, bbb)) == KOS_BADPTR);
        TEST_EXCEPTION();
        TEST(KOS_get_property(frame, o, KOS_context_get_cstring(frame, ccc)) == KOS_BADPTR);
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
            prop_names[i]   = KOS_new_cstring(frame, str_num);
            have_prop[0][i] = 0;
            have_prop[1][i] = 0;
            have_prop[2][i] = 0;
        }

        o[0] = KOS_new_object(frame);
        o[1] = KOS_new_object_with_prototype(frame, o[0]);
        o[2] = KOS_new_object_with_prototype(frame, o[1]);

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
                    TEST(KOS_set_property(frame, o[i_obj], prop_names[i_prop], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[i_obj][i_prop] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(frame, o[i_obj], prop_names[i_prop]) == KOS_SUCCESS);
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
                        TEST(KOS_get_property(frame, o[i_obj], prop_names[i_prop]) == TO_SMALL_INT(expect));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(frame, o[i_obj], prop_names[i_prop]) == KOS_BADPTR);
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
        KOS_OBJ_ID obj_a = KOS_new_object(frame);
        KOS_OBJ_ID obj_b = KOS_new_object_with_prototype(frame, obj_a);
        KOS_OBJ_ID obj_c = KOS_new_object_with_prototype(frame, obj_b);
        KOS_OBJ_ID obj_d = KOS_new_object_with_prototype(frame, obj_c);

        static const char s1[] = "1";
        static const char s2[] = "2";
        static const char s3[] = "3";
        static const char s4[] = "4";
        static const char s5[] = "5";
        static const char s6[] = "6";

        const KOS_OBJ_ID str_1 = KOS_context_get_cstring(frame, s1);
        const KOS_OBJ_ID str_2 = KOS_context_get_cstring(frame, s2);
        const KOS_OBJ_ID str_3 = KOS_context_get_cstring(frame, s3);
        const KOS_OBJ_ID str_4 = KOS_context_get_cstring(frame, s4);
        const KOS_OBJ_ID str_5 = KOS_context_get_cstring(frame, s5);
        const KOS_OBJ_ID str_6 = KOS_context_get_cstring(frame, s6);

        TEST(!IS_BAD_PTR(obj_a));
        TEST(!IS_BAD_PTR(obj_b));
        TEST(!IS_BAD_PTR(obj_c));
        TEST(!IS_BAD_PTR(obj_d));

        TEST(KOS_set_property(frame, obj_a, str_1, TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_a, str_2, TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_c, str_2, TO_SMALL_INT(2)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_a, str_3, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_c, str_4, TO_SMALL_INT(4)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_a, str_5, TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_d, str_5, TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj_d, str_6, TO_SMALL_INT(6)) == KOS_SUCCESS);

        TEST(KOS_set_property(frame, obj_a, str_6, TO_SMALL_INT(300)) == KOS_SUCCESS);
        TEST(KOS_delete_property(frame, obj_a, str_6) == KOS_SUCCESS);

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { KOS_BADPTR, TO_SMALL_INT(1) },
                { KOS_BADPTR, TO_SMALL_INT(100) },
                { KOS_BADPTR, TO_SMALL_INT(3) },
                { KOS_BADPTR, TO_SMALL_INT(200) }
            };
            expected[0].key = str_1;
            expected[1].key = str_2;
            expected[2].key = str_3;
            expected[3].key = str_5;

            TEST(_walk_object(frame, obj_a, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
            TEST(_walk_object(frame, obj_b, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK      walk;
            KOS_OBJECT_WALK_ELEM elem;

            TEST(KOS_object_walk_init_shallow(frame, &walk, obj_b) == KOS_SUCCESS);

            elem = KOS_object_walk(frame, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { KOS_BADPTR, TO_SMALL_INT(2) },
                { KOS_BADPTR, TO_SMALL_INT(4) }
            };
            expected[0].key = str_2;
            expected[1].key = str_4;

            TEST(_walk_object(frame, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { KOS_BADPTR, TO_SMALL_INT(1) },
                { KOS_BADPTR, TO_SMALL_INT(2) },
                { KOS_BADPTR, TO_SMALL_INT(3) },
                { KOS_BADPTR, TO_SMALL_INT(4) },
                { KOS_BADPTR, TO_SMALL_INT(200) }
            };
            expected[0].key = str_1;
            expected[1].key = str_2;
            expected[2].key = str_3;
            expected[3].key = str_4;
            expected[4].key = str_5;

            TEST(_walk_object(frame, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { KOS_BADPTR, TO_SMALL_INT(5) },
                { KOS_BADPTR, TO_SMALL_INT(6) }
            };
            expected[0].key = str_5;
            expected[1].key = str_6;

            TEST(_walk_object(frame, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { KOS_BADPTR, TO_SMALL_INT(1) },
                { KOS_BADPTR, TO_SMALL_INT(2) },
                { KOS_BADPTR, TO_SMALL_INT(3) },
                { KOS_BADPTR, TO_SMALL_INT(4) },
                { KOS_BADPTR, TO_SMALL_INT(5) },
                { KOS_BADPTR, TO_SMALL_INT(6) }
            };
            expected[0].key = str_1;
            expected[1].key = str_2;
            expected[2].key = str_3;
            expected[3].key = str_4;
            expected[4].key = str_5;
            expected[5].key = str_6;

            TEST(_walk_object(frame, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }
    }

    /************************************************************************/
    {
        static const char abc[] = "abc";
        static const char cde[] = "cde";
        static const char efg[] = "efg";
        static const char ghi[] = "ghi";

        const KOS_OBJ_ID str_abc = KOS_context_get_cstring(frame, abc);
        const KOS_OBJ_ID str_cde = KOS_context_get_cstring(frame, cde);
        const KOS_OBJ_ID str_efg = KOS_context_get_cstring(frame, efg);
        const KOS_OBJ_ID str_ghi = KOS_context_get_cstring(frame, ghi);

        KOS_OBJ_ID obj = KOS_new_object(frame);
        TEST( ! IS_BAD_PTR(obj));

        TEST(KOS_set_property(frame, obj, str_abc, TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj, str_cde, TO_SMALL_INT(2)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj, str_efg, TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(frame, obj, str_ghi, TO_SMALL_INT(4)) == KOS_SUCCESS);

        TEST(_KOS_object_copy_prop_table(frame, obj) == KOS_SUCCESS);

        TEST(KOS_get_property(frame, obj, str_abc) == TO_SMALL_INT(1));
        TEST(KOS_get_property(frame, obj, str_cde) == TO_SMALL_INT(2));
        TEST(KOS_get_property(frame, obj, str_efg) == TO_SMALL_INT(3));
        TEST(KOS_get_property(frame, obj, str_ghi) == TO_SMALL_INT(4));
    }

    KOS_context_destroy(&ctx);

    return 0;
}

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

#include "../inc/kos_object.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(&ctx)); KOS_clear_exception(&ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(&ctx))

static int _walk_object(KOS_CONTEXT*               ctx,
                        KOS_OBJ_PTR                obj,
                        KOS_OBJECT_WALK_ELEM*      expected,
                        unsigned                   num_expected,
                        enum KOS_OBJECT_WALK_DEPTH deep)
{
    KOS_OBJECT_WALK      walk;
    KOS_OBJECT_WALK_ELEM elem;
    unsigned             count = 0;

    TEST(KOS_object_walk_init(ctx, &walk, obj, deep) == KOS_SUCCESS);

    for (;;) {
        unsigned i;

        elem = KOS_object_walk(ctx, &walk);
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

    TEST(KOS_context_init(&ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        KOS_ASCII_STRING(non_existent, "non existent");

        const KOS_OBJ_PTR o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));
        TEST(!IS_SMALL_INT(o));
        TEST(HAS_PROPERTIES(o));
        TEST(GET_OBJ_TYPE(o) == OBJ_OBJECT);

        /* Can delete non-existent property */
        TEST(KOS_delete_property(&ctx, o, TO_OBJPTR(&non_existent)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, TO_OBJPTR(&non_existent))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(aaa, "aaa");
        KOS_ASCII_STRING(bbb, "bbb");
        KOS_ASCII_STRING(ccc, "ccc");

        KOS_OBJECT_WALK      walk;
        KOS_OBJECT_WALK_ELEM elem;

        const KOS_OBJ_PTR o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));

        /* Set two properties */
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&aaa), TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&bbb), TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Retrieve both properties */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_SMALL_INT(100));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve both properties by walking */
        {
            TEST(KOS_object_walk_init_shallow(&ctx, &walk, o) == KOS_SUCCESS);

            elem = KOS_object_walk(&ctx, &walk);
            TEST(elem.key   == TO_OBJPTR(&aaa));
            TEST(elem.value == TO_SMALL_INT(100));

            elem = KOS_object_walk(&ctx, &walk);
            TEST(elem.key   == TO_OBJPTR(&bbb));
            TEST(elem.value == TO_SMALL_INT(200));

            elem = KOS_object_walk(&ctx, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));

            elem = KOS_object_walk(&ctx, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }

        /* Cannot retrieve non-existent property */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, TO_OBJPTR(&ccc))));
        TEST_EXCEPTION();

        /* Delete property */
        TEST(KOS_delete_property(&ctx, o, TO_OBJPTR(&aaa)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve a property after it has been deleted */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa))));
        TEST_EXCEPTION();

        /* Retrieve the remaining property */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_SMALL_INT(200));
        TEST_NO_EXCEPTION();

        /* Retrieve the remaining property by walking */
        {
            TEST(KOS_object_walk_init_shallow(&ctx, &walk, o) == KOS_SUCCESS);

            elem = KOS_object_walk(&ctx, &walk);
            TEST(elem.key   == TO_OBJPTR(&bbb));
            TEST(elem.value == TO_SMALL_INT(200));

            elem = KOS_object_walk(&ctx, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));

            elem = KOS_object_walk(&ctx, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(str, "string");

        const KOS_OBJ_PTR o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));

        /* Cannot set property when value is null pointer */
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&str), TO_OBJPTR(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of null pointer */
        TEST(KOS_set_property(&ctx, TO_OBJPTR(0), TO_OBJPTR(&str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a number */
        TEST(KOS_set_property(&ctx, TO_SMALL_INT(123), TO_OBJPTR(&str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a string */
        TEST(KOS_set_property(&ctx, TO_OBJPTR(&str), TO_OBJPTR(&str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a boolean */
        TEST(KOS_set_property(&ctx, KOS_TRUE, TO_OBJPTR(&str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property of a void */
        TEST(KOS_set_property(&ctx, KOS_VOID, TO_OBJPTR(&str), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));

        /* Cannot set property when property name is a null pointer */
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(0), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is an object */
        TEST(KOS_set_property(&ctx, o, o, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a number */
        TEST(KOS_set_property(&ctx, o, TO_SMALL_INT(1), TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a boolean */
        TEST(KOS_set_property(&ctx, o, KOS_FALSE, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        /* Cannot set property when property name is a void */
        TEST(KOS_set_property(&ctx, o, KOS_VOID, TO_SMALL_INT(0)) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(str, "string");

        const KOS_OBJ_PTR o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));

        /* Can set property if name and value are correct */
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&str), TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        /* Cannot retrieve property of a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, TO_OBJPTR(0), TO_OBJPTR(&str))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a null pointer */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, TO_OBJPTR(0))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a number */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, TO_SMALL_INT(10))));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a boolean */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, KOS_FALSE)));
        TEST_EXCEPTION();

        /* Cannot retrieve property when name is a void */
        TEST(IS_BAD_PTR(KOS_get_property(&ctx, o, KOS_VOID)));
        TEST_EXCEPTION();

        /* Can retrieve correct property */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&str)) == TO_SMALL_INT(3));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        #define NUM_PROPS 128
        KOS_OBJ_PTR o;
        KOS_OBJ_PTR prop_names[NUM_PROPS];
        int         have_prop[NUM_PROPS];
        int         i;

        for (i=0; i < NUM_PROPS; i++) {
            char str_num[16];
            snprintf(str_num, sizeof(str_num), "%d", i);
            prop_names[i] = KOS_new_cstring(&ctx, str_num);
            have_prop[i]  = 0;
        }

        o = KOS_new_object(&ctx);
        TEST(!IS_BAD_PTR(o));

        srand((unsigned)time(0));

        for (i=0; i < NUM_PROPS * 4; i++) {
            const int r   = rand();
            const int idx = r % NUM_PROPS;
            const int f   = (r / NUM_PROPS) % 3;

            switch (f) {

                case 0:
                    TEST(KOS_set_property(&ctx, o, prop_names[idx], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(&ctx, o, prop_names[idx]) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[idx] = 0;
                    break;

                case 2:
                    if (have_prop[idx]) {
                        TEST(KOS_get_property(&ctx, o, prop_names[idx]) == TO_SMALL_INT(have_prop[idx]));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(&ctx, o, prop_names[idx]) == TO_OBJPTR(0));
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
        KOS_OBJ_PTR base = KOS_new_object(&ctx);
        KOS_OBJ_PTR o    = KOS_new_object_with_prototype(&ctx, base);

        KOS_ASCII_STRING(aaa, "aaa");
        KOS_ASCII_STRING(bbb, "bbb");
        KOS_ASCII_STRING(ccc, "ccc");

        TEST(!IS_BAD_PTR(base));
        TEST(!IS_BAD_PTR(o));

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        /* Add properties to the prototype */
        TEST(KOS_set_property(&ctx, base, TO_OBJPTR(&aaa), TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, base, TO_OBJPTR(&bbb), TO_SMALL_INT(2)) == KOS_SUCCESS);

        /* Can retrieve properties from prototype */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();

        /* Cannot retrieve non-existent property */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&ccc)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        /* Set properties */
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&aaa), TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, o, TO_OBJPTR(&ccc), TO_SMALL_INT(4)) == KOS_SUCCESS);

        /* Check all properties */
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&ccc)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_SMALL_INT(3));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_SMALL_INT(2));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&ccc)) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete some properties */
        TEST(KOS_delete_property(&ctx, base, TO_OBJPTR(&bbb)) == KOS_SUCCESS);
        TEST(KOS_delete_property(&ctx, o,    TO_OBJPTR(&aaa)) == KOS_SUCCESS);

        /* Check all properties again */
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&bbb)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
        TEST(KOS_get_property(&ctx, base, TO_OBJPTR(&ccc)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&ccc)) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();

        /* Delete more properties */
        TEST(KOS_delete_property(&ctx, o, TO_OBJPTR(&aaa)) == KOS_SUCCESS);
        TEST(KOS_delete_property(&ctx, o, TO_OBJPTR(&bbb)) == KOS_SUCCESS);
        TEST(KOS_delete_property(&ctx, o, TO_OBJPTR(&ccc)) == KOS_SUCCESS);

        /* Check properties again */
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&aaa)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&bbb)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
        TEST(KOS_get_property(&ctx, o, TO_OBJPTR(&ccc)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        #define NUM_PROPS 128
        KOS_OBJ_PTR o[3];
        KOS_OBJ_PTR prop_names[NUM_PROPS];
        int         have_prop[3][NUM_PROPS];
        int         i;

        for (i=0; i < NUM_PROPS; i++) {
            char str_num[16];
            snprintf(str_num, sizeof(str_num), "%d", i);
            prop_names[i]   = KOS_new_cstring(&ctx, str_num);
            have_prop[0][i] = 0;
            have_prop[1][i] = 0;
            have_prop[2][i] = 0;
        }

        o[0] = KOS_new_object(&ctx);
        o[1] = KOS_new_object_with_prototype(&ctx, o[0]);
        o[2] = KOS_new_object_with_prototype(&ctx, o[1]);

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
                    TEST(KOS_set_property(&ctx, o[i_obj], prop_names[i_prop], TO_SMALL_INT(i+1)) == KOS_SUCCESS);
                    TEST_NO_EXCEPTION();
                    have_prop[i_obj][i_prop] = i+1;
                    break;

                case 1:
                    TEST(KOS_delete_property(&ctx, o[i_obj], prop_names[i_prop]) == KOS_SUCCESS);
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
                        TEST(KOS_get_property(&ctx, o[i_obj], prop_names[i_prop]) == TO_SMALL_INT(expect));
                        TEST_NO_EXCEPTION();
                    }
                    else {
                        TEST(KOS_get_property(&ctx, o[i_obj], prop_names[i_prop]) == TO_OBJPTR(0));
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
        KOS_OBJ_PTR obj_a = KOS_new_object(&ctx);
        KOS_OBJ_PTR obj_b = KOS_new_object_with_prototype(&ctx, obj_a);
        KOS_OBJ_PTR obj_c = KOS_new_object_with_prototype(&ctx, obj_b);
        KOS_OBJ_PTR obj_d = KOS_new_object_with_prototype(&ctx, obj_c);

        KOS_ASCII_STRING(str_1, "1");
        KOS_ASCII_STRING(str_2, "2");
        KOS_ASCII_STRING(str_3, "3");
        KOS_ASCII_STRING(str_4, "4");
        KOS_ASCII_STRING(str_5, "5");
        KOS_ASCII_STRING(str_6, "6");

        TEST(!IS_BAD_PTR(obj_a));
        TEST(!IS_BAD_PTR(obj_b));
        TEST(!IS_BAD_PTR(obj_c));
        TEST(!IS_BAD_PTR(obj_d));

        TEST(KOS_set_property(&ctx, obj_a, TO_OBJPTR(&str_1), TO_SMALL_INT(1)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_a, TO_OBJPTR(&str_2), TO_SMALL_INT(100)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_c, TO_OBJPTR(&str_2), TO_SMALL_INT(2)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_a, TO_OBJPTR(&str_3), TO_SMALL_INT(3)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_c, TO_OBJPTR(&str_4), TO_SMALL_INT(4)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_a, TO_OBJPTR(&str_5), TO_SMALL_INT(200)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_d, TO_OBJPTR(&str_5), TO_SMALL_INT(5)) == KOS_SUCCESS);
        TEST(KOS_set_property(&ctx, obj_d, TO_OBJPTR(&str_6), TO_SMALL_INT(6)) == KOS_SUCCESS);

        TEST(KOS_set_property(&ctx, obj_a, TO_OBJPTR(&str_6), TO_SMALL_INT(300)) == KOS_SUCCESS);
        TEST(KOS_delete_property(&ctx, obj_a, TO_OBJPTR(&str_6)) == KOS_SUCCESS);

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { TO_OBJPTR(0), TO_SMALL_INT(1) },
                { TO_OBJPTR(0), TO_SMALL_INT(100) },
                { TO_OBJPTR(0), TO_SMALL_INT(3) },
                { TO_OBJPTR(0), TO_SMALL_INT(200) }
            };
            expected[0].key = TO_OBJPTR(&str_1);
            expected[1].key = TO_OBJPTR(&str_2);
            expected[2].key = TO_OBJPTR(&str_3);
            expected[3].key = TO_OBJPTR(&str_5);

            TEST(_walk_object(&ctx, obj_a, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
            TEST(_walk_object(&ctx, obj_b, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK      walk;
            KOS_OBJECT_WALK_ELEM elem;

            TEST(KOS_object_walk_init_shallow(&ctx, &walk, obj_b) == KOS_SUCCESS);

            elem = KOS_object_walk(&ctx, &walk);
            TEST(IS_BAD_PTR(elem.key));
            TEST(IS_BAD_PTR(elem.value));
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { TO_OBJPTR(0), TO_SMALL_INT(2) },
                { TO_OBJPTR(0), TO_SMALL_INT(4) }
            };
            expected[0].key = TO_OBJPTR(&str_2);
            expected[1].key = TO_OBJPTR(&str_4);

            TEST(_walk_object(&ctx, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { TO_OBJPTR(0), TO_SMALL_INT(1) },
                { TO_OBJPTR(0), TO_SMALL_INT(2) },
                { TO_OBJPTR(0), TO_SMALL_INT(3) },
                { TO_OBJPTR(0), TO_SMALL_INT(4) },
                { TO_OBJPTR(0), TO_SMALL_INT(200) }
            };
            expected[0].key = TO_OBJPTR(&str_1);
            expected[1].key = TO_OBJPTR(&str_2);
            expected[2].key = TO_OBJPTR(&str_3);
            expected[3].key = TO_OBJPTR(&str_4);
            expected[4].key = TO_OBJPTR(&str_5);

            TEST(_walk_object(&ctx, obj_c, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { TO_OBJPTR(0), TO_SMALL_INT(5) },
                { TO_OBJPTR(0), TO_SMALL_INT(6) }
            };
            expected[0].key = TO_OBJPTR(&str_5);
            expected[1].key = TO_OBJPTR(&str_6);

            TEST(_walk_object(&ctx, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_SHALLOW) == KOS_SUCCESS);
        }

        {
            KOS_OBJECT_WALK_ELEM expected[] = {
                { TO_OBJPTR(0), TO_SMALL_INT(1) },
                { TO_OBJPTR(0), TO_SMALL_INT(2) },
                { TO_OBJPTR(0), TO_SMALL_INT(3) },
                { TO_OBJPTR(0), TO_SMALL_INT(4) },
                { TO_OBJPTR(0), TO_SMALL_INT(5) },
                { TO_OBJPTR(0), TO_SMALL_INT(6) }
            };
            expected[0].key = TO_OBJPTR(&str_1);
            expected[1].key = TO_OBJPTR(&str_2);
            expected[2].key = TO_OBJPTR(&str_3);
            expected[3].key = TO_OBJPTR(&str_4);
            expected[4].key = TO_OBJPTR(&str_5);
            expected[5].key = TO_OBJPTR(&str_6);

            TEST(_walk_object(&ctx, obj_d, expected, sizeof(expected)/sizeof(expected[0]), KOS_DEEP) == KOS_SUCCESS);
        }
    }

    KOS_context_destroy(&ctx);

    return 0;
}

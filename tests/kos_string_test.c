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

#include "../inc/kos_string.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../lang/kos_object_internal.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(&ctx)); KOS_clear_exception(&ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(&ctx))

int main(void)
{
    KOS_CONTEXT ctx;

    TEST(KOS_context_init(&ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(&ctx, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(&ctx, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, "\0", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, "\x01", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 1);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(&ctx, "\t\n\r 09AZaz~\x7F");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 12);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s,  0) ==   9);
        TEST(KOS_string_get_char_code(&ctx, s,  1) ==  10);
        TEST(KOS_string_get_char_code(&ctx, s,  2) ==  13);
        TEST(KOS_string_get_char_code(&ctx, s,  3) ==  32);
        TEST(KOS_string_get_char_code(&ctx, s,  4) ==  48);
        TEST(KOS_string_get_char_code(&ctx, s,  5) ==  57);
        TEST(KOS_string_get_char_code(&ctx, s,  6) ==  65);
        TEST(KOS_string_get_char_code(&ctx, s,  7) ==  90);
        TEST(KOS_string_get_char_code(&ctx, s,  8) ==  97);
        TEST(KOS_string_get_char_code(&ctx, s,  9) == 122);
        TEST(KOS_string_get_char_code(&ctx, s, 10) == 126);
        TEST(KOS_string_get_char_code(&ctx, s, 11) == 127);
    }

    /************************************************************************/
    {
        const char src[] = {
            '\x00',                            /* u0000 */
            '\x7F',                            /* u007F */
            '\xC0', '\x80',                    /* u0000 */
            '\xC1', '\xBF',                    /* u007F */
            '\xE0', '\x80', '\x80',            /* u0000 */
            '\xE0', '\x81', '\xBF',            /* u007F */
            '\xF0', '\x80', '\x80', '\x80',    /* u0000 */
            '\xF0', '\x80', '\x81', '\xBF'     /* u007F */
        };
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 8);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  4) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  5) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  7) == 0x007F);
    }

    /************************************************************************/
    {
        const char src[] = {
            '\x00',                          /* u0000 */
            '\x7F',                          /* u007F */
            '\xC0', '\x80',                  /* u0000 */
            '\xC1', '\xBF',                  /* u007F */
            '\xC2', '\x80',                  /* u0080 */
            '\xDF', '\xBF',                  /* u07FF */
            '\xE0', '\x80', '\x80',          /* u0000 */
            '\xE0', '\x81', '\xBF',          /* u007F */
            '\xE0', '\x82', '\x80',          /* u0080 */
            '\xE0', '\x9F', '\xBF',          /* u07FF */
            '\xE0', '\xA0', '\x80',          /* u0800 */
            '\xEF', '\xBF', '\xBF',          /* uFFFF */
            '\xF0', '\x80', '\x80', '\x80',  /* u0000 */
            '\xF0', '\x80', '\x81', '\xBF',  /* u007F */
            '\xF0', '\x8F', '\xBF', '\xBF'   /* uFFFF */
        };
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 15);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  4) == 0x0080);
        TEST(KOS_string_get_char_code(&ctx, s,  5) == 0x07FF);
        TEST(KOS_string_get_char_code(&ctx, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s,  7) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s,  8) == 0x0080);
        TEST(KOS_string_get_char_code(&ctx, s,  9) == 0x07FF);
        TEST(KOS_string_get_char_code(&ctx, s, 10) == 0x0800);
        TEST(KOS_string_get_char_code(&ctx, s, 11) == 0xFFFF);
        TEST(KOS_string_get_char_code(&ctx, s, 12) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s, 13) == 0x007F);
        TEST(KOS_string_get_char_code(&ctx, s, 14) == 0xFFFF);
    }

    /************************************************************************/
    {
        const char src[] = {
            '\x00',                          /* u000000 */
            '\x7F',                          /* u00007F */
            '\xC0', '\x80',                  /* u000000 */
            '\xC1', '\xBF',                  /* u00007F */
            '\xC2', '\x80',                  /* u000080 */
            '\xDF', '\xBF',                  /* u0007FF */
            '\xE0', '\x80', '\x80',          /* u000000 */
            '\xE0', '\x81', '\xBF',          /* u00007F */
            '\xE0', '\x82', '\x80',          /* u000080 */
            '\xE0', '\x9F', '\xBF',          /* u0007FF */
            '\xE0', '\xA0', '\x80',          /* u000800 */
            '\xEF', '\xBF', '\xBF',          /* u00FFFF */
            '\xF0', '\x80', '\x80', '\x80',  /* u000000 */
            '\xF0', '\x80', '\x81', '\xBF',  /* u00007F */
            '\xF0', '\x8F', '\xBF', '\xBF',  /* u00FFFF */
            '\xF4', '\x80', '\x80', '\x80',  /* u100000 */
            '\xF0', '\xBF', '\xBF', '\xBF',  /* u03FFFF */
            '\xF7', '\xBF', '\xBF', '\xBF'   /* u1FFFFF */
        };
        const KOS_OBJ_PTR s = KOS_new_string(&ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 18);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s,  0) == 0x000000);
        TEST(KOS_string_get_char_code(&ctx, s,  1) == 0x00007F);
        TEST(KOS_string_get_char_code(&ctx, s,  2) == 0x000000);
        TEST(KOS_string_get_char_code(&ctx, s,  3) == 0x00007F);
        TEST(KOS_string_get_char_code(&ctx, s,  4) == 0x000080);
        TEST(KOS_string_get_char_code(&ctx, s,  5) == 0x0007FF);
        TEST(KOS_string_get_char_code(&ctx, s,  6) == 0x000000);
        TEST(KOS_string_get_char_code(&ctx, s,  7) == 0x00007F);
        TEST(KOS_string_get_char_code(&ctx, s,  8) == 0x000080);
        TEST(KOS_string_get_char_code(&ctx, s,  9) == 0x0007FF);
        TEST(KOS_string_get_char_code(&ctx, s, 10) == 0x000800);
        TEST(KOS_string_get_char_code(&ctx, s, 11) == 0x00FFFF);
        TEST(KOS_string_get_char_code(&ctx, s, 12) == 0x000000);
        TEST(KOS_string_get_char_code(&ctx, s, 13) == 0x00007F);
        TEST(KOS_string_get_char_code(&ctx, s, 14) == 0x00FFFF);
        TEST(KOS_string_get_char_code(&ctx, s, 15) == 0x100000);
        TEST(KOS_string_get_char_code(&ctx, s, 16) == 0x03FFFF);
        TEST(KOS_string_get_char_code(&ctx, s, 17) == 0x1FFFFF);
    }

    /************************************************************************/
    {
        const char src[] = { '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(&ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xC0', '\x7F' };
        TEST(IS_BAD_PTR(KOS_new_string(&ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(&ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x3F' };
        TEST(IS_BAD_PTR(KOS_new_string(&ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xF8', '\x80', '\x80', '\x80', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(&ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(&ctx, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(&ctx, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(&ctx, "\x01~\x7F\x80\xFF");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 5);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x01);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x7E);
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 0x7F);
        TEST(KOS_string_get_char_code(&ctx, s, 3) == 0x80);
        TEST(KOS_string_get_char_code(&ctx, s, 4) == 0xFF);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_string(&ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, 0, 0, OBJ_STRING_8);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, 0, 0, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* OBJ_STRING_8 is just because of the implementation,
           it could be something else. */
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, 0, 0, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* OBJ_STRING_8 is just because of the implementation,
           it could be something else. */
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x00U, 0x7FU };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/2, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x0000, 0x0100, 0x1000, 0x7FFF, 0x8000, 0xFFFF };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/2, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 6);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x0100);
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 0x1000);
        TEST(KOS_string_get_char_code(&ctx, s, 3) == 0x7FFF);
        TEST(KOS_string_get_char_code(&ctx, s, 4) == 0x8000);
        TEST(KOS_string_get_char_code(&ctx, s, 5) == 0xFFFF);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00U, 0x7FU };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00000000U, 0x00010000U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU};

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 5);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x00000000U);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x00010000U);
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 0x7FFFFFFFU);
        TEST(KOS_string_get_char_code(&ctx, s, 3) == 0x80000000U);
        TEST(KOS_string_get_char_code(&ctx, s, 4) == 0xFFFFFFFFU);
    }

    /************************************************************************/
    {
        const char src[] = { '\x00', '\x40', '\x7F' };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src), OBJ_STRING_8);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0,   0) == 3);
        TEST(KOS_string_to_utf8(s, buf, 3) == 3);
        TEST(buf[0] == '\x00');
        TEST(buf[1] == '\x40');
        TEST(buf[2] == '\x7F');
        TEST(buf[3] == '\xFF');
    }

    /************************************************************************/
    {
        const char src[] = { '\x80', '\xFF' };
        char buf[5] = { '\xFF', '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src), OBJ_STRING_8);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0,   0) == 4);
        TEST(KOS_string_to_utf8(s, buf, 4) == 4);
        TEST(buf[0] == '\xC2');
        TEST(buf[1] == '\x80');
        TEST(buf[2] == '\xC3');
        TEST(buf[3] == '\xBF');
        TEST(buf[4] == '\xFF');
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x0000U, 0x007FU, 0x0080U, 0x07FFU, 0x0800U, 0xFFFFU };
        char buf[13] = { '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF',
                         '\xFF', '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/2, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0,    0) == 12);
        TEST(KOS_string_to_utf8(s, buf, 12) == 12);
        TEST(buf[ 0] == '\x00');
        TEST(buf[ 1] == '\x7F');
        TEST(buf[ 2] == '\xC2');
        TEST(buf[ 3] == '\x80');
        TEST(buf[ 4] == '\xDF');
        TEST(buf[ 5] == '\xBF');
        TEST(buf[ 6] == '\xE0');
        TEST(buf[ 7] == '\xA0');
        TEST(buf[ 8] == '\x80');
        TEST(buf[ 9] == '\xEF');
        TEST(buf[10] == '\xBF');
        TEST(buf[11] == '\xBF');
        TEST(buf[12] == '\xFF');
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x000000U, 0x00007FU, 0x000080U, 0x0007FFU,
                                 0x000800U, 0x00FFFFU, 0x010000U, 0x1FFFFFU };
        char buf[21] = { '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF',
                         '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF',
                         '\xFF', '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_const_string(&ctx, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0,    0) == 20);
        TEST(KOS_string_to_utf8(s, buf, 20) == 20);
        TEST(buf[ 0] == '\x00');
        TEST(buf[ 1] == '\x7F');
        TEST(buf[ 2] == '\xC2');
        TEST(buf[ 3] == '\x80');
        TEST(buf[ 4] == '\xDF');
        TEST(buf[ 5] == '\xBF');
        TEST(buf[ 6] == '\xE0');
        TEST(buf[ 7] == '\xA0');
        TEST(buf[ 8] == '\x80');
        TEST(buf[ 9] == '\xEF');
        TEST(buf[10] == '\xBF');
        TEST(buf[11] == '\xBF');
        TEST(buf[12] == '\xF0');
        TEST(buf[13] == '\x90');
        TEST(buf[14] == '\x80');
        TEST(buf[15] == '\x80');
        TEST(buf[16] == '\xF7');
        TEST(buf[17] == '\xBF');
        TEST(buf[18] == '\xBF');
        TEST(buf[19] == '\xBF');
        TEST(buf[20] == '\xFF');
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_string_add_many(&ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[1];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[3];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "");
        src[1] = KOS_new_const_ascii_cstring(&ctx, "");
        src[2] = KOS_new_const_ascii_cstring(&ctx, "");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[1];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_cstring(&ctx, "abc\xDF\xBF");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0]);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[1];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_cstring(&ctx, "this is a very long 32-bit string \xF7\xBF\xBF\xBF");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0]);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[3];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "one ");
        src[1] = KOS_new_const_ascii_cstring(&ctx, "two ");
        src[2] = KOS_new_const_ascii_cstring(&ctx, "three");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 13);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
        TEST(memcmp(_KOS_get_string_buffer(OBJPTR(KOS_STRING, s)), "one two three", 13) == 0);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[3];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "a");
        src[1] = KOS_new_cstring(&ctx, "\xDF\xBF");
        src[2] = KOS_new_const_ascii_cstring(&ctx, "b");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 3);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'a');
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 0x7FFU);
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 'b');
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[5];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "abcdefghijklmnopqrstuvwxyz");
        src[1] = KOS_new_cstring(&ctx, "\x01");
        src[2] = KOS_new_cstring(&ctx, "\xF0\x90\x80\x82");
        src[3] = KOS_new_const_ascii_cstring(&ctx, "");
        src[4] = KOS_new_cstring(&ctx, "\xE0\x80\x83");
        s      = KOS_string_add_many(&ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 29);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s,  0) == 'a');
        TEST(KOS_string_get_char_code(&ctx, s,  1) == 'b');
        TEST(KOS_string_get_char_code(&ctx, s, 25) == 'z');
        TEST(KOS_string_get_char_code(&ctx, s, 26) == 1);
        TEST(KOS_string_get_char_code(&ctx, s, 27) == 0x10002);
        TEST(KOS_string_get_char_code(&ctx, s, 28) == 3);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src[2];
        KOS_OBJ_PTR s;
        src[0] = KOS_new_const_ascii_cstring(&ctx, "abc");
        src[1] = KOS_new_const_ascii_cstring(&ctx, "def");
        s      = KOS_string_add(&ctx, src[0], src[1]);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 6);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'a');
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 'b');
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 'c');
        TEST(KOS_string_get_char_code(&ctx, s, 3) == 'd');
        TEST(KOS_string_get_char_code(&ctx, s, 4) == 'e');
        TEST(KOS_string_get_char_code(&ctx, s, 5) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_const_ascii_cstring(&ctx, "abcdef");
        s   = KOS_string_slice(&ctx, src, 1, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 4);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'b');
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 'c');
        TEST(KOS_string_get_char_code(&ctx, s, 2) == 'd');
        TEST(KOS_string_get_char_code(&ctx, s, 3) == 'e');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "abcdef\xC4\x80");
        s   = KOS_string_slice(&ctx, src, -3, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'e');
        TEST(KOS_string_get_char_code(&ctx, s, 1) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "\xF0\x90\x80\x80@#$");
        s   = KOS_string_slice(&ctx, src, -1000, 1000);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 4);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x10000U);
        TEST(KOS_string_get_char_code(&ctx, s, 1) == '@');
        TEST(KOS_string_get_char_code(&ctx, s, 2) == '#');
        TEST(KOS_string_get_char_code(&ctx, s, 3) == '$');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "\xF0\x90\x80\x81@#$");
        s   = KOS_string_slice(&ctx, src, 1000, -1000);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* OBJ_STRING_8 is just because of the implementation,
           it could be something else. */
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "abc\xC4\x81$de");
        s   = KOS_string_get_char(&ctx, src, -4);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x101);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "abcd");
        s   = KOS_string_get_char(&ctx, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'c');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(&ctx, "\xF0\x90\x80\x82@#$");
        s   = KOS_string_get_char(&ctx, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 0x10002U);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_const_ascii_cstring(&ctx, "xyz");

        s = KOS_string_get_char(&ctx, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'x');

        s = KOS_string_get_char(&ctx, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'z');

        TEST(IS_BAD_PTR(KOS_string_get_char(&ctx, src, 3)));
        TEST_EXCEPTION();

        s = KOS_string_get_char(&ctx, src, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'z');

        s = KOS_string_get_char(&ctx, src, -3);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(&ctx, s, 0) == 'x');

        TEST(IS_BAD_PTR(KOS_string_get_char(&ctx, src, -4)));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(&ctx, src,  0) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(&ctx, src,  2) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(&ctx, src,  3) == ~0U);
        TEST_EXCEPTION();
        TEST(KOS_string_get_char_code(&ctx, src, -1) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(&ctx, src, -3) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(&ctx, src, -4) == ~0U);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(&ctx, "");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(&ctx, "");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567xyz");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567A");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567a");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(&ctx, "0123456701234567a");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(&ctx, "\xF0\x80\x81\x81");
        KOS_OBJ_PTR s2 = KOS_new_cstring(&ctx, "A");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,0xFFFFU,4,4, 0x8001U, 2 };
        const uint16_t src2[] = { 4,0xFFFFU,4,4, 0x8001U, 1 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2, 0 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000000U, 10 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000000U, 10 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000001U, 101 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000001U, 100 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, ~0U, 66 };
        const uint32_t src2[] = { ~1U,~2U, ~0U, 66, 0 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 6, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 1, 2, 3, 4, 5 };
        const uint32_t src2[] = { 1, 2, 3 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 6, 2, 3, 4, 5 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint32_t src2[] = { 6, 2, 8, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3 };
        const uint32_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint16_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4, 7 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12, 14 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 100 };
        const uint32_t src2[] = { 10, 11, 12, 100 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(&ctx, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(&ctx, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(&ctx, "xyabc");
        KOS_OBJ_PTR s2 = KOS_new_cstring(&ctx, "xyąbc");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(&ctx, "xyąbc");
        KOS_OBJ_PTR s2 = KOS_new_cstring(&ctx, "xybbc");
        TEST(KOS_string_compare(s1, s2) != 0); /* No consistent ordering with wcscoll */
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(&ctx, "");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(&ctx, "abc");
        KOS_OBJ_PTR s3 = KOS_new_const_ascii_cstring(&ctx, "acb");
        KOS_OBJ_PTR s4 = KOS_new_const_ascii_cstring(&ctx, "abcd");
        KOS_OBJ_PTR s5 = KOS_new_const_ascii_cstring(&ctx, "abd");
        KOS_OBJ_PTR s6 = KOS_new_const_ascii_cstring(&ctx, "acd");
        KOS_OBJ_PTR s7 = KOS_new_const_ascii_cstring(&ctx, "cba");
        const uint32_t h1 = KOS_string_get_hash(s1);
        const uint32_t h2 = KOS_string_get_hash(s2);
        const uint32_t h3 = KOS_string_get_hash(s3);
        const uint32_t h4 = KOS_string_get_hash(s4);
        const uint32_t h5 = KOS_string_get_hash(s5);
        const uint32_t h6 = KOS_string_get_hash(s6);
        const uint32_t h7 = KOS_string_get_hash(s7);
        TEST(h1 != 0);
        TEST(h2 != 0);
        TEST(h3 != 0);
        TEST(h4 != 0);
        TEST(h5 != 0);
        TEST(h6 != 0);
        TEST(h7 != 0);
        TEST(h1 != h2);
        TEST(h1 != h3);
        TEST(h1 != h4);
        TEST(h1 != h5);
        TEST(h1 != h6);
        TEST(h1 != h7);
        TEST(h2 != h3);
        TEST(h2 != h4);
        TEST(h2 != h5);
        TEST(h2 != h6);
        TEST(h2 != h7);
        TEST(h3 != h4);
        TEST(h3 != h5);
        TEST(h3 != h6);
        TEST(h3 != h7);
        TEST(h4 != h5);
        TEST(h4 != h6);
        TEST(h4 != h7);
        TEST(h5 != h6);
        TEST(h5 != h7);
        TEST(h6 != h7);
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_string_add(&ctx, TO_OBJPTR(&str), TO_OBJPTR(0)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(&ctx, TO_SMALL_INT(1), TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(&ctx, TO_OBJPTR(&str), KOS_TRUE) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(&ctx, KOS_VOID, TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(&ctx, TO_OBJPTR(&str), KOS_new_array(&ctx, 8)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(&ctx, KOS_new_object(&ctx), TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        TEST(KOS_string_slice(&ctx, TO_OBJPTR(0), 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(&ctx, TO_SMALL_INT(1), 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(&ctx, KOS_FALSE, 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(&ctx, KOS_VOID, 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }


    /************************************************************************/
    {
        TEST(KOS_string_get_char(&ctx, TO_OBJPTR(0), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(&ctx, TO_SMALL_INT(2), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(&ctx, KOS_TRUE, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(&ctx, KOS_VOID, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        TEST(KOS_string_get_char_code(&ctx, TO_OBJPTR(0), 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(&ctx, TO_SMALL_INT(2), 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(&ctx, KOS_TRUE, 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(&ctx, KOS_VOID, 0) == ~0U);
        TEST_EXCEPTION();
    }

    KOS_context_destroy(&ctx);

    return 0;
}

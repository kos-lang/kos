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
#include "../lang/kos_memory.h"
#include "../lang/kos_object_internal.h"
#include "../lang/kos_utf8.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '0', '0' };
        uint32_t       max_code = 0;
        const unsigned len      = _KOS_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '\0', '\0', '}' };
        uint32_t       max_code = 0;
        const unsigned len      = _KOS_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '1', '0', '0', '0', '0', '0', '0', '}' };
        uint32_t       max_code = 0;
        const unsigned len      = _KOS_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(frame, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(frame, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(frame, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(frame, "\0", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_string(frame, "\x01", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 1);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_cstring(frame, "\t\n\r 09AZaz~\x7F");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 12);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s,  0) ==   9);
        TEST(KOS_string_get_char_code(frame, s,  1) ==  10);
        TEST(KOS_string_get_char_code(frame, s,  2) ==  13);
        TEST(KOS_string_get_char_code(frame, s,  3) ==  32);
        TEST(KOS_string_get_char_code(frame, s,  4) ==  48);
        TEST(KOS_string_get_char_code(frame, s,  5) ==  57);
        TEST(KOS_string_get_char_code(frame, s,  6) ==  65);
        TEST(KOS_string_get_char_code(frame, s,  7) ==  90);
        TEST(KOS_string_get_char_code(frame, s,  8) ==  97);
        TEST(KOS_string_get_char_code(frame, s,  9) == 122);
        TEST(KOS_string_get_char_code(frame, s, 10) == 126);
        TEST(KOS_string_get_char_code(frame, s, 11) == 127);
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
        const KOS_OBJ_PTR s = KOS_new_string(frame, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 8);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  4) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  5) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  7) == 0x007F);
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
        const KOS_OBJ_PTR s = KOS_new_string(frame, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 15);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  4) == 0x0080);
        TEST(KOS_string_get_char_code(frame, s,  5) == 0x07FF);
        TEST(KOS_string_get_char_code(frame, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s,  7) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s,  8) == 0x0080);
        TEST(KOS_string_get_char_code(frame, s,  9) == 0x07FF);
        TEST(KOS_string_get_char_code(frame, s, 10) == 0x0800);
        TEST(KOS_string_get_char_code(frame, s, 11) == 0xFFFF);
        TEST(KOS_string_get_char_code(frame, s, 12) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s, 13) == 0x007F);
        TEST(KOS_string_get_char_code(frame, s, 14) == 0xFFFF);
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
        const KOS_OBJ_PTR s = KOS_new_string(frame, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 18);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s,  0) == 0x000000);
        TEST(KOS_string_get_char_code(frame, s,  1) == 0x00007F);
        TEST(KOS_string_get_char_code(frame, s,  2) == 0x000000);
        TEST(KOS_string_get_char_code(frame, s,  3) == 0x00007F);
        TEST(KOS_string_get_char_code(frame, s,  4) == 0x000080);
        TEST(KOS_string_get_char_code(frame, s,  5) == 0x0007FF);
        TEST(KOS_string_get_char_code(frame, s,  6) == 0x000000);
        TEST(KOS_string_get_char_code(frame, s,  7) == 0x00007F);
        TEST(KOS_string_get_char_code(frame, s,  8) == 0x000080);
        TEST(KOS_string_get_char_code(frame, s,  9) == 0x0007FF);
        TEST(KOS_string_get_char_code(frame, s, 10) == 0x000800);
        TEST(KOS_string_get_char_code(frame, s, 11) == 0x00FFFF);
        TEST(KOS_string_get_char_code(frame, s, 12) == 0x000000);
        TEST(KOS_string_get_char_code(frame, s, 13) == 0x00007F);
        TEST(KOS_string_get_char_code(frame, s, 14) == 0x00FFFF);
        TEST(KOS_string_get_char_code(frame, s, 15) == 0x100000);
        TEST(KOS_string_get_char_code(frame, s, 16) == 0x03FFFF);
        TEST(KOS_string_get_char_code(frame, s, 17) == 0x1FFFFF);
    }

    /************************************************************************/
    {
        const char src[] = { '\xC0', '\x80', '\xC2', '\x80' };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_string(frame, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0,   0) == 3);
        TEST(KOS_string_to_utf8(s, buf, 3) == 3);
        TEST(buf[0] == '\x00');
        TEST(buf[1] == '\xC2');
        TEST(buf[2] == '\x80');
        TEST(buf[3] == '\xFF');
    }

    /************************************************************************/
    {
        const char src[] = { '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xC0', '\x7F' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x3F' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xF8', '\x80', '\x80', '\x80', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE8', '\x80', '\xC0' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xF1', '\x80', '\x80', '\xC0' };
        TEST(IS_BAD_PTR(KOS_new_string(frame, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(frame, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(frame, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_cstring(frame, "\x01~\x7F\x80\xFF");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 5);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x01);
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x7E);
        TEST(KOS_string_get_char_code(frame, s, 2) == 0x7F);
        TEST(KOS_string_get_char_code(frame, s, 3) == 0x80);
        TEST(KOS_string_get_char_code(frame, s, 4) == 0xFF);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_ascii_string(frame, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_string(frame, 0, 0, OBJ_STRING_8);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type   == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)      == 0);
        TEST(OBJPTR(KOS_STRING, s)->hash   == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_new_const_string(frame, 0, 0, OBJ_STRING_16);
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
        const KOS_OBJ_PTR s = KOS_new_const_string(frame, 0, 0, OBJ_STRING_32);
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

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/2, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x0000, 0x0100, 0x1000, 0x7FFF, 0x8000, 0xFFFF };

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/2, OBJ_STRING_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 6);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x0100);
        TEST(KOS_string_get_char_code(frame, s, 2) == 0x1000);
        TEST(KOS_string_get_char_code(frame, s, 3) == 0x7FFF);
        TEST(KOS_string_get_char_code(frame, s, 4) == 0x8000);
        TEST(KOS_string_get_char_code(frame, s, 5) == 0xFFFF);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00U, 0x7FU };

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00000000U, 0x00010000U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU};

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 5);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x00000000U);
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x00010000U);
        TEST(KOS_string_get_char_code(frame, s, 2) == 0x7FFFFFFFU);
        TEST(KOS_string_get_char_code(frame, s, 3) == 0x80000000U);
        TEST(KOS_string_get_char_code(frame, s, 4) == 0xFFFFFFFFU);
    }

    /************************************************************************/
    {
        const char src[] = { '\x00', '\x40', '\x7F' };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src), OBJ_STRING_8);
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

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src), OBJ_STRING_8);
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

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/2, OBJ_STRING_16);
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

        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/4, OBJ_STRING_32);
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
        const uint32_t src[] = { 0x00200000U };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };
        const KOS_OBJ_PTR s = KOS_new_const_string(frame, src, sizeof(src)/4, OBJ_STRING_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(KOS_string_to_utf8(s, 0, 0) == ~0U);
        TEST(KOS_string_to_utf8(s, buf, 4) == ~0U);
        TEST_NO_EXCEPTION();
        TEST(buf[0] == '\xFF');
        TEST(buf[1] == '\xFF');
        TEST(buf[2] == '\xFF');
        TEST(buf[3] == '\xFF');
    }

    /************************************************************************/
    {
        const KOS_OBJ_PTR s = KOS_string_add_many(frame, 0, 0);
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
        src[0] = KOS_new_const_ascii_cstring(frame, "");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
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
        src[0] = KOS_new_const_ascii_cstring(frame, "");
        src[1] = KOS_new_const_ascii_cstring(frame, "");
        src[2] = KOS_new_const_ascii_cstring(frame, "");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
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
        src[0] = KOS_new_cstring(frame, "abc\xDF\xBF");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0]);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[1];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_cstring(frame, "this is a very long 32-bit string \xF7\xBF\xBF\xBF");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0]);
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[3];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(frame, "one ");
        src[1] = KOS_new_const_ascii_cstring(frame, "two ");
        src[2] = KOS_new_const_ascii_cstring(frame, "three");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
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
        src[0] = KOS_new_const_ascii_cstring(frame, "a");
        src[1] = KOS_new_cstring(frame, "\xDF\xBF");
        src[2] = KOS_new_const_ascii_cstring(frame, "b");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 3);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'a');
        TEST(KOS_string_get_char_code(frame, s, 1) == 0x7FFU);
        TEST(KOS_string_get_char_code(frame, s, 2) == 'b');
    }

    /************************************************************************/
    {
        KOS_ATOMIC(KOS_OBJ_PTR) src[5];
        KOS_OBJ_PTR             s;
        src[0] = KOS_new_const_ascii_cstring(frame, "abcdefghijklmnopqrstuvwxyz");
        src[1] = KOS_new_cstring(frame, "\xC4\x80");
        src[2] = KOS_new_cstring(frame, "\xF0\x90\x80\x82");
        src[3] = KOS_new_const_ascii_cstring(frame, "");
        src[4] = KOS_new_cstring(frame, "\xE0\x80\x83");
        s      = KOS_string_add_many(frame, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 29);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s,  0) == 'a');
        TEST(KOS_string_get_char_code(frame, s,  1) == 'b');
        TEST(KOS_string_get_char_code(frame, s, 25) == 'z');
        TEST(KOS_string_get_char_code(frame, s, 26) == 0x100);
        TEST(KOS_string_get_char_code(frame, s, 27) == 0x10002);
        TEST(KOS_string_get_char_code(frame, s, 28) == 3);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src[2];
        KOS_OBJ_PTR s;
        src[0] = KOS_new_const_ascii_cstring(frame, "abc");
        src[1] = KOS_new_const_ascii_cstring(frame, "def");
        s      = KOS_string_add(frame, src[0], src[1]);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 6);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'a');
        TEST(KOS_string_get_char_code(frame, s, 1) == 'b');
        TEST(KOS_string_get_char_code(frame, s, 2) == 'c');
        TEST(KOS_string_get_char_code(frame, s, 3) == 'd');
        TEST(KOS_string_get_char_code(frame, s, 4) == 'e');
        TEST(KOS_string_get_char_code(frame, s, 5) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_const_ascii_cstring(frame, "abcdef");
        s   = KOS_string_slice(frame, src, 1, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 4);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'b');
        TEST(KOS_string_get_char_code(frame, s, 1) == 'c');
        TEST(KOS_string_get_char_code(frame, s, 2) == 'd');
        TEST(KOS_string_get_char_code(frame, s, 3) == 'e');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(frame, "abcdef\xC4\x80");
        s   = KOS_string_slice(frame, src, -3, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 2);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'e');
        TEST(KOS_string_get_char_code(frame, s, 1) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(frame, "\xF0\x90\x80\x80@#$");
        s   = KOS_string_slice(frame, src, -1000, 1000);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 4);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x10000U);
        TEST(KOS_string_get_char_code(frame, s, 1) == '@');
        TEST(KOS_string_get_char_code(frame, s, 2) == '#');
        TEST(KOS_string_get_char_code(frame, s, 3) == '$');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(frame, "\xF0\x90\x80\x81@#$");
        s   = KOS_string_slice(frame, src, 1000, -1000);
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
        src = KOS_new_cstring(frame, "a");
        s   = KOS_string_slice(frame, src, 0, 0);
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
        src = KOS_new_cstring(frame, "abc\xC4\x81$de");
        s   = KOS_string_get_char(frame, src, -4);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_16);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x101);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(frame, "abcd");
        s   = KOS_string_get_char(frame, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'c');
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_cstring(frame, "\xF0\x90\x80\x82@#$");
        s   = KOS_string_get_char(frame, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 0x10002U);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR src;
        KOS_OBJ_PTR s;
        src = KOS_new_const_ascii_cstring(frame, "xyz");

        s = KOS_string_get_char(frame, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'x');

        s = KOS_string_get_char(frame, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'z');

        TEST(IS_BAD_PTR(KOS_string_get_char(frame, src, 3)));
        TEST_EXCEPTION();

        s = KOS_string_get_char(frame, src, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'z');

        s = KOS_string_get_char(frame, src, -3);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(OBJPTR(KOS_STRING, s)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(s)    == 1);
        TEST(OBJPTR(KOS_STRING, s)->hash == 0);
        TEST(KOS_string_get_char_code(frame, s, 0) == 'x');

        TEST(IS_BAD_PTR(KOS_string_get_char(frame, src, -4)));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(frame, src,  0) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(frame, src,  2) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(frame, src,  3) == ~0U);
        TEST_EXCEPTION();
        TEST(KOS_string_get_char_code(frame, src, -1) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(frame, src, -3) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(frame, src, -4) == ~0U);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(frame, "");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(frame, "");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(frame, "0123456701234567xyz");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(frame, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(frame, "0123456701234567A");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(frame, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(frame, "0123456701234567a");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(frame, "0123456701234567a");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(frame, "\xF0\x80\x81\x81");
        KOS_OBJ_PTR s2 = KOS_new_cstring(frame, "A");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,0xFFFFU,4,4, 0x8001U, 2 };
        const uint16_t src2[] = { 4,0xFFFFU,4,4, 0x8001U, 1 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2, 0 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000000U, 10 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000000U, 10 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000001U, 101 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000001U, 100 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, ~0U, 66 };
        const uint32_t src2[] = { ~1U,~2U, ~0U, 66, 0 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 6, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 1, 2, 3, 4, 5 };
        const uint32_t src2[] = { 1, 2, 3 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 6, 2, 3, 4, 5 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_32);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint32_t src2[] = { 6, 2, 8, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3 };
        const uint32_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint16_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4, 7 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_16);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_8);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12, 14 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 100 };
        const uint32_t src2[] = { 10, 11, 12, 100 };
        KOS_OBJ_PTR s1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        KOS_OBJ_PTR s2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(frame, "xyabc");
        KOS_OBJ_PTR s2 = KOS_new_cstring(frame, "xyąbc");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(frame, "xyąbc");
        KOS_OBJ_PTR s2 = KOS_new_cstring(frame, "xybbc");
        TEST(KOS_string_compare(s1, s2) != 0); /* No consistent ordering with wcscoll */
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_cstring(frame, "000abcdefghijklmnopqrstuvwxyz");
        KOS_OBJ_PTR s2 = KOS_string_slice(frame, s1, 3, 28);
        KOS_OBJ_PTR s3 = KOS_string_slice(frame, s1, 3, 29);
        TEST(KOS_string_compare(s2, s3) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR s1 = KOS_new_const_ascii_cstring(frame, "");
        KOS_OBJ_PTR s2 = KOS_new_const_ascii_cstring(frame, "abc");
        KOS_OBJ_PTR s3 = KOS_new_const_ascii_cstring(frame, "acb");
        KOS_OBJ_PTR s4 = KOS_new_const_ascii_cstring(frame, "abcd");
        KOS_OBJ_PTR s5 = KOS_new_const_ascii_cstring(frame, "abd");
        KOS_OBJ_PTR s6 = KOS_new_const_ascii_cstring(frame, "acd");
        KOS_OBJ_PTR s7 = KOS_new_const_ascii_cstring(frame, "cba");
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
        uint8_t  src1[] = { 1, 100, 200 };
        uint16_t src2[] = { 1, 100, 200 };
        uint32_t src3[] = { 1, 100, 200 };

        KOS_OBJ_PTR str1;
        KOS_OBJ_PTR str2;
        KOS_OBJ_PTR str3;

        uint32_t hash1;
        uint32_t hash2;
        uint32_t hash3;

        str1 = KOS_new_const_string(frame, src1, sizeof(src1)/sizeof(src1[0]), OBJ_STRING_8);
        TEST( ! IS_BAD_PTR(str1));
        TEST_NO_EXCEPTION();

        str2 = KOS_new_const_string(frame, src2, sizeof(src2)/sizeof(src2[0]), OBJ_STRING_16);
        TEST( ! IS_BAD_PTR(str1));
        TEST_NO_EXCEPTION();

        str3 = KOS_new_const_string(frame, src3, sizeof(src3)/sizeof(src3[0]), OBJ_STRING_32);
        TEST( ! IS_BAD_PTR(str1));
        TEST_NO_EXCEPTION();

        hash1 = KOS_string_get_hash(str1);
        hash2 = KOS_string_get_hash(str2);
        hash3 = KOS_string_get_hash(str3);

        TEST(hash1 == hash2);
        TEST(hash1 == hash3);
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(str, "str");

        TEST(KOS_string_add(frame, TO_OBJPTR(&str), TO_OBJPTR(0)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(frame, TO_SMALL_INT(1), TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(frame, TO_OBJPTR(&str), KOS_TRUE) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(frame, KOS_VOID, TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(frame, TO_OBJPTR(&str), KOS_new_array(frame, 8)) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_add(frame, KOS_new_object(frame), TO_OBJPTR(&str)) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        TEST(KOS_string_slice(frame, TO_OBJPTR(0), 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(frame, TO_SMALL_INT(1), 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(frame, KOS_FALSE, 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_slice(frame, KOS_VOID, 0, 1) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(src, "");

        KOS_OBJ_PTR str = KOS_string_slice(frame, TO_OBJPTR(&src), 0, 1);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == 0);
    }

    /************************************************************************/
    {
        int i;

        const uint32_t src[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

        KOS_OBJ_PTR str = KOS_new_const_string(frame, src, sizeof(src)/sizeof(src[0]), OBJ_STRING_32);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_32);
        TEST(KOS_get_string_length(str) == 16);

        str = KOS_string_slice(frame, str, 1, -6);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_32);
        TEST(KOS_get_string_length(str) == 9);
        for (i = 0; i < 9; i++)
            TEST(KOS_string_get_char_code(frame, str, i) == (unsigned)i+2U);
    }

    /************************************************************************/
    {
        int i;

        KOS_OBJ_PTR str = KOS_new_cstring(frame, "\xF4\x80\x80\x80" "12345678");
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_32);
        TEST(KOS_get_string_length(str) == 9);

        str = KOS_string_slice(frame, str, -1000, 1000);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_32);
        TEST(KOS_get_string_length(str) == 9);
        TEST(KOS_string_get_char_code(frame, str, 0) == 0x100000U);
        for (i = 1; i < 9; i++)
            TEST(KOS_string_get_char_code(frame, str, i) == (unsigned)i+0x30U);
    }

    /************************************************************************/
    {
        TEST(KOS_string_get_char(frame, TO_OBJPTR(0), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(frame, TO_SMALL_INT(2), 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(frame, KOS_TRUE, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(frame, KOS_VOID, 0) == TO_OBJPTR(0));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        TEST(KOS_string_get_char_code(frame, TO_OBJPTR(0), 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(frame, TO_SMALL_INT(2), 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(frame, KOS_TRUE, 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(frame, KOS_VOID, 0) == ~0U);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        uint32_t           src_ok[]      = { 0x1FFFFFU };
        uint32_t           src_invalid[] = { 0x200000U };
        KOS_OBJ_PTR        str;
        struct _KOS_VECTOR vec;

        _KOS_vector_init(&vec);

        str = KOS_new_cstring(frame, "");
        TEST( ! IS_BAD_PTR(str));
        TEST(OBJPTR(KOS_STRING, str)->type == OBJ_STRING_8);
        TEST(KOS_get_string_length(str)    == 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(frame, TO_SMALL_INT(1), &vec) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(frame, str, &vec) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(vec.size == 1);
        TEST(vec.buffer[0] == 0);

        str = KOS_new_const_string(frame, src_invalid, sizeof(src_invalid)/sizeof(src_invalid[0]), OBJ_STRING_32);
        TEST( ! IS_BAD_PTR(str));
        TEST(OBJPTR(KOS_STRING, str)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(str)    == 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(frame, str, &vec) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        str = KOS_new_const_string(frame, src_ok, sizeof(src_ok)/sizeof(src_ok[0]), OBJ_STRING_32);
        TEST( ! IS_BAD_PTR(str));
        TEST(OBJPTR(KOS_STRING, str)->type == OBJ_STRING_32);
        TEST(KOS_get_string_length(str)    == 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(frame, str, &vec) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(vec.size == 5);
        TEST((uint8_t)vec.buffer[0] == 0xF7U);
        TEST((uint8_t)vec.buffer[1] == 0xBFU);
        TEST((uint8_t)vec.buffer[2] == 0xBFU);
        TEST((uint8_t)vec.buffer[3] == 0xBFU);
        TEST(vec.buffer[4] == 0);

        _KOS_vector_destroy(&vec);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str = KOS_object_to_string(frame, TO_SMALL_INT(1));
        TEST( ! IS_BAD_PTR(str));
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == 1);
        TEST(KOS_string_get_char_code(frame, str, 0) == 0x31);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str;
        const char *expected = "4611686018427387904";
        unsigned    size     = (unsigned)strlen(expected);
        unsigned    i;

        KOS_OBJ_PTR v = KOS_new_int(frame, ((int64_t)1) << 62);
        TEST( ! IS_BAD_PTR(v));
        TEST( ! IS_SMALL_INT(v));
        TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);

        str = KOS_object_to_string(frame, v);
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == size);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(frame, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str;
        const char *expected = "1.000000";
        unsigned    size     = (unsigned)strlen(expected);
        unsigned    i;

        KOS_OBJ_PTR v = KOS_new_float(frame, 1);
        TEST( ! IS_BAD_PTR(v));
        TEST( ! IS_SMALL_INT(v));
        TEST(GET_OBJ_TYPE(v) == OBJ_FLOAT);

        str = KOS_object_to_string(frame, v);
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == size);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(frame, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_ASCII_STRING(src, "abc");

        KOS_OBJ_PTR str = KOS_object_to_string(frame, TO_OBJPTR(&src));

        TEST(str == TO_OBJPTR(&src));
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str;
        const char *expected = "void";
        unsigned    size     = (unsigned)strlen(expected);
        unsigned    i;

        str = KOS_object_to_string(frame, KOS_VOID);
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == size);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(frame, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str;
        const char *expected = "true";
        unsigned    size     = (unsigned)strlen(expected);
        unsigned    i;

        str = KOS_object_to_string(frame, KOS_TRUE);
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == size);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(frame, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_PTR str;
        const char *expected = "false";
        unsigned    size     = (unsigned)strlen(expected);
        unsigned    i;

        str = KOS_object_to_string(frame, KOS_FALSE);
        TEST(IS_STRING_OBJ(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING_8);
        TEST(KOS_get_string_length(str) == size);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(frame, str, (int)i) == (unsigned)expected[i]);
    }

    KOS_context_destroy(&ctx);

    return 0;
}

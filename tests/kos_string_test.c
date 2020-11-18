/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_string.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_object.h"
#include "../inc/kos_utils.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_utf8.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

#ifdef CONFIG_STRING16
#define KOS_STRING_ELEM_MIN_8  KOS_STRING_ELEM_16
#define KOS_STRING_ELEM_MIN_16 KOS_STRING_ELEM_16
#elif defined(CONFIG_STRING32)
#define KOS_STRING_ELEM_MIN_8  KOS_STRING_ELEM_32
#define KOS_STRING_ELEM_MIN_16 KOS_STRING_ELEM_32
#else
#define KOS_STRING_ELEM_MIN_8  KOS_STRING_ELEM_8
#define KOS_STRING_ELEM_MIN_16 KOS_STRING_ELEM_16
#endif

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '0', '0' };
        uint32_t       max_code = 0;
        const unsigned len      = kos_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '\0', '\0', '}' };
        uint32_t       max_code = 0;
        const unsigned len      = kos_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const char     src[]    = { '\\', 'x', '{', '1', '0', '0', '0', '0', '0', '0', '}' };
        uint32_t       max_code = 0;
        const unsigned len      = kos_utf8_get_len(src, sizeof(src), KOS_UTF8_WITH_ESCAPE, &max_code);
        TEST(len == ~0U);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_cstring(ctx, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_cstring(ctx, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_string(ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_string(ctx, "\0", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_string(ctx, "\x01", 1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 1);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_string(ctx, 0, 0x100000);
        TEST(IS_BAD_PTR(s));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_cstring(ctx, "\t\n\r 09AZaz~\x7F");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 12);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s,  0) ==   9);
        TEST(KOS_string_get_char_code(ctx, s,  1) ==  10);
        TEST(KOS_string_get_char_code(ctx, s,  2) ==  13);
        TEST(KOS_string_get_char_code(ctx, s,  3) ==  32);
        TEST(KOS_string_get_char_code(ctx, s,  4) ==  48);
        TEST(KOS_string_get_char_code(ctx, s,  5) ==  57);
        TEST(KOS_string_get_char_code(ctx, s,  6) ==  65);
        TEST(KOS_string_get_char_code(ctx, s,  7) ==  90);
        TEST(KOS_string_get_char_code(ctx, s,  8) ==  97);
        TEST(KOS_string_get_char_code(ctx, s,  9) == 122);
        TEST(KOS_string_get_char_code(ctx, s, 10) == 126);
        TEST(KOS_string_get_char_code(ctx, s, 11) == 127);
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
        const KOS_OBJ_ID s = KOS_new_string(ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 8);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  4) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  5) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  7) == 0x007F);
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
        const KOS_OBJ_ID s = KOS_new_string(ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_16);
        TEST(KOS_get_string_length(s) == 15);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s,  0) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  1) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  2) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  3) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  4) == 0x0080);
        TEST(KOS_string_get_char_code(ctx, s,  5) == 0x07FF);
        TEST(KOS_string_get_char_code(ctx, s,  6) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s,  7) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s,  8) == 0x0080);
        TEST(KOS_string_get_char_code(ctx, s,  9) == 0x07FF);
        TEST(KOS_string_get_char_code(ctx, s, 10) == 0x0800);
        TEST(KOS_string_get_char_code(ctx, s, 11) == 0xFFFF);
        TEST(KOS_string_get_char_code(ctx, s, 12) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s, 13) == 0x007F);
        TEST(KOS_string_get_char_code(ctx, s, 14) == 0xFFFF);
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
        const KOS_OBJ_ID s = KOS_new_string(ctx, src, sizeof(src));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 18);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s,  0) == 0x000000);
        TEST(KOS_string_get_char_code(ctx, s,  1) == 0x00007F);
        TEST(KOS_string_get_char_code(ctx, s,  2) == 0x000000);
        TEST(KOS_string_get_char_code(ctx, s,  3) == 0x00007F);
        TEST(KOS_string_get_char_code(ctx, s,  4) == 0x000080);
        TEST(KOS_string_get_char_code(ctx, s,  5) == 0x0007FF);
        TEST(KOS_string_get_char_code(ctx, s,  6) == 0x000000);
        TEST(KOS_string_get_char_code(ctx, s,  7) == 0x00007F);
        TEST(KOS_string_get_char_code(ctx, s,  8) == 0x000080);
        TEST(KOS_string_get_char_code(ctx, s,  9) == 0x0007FF);
        TEST(KOS_string_get_char_code(ctx, s, 10) == 0x000800);
        TEST(KOS_string_get_char_code(ctx, s, 11) == 0x00FFFF);
        TEST(KOS_string_get_char_code(ctx, s, 12) == 0x000000);
        TEST(KOS_string_get_char_code(ctx, s, 13) == 0x00007F);
        TEST(KOS_string_get_char_code(ctx, s, 14) == 0x00FFFF);
        TEST(KOS_string_get_char_code(ctx, s, 15) == 0x100000);
        TEST(KOS_string_get_char_code(ctx, s, 16) == 0x03FFFF);
        TEST(KOS_string_get_char_code(ctx, s, 17) == 0x1FFFFF);
    }

    /************************************************************************/
    {
        const char src[] = { '\xC0', '\x80', '\xC2', '\x80' };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_ID s = KOS_new_string(ctx, src, sizeof(src));
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
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xC0', '\x7F' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE0', '\x3F' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xF8', '\x80', '\x80', '\x80', '\x80' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xE8', '\x80', '\xC0' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const char src[] = { '\xF1', '\x80', '\x80', '\xC0' };
        TEST(IS_BAD_PTR(KOS_new_string(ctx, src, sizeof(src))));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_ascii_cstring(ctx, "");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_ascii_cstring(ctx, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_ascii_cstring(ctx, "\x01~\x7F\x80\xFF");
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 5);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x01);
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x7E);
        TEST(KOS_string_get_char_code(ctx, s, 2) == 0x7F);
        TEST(KOS_string_get_char_code(ctx, s, 3) == 0x80);
        TEST(KOS_string_get_char_code(ctx, s, 4) == 0xFF);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_ascii_string(ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_string(ctx, 0, 0, KOS_STRING_ELEM_8);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_string(ctx, 0, 0, KOS_STRING_ELEM_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* KOS_STRING_ELEM_8 is just because of the implementation,
           it could be something else. */
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const KOS_OBJ_ID s = KOS_new_const_string(ctx, 0, 0, KOS_STRING_ELEM_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* KOS_STRING_ELEM_8 is just because of the implementation,
           it could be something else. */
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x00U, 0x7FU };

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/2, KOS_STRING_ELEM_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_16);
        TEST(KOS_get_string_length(s) == 2);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint16_t src[] = { 0x0000, 0x0100, 0x1000, 0x7FFF, 0x8000, 0xFFFF };

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/2, KOS_STRING_ELEM_16);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_16);
        TEST(KOS_get_string_length(s) == 6);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x0100);
        TEST(KOS_string_get_char_code(ctx, s, 2) == 0x1000);
        TEST(KOS_string_get_char_code(ctx, s, 3) == 0x7FFF);
        TEST(KOS_string_get_char_code(ctx, s, 4) == 0x8000);
        TEST(KOS_string_get_char_code(ctx, s, 5) == 0xFFFF);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00U, 0x7FU };

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/4, KOS_STRING_ELEM_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 2);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x0000);
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x007F);
    }

    /************************************************************************/
    {
        const uint32_t src[] = { 0x00000000U, 0x00010000U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU};

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/4, KOS_STRING_ELEM_32);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 5);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x00000000U);
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x00010000U);
        TEST(KOS_string_get_char_code(ctx, s, 2) == 0x7FFFFFFFU);
        TEST(KOS_string_get_char_code(ctx, s, 3) == 0x80000000U);
        TEST(KOS_string_get_char_code(ctx, s, 4) == 0xFFFFFFFFU);
    }

    /************************************************************************/
    {
        const char src[] = { '\x00', '\x40', '\x7F' };
        char buf[4] = { '\xFF', '\xFF', '\xFF', '\xFF' };

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src), KOS_STRING_ELEM_8);
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

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src), KOS_STRING_ELEM_8);
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

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/2, KOS_STRING_ELEM_16);
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

        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/4, KOS_STRING_ELEM_32);
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
        const KOS_OBJ_ID s = KOS_new_const_string(ctx, src, sizeof(src)/4, KOS_STRING_ELEM_32);
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
        const KOS_OBJ_ID s = KOS_string_add_n(ctx, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[1];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "");
        TEST(!IS_BAD_PTR(src[0].o));
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[3];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "");
        src[1].o = KOS_new_const_ascii_cstring(ctx, "");
        src[2].o = KOS_new_const_ascii_cstring(ctx, "");
        TEST(!IS_BAD_PTR(src[0].o));
        TEST(!IS_BAD_PTR(src[1].o));
        TEST(!IS_BAD_PTR(src[2].o));
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[1];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_cstring(ctx, "abc\xDF\xBF");
        s        = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0].o);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[1];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_cstring(ctx, "this is a very long 32-bit string \xF7\xBF\xBF\xBF");
        s        = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(s == src[0].o);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[3];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "one ");
        src[1].o = KOS_new_const_ascii_cstring(ctx, "two ");
        src[2].o = KOS_new_const_ascii_cstring(ctx, "three");
        TEST(!IS_BAD_PTR(src[0].o));
        TEST(!IS_BAD_PTR(src[1].o));
        TEST(!IS_BAD_PTR(src[2].o));
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 13);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
#if ! defined(CONFIG_STRING16) && ! defined(CONFIG_STRING32)
        TEST(memcmp(kos_get_string_buffer(OBJPTR(STRING, s)), "one two three", 13) == 0);
#endif
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[3];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "a");
        src[1].o = KOS_new_cstring(ctx, "\xDF\xBF");
        src[2].o = KOS_new_const_ascii_cstring(ctx, "b");
        TEST(!IS_BAD_PTR(src[0].o));
        TEST(!IS_BAD_PTR(src[1].o));
        TEST(!IS_BAD_PTR(src[2].o));
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_16);
        TEST(KOS_get_string_length(s) == 3);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'a');
        TEST(KOS_string_get_char_code(ctx, s, 1) == 0x7FFU);
        TEST(KOS_string_get_char_code(ctx, s, 2) == 'b');
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[5];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "abcdefghijklmnopqrstuvwxyz");
        src[1].o = KOS_new_cstring(ctx, "\xC4\x80");
        src[2].o = KOS_new_cstring(ctx, "\xF0\x90\x80\x82");
        src[3].o = KOS_new_const_ascii_cstring(ctx, "");
        src[4].o = KOS_new_cstring(ctx, "\xE0\x80\x83");
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 29);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s,  0) == 'a');
        TEST(KOS_string_get_char_code(ctx, s,  1) == 'b');
        TEST(KOS_string_get_char_code(ctx, s, 25) == 'z');
        TEST(KOS_string_get_char_code(ctx, s, 26) == 0x100);
        TEST(KOS_string_get_char_code(ctx, s, 27) == 0x10002);
        TEST(KOS_string_get_char_code(ctx, s, 28) == 3);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[5];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "");
        src[1].o = KOS_new_const_ascii_cstring(ctx, "");
        src[2].o = KOS_new_const_ascii_cstring(ctx, "x");
        src[3].o = KOS_new_const_ascii_cstring(ctx, "");
        src[4].o = KOS_new_const_ascii_cstring(ctx, "");
        s      = KOS_string_add_n(ctx, src, sizeof(src)/sizeof(src[0]));
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(GET_OBJ_TYPE(s) == OBJ_STRING);
        TEST(s == src[2].o);
    }

    /************************************************************************/
    {
        KOS_LOCAL  src[2];
        KOS_OBJ_ID s;
        src[0].o = KOS_new_const_ascii_cstring(ctx, "abc");
        src[1].o = KOS_new_const_ascii_cstring(ctx, "def");
        s      = KOS_string_add_n(ctx, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 6);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'a');
        TEST(KOS_string_get_char_code(ctx, s, 1) == 'b');
        TEST(KOS_string_get_char_code(ctx, s, 2) == 'c');
        TEST(KOS_string_get_char_code(ctx, s, 3) == 'd');
        TEST(KOS_string_get_char_code(ctx, s, 4) == 'e');
        TEST(KOS_string_get_char_code(ctx, s, 5) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_const_ascii_cstring(ctx, "abcdef");
        s   = KOS_string_slice(ctx, src, 1, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 4);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'b');
        TEST(KOS_string_get_char_code(ctx, s, 1) == 'c');
        TEST(KOS_string_get_char_code(ctx, s, 2) == 'd');
        TEST(KOS_string_get_char_code(ctx, s, 3) == 'e');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "abcdef\xC4\x80");
        s   = KOS_string_slice(ctx, src, -3, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_16);
        TEST(KOS_get_string_length(s) == 2);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'e');
        TEST(KOS_string_get_char_code(ctx, s, 1) == 'f');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "\xF0\x90\x80\x80@#$");
        s   = KOS_string_slice(ctx, src, -1000, 1000);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 4);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x10000U);
        TEST(KOS_string_get_char_code(ctx, s, 1) == '@');
        TEST(KOS_string_get_char_code(ctx, s, 2) == '#');
        TEST(KOS_string_get_char_code(ctx, s, 3) == '$');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "\xF0\x90\x80\x81@#$");
        s   = KOS_string_slice(ctx, src, 1000, -1000);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* KOS_STRING_ELEM_8 is just because of the implementation,
           it could be something else. */
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "a");
        s   = KOS_string_slice(ctx, src, 0, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        /* KOS_STRING_ELEM_8 is just because of the implementation,
           it could be something else. */
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 0);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "abc\xC4\x81$de");
        s   = KOS_string_get_char(ctx, src, -4);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_16);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x101);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "abcd");
        s   = KOS_string_get_char(ctx, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'c');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_cstring(ctx, "\xF0\x90\x80\x82@#$");
        s   = KOS_string_get_char(ctx, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 0x10002U);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID src;
        KOS_OBJ_ID s;
        src = KOS_new_const_ascii_cstring(ctx, "xyz");
        TEST(!IS_BAD_PTR(src));

        s = KOS_string_get_char(ctx, src, 0);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'x');

        s = KOS_string_get_char(ctx, src, 2);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'z');

        TEST(IS_BAD_PTR(KOS_string_get_char(ctx, src, 3)));
        TEST_EXCEPTION();

        s = KOS_string_get_char(ctx, src, -1);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'z');

        s = KOS_string_get_char(ctx, src, -3);
        TEST(!IS_BAD_PTR(s));
        TEST(!IS_SMALL_INT(s));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, s)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(s) == 1);
        TEST(OBJPTR(STRING, s)->header.hash == 0);
        TEST(KOS_string_get_char_code(ctx, s, 0) == 'x');

        TEST(IS_BAD_PTR(KOS_string_get_char(ctx, src, -4)));
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(ctx, src,  0) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(ctx, src,  2) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(ctx, src,  3) == ~0U);
        TEST_EXCEPTION();
        TEST(KOS_string_get_char_code(ctx, src, -1) == 'z');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(ctx, src, -3) == 'x');
        TEST_NO_EXCEPTION();
        TEST(KOS_string_get_char_code(ctx, src, -4) == ~0U);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_const_ascii_cstring(ctx, "");
        KOS_OBJ_ID s2 = KOS_new_const_ascii_cstring(ctx, "");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_const_ascii_cstring(ctx, "0123456701234567xyz");
        KOS_OBJ_ID s2 = KOS_new_const_ascii_cstring(ctx, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_const_ascii_cstring(ctx, "0123456701234567A");
        KOS_OBJ_ID s2 = KOS_new_const_ascii_cstring(ctx, "0123456701234567abcd");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_const_ascii_cstring(ctx, "0123456701234567a");
        KOS_OBJ_ID s2 = KOS_new_const_ascii_cstring(ctx, "0123456701234567a");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_cstring(ctx, "\xF0\x80\x81\x81");
        KOS_OBJ_ID s2 = KOS_new_cstring(ctx, "A");
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0x8000U, 1 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,0xFFFFU,4,4, 0x8001U, 2 };
        const uint16_t src2[] = { 4,0xFFFFU,4,4, 0x8001U, 1 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2 };
        const uint16_t src2[] = { 4,4,0xFFFFU,4, 0xFFFFU, 2, 0 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000000U, 10 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000000U, 10 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, 0x80000001U, 101 };
        const uint32_t src2[] = { ~1U,~2U, 0x80000001U, 100 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { ~1U,~2U, ~0U, 66 };
        const uint32_t src2[] = { ~1U,~2U, ~0U, 66, 0 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 1, 2, 3, 4, 5 };
        const uint16_t src2[] = { 1, 2, 3, 6, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 1, 2, 3, 4, 5 };
        const uint32_t src2[] = { 1, 2, 3 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint32_t src1[] = { 6, 2, 3, 4, 5 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_32);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_8);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint32_t src2[] = { 6, 2, 8, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3 };
        const uint32_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 6, 2, 3, 4, 5 };
        const uint16_t src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_8);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint16_t src1[] = { 6, 2, 3, 4, 7 };
        const uint8_t  src2[] = { 6, 2, 3, 4, 5 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_16);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_8);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) > 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 13 };
        const uint32_t src2[] = { 10, 11, 12, 14 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        const uint8_t  src1[] = { 10, 11, 12, 100 };
        const uint32_t src2[] = { 10, 11, 12, 100 };
        KOS_OBJ_ID s1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        KOS_OBJ_ID s2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_32);
        TEST(KOS_string_compare(s1, s2) == 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_cstring(ctx, "xyabc");
        KOS_OBJ_ID s2 = KOS_new_cstring(ctx, "xyąbc");
        TEST(KOS_string_compare(s1, s2) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_cstring(ctx, "xyąbc");
        KOS_OBJ_ID s2 = KOS_new_cstring(ctx, "xybbc");
        TEST(KOS_string_compare(s1, s2) != 0); /* No consistent ordering with wcscoll */
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_cstring(ctx, "000abcdefghijklmnopqrstuvwxyz");
        KOS_OBJ_ID s2 = KOS_string_slice(ctx, s1, 3, 28);
        KOS_OBJ_ID s3 = KOS_string_slice(ctx, s1, 3, 29);
        TEST(KOS_string_compare(s2, s3) < 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_cstring(ctx, "123_456");
        TEST(KOS_string_compare_slice(s1, -5, -8, s1, -2, -1) < 0);
        TEST(KOS_string_compare_slice(s1, -2, -1, s1, -5, -8) > 0);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID s1 = KOS_new_const_ascii_cstring(ctx, "");
        KOS_OBJ_ID s2 = KOS_new_const_ascii_cstring(ctx, "abc");
        KOS_OBJ_ID s3 = KOS_new_const_ascii_cstring(ctx, "acb");
        KOS_OBJ_ID s4 = KOS_new_const_ascii_cstring(ctx, "abcd");
        KOS_OBJ_ID s5 = KOS_new_const_ascii_cstring(ctx, "abd");
        KOS_OBJ_ID s6 = KOS_new_const_ascii_cstring(ctx, "acd");
        KOS_OBJ_ID s7 = KOS_new_const_ascii_cstring(ctx, "cba");
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

        KOS_OBJ_ID str1;
        KOS_OBJ_ID str2;
        KOS_OBJ_ID str3;

        uint32_t hash1;
        uint32_t hash2;
        uint32_t hash3;

        str1 = KOS_new_const_string(ctx, src1, sizeof(src1)/sizeof(src1[0]), KOS_STRING_ELEM_8);
        TEST( ! IS_BAD_PTR(str1));
        TEST_NO_EXCEPTION();

        str2 = KOS_new_const_string(ctx, src2, sizeof(src2)/sizeof(src2[0]), KOS_STRING_ELEM_16);
        TEST( ! IS_BAD_PTR(str1));
        TEST_NO_EXCEPTION();

        str3 = KOS_new_const_string(ctx, src3, sizeof(src3)/sizeof(src3[0]), KOS_STRING_ELEM_32);
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
        KOS_OBJ_ID empty = KOS_STR_EMPTY;

        TEST(!IS_BAD_PTR(empty));
        TEST(GET_OBJ_TYPE(empty) == OBJ_STRING);
        TEST(KOS_get_string_length(empty) == 0);
    }

    /************************************************************************/
    {
        static const char str[] = "str";
        KOS_LOCAL         a[2];

        KOS_OBJ_ID str_str = KOS_new_const_ascii_cstring(ctx, str);
        TEST(!IS_BAD_PTR(str_str));
        TEST(GET_OBJ_TYPE(str_str) == OBJ_STRING);

        a[0].o = TO_SMALL_INT(1);
        a[1].o = str_str;
        TEST(KOS_string_add_n(ctx, a, 2) == KOS_BADPTR);
        TEST_EXCEPTION();

        a[0].o = str_str;
        a[1].o = KOS_TRUE;
        TEST(KOS_string_add_n(ctx, a, 2) == KOS_BADPTR);
        TEST_EXCEPTION();

        a[0].o = KOS_VOID;
        a[1].o = str_str;
        TEST(KOS_string_add_n(ctx, a, 2) == KOS_BADPTR);
        TEST_EXCEPTION();

        a[0].o = str_str;
        a[1].o = KOS_new_array(ctx, 8);
        TEST(KOS_string_add_n(ctx, a, 2) == KOS_BADPTR);
        TEST_EXCEPTION();

        a[0].o = KOS_new_object(ctx);
        a[1].o = str_str;
        TEST(KOS_string_add_n(ctx, a, 2) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID array;
        KOS_OBJ_ID in_str;
        KOS_OBJ_ID str;

        TEST(KOS_string_add(ctx, KOS_VOID) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_string_add(ctx, KOS_STR_EMPTY) == KOS_BADPTR);
        TEST_EXCEPTION();

        array = KOS_new_array(ctx, 0);
        TEST( ! IS_BAD_PTR(array));
        TEST_NO_EXCEPTION();

        str = KOS_string_add(ctx, array);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 0);

        TEST(KOS_array_resize(ctx, array, 1) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_add(ctx, array) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_array_write(ctx, array, 0, KOS_STR_EMPTY) == KOS_SUCCESS);

        str = KOS_string_add(ctx, array);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 0);

        in_str = KOS_new_const_ascii_cstring(ctx, "test");
        TEST( ! IS_BAD_PTR(in_str));
        TEST(GET_OBJ_TYPE(in_str) == OBJ_STRING);
        TEST(KOS_array_write(ctx, array, 0, in_str) == KOS_SUCCESS);

        str = KOS_string_add(ctx, array);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 4);
        TEST(str == in_str);

        TEST(KOS_array_resize(ctx, array, 2) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_add(ctx, array) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        static const char *in_cstr[]       = { "this", "is", "A", "test" };
        static const char  expected_cstr[] = "thisisAtest";
        KOS_OBJ_ID         array;
        KOS_OBJ_ID         expected;
        KOS_OBJ_ID         str;
        int                i;

        array = KOS_new_array(ctx, sizeof(in_cstr) / sizeof(in_cstr[0]));
        TEST( ! IS_BAD_PTR(array));
        TEST_NO_EXCEPTION();

        for (i = 0; i < (int)(sizeof(in_cstr) / sizeof(in_cstr[0])); ++i) {
            KOS_OBJ_ID in_str = KOS_new_const_ascii_cstring(ctx, in_cstr[i]);
            TEST( ! IS_BAD_PTR(in_str));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_write(ctx, array, i, in_str) == KOS_SUCCESS);
        }

        expected = KOS_new_const_ascii_string(ctx, expected_cstr, sizeof(expected_cstr) - 1);
        TEST( ! IS_BAD_PTR(expected));
        TEST_NO_EXCEPTION();

        str = KOS_string_add(ctx, array);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == KOS_get_string_length(expected));
        TEST(KOS_string_compare(str, expected) == 0);
    }

    /************************************************************************/
    {
        static const char *in_cstr[]       = { "this", "i\xC3\x80s", "test" };
        static const char  expected_cstr[] = "thisi\xC3\x80stest";
        KOS_OBJ_ID         array;
        KOS_OBJ_ID         expected;
        KOS_OBJ_ID         str;
        int                i;

        array = KOS_new_array(ctx, sizeof(in_cstr) / sizeof(in_cstr[0]));
        TEST( ! IS_BAD_PTR(array));
        TEST_NO_EXCEPTION();

        for (i = 0; i < (int)(sizeof(in_cstr) / sizeof(in_cstr[0])); ++i) {
            KOS_OBJ_ID in_str = KOS_new_cstring(ctx, in_cstr[i]);
            TEST( ! IS_BAD_PTR(in_str));
            TEST_NO_EXCEPTION();

            TEST(KOS_array_write(ctx, array, i, in_str) == KOS_SUCCESS);
        }

        expected = KOS_new_cstring(ctx, expected_cstr);
        TEST( ! IS_BAD_PTR(expected));
        TEST_NO_EXCEPTION();

        str = KOS_string_add(ctx, array);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == KOS_get_string_length(expected));
        TEST(KOS_string_compare(str, expected) == 0);
    }

    /************************************************************************/
    {
        TEST(KOS_string_slice(ctx, TO_SMALL_INT(1), 0, 1) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_string_slice(ctx, KOS_FALSE, 0, 1) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_string_slice(ctx, KOS_VOID, 0, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_string_slice(ctx, KOS_STR_EMPTY, 0, 1);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 0);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_8);
    }

    /************************************************************************/
    {
        int i;

        const uint32_t src[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

        KOS_OBJ_ID str = KOS_new_const_string(ctx, src, sizeof(src)/sizeof(src[0]), KOS_STRING_ELEM_32);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 16);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);

        str = KOS_string_slice(ctx, str, 1, -6);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 9);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);
        for (i = 0; i < 9; i++)
            TEST(KOS_string_get_char_code(ctx, str, i) == (unsigned)i+2U);
    }

    /************************************************************************/
    {
        int i;

        KOS_OBJ_ID str = KOS_new_cstring(ctx, "\xF4\x80\x80\x80" "12345678");
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 9);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);

        str = KOS_string_slice(ctx, str, -1000, 1000);
        TEST( ! IS_BAD_PTR(str));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 9);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);
        TEST(KOS_string_get_char_code(ctx, str, 0) == 0x100000U);
        for (i = 1; i < 9; i++)
            TEST(KOS_string_get_char_code(ctx, str, i) == (unsigned)i+0x30U);
    }

    /************************************************************************/
    {
        TEST(KOS_string_get_char(ctx, TO_SMALL_INT(2), 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(ctx, KOS_TRUE, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char(ctx, KOS_VOID, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        TEST(KOS_string_get_char_code(ctx, TO_SMALL_INT(2), 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(ctx, KOS_TRUE, 0) == ~0U);
        TEST_EXCEPTION();

        TEST(KOS_string_get_char_code(ctx, KOS_VOID, 0) == ~0U);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        uint32_t   src_ok[]      = { 0x1FFFFFU };
        uint32_t   src_invalid[] = { 0x200000U };
        KOS_OBJ_ID str;
        KOS_VECTOR vec;

        KOS_vector_init(&vec);

        str = KOS_new_cstring(ctx, "");
        TEST( ! IS_BAD_PTR(str));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_8);
        TEST(KOS_get_string_length(str) == 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(ctx, TO_SMALL_INT(1), &vec) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(ctx, str, &vec) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(vec.size == 1);
        TEST(vec.buffer[0] == 0);

        str = KOS_new_const_string(ctx, src_invalid, sizeof(src_invalid)/sizeof(src_invalid[0]), KOS_STRING_ELEM_32);
        TEST( ! IS_BAD_PTR(str));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(str) == 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(ctx, str, &vec) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();

        str = KOS_new_const_string(ctx, src_ok, sizeof(src_ok)/sizeof(src_ok[0]), KOS_STRING_ELEM_32);
        TEST( ! IS_BAD_PTR(str));
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_32);
        TEST(KOS_get_string_length(str) == 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_string_to_cstr_vec(ctx, str, &vec) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(vec.size == 5);
        TEST((uint8_t)vec.buffer[0] == 0xF7U);
        TEST((uint8_t)vec.buffer[1] == 0xBFU);
        TEST((uint8_t)vec.buffer[2] == 0xBFU);
        TEST((uint8_t)vec.buffer[3] == 0xBFU);
        TEST(vec.buffer[4] == 0);

        KOS_vector_destroy(&vec);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_object_to_string(ctx, TO_SMALL_INT(1));
        TEST( ! IS_BAD_PTR(str));
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 1);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_MIN_8);
        TEST(KOS_string_get_char_code(ctx, str, 0) == 0x31);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID        str;
        static const char expected[] = "4611686018427387904";
        const unsigned    size       = sizeof(expected) - 1;
        unsigned          i;

        KOS_OBJ_ID v = KOS_new_int(ctx, ((int64_t)1) << 62);
        TEST( ! IS_BAD_PTR(v));
        TEST( ! IS_SMALL_INT(v));
        TEST(IS_NUMERIC_OBJ(v));
        TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);

        str = KOS_object_to_string(ctx, v);
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == size);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_MIN_8);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(ctx, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID        str;
        static const char expected[] = "1.0";
        const unsigned    size       = sizeof(expected) - 1;
        unsigned          i;

        KOS_OBJ_ID v = KOS_new_float(ctx, 1);
        TEST( ! IS_BAD_PTR(v));
        TEST( ! IS_SMALL_INT(v));
        TEST(IS_NUMERIC_OBJ(v));
        TEST(GET_OBJ_TYPE(v) == OBJ_FLOAT);

        str = KOS_object_to_string(ctx, v);
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == size);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_MIN_8);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(ctx, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        static const char src[] = "abc";

        KOS_OBJ_ID str;
        KOS_OBJ_ID str_src = KOS_new_const_ascii_cstring(ctx, src);

        TEST(!IS_BAD_PTR(str_src));

        str = KOS_object_to_string(ctx, str_src);

        TEST(str == str_src);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID        str;
        static const char expected[] = "void";
        const unsigned    size       = sizeof(expected) - 1;
        unsigned          i;

        str = KOS_object_to_string(ctx, KOS_VOID);
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == size);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_8);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(ctx, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID        str;
        static const char expected[] = "true";
        const unsigned    size       = sizeof(expected) - 1;
        unsigned          i;

        str = KOS_object_to_string(ctx, KOS_TRUE);
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == size);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_8);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(ctx, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID        str;
        static const char expected[] = "false";
        const unsigned    size       = sizeof(expected) - 1;
        unsigned          i;

        str = KOS_object_to_string(ctx, KOS_FALSE);
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == size);
        TEST(kos_get_string_elem_size(OBJPTR(STRING, str)) == KOS_STRING_ELEM_8);

        for (i = 0; i < size; i++)
            TEST(KOS_string_get_char_code(ctx, str, (int)i) == (unsigned)expected[i]);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str;
        KOS_VECTOR vec;

        KOS_vector_init(&vec);

        str = KOS_new_cstring(ctx, "");
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 0);

        TEST(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_QUOTE_STRINGS, &str, &vec) == KOS_SUCCESS);

        KOS_vector_destroy(&vec);

        TEST(GET_OBJ_TYPE(str)                     == OBJ_STRING);
        TEST(KOS_get_string_length(str)            == 2);
        TEST(KOS_string_get_char_code(ctx, str, 0) == 34);
        TEST(KOS_string_get_char_code(ctx, str, 1) == 34);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str;
        KOS_VECTOR vec;

        KOS_vector_init(&vec);

        str = KOS_new_cstring(ctx, "\\\"\n\x1f\x7f");
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 5);

        TEST(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_QUOTE_STRINGS, &str, &vec) == KOS_SUCCESS);

        KOS_vector_destroy(&vec);

        TEST(GET_OBJ_TYPE(str)                      == OBJ_STRING);
        TEST(KOS_get_string_length(str)             == 18);
        TEST(KOS_string_get_char_code(ctx, str, 0)  == (unsigned)'"');
        TEST(KOS_string_get_char_code(ctx, str, 1)  == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 2)  == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 3)  == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 4)  == (unsigned)'"');
        TEST(KOS_string_get_char_code(ctx, str, 5)  == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 6)  == (unsigned)'x');
        TEST(KOS_string_get_char_code(ctx, str, 7)  == (unsigned)'0');
        TEST(KOS_string_get_char_code(ctx, str, 8)  == (unsigned)'a');
        TEST(KOS_string_get_char_code(ctx, str, 9)  == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 10) == (unsigned)'x');
        TEST(KOS_string_get_char_code(ctx, str, 11) == (unsigned)'1');
        TEST(KOS_string_get_char_code(ctx, str, 12) == (unsigned)'f');
        TEST(KOS_string_get_char_code(ctx, str, 13) == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 14) == (unsigned)'x');
        TEST(KOS_string_get_char_code(ctx, str, 15) == (unsigned)'7');
        TEST(KOS_string_get_char_code(ctx, str, 16) == (unsigned)'f');
        TEST(KOS_string_get_char_code(ctx, str, 17) == (unsigned)'"');
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str;
        KOS_VECTOR vec;

        KOS_vector_init(&vec);

        TEST(KOS_vector_resize(&vec, 1) == KOS_SUCCESS);
        vec.buffer[0] = 0;

        str = KOS_new_cstring(ctx, "\t");
        TEST(GET_OBJ_TYPE(str)          == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 1);

        TEST(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_QUOTE_STRINGS, &str, &vec) == KOS_SUCCESS);

        TEST(vec.size      == 1);
        TEST(vec.buffer[0] == 0);

        KOS_vector_destroy(&vec);

        TEST(GET_OBJ_TYPE(str)                     == OBJ_STRING);
        TEST(KOS_get_string_length(str)            == 6);
        TEST(KOS_string_get_char_code(ctx, str, 0) == (unsigned)'"');
        TEST(KOS_string_get_char_code(ctx, str, 1) == (unsigned)'\\');
        TEST(KOS_string_get_char_code(ctx, str, 2) == (unsigned)'x');
        TEST(KOS_string_get_char_code(ctx, str, 3) == (unsigned)'0');
        TEST(KOS_string_get_char_code(ctx, str, 4) == (unsigned)'9');
        TEST(KOS_string_get_char_code(ctx, str, 5) == (unsigned)'"');
    }

    /************************************************************************/
    {
        int pos = 0;
        TEST(KOS_string_find(ctx, KOS_VOID, KOS_VOID, KOS_FIND_FORWARD, &pos) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(pos == 0);

        TEST(KOS_string_scan(ctx, KOS_VOID, KOS_VOID, KOS_FIND_FORWARD, KOS_SCAN_INCLUDE, &pos) == KOS_ERROR_EXCEPTION);
        TEST_EXCEPTION();
        TEST(pos == 0);

        TEST(KOS_string_reverse(ctx, KOS_VOID) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str0 = KOS_new_cstring(ctx, "");
        KOS_OBJ_ID str2 = KOS_new_cstring(ctx, "ab");
        KOS_OBJ_ID str;

        TEST(KOS_string_repeat(ctx, KOS_VOID, 0) == KOS_BADPTR);
        TEST_EXCEPTION();

        str = KOS_string_repeat(ctx, str0, 0);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 0);

        str = KOS_string_repeat(ctx, str0, 0x10000U);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 0);

        str = KOS_string_repeat(ctx, str2, 0);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 0);

        str = KOS_string_repeat(ctx, str2, 0);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 0);

        str = KOS_string_repeat(ctx, str2, 1);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 2);
        TEST(KOS_string_get_char_code(ctx, str, 0) == (unsigned)'a');
        TEST(KOS_string_get_char_code(ctx, str, 1) == (unsigned)'b');

        str = KOS_string_repeat(ctx, str2, 3);
        TEST(str != KOS_BADPTR);
        TEST_NO_EXCEPTION();
        TEST(KOS_get_string_length(str) == 6);
        TEST(KOS_string_get_char_code(ctx, str, 0) == (unsigned)'a');
        TEST(KOS_string_get_char_code(ctx, str, 1) == (unsigned)'b');
        TEST(KOS_string_get_char_code(ctx, str, 2) == (unsigned)'a');
        TEST(KOS_string_get_char_code(ctx, str, 3) == (unsigned)'b');
        TEST(KOS_string_get_char_code(ctx, str, 4) == (unsigned)'a');
        TEST(KOS_string_get_char_code(ctx, str, 5) == (unsigned)'b');

        str = KOS_string_repeat(ctx, str2, 0x8000U);
        TEST(str == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        uint32_t        code;
        KOS_STRING_ITER iter;
        KOS_DECLARE_STATIC_CONST_STRING(str8, "abc");

        kos_init_string_iter(&iter, KOS_CONST_ID(str8));

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 'a');
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 'b');
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 'c');
        kos_string_iter_advance(&iter);

        TEST(kos_is_string_iter_end(&iter));
    }

    /************************************************************************/
    {
        const char       str[] = { '1', '\xEF', '\xBF', '\xBF', '\xC3', '\x80' };
        uint32_t         code;
        KOS_STRING_ITER  iter;
        const KOS_OBJ_ID str_str = KOS_new_string(ctx, str, sizeof(str));

        kos_init_string_iter(&iter, str_str);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == '1');
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 0xFFFFU);
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 0xC0U);
        kos_string_iter_advance(&iter);

        TEST(kos_is_string_iter_end(&iter));
    }

    /************************************************************************/
    {
        const char       str[] = { '1', '\xF0', '\x90', '\x80', '\x80', '0' };
        uint32_t         code;
        KOS_STRING_ITER  iter;
        const KOS_OBJ_ID str_str = KOS_new_string(ctx, str, sizeof(str));

        kos_init_string_iter(&iter, str_str);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == '1');
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == 0x10000U);
        kos_string_iter_advance(&iter);

        TEST( ! kos_is_string_iter_end(&iter));
        code = kos_string_iter_peek_next_code(&iter);
        TEST(code == '0');
        kos_string_iter_advance(&iter);

        TEST(kos_is_string_iter_end(&iter));
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_new_buffer(ctx, 1);

        TEST( ! IS_BAD_PTR(str));

        str = KOS_new_string_from_buffer(ctx, str, 0, 100);

        TEST(IS_BAD_PTR(str));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_new_buffer(ctx, 1);
        uint8_t   *data;

        TEST( ! IS_BAD_PTR(str));

        data = KOS_buffer_data_volatile(ctx, str);
        TEST(data);
        TEST_NO_EXCEPTION();
        data[0] = 0x80U;

        str = KOS_new_string_from_buffer(ctx, str, 0, 1);

        TEST(IS_BAD_PTR(str));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_new_buffer(ctx, 0x10000);

        TEST( ! IS_BAD_PTR(str));

        str = KOS_new_string_from_buffer(ctx, str, 0, 0x10000);

        TEST(IS_BAD_PTR(str));
        TEST_EXCEPTION();
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_new_buffer(ctx, 2);
        uint8_t   *data;

        TEST( ! IS_BAD_PTR(str));

        data = KOS_buffer_data_volatile(ctx, str);
        TEST(data);
        TEST_NO_EXCEPTION();
        data[0] = 0xC4U;
        data[1] = 0x80U;

        str = KOS_new_string_from_buffer(ctx, str, 0, 2);

        TEST( ! IS_BAD_PTR(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 1);
        TEST(KOS_string_get_char_code(ctx, str, 0) == 0x100U);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID str = KOS_new_buffer(ctx, 4);
        uint8_t   *data;

        TEST( ! IS_BAD_PTR(str));

        data = KOS_buffer_data_volatile(ctx, str);
        TEST(data);
        TEST_NO_EXCEPTION();
        data[0] = 0xF0U;
        data[1] = 0x90U;
        data[2] = 0x80U;
        data[3] = 0x80U;

        str = KOS_new_string_from_buffer(ctx, str, 0, 4);

        TEST( ! IS_BAD_PTR(str));
        TEST(GET_OBJ_TYPE(str) == OBJ_STRING);
        TEST(KOS_get_string_length(str) == 1);
        TEST(KOS_string_get_char_code(ctx, str, 0) == 0x10000U);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

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

#include "../core/kos_heap.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../core/kos_config.h"
#include "../core/kos_misc.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

static void _print_stats(struct _KOS_GC_STATS *stats)
{
    printf("num_objs_evacuated %u\n", stats->num_objs_evacuated);
    printf("num_objs_freed     %u\n", stats->num_objs_freed);
    printf("num_objs_finalized %u\n", stats->num_objs_finalized);
    printf("num_pages_kept     %u\n", stats->num_pages_kept);
    printf("num_pages_freed    %u\n", stats->num_pages_freed);
    printf("size_evacuated     %u\n", stats->size_evacuated);
    printf("size_freed         %u\n", stats->size_freed);
    printf("size_kept          %u\n", stats->size_kept);
}

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;

    /************************************************************************/
    {
        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(frame, 0) == KOS_SUCCESS);

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    {
        struct _KOS_GC_STATS stats[2];
        KOS_OBJ_REF          frame_ref;

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        frame_ref.obj_id = OBJID(STACK_FRAME, frame);
        KOS_track_ref(frame, &frame_ref);

        TEST(KOS_collect_garbage(frame, &stats[0]) == KOS_SUCCESS);
        frame = OBJPTR(STACK_FRAME, frame_ref.obj_id);

        TEST(KOS_collect_garbage(frame, &stats[1]) == KOS_SUCCESS);
        frame = OBJPTR(STACK_FRAME, frame_ref.obj_id);

#define TEST_STATS(i, j, x) \
        if (stats[i].x != stats[j].x) {     \
            _print_stats(&stats[i]);        \
            printf("\n");                   \
            _print_stats(&stats[j]);        \
            TEST(stats[i].x == stats[j].x); \
        }
        TEST_STATS(0, 1, num_objs_evacuated);
        TEST_STATS(0, 1, num_objs_freed);
        TEST_STATS(0, 1, num_objs_finalized);
        TEST_STATS(0, 1, num_pages_kept);
        TEST_STATS(0, 1, num_pages_freed);
        TEST_STATS(0, 1, size_evacuated);
        TEST_STATS(0, 1, size_freed);
        TEST_STATS(0, 1, size_kept);
#undef TEST_STATS

        TEST(stats[0].num_objs_evacuated >  0);
        TEST(stats[0].num_objs_freed     == 0);
        TEST(stats[0].num_objs_finalized == 0);
        TEST(stats[0].num_pages_kept     == 0);
        TEST(stats[0].num_pages_freed    == 1);
        TEST(stats[0].size_evacuated     >  0);
        TEST(stats[0].size_freed         == 0);
        TEST(stats[0].size_kept          == 0);

        {
            static const char long_utf8_string[] =
                "very long UTF-8 string \xF1\x80\x80\x81";

            KOS_OBJ_ID array_id;
            KOS_OBJ_ID cont_id;
            KOS_OBJ_ID prop_id;
            KOS_OBJ_ID obj_id;

            cont_id = KOS_new_object(frame);
            TEST( ! IS_BAD_PTR(cont_id));

            prop_id = KOS_new_cstring(frame, "int");
            TEST( ! IS_BAD_PTR(prop_id));

            obj_id = KOS_new_int(frame, (int64_t)1 << 63);
            TEST( ! IS_SMALL_INT(obj_id));
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(frame, cont_id, prop_id, obj_id) == KOS_SUCCESS);

            prop_id = KOS_new_cstring(frame, "float");
            TEST( ! IS_BAD_PTR(prop_id));

            obj_id = KOS_new_float(frame, 1.5);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(frame, cont_id, prop_id, obj_id) == KOS_SUCCESS);

            prop_id = KOS_new_cstring(frame, "array");
            TEST( ! IS_BAD_PTR(prop_id));

            array_id = KOS_new_array(frame, 10);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(frame, cont_id, prop_id, array_id) == KOS_SUCCESS);

            TEST(KOS_array_write(frame, array_id, 0, KOS_TRUE) == KOS_SUCCESS);
            TEST(KOS_array_write(frame, array_id, 1, KOS_FALSE) == KOS_SUCCESS);
            TEST(KOS_array_write(frame, array_id, 2, KOS_VOID) == KOS_SUCCESS);

            obj_id = KOS_new_string(frame, long_utf8_string, sizeof(long_utf8_string) - 1);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(frame, array_id, 3, obj_id) == KOS_SUCCESS);

            obj_id = KOS_string_slice(frame, obj_id, 1, -1);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(frame, array_id, 4, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_buffer(frame, 256);
            TEST( ! IS_BAD_PTR(obj_id));
            memset(KOS_buffer_data(obj_id), 0x7F, 256);
            TEST(KOS_array_write(frame, array_id, 5, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_object_walk(frame, cont_id, KOS_DEEP);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(frame, array_id, 6, obj_id) == KOS_SUCCESS);
        }

        TEST(KOS_collect_garbage(frame, &stats[1]) == KOS_SUCCESS);
        frame = OBJPTR(STACK_FRAME, frame_ref.obj_id);

        TEST(stats[1].num_objs_evacuated == stats[0].num_objs_evacuated);
        TEST(stats[1].num_objs_freed     == 18);
        TEST(stats[1].num_objs_finalized == 0);
        TEST(stats[1].num_pages_kept     == 0);
        TEST(stats[1].num_pages_freed    == 1);
        TEST(stats[1].size_evacuated     == stats[0].size_evacuated);
        TEST(stats[1].size_freed         >  0);
        TEST(stats[1].size_kept          == 0);

        KOS_context_destroy(&ctx);
    }

    return 0;
}

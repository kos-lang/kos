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
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../core/kos_config.h"
#include "../core/kos_math.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

static void _finalize_1(KOS_CONTEXT ctx, void *priv)
{
    *(int *)priv = 1;
}

static KOS_OBJ_ID _handler(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    return args_obj;
}

static void _fill_buffer(KOS_OBJ_ID buf, int value)
{
    uint8_t *const data = KOS_buffer_data(buf);
    const uint32_t size = KOS_get_buffer_size(buf);

    memset(data, value, size);
}

static int _test_buffer(KOS_OBJ_ID buf, int value, uint32_t size)
{
    uint8_t *data;
    uint32_t actual_size;
    uint32_t i;

    TEST( ! IS_BAD_PTR(buf));
    TEST(GET_OBJ_TYPE(buf) == OBJ_BUFFER);

    data        = KOS_buffer_data(buf);
    actual_size = KOS_get_buffer_size(buf);

    TEST(actual_size == size);

    for (i = 0; i < size; ++i) {
        if ((int)data[i] != value) {
            printf("Invalid data at offset %d, expected 0x%02x but have 0x%02x\n",
                   i, (unsigned)value, (unsigned)data[i]);
            return 1;
        }
    }

    return 0;
}

static int64_t _get_obj_size(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_HEADER *hdr = (KOS_OBJ_HEADER *)((intptr_t)obj_id - 1);
    return GET_SMALL_INT(hdr->alloc_size);
}

static int _test_object(KOS_CONTEXT           ctx,
                        KOS_OBJ_ID            obj_id,
                        unsigned              num_objs,
                        int64_t               total_size,
                        unsigned              num_dead_objs,
                        int64_t               dead_size,
                        struct _KOS_GC_STATS *orig_stats)
{
    struct _KOS_GC_STATS stats;
    int64_t              size;

    TEST( ! IS_BAD_PTR(obj_id));

    size = _get_obj_size(obj_id);

    TEST(KOS_push_local(ctx, &obj_id) == KOS_SUCCESS);

    ctx->retval = KOS_BADPTR;

    TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

    KOS_pop_local(ctx, &obj_id);

    TEST(_get_obj_size(obj_id) == size);

    TEST(stats.num_objs_evacuated == orig_stats->num_objs_evacuated + num_objs);
    TEST(stats.num_objs_freed     == num_dead_objs);
    TEST(stats.num_objs_finalized == 0);
    TEST(stats.num_pages_kept     == 0);
    TEST(stats.num_pages_freed    == 1);
    TEST(stats.size_evacuated     == orig_stats->size_evacuated + (unsigned)total_size);
    TEST(stats.size_freed         == (unsigned)dead_size);
    TEST(stats.size_kept          == 0);

    TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

    TEST(stats.num_objs_evacuated == orig_stats->num_objs_evacuated);
    TEST(stats.num_objs_freed     == num_objs);
    TEST(stats.num_objs_finalized == 0);
    TEST(stats.num_pages_kept     == 0);
    TEST(stats.num_pages_freed    == 1);
    TEST(stats.size_evacuated     == orig_stats->size_evacuated);
    TEST(stats.size_freed         == (unsigned)total_size);
    TEST(stats.size_kept          == 0);

    return 0;
}

int main(void)
{
    KOS_INSTANCE         inst;
    KOS_CONTEXT          ctx;
    struct _KOS_GC_STATS base_stats;

    /************************************************************************/
    /* Test garbage collection on a freshly initialized instance */
    {
        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test garbage collection for various object types.  For each object type:
     * - allocate object
     * - run garbage collector while the object is referenced from stack
     * - remove stack reference
     * - run garbage collector while there are no references to the object.
     */
    {
        KOS_OBJ_ID obj_id[3];

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, 1) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, &base_stats) == KOS_SUCCESS);

        TEST(base_stats.num_objs_evacuated >  0);
        TEST(base_stats.num_objs_freed     == 0);
        TEST(base_stats.num_objs_finalized == 0);
        TEST(base_stats.num_pages_kept     == 0);
        TEST(base_stats.num_pages_freed    == 1);
        TEST(base_stats.size_evacuated     >  0);
        TEST(base_stats.size_freed         == 0);
        TEST(base_stats.size_kept          == 0);

        /* KOS_new_int */

        obj_id[0] = KOS_new_int(ctx, (int64_t)1 << 63);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_float */

        obj_id[0] = KOS_new_float(ctx, 2.0);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_cstring */

        obj_id[0] = KOS_new_cstring(ctx, "test string");

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_string_slice */

        obj_id[1] = KOS_new_cstring(ctx, "abcdefghijklmnopqrstuvwxyz");
        TEST( ! IS_BAD_PTR(obj_id[1]));
        obj_id[0] = KOS_string_slice(ctx, obj_id[1], 1, -1);

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_array (empty) */

        obj_id[0] = KOS_new_array(ctx, 0);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_buffer (empty) */

        obj_id[0] = KOS_new_buffer(ctx, 0);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_object (empty) */

        obj_id[0] = KOS_new_object(ctx);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_array */

        obj_id[0] = KOS_new_array(ctx, 1);
        TEST( ! IS_BAD_PTR(obj_id[0]));
        obj_id[1] = KOS_atomic_read_obj(OBJPTR(ARRAY, obj_id[0])->data);
        TEST( ! IS_BAD_PTR(obj_id[1]));

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_buffer */

        obj_id[0] = KOS_new_buffer(ctx, 1);
        TEST( ! IS_BAD_PTR(obj_id[0]));
        obj_id[1] = KOS_atomic_read_obj(OBJPTR(BUFFER, obj_id[0])->data);
        TEST( ! IS_BAD_PTR(obj_id[1]));

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_object */

        obj_id[0] = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id[0]));
        TEST(KOS_set_property(ctx, obj_id[0], KOS_new_cstring(ctx, ""), KOS_TRUE) == KOS_SUCCESS);
        obj_id[1] = KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id[0])->props);
        TEST( ! IS_BAD_PTR(obj_id[1]));

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_object with prototype */

        obj_id[1] = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id[1]));
        obj_id[0] = KOS_new_object_with_prototype(ctx, obj_id[1]);

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_object_walk (no properties) */

        obj_id[1] = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id[1]));
        obj_id[0] = KOS_new_object_walk(ctx, obj_id[1], KOS_SHALLOW);

        TEST(_test_object(ctx,
                          obj_id[0],
                          2,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]),
                          1, /* dead aux object created inside KOS_new_object_walk */
                          _get_obj_size(obj_id[1]),
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_object_walk (no properties) */

        obj_id[1] = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id[1]));
        TEST(KOS_set_property(ctx, obj_id[1], KOS_new_cstring(ctx, ""), KOS_TRUE) == KOS_SUCCESS);
        obj_id[2] = KOS_atomic_read_obj(OBJPTR(OBJECT, obj_id[1])->props);
        TEST( ! IS_BAD_PTR(obj_id[2]));
        obj_id[0] = KOS_new_object_walk(ctx, obj_id[1], KOS_SHALLOW);

        TEST(_test_object(ctx,
                          obj_id[0],
                          4,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]) +
                              /* little cheat: KOS_OBJECT_WALK also holds a KOS_OBJECT_STORAGE
                               * object, but we assume it's the same size as the one in the
                               * object being walked. */
                              _get_obj_size(obj_id[2]) + _get_obj_size(obj_id[2]),
                          1, /* dead aux object created inside KOS_new_object_walk */
                          _get_obj_size(obj_id[1]),
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_builtin_function */

        obj_id[0] = KOS_new_builtin_function(ctx, _handler, 0);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_dynamic_prop */

        obj_id[0] = KOS_new_dynamic_prop(ctx);

        TEST(_test_object(ctx,
                          obj_id[0],
                          1,
                          _get_obj_size(obj_id[0]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        /* KOS_new_builtin_dynamic_property */

        obj_id[0] = KOS_new_builtin_dynamic_prop(ctx, KOS_BADPTR, _handler, _handler);
        TEST( ! IS_BAD_PTR(obj_id[0]));
        obj_id[1] = OBJPTR(DYNAMIC_PROP, obj_id[0])->getter;
        TEST( ! IS_BAD_PTR(obj_id[1]));
        obj_id[2] = OBJPTR(DYNAMIC_PROP, obj_id[0])->setter;
        TEST( ! IS_BAD_PTR(obj_id[2]));

        TEST(_test_object(ctx,
                          obj_id[0],
                          3,
                          _get_obj_size(obj_id[0]) + _get_obj_size(obj_id[1]) +
                              _get_obj_size(obj_id[2]),
                          0,
                          0,
                          &base_stats) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Run garbage collector twice, ensure that statistics are identical
     * for both runs.
     */
    {
        struct _KOS_GC_STATS stats;

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, &base_stats) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(base_stats.num_objs_evacuated == stats.num_objs_evacuated);
        TEST(base_stats.num_objs_freed == stats.num_objs_freed);
        TEST(base_stats.num_objs_finalized == stats.num_objs_finalized);
        TEST(base_stats.num_pages_kept == stats.num_pages_kept);
        TEST(base_stats.num_pages_freed == stats.num_pages_freed);
        TEST(base_stats.size_evacuated == stats.size_evacuated);
        TEST(base_stats.size_freed == stats.size_freed);
        TEST(base_stats.size_kept == stats.size_kept);

        /* Allocate various types of objects, but don't keep any references
         * to them.  All of these objects must be released by the garbage
         * collector.
         */
        {
            static const char long_utf8_string[] =
                "very long UTF-8 string \xF1\x80\x80\x81";

            KOS_OBJ_ID array_id;
            KOS_OBJ_ID cont_id;
            KOS_OBJ_ID prop_id;
            KOS_OBJ_ID obj_id;

            cont_id = KOS_new_object(ctx);
            TEST( ! IS_BAD_PTR(cont_id));

            prop_id = KOS_new_cstring(ctx, "int");
            TEST( ! IS_BAD_PTR(prop_id));

            obj_id = KOS_new_int(ctx, (int64_t)1 << 63);
            TEST( ! IS_SMALL_INT(obj_id));
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(ctx, cont_id, prop_id, obj_id) == KOS_SUCCESS);

            prop_id = KOS_new_cstring(ctx, "float");
            TEST( ! IS_BAD_PTR(prop_id));

            obj_id = KOS_new_float(ctx, 1.5);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(ctx, cont_id, prop_id, obj_id) == KOS_SUCCESS);

            prop_id = KOS_new_cstring(ctx, "array");
            TEST( ! IS_BAD_PTR(prop_id));

            array_id = KOS_new_array(ctx, 10);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_set_property(ctx, cont_id, prop_id, array_id) == KOS_SUCCESS);

            TEST(KOS_array_write(ctx, array_id, 0, KOS_TRUE) == KOS_SUCCESS);
            TEST(KOS_array_write(ctx, array_id, 1, KOS_FALSE) == KOS_SUCCESS);
            TEST(KOS_array_write(ctx, array_id, 2, KOS_VOID) == KOS_SUCCESS);

            obj_id = KOS_new_string(ctx, long_utf8_string, sizeof(long_utf8_string) - 1);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(ctx, array_id, 3, obj_id) == KOS_SUCCESS);

            obj_id = KOS_string_slice(ctx, obj_id, 1, -1);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(ctx, array_id, 4, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_buffer(ctx, 256);
            TEST( ! IS_BAD_PTR(obj_id));
            memset(KOS_buffer_data(obj_id), 0x7F, 256);
            TEST(KOS_array_write(ctx, array_id, 5, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_object_walk(ctx, cont_id, KOS_DEEP);
            TEST( ! IS_BAD_PTR(obj_id));
            TEST(KOS_array_write(ctx, array_id, 6, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_function(ctx);
            TEST( ! IS_BAD_PTR(obj_id));

            TEST(_KOS_stack_push(ctx, obj_id) == KOS_SUCCESS);
            _KOS_stack_pop(ctx);
            ctx->stack = KOS_BADPTR;
        }

        ctx->retval = KOS_BADPTR;

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 20);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 0);
        TEST(stats.num_pages_freed    == 2);
        TEST(stats.size_evacuated     == base_stats.size_evacuated);
        TEST(stats.size_freed         >  0);
        TEST(stats.size_kept          == 0);

        /* Allocate an object with finalize function.
         * Ensure garbage collector runs the finalize function.
         */
        {
            KOS_OBJ_ID proto_id = KOS_new_object(ctx);
            KOS_OBJ_ID obj_id   = KOS_new_object_with_prototype(ctx, proto_id);
            KOS_OBJ_ID prop_id;
            int        fin      = 0;

            KOS_object_set_private(*OBJPTR(OBJECT, proto_id), &fin);
            OBJPTR(OBJECT, proto_id)->finalize = _finalize_1;

            /* Object references itself */
            prop_id = KOS_new_cstring(ctx, "self");
            TEST( ! IS_BAD_PTR(prop_id));
            TEST(KOS_set_property(ctx, obj_id, prop_id, obj_id) == KOS_SUCCESS);

            ctx->retval = KOS_BADPTR;

            TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

            /* Ensure finalize was run */
            TEST(fin == 1);
        }

        /* The following objects must have been destroyed:
         * - The prototype object.
         * - The main object.
         * - The main object's property table storage.
         * - The string "self".
         */
        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 4);
        TEST(stats.num_objs_finalized == 1);
        TEST(stats.num_pages_kept     == 0);
        TEST(stats.num_pages_freed    == 1);
        TEST(stats.size_evacuated     == base_stats.size_evacuated);
        TEST(stats.size_freed         >  0);
        TEST(stats.size_kept          == 0);

        /* Allocate several types of object and make sure they reference
         * other objects.  First run garbage collector while keeping references
         * to these objects, then run again without any references to ensure
         * the objects get destroyed.
         */
        {
            KOS_OBJ_ID array_id = KOS_BADPTR;
            KOS_OBJ_ID obj_id;

            TEST(KOS_push_local_scope(ctx, 1) == KOS_SUCCESS);

            array_id = KOS_new_array(ctx, 3);
            TEST( ! IS_BAD_PTR(array_id));

            TEST(KOS_push_local(ctx, &array_id) == KOS_SUCCESS);

            obj_id = KOS_new_builtin_function(ctx, _handler, 0);
            TEST( ! IS_BAD_PTR(obj_id));

            TEST(_KOS_stack_push(ctx, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_builtin_class(ctx, _handler, 0);
            TEST( ! IS_BAD_PTR(obj_id));

            TEST(KOS_array_write(ctx, array_id, 0, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_buffer(ctx, 128);
            TEST( ! IS_BAD_PTR(obj_id));

            TEST(KOS_array_write(ctx, array_id, 1, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_object_walk(ctx, obj_id, KOS_SHALLOW);
            TEST( ! IS_BAD_PTR(obj_id));

            TEST(KOS_array_write(ctx, array_id, 2, obj_id) == KOS_SUCCESS);

            obj_id = KOS_new_cstring(ctx, "0123456789012345678901234567890123456789");
            TEST( ! IS_BAD_PTR(obj_id));

            ctx->retval = obj_id;

            obj_id = KOS_string_slice(ctx, obj_id, 1, -1);
            TEST( ! IS_BAD_PTR(obj_id));

            ctx->exception = obj_id;

            TEST(KOS_collect_garbage(ctx, &stats) == KOS_ERROR_EXCEPTION);

            /* The following objects have been evacuated:
             * - 1 for local scope object
             * - 2 for array
             * - 1 for function
             * - 6 for class:
             *      -- 1 class
             *      -- 1 class property buffer
             *      -- 1 prototype object
             *      -- 1 dynamic "prototype" property
             *      -- 1 set function
             *      -- 1 get function
             * - 2 for buffer
             * - 1 for empty walk
             * - 1 for stack
             * - 1 for string in retval
             * - 1 for string as exception
             */
            TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated + 15);
            TEST(stats.num_objs_freed     == 1);
            TEST(stats.num_objs_finalized == 0);
            TEST(stats.num_pages_kept     == 1);
            TEST(stats.num_pages_freed    == 1);
            TEST(stats.size_evacuated     >= base_stats.size_evacuated);
            TEST(stats.size_freed         >  0);
            TEST(stats.size_kept          >  0);

            KOS_pop_local_scope(ctx);

            TEST(KOS_get_array_size(array_id) == 3);

            _KOS_stack_pop(ctx);
            ctx->stack = KOS_BADPTR;

            ctx->retval    = KOS_BADPTR;
            ctx->exception = KOS_BADPTR;

            TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

            TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
            TEST(stats.num_objs_freed     == 16);
            TEST(stats.num_objs_finalized == 0);
            TEST(stats.num_pages_kept     == 0);
            TEST(stats.num_pages_freed    == 2);
            TEST(stats.size_evacuated     >= base_stats.size_evacuated);
            TEST(stats.size_freed         >  0);
            TEST(stats.size_kept          == 0);
        }

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test release of current thread page */
    {
        struct _KOS_GC_STATS stats;

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        TEST(KOS_new_array(ctx, 0) != KOS_BADPTR);

        _KOS_heap_release_thread_page(ctx);

        TEST(KOS_new_array(ctx, 0) != KOS_BADPTR);

        ctx->retval = KOS_BADPTR;

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 2);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 0);
        TEST(stats.num_pages_freed    == 1);
        TEST(stats.size_evacuated     == base_stats.size_evacuated);
        TEST(stats.size_freed         >  0);
        TEST(stats.size_kept          == 0);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test garbage collector with two big buffer objects. */
    {
        struct _KOS_GC_STATS stats;
        KOS_OBJ_ID           obj_id[2] = { KOS_BADPTR, KOS_BADPTR };

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, 2) == KOS_SUCCESS);

        TEST(KOS_push_local(ctx, &obj_id[0]) == KOS_SUCCESS);
        TEST(KOS_push_local(ctx, &obj_id[1]) == KOS_SUCCESS);

        obj_id[0] = KOS_new_buffer(ctx, _KOS_POOL_SIZE / 2U);
        obj_id[1] = KOS_new_buffer(ctx, _KOS_POOL_SIZE / 2U);

        TEST( ! IS_BAD_PTR(obj_id[0]));
        TEST( ! IS_BAD_PTR(obj_id[1]));

        _fill_buffer(obj_id[0], 0x0A);
        _fill_buffer(obj_id[1], 0x0B);

        _KOS_heap_release_thread_page(ctx);

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated + 3);
        TEST(stats.num_objs_freed     == 0);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 2);
        TEST(stats.num_pages_freed    == 1);
        TEST(stats.size_evacuated     >= base_stats.size_evacuated);
        TEST(stats.size_freed         == 0);
        TEST(stats.size_kept          >  0);

        TEST(_test_buffer(obj_id[0], 0x0A, _KOS_POOL_SIZE / 2U) == KOS_SUCCESS);
        TEST(_test_buffer(obj_id[1], 0x0B, _KOS_POOL_SIZE / 2U) == KOS_SUCCESS);

        KOS_pop_local_scope(ctx);

        ctx->retval = KOS_BADPTR;

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 5);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 0);
        TEST(stats.num_pages_freed    >= _KOS_POOL_SIZE / _KOS_PAGE_SIZE);
        TEST(stats.size_evacuated     == base_stats.size_evacuated);
        TEST(stats.size_freed         >  0);
        TEST(stats.size_kept          == 0);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Allocate multiple huge objects which exceed page size to test how
     * page management and coalescing works.
     */
    {
        const uint32_t sizeof_buf    = sizeof(KOS_BUFFER);
        const uint32_t sizeof_buf_st = sizeof(KOS_BUFFER_STORAGE) - 1U;
        const uint32_t obj_align     = 1U << _KOS_OBJ_ALIGN_BITS;
        const uint32_t hdr_size      = KOS_align_up(sizeof_buf, obj_align) +
                                       KOS_align_up(sizeof_buf_st, obj_align);
        const uint32_t page_buf_cap  = KOS_align_up((uint32_t)(_KOS_PAGE_SIZE - hdr_size),
                                                    KOS_BUFFER_CAPACITY_ALIGN);
        const int      min_over_size = -2 * KOS_BUFFER_CAPACITY_ALIGN;
        const int      max_over_size = 2 * KOS_BUFFER_CAPACITY_ALIGN;
        const int      max_num_pages = 2;
        int            num_pages     = 1;

        for ( ; num_pages <= max_num_pages; ++num_pages) {

            int       size     = (int)page_buf_cap + (num_pages - 1) * _KOS_PAGE_SIZE + min_over_size;
            const int max_size = (int)page_buf_cap + (num_pages - 1) * _KOS_PAGE_SIZE + max_over_size;

            for ( ; size <= max_size; size += KOS_BUFFER_CAPACITY_ALIGN) {

                struct _KOS_GC_STATS stats;
                KOS_OBJ_ID           obj_ids[_KOS_POOL_SIZE / _KOS_PAGE_SIZE];
                unsigned             i;
                const unsigned       num_objs = (unsigned)(sizeof(obj_ids) / sizeof(obj_ids[0]));

                TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

                TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

                TEST(KOS_push_local_scope(ctx, num_objs) == KOS_SUCCESS);

                for (i = 0; i < num_objs; ++i) {
                    TEST(KOS_push_local(ctx, &obj_ids[i]) == KOS_SUCCESS);
                    obj_ids[i] = KOS_new_buffer(ctx, size);
                    TEST( ! IS_BAD_PTR(obj_ids[i]));

                    _fill_buffer(obj_ids[i], i);
                }

                TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

                TEST(stats.num_objs_evacuated >= base_stats.num_objs_evacuated);
                TEST(stats.num_objs_freed     == 0);
                TEST(stats.num_objs_finalized == 0);
                TEST(stats.num_pages_kept     >= num_objs);
                TEST(stats.num_pages_freed    == 1);
                TEST(stats.size_evacuated     >= base_stats.size_evacuated);
                TEST(stats.size_freed         == 0);
                TEST(stats.size_kept          >  0);

                for (i = 0; i < num_objs; ++i) {
                    TEST(_test_buffer(obj_ids[i], i, size) == KOS_SUCCESS);
                }

                KOS_pop_local_scope(ctx);

                TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

                KOS_instance_destroy(&inst);
            }
        }
    }

    return 0;
}

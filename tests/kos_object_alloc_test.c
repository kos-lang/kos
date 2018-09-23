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
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../core/kos_config.h"
#include "../core/kos_misc.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

#define NUM_OBJECTS (16 * 1024)

static void *_alloc_integer(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_INTEGER, sizeof(KOS_INTEGER));
}

static void *_alloc_float(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_FLOAT, sizeof(KOS_FLOAT));
}

static void *_alloc_string(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_STRING, sizeof(KOS_STRING));
}

static void *_alloc_object(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_OBJECT, sizeof(KOS_OBJECT));
}

static void *_alloc_array(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_ARRAY, sizeof(KOS_ARRAY));
}

static void *_alloc_buffer(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_BUFFER, sizeof(KOS_BUFFER));
}

static void *_alloc_function(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_FUNCTION, sizeof(KOS_FUNCTION));
}

static void *_alloc_dynamic_prop(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_DYNAMIC_PROP, sizeof(KOS_DYNAMIC_PROP));
}

static void *_alloc_object_walk(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_OBJECT_WALK, sizeof(KOS_OBJECT_WALK));
}

static void *_alloc_module(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_MODULE, sizeof(KOS_MODULE));
}

static void *_alloc_stack(KOS_CONTEXT ctx)
{
    return _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_STACK, sizeof(KOS_STACK));
}

typedef void *(* ALLOC_FUNC)(KOS_CONTEXT ctx);

struct _RANDOM_OBJECT
{
    uint8_t *obj;
    int      size_pot;
};

typedef union _KOS_BYTES {
    KOS_OBJ_HEADER header;
    struct {
        KOS_OBJ_ID alloc_size;
        uint8_t    type;
        uint8_t    value[3];
    }              bytes;
} KOS_BYTES;

static KOS_OBJ_ID _alloc_opaque(KOS_CONTEXT ctx,
                                uint8_t     fill,
                                size_t      size)
{
    unsigned actual_size;
    uint8_t *object = (uint8_t *)_KOS_alloc_object(ctx,
                                                   KOS_ALLOC_DEFAULT,
                                                   OBJ_OPAQUE,
                                                   (uint32_t)size);

    if ( ! object)
        return KOS_BADPTR;

    actual_size = (unsigned)GET_SMALL_INT(((KOS_OPAQUE *)object)->header.alloc_size);

    memset(object + sizeof(KOS_OPAQUE), fill, actual_size - sizeof(KOS_OPAQUE));

    return OBJID(OPAQUE, (KOS_OPAQUE *)object);
}

static KOS_OBJ_ID _alloc_bytes(KOS_CONTEXT ctx,
                               uint8_t     fill)
{
    KOS_BYTES *object = (KOS_BYTES *)_KOS_alloc_object(ctx,
                                                       KOS_ALLOC_DEFAULT,
                                                       OBJ_OPAQUE,
                                                       sizeof(KOS_BYTES));

    if ( ! object)
        return KOS_BADPTR;

    memset(&object->bytes.value, fill, sizeof(object->bytes.value));

    return OBJID(OPAQUE, (KOS_OPAQUE *)object);
}

static int _check_opaque(KOS_OBJ_ID obj_id,
                         uint8_t    value)
{
    uint8_t *object = (uint8_t *)OBJPTR(OPAQUE, obj_id);
    unsigned size   = (unsigned)GET_SMALL_INT(OBJPTR(OPAQUE, obj_id)->header.alloc_size);
    unsigned i;

    for (i = sizeof(KOS_OPAQUE); i < size; ++i) {

        const uint8_t actual = object[i];
        if (actual != value) {
            printf("Corruption at offset 0x%x (out of 0x%x total), expected=0x%02x, actual=0x%02x\n",
                   i, size, value, actual);
            return 0;
        }
    }

    return 1;
}

static int _check_bytes(KOS_OBJ_ID obj_id,
                        uint8_t    value)
{
    KOS_BYTES *object = (KOS_BYTES *)OBJPTR(OPAQUE, obj_id);
    unsigned   i;

    for (i = 0; i < sizeof(object->bytes.value); ++i)
        if (object->bytes.value[i] != value)
            return 0;

    return 1;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    size_t       i;

    const struct _ALLOC_FUNC {
        ALLOC_FUNC           alloc_func;
        enum KOS_OBJECT_TYPE type;
        size_t               size;
    } alloc[] = {
        { _alloc_integer,      OBJ_INTEGER,      sizeof(KOS_INTEGER)      },
        { _alloc_float,        OBJ_FLOAT,        sizeof(KOS_FLOAT)        },
        { _alloc_string,       OBJ_STRING,       sizeof(KOS_STRING)       },
        { _alloc_object,       OBJ_OBJECT,       sizeof(KOS_OBJECT)       },
        { _alloc_array,        OBJ_ARRAY,        sizeof(KOS_ARRAY)        },
        { _alloc_buffer,       OBJ_BUFFER,       sizeof(KOS_BUFFER)       },
        { _alloc_function,     OBJ_FUNCTION,     sizeof(KOS_FUNCTION)     },
        { _alloc_dynamic_prop, OBJ_DYNAMIC_PROP, sizeof(KOS_DYNAMIC_PROP) },
        { _alloc_object_walk,  OBJ_OBJECT_WALK,  sizeof(KOS_OBJECT_WALK)  },
        { _alloc_module,       OBJ_MODULE,       sizeof(KOS_MODULE)       },
        { _alloc_stack,        OBJ_STACK,        sizeof(KOS_STACK)        }
    };

    /************************************************************************/
    {
        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    {
        DECLARE_STATIC_CONST_OBJECT(const_obj) = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 2);

        KOS_BOOLEAN *bool_obj = (KOS_BOOLEAN *)&const_obj._alloc_size;

        TEST(bool_obj->header.alloc_size == 0);
        TEST(bool_obj->header.type       == OBJ_BOOLEAN);
        TEST(bool_obj->boolean.value     == 2);
    }

    /************************************************************************/
    for (i = 0; i < sizeof(alloc) / sizeof(alloc[0]); i++) {

        int    j;
        void **objects;

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        objects = (void **)_KOS_alloc_object(ctx,
                                             KOS_ALLOC_DEFAULT,
                                             OBJ_OPAQUE,
                                             NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = (*alloc[i].alloc_func)(ctx);
            TEST(objects[j]);

            TEST(((uint8_t *)objects[j])[sizeof(KOS_OBJ_ID)] == alloc[i].type);

            TEST(((intptr_t)objects[j] & 7) == 0);

            memset(objects[j], (uint8_t)j, alloc[i].size);
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf = (const uint8_t *)objects[j];
            const uint8_t *end = buf + alloc[i].size;

            for ( ; buf < end; buf++)
                if (*buf != (uint8_t)j) {
                    printf("object type index %d, object %d, offset %u, expected 0x%02x, actual 0x%02x\n",
                           (int)i,
                           j,
                           (unsigned)(buf - (uint8_t *)objects[j]),
                           (unsigned)(uint8_t)j,
                           (unsigned)*buf);
                    TEST(*buf == (uint8_t)j);
                }
        }

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    for (i = 3; i < 7; i++) {

        const int size = 1 << i;
        int       j;
        void    **objects;

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        objects = (void **)_KOS_alloc_object(ctx,
                                             KOS_ALLOC_DEFAULT,
                                             OBJ_OPAQUE,
                                             NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = _KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
            TEST(objects[j]);

            TEST(((intptr_t)objects[j] & 7) == 0);

            memset(objects[j], (uint8_t)j, (size_t)size);
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf = (const uint8_t *)objects[j];
            const uint8_t *end = buf + size;

            for ( ; buf < end; buf++)
                if (*buf != (uint8_t)j) {
                    printf("elem size 1<<%d, object %d, offset %u, expected 0x%02x, actual 0x%02x\n",
                           (int)i,
                           j,
                           (unsigned)(buf - (uint8_t *)objects[j]),
                           (unsigned)(uint8_t)j,
                           (unsigned)*buf);
                    TEST(*buf == (uint8_t)j);
                }
        }

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    {
        struct KOS_RNG         rng;
        struct _RANDOM_OBJECT *objects;
        int                    j;

        _KOS_rng_init(&rng);

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_object(ctx,
                                                             KOS_ALLOC_DEFAULT,
                                                             OBJ_OPAQUE,
                                                             NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size_pot = 3 + (int)_KOS_rng_random_range(&rng, 6 - 3);
            const int size     = 1 << size_pot;

            objects[j].size_pot = size_pot;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
            TEST(objects[j].obj);

            memset(objects[j].obj, (uint8_t)j, (size_t)size);
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf  = objects[j].obj;
            const int      size = 1 << objects[j].size_pot;
            const uint8_t *end  = buf + size;

            for ( ; buf < end; buf++)
                if (*buf != (uint8_t)j) {
                    printf("elem size %d, object %d, offset %u, expected 0x%02x, actual 0x%02x\n",
                           size,
                           j,
                           (unsigned)(buf - (uint8_t *)objects[j].obj),
                           (unsigned)(uint8_t)j,
                           (unsigned)*buf);
                    TEST(*buf == (uint8_t)j);
                }
        }

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    {
        struct KOS_RNG         rng;
        struct _RANDOM_OBJECT *objects;
        int                    j;

        _KOS_rng_init(&rng);

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_object(ctx,
                                                             KOS_ALLOC_DEFAULT,
                                                             OBJ_OPAQUE,
                                                             NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size = 9 + (int)_KOS_rng_random_range(&rng, 128 - 9);

            objects[j].size_pot = size;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object(ctx, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
            TEST(objects[j].obj);

            TEST(((intptr_t)objects[j].obj & 7) == 0);

            memset(objects[j].obj, (uint8_t)j, (size_t)size);
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf  = objects[j].obj;
            const int      size = objects[j].size_pot;
            const uint8_t *end  = buf + size;

            for ( ; buf < end; buf++)
                if (*buf != (uint8_t)j) {
                    printf("elem size %d, object %d, offset %u, expected 0x%02x, actual 0x%02x\n",
                           size,
                           j,
                           (unsigned)(buf - (uint8_t *)objects[j].obj),
                           (unsigned)(uint8_t)j,
                           (unsigned)*buf);
                    TEST(*buf == (uint8_t)j);
                }
        }

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    {
        KOS_OBJ_ID group_one[(_KOS_PAGE_SIZE / 10U) / sizeof(KOS_OPAQUE)];
        KOS_OBJ_ID group_two[(_KOS_POOL_SIZE / _KOS_PAGE_SIZE) + 1U];
        KOS_OBJ_ID group_three[2U * (_KOS_PAGE_SIZE / sizeof(KOS_BYTES))];
        KOS_OBJ_ID group_four[2];
        KOS_OBJ_ID group_five[2];

        TEST(KOS_instance_init(&inst, &ctx) == KOS_SUCCESS);

        /* Test 1:
         * Allocate objects in one page until the page is full, put the page
         * on the list of full pages. */

        group_one[0] = _alloc_opaque(ctx, 0xFEU, 90U * _KOS_PAGE_SIZE / 100U);
        TEST( ! IS_BAD_PTR(group_one[0]));

        for (i = 1; i < sizeof(group_one) / sizeof(group_one[0]); ++i) {
            group_one[i] = _alloc_bytes(ctx, (uint8_t)i);
            TEST( ! IS_BAD_PTR(group_one[i]));
        }

        /* Test 2:
         * Allocate all pages from a pool, accumulate many non-full pages. */

        for (i = 0; i < sizeof(group_two) / sizeof(group_two[0]); ++i) {

            /* Allocate smaller objects (leave more room) in the first half of
             * the allocated pages. */
            const uint32_t size = i < sizeof(group_two) / (2U * sizeof(group_two[0]))
                    ? (_KOS_PAGE_SIZE / 2U)
                    : (90U * _KOS_PAGE_SIZE / 100U);

            group_two[i] = _alloc_opaque(ctx, (uint8_t)i, size);
            TEST( ! IS_BAD_PTR(group_two[i]));
        }

        /* Test 3:
         * Allocate lots of small objects to migrate a non-full page to full set. */

        for (i = 0; i < sizeof(group_three) / sizeof(group_three[0]); ++i) {
            group_three[i] = _alloc_bytes(ctx, (uint8_t)i);
            TEST( ! IS_BAD_PTR(group_three[i]));
        }

        /* Test 4:
         * Allocate huge objects spanning multiple free pages. */

        for (i = 0; i < sizeof(group_four) / sizeof(group_four[0]); ++i) {
            group_four[i] = _alloc_opaque(ctx, (uint8_t)(0x80U + i), 3U * _KOS_PAGE_SIZE / 2U);
            TEST( ! IS_BAD_PTR(group_four[i]));
        }

        /* Test 5:
         * Allocate huge objects which cannot be accommodated in existing full pages. */

        for (i = 0; i < sizeof(group_five) / sizeof(group_five[0]); ++i) {
            group_five[i] = _alloc_opaque(ctx, (uint8_t)(0x90U + i), _KOS_POOL_SIZE / 2U);
            TEST( ! IS_BAD_PTR(group_five[i]));
        }

        /* Check contents of all objects */

        TEST(_check_opaque(group_one[0], 0xFEU));

        for (i = 1; i < sizeof(group_one) / sizeof(group_one[0]); ++i)
            TEST(_check_bytes(group_one[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_two) / sizeof(group_two[0]); ++i)
            TEST(_check_opaque(group_two[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_three) / sizeof(group_three[0]); ++i)
            TEST(_check_bytes(group_three[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_four) / sizeof(group_four[0]); ++i)
            TEST(_check_opaque(group_four[i], (uint8_t)(0x80U + i)));

        for (i = 0; i < sizeof(group_five) / sizeof(group_five[0]); ++i)
            TEST(_check_opaque(group_five[i], (uint8_t)(0x90U + i)));

        KOS_instance_destroy(&inst);
    }

    return 0;
}

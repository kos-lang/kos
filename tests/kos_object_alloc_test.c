/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../core/kos_heap.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "../core/kos_config.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_threads_internal.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

#define NUM_OBJECTS (16 * 1024)

static void *alloc_integer(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_INTEGER, sizeof(KOS_INTEGER));
}

static void *alloc_float(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_FLOAT, sizeof(KOS_FLOAT));
}

static void *alloc_string(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_STRING, sizeof(KOS_STRING));
}

static void *alloc_array(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_ARRAY, sizeof(KOS_ARRAY));
}

static void *alloc_buffer(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_BUFFER, sizeof(KOS_BUFFER));
}

static void *alloc_function(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_FUNCTION, sizeof(KOS_FUNCTION));
}

static void *alloc_dynamic_prop(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_DYNAMIC_PROP, sizeof(KOS_DYNAMIC_PROP));
}

static void *alloc_iterator(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_ITERATOR, sizeof(KOS_ITERATOR));
}

static void *alloc_module(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_MODULE, sizeof(KOS_MODULE));
}

static void *alloc_stack(KOS_CONTEXT ctx)
{
    return kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_STACK, sizeof(KOS_STACK));
}

typedef void *(* ALLOC_FUNC)(KOS_CONTEXT ctx);

struct RANDOM_OBJECT_S {
    uint8_t *obj;
    int      size_pot;
};

typedef union KOS_BYTES_U {
    KOS_OBJ_HEADER header;
    struct {
        KOS_OBJ_ID alloc_size;
        uint8_t    type;
        uint8_t    value[3];
    }              bytes;
} KOS_BYTES;

static KOS_OBJ_ID alloc_opaque(KOS_CONTEXT ctx,
                               uint8_t     fill,
                               size_t      size)
{
    unsigned actual_size;
    uint8_t *object = (uint8_t *)kos_alloc_object(ctx,
                                                  KOS_ALLOC_MOVABLE,
                                                  OBJ_OPAQUE,
                                                  (uint32_t)size);

    if ( ! object)
        return KOS_BADPTR;

    actual_size = kos_get_object_size(((KOS_OPAQUE *)object)->header);

    memset(object + sizeof(KOS_OPAQUE), fill, actual_size - sizeof(KOS_OPAQUE));

    return OBJID(OPAQUE, (KOS_OPAQUE *)object);
}

static KOS_OBJ_ID alloc_bytes(KOS_CONTEXT ctx,
                              uint8_t     fill)
{
    KOS_BYTES *object = (KOS_BYTES *)kos_alloc_object(ctx,
                                                      KOS_ALLOC_MOVABLE,
                                                      OBJ_OPAQUE,
                                                      sizeof(KOS_BYTES));

    if ( ! object)
        return KOS_BADPTR;

    memset(&object->bytes.value, fill, sizeof(object->bytes.value));

    return OBJID(OPAQUE, (KOS_OPAQUE *)object);
}

static int check_opaque(KOS_OBJ_ID obj_id,
                        uint8_t    value)
{
    uint8_t *object = (uint8_t *)OBJPTR(OPAQUE, obj_id);
    unsigned size   = kos_get_object_size(OBJPTR(OPAQUE, obj_id)->header);
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

static int check_bytes(KOS_OBJ_ID obj_id,
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

    const struct ALLOC_FUNC_S {
        ALLOC_FUNC alloc_func;
        KOS_TYPE   type;
        size_t     size;
    } alloc[] = {
        { alloc_integer,      OBJ_INTEGER,      sizeof(KOS_INTEGER)      },
        { alloc_float,        OBJ_FLOAT,        sizeof(KOS_FLOAT)        },
        { alloc_string,       OBJ_STRING,       sizeof(KOS_STRING)       },
        { alloc_array,        OBJ_ARRAY,        sizeof(KOS_ARRAY)        },
        { alloc_buffer,       OBJ_BUFFER,       sizeof(KOS_BUFFER)       },
        { alloc_function,     OBJ_FUNCTION,     sizeof(KOS_FUNCTION)     },
        { alloc_dynamic_prop, OBJ_DYNAMIC_PROP, sizeof(KOS_DYNAMIC_PROP) },
        { alloc_iterator,     OBJ_ITERATOR,     sizeof(KOS_ITERATOR)     },
        { alloc_module,       OBJ_MODULE,       sizeof(KOS_MODULE)       },
        { alloc_stack,        OBJ_STACK,        sizeof(KOS_STACK)        }
    };

    /************************************************************************/
    {
        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    {
        DECLARE_STATIC_CONST_OBJECT(const_obj, OBJ_BOOLEAN, 2);

        KOS_BOOLEAN *bool_obj = (KOS_BOOLEAN *)&const_obj.object;

        TEST(kos_get_object_size(bool_obj->header) == 0);
        TEST(kos_get_object_type(bool_obj->header) == OBJ_BOOLEAN);
        TEST(bool_obj->value                       == 2);
    }

    /************************************************************************/
    for (i = 0; i < sizeof(alloc) / sizeof(alloc[0]); i++) {

        int         j;
        KOS_OPAQUE *alloc_cont;
        void      **objects;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        alloc_cont = (KOS_OPAQUE *)kos_alloc_object(
                ctx,
                KOS_ALLOC_MOVABLE,
                OBJ_OPAQUE,
                NUM_OBJECTS * sizeof(void *) + sizeof(KOS_OPAQUE));
        TEST(alloc_cont);
        objects = (void **)&alloc_cont[1];

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = (*alloc[i].alloc_func)(ctx);
            TEST(objects[j]);

            TEST(kos_get_object_type(*(KOS_OBJ_HEADER *)objects[j]) == alloc[i].type);

            TEST(((intptr_t)objects[j] & 7) == 0);

            memset(((uint8_t *)objects[j]) + sizeof(KOS_OPAQUE),
                   (uint8_t)j,
                   alloc[i].size - sizeof(KOS_OPAQUE));
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf = (const uint8_t *)objects[j];
            const uint8_t *end = buf + alloc[i].size;

            buf += sizeof(KOS_OPAQUE);

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

        const int   size = 1 << i;
        int         j;
        KOS_OPAQUE *alloc_cont;
        void      **objects;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        alloc_cont = (KOS_OPAQUE *)kos_alloc_object(
                ctx,
                KOS_ALLOC_MOVABLE,
                OBJ_OPAQUE,
                NUM_OBJECTS * sizeof(void *) + sizeof(KOS_OPAQUE));
        TEST(alloc_cont);
        objects = (void **)&alloc_cont[1];

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_OPAQUE, size);
            TEST(objects[j]);

            TEST(((intptr_t)objects[j] & 7) == 0);

            if ((size_t)size > sizeof(KOS_OPAQUE))
                memset(((uint8_t *)objects[j]) + sizeof(KOS_OPAQUE),
                       (uint8_t)j,
                       (size_t)size - sizeof(KOS_OPAQUE));
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf = (const uint8_t *)objects[j];
            const uint8_t *end = buf + size;

            buf += sizeof(KOS_OPAQUE);

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
        struct KOS_RNG          rng;
        KOS_OPAQUE             *alloc_cont;
        struct RANDOM_OBJECT_S *objects;
        int                     j;

        kos_rng_init(&rng);

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        alloc_cont = (KOS_OPAQUE *)kos_alloc_object(
                ctx,
                KOS_ALLOC_MOVABLE,
                OBJ_OPAQUE,
                NUM_OBJECTS * sizeof(struct RANDOM_OBJECT_S) + sizeof(KOS_OPAQUE));
        TEST(alloc_cont);
        objects = (struct RANDOM_OBJECT_S *)&alloc_cont[1];

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size_pot = 3 + (int)kos_rng_random_range(&rng, 6 - 3);
            const int size     = 1 << size_pot;

            objects[j].size_pot = size_pot;
            objects[j].obj      = (uint8_t *)kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_OPAQUE, size);
            TEST(objects[j].obj);

            if ((size_t)size > sizeof(KOS_OPAQUE))
                memset(((uint8_t *)objects[j].obj) + sizeof(KOS_OPAQUE),
                       (uint8_t)j,
                       (size_t)size - sizeof(KOS_OPAQUE));
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf  = objects[j].obj;
            const int      size = 1 << objects[j].size_pot;
            const uint8_t *end  = buf + size;

            buf += sizeof(KOS_OPAQUE);

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
        struct KOS_RNG          rng;
        KOS_OPAQUE             *alloc_cont;
        struct RANDOM_OBJECT_S *objects;
        int                     j;

        kos_rng_init(&rng);

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        alloc_cont = (KOS_OPAQUE *)kos_alloc_object(
                ctx,
                KOS_ALLOC_MOVABLE,
                OBJ_OPAQUE,
                NUM_OBJECTS * sizeof(struct RANDOM_OBJECT_S) + sizeof(KOS_OPAQUE));
        TEST(alloc_cont);
        objects = (struct RANDOM_OBJECT_S *)&alloc_cont[1];

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size = 9 + (int)kos_rng_random_range(&rng, 128 - 9);

            objects[j].size_pot = size;
            objects[j].obj      = (uint8_t *)kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_OPAQUE, size);
            TEST(objects[j].obj);

            TEST(((intptr_t)objects[j].obj & 7) == 0);

            if ((size_t)size > sizeof(KOS_OPAQUE))
                memset(((uint8_t *)objects[j].obj) + sizeof(KOS_OPAQUE),
                       (uint8_t)j,
                       (size_t)size - sizeof(KOS_OPAQUE));
        }

        for (j = 0; j < NUM_OBJECTS; j++) {

            const uint8_t *buf  = objects[j].obj;
            const int      size = objects[j].size_pot;
            const uint8_t *end  = buf + size;

            buf += sizeof(KOS_OPAQUE);

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
        KOS_OBJ_ID group_one[(KOS_PAGE_SIZE / 10U) / sizeof(KOS_OPAQUE)];
        KOS_OBJ_ID group_two[(KOS_POOL_SIZE / KOS_PAGE_SIZE) + 1U];
        KOS_OBJ_ID group_three[2U * (KOS_PAGE_SIZE / sizeof(KOS_BYTES))];
        KOS_OBJ_ID group_four[2];
        KOS_OBJ_ID group_five[2];

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        /* Test 1:
         * Allocate objects in one page until the page is full, put the page
         * on the list of full pages. */

        group_one[0] = alloc_opaque(ctx, 0xFEU, 90U * KOS_PAGE_SIZE / 100U);
        TEST( ! IS_BAD_PTR(group_one[0]));

        for (i = 1; i < sizeof(group_one) / sizeof(group_one[0]); ++i) {
            group_one[i] = alloc_bytes(ctx, (uint8_t)i);
            TEST( ! IS_BAD_PTR(group_one[i]));
        }

        /* Test 2:
         * Allocate all pages from a pool, accumulate many non-full pages. */

        for (i = 0; i < sizeof(group_two) / sizeof(group_two[0]); ++i) {

            /* Allocate smaller objects (leave more room) in the first half of
             * the allocated pages. */
            const uint32_t size = i < sizeof(group_two) / (2U * sizeof(group_two[0]))
                    ? (KOS_PAGE_SIZE / 2U)
                    : (90U * KOS_PAGE_SIZE / 100U);

            group_two[i] = alloc_opaque(ctx, (uint8_t)i, size);
            TEST( ! IS_BAD_PTR(group_two[i]));
        }

        /* Test 3:
         * Allocate lots of small objects to migrate a non-full page to full set. */

        for (i = 0; i < sizeof(group_three) / sizeof(group_three[0]); ++i) {
            group_three[i] = alloc_bytes(ctx, (uint8_t)i);
            TEST( ! IS_BAD_PTR(group_three[i]));
        }

        /* Test 4:
         * Allocate huge objects spanning multiple free pages. */

        for (i = 0; i < sizeof(group_four) / sizeof(group_four[0]); ++i) {
            group_four[i] = alloc_opaque(ctx, (uint8_t)(0x80U + i), 3U * KOS_PAGE_SIZE / 2U);
            TEST( ! IS_BAD_PTR(group_four[i]));
        }

        /* Test 5:
         * Allocate huge objects which cannot be accommodated in existing full pages. */

        for (i = 0; i < sizeof(group_five) / sizeof(group_five[0]); ++i) {
            group_five[i] = alloc_opaque(ctx, (uint8_t)(0x90U + i), KOS_POOL_SIZE / 2U);
            TEST( ! IS_BAD_PTR(group_five[i]));
        }

        /* Check contents of all objects */

        TEST(check_opaque(group_one[0], 0xFEU));

        for (i = 1; i < sizeof(group_one) / sizeof(group_one[0]); ++i)
            TEST(check_bytes(group_one[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_two) / sizeof(group_two[0]); ++i)
            TEST(check_opaque(group_two[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_three) / sizeof(group_three[0]); ++i)
            TEST(check_bytes(group_three[i], (uint8_t)i));

        for (i = 0; i < sizeof(group_four) / sizeof(group_four[0]); ++i)
            TEST(check_opaque(group_four[i], (uint8_t)(0x80U + i)));

        for (i = 0; i < sizeof(group_five) / sizeof(group_five[0]); ++i)
            TEST(check_opaque(group_five[i], (uint8_t)(0x90U + i)));

        KOS_instance_destroy(&inst);
    }

    return 0;
}

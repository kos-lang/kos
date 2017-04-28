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

#include "../core/kos_object_alloc.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../core/kos_misc.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

#define NUM_OBJECTS (16 * 1024)

static void *_alloc_integer(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, INTEGER);
}

static void *_alloc_float(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, FLOAT);
}

static void *_alloc_string(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, STRING);
}

static void *_alloc_object(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, OBJECT);
}

static void *_alloc_array(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, ARRAY);
}

static void *_alloc_buffer(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, BUFFER);
}

static void *_alloc_function(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, FUNCTION);
}

static void *_alloc_dynamic_prop(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, DYNAMIC_PROP);
}

static void *_alloc_object_walk(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, OBJECT_WALK);
}

static void *_alloc_module(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, MODULE);
}

typedef struct _KOS_STACK_FRAME *KOS_STACK_FRAME;

#define OBJ_STACK_FRAME 0xF

static void *_alloc_stack_frame(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, STACK_FRAME);
}

typedef void *(* ALLOC_FUNC)(KOS_FRAME frame);

struct _RANDOM_OBJECT
{
    uint8_t *obj;
    int      size_pot;
};

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;
    size_t      i;

    const struct _ALLOC_FUNC {
        ALLOC_FUNC alloc_func;
        size_t     size;
    } alloc[] = {
        { _alloc_integer,      sizeof(KOS_INTEGER)      },
        { _alloc_float,        sizeof(KOS_FLOAT)        },
        { _alloc_string,       sizeof(KOS_STRING)       },
        { _alloc_object,       sizeof(KOS_OBJECT)       },
        { _alloc_array,        sizeof(KOS_ARRAY)        },
        { _alloc_buffer,       sizeof(KOS_BUFFER)       },
        { _alloc_function,     sizeof(KOS_FUNCTION)     },
        { _alloc_dynamic_prop, sizeof(KOS_DYNAMIC_PROP) },
        { _alloc_object_walk,  sizeof(KOS_OBJECT_WALK)  },
        { _alloc_module,       sizeof(KOS_MODULE)       },
        { _alloc_stack_frame,  sizeof(KOS_STACK_FRAME)  }
    };

    /************************************************************************/
    for (i = 0; i < sizeof(alloc) / sizeof(alloc[0]); i++) {

        int    j;
        void **objects;

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        if (alloc[i].size >= 64)
            _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);
        else
            TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_RECLAIMABLE);

        objects = (void **)_KOS_alloc_buffer(frame, NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = (*alloc[i].alloc_func)(frame);
            TEST(objects[j]);

            if (i < 2) /* OBJ_INTEGER, OBJ_FLOAT */
                TEST(((intptr_t)objects[j] & 7) == 0);
            else
                TEST(((intptr_t)objects[j] & 15) == 0);

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

        if (alloc[i].size >= 64)
            TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_FIXED);
        else
            TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_RECLAIMABLE);

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    for (i = 3; i < 7; i++) {

        const int size = 1 << i;
        int       j;
        void    **objects;

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_RECLAIMABLE);

        objects = (void **)_KOS_alloc_buffer(frame, NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = _KOS_alloc_object_internal(frame,
                                                    (enum _KOS_AREA_ELEM_SIZE)i,
                                                    size);
            TEST(objects[j]);

            if (i == 3)
                TEST(((intptr_t)objects[j] & 7) == 0);
            else
                TEST(((intptr_t)objects[j] & 15) == 0);

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

        TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_RECLAIMABLE);

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    {
        struct KOS_RNG         rng;
        struct _RANDOM_OBJECT *objects;
        int                    j;

        _KOS_rng_init(&rng);

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        TEST(_KOS_alloc_get_mode(frame) == KOS_AREA_RECLAIMABLE);

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_buffer(frame, NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size_pot = 3 + (int)_KOS_rng_random_range(&rng, 6 - 3);
            const int size     = 1 << size_pot;

            objects[j].size_pot = size_pot;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object_internal(
                                       frame,
                                       (enum _KOS_AREA_ELEM_SIZE)size_pot,
                                       size);
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

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    {
        struct KOS_RNG         rng;
        struct _RANDOM_OBJECT *objects;
        int                    j;

        _KOS_rng_init(&rng);

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_buffer(frame, NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size = 1 + (int)_KOS_rng_random_range(&rng, 127);

            objects[j].size_pot = size;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object_internal(
                                       frame,
                                       KOS_AREA_128,
                                       size);
            TEST(objects[j].obj);

            TEST(((intptr_t)objects[j].obj & 15) == 0);

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

        KOS_context_destroy(&ctx);
    }

    return 0;
}

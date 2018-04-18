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
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_INTEGER, sizeof(KOS_INTEGER));
}

static void *_alloc_float(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_FLOAT, sizeof(KOS_FLOAT));
}

static void *_alloc_string(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_STRING, sizeof(KOS_STRING));
}

static void *_alloc_object(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_OBJECT, sizeof(KOS_OBJECT));
}

static void *_alloc_array(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_ARRAY, sizeof(KOS_ARRAY));
}

static void *_alloc_buffer(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_BUFFER, sizeof(KOS_BUFFER));
}

static void *_alloc_function(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_FUNCTION, sizeof(KOS_FUNCTION));
}

static void *_alloc_dynamic_prop(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_DYNAMIC_PROP, sizeof(KOS_DYNAMIC_PROP));
}

static void *_alloc_object_walk(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_OBJECT_WALK, sizeof(KOS_OBJECT_WALK));
}

static void *_alloc_module(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_MODULE, sizeof(KOS_MODULE));
}

static void *_alloc_stack_frame(KOS_FRAME frame)
{
    return _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_STACK_FRAME, sizeof(struct _KOS_STACK_FRAME));
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
        { _alloc_stack_frame,  OBJ_STACK_FRAME,  sizeof(struct _KOS_STACK_FRAME) }
    };

    /************************************************************************/
    {
        static struct _KOS_CONST_OBJECT const_obj = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 2);

        KOS_BOOLEAN *bool_obj = (KOS_BOOLEAN *)&const_obj._alloc_size;

        TEST(bool_obj->header.alloc_size == 0);
        TEST(bool_obj->header.type       == OBJ_BOOLEAN);
        TEST(bool_obj->boolean.value     == 2);
    }

    /************************************************************************/
    for (i = 0; i < sizeof(alloc) / sizeof(alloc[0]); i++) {

        int    j;
        void **objects;

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        objects = (void **)_KOS_alloc_object(frame,
                                             KOS_ALLOC_DEFAULT,
                                             OBJ_OPAQUE,
                                             NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = (*alloc[i].alloc_func)(frame);
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

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    for (i = 3; i < 7; i++) {

        const int size = 1 << i;
        int       j;
        void    **objects;

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        objects = (void **)_KOS_alloc_object(frame,
                                             KOS_ALLOC_DEFAULT,
                                             OBJ_OPAQUE,
                                             NUM_OBJECTS * sizeof(void *));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            objects[j] = _KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
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

        KOS_context_destroy(&ctx);
    }

    /************************************************************************/
    {
        struct KOS_RNG         rng;
        struct _RANDOM_OBJECT *objects;
        int                    j;

        _KOS_rng_init(&rng);

        TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_object(frame,
                                                             KOS_ALLOC_DEFAULT,
                                                             OBJ_OPAQUE,
                                                             NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size_pot = 3 + (int)_KOS_rng_random_range(&rng, 6 - 3);
            const int size     = 1 << size_pot;

            objects[j].size_pot = size_pot;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
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

        objects = (struct _RANDOM_OBJECT *)_KOS_alloc_object(frame,
                                                             KOS_ALLOC_DEFAULT,
                                                             OBJ_OPAQUE,
                                                             NUM_OBJECTS * sizeof(struct _RANDOM_OBJECT));
        TEST(objects);

        for (j = 0; j < NUM_OBJECTS; j++) {

            const int size = 9 + (int)_KOS_rng_random_range(&rng, 128 - 9);

            objects[j].size_pot = size;
            objects[j].obj      = (uint8_t *)_KOS_alloc_object(frame, KOS_ALLOC_DEFAULT, OBJ_OBJECT, size);
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

        KOS_context_destroy(&ctx);
    }

    return 0;
}

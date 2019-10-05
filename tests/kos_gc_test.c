/*
 * Copyright (c) 2014-2019 Chris Dragan
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
#define TRIGGER_SIZE ((1U + (100U - KOS_MIGRATION_THRESH) * KOS_SLOTS_PER_PAGE / 100U) << KOS_OBJ_ALIGN_BITS)
#define NELEMS(array) (sizeof(array) / sizeof((array)[0]))

typedef struct OBJECT_DESC_S {
    KOS_TYPE type;
    uint32_t size;
} OBJECT_DESC;

static int alloc_page_with_objects(KOS_CONTEXT        ctx,
                                   KOS_OBJ_ID        *dest,
                                   const OBJECT_DESC *descs,
                                   uint32_t           num_objs)
{
    uint8_t *storage = (uint8_t *)kos_alloc_object_page(ctx, OBJ_OPAQUE);
    uint32_t total_size;

    if ( ! storage)
        return KOS_ERROR_EXCEPTION;

    total_size = kos_get_object_size(*(KOS_OBJ_HEADER *)storage);

    for ( ; num_objs; --num_objs, ++descs, ++dest) {

        KOS_OBJ_HEADER *hdr  = (KOS_OBJ_HEADER *)storage;
        const uint32_t  size = KOS_align_up(descs->size, 1U << KOS_OBJ_ALIGN_BITS);

        assert(total_size > size);

        kos_set_object_type_size(*hdr, descs->type, size);

        total_size -= size;
        storage    += size;
        *dest       = (KOS_OBJ_ID)((intptr_t)hdr + 1);
    }

    assert(total_size > sizeof(KOS_OPAQUE));

    {
        KOS_OBJ_HEADER *hdr = (KOS_OBJ_HEADER *)storage;

        kos_set_object_type_size(*hdr, OBJ_OPAQUE, total_size);
    }

    return KOS_SUCCESS;
}

static KOS_OBJ_ID alloc_page_with_object(KOS_CONTEXT ctx,
                                         KOS_TYPE    type,
                                         size_t      size)
{
    KOS_OBJ_ID  obj_id;
    OBJECT_DESC desc;

    desc.type = type;
    desc.size = (uint32_t)size;

    if (alloc_page_with_objects(ctx, &obj_id, &desc, 1U))
        return KOS_BADPTR;

    return obj_id;
}

static uint32_t get_obj_size(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_HEADER *hdr = (KOS_OBJ_HEADER *)((intptr_t)obj_id - 1);
    return kos_get_object_size(*hdr);
}

static uint32_t get_obj_sizes(KOS_OBJ_ID *obj_ids, uint32_t num_objs)
{
    uint32_t total = 0;

    while (num_objs--)
        total += get_obj_size(*(obj_ids++));

    return total;
}

typedef int (*VERIFY_FUNC)(KOS_OBJ_ID obj_id);

typedef KOS_OBJ_ID (*ALLOC_FUNC)(KOS_CONTEXT  ctx,
                                 uint32_t    *num_objs,
                                 uint32_t    *total_size,
                                 VERIFY_FUNC *verify);

static int verify_integer(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, obj_id)->value == 42);
    return 0;
}

static KOS_OBJ_ID alloc_integer(KOS_CONTEXT  ctx,
                                uint32_t    *num_objs,
                                uint32_t    *total_size,
                                VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_INTEGER, sizeof(KOS_INTEGER));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    OBJPTR(INTEGER, obj_id)->value = 42;
    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_integer;
    return obj_id;
}

static int verify_float(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_FLOAT);
    TEST(OBJPTR(FLOAT, obj_id)->value == 42.0);
    return 0;
}

static KOS_OBJ_ID alloc_float(KOS_CONTEXT  ctx,
                              uint32_t    *num_objs,
                              uint32_t    *total_size,
                              VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_FLOAT, sizeof(KOS_FLOAT));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    OBJPTR(FLOAT, obj_id)->value = 42.0;
    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_float;
    return obj_id;
}

static const char string_local_test[] = "kos";

static int verify_string_local(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
    TEST(OBJPTR(STRING, obj_id)->header.flags == KOS_STRING_LOCAL);
    TEST(OBJPTR(STRING, obj_id)->header.length == (uint16_t)(sizeof(string_local_test) - 1));
    TEST(memcmp(&OBJPTR(STRING, obj_id)->local.data[0], string_local_test, sizeof(string_local_test) - 1) == 0);
    return 0;
}

static KOS_OBJ_ID alloc_string_local(KOS_CONTEXT  ctx,
                                     uint32_t    *num_objs,
                                     uint32_t    *total_size,
                                     VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_STRING,
                                               sizeof(struct KOS_STRING_LOCAL_S) + sizeof(string_local_test) - 2);

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    OBJPTR(STRING, obj_id)->header.flags  = KOS_STRING_LOCAL;
    OBJPTR(STRING, obj_id)->header.length = (uint16_t)(sizeof(string_local_test) - 1);
    memcpy(&OBJPTR(STRING, obj_id)->local.data[0], string_local_test, sizeof(string_local_test) - 1);
    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_string_local;
    return obj_id;
}

static int verify_string_ptr(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
    TEST(OBJPTR(STRING, obj_id)->header.flags == KOS_STRING_PTR);
    TEST(OBJPTR(STRING, obj_id)->header.length == (uint16_t)(sizeof(string_local_test) - 1));
    TEST(OBJPTR(STRING, obj_id)->ptr.data_ptr == string_local_test);
    return 0;
}

static KOS_OBJ_ID alloc_string_ptr(KOS_CONTEXT  ctx,
                                   uint32_t    *num_objs,
                                   uint32_t    *total_size,
                                   VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_STRING, sizeof(struct KOS_STRING_PTR_S));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    OBJPTR(STRING, obj_id)->header.flags  = KOS_STRING_PTR;
    OBJPTR(STRING, obj_id)->header.length = (uint16_t)(sizeof(string_local_test) - 1);
    OBJPTR(STRING, obj_id)->ptr.data_ptr  = string_local_test;
    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_string_ptr;
    return obj_id;
}

static int verify_string_ref(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
    TEST(OBJPTR(STRING, obj_id)->header.flags == KOS_STRING_REF);
    TEST(OBJPTR(STRING, obj_id)->header.length == (uint16_t)(sizeof(string_local_test) - 1));
    TEST(memcmp(OBJPTR(STRING, obj_id)->ref.data_ptr, string_local_test, sizeof(string_local_test) - 1) == 0);
    return 0;
}

static KOS_OBJ_ID alloc_string_ref(KOS_CONTEXT  ctx,
                                   uint32_t    *num_objs,
                                   uint32_t    *total_size,
                                   VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[2];
    OBJECT_DESC desc[2] = {
        { OBJ_STRING, (uint32_t)(sizeof(struct KOS_STRING_LOCAL_S) + sizeof(string_local_test) - 2) },
        { OBJ_STRING, (uint32_t)sizeof(struct KOS_STRING_REF_S) }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(STRING, obj_id[0])->header.flags  = KOS_STRING_LOCAL;
    OBJPTR(STRING, obj_id[0])->header.length = (uint16_t)(sizeof(string_local_test) - 1);
    memcpy(&OBJPTR(STRING, obj_id[0])->local.data[0], string_local_test, sizeof(string_local_test) - 1);

    OBJPTR(STRING, obj_id[1])->header.flags  = KOS_STRING_REF;
    OBJPTR(STRING, obj_id[1])->header.length = (uint16_t)(sizeof(string_local_test) - 1);
    OBJPTR(STRING, obj_id[1])->ref.obj_id    = obj_id[0];
    OBJPTR(STRING, obj_id[1])->ref.data_ptr  = &OBJPTR(STRING, obj_id[0])->local.data[0];

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_string_ref;

    return obj_id[1];
}

static int verify_empty_array(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
    TEST(KOS_get_array_size(obj_id) == 0);
    TEST(kos_get_array_storage(obj_id) == KOS_BADPTR);
    return 0;
}

static KOS_OBJ_ID alloc_empty_array(KOS_CONTEXT  ctx,
                                    uint32_t    *num_objs,
                                    uint32_t    *total_size,
                                    VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_ARRAY, sizeof(KOS_ARRAY));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    KOS_atomic_write_relaxed_u32(OBJPTR(ARRAY, obj_id)->size, 0U);
    KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY, obj_id)->data, KOS_BADPTR);

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_empty_array;

    return obj_id;
}

static int verify_array(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
    TEST(KOS_get_array_size(obj_id) == 1U);

    v = kos_get_array_storage(obj_id);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_ARRAY_STORAGE);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY_STORAGE, v)->capacity) == 1);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, v)->buf[0]);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 43);
    return 0;
}

static KOS_OBJ_ID alloc_array(KOS_CONTEXT  ctx,
                              uint32_t    *num_objs,
                              uint32_t    *total_size,
                              VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[3];
    OBJECT_DESC desc[3] = {
        { OBJ_ARRAY,         (uint32_t)sizeof(KOS_ARRAY)         },
        { OBJ_ARRAY_STORAGE, (uint32_t)sizeof(KOS_ARRAY_STORAGE) },
        { OBJ_INTEGER,       (uint32_t)sizeof(KOS_INTEGER)       }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    KOS_atomic_write_relaxed_u32(OBJPTR(ARRAY, obj_id[0])->size, 1U);
    KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY, obj_id[0])->data, obj_id[1]);

    KOS_atomic_write_relaxed_u32(OBJPTR(ARRAY_STORAGE, obj_id[1])->capacity,       1U);
    KOS_atomic_write_relaxed_u32(OBJPTR(ARRAY_STORAGE, obj_id[1])->num_slots_open, 0U);
    KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, obj_id[1])->next,           KOS_BADPTR);
    KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, obj_id[1])->buf[0],         obj_id[2]);

    OBJPTR(INTEGER, obj_id[2])->value = 43;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_array;

    return obj_id[0];
}

static KOS_OBJ_ID alloc_array_storage_page(KOS_CONTEXT ctx)
{
    KOS_ARRAY_STORAGE *array = (KOS_ARRAY_STORAGE *)kos_alloc_object_page(ctx, OBJ_ARRAY_STORAGE);
    uint32_t           usable_size;
    uint32_t           capacity;
    uint32_t           i;

    if ( ! array)
        return KOS_BADPTR;

    usable_size = kos_get_object_size(array->header) - sizeof(KOS_ARRAY_STORAGE) + sizeof(KOS_OBJ_ID);
    capacity    = usable_size >> KOS_OBJ_ALIGN_BITS;

    KOS_atomic_write_relaxed_u32(array->capacity,       capacity);
    KOS_atomic_write_relaxed_u32(array->num_slots_open, 0U);
    KOS_atomic_write_relaxed_ptr(array->next,           KOS_BADPTR);

    for (i = 0; i < capacity; i++)
        KOS_atomic_write_relaxed_ptr(array->buf[i], KOS_BADPTR);

    return OBJID(ARRAY_STORAGE, array);
}

static void write_array_storage(KOS_OBJ_ID array, uint32_t i, KOS_OBJ_ID value)
{
    assert(GET_OBJ_TYPE(array) == OBJ_ARRAY_STORAGE);
    assert(i < OBJPTR(ARRAY_STORAGE, array)->capacity);

    KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, array)->buf[i], value);
}

static int verify_empty_buffer(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    TEST(KOS_get_buffer_size(obj_id) == 0);
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data)));
    return 0;
}

static KOS_OBJ_ID alloc_empty_buffer(KOS_CONTEXT  ctx,
                                     uint32_t    *num_objs,
                                     uint32_t    *total_size,
                                     VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_BUFFER, sizeof(KOS_BUFFER));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    KOS_atomic_write_relaxed_u32(OBJPTR(BUFFER, obj_id)->size, 0U);
    KOS_atomic_write_relaxed_ptr(OBJPTR(BUFFER, obj_id)->data, KOS_BADPTR);

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_empty_buffer;

    return obj_id;
}

static const uint8_t buffer_test[] = { 1, 2, 3, 4, 5, 6 };

static int verify_buffer(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    TEST(KOS_get_buffer_size(obj_id) == sizeof(buffer_test));

    v = KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_BUFFER_STORAGE);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER_STORAGE, v)->capacity) == sizeof(buffer_test));

    TEST(memcmp(&OBJPTR(BUFFER_STORAGE, v)->buf[0], buffer_test, sizeof(buffer_test)) == 0);
    return 0;
}

static KOS_OBJ_ID alloc_buffer(KOS_CONTEXT  ctx,
                               uint32_t    *num_objs,
                               uint32_t    *total_size,
                               VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[2];
    OBJECT_DESC desc[2] = {
        { OBJ_BUFFER,         (uint32_t)sizeof(KOS_BUFFER) },
        { OBJ_BUFFER_STORAGE, (uint32_t)(sizeof(KOS_BUFFER_STORAGE) + sizeof(buffer_test) - 1) }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    KOS_atomic_write_relaxed_u32(OBJPTR(BUFFER, obj_id[0])->size, (uint32_t)sizeof(buffer_test));
    KOS_atomic_write_relaxed_ptr(OBJPTR(BUFFER, obj_id[0])->data, obj_id[1]);

    KOS_atomic_write_relaxed_u32(OBJPTR(BUFFER_STORAGE, obj_id[1])->capacity, (uint32_t)sizeof(buffer_test));
    memcpy(&OBJPTR(BUFFER_STORAGE, obj_id[1])->buf[0], buffer_test, sizeof(buffer_test));

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_buffer;

    return obj_id[0];
}

static int verify_empty_object(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT, obj_id)->props)));
    TEST(IS_BAD_PTR(OBJPTR(OBJECT, obj_id)->prototype));
    TEST(KOS_object_get_private_ptr(obj_id) == 0);
    return 0;
}

static KOS_OBJ_ID alloc_empty_object(KOS_CONTEXT  ctx,
                                     uint32_t    *num_objs,
                                     uint32_t    *total_size,
                                     VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_OBJECT, sizeof(KOS_OBJECT));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    kos_init_object(OBJPTR(OBJECT, obj_id), KOS_BADPTR);

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_empty_object;

    return obj_id;
}

static int verify_object(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;
    uint32_t   i;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);

    TEST((intptr_t)KOS_object_get_private_ptr(obj_id) == 44);

    v = OBJPTR(OBJECT, obj_id)->prototype;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 45);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT, obj_id)->props);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_OBJECT_STORAGE);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->capacity)       == 4);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->num_slots_used) == 1);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->num_slots_open) == 0);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->active_copies)  == 0);
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->new_prop_table)));

    for (i = 0; i < 4; i++) {
        const KOS_OBJ_ID key   = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->items[i].key);
        const KOS_OBJ_ID value = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->items[i].value);
        const uint32_t   hash  = OBJPTR(OBJECT_STORAGE, v)->items[i].hash.hash;

        TEST(hash == i);

        if (i == 2) {
            TEST( ! IS_BAD_PTR(key));
            TEST(GET_OBJ_TYPE(key) == OBJ_STRING);
            TEST(OBJPTR(STRING, key)->header.flags == KOS_STRING_LOCAL);
            TEST(OBJPTR(STRING, key)->header.length == (uint16_t)(sizeof(string_local_test) - 1));
            TEST(memcmp(&OBJPTR(STRING, key)->local.data[0], string_local_test, sizeof(string_local_test) - 1) == 0);

            TEST( ! IS_BAD_PTR(value));
            TEST(GET_OBJ_TYPE(value) == OBJ_INTEGER);
            TEST(OBJPTR(INTEGER, value)->value == 46);
        }
        else {
            TEST(IS_BAD_PTR(key));
            TEST(IS_BAD_PTR(value));
        }
    }
    return 0;
}

static KOS_OBJ_ID alloc_object(KOS_CONTEXT  ctx,
                               uint32_t    *num_objs,
                               uint32_t    *total_size,
                               VERIFY_FUNC *verify)
{
    uint32_t    i;
    KOS_OBJ_ID  obj_id[5];
    OBJECT_DESC desc[5] = {
        { OBJ_OBJECT,         (uint32_t)sizeof(KOS_OBJECT)  },
        { OBJ_OBJECT_STORAGE, (uint32_t)(sizeof(KOS_OBJECT_STORAGE) + sizeof(KOS_PITEM) * 3) },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_STRING,         (uint32_t)sizeof(struct KOS_STRING_LOCAL_S) }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    kos_init_object(OBJPTR(OBJECT, obj_id[0]), obj_id[2]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT, obj_id[0])->props, obj_id[1]);
    KOS_object_set_private_ptr(obj_id[0], (void *)(intptr_t)44);

    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[1])->capacity,       4);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[1])->num_slots_used, 1);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[1])->num_slots_open, 0);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[1])->active_copies,  0);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, obj_id[1])->new_prop_table, KOS_BADPTR);

    for (i = 0; i < 4; i++) {
        KOS_PITEM *item = &OBJPTR(OBJECT_STORAGE, obj_id[1])->items[i];

        KOS_atomic_write_relaxed_u32(item->hash.hash, i);

        if (i == 2) {
            KOS_atomic_write_relaxed_ptr(item->key,   obj_id[4]);
            KOS_atomic_write_relaxed_ptr(item->value, obj_id[3]);
        }
        else {
            KOS_atomic_write_relaxed_ptr(item->key,   KOS_BADPTR);
            KOS_atomic_write_relaxed_ptr(item->value, KOS_BADPTR);
        }
    }

    OBJPTR(INTEGER, obj_id[2])->value = 45;
    OBJPTR(INTEGER, obj_id[3])->value = 46;

    OBJPTR(STRING, obj_id[4])->header.flags  = KOS_STRING_LOCAL;
    OBJPTR(STRING, obj_id[4])->header.length = (uint16_t)(sizeof(string_local_test) - 1);
    memcpy(&OBJPTR(STRING, obj_id[4])->local.data[0], string_local_test, sizeof(string_local_test) - 1);

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_object;

    return obj_id[0];
}

static void finalize_47(KOS_CONTEXT ctx, void *priv)
{
    *(int *)priv = 47;
}

static int verify_finalize(KOS_OBJ_ID obj_id)
{
    TEST(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT, obj_id)->props)));
    TEST(IS_BAD_PTR(OBJPTR(OBJECT, obj_id)->prototype));
    TEST(KOS_object_get_private_ptr(obj_id) != 0);
    return 0;
}

static int private_test = 1;

static KOS_OBJ_ID alloc_finalize(KOS_CONTEXT  ctx,
                                 uint32_t    *num_objs,
                                 uint32_t    *total_size,
                                 VERIFY_FUNC *verify)
{
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_OBJECT, sizeof(KOS_OBJECT));

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    kos_init_object(OBJPTR(OBJECT, obj_id), KOS_BADPTR);
    KOS_object_set_private_ptr(obj_id, &private_test);
    OBJPTR(OBJECT, obj_id)->finalize = finalize_47;

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_finalize;

    return obj_id;
}

static KOS_OBJ_ID handler(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    return args_obj;
}

static int verify_function(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION);
    TEST(OBJPTR(FUNCTION, obj_id)->flags      == KOS_FUN);
    TEST(OBJPTR(FUNCTION, obj_id)->num_args   == 1);
    TEST(OBJPTR(FUNCTION, obj_id)->num_regs   == 2);
    TEST(OBJPTR(FUNCTION, obj_id)->args_reg   == 3);
    TEST(OBJPTR(FUNCTION, obj_id)->state      == 0);
    TEST(OBJPTR(FUNCTION, obj_id)->instr_offs == 0);

    v = OBJPTR(FUNCTION, obj_id)->module;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 48);

    v = OBJPTR(FUNCTION, obj_id)->closures;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 49);

    v = OBJPTR(FUNCTION, obj_id)->defaults;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 50);

    TEST(OBJPTR(FUNCTION, obj_id)->handler == &handler);

    v = OBJPTR(FUNCTION, obj_id)->generator_stack_frame;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 51);

    return 0;
}

static KOS_OBJ_ID alloc_function(KOS_CONTEXT  ctx,
                                 uint32_t    *num_objs,
                                 uint32_t    *total_size,
                                 VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[5];
    OBJECT_DESC desc[5] = {
        { OBJ_FUNCTION, (uint32_t)sizeof(KOS_FUNCTION) },
        { OBJ_INTEGER,  (uint32_t)sizeof(KOS_INTEGER)  },
        { OBJ_INTEGER,  (uint32_t)sizeof(KOS_INTEGER)  },
        { OBJ_INTEGER,  (uint32_t)sizeof(KOS_INTEGER)  },
        { OBJ_INTEGER,  (uint32_t)sizeof(KOS_INTEGER)  }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(FUNCTION, obj_id[0])->flags      = KOS_FUN;
    OBJPTR(FUNCTION, obj_id[0])->num_args   = 1;
    OBJPTR(FUNCTION, obj_id[0])->num_regs   = 2;
    OBJPTR(FUNCTION, obj_id[0])->args_reg   = 3;
    OBJPTR(FUNCTION, obj_id[0])->state      = 0;
    OBJPTR(FUNCTION, obj_id[0])->instr_offs = 0;
    OBJPTR(FUNCTION, obj_id[0])->handler    = &handler;
    OBJPTR(FUNCTION, obj_id[0])->module     = obj_id[1];
    OBJPTR(FUNCTION, obj_id[0])->closures   = obj_id[2];
    OBJPTR(FUNCTION, obj_id[0])->defaults   = obj_id[3];
    OBJPTR(FUNCTION, obj_id[0])->generator_stack_frame = obj_id[4];

    OBJPTR(INTEGER, obj_id[1])->value = 48;
    OBJPTR(INTEGER, obj_id[2])->value = 49;
    OBJPTR(INTEGER, obj_id[3])->value = 50;
    OBJPTR(INTEGER, obj_id[4])->value = 51;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_function;

    return obj_id[0];
}

static int verify_class(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
    TEST(OBJPTR(CLASS, obj_id)->flags      == KOS_FUN);
    TEST(OBJPTR(CLASS, obj_id)->num_args   == 1);
    TEST(OBJPTR(CLASS, obj_id)->num_regs   == 2);
    TEST(OBJPTR(CLASS, obj_id)->args_reg   == 3);
    TEST(OBJPTR(CLASS, obj_id)->instr_offs == 0);

    v = OBJPTR(CLASS, obj_id)->module;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 52);

    v = OBJPTR(CLASS, obj_id)->closures;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 53);

    v = OBJPTR(CLASS, obj_id)->defaults;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 54);

    TEST(OBJPTR(CLASS, obj_id)->handler == &handler);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->prototype);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 55);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, obj_id)->props);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_OBJECT_STORAGE);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->capacity)       == 1);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->num_slots_used) == 0);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->num_slots_open) == 0);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, v)->active_copies)  == 0);
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->new_prop_table)));
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->items[0].key)));
    TEST(IS_BAD_PTR(KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, v)->items[0].value)));

    return 0;
}

static KOS_OBJ_ID alloc_class(KOS_CONTEXT  ctx,
                              uint32_t    *num_objs,
                              uint32_t    *total_size,
                              VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[6];
    OBJECT_DESC desc[6] = {
        { OBJ_CLASS,          (uint32_t)sizeof(KOS_CLASS)          },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER)        },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER)        },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER)        },
        { OBJ_INTEGER,        (uint32_t)sizeof(KOS_INTEGER)        },
        { OBJ_OBJECT_STORAGE, (uint32_t)sizeof(KOS_OBJECT_STORAGE) },
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(CLASS, obj_id[0])->flags      = KOS_FUN;
    OBJPTR(CLASS, obj_id[0])->num_args   = 1;
    OBJPTR(CLASS, obj_id[0])->num_regs   = 2;
    OBJPTR(CLASS, obj_id[0])->args_reg   = 3;
    OBJPTR(CLASS, obj_id[0])->instr_offs = 0;
    OBJPTR(CLASS, obj_id[0])->handler    = &handler;
    OBJPTR(CLASS, obj_id[0])->module     = obj_id[1];
    OBJPTR(CLASS, obj_id[0])->closures   = obj_id[2];
    OBJPTR(CLASS, obj_id[0])->defaults   = obj_id[3];
    KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, obj_id[0])->prototype, obj_id[4]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, obj_id[0])->props,     obj_id[5]);

    OBJPTR(INTEGER, obj_id[1])->value = 52;
    OBJPTR(INTEGER, obj_id[2])->value = 53;
    OBJPTR(INTEGER, obj_id[3])->value = 54;
    OBJPTR(INTEGER, obj_id[4])->value = 55;

    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[5])->capacity,       1);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[5])->num_slots_used, 0);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[5])->num_slots_open, 0);
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, obj_id[5])->active_copies,  0);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, obj_id[5])->new_prop_table, KOS_BADPTR);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, obj_id[5])->items[0].key,   KOS_BADPTR);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, obj_id[5])->items[0].value, KOS_BADPTR);

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_class;

    return obj_id[0];
}

static int verify_opaque(KOS_OBJ_ID obj_id)
{
    const uint8_t *ptr  = (const uint8_t *)OBJPTR(OPAQUE, obj_id) + sizeof(KOS_OPAQUE);
    const uint32_t size = 2U << KOS_OBJ_ALIGN_BITS;
    uint32_t       i;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_OPAQUE);

    for (i = 0; i < size; i++)
    {
        TEST(ptr[i] == i);
    }
    return 0;
}

static KOS_OBJ_ID alloc_opaque(KOS_CONTEXT  ctx,
                               uint32_t    *num_objs,
                               uint32_t    *total_size,
                               VERIFY_FUNC *verify)
{
    const uint32_t size   = 2U << KOS_OBJ_ALIGN_BITS;
    KOS_OBJ_ID     obj_id = alloc_page_with_object(ctx, OBJ_OPAQUE, sizeof(KOS_OPAQUE) + size);
    uint8_t       *ptr;
    uint32_t       i;

    if (IS_BAD_PTR(obj_id))
        return KOS_BADPTR;

    ptr = (uint8_t *)OBJPTR(OPAQUE, obj_id) + sizeof(KOS_OPAQUE);

    for (i = 0; i < size; i++)
        ptr[i] = (uint8_t)i;

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_opaque;

    return obj_id;
}

static int verify_dynamic_prop(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_DYNAMIC_PROP);

    v = OBJPTR(DYNAMIC_PROP, obj_id)->getter;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 56);

    v = OBJPTR(DYNAMIC_PROP, obj_id)->setter;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 57);

    return 0;
}

static KOS_OBJ_ID alloc_dynamic_prop(KOS_CONTEXT  ctx,
                                     uint32_t    *num_objs,
                                     uint32_t    *total_size,
                                     VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[3];
    OBJECT_DESC desc[3] = {
        { OBJ_DYNAMIC_PROP, (uint32_t)sizeof(KOS_DYNAMIC_PROP) },
        { OBJ_INTEGER,      (uint32_t)sizeof(KOS_INTEGER)      },
        { OBJ_INTEGER,      (uint32_t)sizeof(KOS_INTEGER)      }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(DYNAMIC_PROP, obj_id[0])->getter = obj_id[1];
    OBJPTR(DYNAMIC_PROP, obj_id[0])->setter = obj_id[2];

    OBJPTR(INTEGER, obj_id[1])->value = 56;
    OBJPTR(INTEGER, obj_id[2])->value = 57;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_dynamic_prop;

    return obj_id[0];
}

static int verify_object_walk(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT_WALK);

    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_WALK, obj_id)->index) == 58);

    v = OBJPTR(OBJECT_WALK, obj_id)->obj;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 59);

    v = OBJPTR(OBJECT_WALK, obj_id)->key_table;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 60);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, obj_id)->last_key);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 61);

    v = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, obj_id)->last_value);
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 62);

    return 0;
}

static KOS_OBJ_ID alloc_object_walk(KOS_CONTEXT  ctx,
                                    uint32_t    *num_objs,
                                    uint32_t    *total_size,
                                    VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[5];
    OBJECT_DESC desc[5] = {
        { OBJ_OBJECT_WALK, (uint32_t)sizeof(KOS_OBJECT_WALK) },
        { OBJ_INTEGER,     (uint32_t)sizeof(KOS_INTEGER)     },
        { OBJ_INTEGER,     (uint32_t)sizeof(KOS_INTEGER)     },
        { OBJ_INTEGER,     (uint32_t)sizeof(KOS_INTEGER)     },
        { OBJ_INTEGER,     (uint32_t)sizeof(KOS_INTEGER)     }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(OBJECT_WALK, obj_id[0])->obj       = obj_id[1];
    OBJPTR(OBJECT_WALK, obj_id[0])->key_table = obj_id[2];
    KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_WALK, obj_id[0])->index,      58);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, obj_id[0])->last_key,   obj_id[3]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, obj_id[0])->last_value, obj_id[4]);

    OBJPTR(INTEGER, obj_id[1])->value = 59;
    OBJPTR(INTEGER, obj_id[2])->value = 60;
    OBJPTR(INTEGER, obj_id[3])->value = 61;
    OBJPTR(INTEGER, obj_id[4])->value = 62;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_object_walk;

    return obj_id[0];
}

static int verify_module(KOS_OBJ_ID obj_id)
{
    KOS_OBJ_ID v;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_MODULE);

    v = OBJPTR(MODULE, obj_id)->name;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 63);

    v = OBJPTR(MODULE, obj_id)->path;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 64);

    v = OBJPTR(MODULE, obj_id)->constants;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 65);

    v = OBJPTR(MODULE, obj_id)->global_names;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 66);

    v = OBJPTR(MODULE, obj_id)->globals;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 67);

    v = OBJPTR(MODULE, obj_id)->module_names;
    TEST( ! IS_BAD_PTR(v));
    TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
    TEST(OBJPTR(INTEGER, v)->value == 68);

    return 0;
}

static KOS_OBJ_ID alloc_module(KOS_CONTEXT  ctx,
                               uint32_t    *num_objs,
                               uint32_t    *total_size,
                               VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[7];
    OBJECT_DESC desc[7] = {
        { OBJ_MODULE,  (uint32_t)sizeof(KOS_MODULE)  },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(MODULE, obj_id[0])->name           = obj_id[1];
    OBJPTR(MODULE, obj_id[0])->path           = obj_id[2];
    OBJPTR(MODULE, obj_id[0])->constants      = obj_id[3];
    OBJPTR(MODULE, obj_id[0])->global_names   = obj_id[4];
    OBJPTR(MODULE, obj_id[0])->globals        = obj_id[5];
    OBJPTR(MODULE, obj_id[0])->module_names   = obj_id[6];
    OBJPTR(MODULE, obj_id[0])->flags          = 0;
    OBJPTR(MODULE, obj_id[0])->inst           = 0;
    OBJPTR(MODULE, obj_id[0])->bytecode       = 0;
    OBJPTR(MODULE, obj_id[0])->line_addrs     = 0;
    OBJPTR(MODULE, obj_id[0])->func_addrs     = 0;
    OBJPTR(MODULE, obj_id[0])->num_line_addrs = 0;
    OBJPTR(MODULE, obj_id[0])->num_func_addrs = 0;
    OBJPTR(MODULE, obj_id[0])->bytecode_size  = 0;
    OBJPTR(MODULE, obj_id[0])->main_idx       = 0;

    OBJPTR(INTEGER, obj_id[1])->value = 63;
    OBJPTR(INTEGER, obj_id[2])->value = 64;
    OBJPTR(INTEGER, obj_id[3])->value = 65;
    OBJPTR(INTEGER, obj_id[4])->value = 66;
    OBJPTR(INTEGER, obj_id[5])->value = 67;
    OBJPTR(INTEGER, obj_id[6])->value = 68;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_module;

    return obj_id[0];
}

static int verify_stack(KOS_OBJ_ID obj_id)
{
    int i;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_STACK);
    TEST(OBJPTR(STACK, obj_id)->capacity == 4);
    TEST(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, obj_id)->size) == 4);

    for (i = 0; i < 4; i++)
    {
        const KOS_OBJ_ID v = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, obj_id)->buf[i]);

        TEST( ! IS_BAD_PTR(v));
        TEST(GET_OBJ_TYPE(v) == OBJ_INTEGER);
        TEST(OBJPTR(INTEGER, v)->value == 69 + i);
    }

    return 0;
}

static KOS_OBJ_ID alloc_stack(KOS_CONTEXT  ctx,
                              uint32_t    *num_objs,
                              uint32_t    *total_size,
                              VERIFY_FUNC *verify)
{
    KOS_OBJ_ID  obj_id[5];
    OBJECT_DESC desc[5] = {
        { OBJ_STACK,   (uint32_t)(sizeof(KOS_STACK) + 3U * sizeof(KOS_OBJ_ID)) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) },
        { OBJ_INTEGER, (uint32_t)sizeof(KOS_INTEGER) }
    };

    if (alloc_page_with_objects(ctx, obj_id, desc, NELEMS(obj_id)))
        return KOS_BADPTR;

    OBJPTR(STACK, obj_id[0])->capacity = 4;
    KOS_atomic_write_relaxed_u32(OBJPTR(STACK, obj_id[0])->size,   4);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, obj_id[0])->buf[0], obj_id[1]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, obj_id[0])->buf[1], obj_id[2]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, obj_id[0])->buf[2], obj_id[3]);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, obj_id[0])->buf[3], obj_id[4]);

    OBJPTR(INTEGER, obj_id[1])->value = 69;
    OBJPTR(INTEGER, obj_id[2])->value = 70;
    OBJPTR(INTEGER, obj_id[3])->value = 71;
    OBJPTR(INTEGER, obj_id[4])->value = 72;

    *num_objs   = NELEMS(obj_id);
    *total_size = get_obj_sizes(obj_id, NELEMS(obj_id));
    *verify     = &verify_stack;

    return obj_id[0];
}

static int verify_local_refs(KOS_OBJ_ID obj_id)
{
    size_t i;

    TEST(GET_OBJ_TYPE(obj_id) == OBJ_LOCAL_REFS);
    TEST(OBJPTR(LOCAL_REFS, obj_id)->next == KOS_BADPTR);

    for (i = 0; i < NELEMS(OBJPTR(LOCAL_REFS, obj_id)->refs); i++)
        TEST(OBJPTR(LOCAL_REFS, obj_id)->refs[i] == 0);

    return 0;
}

static KOS_OBJ_ID alloc_local_refs(KOS_CONTEXT  ctx,
                                   uint32_t    *num_objs,
                                   uint32_t    *total_size,
                                   VERIFY_FUNC *verify)
{
    size_t     i;
    KOS_OBJ_ID obj_id = alloc_page_with_object(ctx, OBJ_LOCAL_REFS, sizeof(KOS_LOCAL_REFS));

    if (obj_id == KOS_BADPTR)
        return KOS_BADPTR;

    OBJPTR(LOCAL_REFS, obj_id)->num_tracked = 0;
    OBJPTR(LOCAL_REFS, obj_id)->prev_scope  = 0;
    OBJPTR(LOCAL_REFS, obj_id)->next        = KOS_BADPTR;

    for (i = 0; i < NELEMS(OBJPTR(LOCAL_REFS, obj_id)->refs); i++)
        OBJPTR(LOCAL_REFS, obj_id)->refs[i] = 0;

    *num_objs   = 1;
    *total_size = get_obj_size(obj_id);
    *verify     = &verify_local_refs;

    return obj_id;
}

static int test_object(ALLOC_FUNC    alloc_object_func,
                       KOS_GC_STATS *orig_stats)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    KOS_OBJ_ID   prev_locals;
    KOS_OBJ_ID   obj_id;
    KOS_GC_STATS stats;
    VERIFY_FUNC  verify;
    int64_t      size;
    int          pushed = 0;
    uint32_t     f47    = 0;
    uint32_t     num_objs;
    uint32_t     total_size;

    /* Test case when the object is evacuated to existing page */

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

    TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

    obj_id = alloc_object_func(ctx, &num_objs, &total_size, &verify);
    TEST( ! IS_BAD_PTR(obj_id));
    TEST(verify(obj_id) == KOS_SUCCESS);

    size = get_obj_size(obj_id);

    if (GET_OBJ_TYPE(obj_id) == OBJ_OBJECT && OBJPTR(OBJECT, obj_id)->finalize == finalize_47)
        f47 = 1;

    TEST(!f47 || private_test == 1);

    TEST(KOS_push_locals(ctx, &pushed, 1, &obj_id) == KOS_SUCCESS);

    TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

    KOS_pop_locals(ctx, pushed);

    TEST(get_obj_size(obj_id) == size);
    TEST(verify(obj_id) == KOS_SUCCESS);

    TEST(!f47 || private_test == 1);

#ifndef CONFIG_MAD_GC
    TEST(stats.num_objs_evacuated == num_objs);
    TEST(stats.num_objs_freed     == 1);
    TEST(stats.num_objs_finalized == 0);
    TEST(stats.num_pages_kept     == 1);
    TEST(stats.num_pages_freed    == 1);
    TEST(stats.size_evacuated     == (unsigned)total_size);
    TEST(stats.size_freed         == (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS) - (unsigned)total_size);
    TEST(stats.size_kept          == orig_stats->size_kept);
#endif

    KOS_instance_destroy(&inst);

    /* Test case when the object is destroyed */

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

    TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

    obj_id = alloc_object_func(ctx, &num_objs, &total_size, &verify);
    TEST( ! IS_BAD_PTR(obj_id));
    TEST(verify(obj_id) == KOS_SUCCESS);

    TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

#ifndef CONFIG_MAD_GC
    TEST(stats.num_objs_evacuated == 0);
    TEST(stats.num_objs_freed     == num_objs + 1);
    TEST(stats.num_objs_finalized == f47);
    TEST(stats.num_pages_kept     == 1);
    TEST(stats.num_pages_freed    == 1);
    TEST(stats.size_evacuated     == 0);
    TEST(stats.size_freed         == (KOS_SLOTS_PER_PAGE << KOS_OBJ_ALIGN_BITS));
    TEST(stats.size_kept          == orig_stats->size_kept);
#endif

    TEST(!f47 || private_test == 47);
    private_test = 1;

    KOS_instance_destroy(&inst);

    return KOS_SUCCESS;
}

typedef struct CHECKSUMMED_OPAQUE_S {
    KOS_OBJ_HEADER header;
    uint32_t       checksum;
    uint8_t        data[1];
} CHECKSUMMED_OPAQUE;

static uint32_t calc_checksum(KOS_OBJ_ID obj)
{
    CHECKSUMMED_OPAQUE *opaque = (CHECKSUMMED_OPAQUE *)OBJPTR(OPAQUE, obj);
    uint8_t            *ptr    = &opaque->data[0];
    uint8_t            *end;
    uint32_t            size;
    uint32_t            checksum = ~0U;

    size = kos_get_object_size(opaque->header) - sizeof(CHECKSUMMED_OPAQUE) + 1;
    end  = ptr + size;

    do {
        const uint32_t b = *(ptr++);
        checksum -= b;
    } while (ptr != end);

    return checksum;
}

static void fill_opaque_with_random(KOS_OBJ_ID      obj,
                                    struct KOS_RNG *rng)
{
    CHECKSUMMED_OPAQUE *opaque = (CHECKSUMMED_OPAQUE *)OBJPTR(OPAQUE, obj);
    uint8_t            *ptr    = &opaque->data[0];
    uint8_t            *end;
    uint32_t            size;

    size = kos_get_object_size(opaque->header) - sizeof(CHECKSUMMED_OPAQUE) + 1;
    end  = ptr + size;

    do {
        uint64_t r = kos_rng_random(rng);
        uint32_t j;

        for (j = 0; ptr != end && j < 8; j++) {
            *(ptr++) = (uint8_t)r;
            r >>= 8;
        }
    } while (ptr != end);

    opaque->checksum = calc_checksum(obj);
}

static int verify_opaque_checksum(KOS_OBJ_ID obj)
{
    CHECKSUMMED_OPAQUE *opaque = (CHECKSUMMED_OPAQUE *)OBJPTR(OPAQUE, obj);

    TEST(opaque->checksum == calc_checksum(obj));

    return KOS_SUCCESS;
}

static int alloc_full_pages(KOS_CONTEXT     ctx,
                            struct KOS_RNG *rng,
                            KOS_OBJ_ID     *array,
                            uint32_t        max_pages,
                            uint32_t       *num_pages_allocated)
{
    uint32_t capacity  = 0;
    uint32_t i         = 0;
    uint32_t num_pages = 0;

    assert(max_pages > 0U);

    *array = alloc_array_storage_page(ctx);
    if (IS_BAD_PTR(*array)) {
        *num_pages_allocated = 0;
        return KOS_SUCCESS;
    }

    capacity = OBJPTR(ARRAY_STORAGE, *array)->capacity;
    ++num_pages;

    while (num_pages < max_pages) {
        KOS_OBJ_ID next_obj;

        if (i == capacity)
            next_obj = alloc_array_storage_page(ctx);
        else
            next_obj = OBJID(OPAQUE, (KOS_OPAQUE *)kos_alloc_object_page(ctx, OBJ_OPAQUE));

        if (IS_BAD_PTR(next_obj)) {
            TEST_EXCEPTION();
            break;
        }

        ++num_pages;

        if (i == capacity) {
            TEST(KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY_STORAGE, *array)->capacity) == capacity);

            write_array_storage(next_obj, 0, *array);

            *array = next_obj;
            i      = 1;
        }
        else {
            write_array_storage(*array, i++, next_obj);

            fill_opaque_with_random(next_obj, rng);
        }
    }

    *num_pages_allocated = num_pages;
    return KOS_SUCCESS;
}

static int verify_full_pages(KOS_OBJ_ID array)
{
    for (;;) {
        const uint32_t capacity = OBJPTR(ARRAY_STORAGE, array)->capacity;
        uint32_t       i;
        KOS_OBJ_ID     obj;

        for (i = 1; i < capacity; i++) {
            obj = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, array)->buf[i]);

            if (IS_BAD_PTR(obj))
                break;

            TEST(GET_OBJ_TYPE(obj) == OBJ_OPAQUE);

            TEST(verify_opaque_checksum(obj) == KOS_SUCCESS);
        }

        obj = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, array)->buf[0]);

        if (GET_OBJ_TYPE(obj) == OBJ_OPAQUE) {
            TEST(verify_opaque_checksum(obj) == KOS_SUCCESS);
            break;
        }

        array = obj;
    }

    return KOS_SUCCESS;
}

int main(void)
{
    KOS_INSTANCE   inst;
    KOS_CONTEXT    ctx;
    KOS_GC_STATS   base_stats;
    struct KOS_RNG rng;
    unsigned       max_pages = 0;

    kos_rng_init(&rng);

    /************************************************************************/
    /* Test garbage collection on a freshly initialized instance */
    {
        KOS_OBJ_ID prev_locals;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

        TEST(KOS_collect_garbage(ctx, &base_stats) == KOS_SUCCESS);

#ifndef CONFIG_MAD_GC
        TEST(base_stats.num_objs_evacuated == 0);
        TEST(base_stats.num_objs_freed     == 0);
        TEST(base_stats.num_objs_finalized == 0);
        TEST(base_stats.num_pages_kept     == 1); /* 1 page on 64-bit architecture with 4KB pages */
        TEST(base_stats.num_pages_freed    == 0);
        TEST(base_stats.size_evacuated     == 0);
        TEST(base_stats.size_freed         == 0);
        TEST(base_stats.size_kept          >  0);
#endif

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
        TEST(test_object(alloc_integer,      &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_float,        &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_string_local, &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_string_ptr,   &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_string_ref,   &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_empty_array,  &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_array,        &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_empty_buffer, &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_buffer,       &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_empty_object, &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_object,       &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_finalize,     &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_function,     &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_class,        &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_opaque,       &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_dynamic_prop, &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_object_walk,  &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_module,       &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_stack,        &base_stats) == KOS_SUCCESS);
        TEST(test_object(alloc_local_refs,   &base_stats) == KOS_SUCCESS);
    }

    /************************************************************************/
    /* Test release of current thread page */
    {
        KOS_GC_STATS stats;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_new_array(ctx, 0) != KOS_BADPTR);

        kos_heap_release_thread_page(ctx);

        TEST(KOS_new_array(ctx, 0) != KOS_BADPTR);

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

#ifndef CONFIG_MAD_GC
        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 0);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 1);
        TEST(stats.num_pages_freed    == 0);
        TEST(stats.size_evacuated     == base_stats.size_evacuated);
        TEST(stats.size_freed         == 0);
        TEST(stats.size_kept          >  0);
#endif

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test internal tracked refs */
    {
        KOS_GC_STATS stats;
        KOS_OBJ_ID   obj_id    = KOS_BADPTR;
        int          finalized = 0;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        kos_track_refs(ctx, 1, &obj_id);

        obj_id = KOS_new_object(ctx);
        KOS_object_set_private_ptr(obj_id, &finalized);
        OBJPTR(OBJECT, obj_id)->finalize = finalize_47;

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(finalized == 0);

#ifndef CONFIG_MAD_GC
        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 0);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == 1);
        TEST(stats.num_pages_freed    == 0);
        TEST(stats.size_evacuated     == 0);
        TEST(stats.size_freed         == 0);
        TEST(stats.size_kept          == base_stats.size_kept + get_obj_size(obj_id));
#endif

        kos_untrack_refs(ctx, 1);

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

        TEST(finalized == 47);

#ifndef CONFIG_MAD_GC
        TEST(stats.num_objs_evacuated == base_stats.num_objs_evacuated);
        TEST(stats.num_objs_freed     == 0);
        TEST(stats.num_objs_finalized == 1);
        TEST(stats.num_pages_kept     == 1);
        TEST(stats.num_pages_freed    == 0);
        TEST(stats.size_evacuated     == 0);
        TEST(stats.size_freed         == 0);
        TEST(stats.size_kept          == base_stats.size_kept);
#endif

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test local refs */
    {
        KOS_OBJ_ID prev_locals;
        int        finalized[NELEMS(((KOS_LOCAL_REFS *)0)->refs)];
        KOS_OBJ_ID obj_id[NELEMS(((KOS_LOCAL_REFS *)0)->refs)];
        size_t     i;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

        for (i = 0; i < NELEMS(((KOS_LOCAL_REFS *)0)->refs); i++) {
            int pushed = 0;

            obj_id[i] = KOS_new_object(ctx);
            TEST( ! IS_BAD_PTR(obj_id[i]));

            TEST(KOS_push_locals(ctx, &pushed, 1, &obj_id[i]) == KOS_SUCCESS);

            finalized[i] = 0;
            KOS_object_set_private_ptr(obj_id[i], &finalized[i]);
            OBJPTR(OBJECT, obj_id[i])->finalize = finalize_47;
        }

        TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

        for (i = 0; i < NELEMS(((KOS_LOCAL_REFS *)0)->refs); i++)
            TEST(finalized[i] == 0);


        for (i = 0; i < NELEMS(((KOS_LOCAL_REFS *)0)->refs); i++)
            KOS_pop_locals(ctx, 1);

        TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

        for (i = 0; i < NELEMS(((KOS_LOCAL_REFS *)0)->refs); i++)
            TEST(finalized[i] == 47);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Test object finalization when destroying instance */
    {
        KOS_OBJ_ID prev_locals;
        KOS_OBJ_ID obj_id    = KOS_BADPTR;
        int        finalized = 0;
        int        pushed    = 0;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);

        obj_id = KOS_new_object(ctx);
        TEST( ! IS_BAD_PTR(obj_id));

        TEST(KOS_push_locals(ctx, &pushed, 1, &obj_id) == KOS_SUCCESS);

        KOS_object_set_private_ptr(obj_id, &finalized);
        OBJPTR(OBJECT, obj_id)->finalize = finalize_47;

        TEST(KOS_collect_garbage(ctx, 0) == KOS_SUCCESS);

        TEST(finalized == 0);

        KOS_instance_destroy(&inst);

        TEST(finalized == 47);
    }

    /************************************************************************/
    /* Allocate as many pages as possible, up to OOM */
    {
        KOS_OBJ_ID prev_locals;
        KOS_OBJ_ID array  = KOS_BADPTR;
        int        pushed = 0;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);
        TEST(KOS_push_locals(ctx, &pushed, 1, &array) == KOS_SUCCESS);

        TEST(alloc_full_pages(ctx, &rng, &array, ~0U, &max_pages) == KOS_SUCCESS);

        TEST(max_pages == (KOS_MAX_HEAP_SIZE >> KOS_PAGE_BITS) - 1);

        TEST(verify_full_pages(array) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Allocate all pages minus one, so that there is no space for evac */
    {
        KOS_OBJ_ID   prev_locals;
        KOS_OBJ_ID   array     = KOS_BADPTR;
        int          pushed    = 0;
        uint32_t     num_pages = 0;
        KOS_GC_STATS stats;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);
        TEST(KOS_push_locals(ctx, &pushed, 1, &array) == KOS_SUCCESS);

        TEST(alloc_full_pages(ctx, &rng, &array, max_pages - 1U, &num_pages) == KOS_SUCCESS);

        TEST(num_pages == max_pages - 1U);

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

#ifndef CONFIG_MAD_GC
        TEST(stats.num_objs_evacuated == 0);
        TEST(stats.num_objs_freed     == 0);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == max_pages);
        TEST(stats.num_pages_freed    == 0);
        TEST(stats.size_evacuated     == 0);
        TEST(stats.size_freed         == 0);
        TEST(stats.size_kept          >  base_stats.size_kept);
#endif

        TEST(verify_full_pages(array) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    /************************************************************************/
    /* Allocate all pages minus two, then evac */
    {
        KOS_OBJ_ID   prev_locals;
        KOS_OBJ_ID   array     = KOS_BADPTR;
        int          pushed    = 0;
        uint32_t     num_pages = 0;
        KOS_GC_STATS stats;
        uint32_t     i;
        uint32_t     num_freed = (100U - KOS_MIGRATION_THRESH) * (KOS_PAGE_SIZE >> KOS_OBJ_ALIGN_BITS) / 100U;

        TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

        TEST(KOS_push_local_scope(ctx, &prev_locals) == KOS_SUCCESS);
        TEST(KOS_push_locals(ctx, &pushed, 1, &array) == KOS_SUCCESS);

        TEST(alloc_full_pages(ctx, &rng, &array, max_pages - 2U, &num_pages) == KOS_SUCCESS);

        TEST(num_pages == max_pages - 2U);

        /* Trigger evacuation */
        for (i = 0; i < num_freed; i++)
            TEST( ! IS_BAD_PTR(KOS_new_cstring(ctx, "abc")));

        TEST(KOS_collect_garbage(ctx, &stats) == KOS_SUCCESS);

#ifndef CONFIG_MAD_GC
        TEST(stats.num_objs_freed     == num_freed);
        TEST(stats.num_objs_finalized == 0);
        TEST(stats.num_pages_kept     == num_pages);
        TEST(stats.num_pages_freed    == 1);
        TEST(stats.size_evacuated     == base_stats.size_kept);
        TEST(stats.size_freed         == num_freed << KOS_OBJ_ALIGN_BITS);
        TEST(stats.size_kept          >  base_stats.size_kept);
#endif

        TEST(verify_full_pages(array) == KOS_SUCCESS);

        KOS_instance_destroy(&inst);
    }

    return 0;
}

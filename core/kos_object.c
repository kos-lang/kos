/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_object.h"
#include "../inc/kos_atomic.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"

/*
 * Object properties are held in a hash table.  The lock-free algorithm for
 * managing the hash table is loosely based on the algorithm presented by
 * dr Cliff Click, but simplified.
 *
 * Here is a diagram of what the slots in the object's hash table can contain:
 *
 *                              resize
 *                              +-------> {K, R}
 *                              |            |resize
 *                              |            |
 *                     write    |   write    v
 *   start ---> {0, T} ----> {K, T} <---> {K, V}
 *                 |            |            |
 *           resize|      resize|      resize|
 *                 v            v            |
 *              {0, C}       {K, C} <--------+
 *
 *  0 - KOS_BADPTR, indicates an empty/unused slot
 *  T - TOMBSTONE, indicates a deleted property
 *  C - CLOSED, indicates that the property's value was salvaged to new table
 *      during resize
 *  R - RESERVED, indicates a reserved slot in the new table during resize
 *  K - Some key.  When a slot is allocated for a given key, this key stays in
 *      this table forever, it never changes.
 *  V - Some value.  Values can change over time.  When a property is deleted,
 *      TOMBSTONE is written as a value.
 */

KOS_DECLARE_STATIC_CONST_STRING(str_err_no_own_properties, "object has no own properties");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_string,        "property name is not a string");

DECLARE_STATIC_CONST_OBJECT(tombstone, OBJ_OPAQUE, 0xB0);
DECLARE_STATIC_CONST_OBJECT(closed,    OBJ_OPAQUE, 0xB1);
DECLARE_STATIC_CONST_OBJECT(reserved,  OBJ_OPAQUE, 0xB2);

/* When a key is deleted, it remains in the table, but its value is marked
 * as TOMBSTONE. */
#define TOMBSTONE KOS_CONST_ID(tombstone)
/* When the hash table is too small and is being resized, values moved to the
 * new table are marked as CLOSED in the old table. */
#define CLOSED    KOS_CONST_ID(closed)
/* During resize operation, slots in the new table are marked as reserved,
 * which is part of strategy to avoid race conditions. */
#define RESERVED  KOS_CONST_ID(reserved)

KOS_OBJ_ID KOS_new_object(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    return KOS_new_object_with_prototype(ctx, inst->prototypes.object_proto);
}

KOS_OBJ_ID KOS_new_object_with_prototype(KOS_CONTEXT ctx,
                                         KOS_OBJ_ID  prototype_obj)
{
    KOS_OBJECT *obj;
    KOS_LOCAL   prototype;

    KOS_init_local_with(ctx, &prototype, prototype_obj);

    obj = (KOS_OBJECT *)kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_OBJECT, sizeof(KOS_OBJECT));

    if (obj) {
        assert(kos_get_object_type(obj->header) == OBJ_OBJECT);
        kos_init_object(obj, prototype.o);
    }

    KOS_destroy_top_local(ctx, &prototype);

    return OBJID(OBJECT, obj);
}

KOS_OBJ_ID KOS_new_object_with_private(KOS_CONTEXT       ctx,
                                       KOS_OBJ_ID        prototype_obj,
                                       KOS_PRIVATE_CLASS priv_class,
                                       KOS_FINALIZE      finalize)
{
    KOS_OBJECT *obj;
    KOS_LOCAL   prototype;

    KOS_init_local_with(ctx, &prototype, prototype_obj);

    obj = (KOS_OBJECT *)kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_OBJECT, sizeof(KOS_OBJECT_WITH_PRIVATE));

    if (obj) {
        assert(kos_get_object_type(obj->header) == OBJ_OBJECT);
        kos_init_object(obj, prototype.o);

        ((KOS_OBJECT_WITH_PRIVATE *)obj)->priv_class = priv_class;
        ((KOS_OBJECT_WITH_PRIVATE *)obj)->finalize   = finalize;

        KOS_atomic_write_relaxed_ptr(((KOS_OBJECT_WITH_PRIVATE *)obj)->priv, (void *)KOS_NULL);
    }

    KOS_destroy_top_local(ctx, &prototype);

    return OBJID(OBJECT, obj);
}

static KOS_ATOMIC(KOS_OBJ_ID) *get_properties(KOS_OBJ_ID obj_id)
{
    KOS_ATOMIC(KOS_OBJ_ID) *props;

    switch (GET_OBJ_TYPE(obj_id)) {

        case OBJ_OBJECT:
            props = &OBJPTR(OBJECT, obj_id)->props;
            break;

        case OBJ_CLASS:
            props = &OBJPTR(CLASS, obj_id)->props;
            break;

        default:
            props = KOS_NULL;
            break;
    }

    return props;
}

static int has_properties(KOS_OBJ_ID obj_id)
{
    const KOS_TYPE type = GET_OBJ_TYPE(obj_id);

    return type == OBJ_OBJECT || type == OBJ_CLASS;
}

static KOS_OBJ_ID alloc_buffer(KOS_CONTEXT ctx, unsigned capacity)
{
    KOS_OBJECT_STORAGE *const storage = (KOS_OBJECT_STORAGE *)
            kos_alloc_object(ctx,
                             KOS_ALLOC_MOVABLE,
                             OBJ_OBJECT_STORAGE,
                             sizeof(KOS_OBJECT_STORAGE) +
                                   (capacity - 1) * sizeof(KOS_PITEM));

    if (storage) {
        assert(kos_get_object_type(storage->header) == OBJ_OBJECT_STORAGE);
    }

    return OBJID(OBJECT_STORAGE, storage);
}

void kos_init_object(KOS_OBJECT *obj, KOS_OBJ_ID prototype)
{
    obj->prototype = prototype;
    KOS_atomic_write_relaxed_ptr(obj->props, KOS_BADPTR);
}

static int is_key_equal(KOS_OBJ_ID key,
                        uint32_t   hash,
                        KOS_OBJ_ID prop_key,
                        KOS_PITEM *prop_item)
{
    if (key == prop_key)
        return 1;
    else {
        const uint32_t prop_hash = KOS_atomic_read_relaxed_u32(prop_item->hash.hash);

        if (prop_hash && hash != prop_hash)
            return 0;

        return ! KOS_string_compare(key, prop_key);
    }
}

static KOS_OBJ_ID read_props(KOS_ATOMIC(KOS_OBJ_ID) *ptr)
{
    return KOS_atomic_read_acquire_obj(*ptr);
}

static int salvage_item(KOS_CONTEXT ctx,
                        KOS_PITEM  *old_item,
                        KOS_OBJ_ID  new_table,
                        uint32_t    new_capacity)
{
    KOS_OBJ_ID     key;
    KOS_OBJ_ID     value;
    KOS_PITEM     *new_item;
    const uint32_t mask = new_capacity - 1;
    uint32_t       hash;
    uint32_t       idx;
    int            ret;

    /* Attempt to close an empty or deleted slot early */
    if (KOS_atomic_cas_strong_ptr(old_item->value, TOMBSTONE, CLOSED))
        return 1;

    value = KOS_atomic_read_relaxed_obj(old_item->value);
    if (value == CLOSED)
        return 0;

    key  = KOS_atomic_read_relaxed_obj(old_item->key);
    assert( ! IS_BAD_PTR(key));
    hash = KOS_atomic_read_relaxed_u32(old_item->hash.hash);
    idx  = hash & mask;

    /* Claim a slot in the new table */
    for (;;) {

        KOS_OBJ_ID dest_key;

        new_item = OBJPTR(OBJECT_STORAGE, new_table)->items + idx;

        if (KOS_atomic_cas_strong_ptr(new_item->key, KOS_BADPTR, key)) {
            KOS_atomic_write_relaxed_u32(new_item->hash.hash, hash);
            KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, new_table)->num_slots_used, 1);
            break;
        }

        /* This slot in the new table is already taken */
        dest_key = KOS_atomic_read_relaxed_obj(new_item->key);
        assert( ! IS_BAD_PTR(dest_key));
        if (is_key_equal(key, hash, dest_key, new_item))
            /* Someone already wrote this key to the new table */
            break;

        idx = (idx + 1) & mask;
    }

    /* Mark the value as reserved */
    if ( ! KOS_atomic_cas_strong_ptr(new_item->value, TOMBSTONE, RESERVED))
        /* Another thread salvaged this slot */
        return 0;

    /* Get the value from the old table and close the slot */
    value = (KOS_OBJ_ID)KOS_atomic_swap_ptr(old_item->value, CLOSED);
    if (value == CLOSED) {
        /* While this thread has reserved a slot in the new table,
         * another thread went the fast path at the top of this function
         * and closed the slot quickly.  Now we will attempt to mark
         * the slot as deleted in the new table. */
        value = TOMBSTONE;
        ret   = 0;
    }
    else
        ret = 1; /* Slot successfuly closed */

    /* Store the value in the new table, unless another thread already
     * wrote something newer */
    if (KOS_atomic_cas_strong_ptr(new_item->value, RESERVED, value))
        return ret;

    return ret;
}

static void copy_table(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  src_obj_id,
                       KOS_OBJ_ID  old_table,
                       KOS_OBJ_ID  new_table)
{
    const uint32_t old_capacity = KOS_atomic_read_relaxed_u32(
                                    OBJPTR(OBJECT_STORAGE, old_table)->capacity);
    const uint32_t new_capacity = KOS_atomic_read_relaxed_u32(
                                    OBJPTR(OBJECT_STORAGE, new_table)->capacity);
    const uint32_t mask         = old_capacity - 1;
    const uint32_t fuzz         = 64U * (old_capacity - KOS_atomic_read_relaxed_u32(
                                    OBJPTR(OBJECT_STORAGE, old_table)->num_slots_open));
    uint32_t       i            = fuzz & mask;

    KOS_ATOMIC(KOS_OBJ_ID) *props;

    KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, old_table)->active_copies, 1);

    for (;;) {
        if (salvage_item(ctx,
                         OBJPTR(OBJECT_STORAGE, old_table)->items + i,
                         new_table,
                         new_capacity)) {
            KOS_PERF_CNT(object_salvage_success);
            if (KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, old_table)->num_slots_open,
                                   -1) == 1)
                break;
        }
        /* Early exit if another thread has finished salvaging */
        else {
            KOS_PERF_CNT(object_salvage_fail);
            if ( ! KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table)->num_slots_open))
                break;
        }

        /* Next slot */
        i = (i + 1) & mask;
    }

    /* Avoid race when one thread marks a slot as reserved in the new
     * table while another thread deletes the original item and
     * closes the source slot. */
    if (KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, old_table)->active_copies, -1) > 1) {
        while (KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table)->active_copies))
            kos_yield();
    }

    props = get_properties(src_obj_id);
    if (KOS_atomic_cas_strong_ptr(*props, old_table, new_table)) {
#ifndef NDEBUG
        for (i = 0; i < old_capacity; i++) {
            KOS_PITEM *item  = OBJPTR(OBJECT_STORAGE, old_table)->items + i;
            KOS_OBJ_ID value = KOS_atomic_read_relaxed_obj(item->value);
            assert(value == CLOSED);
        }
#endif
    }
}

static int need_resize(KOS_OBJ_ID table, unsigned num_reprobes)
{
    /* Determine if resize is needed based on the number of reprobes */
#if KOS_MAX_PROP_REPROBES * 2 <= KOS_MIN_PROPS_CAPACITY
    assert( ! IS_BAD_PTR(table));
    if (num_reprobes < KOS_MAX_PROP_REPROBES)
        return 0;
#else
    uint32_t capacity;
    uint32_t usage;

    assert( ! IS_BAD_PTR(table));
    capacity = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, table)->capacity);

    if (capacity >= KOS_MAX_PROP_REPROBES * 2 && num_reprobes < KOS_MAX_PROP_REPROBES)
        return 0;

    /* For small property tables use a simpler heuristic */
    usage = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, table)->num_slots_used);
    if (usage * 4 < capacity * 3)
        return 0;
#endif
    return 1;
}

static int resize_prop_table(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id,
                             KOS_OBJ_ID  old_table_obj,
                             uint32_t    grow_factor)
{
    int            error        = KOS_SUCCESS;
    const uint32_t old_capacity = IS_BAD_PTR(old_table_obj) ? 0U :
        KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table_obj)->capacity);
    const uint32_t new_capacity = old_capacity ? old_capacity * grow_factor
                                               : KOS_MIN_PROPS_CAPACITY;
    KOS_OBJ_ID     new_table    = KOS_BADPTR;

    if ( ! IS_BAD_PTR(old_table_obj))
        new_table = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, old_table_obj)->new_prop_table);

    if ( ! IS_BAD_PTR(new_table)) {
        /* Another thread is already resizing the property table, help it */
        copy_table(ctx, obj_id, old_table_obj, new_table);

        KOS_PERF_CNT(object_resize_success);
    }
    else {

        KOS_LOCAL obj;
        KOS_LOCAL old_table;

        KOS_init_local_with(ctx, &obj, obj_id);
        KOS_init_local_with(ctx, &old_table, old_table_obj);

        new_table = alloc_buffer(ctx, new_capacity);

        if ( ! IS_BAD_PTR(new_table)) {
            unsigned i;

            KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->capacity,       new_capacity);
            KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->num_slots_used, 0);
            KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->num_slots_open, new_capacity);
            KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->active_copies,  0);
            KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, new_table)->new_prop_table, KOS_BADPTR);

            for (i = 0; i < new_capacity; i++) {
                KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, new_table)->items[i].key,       KOS_BADPTR);
                KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->items[i].hash.hash, 0);
                KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_STORAGE, new_table)->items[i].value,     TOMBSTONE);
            }

            if ( ! IS_BAD_PTR(old_table.o)) {
                if (KOS_atomic_cas_strong_ptr(OBJPTR(OBJECT_STORAGE, old_table.o)->new_prop_table,
                                              KOS_BADPTR,
                                              new_table)) {

                    copy_table(ctx, obj.o, old_table.o, new_table);

                    KOS_PERF_CNT(object_resize_success);
                }
                /* Somebody already resized it */
                else {
                    /* Help copy the new table if it is still being resized */
                    if (KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table.o)->active_copies)) {
                        new_table = KOS_atomic_read_relaxed_obj(
                                OBJPTR(OBJECT_STORAGE, old_table.o)->new_prop_table);
                        copy_table(ctx, obj.o, old_table.o, new_table);
                    }

                    KOS_PERF_CNT(object_resize_fail);
                }
            }
            else {
                KOS_ATOMIC(KOS_OBJ_ID) *props = get_properties(obj.o);

                if ( ! KOS_atomic_cas_strong_ptr(*props,
                                                 KOS_BADPTR,
                                                 new_table)) {
                    /* Somebody already resized it */
                    KOS_PERF_CNT(object_resize_fail);
                }
            }
        }
        else
            error = KOS_ERROR_EXCEPTION;

        KOS_destroy_top_locals(ctx, &old_table, &obj);
    }

    return error;
}

static void raise_no_property(KOS_CONTEXT ctx, KOS_OBJ_ID prop)
{
    KOS_VECTOR prop_cstr;

    KOS_vector_init(&prop_cstr);

    if ( ! KOS_string_to_cstr_vec(ctx, prop, &prop_cstr))
        KOS_raise_printf(ctx, "no such property: \"%s\"", prop_cstr.buffer);

    KOS_vector_destroy(&prop_cstr);
}

KOS_OBJ_ID KOS_get_property_with_depth(KOS_CONTEXT      ctx,
                                       KOS_OBJ_ID       obj_id,
                                       KOS_OBJ_ID       prop,
                                       enum KOS_DEPTH_E shallow)
{
    KOS_OBJ_ID retval = KOS_BADPTR;

    assert( ! IS_BAD_PTR(obj_id));
    assert( ! IS_BAD_PTR(prop));

    if (GET_OBJ_TYPE(prop) != OBJ_STRING)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
    else {
        KOS_ATOMIC(KOS_OBJ_ID) *props = get_properties(obj_id);

        /* Find non-empty property table in this object or in a prototype */
        if ( ! shallow) {
            while ( ! props || IS_BAD_PTR(read_props(props))) {
                obj_id = KOS_get_prototype(ctx, obj_id);

                if (IS_BAD_PTR(obj_id)) {
                    props = KOS_NULL;
                    break;
                }

                props = get_properties(obj_id);
            }
        }
        else if (props && IS_BAD_PTR(read_props(props)))
            props = KOS_NULL;

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop);
            unsigned       idx          = hash;
            KOS_OBJ_ID     prop_table   = read_props(props);
            KOS_PITEM     *items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
            unsigned       num_reprobes = KOS_atomic_read_relaxed_u32(
                                            OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
            unsigned       mask         = num_reprobes - 1;

            for (;;) {
                KOS_PITEM *const cur_item  = items + (idx &= mask);
                KOS_OBJ_ID       cur_key   = KOS_atomic_read_relaxed_obj(cur_item->key);
                const KOS_OBJ_ID cur_value = KOS_atomic_read_acquire_obj(cur_item->value);

                /* Object property table is being resized, so read value from the new table */
                if (cur_value == CLOSED) {
                    /* Help copy the old table to avoid races when it is partially copied */
                    KOS_OBJ_ID new_prop_table = KOS_atomic_read_relaxed_obj(
                            OBJPTR(OBJECT_STORAGE, prop_table)->new_prop_table);
                    assert( ! IS_BAD_PTR(new_prop_table));

                    copy_table(ctx, obj_id, prop_table, new_prop_table);

                    idx          = hash;
                    prop_table   = new_prop_table;
                    items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
                    num_reprobes = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
                    mask         = num_reprobes - 1;
                    continue;
                }

                /* Key found */
                if ( ! IS_BAD_PTR(cur_key) && is_key_equal(prop, hash, cur_key, cur_item)) {

                    if (cur_value != TOMBSTONE) {
                        assert(cur_value != RESERVED);
                        retval = cur_value;
                        break;
                    }

                    /* Key deleted or write incomplete, will look in prototype */
                    cur_key = KOS_BADPTR;
                }

                /* Assume key not found if too many reprobes */
                if ( ! num_reprobes)
                    cur_key = KOS_BADPTR;

                /* If no such key, look in prototypes */
                if (IS_BAD_PTR(cur_key)) {

                    /* Find non-empty property table in a prototype */
                    if ( ! shallow) {
                        do {
                            obj_id = KOS_get_prototype(ctx, obj_id);

                            if (IS_BAD_PTR(obj_id)) /* end of prototype chain */
                                break;

                            props = get_properties(obj_id);
                        } while ( ! props || IS_BAD_PTR(read_props(props)));
                    }
                    else
                        obj_id = KOS_BADPTR;

                    if (IS_BAD_PTR(obj_id)) {
                        raise_no_property(ctx, prop);
                        break;
                    }
                    assert(props);

                    idx          = hash;
                    prop_table   = read_props(props);
                    items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
                    num_reprobes = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
                    mask         = num_reprobes - 1;
                }
                else {
                    /* Probe next slot */
                    ++idx;
                    --num_reprobes;
                }
            }
        }
        else
            raise_no_property(ctx, prop);
    }

    if ( ! IS_BAD_PTR(retval))
        KOS_PERF_CNT(object_get_success);
    else
        KOS_PERF_CNT(object_get_fail);

    return retval;
}

int kos_object_copy_prop_table(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id)
{
    KOS_ATOMIC(KOS_OBJ_ID) *props;

    assert( ! IS_BAD_PTR(obj_id));
    assert(has_properties(obj_id));

    props = get_properties(obj_id);

    return resize_prop_table(ctx, obj_id, props ? read_props(props) : KOS_BADPTR, 1U);
}

int KOS_set_property(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  obj_id,
                     KOS_OBJ_ID  prop_obj,
                     KOS_OBJ_ID  value_obj)
{
    KOS_LOCAL obj;
    KOS_LOCAL prop;
    KOS_LOCAL value;
    int       error = KOS_ERROR_EXCEPTION;

    assert( ! IS_BAD_PTR(obj_id));
    assert( ! IS_BAD_PTR(prop_obj));
    assert( ! IS_BAD_PTR(value_obj));

    KOS_init_local_with(ctx, &obj,   obj_id);
    KOS_init_local_with(ctx, &prop,  prop_obj);
    KOS_init_local_with(ctx, &value, value_obj);

    if (GET_OBJ_TYPE(prop.o) != OBJ_STRING)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
    else if ( ! has_properties(obj.o))
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_no_own_properties));
    else {
        KOS_ATOMIC(KOS_OBJ_ID) *props;

        props = get_properties(obj.o);

        /* Check if property table is non-empty */
        if (IS_BAD_PTR(read_props(props))) {

            /* It is OK to delete non-existent props even if property table is empty */
            if (value.o == TOMBSTONE) {
                error = KOS_SUCCESS;
                props = KOS_NULL;
            }
            /* Allocate property table */
            else {
                error = resize_prop_table(ctx, obj.o, KOS_BADPTR, 0U);
                if ( ! error) {
                    error = KOS_ERROR_EXCEPTION;
                    props = get_properties(obj.o);
                }
                else {
                    assert(KOS_is_exception_pending(ctx));
                    props = KOS_NULL;
                }
            }
        }
#ifdef CONFIG_MAD_GC
        else {
            error = kos_trigger_mad_gc(ctx);
            props = error ? KOS_NULL : get_properties(obj.o);
        }
#endif

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop.o);
            unsigned       idx          = hash;
            unsigned       num_reprobes = 0;
            KOS_OBJ_ID     prop_table   = read_props(props);
            KOS_PITEM     *items;
            unsigned       mask;
            #ifdef CONFIG_PERF
            int            collis_depth = -1;
            #endif

            items = OBJPTR(OBJECT_STORAGE, prop_table)->items;
            mask  = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity) - 1;

            for (;;) {
                KOS_PITEM *cur_item = items + (idx &= mask);
                KOS_OBJ_ID cur_key  = KOS_atomic_read_relaxed_obj(cur_item->key);
                KOS_OBJ_ID oldval;

                #ifdef CONFIG_PERF
                ++collis_depth;
                #endif

                /* Found a new empty slot */
                if (IS_BAD_PTR(cur_key)) {

                    /* If we are deleting a non-existent property, just bail */
                    if (value.o == TOMBSTONE) {
                        error = KOS_SUCCESS;
                        break;
                    }

                    /* Attempt to write the new key */
                    if ( ! KOS_atomic_cas_weak_ptr(cur_item->key, KOS_BADPTR, prop.o))
                        /* Reprobe the slot if another thread has written a key */
                        continue;

                    KOS_PERF_CNT_ARRAY(object_collision, KOS_min(collis_depth, 3));

                    KOS_atomic_write_relaxed_u32(cur_item->hash.hash, hash);
                    KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, prop_table)->num_slots_used, 1);
                }
                /* Otherwise check if this key matches the sought property */
                else if ( ! is_key_equal(prop.o, hash, cur_key, cur_item)) {

                    /* Resize if property table is full */
                    if (num_reprobes > KOS_MAX_PROP_REPROBES) {
                        error = resize_prop_table(ctx, obj.o, prop_table, 2U);
                        if (error)
                            break;

                        props        = get_properties(obj.o);
                        prop_table   = read_props(props);
                        idx          = hash;
                        items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
                        mask         = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity) - 1;
                        num_reprobes = 0;
                    }
                    /* Probe next slot */
                    else {
                        ++idx;
                        ++num_reprobes;
                    }
                    continue;
                }

                /* Read value at this slot */
                oldval = KOS_atomic_read_acquire_obj(cur_item->value);

                /* We will use the new table if it was copied */
                if (oldval != CLOSED) {

                    /* If this is a dynamic property, throw it */
                    if ( ! IS_BAD_PTR(oldval) &&
                        GET_OBJ_TYPE(oldval) == OBJ_DYNAMIC_PROP &&
                        value.o != TOMBSTONE) {

                        KOS_raise_exception(ctx, oldval);
                        error = KOS_ERROR_SETTER;
                        break;
                    }

                    /* It's OK if someone else wrote in the mean time */
                    if ( ! KOS_atomic_cas_strong_ptr(cur_item->value, oldval, value.o))
                        /* Re-read in case it was moved to the new table */
                        oldval = KOS_atomic_read_acquire_obj(cur_item->value);
                }

                /* Another thread is resizing the table - use new property table */
                if (oldval == CLOSED) {
                    const KOS_OBJ_ID new_prop_table = KOS_atomic_read_relaxed_obj(
                            OBJPTR(OBJECT_STORAGE, prop_table)->new_prop_table);
                    assert( ! IS_BAD_PTR(new_prop_table));

                    copy_table(ctx, obj.o, prop_table, new_prop_table);

                    prop_table   = new_prop_table;
                    idx          = hash;
                    items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
                    mask         = KOS_atomic_read_relaxed_u32(
                                    OBJPTR(OBJECT_STORAGE, prop_table)->capacity) - 1;
                    num_reprobes = 0;
                    continue;
                }

                error = KOS_SUCCESS;
                break;
            }

            /* Check if we need to resize the table */
            if ( ! error && need_resize(prop_table, num_reprobes))
                error = resize_prop_table(ctx, obj.o, prop_table, 2U);
        }
    }

#ifdef CONFIG_PERF
    if (value.o == TOMBSTONE) {
        if (error)
            KOS_PERF_CNT(object_delete_fail);
        else
            KOS_PERF_CNT(object_delete_success);
    }
    else {
        if (error)
            KOS_PERF_CNT(object_set_fail);
        else
            KOS_PERF_CNT(object_set_success);
    }
#endif

    KOS_destroy_top_locals(ctx, &value, &obj);

    return error;
}

int KOS_delete_property(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  obj_id,
                        KOS_OBJ_ID  prop)
{
    assert( ! IS_BAD_PTR(obj_id));
    assert( ! IS_BAD_PTR(prop));

    if (GET_OBJ_TYPE(prop) != OBJ_STRING) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_string));
        return KOS_ERROR_EXCEPTION;
    }
    else if ( ! has_properties(obj_id))
        return KOS_SUCCESS;
    else
        return KOS_set_property(ctx, obj_id, prop, TOMBSTONE);
}

static KOS_OBJ_ID new_builtin_dynamic_prop(KOS_CONTEXT          ctx,
                                           KOS_OBJ_ID           module_obj,
                                           KOS_OBJ_ID           name_obj,
                                           KOS_FUNCTION_HANDLER getter,
                                           KOS_FUNCTION_HANDLER setter)
{
    KOS_LOCAL dyn_prop;
    KOS_LOCAL name;

    assert(getter);
    assert( ! kos_is_heap_object(module_obj));

    KOS_init_local(     ctx, &dyn_prop);
    KOS_init_local_with(ctx, &name,     name_obj);

    dyn_prop.o = KOS_new_dynamic_prop(ctx);

    if ( ! IS_BAD_PTR(dyn_prop.o)) {

        KOS_OBJ_ID func_obj = KOS_new_function(ctx);

        if ( ! IS_BAD_PTR(func_obj)) {
            OBJPTR(FUNCTION, func_obj)->module        = module_obj;
            OBJPTR(FUNCTION, func_obj)->opts.min_args = 0;
            OBJPTR(FUNCTION, func_obj)->handler       = getter;
            OBJPTR(FUNCTION, func_obj)->name          = name.o;
            OBJPTR(DYNAMIC_PROP, dyn_prop.o)->getter  = func_obj;
        }
        else
            dyn_prop.o = KOS_BADPTR;
    }

    if ( ! IS_BAD_PTR(dyn_prop.o)) {

        if (setter) {

            KOS_OBJ_ID func_obj = KOS_new_function(ctx);

            if ( ! IS_BAD_PTR(func_obj)) {
                OBJPTR(FUNCTION, func_obj)->module        = module_obj;
                OBJPTR(FUNCTION, func_obj)->opts.min_args = 0;
                OBJPTR(FUNCTION, func_obj)->handler       = setter;
                OBJPTR(FUNCTION, func_obj)->name          = name.o;
                OBJPTR(DYNAMIC_PROP, dyn_prop.o)->setter  = func_obj;
            }
            else
                dyn_prop.o = KOS_BADPTR;
        }
    }

    return KOS_destroy_top_locals(ctx, &name, &dyn_prop);
}

int KOS_set_builtin_dynamic_property(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_OBJ_ID           prop_obj,
                                     KOS_OBJ_ID           module_obj,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID dyn_prop;
    KOS_LOCAL  obj;
    KOS_LOCAL  prop;

    KOS_init_local_with(ctx, &obj,  obj_id);
    KOS_init_local_with(ctx, &prop, prop_obj);
    assert( ! kos_is_heap_object(module_obj));

    dyn_prop = new_builtin_dynamic_prop(ctx, module_obj, prop.o, getter, setter);

    if ( ! IS_BAD_PTR(dyn_prop))
        error = KOS_set_property(ctx, obj.o, prop.o, dyn_prop);
    else
        error = KOS_ERROR_EXCEPTION;

    KOS_destroy_top_locals(ctx, &prop, &obj);

    return error;
}

KOS_OBJ_ID KOS_get_prototype(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id)
{
    KOS_OBJ_ID    ret  = KOS_BADPTR;
    KOS_INSTANCE *inst = ctx->inst;

    assert( ! IS_BAD_PTR(obj_id));

    if (IS_SMALL_INT(obj_id))
        ret = inst->prototypes.integer_proto;

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            ret = inst->prototypes.integer_proto;
            break;

        case OBJ_FLOAT:
            ret = inst->prototypes.float_proto;
            break;

        case OBJ_OBJECT:
            ret = OBJPTR(OBJECT, obj_id)->prototype;
            break;

        case OBJ_STRING:
            ret = inst->prototypes.string_proto;
            break;

        case OBJ_ARRAY:
            ret = inst->prototypes.array_proto;
            break;

        case OBJ_BUFFER:
            ret = inst->prototypes.buffer_proto;
            break;

        case OBJ_FUNCTION: {
            const KOS_FUNCTION_STATE state = (KOS_FUNCTION_STATE)
                KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, obj_id)->state);
            if (state == KOS_FUN)
                ret = inst->prototypes.function_proto;
            else
                ret = inst->prototypes.generator_proto;
            break;
        }

        case OBJ_CLASS:
            ret = inst->prototypes.class_proto;
            break;

        case OBJ_BOOLEAN:
            ret = inst->prototypes.boolean_proto;
            break;

        case OBJ_VOID:
            ret = KOS_BADPTR;
            break;

        default:
            ret = inst->prototypes.object_proto;
            break;
    }

    return ret;
}

int KOS_has_prototype(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      KOS_OBJ_ID  proto_id)
{
    do {
        obj_id = KOS_get_prototype(ctx, obj_id);
        if (obj_id == proto_id)
            return 1;
    } while ( ! IS_BAD_PTR(obj_id));

    return 0;
}

void *KOS_object_get_private(KOS_OBJ_ID obj, KOS_PRIVATE_CLASS priv_class)
{
    KOS_OBJECT_WITH_PRIVATE *obj_ptr;

    if (GET_OBJ_TYPE(obj) != OBJ_OBJECT)
        return KOS_NULL;

    obj_ptr = (KOS_OBJECT_WITH_PRIVATE *)OBJPTR(OBJECT, obj);

    if (kos_get_object_size(obj_ptr->header) < sizeof(KOS_OBJECT_WITH_PRIVATE))
        return KOS_NULL;

    if (obj_ptr->priv_class != priv_class)
        return KOS_NULL;

    return KOS_atomic_read_relaxed_ptr(obj_ptr->priv);
}

void* KOS_object_swap_private(KOS_OBJ_ID obj, KOS_PRIVATE_CLASS priv_class, void *new_priv)
{
    KOS_OBJECT_WITH_PRIVATE *obj_ptr;

    if (GET_OBJ_TYPE(obj) != OBJ_OBJECT)
        return new_priv;

    obj_ptr = (KOS_OBJECT_WITH_PRIVATE *)OBJPTR(OBJECT, obj);

    if (kos_get_object_size(obj_ptr->header) < sizeof(KOS_OBJECT_WITH_PRIVATE))
        return new_priv;

    if (obj_ptr->priv_class != priv_class)
        return new_priv;

    return KOS_atomic_swap_ptr(obj_ptr->priv, new_priv);
}

KOS_OBJ_ID kos_new_object_walk(KOS_CONTEXT      ctx,
                               KOS_OBJ_ID       obj_id,
                               enum KOS_DEPTH_E depth)
{
    int                     error = KOS_SUCCESS;
    KOS_LOCAL               obj;
    KOS_LOCAL               walk;
    KOS_ATOMIC(KOS_OBJ_ID) *props;

    KOS_init_locals(ctx, 2, &obj, &walk);
    obj.o = obj_id;

    walk.o = OBJID(ITERATOR,
                   (KOS_ITERATOR *)kos_alloc_object(ctx,
                                                    KOS_ALLOC_MOVABLE,
                                                    OBJ_ITERATOR,
                                                    sizeof(KOS_ITERATOR)));
    if (IS_BAD_PTR(walk.o))
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    assert(READ_OBJ_TYPE(walk.o) == OBJ_ITERATOR);

    props = get_properties(obj.o);

    OBJPTR(ITERATOR, walk.o)->index         = 0;
    OBJPTR(ITERATOR, walk.o)->depth         = (uint8_t)depth;
    OBJPTR(ITERATOR, walk.o)->type          = OBJ_OBJECT;
    OBJPTR(ITERATOR, walk.o)->obj           = obj.o;
    OBJPTR(ITERATOR, walk.o)->prop_obj      = obj.o;
    OBJPTR(ITERATOR, walk.o)->key_table     = props ? read_props(props) : KOS_BADPTR;
    OBJPTR(ITERATOR, walk.o)->returned_keys = KOS_BADPTR;
    OBJPTR(ITERATOR, walk.o)->last_key      = KOS_BADPTR;
    OBJPTR(ITERATOR, walk.o)->last_value    = KOS_BADPTR;

    if (depth == KOS_DEEP) {
        const KOS_OBJ_ID keys = KOS_new_object(ctx);
        TRY_OBJID(keys);
        KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->returned_keys, keys);
    }

cleanup:
    walk.o = KOS_destroy_top_locals(ctx, &obj, &walk);

    if (error)
        walk.o = KOS_BADPTR; /* Object is garbage collected */

    return walk.o;
}

int kos_object_walk(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  iterator_id)
{
    int       error    = KOS_ERROR_INTERNAL;
    uint32_t  capacity = 0;
    KOS_LOCAL walk;
    KOS_LOCAL table;
    KOS_LOCAL returned_keys;
    KOS_LOCAL key;

    KOS_init_locals(ctx, 4, &walk, &table, &returned_keys, &key);
    walk.o = iterator_id;

    assert(GET_OBJ_TYPE(walk.o) == OBJ_ITERATOR);
    assert(OBJPTR(ITERATOR, walk.o)->type == OBJ_OBJECT ||
           OBJPTR(ITERATOR, walk.o)->type == OBJ_CLASS);

    returned_keys.o = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, walk.o)->returned_keys);

    table.o = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, walk.o)->key_table);
    if ( ! IS_BAD_PTR(table.o))
        capacity = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, table.o)->capacity);

    for (;;) {

        KOS_OBJ_ID     value;
        const uint32_t index = KOS_atomic_add_u32(OBJPTR(ITERATOR, walk.o)->index, 1U);

        if (index >= capacity) {

            if (OBJPTR(ITERATOR, walk.o)->depth == KOS_DEEP) {

                KOS_OBJ_ID obj_id;

                for (;;) {
                    KOS_ATOMIC(KOS_OBJ_ID) *props;

                    obj_id = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, walk.o)->prop_obj);
                    if (IS_BAD_PTR(obj_id))
                        break;

                    obj_id = KOS_get_prototype(ctx, obj_id);
                    if (IS_BAD_PTR(obj_id))
                        break;

                    KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->prop_obj, obj_id);

                    props = get_properties(obj_id);
                    if ( ! props)
                        continue;

                    table.o = read_props(props);
                    if (IS_BAD_PTR(table.o))
                        continue;

                    capacity = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, table.o)->capacity);

                    KOS_atomic_write_relaxed_u32(OBJPTR(ITERATOR, walk.o)->index, 0U);
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, walk.o)->key_table, table.o);
                    break;
                }

                if ( ! IS_BAD_PTR(obj_id))
                    continue;
            }

            KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_key,   KOS_BADPTR);
            KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_value, KOS_BADPTR);
            error = KOS_ERROR_NOT_FOUND;
            break;
        }

        key.o = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, table.o)->items[index].key);

        if (IS_BAD_PTR(key.o))
            continue;

        if ( ! IS_BAD_PTR(returned_keys.o)) {

            if ( ! IS_BAD_PTR(KOS_get_property_shallow(ctx, returned_keys.o, key.o)))
                continue;

            KOS_clear_exception(ctx);

            error = KOS_set_property(ctx, returned_keys.o, key.o, KOS_VOID);
            if (error)
                break;
        }

        value = KOS_atomic_read_acquire_obj(OBJPTR(OBJECT_STORAGE, table.o)->items[index].value);

        assert( ! IS_BAD_PTR(value));

        if (value == TOMBSTONE)
            continue;

        if ((value != CLOSED) && (value != RESERVED)) {
            KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_key,   key.o);
            KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_value, value);
            error = KOS_SUCCESS;
            break;
        }

        value = KOS_get_property_shallow(ctx, OBJPTR(ITERATOR, walk.o)->prop_obj, key.o);

        if (IS_BAD_PTR(value)) {
            KOS_clear_exception(ctx);
            continue;
        }

        KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_key,   key.o);
        KOS_atomic_write_relaxed_ptr(OBJPTR(ITERATOR, walk.o)->last_value, value);
        error = KOS_SUCCESS;
        break;
    }

    KOS_destroy_top_locals(ctx, &walk, &key);

    return error;
}

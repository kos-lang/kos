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

#include "../inc/kos_object.h"
#include "../inc/kos_atomic.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
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

static const char str_err_null_ptr[]          = "null pointer";
static const char str_err_not_string[]        = "property name is not a string";
static const char str_err_no_property[]       = "no such property";
static const char str_err_no_own_properties[] = "object has no own properties";

DECLARE_STATIC_CONST_OBJECT(tombstone) = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xB0);
DECLARE_STATIC_CONST_OBJECT(closed)    = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xB1);
DECLARE_STATIC_CONST_OBJECT(reserved)  = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xB2);

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
                                         KOS_OBJ_ID  prototype)
{
    KOS_OBJECT *obj;

    kos_track_refs(ctx, 1, &prototype);

    obj = (KOS_OBJECT *)kos_alloc_object(ctx, OBJ_OBJECT, sizeof(KOS_OBJECT));

    kos_untrack_refs(ctx, 1);

    if (obj) {
        assert(obj->header.type == OBJ_OBJECT);
        kos_init_object(obj, prototype);
    }

    return OBJID(OBJECT, obj);
}

static KOS_ATOMIC(KOS_OBJ_ID) *_get_properties(KOS_OBJ_ID obj_id)
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
            props = 0;
            break;
    }

    return props;
}

static int _has_properties(KOS_OBJ_ID obj_id)
{
    const KOS_TYPE type = GET_OBJ_TYPE(obj_id);

    return type == OBJ_OBJECT || type == OBJ_CLASS;
}

static KOS_OBJ_ID _alloc_buffer(KOS_CONTEXT ctx, unsigned capacity)
{
    KOS_OBJECT_STORAGE *const storage = (KOS_OBJECT_STORAGE *)
            kos_alloc_object(ctx,
                             OBJ_OBJECT_STORAGE,
                             sizeof(KOS_OBJECT_STORAGE) +
                                   (capacity - 1) * sizeof(KOS_PITEM));

    if (storage) {
        assert(storage->header.type == OBJ_OBJECT_STORAGE);
    }

    return OBJID(OBJECT_STORAGE, storage);
}

void kos_init_object(KOS_OBJECT *obj, KOS_OBJ_ID prototype)
{
    obj->prototype = prototype;
    obj->finalize  = 0;

    KOS_atomic_write_relaxed_ptr(obj->priv,  TO_SMALL_INT(0));
    KOS_atomic_write_relaxed_ptr(obj->props, KOS_BADPTR);
}

static int _is_key_equal(KOS_OBJ_ID key,
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

static KOS_OBJ_ID _read_props(KOS_ATOMIC(KOS_OBJ_ID) *ptr)
{
    return KOS_atomic_read_acquire_obj(*ptr);
}

static int _salvage_item(KOS_CONTEXT ctx,
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
        if (_is_key_equal(key, hash, dest_key, new_item))
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

static void _copy_table(KOS_CONTEXT ctx,
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
        if (_salvage_item(ctx,
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

    props = _get_properties(src_obj_id);
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

static int _need_resize(KOS_OBJ_ID table, unsigned num_reprobes)
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

static int _resize_prop_table(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id,
                              KOS_OBJ_ID  old_table,
                              uint32_t    grow_factor)
{
    int            error        = KOS_SUCCESS;
    const uint32_t old_capacity = IS_BAD_PTR(old_table) ? 0U :
        KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table)->capacity);
    const uint32_t new_capacity = old_capacity ? old_capacity * grow_factor
                                               : KOS_MIN_PROPS_CAPACITY;
    KOS_OBJ_ID     new_table    = KOS_BADPTR;

    if ( ! IS_BAD_PTR(old_table))
        new_table = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, old_table)->new_prop_table);

    if ( ! IS_BAD_PTR(new_table)) {
        /* Another thread is already resizing the property table, help it */
        _copy_table(ctx, obj_id, old_table, new_table);

        KOS_PERF_CNT(object_resize_success);
    }
    else {

        kos_track_refs(ctx, 2, &obj_id, &old_table);

        new_table = _alloc_buffer(ctx, new_capacity);

        kos_untrack_refs(ctx, 2);

        if ( ! IS_BAD_PTR(new_table)) {
            unsigned i;

            KOS_atomic_write_relaxed_u32(OBJPTR(OBJECT_STORAGE, new_table)->capacity, new_capacity);
            OBJPTR(OBJECT_STORAGE, new_table)->num_slots_used = 0;
            OBJPTR(OBJECT_STORAGE, new_table)->num_slots_open = new_capacity;
            OBJPTR(OBJECT_STORAGE, new_table)->active_copies  = 0;
            OBJPTR(OBJECT_STORAGE, new_table)->new_prop_table = KOS_BADPTR;

            for (i = 0; i < new_capacity; i++) {
                OBJPTR(OBJECT_STORAGE, new_table)->items[i].key       = KOS_BADPTR;
                OBJPTR(OBJECT_STORAGE, new_table)->items[i].hash.hash = 0;
                OBJPTR(OBJECT_STORAGE, new_table)->items[i].value     = TOMBSTONE;
            }

            if ( ! IS_BAD_PTR(old_table)) {
                if (KOS_atomic_cas_strong_ptr(OBJPTR(OBJECT_STORAGE, old_table)->new_prop_table,
                                              KOS_BADPTR,
                                              new_table)) {

                    _copy_table(ctx, obj_id, old_table, new_table);

                    KOS_PERF_CNT(object_resize_success);
                }
                /* Somebody already resized it */
                else {
                    /* Help copy the new table if it is still being resized */
                    if (KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, old_table)->active_copies)) {
                        new_table = KOS_atomic_read_relaxed_obj(
                                OBJPTR(OBJECT_STORAGE, old_table)->new_prop_table);
                        _copy_table(ctx, obj_id, old_table, new_table);
                    }

                    KOS_PERF_CNT(object_resize_fail);
                }
            }
            else {
                KOS_ATOMIC(KOS_OBJ_ID) *props = _get_properties(obj_id);

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
    }

    return error;
}

KOS_OBJ_ID KOS_get_property(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            KOS_OBJ_ID  prop)
{
    KOS_OBJ_ID retval = KOS_BADPTR;

    if (IS_BAD_PTR(obj_id) || IS_BAD_PTR(prop))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(prop) != OBJ_STRING)
        KOS_raise_exception_cstring(ctx, str_err_not_string);
    else {
        KOS_ATOMIC(KOS_OBJ_ID) *props = _get_properties(obj_id);

        /* Find non-empty property table in this object or in a prototype */
        while ( ! props || IS_BAD_PTR(_read_props(props))) {
            obj_id = KOS_get_prototype(ctx, obj_id);

            if (IS_BAD_PTR(obj_id)) {
                props = 0;
                break;
            }

            props = _get_properties(obj_id);
        }

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop);
            unsigned       idx          = hash;
            KOS_OBJ_ID     prop_table   = _read_props(props);
            KOS_PITEM     *items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
            unsigned       num_reprobes = KOS_atomic_read_relaxed_u32(
                                            OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
            unsigned       mask         = num_reprobes - 1;

            for (;;) {
                KOS_PITEM *const cur_item  = items + (idx &= mask);
                KOS_OBJ_ID       cur_key   = KOS_atomic_read_relaxed_obj(cur_item->key);
                const KOS_OBJ_ID cur_value = KOS_atomic_read_relaxed_obj(cur_item->value);

                /* Object property table is being resized, so read value from the new table */
                if (cur_value == CLOSED) {
                    /* Help copy the old table to avoid races when it is partially copied */
                    KOS_OBJ_ID new_prop_table = KOS_atomic_read_relaxed_obj(
                            OBJPTR(OBJECT_STORAGE, prop_table)->new_prop_table);
                    assert( ! IS_BAD_PTR(new_prop_table));

                    _copy_table(ctx, obj_id, prop_table, new_prop_table);

                    idx          = hash;
                    prop_table   = new_prop_table;
                    items        = OBJPTR(OBJECT_STORAGE, prop_table)->items;
                    num_reprobes = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
                    mask         = num_reprobes - 1;
                    continue;
                }

                /* Key found */
                if ( ! IS_BAD_PTR(cur_key) && _is_key_equal(prop, hash, cur_key, cur_item)) {

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
                    do {
                        obj_id = KOS_get_prototype(ctx, obj_id);

                        if (IS_BAD_PTR(obj_id)) /* end of prototype chain */
                            break;

                        props = _get_properties(obj_id);
                    } while ( ! props || IS_BAD_PTR(_read_props(props)));

                    if (IS_BAD_PTR(obj_id)) {
                        KOS_raise_exception_cstring(ctx, str_err_no_property);
                        break;
                    }
                    assert(props);

                    idx          = hash;
                    prop_table   = _read_props(props);
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
            KOS_raise_exception_cstring(ctx, str_err_no_property);
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
    assert(_has_properties(obj_id));

    props = _get_properties(obj_id);

    return _resize_prop_table(ctx, obj_id, props ? _read_props(props) : KOS_BADPTR, 1U);
}

int KOS_set_property(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  obj_id,
                     KOS_OBJ_ID  prop,
                     KOS_OBJ_ID  value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj_id) || IS_BAD_PTR(prop) || IS_BAD_PTR(value))
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
    else if (GET_OBJ_TYPE(prop) != OBJ_STRING)
        KOS_raise_exception_cstring(ctx, str_err_not_string);
    else if ( ! _has_properties(obj_id))
        KOS_raise_exception_cstring(ctx, str_err_no_own_properties);
    else {
        KOS_ATOMIC(KOS_OBJ_ID) *props;

        kos_track_refs(ctx, 3, &obj_id, &prop, &value);

        props = _get_properties(obj_id);

        /* Check if property table is non-empty */
        if (IS_BAD_PTR(_read_props(props))) {

            /* It is OK to delete non-existent props even if property table is empty */
            if (value == TOMBSTONE) {
                error = KOS_SUCCESS;
                props = 0;
            }
            /* Allocate property table */
            else {
                error = _resize_prop_table(ctx, obj_id, KOS_BADPTR, 0U);
                if ( ! error) {
                    error = KOS_ERROR_EXCEPTION;
                    props = _get_properties(obj_id);
                }
                else {
                    assert(KOS_is_exception_pending(ctx));
                    props = 0;
                }
            }
        }
#ifdef CONFIG_MAD_GC
        else {
            error = kos_trigger_mad_gc(ctx);
            props = error ? 0 : _get_properties(obj_id);
        }
#endif

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop);
            unsigned       idx          = hash;
            unsigned       num_reprobes = 0;
            KOS_OBJ_ID     prop_table   = _read_props(props);
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
                    if (value == TOMBSTONE) {
                        error = KOS_SUCCESS;
                        break;
                    }

                    /* Attempt to write the new key */
                    if ( ! KOS_atomic_cas_weak_ptr(cur_item->key, KOS_BADPTR, prop))
                        /* Reprobe the slot if another thread has written a key */
                        continue;

                    KOS_PERF_CNT_ARRAY(object_collision, KOS_min(collis_depth, 3));

                    KOS_atomic_write_relaxed_u32(cur_item->hash.hash, hash);
                    KOS_atomic_add_i32(OBJPTR(OBJECT_STORAGE, prop_table)->num_slots_used, 1);
                }
                /* Otherwise check if this key matches the sought property */
                else if ( ! _is_key_equal(prop, hash, cur_key, cur_item)) {

                    /* Resize if property table is full */
                    if (num_reprobes > KOS_MAX_PROP_REPROBES) {
                        error = _resize_prop_table(ctx, obj_id, prop_table, 2U);
                        if (error)
                            break;

                        props        = _get_properties(obj_id);
                        prop_table   = _read_props(props);
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
                oldval = KOS_atomic_read_relaxed_obj(cur_item->value);

                /* We will use the new table if it was copied */
                if (oldval != CLOSED) {

                    /* If this is a dynamic property, throw it */
                    if ( ! IS_BAD_PTR(oldval) &&
                        GET_OBJ_TYPE(oldval) == OBJ_DYNAMIC_PROP &&
                        value != TOMBSTONE) {

                        KOS_raise_exception(ctx, oldval);
                        error = KOS_ERROR_SETTER;
                        break;
                    }

                    /* It's OK if someone else wrote in the mean time */
                    if ( ! KOS_atomic_cas_strong_ptr(cur_item->value, oldval, value))
                        /* Re-read in case it was moved to the new table */
                        oldval = KOS_atomic_read_relaxed_obj(cur_item->value);
                }

                /* Another thread is resizing the table - use new property table */
                if (oldval == CLOSED) {
                    const KOS_OBJ_ID new_prop_table = KOS_atomic_read_relaxed_obj(
                            OBJPTR(OBJECT_STORAGE, prop_table)->new_prop_table);
                    assert( ! IS_BAD_PTR(new_prop_table));

                    _copy_table(ctx, obj_id, prop_table, new_prop_table);

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
            if ( ! error && _need_resize(prop_table, num_reprobes))
                error = _resize_prop_table(ctx, obj_id, prop_table, 2U);
        }

        kos_untrack_refs(ctx, 3);
    }

#ifdef CONFIG_PERF
    if (value == TOMBSTONE) {
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

    return error;
}

int KOS_delete_property(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  obj_id,
                        KOS_OBJ_ID  prop)
{
    if (IS_BAD_PTR(prop)) {
        KOS_raise_exception_cstring(ctx, str_err_null_ptr);
        return KOS_ERROR_EXCEPTION;
    }
    else if (GET_OBJ_TYPE(prop) != OBJ_STRING) {
        KOS_raise_exception_cstring(ctx, str_err_not_string);
        return KOS_ERROR_EXCEPTION;
    }
    else if ( ! IS_BAD_PTR(obj_id) && ! _has_properties(obj_id))
        return KOS_SUCCESS;
    else
        return KOS_set_property(ctx, obj_id, prop, TOMBSTONE);
}

int KOS_set_builtin_dynamic_property(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_OBJ_ID           prop,
                                     KOS_OBJ_ID           module_obj,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID dyn_prop;

    kos_track_refs(ctx, 3, &obj_id, &prop, &module_obj);

    dyn_prop = KOS_new_builtin_dynamic_prop(ctx, module_obj, getter, setter);

    kos_untrack_refs(ctx, 3);

    if ( ! IS_BAD_PTR(dyn_prop))
        error = KOS_set_property(ctx, obj_id, prop, dyn_prop);
    else
        error = KOS_ERROR_EXCEPTION;

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
            const KOS_FUNCTION_STATE state =
                (KOS_FUNCTION_STATE)OBJPTR(FUNCTION, obj_id)->state;
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

KOS_OBJ_ID KOS_new_object_walk(KOS_CONTEXT                  ctx,
                               KOS_OBJ_ID                   obj_id,
                               enum KOS_OBJECT_WALK_DEPTH_E deep)
{
    int        error         = KOS_SUCCESS;
    KOS_OBJ_ID walk_id       = KOS_BADPTR;
    KOS_OBJ_ID key_table_obj = KOS_BADPTR;
    KOS_OBJ_ID prop_table    = KOS_BADPTR;

    kos_track_refs(ctx, 4, &obj_id, &walk_id, &key_table_obj, &prop_table);

    walk_id = OBJID(OBJECT_WALK,
                    (KOS_OBJECT_WALK *)kos_alloc_object(ctx,
                                                        OBJ_OBJECT_WALK,
                                                        sizeof(KOS_OBJECT_WALK)));
    if (IS_BAD_PTR(walk_id))
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    OBJPTR(OBJECT_WALK, walk_id)->header.type = OBJ_OBJECT_WALK;
    OBJPTR(OBJECT_WALK, walk_id)->obj         = obj_id;
    OBJPTR(OBJECT_WALK, walk_id)->key_table   = KOS_BADPTR;
    OBJPTR(OBJECT_WALK, walk_id)->index       = 0;
    OBJPTR(OBJECT_WALK, walk_id)->last_key    = KOS_BADPTR;
    OBJPTR(OBJECT_WALK, walk_id)->last_value  = KOS_BADPTR;

    key_table_obj = KOS_new_object(ctx);
    TRY_OBJID(key_table_obj);

    do {
        uint32_t i;
        uint32_t capacity;

        KOS_ATOMIC(KOS_OBJ_ID) *props = _get_properties(obj_id);

        obj_id = KOS_get_prototype(ctx, obj_id);

        if ( ! props)
            continue;

        prop_table = _read_props(props);

        if (IS_BAD_PTR(prop_table))
            continue;

        capacity = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity);

        for (i = 0; i < capacity; i++) {
            KOS_PITEM       *cur_item = OBJPTR(OBJECT_STORAGE, prop_table)->items + i;
            const KOS_OBJ_ID key      = KOS_atomic_read_relaxed_obj(cur_item->key);
            const KOS_OBJ_ID value    = KOS_atomic_read_relaxed_obj(cur_item->value);

            if (IS_BAD_PTR(key) || value == TOMBSTONE)
                continue;

            if (value == CLOSED) {
                const KOS_OBJ_ID new_prop_table = KOS_atomic_read_relaxed_obj(
                        OBJPTR(OBJECT_STORAGE, prop_table)->new_prop_table);

                _copy_table(ctx, obj_id, prop_table, new_prop_table);

                prop_table = new_prop_table;
                capacity   = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, prop_table)->capacity);
                continue;
            }

            TRY(KOS_set_property(ctx, key_table_obj, key, KOS_VOID));
        }
    } while ( ! IS_BAD_PTR(obj_id) && deep);

    OBJPTR(OBJECT_WALK, walk_id)->key_table =
            _read_props(_get_properties(key_table_obj));

cleanup:
    kos_untrack_refs(ctx, 4);

    if (error)
        walk_id = KOS_BADPTR; /* Object is garbage collected */

    return walk_id;
}

KOS_OBJ_ID KOS_new_object_walk_copy(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  walk_id)
{
    int              error = KOS_SUCCESS;
    KOS_OBJECT_WALK *src;
    KOS_OBJECT_WALK *walk;

    kos_track_refs(ctx, 1, &walk_id);

    walk = (KOS_OBJECT_WALK *)kos_alloc_object(ctx,
                                               OBJ_OBJECT_WALK,
                                               sizeof(KOS_OBJECT_WALK));

    kos_untrack_refs(ctx, 1);

    if ( ! walk)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    assert(GET_OBJ_TYPE(walk_id) == OBJ_OBJECT_WALK);

    src = OBJPTR(OBJECT_WALK, walk_id);

    KOS_atomic_write_relaxed_u32(walk->index, KOS_atomic_read_relaxed_u32(src->index));
    walk->obj        = src->obj;
    walk->key_table  = src->key_table;
    walk->last_key   = KOS_atomic_read_relaxed_obj(src->last_key);
    walk->last_value = KOS_atomic_read_relaxed_obj(src->last_value);

cleanup:
    return error ? KOS_BADPTR : OBJID(OBJECT_WALK, walk);
}

int KOS_object_walk(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  walk_id)
{
    int        error    = KOS_ERROR_INTERNAL;
    uint32_t   capacity = 0;
    KOS_OBJ_ID table    = KOS_BADPTR;
    KOS_OBJ_ID key      = KOS_BADPTR;

    assert(GET_OBJ_TYPE(walk_id) == OBJ_OBJECT_WALK);

    if ( ! IS_BAD_PTR(OBJPTR(OBJECT_WALK, walk_id)->key_table)) {
        table    = OBJPTR(OBJECT_WALK, walk_id)->key_table;
        capacity = KOS_atomic_read_relaxed_u32(OBJPTR(OBJECT_STORAGE, table)->capacity);
    }

    kos_track_refs(ctx, 3, &walk_id, &table, &key);

    for (;;) {

        const int32_t index = KOS_atomic_add_i32(OBJPTR(OBJECT_WALK, walk_id)->index, 1);

        if ((uint32_t)index >= capacity) {
            KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, walk_id)->last_key,   KOS_BADPTR);
            KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, walk_id)->last_value, KOS_BADPTR);
            error = KOS_ERROR_NOT_FOUND;
            break;
        }

        key = KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_STORAGE, table)->items[index].key);

        if ( ! IS_BAD_PTR(key)) {

            const KOS_OBJ_ID value = KOS_get_property(ctx,
                                                      OBJPTR(OBJECT_WALK, walk_id)->obj,
                                                      key);

            if (IS_BAD_PTR(value))
                KOS_clear_exception(ctx);
            else {
                KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, walk_id)->last_key,   key);
                KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WALK, walk_id)->last_value, value);
                error = KOS_SUCCESS;
                break;
            }
        }
    }

    kos_untrack_refs(ctx, 3);

    return error;
}

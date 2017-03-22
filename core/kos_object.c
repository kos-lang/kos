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

#include "../inc/kos_object.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_threads.h"
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
 *  0 - TO_OBJPTR(0), indicates an empty/unused slot
 *  T - TOMBSTONE, indicates a deleted property
 *  C - CLOSED, indicates that the property's value was salvaged to new table
 *      during resize
 *  R - RESERVED, indicates a reserved slot in the new table during resize
 *  K - Some key.  When a slot is allocated for a given key, this key stays in
 *      this table forever, it never changes.
 *  V - Some value.  Values can change over time.  When a property is deleted,
 *      TOMBSTONE is written as a value.
 */

static KOS_ASCII_STRING(str_err_null_ptr,          "null pointer");
static KOS_ASCII_STRING(str_err_not_string,        "property name is not a string");
static KOS_ASCII_STRING(str_err_no_property,       "no such property");
static KOS_ASCII_STRING(str_err_no_own_properties, "object has no own properties");

static KOS_SPECIAL _tombstone = { OBJ_SPECIAL, 0 };
static KOS_SPECIAL _closed    = { OBJ_SPECIAL, 0 };
static KOS_SPECIAL _reserved  = { OBJ_SPECIAL, 0 };

/* When a key is deleted, it remains in the table, but its value is marked
 * as TOMBSTONE. */
#define TOMBSTONE TO_OBJPTR(&_tombstone)
/* When the hash table is too small and is being resized, values moved to the
 * new table are marked as CLOSED in the old table. */
#define CLOSED    TO_OBJPTR(&_closed)
/* During resize operation, slots in the new table are marked as reserved,
 * which is part of strategy to avoid race conditions. */
#define RESERVED  TO_OBJPTR(&_reserved)

#ifdef CONFIG_OBJECT_STATS
#ifdef __cplusplus
static KOS_ATOMIC(uint32_t) _stat_num_successful_resizes(0);
static KOS_ATOMIC(uint32_t) _stat_num_failed_resizes    (0);
static KOS_ATOMIC(uint32_t) _stat_num_successful_writes (0);
static KOS_ATOMIC(uint32_t) _stat_num_failed_writes     (0);
static KOS_ATOMIC(uint32_t) _stat_num_successful_reads  (0);
static KOS_ATOMIC(uint32_t) _stat_num_failed_reads      (0);
#else
static KOS_ATOMIC(uint32_t) _stat_num_successful_resizes = 0;
static KOS_ATOMIC(uint32_t) _stat_num_failed_resizes     = 0;
static KOS_ATOMIC(uint32_t) _stat_num_successful_writes  = 0;
static KOS_ATOMIC(uint32_t) _stat_num_failed_writes      = 0;
static KOS_ATOMIC(uint32_t) _stat_num_successful_reads   = 0;
static KOS_ATOMIC(uint32_t) _stat_num_failed_reads       = 0;
#endif

struct _KOS_OBJECT_STATS _KOS_get_object_stats()
{
    struct _KOS_OBJECT_STATS stats;
    stats.num_successful_resizes = KOS_atomic_read_u32(_stat_num_successful_resizes);
    stats.num_failed_resizes     = KOS_atomic_read_u32(_stat_num_failed_resizes);
    stats.num_successful_writes  = KOS_atomic_read_u32(_stat_num_successful_writes);
    stats.num_failed_writes      = KOS_atomic_read_u32(_stat_num_failed_writes);
    stats.num_successful_reads   = KOS_atomic_read_u32(_stat_num_successful_reads);
    stats.num_failed_reads       = KOS_atomic_read_u32(_stat_num_failed_reads);
    return stats;
}

#define UPDATE_STATS(stat) KOS_atomic_add_i32(_stat_##stat, 1)

#else
#   define UPDATE_STATS(stat) (void)0
#endif

typedef struct _KOS_PROPERTY_BUF KOS_PBUF;

KOS_OBJ_PTR KOS_new_object(KOS_STACK_FRAME *frame)
{
    KOS_CONTEXT *ctx;

    assert( ! IS_BAD_PTR(frame->module));
    assert(OBJPTR(KOS_MODULE, frame->module)->context);

    ctx = OBJPTR(KOS_MODULE, frame->module)->context;
    return KOS_new_object_with_prototype(frame, TO_OBJPTR(&ctx->object_prototype));
}

KOS_OBJ_PTR KOS_new_object_with_prototype(KOS_STACK_FRAME *frame,
                                          KOS_OBJ_PTR      prototype)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(frame, KOS_OBJECT);

    if (obj)
        _KOS_init_object(&obj->object, prototype);

    return TO_OBJPTR(obj);
}

static KOS_OBJECT *_get_properties(KOS_OBJ_PTR obj)
{
    KOS_OBJECT *props = 0;

    if (IS_TYPE(OBJ_OBJECT, obj))
        props = OBJPTR(KOS_OBJECT, obj);

    return props;
}

static KOS_PBUF *_alloc_buffer(KOS_STACK_FRAME *frame, unsigned capacity)
{
    return (KOS_PBUF *)_KOS_alloc_buffer(frame,
           sizeof(KOS_PBUF) + (capacity - 1) * sizeof(KOS_PITEM));
}

static void _free_buffer(KOS_STACK_FRAME *frame, KOS_PBUF *buf)
{
    _KOS_free_buffer(frame, buf, sizeof(KOS_PBUF) + (buf->capacity - 1) * sizeof(KOS_PITEM));
}

void _KOS_init_object(KOS_OBJECT *obj, KOS_OBJ_PTR prototype)
{
    obj->type      = OBJ_OBJECT;
    obj->prototype = prototype;
    obj->priv      = 0;
    obj->finalize  = 0;

    KOS_atomic_write_ptr(obj->props, (void *)0);
}

static int _is_key_equal(KOS_OBJ_PTR key,
                         uint32_t    hash,
                         KOS_OBJ_PTR prop_key,
                         KOS_PITEM  *prop_item)
{
    if (key == prop_key)
        return 1;
    else {
        const uint32_t prop_hash = KOS_atomic_read_u32(prop_item->hash.hash);

        if (prop_hash && hash != prop_hash)
            return 0;

        return ! KOS_string_compare(key, prop_key);
    }
}

static int _salvage_item(KOS_PITEM *old_item, KOS_PBUF *new_table, uint32_t new_capacity)
{
    KOS_OBJ_PTR    key;
    KOS_OBJ_PTR    value;
    KOS_PITEM     *new_item;
    const uint32_t mask = new_capacity - 1;
    uint32_t       hash;
    uint32_t       idx;

    /* Attempt to close an empty or deleted slot early */
    if (KOS_atomic_cas_ptr(old_item->value, TOMBSTONE, CLOSED))
        return 1;

    value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(old_item->value);
    if (value == CLOSED)
        return 0;

    key  = (KOS_OBJ_PTR)KOS_atomic_read_ptr(old_item->key);
    assert( ! IS_BAD_PTR(key));
    hash = KOS_atomic_read_u32(old_item->hash.hash);
    idx  = hash & mask;

    /* Claim a slot in the new table */
    for (;;) {

        KOS_OBJ_PTR dest_key;

        new_item = new_table->items + idx;

        if (KOS_atomic_cas_ptr(new_item->key, TO_OBJPTR(0), key)) {
            KOS_atomic_write_u32(new_item->hash.hash, hash);
            KOS_atomic_add_i32(new_table->num_slots_used, 1);
            break;
        }

        /* This slot in the new table is already taken */
        dest_key = (KOS_OBJ_PTR)KOS_atomic_read_ptr(new_item->key);
        assert( ! IS_BAD_PTR(dest_key));
        if (_is_key_equal(key, hash, dest_key, new_item))
            /* Someone already wrote this key to the new table */
            break;

        idx = (idx + 1) & mask;
    }

    /* Mark the value as reserved */
    if ( ! KOS_atomic_cas_ptr(new_item->value, TOMBSTONE, RESERVED))
        /* Another thread salvaged this slot */
        return 0;

    /* Get the value from the old table and close the slot */
    value = (KOS_OBJ_PTR)KOS_atomic_swap_ptr(old_item->value, CLOSED);
    if (value == CLOSED)
        /* Another thread closed this slot,
         * we will attempt to mark the slot in the new table as closed. */
        value = TOMBSTONE;

    /* Store the value in the new table, unless another thread salvaged
     * this slot */
    return KOS_atomic_cas_ptr(new_item->value, RESERVED, value);
}

static void _copy_table(KOS_STACK_FRAME        *frame,
                        KOS_OBJECT *props,
                        KOS_PBUF               *old_table,
                        KOS_PBUF               *new_table)
{
    const uint32_t old_capacity = old_table->capacity;
    const uint32_t new_capacity = new_table->capacity;
    const uint32_t mask         = old_capacity - 1;
    const uint32_t fuzz         = 64U * (uint32_t)(KOS_atomic_add_i32(old_table->active_copies, 1) - 1);
    uint32_t       i            = fuzz & mask;
    const uint32_t end          = i;
    int            last;

    do {
        if ( ! _salvage_item(old_table->items + i, new_table, new_capacity))
            /* Early exit if another thread has finished salvaging */
            if (KOS_atomic_read_u32(old_table->all_salvaged))
                break;
        i = (i + 1) & mask;
    } while (i != end);

    last = KOS_atomic_add_i32(old_table->active_copies, -1) == 2;

    if (KOS_atomic_cas_u32(old_table->all_salvaged, 0, 1)) {
        /* yay! */
    }

    if (last) {
        if (KOS_atomic_cas_ptr(props->props, (void *)old_table, (void *)new_table))
            KOS_atomic_add_i32(old_table->active_copies, -1);
    }
    else
        while (KOS_atomic_read_u32(old_table->active_copies))
            _KOS_yield();

    if (last) {
#ifndef NDEBUG
        for (i = 0; i < old_capacity; i++) {
            KOS_PITEM  *item  = old_table->items + i;
            KOS_OBJ_PTR value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(item->value);
            assert(value == CLOSED);
        }
#endif

        /* TODO what if someone still uses it? !!! */
        _free_buffer(frame, old_table);
    }
}

static int _need_resize(KOS_PBUF *table, unsigned num_reprobes)
{
    /* Determine if resize is needed based on the number of reprobes */
#if KOS_MAX_PROP_REPROBES * 2 <= KOS_MIN_PROPS_CAPACITY
    assert(table);
    if (num_reprobes < KOS_MAX_PROP_REPROBES)
        return 0;
#else
    uint32_t capacity;
    uint32_t usage;

    assert(table);
    capacity = table->capacity;

    if (capacity >= KOS_MAX_PROP_REPROBES * 2 && num_reprobes < KOS_MAX_PROP_REPROBES)
        return 0;

    /* For small property tables use a simpler heuristic */
    usage = KOS_atomic_read_u32(table->num_slots_used);
    if (usage * 4 < capacity * 3)
        return 0;
#endif
    return 1;
}

static int _resize_prop_table(KOS_STACK_FRAME *frame,
                              KOS_OBJ_PTR      obj,
                              KOS_PBUF        *old_table,
                              uint32_t         grow_factor)
{
    int         error = KOS_SUCCESS;
    KOS_OBJECT *props = _get_properties(obj);

    const uint32_t old_capacity = old_table ? old_table->capacity : 0U;
    const uint32_t new_capacity = old_capacity ? old_capacity * grow_factor : KOS_MIN_PROPS_CAPACITY;
    KOS_PBUF      *new_table    = _alloc_buffer(frame, new_capacity);

    assert(props);

    if (new_table) {
        unsigned i;

        new_table->num_slots_used = 0;
        new_table->capacity       = new_capacity;
        new_table->active_copies  = 1;
        new_table->all_salvaged   = 0;
        new_table->new_prop_table = (KOS_PBUF_PTR)0;

        for (i = 0; i < new_capacity; i++) {
            new_table->items[i].key       = TO_OBJPTR(0);
            new_table->items[i].hash.hash = 0;
            new_table->items[i].value     = TOMBSTONE;
        }

        if (old_table) {
            if (KOS_atomic_cas_ptr(old_table->new_prop_table, (KOS_PBUF_PTR)0, new_table)) {

                _copy_table(frame, props, old_table, new_table);

                UPDATE_STATS(num_successful_resizes);
            }
            else {
                /* Somebody already resized it */
                _free_buffer(frame, new_table);

                /* Help copy the new table if it is still being resized */
                if (KOS_atomic_read_u32(old_table->active_copies)) {
                    new_table = (KOS_PBUF *)KOS_atomic_read_ptr(old_table->new_prop_table);
                    _copy_table(frame, props, old_table, new_table);
                }

                UPDATE_STATS(num_failed_resizes);
            }
        }
        else
            if ( ! KOS_atomic_cas_ptr(props->props, (void *)0, (void *)new_table)) {
                /* Somebody already resized it */
                _free_buffer(frame, new_table);
                UPDATE_STATS(num_failed_resizes);
            }
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY; /* TODO throw exception */

    return error;
}

KOS_OBJ_PTR KOS_get_property(KOS_STACK_FRAME *frame,
                             KOS_OBJ_PTR      obj,
                             KOS_OBJ_PTR      prop)
{
    KOS_OBJ_PTR retval = TO_OBJPTR(0);

    if (IS_BAD_PTR(obj) || IS_BAD_PTR(prop))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(prop) || ! IS_STRING_OBJ(prop))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
    else {
        KOS_OBJECT *props = _get_properties(obj);

        /* Find non-empty property table in this object or in a prototype */
        while ( ! props || ! KOS_atomic_read_ptr(props->props)) {
            obj = KOS_get_prototype(frame, obj);

            if (IS_BAD_PTR(obj)) {
                props = 0;
                break;
            }

            props = _get_properties(obj);
        }

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop);
            unsigned       idx          = hash;
            KOS_PBUF      *prop_table   = (KOS_PBUF *)KOS_atomic_read_ptr(props->props);
            KOS_PITEM     *items        = prop_table->items;
            unsigned       num_reprobes = prop_table->capacity;
            unsigned       mask         = num_reprobes - 1;

            for (;;) {
                KOS_PITEM *const  cur_item  = items + (idx &= mask);
                KOS_OBJ_PTR       cur_key   = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->key);
                const KOS_OBJ_PTR cur_value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->value);

                /* Object property table is being resized, so read value from the new table */
                if (cur_value == CLOSED) {
                    /* Help copy the old table to avoid races when it is partially copied */
                    KOS_PBUF *new_prop_table = (KOS_PBUF *)KOS_atomic_read_ptr(prop_table->new_prop_table);
                    assert(new_prop_table);

                    _copy_table(frame, props, prop_table, new_prop_table);

                    idx          = hash;
                    prop_table   = new_prop_table;
                    items        = prop_table->items;
                    num_reprobes = prop_table->capacity;
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
                    cur_key = TO_OBJPTR(0);
                }

                /* Assume key not found if too many reprobes */
                if ( ! num_reprobes)
                    cur_key = TO_OBJPTR(0);

                /* If no such key, look in prototypes */
                if (IS_BAD_PTR(cur_key)) {

                    /* Find non-empty property table in a prototype */
                    do {
                        obj = KOS_get_prototype(frame, obj);

                        if (IS_BAD_PTR(obj)) /* end of prototype chain */
                            break;

                        props = _get_properties(obj);
                    }
                    while ( ! props || ! props->props);

                    if (IS_BAD_PTR(obj)) {
                        KOS_raise_exception(frame, TO_OBJPTR(&str_err_no_property));
                        break;
                    }
                    assert(props);

                    idx          = hash;
                    prop_table   = (KOS_PBUF *)KOS_atomic_read_ptr(props->props);
                    items        = prop_table->items;
                    num_reprobes = prop_table->capacity;
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
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_no_property));
    }

    if (IS_BAD_PTR(retval))
        UPDATE_STATS(num_failed_reads);
    else
        UPDATE_STATS(num_successful_reads);

    return retval;
}

int _KOS_object_copy_prop_table(KOS_STACK_FRAME *frame,
                                KOS_OBJ_PTR      obj)
{
    KOS_OBJECT *props;

    assert( ! IS_BAD_PTR(obj));
    assert( ! IS_SMALL_INT(obj));
    assert(IS_TYPE(OBJ_OBJECT, obj));

    props = _get_properties(obj);

    return _resize_prop_table(frame, obj,
            props ? (KOS_PBUF *)KOS_atomic_read_ptr(props->props) : 0, 1U);
}

int KOS_set_property(KOS_STACK_FRAME *frame,
                     KOS_OBJ_PTR      obj,
                     KOS_OBJ_PTR      prop,
                     KOS_OBJ_PTR      value)
{
    int error = KOS_ERROR_EXCEPTION;

    if (IS_BAD_PTR(obj) || IS_BAD_PTR(prop) || IS_BAD_PTR(value))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
    else if (IS_SMALL_INT(prop) || ! IS_STRING_OBJ(prop))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
    else if ( ! IS_TYPE(OBJ_OBJECT, obj))
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_no_own_properties));
    else {
        KOS_OBJECT *props = _get_properties(obj);

        /* Check if property table is non-empty */
        if ( ! KOS_atomic_read_ptr(props->props)) {

            /* It is OK to delete non-existent props even if property table is empty */
            if (value == TOMBSTONE) {
                error = KOS_SUCCESS;
                props = 0;
            }
            /* Allocate property table */
            else {
                const int rerror = _resize_prop_table(frame, obj, 0, 0U);
                if (rerror) {
                    assert(KOS_is_exception_pending(frame));
                    error = rerror;
                    props = 0;
                }
            }
        }

        if (props) {
            const uint32_t hash         = KOS_string_get_hash(prop);
            unsigned       idx          = hash;
            unsigned       num_reprobes = 0;
            KOS_PBUF      *prop_table   = (KOS_PBUF *)KOS_atomic_read_ptr(props->props);
            KOS_PITEM     *items;
            unsigned       mask;

            items = prop_table->items;
            mask  = prop_table->capacity - 1;

            for (;;) {
                KOS_PITEM  *cur_item = items + (idx &= mask);
                KOS_OBJ_PTR cur_key  = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->key);
                KOS_OBJ_PTR oldval;

                /* Found a new empty slot */
                if (IS_BAD_PTR(cur_key)) {

                    /* If we are deleting a non-existent property, just bail */
                    if (value == TOMBSTONE) {
                        error = KOS_SUCCESS;
                        break;
                    }

                    /* Attempt to write the new key */
                    if ( ! KOS_atomic_cas_ptr(cur_item->key, TO_OBJPTR(0), prop))
                        /* Reprobe the slot if another thread has written a key */
                        continue;

                    KOS_atomic_write_u32(cur_item->hash.hash, hash);
                    KOS_atomic_add_i32(prop_table->num_slots_used, 1);
                }
                /* Otherwise check if this key matches the sought property */
                else if ( ! _is_key_equal(prop, hash, cur_key, cur_item)) {

                    /* Resize if property table is full */
                    if (num_reprobes > KOS_MAX_PROP_REPROBES) {
                        error = _resize_prop_table(frame, obj, prop_table, 2U);
                        if (error)
                            break;

                        prop_table   = (KOS_PBUF *)KOS_atomic_read_ptr(props->props);
                        idx          = hash;
                        items        = prop_table->items;
                        mask         = prop_table->capacity - 1;
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
                oldval = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->value);

                /* We will use the new table if it was copied */
                if (oldval != CLOSED) {

                    /* If this is a dynamic property, throw it */
                    if ( ! IS_BAD_PTR(oldval) && ! IS_SMALL_INT(oldval) &&
                        GET_OBJ_TYPE(oldval) == OBJ_DYNAMIC_PROP &&
                        value != TOMBSTONE) {

                        KOS_raise_exception(frame, oldval);
                        error = KOS_ERROR_SETTER;
                        break;
                    }

                    /* It's OK if someone else wrote in the mean time */
                    if ( ! KOS_atomic_cas_ptr(cur_item->value, oldval, value))
                        /* Re-read in case it was moved to the new table */
                        oldval = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->value);
                }

                /* Another thread is resizing the table - use new property table */
                if (oldval == CLOSED) {
                    KOS_PBUF *const new_prop_table = (KOS_PBUF *)KOS_atomic_read_ptr(prop_table->new_prop_table);
                    assert(new_prop_table);

                    _copy_table(frame, props, prop_table, new_prop_table);

                    prop_table   = new_prop_table;
                    idx          = hash;
                    items        = prop_table->items;
                    mask         = prop_table->capacity - 1;
                    num_reprobes = 0;
                    continue;
                }

                error = KOS_SUCCESS;
                break;
            }

            /* Check if we need to resize the table */
            if ( ! error && _need_resize(prop_table, num_reprobes))
                error = _resize_prop_table(frame, obj, prop_table, 2U);
        }
    }

    if (error)
        UPDATE_STATS(num_failed_writes);
    else
        UPDATE_STATS(num_successful_writes);

    return error;
}

int KOS_delete_property(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      obj,
                        KOS_OBJ_PTR      prop)
{
    if (IS_BAD_PTR(prop)) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_null_ptr));
        return KOS_ERROR_EXCEPTION;
    }
    else if (IS_SMALL_INT(prop) || ! IS_STRING_OBJ(prop)) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_string));
        return KOS_ERROR_EXCEPTION;
    }
    else if ( ! IS_BAD_PTR(obj) &&  ! IS_TYPE(OBJ_OBJECT, obj))
        return KOS_SUCCESS;
    else
        return KOS_set_property(frame, obj, prop, TOMBSTONE);
}

KOS_OBJ_PTR KOS_new_builtin_dynamic_property(KOS_STACK_FRAME     *frame,
                                             KOS_FUNCTION_HANDLER getter,
                                             KOS_FUNCTION_HANDLER setter)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR dyn_prop = TO_OBJPTR(0);
    KOS_OBJ_PTR get_obj  = KOS_new_function(frame, KOS_VOID);
    KOS_OBJ_PTR set_obj;

    TRY_OBJPTR(get_obj);

    set_obj = KOS_new_function(frame, KOS_VOID);
    TRY_OBJPTR(set_obj);

    OBJPTR(KOS_FUNCTION, get_obj)->min_args = 0;
    OBJPTR(KOS_FUNCTION, get_obj)->handler  = getter;

    OBJPTR(KOS_FUNCTION, set_obj)->min_args = 1;
    OBJPTR(KOS_FUNCTION, set_obj)->handler  = setter;

    dyn_prop = KOS_new_dynamic_prop(frame, get_obj, set_obj);
    TRY_OBJPTR(dyn_prop);

_error:
    return error ? TO_OBJPTR(0) : dyn_prop;
}

int KOS_set_builtin_dynamic_property(KOS_STACK_FRAME     *frame,
                                     KOS_OBJ_PTR          obj,
                                     KOS_OBJ_PTR          prop,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR dyn_prop = KOS_new_builtin_dynamic_property(frame, getter, setter);

    TRY_OBJPTR(dyn_prop);

    TRY(KOS_set_property(frame, obj, prop, dyn_prop));

_error:
    return error;
}

KOS_OBJ_PTR KOS_get_prototype(KOS_STACK_FRAME *frame,
                              KOS_OBJ_PTR      obj)
{
    KOS_OBJ_PTR  ret = TO_OBJPTR(0);
    KOS_CONTEXT *ctx;

    assert( ! IS_BAD_PTR(frame->module));
    assert(OBJPTR(KOS_MODULE, frame->module)->context);

    ctx = OBJPTR(KOS_MODULE, frame->module)->context;

    if (IS_SMALL_INT(obj))
        ret = TO_OBJPTR(&ctx->integer_prototype);
    else switch (GET_OBJ_TYPE(obj)) {

        case OBJ_INTEGER:
            ret = TO_OBJPTR(&ctx->integer_prototype);
            break;

        case OBJ_FLOAT:
            ret = TO_OBJPTR(&ctx->float_prototype);
            break;

        case OBJ_BOOLEAN:
            ret = TO_OBJPTR(&ctx->boolean_prototype);
            break;

        case OBJ_VOID:
            ret = TO_OBJPTR(&ctx->void_prototype);
            break;

        case OBJ_STRING_8:
            /* fall through */
        case OBJ_STRING_16:
            /* fall through */
        case OBJ_STRING_32:
            ret = TO_OBJPTR(&ctx->string_prototype);
            break;

        case OBJ_ARRAY:
            ret = TO_OBJPTR(&ctx->array_prototype);
            break;

        case OBJ_BUFFER:
            ret = TO_OBJPTR(&ctx->buffer_prototype);
            break;

        case OBJ_FUNCTION:
            ret = TO_OBJPTR(&ctx->function_prototype);
            break;

        case OBJ_OBJECT:
            ret = OBJPTR(KOS_OBJECT, obj)->prototype;
            break;

        default:
            ret = TO_OBJPTR(&ctx->object_prototype);
            break;
    }

    return ret;
}

KOS_OBJ_PTR KOS_new_object_walk(KOS_STACK_FRAME           *frame,
                                KOS_OBJ_PTR                obj,
                                enum KOS_OBJECT_WALK_DEPTH deep)
{
    KOS_ANY_OBJECT *walk = _KOS_alloc_object(frame, KOS_OBJECT_WALK);

    if (obj) {
        const int error = KOS_object_walk_init(frame, &walk->walk, obj, deep);

        if (error) {
            assert(KOS_is_exception_pending(frame));
            walk = 0;
        }
    }

    return TO_OBJPTR(walk);
}

int KOS_object_walk_init(KOS_STACK_FRAME           *frame,
                         KOS_OBJECT_WALK           *walk,
                         KOS_OBJ_PTR                obj,
                         enum KOS_OBJECT_WALK_DEPTH deep)
{
    int         error         = KOS_SUCCESS;
    KOS_OBJ_PTR key_table_obj = KOS_new_object(frame);

    if (IS_BAD_PTR(key_table_obj))
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    walk->type          = OBJ_OBJECT_WALK;
    walk->obj           = obj;
    walk->key_table_obj = key_table_obj;
    walk->key_table     = (void *)0;
    walk->index         = 0;

    do {
        KOS_PBUF  *prop_table;
        KOS_PITEM *cur_item;
        KOS_PITEM *items_end;

        KOS_OBJECT *props = _get_properties(obj);

        obj = KOS_get_prototype(frame, obj);

        if ( ! props)
            continue;

        prop_table = (KOS_PBUF *)KOS_atomic_read_ptr(props->props);

        if ( ! prop_table)
            continue;

        cur_item  = prop_table->items;
        items_end = cur_item + prop_table->capacity;

        for ( ; cur_item < items_end; cur_item++) {
            const KOS_OBJ_PTR key   = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->key);
            const KOS_OBJ_PTR value = (KOS_OBJ_PTR)KOS_atomic_read_ptr(cur_item->value);

            if (IS_BAD_PTR(key) || value == TOMBSTONE)
                continue;

            TRY(KOS_set_property(frame, key_table_obj, key, KOS_VOID));
        }
    }
    while ( ! IS_BAD_PTR(obj) && deep);

    walk->key_table = (KOS_PBUF *)KOS_atomic_read_ptr(_get_properties(key_table_obj)->props);

_error:
    return error;
}

KOS_OBJECT_WALK_ELEM KOS_object_walk(KOS_STACK_FRAME *frame,
                                     KOS_OBJECT_WALK *walk)
{
    KOS_OBJECT_WALK_ELEM elem     = { TO_OBJPTR(0), TO_OBJPTR(0) };
    uint32_t             capacity = 0;
    KOS_PITEM           *table    = 0;

    if (walk->key_table) {
        KOS_PBUF *key_table = (KOS_PBUF *)(walk->key_table);

        capacity = key_table->capacity;
        table    = key_table->items;
    }

    for (;;) {

        KOS_OBJ_PTR   key;
        const int32_t index = KOS_atomic_add_i32(walk->index, 1);

        if ((uint32_t)index >= capacity)
            break;

        key = (KOS_OBJ_PTR)KOS_atomic_read_ptr(table[index].key);

        if ( ! IS_BAD_PTR(key)) {

            const KOS_OBJ_PTR value = KOS_get_property(frame, walk->obj, key);

            if (IS_BAD_PTR(value))
                KOS_clear_exception(frame);
            else {
                elem.key   = key;
                elem.value = value;
                break;
            }
        }
    }

    return elem;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../core/kos_try.h"

static int set_value(KOS_CONTEXT                      ctx,
                     KOS_LOCAL                       *obj,
                     const struct KOS_CONST_STRING_S *prop_str,
                     unsigned                         value)
{
    KOS_OBJ_ID value_id;
    int        error;

    value_id = KOS_new_int(ctx, (int64_t)value);
    TRY_OBJID(value_id);

    error = KOS_set_property(ctx, obj->o, KOS_CONST_ID(*prop_str), value_id);

cleanup:
    return error;
}

/* @item gc collect_garbage()
 *
 *     collect_garbage()
 *
 * Runs the garbage collector.
 *
 * Returns an object containing statistics from the garbage collection cycle.
 *
 * Throws an exception if there was an error, for example if the heap
 * ran out of memory.
 */
static KOS_OBJ_ID collect_garbage(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL    out;
    KOS_GC_STATS stats = KOS_GC_STATS_INIT(~0U);
    int          error;

    KOS_init_local(ctx, &out);

    TRY(KOS_collect_garbage(ctx, &stats));

    out.o = KOS_new_object(ctx);
    TRY_OBJID(out.o);

#define SET_VALUE(name) do {                            \
    KOS_DECLARE_STATIC_CONST_STRING(str_##name, #name); \
    TRY(set_value(ctx, &out, &str_##name, stats.name)); \
} while (0)

    SET_VALUE(num_objs_evacuated);
    SET_VALUE(num_objs_freed);
    SET_VALUE(num_objs_finalized);
    SET_VALUE(num_pages_kept);
    SET_VALUE(num_pages_freed);
    SET_VALUE(size_evacuated);
    SET_VALUE(size_freed);
    SET_VALUE(size_kept);
    SET_VALUE(initial_heap_size);
    SET_VALUE(initial_used_heap_size);
    SET_VALUE(initial_malloc_size);
    SET_VALUE(heap_size);
    SET_VALUE(used_heap_size);
    SET_VALUE(malloc_size);
    SET_VALUE(time_us);
#undef SET_VALUE

cleanup:
    out.o = KOS_destroy_top_local(ctx, &out);

    return error ? KOS_BADPTR : out.o;
}

int kos_module_gc_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_FUNCTION(ctx, module.o, "collect_garbage", collect_garbage, 0);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

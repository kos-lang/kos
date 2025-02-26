/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../core/kos_misc.h"
#include "../core/kos_try.h"
#include <math.h>

static const char str_err_invalid_range[] = "invalid range";
static const char str_err_invalid_seed[]  = "invalid seed";
static const char str_err_no_max_value[]  = "max argument missing";
static const char str_err_no_min_value[]  = "min argument missing";
static const char str_err_not_random[]    = "invalid this";
KOS_DECLARE_STATIC_CONST_STRING(str_err_mutex_fail, "failed to allocate mutex");

typedef struct KOS_RNG_CONTAINER_S {
    KOS_MUTEX      mutex;
    struct KOS_RNG rng;
} KOS_RNG_CONTAINER;

static void finalize(KOS_CONTEXT ctx,
                     void       *priv)
{
    if (priv) {
        KOS_RNG_CONTAINER *rng = (KOS_RNG_CONTAINER *)priv;

        kos_destroy_mutex(&rng->mutex);

        KOS_free(priv);
    }
}

KOS_DECLARE_PRIVATE_CLASS(random_priv_class);

/* @item random random()
 *
 *     random([seed])
 *
 * Pseudo-random number generator class.
 *
 * Returns a new pseudo-random generator object.
 *
 * If the optional argument `seed` is not specified, the random number
 * generator is initialized from a system-specific entropy source.  For example,
 * on Windows CryptGenRandom() is used, otherwise `/dev/urandom` is used if
 * it is available.
 *
 * If `seed` is specified, it is used as seed for the pseudo-random number
 * generator.  `seed` is either an integer or a float.  If `seed` is a float,
 * it is converted to an integer using floor method.
 *
 * The underlying pseudo-random generator initialized by this class
 * uses PCG XSH RR 32 algorithm.
 *
 * The quality of pseudo-random numbers produced by this generator is sufficient
 * for most purposes, but it is not recommended for cryptographic applications.
 *
 * Example:
 *
 *     > const r = random.random(42)
 *     > r.integer()
 *     -6031299347323205752
 *     > r.integer()
 *     -474045495260715754
 */
KOS_DECLARE_STATIC_CONST_STRING(str_seed, "seed");

static const KOS_CONVERT random_args[2] = {
    KOS_DEFINE_OPTIONAL_ARG(str_seed, KOS_VOID),
    KOS_DEFINE_TAIL_ARG()
};

static KOS_OBJ_ID kos_random(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int                error    = KOS_SUCCESS;
    KOS_RNG_CONTAINER *rng      = KOS_NULL;
    KOS_LOCAL          args;
    KOS_LOCAL          seed;
    KOS_LOCAL          ret;

    assert(KOS_get_array_size(args_obj) >= 1);

    KOS_init_locals(ctx, &args, &seed, &ret, kos_end_locals);

    args.o = args_obj;

    ret.o = KOS_new_object_with_private(ctx, this_obj, &random_priv_class, finalize);
    TRY_OBJID(ret.o);

    seed.o = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(seed.o);

    assert( ! IS_BAD_PTR(seed.o));

    if ((seed.o != KOS_VOID) && ! IS_NUMERIC_OBJ(seed.o))
        RAISE_EXCEPTION(str_err_invalid_seed);

    rng = (KOS_RNG_CONTAINER *)KOS_malloc(sizeof(KOS_RNG_CONTAINER));

    if ( ! rng) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    }

    if (seed.o == KOS_VOID)
        kos_rng_init(&rng->rng);
    else {

        int64_t seed_value;

        TRY(KOS_get_integer(ctx, seed.o, &seed_value));

        kos_rng_init_seed(&rng->rng, (uint64_t)seed_value);
    }

    if (kos_create_mutex(&rng->mutex))
        RAISE_EXCEPTION_STR(str_err_mutex_fail);

    KOS_object_set_private_ptr(ret.o, rng);
    rng = KOS_NULL;

cleanup:
    if (rng)
        KOS_free(rng);

    ret.o = KOS_destroy_top_locals(ctx, &args, &ret);

    return error ? KOS_BADPTR : ret.o;
}

static int get_rng(KOS_CONTEXT         ctx,
                   KOS_OBJ_ID          this_obj,
                   KOS_RNG_CONTAINER **rng)
{
    int                error = KOS_SUCCESS;
    KOS_RNG_CONTAINER *rng_ptr;

    assert( ! IS_BAD_PTR(this_obj));

    rng_ptr = (KOS_RNG_CONTAINER *)KOS_object_get_private(this_obj, &random_priv_class);
    if ( ! rng_ptr)
        RAISE_EXCEPTION(str_err_not_random);

    *rng = rng_ptr;

cleanup:
    return error;
}

/* @item random random.prototype.integer()
 *
 *     random.prototype.integer()
 *     random.prototype.integer(min, max)
 *
 * Generates a pseudo-random integer with uniform distribution.
 *
 * Returns a random integer.
 *
 * The first variant generates any integer number.
 *
 * The second variant generates an integer between the chosen `min` and `max`
 * values.  The `min` and `max` values are included in the possible range.
 *
 * Examples:
 *
 *     > const r = random.random(100)
 *     > r.integer()
 *     -5490786365174251167
 *     > r.integer(0, 1)
 *     0
 *     > r.integer(-10, 10)
 *     -2
 */
KOS_DECLARE_STATIC_CONST_STRING(str_min, "min");
KOS_DECLARE_STATIC_CONST_STRING(str_max, "max");

static const KOS_CONVERT rand_integer_args[3] = {
    KOS_DEFINE_OPTIONAL_ARG(str_min, KOS_VOID),
    KOS_DEFINE_OPTIONAL_ARG(str_max, KOS_VOID),
    KOS_DEFINE_TAIL_ARG()
};

static KOS_OBJ_ID rand_integer(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_RNG_CONTAINER *rng       = KOS_NULL;
    KOS_OBJ_ID         min_id;
    KOS_OBJ_ID         max_id;
    int                error     = KOS_SUCCESS;
    int64_t            value     = 0;
    int64_t            min_value = 0;
    int64_t            max_value = 0;

    assert(KOS_get_array_size(args_obj) >= 2);

    TRY(get_rng(ctx, this_obj, &rng));

    min_id = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(min_id);

    max_id = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(max_id);

    if ((min_id == KOS_VOID) && (max_id != KOS_VOID))
        RAISE_EXCEPTION(str_err_no_min_value);

    if ((min_id != KOS_VOID) && (max_id == KOS_VOID))
        RAISE_EXCEPTION(str_err_no_max_value);

    if (min_id != KOS_VOID) {

        TRY(KOS_get_integer(ctx, min_id, &min_value));
        TRY(KOS_get_integer(ctx, max_id, &max_value));

        if (min_value > max_value)
            RAISE_EXCEPTION(str_err_invalid_range);

        if (min_value == max_value)
            return KOS_new_int(ctx, min_value);
    }

    kos_lock_mutex(rng->mutex);

    if (min_id != KOS_VOID)
        value = min_value +
            (int64_t)kos_rng_random_range(&rng->rng,
                                          (uint64_t)(max_value - min_value));
    else
        value = (int64_t)kos_rng_random(&rng->rng);

    kos_unlock_mutex(rng->mutex);

cleanup:
    return error ? KOS_BADPTR : KOS_new_int(ctx, value);
}

/* @item random random.prototype.float()
 *
 *     random.prototype.float()
 *
 * Generates a pseudo-random float with uniform distribution from 0.0
 * (inclusive) to 1.0 (exclusive).
 *
 * Returns a float in the range from 0.0 to 1.0, where 0.0 can be possibly
 * produced and 1.0 is never produced.
 *
 * Example:
 *
 *     > const r = random.random(42)
 *     > r.float()
 *     0.782519239019594
 */
static KOS_OBJ_ID rand_float(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_RNG_CONTAINER *rng   = KOS_NULL;
    int                error = KOS_SUCCESS;
    KOS_NUMERIC_VALUE  value;

    value.i = 0;

    TRY(get_rng(ctx, this_obj, &rng));

    kos_lock_mutex(rng->mutex);

    value.i = (int64_t)kos_rng_random(&rng->rng);

    kos_unlock_mutex(rng->mutex);

    /* Set sign bit to 0 and exponent field to 0x3FF, which corresponds to
     * exponent value 0, making this value uniformly distributed
     * from 1.0 to 2.0, with 1.0 being in the range and 2.0 never in the range. */
    value.i = (value.i & (int64_t)~((uint64_t)0xFFF00000U << 32))
            | ((int64_t)0x3FF00000U << 32);

cleanup:
    return error ? KOS_BADPTR : KOS_new_float(ctx, value.d - 1.0);
}

int kos_module_random_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL proto;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &proto);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,          "random",  kos_random,   random_args, &proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, proto.o, "integer", rand_integer, rand_integer_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, proto.o, "float",   rand_float,   KOS_NULL);

cleanup:
    KOS_destroy_top_locals(ctx, &proto, &module);

    return error;
}

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

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../core/kos_heap.h" /* TODO until we have GC */
#include "../core/kos_malloc.h"
#include "../core/kos_misc.h"
#include "../core/kos_try.h"
#include <math.h>

static const char str_err_invalid_range[] = "invalid range";
static const char str_err_invalid_seed[]  = "invalid seed";
static const char str_err_no_max_value[]  = "max argument missing";
static const char str_err_not_random[]    = "invalid this";
static const char str_err_out_of_memory[] = "out of memory";

struct _KOS_RNG_CONTAINER {
    KOS_OBJ_HEADER       header; /* TODO remove this when we switch to malloc */
    KOS_ATOMIC(uint32_t) lock;
    struct KOS_RNG       rng;
};

static void _finalize(KOS_CONTEXT ctx,
                      void       *priv)
{
    /* TODO free
    if (priv)
        kos_free_buffer(ctx, priv, sizeof(struct _KOS_RNG_CONTAINER));
    */
}

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
static KOS_OBJ_ID _random(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int                        error    = KOS_SUCCESS;
    struct _KOS_RNG_CONTAINER *rng      = 0;
    KOS_OBJ_ID                 ret;
    KOS_OBJ_ID                 seed_obj = KOS_BADPTR;

    ret = KOS_new_object_with_prototype(ctx, this_obj);
    TRY_OBJID(ret);

    OBJPTR(OBJECT, ret)->finalize = _finalize;

    if (KOS_get_array_size(args_obj) > 0) {

        seed_obj = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(seed_obj);

        assert( ! IS_BAD_PTR(seed_obj));

        if ( ! IS_NUMERIC_OBJ(seed_obj))
            RAISE_EXCEPTION(str_err_invalid_seed);
    }

    /* TODO malloc when GC supports finalize */
    /*
    rng = (struct _KOS_RNG_CONTAINER *)kos_malloc(sizeof(struct _KOS_RNG_CONTAINER));
    */
    rng = (struct _KOS_RNG_CONTAINER *)kos_alloc_object(ctx,
                                                        OBJ_OPAQUE,
                                                        sizeof(struct _KOS_RNG_CONTAINER));

    if ( ! rng)
        RAISE_EXCEPTION(str_err_out_of_memory);

    rng->lock = 0;

    if (IS_BAD_PTR(seed_obj))
        kos_rng_init(&rng->rng);
    else {

        int64_t seed;

        TRY(KOS_get_integer(ctx, seed_obj, &seed));

        kos_rng_init_seed(&rng->rng, (uint64_t)seed);
    }

    KOS_object_set_private(*OBJPTR(OBJECT, ret), rng);
    rng = 0;

_error:
    /* TODO free
    if (rng)
        kos_free_buffer(ctx, rng, sizeof(struct _KOS_RNG_CONTAINER));
    */

    return error ? KOS_BADPTR : ret;
}

static int _get_rng(KOS_CONTEXT                 ctx,
                    KOS_OBJ_ID                  this_obj,
                    struct _KOS_RNG_CONTAINER **rng)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT)
        RAISE_EXCEPTION(str_err_not_random);

    *rng = (struct _KOS_RNG_CONTAINER *)KOS_object_get_private(*OBJPTR(OBJECT, this_obj));

    if ( ! *rng)
        RAISE_EXCEPTION(str_err_not_random);

_error:
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
static KOS_OBJ_ID _rand_integer(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    struct _KOS_RNG_CONTAINER *rng       = 0;
    int                        error     = KOS_SUCCESS;
    int64_t                    value     = 0;
    int                        min_max   = 0;
    int64_t                    min_value = 0;
    int64_t                    max_value = 0;

    TRY(_get_rng(ctx, this_obj, &rng));

    if (KOS_get_array_size(args_obj) == 1)
        RAISE_EXCEPTION(str_err_no_max_value);

    if (KOS_get_array_size(args_obj) > 1) {

        KOS_OBJ_ID arg;

        arg = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &min_value));

        arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &max_value));

        if (min_value >= max_value)
            RAISE_EXCEPTION(str_err_invalid_range);

        min_max = 1;
    }

    kos_spin_lock(&rng->lock);

    if (min_max)
        value = min_value +
            (int64_t)kos_rng_random_range(&rng->rng,
                                          (uint64_t)(max_value - min_value));
    else
        value = (int64_t)kos_rng_random(&rng->rng);

    kos_spin_unlock(&rng->lock);

_error:
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
static KOS_OBJ_ID _rand_float(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    struct _KOS_RNG_CONTAINER *rng   = 0;
    int                        error = KOS_SUCCESS;
    union _KOS_NUMERIC_VALUE   value;

    value.i = 0;

    TRY(_get_rng(ctx, this_obj, &rng));

    kos_spin_lock(&rng->lock);

    value.i = (int64_t)kos_rng_random(&rng->rng);

    kos_spin_unlock(&rng->lock);

    /* Set sign bit to 0 and exponent field to 0x3FF, which corresponds to
     * exponent value 0, making this value uniformly distributed
     * from 1.0 to 2.0, with 1.0 being in the range and 2.0 never in the range. */
    value.i = (value.i & (int64_t)~((uint64_t)0xFFF00000U << 32))
            | ((int64_t)0x3FF00000U << 32);

_error:
    return error ? KOS_BADPTR : KOS_new_float(ctx, value.d - 1.0);
}

int kos_module_random_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID proto;

    TRY_ADD_CONSTRUCTOR(    ctx, module,        "random",  _random,       0, &proto);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "integer", _rand_integer, 0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "float",   _rand_float,   0);

_error:
    return error;
}

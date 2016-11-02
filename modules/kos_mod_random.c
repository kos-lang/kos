/*
 * Copyright (c) 2014-2016 Chris Dragan
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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../lang/kos_malloc.h"
#include "../lang/kos_misc.h"
#include "../lang/kos_object_alloc.h" /* TODO until we have finalize */
#include "../lang/kos_try.h"
#include <math.h>

static KOS_ASCII_STRING(str_err_invalid_range, "invalid range");
static KOS_ASCII_STRING(str_err_invalid_seed,  "invalid seed");
static KOS_ASCII_STRING(str_err_no_max_value,  "max argument missing");
static KOS_ASCII_STRING(str_err_not_random,    "invalid this");
static KOS_ASCII_STRING(str_err_out_of_memory, "out of memory");

struct _KOS_RNG_CONTAINER {
    KOS_ATOMIC(uint32_t) lock;
    struct KOS_RNG       rng;
};

static KOS_OBJ_PTR _random(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      this_obj,
                           KOS_OBJ_PTR      args_obj)
{
    /* TODO add finalize */

    int                        error    = KOS_SUCCESS;
    struct _KOS_RNG_CONTAINER *rng;
    KOS_OBJ_PTR                ret;
    KOS_OBJ_PTR                seed_obj = TO_OBJPTR(0);

    ret = KOS_new_object_with_prototype(frame, this_obj);
    TRY_OBJPTR(ret);

    if (KOS_get_array_size(args_obj) > 0) {

        seed_obj = KOS_array_read(frame, args_obj, 0);
        TRY_OBJPTR(seed_obj);

        assert( ! IS_BAD_PTR(seed_obj));

        if ( ! IS_NUMERIC_OBJ(seed_obj))
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_seed));
    }

    /* TODO malloc when we have finalize */
    /*
    rng = (struct _KOS_RNG_CONTAINER *)_KOS_malloc(sizeof(struct _KOS_RNG_CONTAINER));
    */
    rng = (struct _KOS_RNG_CONTAINER *)_KOS_alloc_buffer(frame, sizeof(struct _KOS_RNG_CONTAINER));

    if ( ! rng)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_out_of_memory));

    rng->lock = 0;

    if (IS_BAD_PTR(seed_obj))
        _KOS_rng_init(&rng->rng);
    else {

        int64_t seed;

        error = KOS_get_integer(frame, seed_obj, &seed);

        if (error) {
            /* TODO enable when whe have finalize
            _KOS_free(rng);
            */
            goto _error;
        }

        _KOS_rng_init_seed(&rng->rng, (uint64_t)seed);
    }

    KOS_object_set_private(*OBJPTR(KOS_OBJECT, ret), rng);

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static int _get_rng(KOS_STACK_FRAME            *frame,
                    KOS_OBJ_PTR                 this_obj,
                    struct _KOS_RNG_CONTAINER **rng)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(this_obj));

    if ( ! IS_TYPE(OBJ_OBJECT, this_obj))
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_random));

    *rng = (struct _KOS_RNG_CONTAINER *)KOS_object_get_private(*OBJPTR(KOS_OBJECT, this_obj));

    if ( ! *rng)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_random));

_error:
    return error;
}

static KOS_OBJ_PTR _rand_integer(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      this_obj,
                                 KOS_OBJ_PTR      args_obj)
{
    struct _KOS_RNG_CONTAINER *rng       = 0;
    int                        error     = KOS_SUCCESS;
    int64_t                    value;
    int                        min_max   = 0;
    int64_t                    min_value = 0;
    int64_t                    max_value = 0;

    TRY(_get_rng(frame, this_obj, &rng));

    if (KOS_get_array_size(args_obj) == 1)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_no_max_value));

    if (KOS_get_array_size(args_obj) > 1) {

        KOS_OBJ_PTR arg;

        arg = KOS_array_read(frame, args_obj, 0);
        TRY_OBJPTR(arg);

        TRY(KOS_get_integer(frame, arg, &min_value));

        arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJPTR(arg);

        TRY(KOS_get_integer(frame, arg, &max_value));

        if (min_value >= max_value)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_invalid_range));

        min_max = 1;
    }

    _KOS_spin_lock(&rng->lock);

    if (min_max)
        value = min_value +
            (int64_t)_KOS_rng_random_range(&rng->rng,
                                           (uint64_t)(max_value - min_value));
    else
        value = (int64_t)_KOS_rng_random(&rng->rng);

    _KOS_spin_unlock(&rng->lock);

_error:
    return error ? TO_OBJPTR(0) : KOS_new_int(frame, value);
}

static KOS_OBJ_PTR _rand_float(KOS_STACK_FRAME *frame,
                               KOS_OBJ_PTR      this_obj,
                               KOS_OBJ_PTR      args_obj)
{
    struct _KOS_RNG_CONTAINER *rng       = 0;
    int                        error     = KOS_SUCCESS;
    union _KOS_NUMERIC_VALUE   value;

    value.i = 0;

    TRY(_get_rng(frame, this_obj, &rng));

    _KOS_spin_lock(&rng->lock);

    value.i = _KOS_rng_random(&rng->rng);

    _KOS_spin_unlock(&rng->lock);

    /* Set sign bit to 0 and exponent field to 0x3FF, which corresponds to
     * exponent value 0, making this value uniformly distributed
     * from 1.0 to 2.0, with 1.0 being in the range and 2.0 never in the range. */
    value.i = (value.i & ~((uint64_t)0xFFF00000U << 32))
            | ((uint64_t)0x3FF00000U << 32);

_error:
    return error ? TO_OBJPTR(0) : KOS_new_float(frame, value.d - 1.0);
}

int _KOS_module_random_init(KOS_STACK_FRAME *frame)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR proto;

    TRY_ADD_CONSTRUCTOR(    frame,        "random",  _random,       0, &proto);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "integer", _rand_integer, 0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "float",   _rand_float,   0);

_error:
    return error;
}

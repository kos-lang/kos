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
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_misc.h"
#include "../core/kos_try.h"
#include <math.h>

static const char str_err_abs_minus_max[] = "cannot calculate abs of the lowest integer value";
static const char str_err_negative_root[] = "invalid base";
static const char str_err_not_number[]    = "object is not a number";
static const char str_err_pow_0_0[]       = "0 to the power of 0";

/* @item math abs()
 *
 *     abs(number)
 *
 * Returns absolute value of `number`.
 *
 * Preserves the type of the input argument (integer or float).
 *
 * If `number` is an integer and it is the lowest possible integer value
 * (`0x8000_0000_0000_0000`), then throws an exception.
 *
 * Examples:
 *
 *     > math.abs(-100)
 *     100
 *     > math.abs(-math.infinity)
 *     infinity
 */
static KOS_OBJ_ID _abs(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(ctx, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE) {
            if (numeric.u.i == (int64_t)((uint64_t)1U << 63))
                KOS_raise_exception_cstring(ctx, str_err_abs_minus_max);
            else
                ret = KOS_new_int(ctx, numeric.u.i < 0 ? -numeric.u.i : numeric.u.i);
        }
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);

            numeric.u.i &= (int64_t)~((uint64_t)1U << 63);

            ret = KOS_new_float(ctx, numeric.u.d);
        }
    }

    return ret;
}

/* @item math ceil()
 *
 *     ceil(number)
 *
 * Rounds a number to the closest, but higher or equal integer value.
 *
 * Preserves the type of the input argument.  If `number` is an integer,
 * returns that integer.  If `number` is a float, returns a rounded float.
 *
 * Examples:
 *
 *     > math.ceil(10.5)
 *     11.0
 *     > math.ceil(-0.1)
 *     -0.0
 */
static KOS_OBJ_ID _ceil(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg))
        ret = arg;

    else switch (READ_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            ret = arg;
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, ceil(OBJPTR(FLOAT, arg)->value));
            break;

        default:
            KOS_raise_exception_cstring(ctx, str_err_not_number);
            break;
    }

    return ret;
}

/* @item math exp()
 *
 *     exp(number)
 *
 * Returns Eulers number *e* raised to the power of `number`.
 *
 * The value returned is always a float.
 *
 * Examples:
 *
 *     > math.exp(1)
 *     2.718281828459045
 *     > math.exp(-1)
 *     0.367879441171442
 */
static KOS_OBJ_ID _exp(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID  ret = KOS_BADPTR;
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(ctx, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE)
            numeric.u.d = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
        }

        ret = KOS_new_float(ctx, exp(numeric.u.d));
    }

    return ret;
}

/* @item math expm1()
 *
 *     expm1(number)
 *
 * Returns Eulers number *e* raised to the power of `number` and subtracts `1`.
 *
 * The returned value returned is always a float.
 *
 * The returned value has a higher precision than `math.exp(number) - 1`.
 *
 * Example:
 *
 *     > math.expm1(2)
 *     6.38905609893065
 */
static KOS_OBJ_ID _expm1(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  this_obj,
                         KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID  ret = KOS_BADPTR;
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(ctx, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE)
            numeric.u.d = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
        }

        ret = KOS_new_float(ctx, expm1(numeric.u.d));
    }

    return ret;
}

/* @item math floor()
 *
 *     floor(number)
 *
 * Rounds a number to the closest, but lower or equal integer value.
 *
 * Preserves the type of the input argument.  If `number` is an integer,
 * returns that integer.  If `number` is a float, returns a rounded float.
 *
 * Examples:
 *
 *     > math.floor(0.1)
 *     0.0
 *     > math.floor(-0.1)
 *     -1.0
 */
static KOS_OBJ_ID _floor(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  this_obj,
                         KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg))
        ret = arg;

    else switch (READ_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            ret = arg;
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, floor(OBJPTR(FLOAT, arg)->value));
            break;

        default:
            KOS_raise_exception_cstring(ctx, str_err_not_number);
            break;
    }

    return ret;
}

/* @item math is_infinity()
 *
 *     is_infinity(number)
 *
 * Returns `true` if the `number` is a float and its value is plus or minus
 * infinity, otherwise returns `false`.
 *
 * Examples:
 *
 *     > math.is_infinity(math.infinity)
 *     true
 *     > math.is_infinity(math.nan)
 *     false
 *     > math.is_infinity(1e60)
 *     false
 */
static KOS_OBJ_ID _is_infinity(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (GET_OBJ_TYPE(arg) == OBJ_FLOAT) {

        union _KOS_NUMERIC_VALUE value;

        value.d = OBJPTR(FLOAT, arg)->value;
        ret     = KOS_BOOL(((value.i >> 52) & 0x7FF) == 0x7FF && ! ((uint64_t)value.i << 12));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

/* @item math is_nan()
 *
 *     is_nan(number)
 *
 * Returns `true` if the `number` is a float and its value is a "not-a-number",
 * otherwise returns `false`.
 *
 * Examples:
 *
 *     > math.is_nan(math.nan)
 *     true
 *     > math.is_nan(1.0)
 *     false
 *     > math.is_nan([])
 *     false
 */
static KOS_OBJ_ID _is_nan(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (GET_OBJ_TYPE(arg) == OBJ_FLOAT) {

        union _KOS_NUMERIC_VALUE value;

        value.d = OBJPTR(FLOAT, arg)->value;
        ret     = KOS_BOOL(((value.i >> 52) & 0x7FF) == 0x7FF && ((uint64_t)value.i << 12));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

/* @item math pow()
 *
 *     pow(num, power)
 *
 * Returns `num` raised to `power`.
 *
 * The returned value is always a float.
 *
 * Throws an exception if `num` is negative and `power` is not an
 * integer value (it can still be a float type, but its value must be
 * mathematically an integer).
 *
 * Examples:
 *
 *     > math.pow(2, 2)
 *     4.0
 *     > math.pow(10, -2)
 *     0.01
 */
static KOS_OBJ_ID _pow(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC arg1;
    KOS_NUMERIC arg2;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &arg1));
    TRY(KOS_get_numeric_arg(ctx, args_obj, 1, &arg2));

    if (arg1.type == KOS_INTEGER_VALUE)
        arg1.u.d = (double)arg1.u.i;
    else {
        assert(arg1.type == KOS_FLOAT_VALUE);
    }

    if (arg2.type == KOS_INTEGER_VALUE)
        arg2.u.d = (double)arg2.u.i;
    else {
        assert(arg2.type == KOS_FLOAT_VALUE);
    }

    if (arg1.u.d == 0) {
        if (arg2.u.d == 0)
            RAISE_EXCEPTION(str_err_pow_0_0);
        else
            ret = TO_SMALL_INT(0);
    }
    else if (arg1.u.d == 1 || arg2.u.d == 0)
        ret = TO_SMALL_INT(1);
    else if (arg1.u.d < 0 && ceil(arg2.u.d) != arg2.u.d)
        RAISE_EXCEPTION(str_err_negative_root);
    else
        ret = KOS_new_float(ctx, pow(arg1.u.d, arg2.u.d));

_error:
    return error ? KOS_BADPTR : ret;
}

/* @item math sqrt()
 *
 *     sqrt(number)
 *
 * Returns square root of `number`.
 *
 * The returned value is always a float.
 *
 * Throws an exception if `number` is negative.
 *
 * Example:
 *
 *     > math.sqrt(4)
 *     2.0
 */
static KOS_OBJ_ID _sqrt(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i < 0)
            RAISE_EXCEPTION(str_err_negative_root);
        numeric.u.d = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        if (numeric.u.d < 0)
            RAISE_EXCEPTION(str_err_negative_root);
    }

    ret = KOS_new_float(ctx, sqrt(numeric.u.d));

_error:
    return error ? KOS_BADPTR : ret;
}

int kos_module_math_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int error = KOS_SUCCESS;

    /* @item math infinity
     *
     *     infinity
     *
     * Constant float value representing positive infinity.
     */
    {
        static const char str_infinity[] = "infinity";
        KOS_OBJ_ID        inf;
        union _KOS_NUMERIC_VALUE value;

        value.i = (uint64_t)0x7FF00000U << 32;

        inf = KOS_new_const_ascii_string(ctx, str_infinity, sizeof(str_infinity) - 1);
        TRY_OBJID(inf);

        TRY(KOS_module_add_global(ctx, module, inf, KOS_new_float(ctx, value.d), 0));
    }

    /* @item math nan
     *
     *     nan
     *
     * Constant float value representing "not-a-number".
     */
    {
        static const char str_nan[] = "nan";
        KOS_OBJ_ID        nan;
        union _KOS_NUMERIC_VALUE value;

        value.i = ((uint64_t)0x7FF00000U << 32) | 1U;

        nan = KOS_new_const_ascii_string(ctx, str_nan, sizeof(str_nan) - 1);
        TRY_OBJID(nan);

        TRY(KOS_module_add_global(ctx, module, nan, KOS_new_float(ctx, value.d), 0));
    }

    TRY_ADD_FUNCTION(ctx, module, "abs",         _abs,         1);
    TRY_ADD_FUNCTION(ctx, module, "ceil",        _ceil,        1);
    TRY_ADD_FUNCTION(ctx, module, "exp",         _exp,         1);
    TRY_ADD_FUNCTION(ctx, module, "expm1",       _expm1,       1);
    TRY_ADD_FUNCTION(ctx, module, "floor",       _floor,       1);
    TRY_ADD_FUNCTION(ctx, module, "is_infinity", _is_infinity, 1);
    TRY_ADD_FUNCTION(ctx, module, "is_nan",      _is_nan,      1);
    TRY_ADD_FUNCTION(ctx, module, "pow",         _pow,         2);
    TRY_ADD_FUNCTION(ctx, module, "sqrt",        _sqrt,        1);

_error:
    return error;
}

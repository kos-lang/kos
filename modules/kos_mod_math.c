/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_misc.h"
#include "../core/kos_try.h"
#include <math.h>

static const char str_err_abs_minus_max[] = "cannot calculate abs of the lowest integer value";
static const char str_err_m1_or_less[]    = "value is not greater than -1";
static const char str_err_negative_root[] = "invalid base";
static const char str_err_not_number[]    = "object is not a number";
static const char str_err_outside_m1_1[]  = "value outside of [-1, 1] range";
static const char str_err_pow_0_0[]       = "0 to the power of 0";
static const char str_err_zero_or_less[]  = "value is not positive";

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
static KOS_OBJ_ID kos_abs(KOS_CONTEXT ctx,
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

/* @item math acos()
 *
 *     acos(number)
 *
 * Returns principal value of the arc cosine of `number`.
 *
 * The returned value is always a float.
 *
 * Throws an exception if `number` is outside of the [-1, 1] range.
 *
 * Example:
 *
 *     > math.acos(1)
 *     0.0
 */
static KOS_OBJ_ID kos_acos(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if ((numeric.u.i < -1) || (numeric.u.i > 1))
            RAISE_EXCEPTION(str_err_outside_m1_1);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if ((value < -1) || (value > 1))
            RAISE_EXCEPTION(str_err_outside_m1_1);
    }

    ret = KOS_new_float(ctx, acos(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math asin()
 *
 *     asin(number)
 *
 * Returns principal value of the arc sine of `number`.
 *
 * The returned value is always a float.
 *
 * Throws an exception if `number` is outside of the [-1, 1] range.
 *
 * Example:
 *
 *     > math.asin(-1)
 *     0.0
 */
static KOS_OBJ_ID kos_asin(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if ((numeric.u.i < -1) || (numeric.u.i > 1))
            RAISE_EXCEPTION(str_err_outside_m1_1);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if ((value < -1) || (value > 1))
            RAISE_EXCEPTION(str_err_outside_m1_1);
    }

    ret = KOS_new_float(ctx, asin(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math atan()
 *
 *     atan(number)
 *
 * Returns principal value of the arc tangent of `number`.
 *
 * The returned value is always a float.
 *
 * Example:
 *
 *     > math.atan(math.infinity)
 *     1.570796326794897
 */
static KOS_OBJ_ID kos_atan(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
    }

    ret = KOS_new_float(ctx, atan(value));

cleanup:
    return error ? KOS_BADPTR : ret;
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
static KOS_OBJ_ID kos_ceil(KOS_CONTEXT ctx,
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

/* @item math cos()
 *
 *     cos(number)
 *
 * Returns cosine of `number`.
 *
 * The returned value is always a float.
 *
 * Example:
 *
 *     > math.cos(math.pi / 2)
 *     0.0
 */
static KOS_OBJ_ID kos_cos(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE)
        value = (double)numeric.u.i;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
    }

    ret = KOS_new_float(ctx, cos(value));

cleanup:
    return error ? KOS_BADPTR : ret;
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
static KOS_OBJ_ID kos_exp(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID  ret = KOS_BADPTR;
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(ctx, args_obj, 0, &numeric) == KOS_SUCCESS) {

        double value;

        if (numeric.type == KOS_INTEGER_VALUE)
            value = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
            value = numeric.u.d;
        }

        ret = KOS_new_float(ctx, exp(value));
    }

    return ret;
}

/* @item math expm1()
 *
 *     expm1(number)
 *
 * Returns Euler's number *e* raised to the power of `number` and subtracts `1`.
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
static KOS_OBJ_ID kos_expm1(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID  ret = KOS_BADPTR;
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(ctx, args_obj, 0, &numeric) == KOS_SUCCESS) {

        double value;

        if (numeric.type == KOS_INTEGER_VALUE)
            value = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
            value = numeric.u.d;
        }

        ret = KOS_new_float(ctx, expm1(value));
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
static KOS_OBJ_ID kos_floor(KOS_CONTEXT ctx,
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

/* @item math log()
 *
 *     log(number)
 *
 * Returns natural (base *e*) logarithm of `number`.
 *
 * The value returned is always a float.
 *
 * Throws an exception if `number` is 0 or less.
 *
 * Examples:
 *
 *     > math.log(1)
 *     0.0
 */
static KOS_OBJ_ID kos_log(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i <= 0)
            RAISE_EXCEPTION(str_err_zero_or_less);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if (value <= 0)
            RAISE_EXCEPTION(str_err_zero_or_less);
    }

    ret = KOS_new_float(ctx, log(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math log10()
 *
 *     log10(number)
 *
 * Returns base 10 logarithm of `number`.
 *
 * The value returned is always a float.
 *
 * Throws an exception if `number` is 0 or less.
 *
 * Examples:
 *
 *     > math.log10(1)
 *     0.0
 *     > math.log10(100)
 *     2.0
 */
static KOS_OBJ_ID kos_log10(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i <= 0)
            RAISE_EXCEPTION(str_err_zero_or_less);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if (value <= 0)
            RAISE_EXCEPTION(str_err_zero_or_less);
    }

    ret = KOS_new_float(ctx, log10(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math log1p()
 *
 *     log1p(number)
 *
 * Returns natural (base *e*) logarithm of `1 + number`.
 *
 * The value returned is always a float.
 *
 * The returned value has a higher precision than `math.log(number + 1)`.
 *
 * Throws an exception if `number` is -1 or less.
 *
 * Examples:
 *
 *     > math.log1p(0)
 *     0.0
 */
static KOS_OBJ_ID kos_log1p(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i <= -1)
            RAISE_EXCEPTION(str_err_m1_or_less);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if (value <= -1)
            RAISE_EXCEPTION(str_err_m1_or_less);
    }

    ret = KOS_new_float(ctx, log1p(value));

cleanup:
    return error ? KOS_BADPTR : ret;
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
static KOS_OBJ_ID kos_is_infinity(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (GET_OBJ_TYPE(arg) == OBJ_FLOAT) {

        KOS_NUMERIC_VALUE value;

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
static KOS_OBJ_ID kos_is_nan(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (GET_OBJ_TYPE(arg) == OBJ_FLOAT) {

        KOS_NUMERIC_VALUE value;

        value.d = OBJPTR(FLOAT, arg)->value;
        ret     = KOS_BOOL(((value.i >> 52) & 0x7FF) == 0x7FF && ((uint64_t)value.i << 12));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

/* @item math pow()
 *
 *     pow(number, power)
 *
 * Returns `number` raised to `power`.
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
static KOS_OBJ_ID kos_pow(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC arg1;
    KOS_NUMERIC arg2;
    double      val1;
    double      val2;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &arg1));
    TRY(KOS_get_numeric_arg(ctx, args_obj, 1, &arg2));

    if (arg1.type == KOS_INTEGER_VALUE)
        val1 = (double)arg1.u.i;
    else {
        assert(arg1.type == KOS_FLOAT_VALUE);
        val1 = arg1.u.d;
    }

    if (arg2.type == KOS_INTEGER_VALUE)
        val2 = (double)arg2.u.i;
    else {
        assert(arg2.type == KOS_FLOAT_VALUE);
        val2 = arg2.u.d;
    }

    if (val1 == 0) {
        if (val2 == 0)
            RAISE_EXCEPTION(str_err_pow_0_0);
        else
            ret = TO_SMALL_INT(0);
    }
    else if (val1 == 1 || val2 == 0)
        ret = TO_SMALL_INT(1);
    else if (val1 < 0 && ceil(val2) != val2)
        RAISE_EXCEPTION(str_err_negative_root);
    else
        ret = KOS_new_float(ctx, pow(val1, val2));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math sin()
 *
 *     sin(number)
 *
 * Returns sine of `number`.
 *
 * The returned value is always a float.
 *
 * Example:
 *
 *     > math.sin(math.pi / 2)
 *     1.0
 */
static KOS_OBJ_ID kos_sin(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE)
        value = (double)numeric.u.i;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
    }

    ret = KOS_new_float(ctx, sin(value));

cleanup:
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
static KOS_OBJ_ID kos_sqrt(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i < 0)
            RAISE_EXCEPTION(str_err_negative_root);
        value = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
        if (value < 0)
            RAISE_EXCEPTION(str_err_negative_root);
    }

    ret = KOS_new_float(ctx, sqrt(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item math tan()
 *
 *     tan(number)
 *
 * Returns tangent of `number`.
 *
 * The returned value is always a float.
 *
 * Example:
 *
 *     > math.tan(math.pi / 4)
 *     1.0
 */
static KOS_OBJ_ID kos_tan(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_NUMERIC numeric;
    double      value;

    TRY(KOS_get_numeric_arg(ctx, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE)
        value = (double)numeric.u.i;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        value = numeric.u.d;
    }

    ret = KOS_new_float(ctx, tan(value));

cleanup:
    return error ? KOS_BADPTR : ret;
}

KOS_DECLARE_STATIC_CONST_STRING(str_number, "number");

static const KOS_CONVERT number_arg[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_number),
    KOS_DEFINE_TAIL_ARG()
};

KOS_DECLARE_STATIC_CONST_STRING(str_power, "power");

static const KOS_CONVERT pow_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_number),
    KOS_DEFINE_MANDATORY_ARG(str_power ),
    KOS_DEFINE_TAIL_ARG()
};

KOS_INIT_MODULE(math, KOS_MODULE_NEEDS_KOS_SOURCE)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_debug_output();

    KOS_init_local_with(ctx, &module, module_obj);

    /* @item math infinity
     *
     *     infinity
     *
     * Constant float value representing positive infinity.
     */
    {
        KOS_NUMERIC_VALUE value;
        KOS_OBJ_ID        value_obj;

        value.i = (uint64_t)0x7FF00000U << 32;

        value_obj = KOS_new_float(ctx, value.d);
        TRY_OBJID(value_obj);

        TRY_ADD_GLOBAL(ctx, module.o, "infinity", value_obj);
    }

    /* @item math nan
     *
     *     nan
     *
     * Constant float value representing "not-a-number".
     */
    {
        KOS_NUMERIC_VALUE value;
        KOS_OBJ_ID        value_obj;

        value.i = ((uint64_t)0x7FF00000U << 32) | 1U;

        value_obj = KOS_new_float(ctx, value.d);
        TRY_OBJID(value_obj);

        TRY_ADD_GLOBAL(ctx, module.o, "nan", value_obj);
    }

    TRY_ADD_FUNCTION(ctx, module.o, "abs",         kos_abs,         number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "acos",        kos_acos,        number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "asin",        kos_asin,        number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "atan",        kos_atan,        number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "ceil",        kos_ceil,        number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "cos",         kos_cos,         number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "exp",         kos_exp,         number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "expm1",       kos_expm1,       number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "floor",       kos_floor,       number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "log",         kos_log,         number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "log10",       kos_log10,       number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "log1p",       kos_log1p,       number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "is_infinity", kos_is_infinity, number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "is_nan",      kos_is_nan,      number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "pow",         kos_pow,         pow_args);
    TRY_ADD_FUNCTION(ctx, module.o, "sin",         kos_sin,         number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "sqrt",        kos_sqrt,        number_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "tan",         kos_tan,         number_arg);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

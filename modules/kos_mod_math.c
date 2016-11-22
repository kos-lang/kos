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
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../lang/kos_misc.h"
#include "../lang/kos_try.h"
#include <math.h>

static KOS_ASCII_STRING(str_err_negative_root, "invalid base");
static KOS_ASCII_STRING(str_err_not_number,    "object is not a number");
static KOS_ASCII_STRING(str_err_pow_0_0,       "0 to the power of 0");

static KOS_OBJ_PTR _abs(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      this_obj,
                        KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(frame, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE)
            ret = KOS_new_int(frame, numeric.u.i < 0 ? -numeric.u.i : numeric.u.i);
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);

            numeric.u.i &= (int64_t)~((uint64_t)1U << 63);

            ret = KOS_new_float(frame, numeric.u.d);
        }
    }

    return ret;
}

static KOS_OBJ_PTR _ceil(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg))
        ret = arg;
    else switch (GET_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            ret = arg;
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(frame, ceil(OBJPTR(KOS_FLOAT, arg)->number));
            break;

        default:
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_number));
            break;
    }

    return ret;
}

static KOS_OBJ_PTR _exp(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      this_obj,
                        KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(frame, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE)
            numeric.u.d = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
        }

        ret = KOS_new_float(frame, exp(numeric.u.d));
    }

    return ret;
}

static KOS_OBJ_PTR _expm1(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      this_obj,
                          KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_NUMERIC numeric;

    if (KOS_get_numeric_arg(frame, args_obj, 0, &numeric) == KOS_SUCCESS) {

        if (numeric.type == KOS_INTEGER_VALUE)
            numeric.u.d = (double)numeric.u.i;
        else {
            assert(numeric.type == KOS_FLOAT_VALUE);
        }

        ret = KOS_new_float(frame, expm1(numeric.u.d));
    }

    return ret;
}

static KOS_OBJ_PTR _floor(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      this_obj,
                          KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg))
        ret = arg;
    else switch (GET_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            ret = arg;
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(frame, floor(OBJPTR(KOS_FLOAT, arg)->number));
            break;

        default:
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_number));
            break;
    }

    return ret;
}

static KOS_OBJ_PTR _is_infinity(KOS_STACK_FRAME *frame,
                                KOS_OBJ_PTR      this_obj,
                                KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_FLOAT)
        ret = KOS_FALSE;
    else {

        union _KOS_NUMERIC_VALUE value;

        value.d = OBJPTR(KOS_FLOAT, arg)->number;
        ret     = KOS_BOOL(((value.i >> 52) & 0x7FF) == 0x7FF && ! ((uint64_t)value.i << 12));
    }

    return ret;
}

static KOS_OBJ_PTR _is_nan(KOS_STACK_FRAME *frame,
                           KOS_OBJ_PTR      this_obj,
                           KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_FLOAT)
        ret = KOS_FALSE;
    else {

        union _KOS_NUMERIC_VALUE value;

        value.d = OBJPTR(KOS_FLOAT, arg)->number;
        ret     = KOS_BOOL(((value.i >> 52) & 0x7FF) == 0x7FF && ((uint64_t)value.i << 12));
    }

    return ret;
}

static KOS_OBJ_PTR _pow(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      this_obj,
                        KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    KOS_NUMERIC arg1;
    KOS_NUMERIC arg2;

    TRY(KOS_get_numeric_arg(frame, args_obj, 0, &arg1));
    TRY(KOS_get_numeric_arg(frame, args_obj, 1, &arg2));

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
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_pow_0_0));
        else
            ret = TO_SMALL_INT(0);
    }
    else if (arg1.u.d == 1 || arg2.u.d == 0)
        ret = TO_SMALL_INT(1);
    else if (arg1.u.d < 0 && ceil(arg2.u.d) != arg2.u.d)
        RAISE_EXCEPTION(TO_OBJPTR(&str_err_negative_root));
    else
        ret = KOS_new_float(frame, pow(arg1.u.d, arg2.u.d));

_error:
    return error ? TO_OBJPTR(0) : ret;
}

static KOS_OBJ_PTR _sqrt(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR ret   = TO_OBJPTR(0);
    KOS_NUMERIC numeric;

    TRY(KOS_get_numeric_arg(frame, args_obj, 0, &numeric));

    if (numeric.type == KOS_INTEGER_VALUE) {
        if (numeric.u.i < 0)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_negative_root));
        numeric.u.d = (double)numeric.u.i;
    }
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        if (numeric.u.d < 0)
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_negative_root));
    }

    ret = KOS_new_float(frame, sqrt(numeric.u.d));

_error:
    return error ? TO_OBJPTR(0) : ret;
}

int _KOS_module_math_init(KOS_STACK_FRAME *frame)
{
    int error = KOS_SUCCESS;

    {
        static KOS_ASCII_STRING(str_infinity, "infinity");
        union _KOS_NUMERIC_VALUE value;
        value.i = (uint64_t)0x7FF00000U << 32;
        TRY(KOS_module_add_global(frame, TO_OBJPTR(&str_infinity), KOS_new_float(frame, value.d), 0));
    }

    {
        static KOS_ASCII_STRING(str_nan, "nan");
        union _KOS_NUMERIC_VALUE value;
        value.i = ((uint64_t)0x7FF00000U << 32) | 1U;
        TRY(KOS_module_add_global(frame, TO_OBJPTR(&str_nan), KOS_new_float(frame, value.d), 0));
    }

    TRY_ADD_FUNCTION(frame, "abs",         _abs,         1);
    TRY_ADD_FUNCTION(frame, "ceil",        _ceil,        1);
    TRY_ADD_FUNCTION(frame, "exp",         _exp,         1);
    TRY_ADD_FUNCTION(frame, "expm1",       _expm1,       1);
    TRY_ADD_FUNCTION(frame, "floor",       _floor,       1);
    TRY_ADD_FUNCTION(frame, "is_infinity", _is_infinity, 1);
    TRY_ADD_FUNCTION(frame, "is_nan",      _is_nan,      1);
    TRY_ADD_FUNCTION(frame, "pow",         _pow,         2);
    TRY_ADD_FUNCTION(frame, "sqrt",        _sqrt,        1);

_error:
    return error;
}

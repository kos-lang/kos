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
#include "../lang/kos_try.h"
#include <math.h>

static KOS_ASCII_STRING(str_err_not_number, "object is not a number");

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

static KOS_OBJ_PTR _sqrt(KOS_STACK_FRAME *frame,
                         KOS_OBJ_PTR      this_obj,
                         KOS_OBJ_PTR      args_obj)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);
    KOS_OBJ_PTR arg = KOS_array_read(frame, args_obj, 0);

    assert( ! IS_BAD_PTR(arg));

    if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) == OBJ_INTEGER) {
        int64_t value;

        if (IS_SMALL_INT(arg))
            value = GET_SMALL_INT(arg);
        else
            value = OBJPTR(KOS_INTEGER, arg)->number;

        ret = KOS_new_float(frame, sqrt((double)value));
    }
    else if (GET_OBJ_TYPE(arg) == OBJ_FLOAT)
        ret = KOS_new_float(frame, sqrt(OBJPTR(KOS_FLOAT, arg)->number));
    else
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_not_number));

    return ret;
}

int _KOS_module_math_init(KOS_STACK_FRAME *frame)
{
    int error = KOS_SUCCESS;

    TRY_ADD_FUNCTION(frame, "ceil",  _ceil,  1);
    TRY_ADD_FUNCTION(frame, "floor", _floor, 1);
    TRY_ADD_FUNCTION(frame, "sqrt",  _sqrt,  1);

_error:
    return error;
}

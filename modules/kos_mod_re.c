/*
 * Copyright (c) 2014-2020 Chris Dragan
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
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../core/kos_try.h"

KOS_DECLARE_STATIC_CONST_STRING(str_err_regex_not_a_string, "regular expression is not a string");

/* @item re re()
 *
 *     re(regex)
 *
 * Regular expression class.
 *
 * `regex` is a string containing a regular expression.
 *
 * Example:
 *
 *     > re("...")
 */
static KOS_OBJ_ID re_ctor(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL regex_str;

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_init_locals(ctx, 1, &regex_str);

    regex_str.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(regex_str.o);

    if (GET_OBJ_TYPE(regex_str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_regex_not_a_string);

cleanup:
    KOS_destroy_locals(ctx, 1, &regex_str);

    return error ? KOS_BADPTR : KOS_VOID;
}

int kos_module_re_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    KOS_OBJ_ID proto  = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 2, &module, &proto));

    TRY_ADD_CONSTRUCTOR(ctx, module, "re", re_ctor, 1, &proto);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

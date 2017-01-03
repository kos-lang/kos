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

#include "../inc/kos_utils.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_string.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <assert.h>
#include <stdio.h>

static KOS_ASCII_STRING(str_err_not_number,    "object is not a number");

int KOS_get_numeric_arg(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      args_obj,
                        int              idx,
                        KOS_NUMERIC     *numeric)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR arg;

    assert( ! IS_BAD_PTR(args_obj));
    assert( ! IS_SMALL_INT(args_obj));
    assert(GET_OBJ_TYPE(args_obj) == OBJ_ARRAY);
    assert(idx < (int)KOS_get_array_size(args_obj));

    arg = KOS_array_read(frame, args_obj, idx);
    TRY_OBJPTR(arg);

    if (IS_SMALL_INT(arg)) {
        numeric->type = KOS_INTEGER_VALUE;
        numeric->u.i  = GET_SMALL_INT(arg);
    }
    else switch (GET_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            numeric->type = KOS_INTEGER_VALUE;
            numeric->u.i  = OBJPTR(KOS_INTEGER, arg)->number;
            break;

        case OBJ_FLOAT:
            numeric->type = KOS_FLOAT_VALUE;
            numeric->u.d  = OBJPTR(KOS_FLOAT, arg)->number;
            break;

        default:
            RAISE_EXCEPTION(TO_OBJPTR(&str_err_not_number));
    }

_error:
    return error;
}

void KOS_print_exception(KOS_STACK_FRAME *frame)
{
    struct _KOS_VECTOR cstr;
    KOS_OBJ_PTR        exception;

    _KOS_vector_init(&cstr);

    exception = KOS_get_exception(frame);
    assert(!IS_BAD_PTR(exception));

    if (!IS_SMALL_INT(exception) && IS_STRING_OBJ(exception)) {
        KOS_clear_exception(frame);
        if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, exception, &cstr))
            fprintf(stderr, "%s\n", cstr.buffer);
    }
    else {
        KOS_OBJ_PTR formatted;

        KOS_clear_exception(frame);
        formatted = KOS_format_exception(frame, exception);
        if (IS_BAD_PTR(formatted)) {
            KOS_OBJ_PTR str;

            KOS_clear_exception(frame);

            str = KOS_object_to_string(frame, exception);

            KOS_clear_exception(frame);

            if (IS_BAD_PTR(str) || KOS_string_to_cstr_vec(frame, str, &cstr))
                fprintf(stderr, "Exception: <unable to format>\n");
            else
                fprintf(stderr, "%s\n", cstr.buffer);
        }
        else {
            uint32_t i;
            uint32_t lines;

            assert(!IS_SMALL_INT(formatted) && GET_OBJ_TYPE(formatted) == OBJ_ARRAY);

            lines = KOS_get_array_size(formatted);

            for (i = 0; i < lines; i++) {
                KOS_OBJ_PTR line = KOS_array_read(frame, formatted, (int)i);
                assert(!KOS_is_exception_pending(frame));
                if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, line, &cstr))
                    fprintf(stderr, "%s\n", cstr.buffer);
            }
        }
    }

    _KOS_vector_destroy(&cstr);
}

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

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>

static const char str_args[]                          = "args";
static const char str_builtin[]                       = "<builtin>";
static const char str_err_already_joined[]            = "thread already joined";
static const char str_err_bad_number[]                = "number parse failed";
static const char str_err_bad_pack_value[]            = "invalid value type for pack format";
static const char str_err_cannot_convert_to_array[]   = "unsupported type passed to array constructor";
static const char str_err_cannot_convert_to_buffer[]  = "unsupported type passed to buffer constructor";
static const char str_err_cannot_convert_to_string[]  = "unsupported type passed to string constructor";
static const char str_err_cannot_override_prototype[] = "cannot override prototype";
static const char str_err_ctor_not_callable[]         = "constructor constructor is not not callable";
static const char str_err_gen_not_callable[]          = "generator constructor is not not callable";
static const char str_err_invalid_array_size[]        = "array size out of range";
static const char str_err_invalid_byte_value[]        = "buffer element value out of range";
static const char str_err_invalid_buffer_size[]       = "buffer size out of range";
static const char str_err_invalid_pack_format[]       = "invalid pack format";
static const char str_err_invalid_string_idx[]        = "string index is out of range";
static const char str_err_join_self[]                 = "thread cannot join itself";
static const char str_err_not_array[]                 = "object is not an array";
static const char str_err_not_boolean[]               = "object is not a boolean";
static const char str_err_not_buffer[]                = "object is not a buffer";
static const char str_err_not_enough_pack_values[]    = "insufficient number of packed values";
static const char str_err_not_function[]              = "object is not a function";
static const char str_err_not_string[]                = "object is not a string";
static const char str_err_not_thread[]                = "object is not a thread";
static const char str_err_too_many_repeats[]          = "invalid string repeat count";
static const char str_err_unpack_buf_too_short[]      = "unpacked buffer too short";
static const char str_err_unsup_operand_types[]       = "unsupported operand types";
static const char str_err_use_async[]                 = "use async to launch threads";
static const char str_function[]                      = "function";
static const char str_result[]                        = "result";

#define TRY_CREATE_CONSTRUCTOR(name)                                         \
do {                                                                         \
    static const char str_name[] = #name;                                    \
    KOS_OBJ_ID        str        = KOS_context_get_cstring(frame, str_name); \
    KOS_CONTEXT      *ctx        = KOS_context_from_frame(frame);            \
    TRY(_create_constructor(frame,                                           \
                            str,                                             \
                            _##name##_constructor,                           \
                            ctx->name##_prototype));                         \
} while (0)

#define PROTO(type) (frame->module->context->type##_prototype)

/* @item lang print()
 *
 *     print(values...)
 *
 * Converts all arguments to printable strings and prints them on stdout.
 *
 * Accepts zero or more arguments to print.
 *
 * Printed values are separated with a single space.
 *
 * After printing all values prints an EOL character.  If no values are
 * provided, just prints an EOL character.
 */
static KOS_OBJ_ID _print(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    TRY(KOS_print_to_cstr_vec(frame, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

    if (cstr.size) {
        cstr.buffer[cstr.size - 1] = '\n';
        fwrite(cstr.buffer, 1, cstr.size, stdout);
    }
    else
        printf("\n");

_error:
    _KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : KOS_VOID;
}

/* @item lang print_()
 *
 *     print_(values...)
 *
 * Converts all arguments to printable strings and prints them on stdout.
 *
 * Accepts zero or more arguments to print.
 *
 * Printed values are separated with a single space.
 *
 * Unlike `print()`, does not print an EOL character after finishing printing.
 */
static KOS_OBJ_ID _print_(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    TRY(KOS_print_to_cstr_vec(frame, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

    if (cstr.size > 1)
        fwrite(cstr.buffer, 1, cstr.size - 1, stdout);

_error:
    _KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : KOS_VOID;
}

static KOS_OBJ_ID _object_iterator(KOS_FRAME                  frame,
                                   KOS_OBJ_ID                 regs_obj,
                                   KOS_OBJ_ID                 args_obj,
                                   enum KOS_OBJECT_WALK_DEPTH deep)
{
    int                  error = KOS_SUCCESS;
    KOS_OBJ_ID           walk;
    KOS_OBJ_ID           ret  = KOS_BADPTR;

    assert( ! IS_BAD_PTR(regs_obj));
    TRY_OBJID(regs_obj);

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);
    assert(KOS_get_array_size(regs_obj) > 0);

    walk = KOS_array_read(frame, regs_obj, 0);
    assert( ! IS_BAD_PTR(walk));
    TRY_OBJID(walk);

    if (GET_OBJ_SUBTYPE(walk) != OBJ_OBJECT_WALK) {
        walk = KOS_new_object_walk(frame, walk, deep);
        TRY_OBJID(walk);

        TRY(KOS_array_write(frame, regs_obj, 0, walk));
    }

    {
        KOS_OBJECT_WALK_ELEM elem;

        KOS_OBJ_ID array = KOS_new_array(frame, 2);
        TRY_OBJID(array);

        elem = KOS_object_walk(frame, OBJPTR(OBJECT_WALK, walk));

        TRY_OBJID(elem.key);
        assert( ! IS_BAD_PTR(elem.value));

        TRY(KOS_array_write(frame, array, 0, elem.key));
        TRY(KOS_array_write(frame, array, 1, elem.value));

        ret = array;
    }

_error:
    return ret;
}

/* @item lang shallow()
 *
 *     shallow(obj)
 *
 * A generator which produces properties of an object in a shallow manner,
 * i.e. without descending into prototypes.
 *
 * Returns an iterator function, which yields 2-element arrays, which are
 * [key, value] pairs of subsequent properties of the `obj` object.
 *
 * The order of the elements yielded is unspecified.
 *
 * Example:
 *
 *     > array(shallow({x:0, y:1}))
 *     [["y", 1], ["x", 0]]
 */
static KOS_OBJ_ID _shallow(KOS_FRAME  frame,
                           KOS_OBJ_ID regs_obj,
                           KOS_OBJ_ID args_obj)
{
    return _object_iterator(frame, regs_obj, args_obj, KOS_SHALLOW);
}

/* @item lang deep()
 *
 *     deep(obj)
 *
 * A generator which produces properties of an object and all its prototypes.
 *
 * Returns an iterator function, which yields 2-element arrays, which are
 * [key, value] pairs of subsequent properties of the `obj` object.
 *
 * The order of the elements yielded is unspecified.
 *
 * Example:
 *
 *     > array(deep({x:0, y:1}))
 *     [["any", <function>], ["all", <function>], ["filter", <function>],
 *      ["count", <function>], ["reduce", <function>], ["iterator", <function>],
 *      ["map", <function>], ["y", 1], ["x", 0]]
 */
static KOS_OBJ_ID _deep(KOS_FRAME  frame,
                        KOS_OBJ_ID regs_obj,
                        KOS_OBJ_ID args_obj)
{
    return _object_iterator(frame, regs_obj, args_obj, KOS_DEEP);
}

/* @item lang void.prototype.iterator()
 *
 *     void.prototype.iterator()
 *
 * A generator which does not produce any elements.
 *
 * Returns an iterator function which always terminates iteration on the
 * first call.
 */
static KOS_OBJ_ID _iterator(KOS_FRAME  frame,
                            KOS_OBJ_ID regs_obj,
                            KOS_OBJ_ID args_obj)
{
    return KOS_BADPTR;
}

static int _create_constructor(KOS_FRAME            frame,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER constructor,
                               KOS_OBJ_ID           prototype)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID func_obj = KOS_new_function(frame, prototype);

    TRY_OBJID(func_obj);

    assert(frame->module);

    OBJPTR(FUNCTION, func_obj)->handler = constructor;
    OBJPTR(FUNCTION, func_obj)->module  = frame->module;

    TRY(KOS_module_add_global(frame,
                              str_name,
                              func_obj,
                              0));

_error:
    return error;
}

/* @item lang number()
 *
 *     number(value = 0)
 *
 * Numeric type constructor.
 *
 * The optional `value` argument can be an integer, a float or a string.
 *
 * If `value` is not provided, returns 0.
 *
 * If `value` is an integer or a float, returns `value`.
 *
 * If `value` is a string, parses it in the same manner numeric literals are
 * parsed by the interpreter and returns the number as either an integer or
 * a float, depending on the parsing result.
 * Throws an exception if the string cannot be parsed.
 *
 * The prototype of `number.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > number()
 *     0
 *     > number(10)
 *     10
 *     > number(10.0)
 *     10.0
 *     > number("123.000")
 *     123.0
 *     > number("0x100")
 *     256
 */
static KOS_OBJ_ID _number_constructor(KOS_FRAME  frame,
                                      KOS_OBJ_ID this_obj,
                                      KOS_OBJ_ID args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        if (IS_NUMERIC_OBJ(arg))
            ret = arg;
        else if (GET_OBJ_TYPE(arg) == OBJ_STRING) {
            struct _KOS_VECTOR cstr;
            _KOS_vector_init(&cstr);

            if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, arg, &cstr)) {

                const char         *begin = cstr.buffer;
                const char         *end   = begin + cstr.size - 1;
                struct _KOS_NUMERIC numeric;

                assert(begin <= end);

                if (KOS_SUCCESS == _KOS_parse_numeric(begin, end, &numeric)) {

                    if (numeric.type == KOS_INTEGER_VALUE)
                        ret = KOS_new_int(frame, numeric.u.i);
                    else {
                        assert(numeric.type == KOS_FLOAT_VALUE);
                        ret = KOS_new_float(frame, numeric.u.d);
                    }
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_bad_number);
            }

            _KOS_vector_destroy(&cstr);
        }
        else
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
    }

    return ret;
}

/* @item lang integer()
 *
 *     integer(value = 0)
 *
 * Integer type constructor.
 *
 * The optional `value` argument can be an integer, a float or a string.
 *
 * If `value` is not provided, returns 0.
 *
 * If `value` is an integer, returns `value`.
 *
 * If `value` is a float, converts it to integer using floor mode and returns the
 * converted value.
 *
 * If `value` is a string, parses it in the same manner numeric literals are
 * parsed by the interpreter, requiring that the string is an integer literal.
 * Throws an exception if the string is a floating-point literal or cannot be
 * parsed.
 *
 * The prototype of `integer.prototype` is `number.prototype`.
 *
 * Examples:
 *
 *     > integer()
 *     0
 *     > integer(10)
 *     10
 *     > integer(4.2)
 *     4
 *     > integer("123")
 *     123
 */
static KOS_OBJ_ID _integer_constructor(KOS_FRAME  frame,
                                       KOS_OBJ_ID this_obj,
                                       KOS_OBJ_ID args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        int64_t    value;
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        if (IS_NUMERIC_OBJ(arg)) {
            if (KOS_get_integer(frame, arg, &value) == KOS_SUCCESS)
                ret = KOS_new_int(frame, value);
        }
        else if (GET_OBJ_TYPE(arg) == OBJ_STRING) {

            struct _KOS_VECTOR cstr;
            _KOS_vector_init(&cstr);

            if (KOS_SUCCESS == KOS_string_to_cstr_vec(frame, arg, &cstr)) {

                const char *begin = cstr.buffer;
                const char *end   = begin + cstr.size - 1;
                int         error;

                assert(begin <= end);

                error = _KOS_parse_int(begin, end, &value);

                if (error)
                    KOS_raise_exception_cstring(frame, str_err_bad_number);
                else
                    ret = KOS_new_int(frame, value);
            }

            _KOS_vector_destroy(&cstr);
        }
        else if ( ! IS_BAD_PTR(arg))
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
    }

    return ret;
}

/* @item lang float()
 *
 *     float(value = 0)
 *
 * Float type constructor.
 *
 * The optional `value` argument can be an integer, a float or a string.
 *
 * If `value` is not provided, returns 0.
 *
 * If `value` is an integer, converts it to a float and returns the converted value.
 *
 * If `value` is a float, returns `value`.
 *
 * If `value` is a string, parses it in the same manner numeric literals are
 * parsed by the interpreter, assuming it is a floating-point literal.
 * Throws an exception if the string cannot be parsed.
 *
 * The prototype of `float.prototype` is `number.prototype`.
 *
 * Examples:
 *
 *     > float()
 *     0.0
 *     > float(10)
 *     10.0
 *     > float("123.5")
 *     123.5
 */
static KOS_OBJ_ID _float_constructor(KOS_FRAME  frame,
                                     KOS_OBJ_ID this_obj,
                                     KOS_OBJ_ID args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = KOS_new_float(frame, 0);
    else {
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        if (IS_NUMERIC_OBJ(arg)) {

            switch (GET_NUMERIC_TYPE(arg)) {

                default:
                    ret = KOS_new_float(frame, (double)GET_SMALL_INT(arg));
                    break;

                case OBJ_NUM_INTEGER:
                    ret = KOS_new_float(frame, (double)*OBJPTR(INTEGER, arg));
                    break;

                case OBJ_NUM_FLOAT:
                    ret = arg;
                    break;
            }
        }
        else if (GET_OBJ_TYPE(arg) == OBJ_STRING) {

            struct _KOS_VECTOR cstr;
            _KOS_vector_init(&cstr);

            if (KOS_string_to_cstr_vec(frame, arg, &cstr) == KOS_SUCCESS) {

                const char *begin = cstr.buffer;
                const char *end   = begin + cstr.size - 1;
                double      value;
                int         error;

                assert(begin <= end);

                error = _KOS_parse_double(begin, end, &value);

                if (error)
                    KOS_raise_exception_cstring(frame, str_err_bad_number);
                else
                    ret = KOS_new_float(frame, value);
            }

            _KOS_vector_destroy(&cstr);
        }
        else if ( ! IS_BAD_PTR(arg))
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
    }

    return ret;
}

/* @item lang boolean()
 *
 *     boolean(value = false)
 *
 * Boolean type constructor.
 *
 * Returns the value converted to a boolean using standard truth detection
 * rules.
 *
 * If `value` is `false`, `void`, integer `0` or float `0.0` returns `false`.
 * Otherwise returns `true`.
 *
 * If `value` is not provided, returns `false`.
 *
 * The prototype of `boolean.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > boolean()
 *     false
 *     > boolean(0)
 *     false
 *     > boolean([])
 *     true
 *     > boolean("")
 *     true
 *     > boolean("false")
 *     true
 */
static KOS_OBJ_ID _boolean_constructor(KOS_FRAME  frame,
                                       KOS_OBJ_ID this_obj,
                                       KOS_OBJ_ID args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args > 0) {
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        if ( ! IS_BAD_PTR(arg))
            ret = KOS_BOOL(_KOS_is_truthy(arg));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

/* @item lang void()
 *
 *     void()
 *
 * Void type constructor.
 *
 * Returns `void`.
 *
 * Because `void` is a keyword, this constructor can only be referenced
 * indirectly via the `lang` module, it cannot be referenced if it is
 * imported directly into the current module.
 *
 * The prototype of `void.prototype` is `object.prototype`.
 *
 * Example:
 *
 *     > lang.void()
 *     void
 */
static KOS_OBJ_ID _void_constructor(KOS_FRAME  frame,
                                    KOS_OBJ_ID this_obj,
                                    KOS_OBJ_ID args_obj)
{
    return KOS_VOID;
}

/* @item lang string()
 *
 *     string(args...)
 *
 * String type constructor.
 *
 * Returns a new string created from converting all arguments to strings
 * and concatenating them.
 *
 * If no arguments are provided, returns an empty string.
 *
 * Each argument can be a string, an integer, a float, an array or a buffer.
 * Any other argument type triggers an exception.
 *
 * A string argument undergoes no conversion (concatenation still applies).
 *
 * An integer or a float argument is converted to a string by creating
 * a string which can be parsed back to that number.
 *
 * An array argument must contain numbers, which are unicode code points
 * in the range from 0 to 0x1FFFFF, inclusive.  Float numbers are converted
 * to integers using floor operation.  Any array elements which are not
 * numbers or exceed the above range trigger an exception.  The new string
 * created from the array contains characters corresponding to the specified
 * code points and the string length is equal to the length of the array.
 *
 * A buffer argument is treated as if contains an UTF-8 string and the
 * string is decoded from it.  Any errors in the UTF-8 sequence trigger
 * an exception.
 *
 * The prototype of `string.prototype` is `object.prototype`.
 *
 * Example:
 *
 *     > string("kos", [108, 97, 110, 103], 32)
 *     "koslang32"
 */
static KOS_OBJ_ID _string_constructor(KOS_FRAME  frame,
                                      KOS_OBJ_ID this_obj,
                                      KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = KOS_new_string(frame, 0, 0);

    else {

        uint32_t i;

        for (i = 0; i < num_args; i++) {
            KOS_OBJ_ID obj = KOS_array_read(frame, args_obj, (int)i);
            TRY_OBJID(obj);

            if (IS_NUMERIC_OBJ(obj))
                obj = KOS_object_to_string(frame, obj);

            else switch (GET_OBJ_TYPE(obj)) {

                case OBJ_STRING:
                    break;

                case OBJ_ARRAY:
                    obj = KOS_new_string_from_codes(frame, obj);
                    break;

                case OBJ_BUFFER: {
                    const uint32_t    size = KOS_get_buffer_size(obj);
                    const char *const buf  = (char *)KOS_buffer_data(obj);

                    obj = KOS_new_string(frame, buf, size);
                    break;
                }

                default:
                    RAISE_EXCEPTION(str_err_cannot_convert_to_string);
            }

            TRY_OBJID(obj);

            TRY(KOS_array_write(frame, args_obj, (int)i, obj));
        }

        if (i == num_args)
            ret = KOS_string_add_many(frame, _KOS_get_array_buffer(OBJPTR(ARRAY, args_obj)), num_args);
    }

_error:
    return error ? KOS_BADPTR : ret;
}

/* @item lang stringify()
 *
 *     stringify(args...)
 *
 * Converts values to human-readable string representation.
 *
 * Returns a new string created from converting all arguments to strings
 * and concatenating them.
 *
 * If no arguments are provided, returns an empty string.
 *
 * String arguments are treated literally without any conversion.
 *
 * Integer, float, boolean and void arguments are converted to their
 * string representation, which is the same as in source code.
 *
 * Arrays and objects are converted to a human-readable representation
 * similar to their apperance in source code, except all the conversion
 * rules apply in the same way to their elements, so for example
 * strings are not double-quoted.
 *
 * TODO describe buffer, function
 *
 * Example:
 *
 *     > stringify(true, "true", 42, [10, "str"])
 *     "truetrue42[10, str]"
 */
static KOS_OBJ_ID _stringify(KOS_FRAME  frame,
                             KOS_OBJ_ID this_obj,
                             KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = KOS_new_string(frame, 0, 0);

    else {

        uint32_t i;

        for (i = 0; i < num_args; i++) {

            KOS_OBJ_ID obj = KOS_array_read(frame, args_obj, (int)i);
            TRY_OBJID(obj);

            obj = KOS_object_to_string(frame, obj);
            TRY_OBJID(obj);

            TRY(KOS_array_write(frame, args_obj, (int)i, obj));
        }

        if (i == num_args)
            ret = KOS_string_add_many(frame, _KOS_get_array_buffer(OBJPTR(ARRAY, args_obj)), num_args);
    }

_error:
    return error ? KOS_BADPTR : ret;
}

/* @item lang object()
 *
 *     object()
 *
 * Object type constructor.
 *
 * Returns a new empty object.  Equivalent to empty object literal `{}`.
 *
 * `object.prototype` is directly or indirectly the prototype for all object types.
 *
 * Example:
 *
 *     > object()
 *     {}
 */
static KOS_OBJ_ID _object_constructor(KOS_FRAME  frame,
                                      KOS_OBJ_ID this_obj,
                                      KOS_OBJ_ID args_obj)
{
    return KOS_new_object(frame);
}

/* @item lang array()
 *
 *     array(size = 0)
 *     array(args...)
 *
 * Array type constructor.
 *
 * The first variant constructs an array of the specified size.  `size` defaults
 * to 0, in which case the result is equivalent to empty array literal `[]`.
 * If size is greater than 0, the array is filled with `void` values.
 *
 * The second variant constructs an array from one or more non-numeric objects.
 * Each of these input arguments is converted to an array and the resulting
 * arrays are concatenated, producing the final array, which is returned
 * by the constructor.  The following input types are supported:
 *
 *  * array    - An array is simply concatenated with other input arguments without
 *               any transformation.
 *               This can be used e.g. to make a shallow copy of an existing
 *               array or to concatenate two arrays.
 *  * string   - An array is produced containing individual characters of the
 *               input string.  The array's elements are strings of size 1.
 *  * buffer   - A buffer is converted into an array containing individual
 *               elements of the buffer.
 *  * function - If the function is an iterator (a primed generator), subsequent
 *               elements are obtained from it and added to the array.
 *               For non-iterator functions an exception is thrown.
 *  * object   - TODO
 *
 * The prototype of `array.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > array()
 *     []
 *     > array(5)
 *     [void, void, void, void, void]
 *     > array("hello")
 *     ["h", "e", "l", "l", "o"]
 *     > array(range(5))
 *     [0, 1, 2, 3, 4]
 *     > array(shallow({one: 1, two: 2, three: 3}))
 *     [["one", 1], ["two", 2], ["three", 3]]
 */
static KOS_OBJ_ID _array_constructor(KOS_FRAME  frame,
                                     KOS_OBJ_ID this_obj,
                                     KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_ID     array    = KOS_new_array(frame, 0);
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i_arg;

    TRY_OBJID(array);

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const uint32_t cur_size = KOS_get_array_size(array);

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, (int)i_arg);
        TRY_OBJID(arg);

        if (i_arg == 0 && num_args == 1 && IS_NUMERIC_OBJ(arg)) {
            int64_t value;

            TRY(KOS_get_integer(frame, arg, &value));

            if (value < 0 || value > INT_MAX)
                RAISE_EXCEPTION(str_err_invalid_array_size);

            TRY(KOS_array_resize(frame, array, (uint32_t)value));

            continue;
        }

        switch (GET_OBJ_TYPE(arg)) {

            case OBJ_ARRAY:
                TRY(KOS_array_insert(frame, array, cur_size, cur_size,
                                     arg, 0, (int32_t)KOS_get_array_size(arg)));
                break;

            case OBJ_STRING: {
                const uint32_t len = KOS_get_string_length(arg);
                uint32_t       i;

                TRY(KOS_array_resize(frame, array, cur_size + len));

                for (i = 0; i < len; i++) {
                    KOS_OBJ_ID ch = KOS_string_get_char(frame, arg, (int)i);
                    TRY_OBJID(ch);
                    TRY(KOS_array_write(frame, array, (int)(cur_size + i), ch));
                }
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t size = KOS_get_buffer_size(arg);
                uint32_t       i;
                uint8_t       *buf  = 0;

                if (size) {
                    buf = KOS_buffer_data(arg);
                    assert(buf);
                }

                TRY(KOS_array_resize(frame, array, cur_size + size));

                for (i = 0; i < size; i++) {
                    const KOS_OBJ_ID byte = TO_SMALL_INT((int)buf[i]);
                    TRY(KOS_array_write(frame, array, (int)(cur_size + i), byte));
                }
                break;
            }

            case OBJ_FUNCTION: {
                enum _KOS_FUNCTION_STATE state =
                        (enum _KOS_FUNCTION_STATE)OBJPTR(FUNCTION, arg)->state;

                if (state != KOS_GEN_READY && state != KOS_GEN_ACTIVE && state != KOS_GEN_DONE) {
                    KOS_raise_exception_cstring(frame, str_err_cannot_convert_to_array);
                    return KOS_BADPTR;
                }

                if (state != KOS_GEN_DONE) {
                    KOS_OBJ_ID gen_args = KOS_new_array(frame, 0);
                    TRY_OBJID(gen_args);

                    for (;;) {
                        KOS_OBJ_ID ret = KOS_call_function(frame, arg, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret)) /* end of iterator */
                            break;
                        TRY(KOS_array_push(frame, array, ret, 0));
                    }
                }
                break;
            }

            case OBJ_OBJECT:
                /* TODO keys */
                break;

            default:
                KOS_raise_exception_cstring(frame, str_err_cannot_convert_to_array);
                return KOS_BADPTR;
        }
    }

_error:
    return error ? KOS_BADPTR : array;
}

/* @item lang buffer()
 *
 *     buffer(size = 0)
 *     buffer(args...)
 *
 * Buffer type constructor.
 *
 * The first variant constructs a buffer of the specified size.  `size` defaults
 * to 0.  If size is greater than 0, the buffer is filled with zeroes.
 *
 * The second variant constructs a buffer from one or more non-numeric objects.
 * Each of these input arguments is converted to a buffer and the resulting
 * buffers are concatenated, producing the final buffer, which is returned
 * by the constructor.  The following input types are supported:
 *
 *  * array    - The array must contain numbers from 0 to 255 (floor operation
 *               is applied to floats).  Any other array elements trigger an
 *               exception.  The array is converted to a buffer containing
 *               bytes with values from the array.
 *  * string   - The string is converted to an UTF-8 representation stored
 *               into a buffer.
 *  * buffer   - A buffer is simply concatenated with other input arguments without
 *               any transformation.
 *               This can be used to make a copy of a buffer.
 *  * function - If the function is an iterator (a primed generator), subsequent
 *               elements are obtained from it and added to the buffer.  The
 *               values returned by the iterator must be numbers from 0 to 255
 *               (floor operation is applied to floats), any other values trigger
 *               an exception.
 *               For non-iterator functions an exception is thrown.
 *
 * The prototype of `buffer.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > buffer()
 *     <>
 *     > buffer(5)
 *     <00 00 00 00 00>
 *     > buffer("hello")
 *     <68 65 6c 6c 6f>
 *     > buffer(range(4))
 *     <00 01 02 03>
 */
static KOS_OBJ_ID _buffer_constructor(KOS_FRAME  frame,
                                      KOS_OBJ_ID this_obj,
                                      KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_ID     buffer   = KOS_new_buffer(frame, 0);
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i_arg;

    TRY_OBJID(buffer);

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const uint32_t cur_size = KOS_get_buffer_size(buffer);

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, (int)i_arg);
        TRY_OBJID(arg);

        if (i_arg == 0 && num_args == 1 && IS_NUMERIC_OBJ(arg)) {
            int64_t value;

            TRY(KOS_get_integer(frame, arg, &value));

            if (value < 0 || value > INT_MAX)
                RAISE_EXCEPTION(str_err_invalid_buffer_size);

            if (value) {
                TRY(KOS_buffer_resize(frame, buffer, (uint32_t)value));

                memset(KOS_buffer_data(buffer), 0, (size_t)value);
            }

            continue;
        }

        switch (GET_OBJ_TYPE(arg)) {

            case OBJ_ARRAY: {
                const uint32_t size = KOS_get_array_size(arg);
                uint32_t       i;
                uint8_t       *data;

                TRY(KOS_buffer_resize(frame, buffer, cur_size + size));

                data = KOS_buffer_data(buffer) + cur_size;

                for (i = 0; i < size; i++) {
                    int64_t value;

                    KOS_OBJ_ID elem = KOS_array_read(frame, arg, i);
                    TRY_OBJID(elem);

                    TRY(KOS_get_integer(frame, elem, &value));

                    if (value < 0 || value > 255)
                        RAISE_EXCEPTION(str_err_invalid_byte_value);

                    *(data++) = (uint8_t)(uint64_t)value;
                }
                break;
            }

            case OBJ_STRING: {
                const uint32_t size = KOS_string_to_utf8(arg, 0, 0);

                TRY(KOS_buffer_resize(frame, buffer, cur_size + size));

                KOS_string_to_utf8(arg, KOS_buffer_data(buffer) + cur_size, size);
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t size = KOS_get_buffer_size(arg);

                TRY(KOS_buffer_resize(frame, buffer, cur_size + size));

                memcpy(KOS_buffer_data(buffer) + cur_size,
                       KOS_buffer_data(arg),
                       size);
                break;
            }

            case OBJ_FUNCTION: {
                enum _KOS_FUNCTION_STATE state =
                        (enum _KOS_FUNCTION_STATE)OBJPTR(FUNCTION, arg)->state;

                if (state != KOS_GEN_READY && state != KOS_GEN_ACTIVE && state != KOS_GEN_DONE) {
                    KOS_raise_exception_cstring(frame, str_err_cannot_convert_to_buffer);
                    return KOS_BADPTR;
                }

                if (state != KOS_GEN_DONE) {
                    uint8_t *data;
                    uint32_t size     = cur_size;
                    uint32_t capacity = cur_size;

                    KOS_OBJ_ID gen_args = KOS_new_array(frame, 0);
                    TRY_OBJID(gen_args);

                    if (cur_size < 64) {
                        TRY(KOS_buffer_resize(frame, buffer, 64));
                        capacity = 64;
                    }

                    data = KOS_buffer_data(buffer) + cur_size;

                    for (;;) {
                        int64_t value;

                        KOS_OBJ_ID ret = KOS_call_function(frame, arg, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret)) /* end of iterator */
                            break;

                        TRY(KOS_get_integer(frame, ret, &value));

                        if (value < 0 || value > 255)
                            RAISE_EXCEPTION(str_err_invalid_byte_value);

                        if (size >= capacity) {
                            capacity *= 2;
                            TRY(KOS_buffer_resize(frame, buffer, capacity));
                        }

                        *(data++) = (uint8_t)(uint64_t)value;
                        ++size;
                    }

                    TRY(KOS_buffer_resize(frame, buffer, size));
                }
                break;
            }

            default:
                KOS_raise_exception_cstring(frame, str_err_cannot_convert_to_buffer);
                return KOS_BADPTR;
        }
    }

_error:
    return error ? KOS_BADPTR : buffer;
}

/* @item lang function()
 *
 *     function(func)
 *
 * Function type constructor.
 *
 * The argument is a function object which is returned by
 * this constructor, no new object is created by it.
 * Throws an exception if the argument is not a function.
 *
 * The prototype of `function.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID _function_constructor(KOS_FRAME  frame,
                                        KOS_OBJ_ID this_obj,
                                        KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (KOS_get_array_size(args_obj) != 1)
        KOS_raise_exception_cstring(frame, str_err_not_function);

    else {

        ret = KOS_array_read(frame, args_obj, 0);
        if ( ! IS_BAD_PTR(ret)) {
            if (GET_OBJ_TYPE(ret) != OBJ_FUNCTION) {
                KOS_raise_exception_cstring(frame, str_err_not_function);
                ret = KOS_BADPTR;
            }
        }
    }

    return ret;
}

/* @item lang constructor()
 *
 *     constructor()
 *
 * Constructor for constructor functions.
 *
 * The purpose of this constructor is to be used with the `instanceof`
 * operator to detect constructor functions.
 *
 * Calling this constructor function throws an exception.
 *
 * The prototype of `constructor.prototype` is `function.prototype`.
 */
static KOS_OBJ_ID _constructor_constructor(KOS_FRAME  frame,
                                           KOS_OBJ_ID this_obj,
                                           KOS_OBJ_ID args_obj)
{
    KOS_raise_exception_cstring(frame, str_err_ctor_not_callable);
    return KOS_BADPTR;
}

/* @item lang generator()
 *
 *     generator()
 *
 * Constructor for generator functions.
 *
 * The purpose of this constructor is to be used with the `instanceof`
 * operator to detect generator functions.
 *
 * Calling this constructor function throws an exception.
 *
 * The prototype of `generator.prototype` is `function.prototype`.
 */
static KOS_OBJ_ID _generator_constructor(KOS_FRAME  frame,
                                         KOS_OBJ_ID this_obj,
                                         KOS_OBJ_ID args_obj)
{
    KOS_raise_exception_cstring(frame, str_err_gen_not_callable);
    return KOS_BADPTR;
}

/* @item lang exception()
 *
 *     exception([value])
 *
 * Exception object constructor.
 *
 * All caught exception objects have `exception.prototype` as their prototype.
 * This constructor gives access to that prototype.
 *
 * Calling this constructor function throws an exception, it does not return
 * an exception object.  The thrown exception's `value` property can be set
 * to the optional `value` argument.  In other words, calling this constructor
 * function is equivalent to throwing `value`.
 *
 * If `value` is not specified, `void` is thrown.
 *
 * The prototype of `exception.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID _exception_constructor(KOS_FRAME  frame,
                                         KOS_OBJ_ID this_obj,
                                         KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID     exception = KOS_VOID;
    const uint32_t num_args  = KOS_get_array_size(args_obj);

    if (num_args > 0)
        exception = KOS_array_read(frame, args_obj, 0);

    KOS_raise_exception(frame, exception);
    return KOS_BADPTR;
}

/* @item lang generator_end()
 *
 *     generator_end()
 *
 * Generator end object constructor.
 *
 * A generator end object is typically thrown when an iterator function is
 * called but has no more values to yield.  In other words, a thrown generator
 * end object indicates end of a generator.  The generator end object can
 * be caught and it becomes the `value` of the exception object caught.
 *
 * Calling this constructor function throws an exception.
 *
 * The prototype of `generator_end.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID _generator_end_constructor(KOS_FRAME  frame,
                                             KOS_OBJ_ID this_obj,
                                             KOS_OBJ_ID args_obj)
{
    KOS_raise_generator_end(frame);
    return KOS_BADPTR;
}

/* @item lang thread()
 *
 *     thread()
 *
 * Thread object constructor.
 *
 * Thread objects are created by calling `function.prototype.async()`.
 *
 * The purpose of this constructor is to be used with the `instanceof`
 * operator to detect thread objects.
 *
 * Calling this constructor function throws an exception.
 *
 * The prototype of `thread.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID _thread_constructor(KOS_FRAME  frame,
                                      KOS_OBJ_ID this_obj,
                                      KOS_OBJ_ID args_obj)
{
    KOS_raise_exception_cstring(frame, str_err_use_async);
    return KOS_BADPTR;
}

/* @item lang function.prototype.apply()
 *
 *     function.prototype.apply(this_object, args)
 *
 * Invokes a function with the specified this object and arguments.
 *
 * Returns the value returned by the function.
 *
 * The `this_object` argument is the object which is bound to the function as
 * `this` for this invocation.  It can be any object or `void`.
 *
 * The `args` argument is an array (can be empty) containing arguments for
 * the function.
 *
 * Example:
 *
 *     > fun f(a) { return this + a }
 *     > f.apply(1, [2])
 *     3
 */
static KOS_OBJ_ID _apply(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID ret      = KOS_BADPTR;
    KOS_OBJ_ID arg_args = KOS_BADPTR;
    KOS_OBJ_ID arg_this;

    arg_this = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(arg_this);

    arg_args = KOS_array_read(frame, args_obj, 1);
    TRY_OBJID(arg_args);

    arg_args = KOS_array_slice(frame, arg_args, 0, MAX_INT64);
    TRY_OBJID(arg_args);

    ret = KOS_apply_function(frame, this_obj, arg_this, arg_args);

_error:
    return error ? KOS_BADPTR : ret;
}

static void _async_func(KOS_FRAME frame,
                        void     *cookie)
{
    KOS_OBJECT *thread_obj = (KOS_OBJECT *)cookie;
    KOS_OBJ_ID  func_obj;
    KOS_OBJ_ID  args_obj;
    KOS_OBJ_ID  ret_obj;

    func_obj = KOS_get_property(frame, OBJID(OBJECT, thread_obj),
                    KOS_context_get_cstring(frame, str_function));
    if (IS_BAD_PTR(func_obj))
        return;

    args_obj = KOS_get_property(frame, OBJID(OBJECT, thread_obj),
                    KOS_context_get_cstring(frame, str_args));
    if (IS_BAD_PTR(args_obj))
        return;

    ret_obj = KOS_apply_function(frame, func_obj, OBJID(OBJECT, thread_obj), args_obj);
    if (IS_BAD_PTR(ret_obj))
        return;

    KOS_set_property(frame, OBJID(OBJECT, thread_obj),
            KOS_context_get_cstring(frame, str_result), ret_obj);
}

static void _thread_finalize(KOS_FRAME frame,
                             void     *priv)
{
    /* TODO don't block GC: add detection for finished threads, put thread
     *      on a list for GC to retry to join */
    if (priv)
        _KOS_thread_join(frame, (_KOS_THREAD)priv);
}

/* @item lang function.prototype.async()
 *
 *     function.prototype.async(args...)
 *
 * Invokes a function asynchronously on a new thread.
 *
 * Returns the created thread object.
 *
 * The returned thread object is also bound as `this` to the function
 * being invoked on that thread.
 *
 * All the arguments are passed to the function.
 *
 * Example:
 *
 *     > fun f(a, b) { return a + b }
 *     > const t = f.async(1, 2)
 *     > t.wait()
 *     3
 */
static KOS_OBJ_ID _async(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int         error      = KOS_SUCCESS;
    KOS_OBJ_ID  thread_obj = KOS_BADPTR;
    _KOS_THREAD thread;

    if (GET_OBJ_TYPE(this_obj) != OBJ_FUNCTION) {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        return KOS_BADPTR;
    }

    thread_obj = KOS_new_object_with_prototype(frame,
            KOS_context_from_frame(frame)->thread_prototype);
    TRY_OBJID(thread_obj);

    TRY(KOS_set_property(frame, thread_obj,
                KOS_context_get_cstring(frame, str_function), this_obj));
    TRY(KOS_set_property(frame, thread_obj,
                KOS_context_get_cstring(frame, str_args), args_obj));

    OBJPTR(OBJECT, thread_obj)->finalize = _thread_finalize;

    TRY(_KOS_thread_create(frame, _async_func, OBJPTR(OBJECT, thread_obj), &thread));

    KOS_object_set_private(*OBJPTR(OBJECT, thread_obj), (void *)thread);

_error:
    return error ? KOS_BADPTR : thread_obj;
}

/* @item lang thread.prototype.wait()
 *
 *     thread.prototype.wait()
 *
 * Waits for thread to complete.
 *
 * Returns the return value returned from the thread function.
 *
 * If the thread function ended with an exception, rethrows that exception
 * on the current thread.
 *
 * Example:
 *
 *     > fun f { return 42 }
 *     > const t = f.async()
 *     > t.wait()
 *     42
 */
static KOS_OBJ_ID _wait(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int                error = KOS_SUCCESS;
    KOS_OBJ_ID         ret   = KOS_BADPTR;
    KOS_CONTEXT *const ctx   = KOS_context_from_frame(frame);
    _KOS_THREAD        thread;

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT) {
        KOS_raise_exception_cstring(frame, str_err_not_thread);
        return KOS_BADPTR;
    }

    if ( ! KOS_has_prototype(frame, this_obj, ctx->thread_prototype)) {
        KOS_raise_exception_cstring(frame, str_err_not_thread);
        return KOS_BADPTR;
    }

    thread = (_KOS_THREAD)KOS_object_swap_private(*OBJPTR(OBJECT, this_obj), (void *)0);

    if ( ! thread) {
        KOS_raise_exception_cstring(frame, str_err_already_joined);
        return KOS_BADPTR;
    }

    if (thread && _KOS_is_current_thread(thread)) {

        /* NOTE: This causes ABA problem if a thread is trying to join itself.
         *       Possible solution could be to use a special pointer to indicate
         *       that the thread is being joined. */

        KOS_object_set_private(*OBJPTR(OBJECT, this_obj), (void *)thread);

        KOS_raise_exception_cstring(frame, str_err_join_self);
        return KOS_BADPTR;
    }

    /* TODO don't block GC */

    error = _KOS_thread_join(frame, thread);

    if (!error) {
        ret = KOS_get_property(frame, this_obj,
                KOS_context_get_cstring(frame, str_result));
        if (IS_BAD_PTR(ret))
            error = KOS_ERROR_EXCEPTION;
    }

    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _set_prototype(KOS_FRAME  frame,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_OBJ_ID                 arg     = KOS_array_read(frame, args_obj, 0);
        const KOS_FUNCTION_HANDLER handler = OBJPTR(FUNCTION, this_obj)->handler;

        /* TODO:
         * - Forbid non-constructor functions?
         * - Create a separate built-in constructor for constructor functions?
         * - Forbid for all built-in functions?
         */
        if (handler == _array_constructor         ||
            handler == _boolean_constructor       ||
            handler == _buffer_constructor        ||
            handler == _constructor_constructor   ||
            handler == _exception_constructor     ||
            handler == _float_constructor         ||
            handler == _function_constructor      ||
            handler == _generator_constructor     ||
            handler == _generator_end_constructor ||
            handler == _integer_constructor       ||
            handler == _number_constructor        ||
            handler == _object_constructor        ||
            handler == _string_constructor        ||
            handler == _thread_constructor        ||
            handler == _void_constructor) {

            KOS_raise_exception_cstring(frame, str_err_cannot_override_prototype);
            arg = KOS_BADPTR;
        }

        if ( ! IS_BAD_PTR(arg)) {
            KOS_FUNCTION *func = OBJPTR(FUNCTION, this_obj);
            KOS_atomic_write_ptr(func->prototype, (void *)arg);
            ret = this_obj;
        }
    }
    else
        KOS_raise_exception_cstring(frame, str_err_not_function);

    return ret;
}

/* @item lang string.prototype.slice()
 *
 *     string.prototype.slice(begin, end)
 *
 * Extracts substring from a string.
 *
 * Returns a new string, unless the entire string was selected, in which
 * case returns the same string object.  (Note: strings are immutable.)
 *
 * `begin` and `end` specify the range of characters to extract in a new
 * string.  `begin` is the index of the first character and `end` is the index
 * of the character trailing the last character to extract.
 * A negative index is an offset from the end, such that `-1` indicates the
 * last character of the string.
 * If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
 * equivalent to string size.
 *
 * This function is invoked by the slice operator.
 *
 * Examples:
 *
 *     > "language".slice(0, 4)
 *     "lang"
 *     > "language".slice(void, void)
 *     "language"
 *     > "language".slice(-5, -1)
 *     "guag"
 */

/* @item lang array.prototype.slice()
 *
 *     array.prototype.slice(begin, end)
 *
 * Extracts a range of elements from an array.
 *
 * Returns a new array.
 *
 * It can be used to create a flat copy of an array.
 *
 * `begin` and `end` specify the range of elements to extract in a new
 * array.  `begin` is the index of the first element and `end` is the index
 * of the element trailing the last element to extract.
 * A negative index is an offset from the end, such that `-1` indicates the
 * last element of the array.
 * If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
 * equivalent to array size.
 *
 * This function is invoked by the slice operator.
 *
 * Examples:
 *
 *     > [1, 2, 3, 4, 5, 6, 7, 8].slice(0, 4)
 *     [1, 2, 3, 4]
 *     > [1, 2, 3, 4, 5, 6, 7, 8].slice(void, void)
 *     [1, 2, 3, 4, 5, 6, 7, 8]
 *     > [1, 2, 3, 4, 5, 6, 7, 8].slice(-5, -1)
 *     [4, 5, 6, 7]
 */

/* @item lang buffer.prototype.slice()
 *
 *     buffer.prototype.slice(begin, end)
 *
 * Extracts a range of elements from a buffer.
 *
 * Returns a new buffer.
 *
 * It can be used to create a flat copy of a buffer.
 *
 * `begin` and `end` specify the range of elements to extract in a new
 * buffer.  `begin` is the index of the first element and `end` is the index
 * of the element trailing the last element to extract.
 * A negative index is an offset from the end, such that `-1` indicates the
 * last element of the buffer.
 * If `begin` is `void`, it is equivalent to `0`.  If `end` is `void`, it is
 * equivalent to buffer size.
 *
 * This function is invoked by the slice operator.
 *
 * Examples:
 *
 *     > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(0, 4)
 *     <1, 2, 3, 4>
 *     > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(void, void)
 *     <1, 2, 3, 4, 5, 6, 7, 8>
 *     > buffer([1, 2, 3, 4, 5, 6, 7, 8]).slice(-5, -1)
 *     <4, 5, 6, 7>
 */
static KOS_OBJ_ID _slice(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    KOS_OBJ_ID a_obj;
    KOS_OBJ_ID b_obj;
    int64_t    idx_a = 0;
    int64_t    idx_b = 0;

    a_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(a_obj);

    b_obj = KOS_array_read(frame, args_obj, 1);
    TRY_OBJID(b_obj);

    if (IS_NUMERIC_OBJ(a_obj))
        TRY(KOS_get_integer(frame, a_obj, &idx_a));
    else if (a_obj == KOS_VOID)
        idx_a = 0;
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }

    if (IS_NUMERIC_OBJ(b_obj))
        TRY(KOS_get_integer(frame, b_obj, &idx_b));
    else if (b_obj == KOS_VOID)
        idx_b = MAX_INT64;
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }

    if (GET_OBJ_TYPE(this_obj) == OBJ_STRING)
        ret = KOS_string_slice(frame, this_obj, idx_a, idx_b);
    else if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_buffer_slice(frame, this_obj, idx_a, idx_b);
    else
        ret = KOS_array_slice(frame, this_obj, idx_a, idx_b);

_error:
    return ret;
}

/* @item lang array.prototype.size
 *
 *     array.prototype.size
 *
 * Read-only size of the array.
 *
 * Example:
 *
 *     > [1, 10, 100].size
 *     3
 */
static KOS_OBJ_ID _get_array_size(KOS_FRAME  frame,
                                  KOS_OBJ_ID this_obj,
                                  KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_ARRAY)
        ret = KOS_new_int(frame, (int64_t)KOS_get_array_size(this_obj));
    else {
        KOS_raise_exception_cstring(frame, str_err_not_array);
        ret = KOS_BADPTR;
    }

    return ret;
}

/* @item lang buffer.prototype.size
 *
 *     buffer.prototype.size
 *
 * Read-only size of the buffer.
 *
 * Example:
 *
 *     > buffer([1, 10, 100]).size
 *     3
 */
static KOS_OBJ_ID _get_buffer_size(KOS_FRAME  frame,
                                   KOS_OBJ_ID this_obj,
                                   KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_new_int(frame, (int64_t)KOS_get_buffer_size(this_obj));
    else {
        KOS_raise_exception_cstring(frame, str_err_not_buffer);
        ret = KOS_BADPTR;
    }

    return ret;
}

/* @item lang array.prototype.resize()
 *
 *     array.prototype.resize(size)
 *
 * Resizes an array.
 *
 * Returns the array being resized.
 *
 * `size` is the new size of the array.
 *
 * If `size` is greater than the current array size, `void` elements are
 * appended to expand the array.
 *
 * Example:
 *
 *     > const a = []
 *     > a.resize(5)
 *     [void, void, void, void, void]
 */

/* @item lang buffer.prototype.resize()
 *
 *     buffer.prototype.resize(size)
 *
 * Resizes a buffer.
 *
 * Returns the buffer being resized.
 *
 * `size` is the new size of the buffer.
 *
 * If `size` is greater than the current buffer size, `void` elements are
 * appended to expand the buffer.
 *
 * Example:
 *
 *     > const a = buffer()
 *     > b.resize(5)
 *     <00 00 00 00 00>
 */
static KOS_OBJ_ID _resize(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID size_obj;
    int64_t    size;

    size_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(size_obj);

    TRY(KOS_get_integer(frame, size_obj, &size));

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception_cstring(frame, str_err_invalid_buffer_size);
            return KOS_BADPTR;
        }

        TRY(KOS_buffer_resize(frame, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception_cstring(frame, str_err_invalid_array_size);
            return KOS_BADPTR;
        }

        TRY(KOS_array_resize(frame, this_obj, (uint32_t)size));
    }

_error:
    return error ? KOS_BADPTR : this_obj;
}

static KOS_OBJ_ID _fill(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     arg      = KOS_array_read(frame, args_obj, 0);
    int64_t        begin    = 0;
    int64_t        end      = 0;

    if (num_args > 2) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &begin));
        else if (arg == KOS_VOID)
            begin = 0;
        else {
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &end));
        else if (arg == KOS_VOID)
            end = MAX_INT64;
        else {
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        arg = KOS_array_read(frame, args_obj, 2);
        TRY_OBJID(arg);
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &begin));
        else if (arg == KOS_VOID)
            begin = 0;
        else {
            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        end = MAX_INT64;

        arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);
    }
    else {

        begin = 0;
        end   = MAX_INT64;
    }

    if (GET_OBJ_TYPE(this_obj) == OBJ_ARRAY)
        error = KOS_array_fill(frame, this_obj, begin, end, arg);

    else {

        int64_t        value;

        TRY(KOS_get_integer(frame, arg, &value));

        if (value < 0 || value > 255)
            RAISE_EXCEPTION(str_err_invalid_byte_value);

        error = KOS_buffer_fill(frame, this_obj, begin, end, (uint8_t)value);
    }

_error:
    return error ? KOS_BADPTR : this_obj;
}

struct _KOS_PACK_FORMAT {
    KOS_OBJ_ID fmt_str;
    KOS_OBJ_ID data;
    int        idx;
    int        big_end;
};

typedef int (*_KOS_PACK_FORMAT_FUNC)(KOS_FRAME                frame,
                                     struct _KOS_PACK_FORMAT *fmt,
                                     KOS_OBJ_ID               buffer_obj,
                                     char                     value_fmt,
                                     unsigned                 size,
                                     unsigned                 count);

static int _is_whitespace(unsigned char_code)
{
    return char_code == 0      || /* NUL */
           char_code == 9      || /* TAB */
           char_code == 11     || /* VTAB */
           char_code == 12     || /* FF */
           char_code == 32     || /* space */
           char_code == 0xA0   || /* NBSP */
           char_code == 0x2028 || /* line separator */
           char_code == 0x2029 || /* paragraph separator */
           char_code == 0xFEFF;   /* BOM */
}

static void _pack_format_skip_spaces(KOS_FRAME  frame,
                                     KOS_OBJ_ID fmt_str,
                                     unsigned  *i_ptr)
{
    const unsigned size = KOS_get_string_length(fmt_str);
    unsigned       i    = *i_ptr;
    unsigned       c;

    if (i >= size)
        return;

    do {
        c = KOS_string_get_char_code(frame, fmt_str, (int)i++);
        assert(c != ~0U);
    }
    while (i < size && _is_whitespace(c));

    if (i < size || ! _is_whitespace(c))
        i--;

    *i_ptr = i;
}

static unsigned _pack_format_get_count(KOS_FRAME  frame,
                                       KOS_OBJ_ID fmt_str,
                                       unsigned  *i_ptr)
{
    const unsigned size  = KOS_get_string_length(fmt_str);
    unsigned       i     = *i_ptr;
    unsigned       c;
    unsigned       count;

    assert(i < size);

    c = KOS_string_get_char_code(frame, fmt_str, (int)i++);

    assert(c >= '0' && c <= '9');

    count = c - (unsigned)'0';

    while (i < size) {

        c = KOS_string_get_char_code(frame, fmt_str, (int)i++);

        assert(c != ~0U);

        if (c < '0' || c > '9') {
            i--;
            break;
        }

        count = count * 10 + (c - (unsigned)'0');
    }

    *i_ptr = i;
    return count;
}

static int _process_pack_format(KOS_FRAME                frame,
                                KOS_OBJ_ID               buffer_obj,
                                _KOS_PACK_FORMAT_FUNC    handler,
                                struct _KOS_PACK_FORMAT *fmt)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_ID     fmt_str  = fmt->fmt_str;
    const unsigned fmt_size = KOS_get_string_length(fmt_str);
    unsigned       i_fmt    = 0;

    while (i_fmt < fmt_size) {

        unsigned count = 1;
        unsigned size  = 1;
        unsigned c;

        _pack_format_skip_spaces(frame, fmt_str, &i_fmt);

        if (i_fmt >= fmt_size)
            break;

        c = KOS_string_get_char_code(frame, fmt_str, (int)i_fmt++);
        assert(c != ~0U);

        if (c >= '0' && c <= '9') {
            --i_fmt;
            count = _pack_format_get_count(frame, fmt_str, &i_fmt);
            assert(count != ~0U);

            _pack_format_skip_spaces(frame, fmt_str, &i_fmt);

            if (i_fmt >= fmt_size)
                RAISE_EXCEPTION(str_err_invalid_pack_format);

            c = KOS_string_get_char_code(frame, fmt_str, (int)i_fmt++);
            assert(c != ~0U);
        }

        switch (c) {

            case '<':
                fmt->big_end = 0;
                break;

            case '>':
                fmt->big_end = 1;
                break;

            case 'x':
                break;

            case 'u':
                /* fall through */
            case 'i':
                /* fall through */
            case 'f':
                /* fall through */
            case 'b':
                /* fall through */
            case 's': {
                unsigned next_c;
                _pack_format_skip_spaces(frame, fmt_str, &i_fmt);
                next_c = (i_fmt < fmt_size) ? KOS_string_get_char_code(frame, fmt_str, (int)i_fmt) : ~0U;
                if (next_c >= '0' && next_c <= '9') {
                    size = _pack_format_get_count(frame, fmt_str, &i_fmt);
                }
                else if (c == 's') {
                    size = ~0U;
                }
                else
                    RAISE_EXCEPTION(str_err_invalid_pack_format);
                break;
            }

            default:
                RAISE_EXCEPTION(str_err_invalid_pack_format);
        }

        if (c != '<' && c != '>')
            TRY(handler(frame, fmt, buffer_obj, (char)c, size, count));
    }

_error:
    return error;
}

static int _pack_format(KOS_FRAME                frame,
                        struct _KOS_PACK_FORMAT *fmt,
                        KOS_OBJ_ID               buffer_obj,
                        char                     value_fmt,
                        unsigned                 size,
                        unsigned                 count)
{
    int                error = KOS_SUCCESS;
    int                big_end;
    uint8_t           *dst   = 0;
    struct _KOS_VECTOR str_buf;

    _KOS_vector_init(&str_buf);

    if (fmt->idx < 0) {
        KOS_OBJ_ID obj = fmt->data;

        fmt->idx = 1;

        if (KOS_get_array_size(obj) > 1) {

            obj = KOS_array_read(frame, obj, 1);

            if (GET_OBJ_TYPE(obj) == OBJ_ARRAY) {
                fmt->data = obj;
                fmt->idx  = 0;
            }
        }
    }

    assert(size != ~0U || value_fmt == 's');

    if (size != ~0U && size && count) {
        dst = KOS_buffer_make_room(frame, buffer_obj, size * count);
        if ( ! dst)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else if (size == ~0U && value_fmt != 's')
        RAISE_EXCEPTION(str_err_invalid_pack_format);

    big_end = fmt->big_end;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);

            if (count)
                memset(dst, 0, size * count);
            break;

        case 'u':
            /* fall through */
        case 'i': {

            if (size != 1 && size != 2 && size != 4 && size != 8)
                RAISE_EXCEPTION(str_err_invalid_pack_format);

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(str_err_not_enough_pack_values);

            for ( ; count; count--) {
                unsigned   i;
                int64_t    value;
                KOS_OBJ_ID value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);

                TRY_OBJID(value_obj);

                if ( ! IS_NUMERIC_OBJ(value_obj))
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                TRY(KOS_get_integer(frame, value_obj, &value));

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? (size - 1U - i) : i;
                    dst[offs] = (uint8_t)(value & 0xFF);
                    value >>= 8;
                }

                dst += size;
            }
            break;
        }

        case 'f': {

            if (size != 4 && size != 8)
                RAISE_EXCEPTION(str_err_invalid_pack_format);

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(str_err_not_enough_pack_values);

            for ( ; count; count--) {
                unsigned   i;
                KOS_OBJ_ID value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                double     value     = 0;
                uint64_t   out_val;

                TRY_OBJID(value_obj);

                if (IS_NUMERIC_OBJ(value_obj)) {

                    switch (GET_NUMERIC_TYPE(value_obj)) {

                        default:
                            value = (double)GET_SMALL_INT(value_obj);
                            break;

                        case OBJ_NUM_INTEGER:
                            value = (double)*OBJPTR(INTEGER, value_obj);
                            break;

                        case OBJ_NUM_FLOAT:
                            value = *OBJPTR(FLOAT, value_obj);
                            break;
                    }
                }
                else
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                if (size == 4)
                    out_val = _KOS_float_to_uint32_t((float)value);
                else
                    out_val = _KOS_double_to_uint64_t(value);

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? (size - 1U - i) : i;
                    dst[offs] = (uint8_t)(out_val & 0xFFU);
                    out_val >>= 8;
                }

                dst += size;
            }
            break;
        }

        case 'b': {

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(str_err_not_enough_pack_values);

            for ( ; count; count--) {
                KOS_OBJ_ID value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                uint8_t   *data      = 0;
                uint32_t   data_size;
                uint32_t   copy_size;

                TRY_OBJID(value_obj);

                if (GET_OBJ_TYPE(value_obj) != OBJ_BUFFER)
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                data_size = KOS_get_buffer_size(value_obj);
                if (data_size)
                    data = KOS_buffer_data(value_obj);

                copy_size = size > data_size ? data_size : size;

                if (copy_size)
                    /* TODO what if input buf == output buf ? */
                    memcpy(dst, data, copy_size);

                if (copy_size < size)
                    memset(dst + copy_size, 0, size - copy_size);

                dst += size;
            }
            break;
        }

        case 's':
            /* fall through */
        default: {

            assert(value_fmt == 's');

            if ((unsigned)fmt->idx + count > KOS_get_array_size(fmt->data))
                RAISE_EXCEPTION(str_err_not_enough_pack_values);

            for ( ; count; count--) {
                KOS_OBJ_ID value_obj = KOS_array_read(frame, fmt->data, fmt->idx++);
                uint32_t   copy_size;

                TRY_OBJID(value_obj);

                if (GET_OBJ_TYPE(value_obj) != OBJ_STRING)
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                TRY(KOS_string_to_cstr_vec(frame, value_obj, &str_buf));

                copy_size = size > str_buf.size-1 ? (uint32_t)str_buf.size-1 : size;

                if (size == ~0U)
                    dst = KOS_buffer_make_room(frame, buffer_obj, copy_size);

                if (copy_size)
                    memcpy(dst, str_buf.buffer, copy_size);

                if (size != ~0U) {
                    if (copy_size < size)
                        memset(dst + copy_size, 0, size - copy_size);

                    dst += size;
                }
            }
            break;
        }
    }

_error:
    _KOS_vector_destroy(&str_buf);
    return error;
}

static int _unpack_format(KOS_FRAME                frame,
                          struct _KOS_PACK_FORMAT *fmt,
                          KOS_OBJ_ID               buffer_obj,
                          char                     value_fmt,
                          unsigned                 size,
                          unsigned                 count)
{
    int            error     = KOS_SUCCESS;
    uint8_t       *data      = 0;
    const uint32_t data_size = KOS_get_buffer_size(buffer_obj);
    int            big_end   = fmt->big_end;
    KOS_OBJ_ID     obj;

    if (size == ~0U) {
        if (value_fmt != 's' || count != 1)
            RAISE_EXCEPTION(str_err_invalid_pack_format);

        size = data_size - fmt->idx;
    }

    if (fmt->idx + size * count > data_size)
        RAISE_EXCEPTION(str_err_unpack_buf_too_short);

    assert(data_size);
    data = KOS_buffer_data(buffer_obj);
    assert(data);

    data += fmt->idx;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);
            data += size * count;
            break;

        case 'f':
            /* fall through */
        case 'i':
            /* fall through */
        case 'u': {
            assert(size == 1 || size == 2 || size == 4 || size == 8);
            for ( ; count; count--) {
                uint64_t value = 0;
                unsigned i;

                for (i = 0; i < size; i++) {
                    const unsigned offs = big_end ? i : (size - 1 - i);
                    value = (value << 8) | data[offs];
                }

                if (value_fmt == 'i' && size < 8) {
                    const unsigned shift = 64 - 8 * size;
                    const int64_t  ival  = (int64_t)(value << shift);
                    obj = KOS_new_int(frame, ival >> shift);
                }
                else if (value_fmt == 'f') {
                    double fvalue;
                    if (size == 4) {
                        union {
                            float    f;
                            uint32_t u;
                        } u2f;
                        u2f.u  = (uint32_t)value;
                        fvalue = u2f.f;
                    }
                    else {
                        union {
                            double   f;
                            uint64_t u;
                        } u2f;
                        u2f.u  = value;
                        fvalue = u2f.f;
                    }
                    obj = KOS_new_float(frame, fvalue);
                }
                else
                    obj = KOS_new_int(frame, (int64_t)value);

                TRY_OBJID(obj);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }

        case 'b': {
            for ( ; count; count--) {
                obj = KOS_new_buffer(frame, size);

                TRY_OBJID(obj);

                if (size)
                    memcpy(KOS_buffer_data(obj), data, size);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }

        case 's':
            /* fall through */
        default: {
            assert(value_fmt == 's');
            for ( ; count; count--) {
                obj = KOS_new_string(frame, (char *)data, size);

                TRY_OBJID(obj);

                TRY(KOS_array_push(frame, fmt->data, obj, 0));

                data += size;
            }
            break;
        }
    }

    fmt->idx = (int)(data - KOS_buffer_data(buffer_obj));

_error:
    return error;
}

static KOS_OBJ_ID _pack(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = KOS_array_read(frame, args_obj, 0);
    fmt.data    = args_obj;
    fmt.idx     = -1;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(fmt.fmt_str));

    if (GET_OBJ_TYPE(fmt.fmt_str) == OBJ_STRING)
        error = _process_pack_format(frame, this_obj, _pack_format, &fmt);
    else {
        KOS_raise_exception_cstring(frame, str_err_not_string);
        error = KOS_ERROR_EXCEPTION;
    }

    return error ? KOS_BADPTR : this_obj;
}

static KOS_OBJ_ID _unpack(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    int                     error;
    struct _KOS_PACK_FORMAT fmt;

    fmt.fmt_str = KOS_BADPTR;
    fmt.data    = KOS_BADPTR;
    fmt.idx     = 0;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_BUFFER)
        RAISE_EXCEPTION(str_err_not_buffer);

    fmt.fmt_str = KOS_array_read(frame, args_obj, 0);
    fmt.data    = KOS_new_array(frame, 0);

    TRY_OBJID(fmt.fmt_str);
    TRY_OBJID(fmt.data);

    if (IS_NUMERIC_OBJ(fmt.fmt_str)) {
        int64_t idx = 0;

        TRY(KOS_get_integer(frame, fmt.fmt_str, &idx));

        idx = _KOS_fix_index(idx, KOS_get_buffer_size(this_obj));

        fmt.idx = (int)idx;

        fmt.fmt_str = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(fmt.fmt_str);
    }

    if (GET_OBJ_TYPE(fmt.fmt_str) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    TRY(_process_pack_format(frame, this_obj, _unpack_format, &fmt));

_error:
    return error ? KOS_BADPTR : fmt.data;
}

static KOS_OBJ_ID _copy_buffer(KOS_FRAME  frame,
                               KOS_OBJ_ID this_obj,
                               KOS_OBJ_ID args_obj)
{
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     arg        = KOS_array_read(frame, args_obj, 0);
    int64_t        dest_begin = 0;
    int64_t        src_begin  = 0;
    int64_t        src_end    = MAX_INT64;
    KOS_OBJ_ID     src;

    if (num_args > 3) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &dest_begin));
        else if (arg != KOS_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        src = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(src);

        arg = KOS_array_read(frame, args_obj, 2);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_begin));
        else if (arg != KOS_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        arg = KOS_array_read(frame, args_obj, 3);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_end));
        else if (arg != KOS_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);
    }
    else if (num_args > 2) {

        int arg_idx = 1;

        if (IS_NUMERIC_OBJ(arg) || arg == KOS_VOID) {

            arg_idx = 2;

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &dest_begin));

            src = KOS_array_read(frame, args_obj, 1);
            TRY_OBJID(src);
        }
        else
            src = arg;

        arg = KOS_array_read(frame, args_obj, arg_idx);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(frame, arg, &src_begin));
        else if (arg != KOS_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        if (arg_idx == 1) {

            arg = KOS_array_read(frame, args_obj, arg_idx+1);
            TRY_OBJID(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &src_end));
            else if (arg != KOS_VOID)
                RAISE_EXCEPTION(str_err_unsup_operand_types);
        }
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg) || arg == KOS_VOID) {

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &dest_begin));

            src = KOS_array_read(frame, args_obj, 1);
            TRY_OBJID(src);
        }
        else {

            src = arg;

            arg = KOS_array_read(frame, args_obj, 1);
            TRY_OBJID(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(frame, arg, &src_begin));
            else if (arg != KOS_VOID)
                RAISE_EXCEPTION(str_err_unsup_operand_types);
        }
    }
    else {

        src        = arg;
        dest_begin = 0;
        src_begin  = 0;
        src_end    = MAX_INT64;
    }

    error = KOS_buffer_copy(frame, this_obj, dest_begin, src, src_begin, src_end);

_error:
    return error ? KOS_BADPTR : this_obj;
}

static KOS_OBJ_ID _reserve(KOS_FRAME  frame,
                           KOS_OBJ_ID this_obj,
                           KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID size_obj;
    int64_t    size;

    size_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(size_obj);

    TRY(KOS_get_integer(frame, size_obj, &size));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_buffer_size);

        TRY(KOS_buffer_reserve(frame, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_array_size);

        TRY(KOS_array_reserve(frame, this_obj, (uint32_t)size));
    }

_error:
    return error ? KOS_BADPTR : this_obj;
}

static KOS_OBJ_ID _insert_array(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     begin_obj;
    KOS_OBJ_ID     end_obj;
    KOS_OBJ_ID     src_obj;
    int64_t        begin    = 0;
    int64_t        end      = 0;
    int64_t        src_len;

    begin_obj = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(begin_obj);

    end_obj = KOS_array_read(frame, args_obj, 1);
    TRY_OBJID(end_obj);

    if (num_args > 2) {
        src_obj = KOS_array_read(frame, args_obj, 2);
        TRY_OBJID(src_obj);
    }
    else {
        src_obj = end_obj;
        end_obj = begin_obj;
    }

    if (GET_OBJ_TYPE(this_obj) != OBJ_ARRAY ||
        GET_OBJ_TYPE(src_obj)  != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    if (IS_NUMERIC_OBJ(begin_obj))
        TRY(KOS_get_integer(frame, begin_obj, &begin));
    else if (begin_obj == KOS_VOID)
        begin = num_args == 2 ? MAX_INT64 : 0;
    else
        RAISE_EXCEPTION(str_err_unsup_operand_types);

    if (IS_NUMERIC_OBJ(end_obj))
        TRY(KOS_get_integer(frame, end_obj, &end));
    else if (end_obj == KOS_VOID)
        end = MAX_INT64;
    else
        RAISE_EXCEPTION(str_err_unsup_operand_types);

    src_len = MAX_INT64;

    TRY(KOS_array_insert(frame, this_obj, begin, end, src_obj, 0, src_len));

_error:
    return error ? KOS_BADPTR : this_obj;
}

static KOS_OBJ_ID _pop(KOS_FRAME  frame,
                       KOS_OBJ_ID this_obj,
                       KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_ID     ret      = KOS_BADPTR;
    const uint32_t num_args = KOS_get_array_size(args_obj);

    if (num_args == 0)
        ret = KOS_array_pop(frame, this_obj);

    else {
        int64_t    num = 0;
        int        idx;
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &num));

        if (num < 0 || num > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_array_size);

        ret = KOS_new_array(frame, (unsigned)num);
        TRY_OBJID(ret);

        for (idx = (int)(num - 1); idx >= 0; idx--) {
            arg = KOS_array_pop(frame, this_obj);
            TRY_OBJID(arg);

            TRY(KOS_array_write(frame, ret, idx, arg));
        }
    }

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _push(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_VOID;
    uint32_t       i;

    for (i = 0; i < num_args; i++) {
        uint32_t   idx      = ~0U;
        KOS_OBJ_ID elem_obj = KOS_array_read(frame, args_obj, (int)i);
        TRY_OBJID(elem_obj);

        TRY(KOS_array_push(frame, this_obj, elem_obj, &idx));

        if (i == 0) {
            ret = KOS_new_int(frame, (int64_t)idx);
            TRY_OBJID(ret);
        }
    }

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _ends_with(KOS_FRAME  frame,
                             KOS_OBJ_ID this_obj,
                             KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    unsigned   this_len;
    unsigned   arg_len;

    arg = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(arg) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    this_len = KOS_get_string_length(this_obj);
    arg_len  = KOS_get_string_length(arg);

    if (arg_len > this_len)
        ret = KOS_FALSE;
    else
        ret = KOS_string_compare_slice(this_obj,
                                       this_len - arg_len,
                                       this_len,
                                       arg,
                                       0,
                                       arg_len)
            ? KOS_FALSE : KOS_TRUE;

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _repeat(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg   = KOS_array_read(frame, args_obj, 0);
    KOS_OBJ_ID ret   = KOS_BADPTR;
    int64_t    num;
    unsigned   text_len;

    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    TRY(KOS_get_integer(frame, arg, &num));

    text_len = KOS_get_string_length(this_obj);

    if (num < 0 || num > 0xFFFFU || (num * text_len) > 0xFFFFU)
        RAISE_EXCEPTION(str_err_too_many_repeats);

    ret = KOS_string_repeat(frame, this_obj, (unsigned)num);

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _find(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int        error   = KOS_SUCCESS;
    int        pos     = 0;
    KOS_OBJ_ID pattern = KOS_array_read(frame, args_obj, 0);

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t        pos64;
        const unsigned len = KOS_get_string_length(this_obj);

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &pos64));

        pos = (int)_KOS_fix_index(pos64, len);
    }

    TRY(KOS_string_find(frame, this_obj, pattern, KOS_FIND_FORWARD, &pos));

_error:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

static KOS_OBJ_ID _rfind(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int        error   = KOS_SUCCESS;
    int        pos     = -1;
    KOS_OBJ_ID pattern = KOS_array_read(frame, args_obj, 0);
    unsigned   text_len;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    text_len = KOS_get_string_length(this_obj);
    pos      = (int)(text_len - KOS_get_string_length(pattern));

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t pos64;

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &pos64));

        if (pos64 < -(int64_t)text_len)
            pos = -1;
        else {
            const int new_pos = (int)_KOS_fix_index(pos64, text_len);

            if (new_pos < pos)
                pos = new_pos;
        }
    }

    TRY(KOS_string_find(frame, this_obj, pattern, KOS_FIND_REVERSE, &pos));

_error:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

static KOS_OBJ_ID _scan(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int                    error   = KOS_SUCCESS;
    int                    pos     = 0;
    KOS_OBJ_ID             pattern = KOS_array_read(frame, args_obj, 0);
    enum _KOS_SCAN_INCLUDE include = KOS_SCAN_INCLUDE;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t        pos64;
        const unsigned len = KOS_get_string_length(this_obj);

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);

        if (arg == KOS_FALSE)
            include = KOS_SCAN_EXCLUDE;

        else if (arg != KOS_TRUE) {

            TRY(KOS_get_integer(frame, arg, &pos64));

            pos = (int)_KOS_fix_index(pos64, len);

            if (KOS_get_array_size(args_obj) > 2) {

                arg = KOS_array_read(frame, args_obj, 2);
                TRY_OBJID(arg);

                if (arg == KOS_FALSE)
                    include = KOS_SCAN_EXCLUDE;
                else if (arg != KOS_TRUE)
                    RAISE_EXCEPTION(str_err_not_boolean);
            }
        }
    }

    TRY(KOS_string_scan(frame, this_obj, pattern, KOS_FIND_FORWARD, include, &pos));

_error:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

static KOS_OBJ_ID _rscan(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int                    error   = KOS_SUCCESS;
    int                    pos     = -1;
    KOS_OBJ_ID             pattern = KOS_array_read(frame, args_obj, 0);
    enum _KOS_SCAN_INCLUDE include = KOS_SCAN_INCLUDE;
    unsigned               text_len;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    text_len = KOS_get_string_length(this_obj);
    pos      = (int)(text_len - 1);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t pos64;

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 1);
        TRY_OBJID(arg);

        if (arg == KOS_FALSE)
            include = KOS_SCAN_EXCLUDE;

        else if (arg != KOS_TRUE) {

            TRY(KOS_get_integer(frame, arg, &pos64));

            if (pos64 < -(int64_t)text_len)
                pos = -1;
            else {
                const int new_pos = (int)_KOS_fix_index(pos64, text_len);

                if (new_pos < pos)
                    pos = new_pos;
            }

            if (KOS_get_array_size(args_obj) > 2) {

                arg = KOS_array_read(frame, args_obj, 2);
                TRY_OBJID(arg);

                if (arg == KOS_FALSE)
                    include = KOS_SCAN_EXCLUDE;
                else if (arg != KOS_TRUE)
                    RAISE_EXCEPTION(str_err_not_boolean);
            }
        }
    }

    TRY(KOS_string_scan(frame, this_obj, pattern, KOS_FIND_REVERSE, include, &pos));

_error:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

static KOS_OBJ_ID _ord(KOS_FRAME  frame,
                       KOS_OBJ_ID this_obj,
                       KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    int64_t    idx   = 0;
    unsigned   code;

    if (KOS_get_array_size(args_obj) > 0) {

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &idx));

        if (idx < INT_MIN || idx > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_string_idx);
    }

    code = KOS_string_get_char_code(frame, this_obj, (int)idx);
    if (code == ~0U)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    ret = KOS_new_int(frame, (int64_t)code);

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _starts_with(KOS_FRAME  frame,
                               KOS_OBJ_ID this_obj,
                               KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    unsigned   this_len;
    unsigned   arg_len;

    arg = KOS_array_read(frame, args_obj, 0);
    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(arg) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    this_len = KOS_get_string_length(this_obj);
    arg_len  = KOS_get_string_length(arg);

    if (arg_len > this_len)
        ret = KOS_FALSE;
    else
        ret = KOS_string_compare_slice(this_obj,
                                       0,
                                       arg_len,
                                       arg,
                                       0,
                                       arg_len)
            ? KOS_FALSE : KOS_TRUE;

_error:
    return error ? KOS_BADPTR : ret;
}

static KOS_OBJ_ID _get_string_size(KOS_FRAME  frame,
                                   KOS_OBJ_ID this_obj,
                                   KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_STRING)
        ret = KOS_new_int(frame, (int64_t)KOS_get_string_length(this_obj));
    else {
        KOS_raise_exception_cstring(frame, str_err_not_string);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _reverse(KOS_FRAME  frame,
                           KOS_OBJ_ID this_obj,
                           KOS_OBJ_ID args_obj)
{
    return KOS_string_reverse(frame, this_obj);
}

static KOS_OBJ_ID _get_function_name(KOS_FRAME  frame,
                                     KOS_OBJ_ID this_obj,
                                     KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func = OBJPTR(FUNCTION, this_obj);

        /* TODO add builtin function name */
        if ( ! func->module || func->instr_offs == ~0U)
            ret = KOS_context_get_cstring(frame, str_builtin);
        else
            ret = KOS_module_addr_to_func_name(func->module,
                                               func->instr_offs);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _get_instructions(KOS_FRAME  frame,
                                    KOS_OBJ_ID this_obj,
                                    KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func      = OBJPTR(FUNCTION, this_obj);
        uint32_t            num_instr = 0;

        if (func->module)
            num_instr = KOS_module_func_get_num_instr(func->module,
                                                      func->instr_offs);

        ret = KOS_new_int(frame, (int64_t)num_instr);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _get_code_size(KOS_FRAME  frame,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func      = OBJPTR(FUNCTION, this_obj);
        uint32_t            code_size = 0;

        if (func->module)
            code_size = KOS_module_func_get_code_size(func->module,
                                                      func->instr_offs);

        ret = KOS_new_int(frame, (int64_t)code_size);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _get_prototype(KOS_FRAME  frame,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func = OBJPTR(FUNCTION, this_obj);

        ret = (KOS_OBJ_ID)KOS_atomic_read_ptr(func->prototype);

        assert( ! IS_BAD_PTR(ret));
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _get_registers(KOS_FRAME  frame,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_FUNCTION) {

        KOS_FUNCTION *const func = OBJPTR(FUNCTION, this_obj);

        ret = KOS_new_int(frame, (int64_t)func->num_regs);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _print_exception(KOS_FRAME  frame,
                                   KOS_OBJ_ID this_obj,
                                   KOS_OBJ_ID args_obj)
{
    int                error;
    KOS_OBJ_ID         ret       = KOS_BADPTR;
    uint32_t           i;
    uint32_t           lines;
    KOS_OBJ_ID         formatted = KOS_format_exception(frame, this_obj);
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    TRY_OBJID(formatted);

    assert(GET_OBJ_TYPE(formatted) == OBJ_ARRAY);

    lines = KOS_get_array_size(formatted);

    for (i = 0; i < lines; i++) {
        KOS_OBJ_ID line = KOS_array_read(frame, formatted, (int)i);
        TRY_OBJID(line);
        TRY(KOS_string_to_cstr_vec(frame, line, &cstr));
        if (cstr.size) {
            cstr.buffer[cstr.size - 1] = '\n';
            fwrite(cstr.buffer, 1, cstr.size, stdout);
        }
        else
            printf("\n");
    }

    ret = this_obj;

_error:
    _KOS_vector_destroy(&cstr);
    return ret;
}

int _KOS_module_lang_init(KOS_FRAME frame)
{
    int error = KOS_SUCCESS;

    TRY_ADD_FUNCTION( frame, "print",     _print,     0);
    TRY_ADD_FUNCTION( frame, "print_",    _print_,    0);
    TRY_ADD_FUNCTION( frame, "stringify", _stringify, 0);
    TRY_ADD_GENERATOR(frame, "deep",      _deep,      1);
    TRY_ADD_GENERATOR(frame, "shallow",   _shallow,   1);

    {
        KOS_CONTEXT *const ctx = KOS_context_from_frame(frame);
        TRY(KOS_module_add_global(frame, KOS_context_get_cstring(frame, str_args), ctx->args, 0));
    }

    TRY_CREATE_CONSTRUCTOR(array);
    TRY_CREATE_CONSTRUCTOR(boolean);
    TRY_CREATE_CONSTRUCTOR(buffer);
    TRY_CREATE_CONSTRUCTOR(constructor);
    TRY_CREATE_CONSTRUCTOR(exception);
    TRY_CREATE_CONSTRUCTOR(float);
    TRY_CREATE_CONSTRUCTOR(function);
    TRY_CREATE_CONSTRUCTOR(generator);
    TRY_CREATE_CONSTRUCTOR(generator_end);
    TRY_CREATE_CONSTRUCTOR(integer);
    TRY_CREATE_CONSTRUCTOR(number);
    TRY_CREATE_CONSTRUCTOR(object);
    TRY_CREATE_CONSTRUCTOR(string);
    TRY_CREATE_CONSTRUCTOR(thread);
    TRY_CREATE_CONSTRUCTOR(void);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "insert_array",  _insert_array,      2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "fill",          _fill,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "pop",           _pop,               0);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "push",          _push,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "reserve",       _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "resize",        _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(array),      "slice",         _slice,             2);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(array),      "size",          _get_array_size,    0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "copy_buffer",   _copy_buffer,       1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "fill",          _fill,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "pack",          _pack,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "reserve",       _reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "resize",        _resize,            1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "slice",         _slice,             2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(buffer),     "unpack",        _unpack,            1);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(buffer),     "size",          _get_buffer_size,   0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(constructor),"set_prototype", _set_prototype,     1);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(exception),  "print",         _print_exception,   0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(function),   "apply",         _apply,             2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(function),   "async",         _async,             0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function),   "instructions",  _get_instructions,  0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function),   "name",          _get_function_name, 0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function),   "prototype",     _get_prototype,     0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function),   "registers",     _get_registers,     0);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(function),   "size",          _get_code_size,     0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "ends_with",     _ends_with,         1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "find",          _find,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "ord",           _ord,               0);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "repeat",        _repeat,            1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "rfind",         _rfind,             1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "rscan",         _rscan,             1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "reverse",       _reverse,           0);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "scan",          _scan,              1);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "slice",         _slice,             2);
    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(string),     "starts_with",   _starts_with,       1);
    TRY_ADD_MEMBER_PROPERTY( frame, PROTO(string),     "size",          _get_string_size,   0);

    TRY_ADD_MEMBER_FUNCTION( frame, PROTO(thread),     "wait",          _wait,              0);

    TRY_ADD_MEMBER_GENERATOR(frame, PROTO(void),       "iterator",      _iterator,          0);

_error:
    return error;
}

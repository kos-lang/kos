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
#include "../inc/kos_buffer.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_malloc.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>

static const char str_err_already_joined[]           = "thread already joined";
static const char str_err_args_not_array[]           = "function arguments are not an array";
static const char str_err_bad_number[]               = "number parse failed";
static const char str_err_bad_pack_value[]           = "invalid value type for pack format";
static const char str_err_cannot_convert_to_array[]  = "unsupported type passed to array class";
static const char str_err_cannot_convert_to_buffer[] = "unsupported type passed to buffer class";
static const char str_err_cannot_convert_to_string[] = "unsupported type passed to string class";
static const char str_err_invalid_array_size[]       = "array size out of range";
static const char str_err_invalid_byte_value[]       = "buffer element value out of range";
static const char str_err_invalid_char_code[]        = "invalid character code";
static const char str_err_invalid_buffer_size[]      = "buffer size out of range";
static const char str_err_invalid_key_type[]         = "invalid key type, must be function or void";
static const char str_err_invalid_pack_format[]      = "invalid pack format";
static const char str_err_invalid_reverse_type[]     = "invalid reverse type, must be boolean";
static const char str_err_invalid_string[]           = "invalid string";
static const char str_err_invalid_string_idx[]       = "string index is out of range";
static const char str_err_join_self[]                = "thread cannot join itself";
static const char str_err_not_array[]                = "object is not an array";
static const char str_err_not_boolean[]              = "object is not a boolean";
static const char str_err_not_buffer[]               = "object is not a buffer";
static const char str_err_not_class[]                = "object is not a class";
static const char str_err_not_enough_pack_values[]   = "insufficient number of packed values";
static const char str_err_not_function[]             = "object is not a function";
static const char str_err_not_generator[]            = "object is not a generator";
static const char str_err_not_string[]               = "object is not a string";
static const char str_err_not_thread[]               = "object is not a thread";
static const char str_err_too_many_repeats[]         = "invalid string repeat count";
static const char str_err_unpack_buf_too_short[]     = "unpacked buffer too short";
static const char str_err_unsup_operand_types[]      = "unsupported operand types";
static const char str_err_use_async[]                = "use async to launch threads";

#define TRY_CREATE_CONSTRUCTOR(name, module)               \
do {                                                       \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, #name);      \
    TRY(create_class(ctx,                                  \
                     module,                               \
                     KOS_CONST_ID(str_name),               \
                     name##_constructor,                   \
                     ctx->inst->prototypes.name##_proto)); \
} while (0)

#define PROTO(type) (ctx->inst->prototypes.type##_proto)

/* @item base print()
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
static KOS_OBJ_ID print(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    TRY(KOS_print_to_cstr_vec(ctx, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

    KOS_suspend_context(ctx);

    if (cstr.size) {
        cstr.buffer[cstr.size - 1] = '\n';
        fwrite(cstr.buffer, 1, cstr.size, stdout);
    }
    else
        printf("\n");

    KOS_resume_context(ctx);

cleanup:
    kos_vector_destroy(&cstr);

    return error ? KOS_BADPTR : KOS_VOID;
}

static KOS_OBJ_ID object_iterator(KOS_CONTEXT                  ctx,
                                  KOS_OBJ_ID                   regs_obj,
                                  KOS_OBJ_ID                   args_obj,
                                  enum KOS_OBJECT_WALK_DEPTH_E deep)
{
    int        error;
    int        pushed = 0;
    KOS_OBJ_ID ret    = KOS_BADPTR;
    KOS_OBJ_ID array  = KOS_BADPTR;
    KOS_OBJ_ID walk   = KOS_BADPTR;
    KOS_OBJ_ID value  = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 4, &regs_obj, &array, &walk, &value));

    assert( ! IS_BAD_PTR(regs_obj));
    TRY_OBJID(regs_obj);

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);
    assert(KOS_get_array_size(regs_obj) > 0);

    walk = KOS_array_read(ctx, regs_obj, 0);
    assert( ! IS_BAD_PTR(walk));
    TRY_OBJID(walk);

    if (GET_OBJ_TYPE(walk) != OBJ_OBJECT_WALK) {
        walk = KOS_new_object_walk(ctx, walk, deep);
        TRY_OBJID(walk);

        TRY(KOS_array_write(ctx, regs_obj, 0, walk));
    }

    {
        array = KOS_new_array(ctx, 2);
        TRY_OBJID(array);

        if ( ! KOS_object_walk(ctx, walk)) {

            value = KOS_get_walk_value(walk);

            assert( ! IS_BAD_PTR(KOS_get_walk_key(walk)));
            assert( ! IS_BAD_PTR(value));

            if (GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                KOS_OBJ_ID args = KOS_new_array(ctx, 0);
                TRY_OBJID(args);

                value = KOS_call_function(ctx,
                                          OBJPTR(DYNAMIC_PROP, value)->getter,
                                          OBJPTR(OBJECT_WALK, walk)->obj,
                                          args);
                if (IS_BAD_PTR(value)) {
                    assert(KOS_is_exception_pending(ctx));
                    KOS_clear_exception(ctx);

                    value = OBJPTR(DYNAMIC_PROP, KOS_get_walk_value(walk))->getter;
                }
            }

            TRY(KOS_array_write(ctx, array, 0, KOS_get_walk_key(walk)));
            TRY(KOS_array_write(ctx, array, 1, value));

            ret = array;
        }
    }

cleanup:
    return ret;
}

/* @item base shallow()
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
 *     > [ shallow({x:0, y:1}) ... ]
 *     [["y", 1], ["x", 0]]
 */
static KOS_OBJ_ID shallow(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  regs_obj,
                          KOS_OBJ_ID  args_obj)
{
    return object_iterator(ctx, regs_obj, args_obj, KOS_SHALLOW);
}

/* @item base deep()
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
 *     > [ deep({x:0, y:1}) ... ]
 *     [["any", <function>], ["all", <function>], ["filter", <function>],
 *      ["count", <function>], ["reduce", <function>], ["iterator", <function>],
 *      ["map", <function>], ["y", 1], ["x", 0]]
 */
static KOS_OBJ_ID deep(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  regs_obj,
                       KOS_OBJ_ID  args_obj)
{
    return object_iterator(ctx, regs_obj, args_obj, KOS_DEEP);
}

static int create_class(KOS_CONTEXT          ctx,
                        KOS_OBJ_ID           module_obj,
                        KOS_OBJ_ID           str_name,
                        KOS_FUNCTION_HANDLER constructor,
                        KOS_OBJ_ID           prototype)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID func_obj = KOS_BADPTR;
    int        pushed   = 0;

    TRY(KOS_push_locals(ctx, &pushed, 3, &module_obj, &str_name, &prototype));

    func_obj = KOS_new_class(ctx, prototype);
    TRY_OBJID(func_obj);

    OBJPTR(CLASS, func_obj)->handler = constructor;
    OBJPTR(CLASS, func_obj)->module  = module_obj;

    TRY(KOS_module_add_global(ctx,
                              module_obj,
                              str_name,
                              func_obj,
                              0));

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

/* @item base number()
 *
 *     number(value = 0)
 *
 * Numeric type class.
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
static KOS_OBJ_ID number_constructor(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  this_obj,
                                     KOS_OBJ_ID  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_NUMERIC_OBJ(arg))
            ret = arg;
        else if (READ_OBJ_TYPE(arg) == OBJ_STRING) {

            KOS_VECTOR cstr;

            kos_vector_init(&cstr);

            if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, arg, &cstr)) {

                const char *begin = cstr.buffer;
                const char *end   = begin + cstr.size - 1;
                KOS_NUMERIC numeric;

                assert(begin <= end);

                if (KOS_SUCCESS == kos_parse_numeric(begin, end, &numeric)) {

                    if (numeric.type == KOS_INTEGER_VALUE)
                        ret = KOS_new_int(ctx, numeric.u.i);
                    else {
                        assert(numeric.type == KOS_FLOAT_VALUE);
                        ret = KOS_new_float(ctx, numeric.u.d);
                    }
                }
                else
                    KOS_raise_exception_cstring(ctx, str_err_bad_number);
            }

            kos_vector_destroy(&cstr);
        }
        else
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
    }

    return ret;
}

/* @item base integer()
 *
 *     integer(value = 0)
 *
 * Integer type class.
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
static KOS_OBJ_ID integer_constructor(KOS_CONTEXT ctx,
                                      KOS_OBJ_ID  this_obj,
                                      KOS_OBJ_ID  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args == 0)
        ret = TO_SMALL_INT(0);
    else {
        int64_t    value;
        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_NUMERIC_OBJ(arg)) {
            if (KOS_get_integer(ctx, arg, &value) == KOS_SUCCESS)
                ret = KOS_new_int(ctx, value);
        }
        else if (READ_OBJ_TYPE(arg) == OBJ_STRING) {

            KOS_VECTOR cstr;

            kos_vector_init(&cstr);

            if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, arg, &cstr)) {

                const char *begin = cstr.buffer;
                const char *end   = begin + cstr.size - 1;
                int         error;

                assert(begin <= end);

                error = kos_parse_int(begin, end, &value);

                if (error)
                    KOS_raise_exception_cstring(ctx, str_err_bad_number);
                else
                    ret = KOS_new_int(ctx, value);
            }

            kos_vector_destroy(&cstr);
        }
        else if ( ! IS_BAD_PTR(arg))
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
    }

    return ret;
}

/* @item base float()
 *
 *     float(value = 0.0)
 *
 * Float type class.
 *
 * The optional `value` argument can be an integer, a float or a string.
 *
 * If `value` is not provided, returns `0.0`.
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
static KOS_OBJ_ID float_constructor(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;
    KOS_OBJ_ID     arg;

    if (num_args == 0)
        return KOS_new_float(ctx, 0);

    arg = KOS_array_read(ctx, args_obj, 0);

    if (IS_BAD_PTR(arg))
        return arg;

    if (IS_SMALL_INT(arg))
        return KOS_new_float(ctx, (double)GET_SMALL_INT(arg));

    switch (READ_OBJ_TYPE(arg)) {

        case OBJ_INTEGER:
            ret = KOS_new_float(ctx, (double)(OBJPTR(INTEGER, arg)->value));
            break;

        case OBJ_FLOAT:
            ret = arg;
            break;

        case OBJ_STRING: {

            KOS_VECTOR cstr;

            kos_vector_init(&cstr);

            if (KOS_string_to_cstr_vec(ctx, arg, &cstr) == KOS_SUCCESS) {

                const char *begin = cstr.buffer;
                const char *end   = begin + cstr.size - 1;
                double      value;
                int         error;

                assert(begin <= end);

                error = kos_parse_double(begin, end, &value);

                if (error)
                    KOS_raise_exception_cstring(ctx, str_err_bad_number);
                else
                    ret = KOS_new_float(ctx, value);
            }

            kos_vector_destroy(&cstr);
            break;
        }

        default:
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
            break;
    }

    return ret;
}

/* @item base boolean()
 *
 *     boolean(value = false)
 *
 * Boolean type class.
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
static KOS_OBJ_ID boolean_constructor(KOS_CONTEXT ctx,
                                      KOS_OBJ_ID  this_obj,
                                      KOS_OBJ_ID  args_obj)
{
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    if (num_args > 0) {
        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        if ( ! IS_BAD_PTR(arg))
            ret = KOS_BOOL(kos_is_truthy(arg));
    }
    else
        ret = KOS_FALSE;

    return ret;
}

/* @item base string()
 *
 *     string(args...)
 *
 * String type class.
 *
 * Returns a new string created from converting all arguments to strings
 * and concatenating them.
 *
 * If no arguments are provided, returns an empty string.
 *
 * For multiple arguments, constructs a string which is a concatenation of
 * strings created from each argument.  The following argument types are
 * supported:
 *
 *  * array    - The array must contain numbers from 0 to 0x1FFFFF, inclusive.
 *               Float numbers are converted to integers using floor operation.
 *               Any other types of array elements trigger an exception.  The
 *               array's elements are code points from which a new string is
 *               created.  The new string's length is equal to the length of
 *               the array.
 *  * buffer   - A buffer is treated as an UTF-8 sequence and it is decoded
 *               into a string.
 *  * integer  - An integer is converted to its string representation.
 *  * float    - An float is converted to its string representation.
 *  * function - If the function is an iterator (a primed generator),
 *               subsequent elements are obtained from it and added to the
 *               string.  The acceptable types of values returned from the
 *               iterator are: number from 0 to 0x1FFFFF inclusive, which
 *               is treated as a code point, array of numbers from 0 to
 *               0x1FFFFF, each treated as a code point, buffer treated
 *               as a UTF-8 sequence and string.  All elements returned
 *               by the iterator are concatenated in the order they are
 *               returned.
 *               If the function is not an iterator, an exception is thrown.
 *  * string   - No conversion is performed.
 *
 * The prototype of `string.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > string(10.1)
 *     "10.1"
 *     > string("kos", [108, 97, 110, 103], 32)
 *     "koslang32"
 */
static KOS_OBJ_ID string_constructor(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  this_obj,
                                     KOS_OBJ_ID  args_obj)
{
    int            error      = KOS_SUCCESS;
    int            pushed     = 0;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret        = KOS_BADPTR;
    KOS_OBJ_ID     obj        = KOS_BADPTR;
    KOS_OBJ_ID     codes      = KOS_BADPTR;
    KOS_OBJ_ID     substrings = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 5, &args_obj, &obj, &ret, &codes, &substrings));

    if (num_args == 0)
        ret = KOS_new_string(ctx, 0, 0);

    else {

        uint32_t i;

        for (i = 0; i < num_args; i++) {
            obj = KOS_array_read(ctx, args_obj, (int)i);
            TRY_OBJID(obj);

            if (IS_NUMERIC_OBJ(obj))
                obj = KOS_object_to_string(ctx, obj);

            else switch (READ_OBJ_TYPE(obj)) {

                case OBJ_STRING:
                    break;

                case OBJ_ARRAY:
                    obj = KOS_new_string_from_codes(ctx, obj);
                    break;

                case OBJ_BUFFER:
                    obj = KOS_new_string_from_buffer(ctx, obj, 0, KOS_get_buffer_size(obj));
                    break;


                case OBJ_FUNCTION: {
                    KOS_FUNCTION_STATE state;

                    if ( ! KOS_is_generator(obj, &state))
                        RAISE_EXCEPTION(str_err_cannot_convert_to_string);

                    if (IS_BAD_PTR(substrings)) {
                        substrings = KOS_new_array(ctx, 32);
                        TRY_OBJID(substrings);
                    }

                    TRY(KOS_array_resize(ctx, substrings, 0));

                    if (state != KOS_GEN_DONE) {

                        for (;;) {
                            KOS_TYPE   type;
                            KOS_OBJ_ID gen_args;

                            gen_args = KOS_new_array(ctx, 0);
                            TRY_OBJID(gen_args);

                            ret = KOS_call_generator(ctx, obj, KOS_VOID, gen_args);
                            if (IS_BAD_PTR(ret)) { /* end of iterator */
                                if (KOS_is_exception_pending(ctx))
                                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                                break;
                            }

                            type = GET_OBJ_TYPE(ret);

                            switch (type) {

                                case OBJ_SMALL_INTEGER:
                                    /* fall through */
                                case OBJ_INTEGER:
                                    /* fall through */
                                case OBJ_FLOAT: {
                                    int64_t value;
                                    TRY(KOS_get_integer(ctx, ret, &value));

                                    if (value < 0 || value > 0x1FFFFF)
                                        RAISE_EXCEPTION(str_err_invalid_char_code);

                                    if (IS_BAD_PTR(codes)) {
                                        codes = KOS_new_array(ctx, 128);
                                        TRY_OBJID(codes);
                                        TRY(KOS_array_resize(ctx, codes, 0));
                                    }

                                    TRY(KOS_array_push(ctx, codes, TO_SMALL_INT((int)value), 0));
                                    break;
                                }

                                case OBJ_ARRAY:
                                    /* fall through */
                                case OBJ_STRING:
                                    /* fall through */
                                case OBJ_BUFFER:

                                    if ( ! IS_BAD_PTR(codes) && KOS_get_array_size(codes)) {
                                        KOS_OBJ_ID str = KOS_new_string_from_codes(ctx, codes);
                                        TRY_OBJID(str);

                                        TRY(KOS_array_push(ctx, substrings, str, 0));

                                        TRY(KOS_array_resize(ctx, codes, 0));
                                    }

                                    if (type == OBJ_ARRAY) {
                                        ret = KOS_new_string_from_codes(ctx, ret);
                                        TRY_OBJID(ret);
                                    }
                                    else if (type == OBJ_BUFFER) {
                                        ret = KOS_new_string_from_buffer(ctx, ret, 0, KOS_get_buffer_size(ret));
                                        TRY_OBJID(ret);
                                    }

                                    TRY(KOS_array_push(ctx, substrings, ret, 0));
                                    break;

                                default:
                                    RAISE_EXCEPTION(str_err_cannot_convert_to_string);
                            }
                        }

                        if ( ! IS_BAD_PTR(codes) && KOS_get_array_size(codes)) {
                            KOS_OBJ_ID str = KOS_new_string_from_codes(ctx, codes);
                            TRY_OBJID(str);

                            TRY(KOS_array_push(ctx, substrings, str, 0));

                            TRY(KOS_array_resize(ctx, codes, 0));
                        }

                        obj = KOS_string_add(ctx, substrings);
                    }
                    break;
                }

                default:
                    RAISE_EXCEPTION(str_err_cannot_convert_to_string);
            }

            TRY_OBJID(obj);

            TRY(KOS_array_write(ctx, args_obj, (int)i, obj));
        }

        if (i == num_args)
            ret = KOS_string_add(ctx, args_obj);
    }

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base stringify()
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
 * `stringify()` is implicitly invoked during string interpolation, so
 * the result of `stringify()` is the same as the result of string
 * interpolation.
 *
 * String arguments are treated literally without any conversion.
 *
 * Integer, float, boolean and void arguments are converted to their
 * string representation, which is the same as in source code.
 *
 * Array and object arguments are converted to a human-readable representation
 * similar to their apperance in source code.  Strings inside arrays
 * and objects are double-quoted.
 *
 * Buffer arguments are converted to the form of `<xx xx ...>`, where `xx` are
 * two hexadecimal digits representing every byte in the buffer.
 *
 * Function arguments are converted to the form of `<function nnn @ xxx>`,
 * where `nnn` is the function name and `xxx` is the bytecode offset of the
 * function's entry point.
 *
 * Example:
 *
 *     > stringify(true, "true", 42, [10, "str"])
 *     "truetrue42[10, str]"
 */
static KOS_OBJ_ID stringify(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 1, &args_obj));
    }

    if (num_args == 0)
        ret = KOS_new_string(ctx, 0, 0);

    else {

        uint32_t i;

        for (i = 0; i < num_args; i++) {

            KOS_OBJ_ID obj = KOS_array_read(ctx, args_obj, (int)i);
            TRY_OBJID(obj);

            obj = KOS_object_to_string(ctx, obj);
            TRY_OBJID(obj);

            TRY(KOS_array_write(ctx, args_obj, (int)i, obj));
        }

        if (i == num_args)
            ret = KOS_string_add(ctx, args_obj);
    }

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base object()
 *
 *     object()
 *
 * Object type class.
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
static KOS_OBJ_ID object_constructor(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  this_obj,
                                     KOS_OBJ_ID  args_obj)
{
    return KOS_new_object(ctx);
}

static int make_room_in_array(KOS_CONTEXT ctx,
                              KOS_OBJ_ID *array_obj_ptr,
                              uint32_t    size)
{
    if (size) {

        KOS_OBJ_ID array_obj = *array_obj_ptr;

        if (IS_BAD_PTR(array_obj)) {

            array_obj = KOS_new_array(ctx, size);
            if (IS_BAD_PTR(array_obj))
                return KOS_ERROR_EXCEPTION;

            *array_obj_ptr = array_obj;
        }
        else
            return KOS_array_resize(ctx, array_obj, KOS_get_array_size(array_obj) + size);
    }

    return KOS_SUCCESS;
}

/* @item base array()
 *
 *     array(size = 0, value = void)
 *     array(arg, ...)
 *
 * Array type class.
 *
 * The first variant constructs an array of the specified size.  `size` defaults
 * to 0.  `value` is the value to fill the array with if `size` is greater than
 * 0.
 *
 * The second variant constructs an from one or more non-numeric objects.
 * Each of these input arguments is converted to an array and the resulting
 * arrays are concatenated, producing the final array, which is returned
 * by the class.  The following argument types are supported:
 *
 *  * array    - The array is simply appended to the new array without conversion.
 *               This can be used to make a copy of an array.
 *  * buffer   - Buffer's bytes are appended to the new array as integers.
 *  * function - If the function is an iterator (a primed generator), subsequent
 *               elements are obtained from it and appended to the array.
 *               For non-iterator functions an exception is thrown.
 *  * object   - Object's elements are extracted using shallow operation, i.e.
 *               without traversing its prototypes, then subsequent properties
 *               are appended to the array as two-element arrays containing
 *               the property name (key) and property value.
 *  * string   - All characters in the string are converted to code points (integers)
 *               and then each code point is subsequently appended to the new array.
 *
 * The prototype of `array.prototype` is `object.prototype`.
 *
 * Examples:
 *
 *     > array()
 *     []
 *     > array(3, "abc")
 *     ["abc", "abc", "abc"]
 *     > array("hello")
 *     [104, 101, 108, 108, 111]
 *     > array(range(5))
 *     [0, 1, 2, 3, 4]
 *     > array({ one: 1, two: 2, three: 3 })
 *     [["one", 1], ["two", 2], ["three", 3]]
 */
static KOS_OBJ_ID array_constructor(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    uint32_t       i_arg      = 0;
    uint32_t       cur_size   = 0;
    int            pushed     = 0;
    KOS_OBJ_ID     new_array  = KOS_BADPTR;
    KOS_OBJ_ID     arg        = KOS_BADPTR;
    KOS_OBJ_ID     gen_args   = KOS_BADPTR;
    KOS_OBJ_ID     walk       = KOS_BADPTR;
    KOS_OBJ_ID     walk_value = KOS_BADPTR;

    if (num_args == 0)
        return KOS_new_array(ctx, 0);

    TRY(KOS_push_locals(ctx, &pushed, 6, &args_obj, &new_array, &arg, &gen_args, &walk, &walk_value));

    arg = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg);

    if (num_args < 3 && IS_NUMERIC_OBJ(arg)) {

        int64_t size;

        TRY(KOS_get_integer(ctx, arg, &size));

        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_array_size);

        if (num_args == 2) {
            arg = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(arg);

            ++i_arg;
        }

        assert(IS_BAD_PTR(new_array));

        new_array = KOS_new_array(ctx, (uint32_t)size);
        TRY_OBJID(new_array);

        if (size && num_args == 2)
            TRY(KOS_array_fill(ctx, new_array, 0, size, arg));

        return new_array;
    }

    do {

        if (i_arg) {
            arg = KOS_array_read(ctx, args_obj, (int)i_arg);
            TRY_OBJID(arg);
        }

        switch (GET_OBJ_TYPE(arg)) {

            case OBJ_ARRAY: {
                const uint32_t src_size = KOS_get_array_size(arg);

                if (src_size) {

                    TRY(make_room_in_array(ctx, &new_array, src_size));

                    TRY(KOS_array_insert(ctx, new_array, cur_size, cur_size + src_size,
                                         arg, 0, src_size));

                    cur_size += src_size;
                }
                break;
            }

            case OBJ_STRING: {
                const uint32_t str_size = KOS_get_string_length(arg);

                if (str_size) {

                    uint32_t i;

                    TRY(make_room_in_array(ctx, &new_array, str_size));

                    for (i = 0; i < str_size; i++) {

                        const unsigned ch_code = KOS_string_get_char_code(ctx, arg, i);

                        KOS_OBJ_ID value = KOS_new_int(ctx, (int64_t)ch_code);
                        TRY_OBJID(value);

                        TRY(KOS_array_write(ctx, new_array, cur_size + i, value));
                    }

                    cur_size += str_size;
                }
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t buf_size = KOS_get_buffer_size(arg);

                if (buf_size) {

                    uint32_t i;

                    TRY(make_room_in_array(ctx, &new_array, buf_size));

                    for (i = 0; i < buf_size; i++) {

                        const uint8_t value = KOS_buffer_data_volatile(arg)[i];

                        TRY(KOS_array_write(ctx, new_array,
                                            cur_size + i, TO_SMALL_INT((int)value)));
                    }

                    cur_size += buf_size;
                }
                break;
            }

            case OBJ_OBJECT: {
                walk = KOS_new_object_walk(ctx, arg, KOS_SHALLOW);
                TRY_OBJID(walk);

                while ( ! KOS_object_walk(ctx, walk)) {

                    walk_value = KOS_get_walk_value(walk);

                    assert( ! IS_BAD_PTR(KOS_get_walk_key(walk)));
                    assert( ! IS_BAD_PTR(walk_value));

                    if (GET_OBJ_TYPE(walk_value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args = KOS_new_array(ctx, 0);
                        TRY_OBJID(args);

                        walk_value = KOS_call_function(ctx,
                                                       OBJPTR(DYNAMIC_PROP, walk_value)->getter,
                                                       OBJPTR(OBJECT_WALK, walk)->obj,
                                                       args);
                        if (IS_BAD_PTR(walk_value)) {
                            assert(KOS_is_exception_pending(ctx));
                            KOS_clear_exception(ctx);

                            walk_value = OBJPTR(DYNAMIC_PROP, KOS_get_walk_value(walk))->getter;
                        }
                    }

                    gen_args = KOS_new_array(ctx, 2);
                    TRY_OBJID(gen_args);

                    TRY(KOS_array_write(ctx, gen_args, 0, KOS_get_walk_key(walk)));
                    TRY(KOS_array_write(ctx, gen_args, 1, walk_value));

                    if (IS_BAD_PTR(new_array)) {
                        new_array = KOS_new_array(ctx, 1);
                        TRY_OBJID(new_array);

                        TRY(KOS_array_write(ctx, new_array, 0, gen_args));
                    }
                    else
                        TRY(KOS_array_push(ctx, new_array, gen_args, 0));

                    ++cur_size;
                }
                break;
            }

            case OBJ_FUNCTION: {
                KOS_FUNCTION_STATE state;

                if ( ! KOS_is_generator(arg, &state)) {
                    KOS_raise_exception_cstring(ctx, str_err_cannot_convert_to_array);
                    return KOS_BADPTR;
                }

                if (state != KOS_GEN_DONE) {

                    gen_args = KOS_new_array(ctx, 0);
                    TRY_OBJID(gen_args);

                    for (;;) {

                        KOS_OBJ_ID ret = KOS_call_generator(ctx, arg, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret)) { /* end of iterator */
                            if (KOS_is_exception_pending(ctx))
                                RAISE_ERROR(KOS_ERROR_EXCEPTION);
                            break;
                        }

                        if (IS_BAD_PTR(new_array)) {
                            kos_track_refs(ctx, 1, &ret);
                            new_array = KOS_new_array(ctx, 1);
                            kos_untrack_refs(ctx, 1);
                            TRY_OBJID(new_array);

                            TRY(KOS_array_write(ctx, new_array, 0, ret));
                        }
                        else
                            TRY(KOS_array_push(ctx, new_array, ret, 0));

                        ++cur_size;
                    }
                }
                break;
            }

            default:
                KOS_raise_exception_cstring(ctx, str_err_cannot_convert_to_array);
                return KOS_BADPTR;
        }

        ++i_arg;

    } while (i_arg < num_args);

    if ( ! cur_size) {
        assert(IS_BAD_PTR(new_array));
        new_array = KOS_new_array(ctx, 0);
    }

cleanup:
    return error ? KOS_BADPTR : new_array;
}

/* @item base buffer()
 *
 *     buffer(size = 0, value = 0)
 *     buffer(arg, ...)
 *
 * Buffer type class.
 *
 * The first variant constructs a buffer of the specified size.  `size` defaults
 * to 0.  `value` is the value to fill the buffer with is `size` is greater than
 * 0.  `value` must be a number from 0 to 255 (floor operation is applied to
 * floats), it defaults to 0 if it's not specified.
 *
 * The second variant constructs a buffer from one or more non-numeric objects.
 * Each of these input arguments is converted to a buffer and the resulting
 * buffers are concatenated, producing the final buffer, which is returned
 * by the class.  The following argument types are supported:
 *
 *  * array    - The array must contain numbers from 0 to 255 (floor operation
 *               is applied to floats).  Any other array elements trigger an
 *               exception.  The array is converted to a buffer containing
 *               bytes with values from the array.
 *  * buffer   - A buffer is simply concatenated with other input arguments without
 *               any transformation.
 *               This can be used to make a copy of a buffer.
 *  * function - If the function is an iterator (a primed generator), subsequent
 *               elements are obtained from it and added to the buffer.  The
 *               values returned by the iterator must be numbers from 0 to 255
 *               (floor operation is applied to floats), any other values trigger
 *               an exception.
 *               For non-iterator functions an exception is thrown.
 *  * string   - The string is converted to an UTF-8 representation stored
 *               into a buffer.
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
static KOS_OBJ_ID buffer_constructor(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  this_obj,
                                     KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    int            pushed   = 0;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i_arg;
    KOS_OBJ_ID     buffer   = KOS_BADPTR;
    KOS_OBJ_ID     arg      = KOS_BADPTR;
    KOS_OBJ_ID     gen_args = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 4, &args_obj, &buffer, &arg, &gen_args));

    buffer = KOS_new_buffer(ctx, 0);
    TRY_OBJID(buffer);

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        const uint32_t cur_size = KOS_get_buffer_size(buffer);

        arg = KOS_array_read(ctx, args_obj, (int)i_arg);
        TRY_OBJID(arg);

        if (i_arg == 0 && num_args < 3 && IS_NUMERIC_OBJ(arg)) {
            int64_t size;
            int64_t value = 0;

            TRY(KOS_get_integer(ctx, arg, &size));

            if (size < 0 || size > INT_MAX)
                RAISE_EXCEPTION(str_err_invalid_buffer_size);

            if (num_args == 2) {
                arg = KOS_array_read(ctx, args_obj, 1);
                TRY_OBJID(arg);

                TRY(KOS_get_integer(ctx, arg, &value));

                if (value < 0 || value > 255)
                    RAISE_EXCEPTION(str_err_cannot_convert_to_buffer);

                ++i_arg;
            }

            if (size) {
                TRY(KOS_buffer_resize(ctx, buffer, (uint32_t)size));

                memset(KOS_buffer_data_volatile(buffer), (int)value, (size_t)size);
            }

            continue;
        }

        switch (GET_OBJ_TYPE(arg)) {

            case OBJ_ARRAY: {
                const uint32_t size = KOS_get_array_size(arg);
                uint32_t       i;
                uint8_t       *data = 0;

                if ( ! size)
                    break;

                TRY(KOS_buffer_resize(ctx, buffer, cur_size + size));

                data = KOS_buffer_data_volatile(buffer) + cur_size;

                for (i = 0; i < size; i++) {
                    int64_t value;

                    KOS_OBJ_ID elem = KOS_array_read(ctx, arg, i);
                    TRY_OBJID(elem);

                    TRY(KOS_get_integer(ctx, elem, &value));

                    if (value < 0 || value > 255)
                        RAISE_EXCEPTION(str_err_invalid_byte_value);

                    *(data++) = (uint8_t)(uint64_t)value;
                }
                break;
            }

            case OBJ_STRING: {
                const uint32_t size = KOS_string_to_utf8(arg, 0, 0);

                if (size == ~0U) {
                    KOS_raise_exception_cstring(ctx, str_err_invalid_string);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                TRY(KOS_buffer_resize(ctx, buffer, cur_size + size));

                KOS_string_to_utf8(arg, KOS_buffer_data_volatile(buffer) + cur_size, size);
                break;
            }

            case OBJ_BUFFER: {
                const uint32_t size = KOS_get_buffer_size(arg);

                TRY(KOS_buffer_resize(ctx, buffer, cur_size + size));

                memcpy(KOS_buffer_data_volatile(buffer) + cur_size,
                       KOS_buffer_data_volatile(arg),
                       size);
                break;
            }

            case OBJ_FUNCTION: {
                KOS_FUNCTION_STATE state;

                if ( ! KOS_is_generator(arg, &state)) {
                    KOS_raise_exception_cstring(ctx, str_err_cannot_convert_to_buffer);
                    return KOS_BADPTR;
                }

                if (state != KOS_GEN_DONE) {
                    uint32_t size     = cur_size;
                    uint32_t capacity = cur_size;

                    gen_args = KOS_new_array(ctx, 0);
                    TRY_OBJID(gen_args);

                    if (cur_size < 64) {
                        TRY(KOS_buffer_resize(ctx, buffer, 64));
                        capacity = 64;
                    }

                    for (;;) {
                        int64_t  value;
                        uint8_t *data;

                        KOS_OBJ_ID ret = KOS_call_generator(ctx, arg, KOS_VOID, gen_args);
                        if (IS_BAD_PTR(ret)) { /* end of iterator */
                            if (KOS_is_exception_pending(ctx))
                                RAISE_ERROR(KOS_ERROR_EXCEPTION);
                            break;
                        }

                        TRY(KOS_get_integer(ctx, ret, &value));

                        if (value < 0 || value > 255)
                            RAISE_EXCEPTION(str_err_invalid_byte_value);

                        if (size >= capacity) {
                            capacity *= 2;
                            TRY(KOS_buffer_resize(ctx, buffer, capacity));
                        }

                        data = KOS_buffer_data_volatile(buffer) + size;

                        *data = (uint8_t)(uint64_t)value;
                        ++size;
                    }

                    TRY(KOS_buffer_resize(ctx, buffer, size));
                }
                break;
            }

            default:
                KOS_raise_exception_cstring(ctx, str_err_cannot_convert_to_buffer);
                return KOS_BADPTR;
        }
    }

cleanup:
    return error ? KOS_BADPTR : buffer;
}

/* @item base function()
 *
 *     function(func)
 *
 * Function type class.
 *
 * The argument is a function object.
 *
 *  * For regular functions, returns the same function object which was
 *    passed.
 *  * For classes (constuctor functions), returns a copy of
 *    the function object without copying any properties,
 *    not even the prototype.
 *  * For generator functions (not instantiated), returns the same generator
 *    function which was passed.
 *  * For instantiated generator functions (iterators), returns a copy of
 *    the generator function object, uninstantiated.
 *
 * Throws an exception if the argument is not a function.
 *
 * The prototype of `function.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID function_constructor(KOS_CONTEXT ctx,
                                       KOS_OBJ_ID  this_obj,
                                       KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (KOS_get_array_size(args_obj) != 1)
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    else {

        ret = KOS_array_read(ctx, args_obj, 0);
        if ( ! IS_BAD_PTR(ret)) {
            const KOS_TYPE type = GET_OBJ_TYPE(ret);

            if (type != OBJ_FUNCTION && type != OBJ_CLASS) {
                KOS_raise_exception_cstring(ctx, str_err_not_function);
                ret = KOS_BADPTR;
            }
            else {
                const KOS_FUNCTION_STATE state = (KOS_FUNCTION_STATE)
                    OBJPTR(FUNCTION, ret)->state;

                switch (state) {

                    case KOS_FUN:
                        /* fall through */
                    case KOS_GEN_INIT:
                        break;

                    default:
                        ret = kos_copy_function(ctx, ret);

                        if (state > KOS_GEN_INIT)
                            OBJPTR(FUNCTION, ret)->state = KOS_GEN_INIT;
                }
            }
        }
    }

    return ret;
}

/* @item base class()
 *
 *     class(func)
 *
 * Class type class.
 *
 * Because `class` is a keyword, this class can only be referenced
 * indirectly via the base module, it cannot be referenced if it is imported
 * directly into the current module.
 *
 * Returns a copy of the `func` class object without copying any properties,
 * not even the prototype.
 *
 * Throws an exception if the `func` argument is not a class.
 *
 * The prototype of `class.prototype` is `function.prototype`.
 */
static KOS_OBJ_ID class_constructor(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (KOS_get_array_size(args_obj) != 1)
        KOS_raise_exception_cstring(ctx, str_err_not_class);

    else {

        ret = KOS_array_read(ctx, args_obj, 0);
        if ( ! IS_BAD_PTR(ret)) {
            if (GET_OBJ_TYPE(ret) == OBJ_CLASS)
                ret = kos_copy_function(ctx, ret);
            else {
                KOS_raise_exception_cstring(ctx, str_err_not_class);
                ret = KOS_BADPTR;
            }
        }
    }

    return ret;
}

/* @item base generator()
 *
 *     generator(func)
 *
 * Generator function class.
 *
 * This class can be used with the `instanceof` operator to detect generator
 * functions.
 *
 * The `func` argument must be a generator function.
 *
 *  * For generator functions (not instantiated), returns the same generator
 *    function which was passed.
 *  * For instantiated generator functions (iterators), returns a copy of
 *    the generator function object, uninstantiated.
 *
 * Throws an exception if the `func` argument is not a generator.
 *
 * The prototype of `generator.prototype` is `function.prototype`.
 */
static KOS_OBJ_ID generator_constructor(KOS_CONTEXT ctx,
                                        KOS_OBJ_ID  this_obj,
                                        KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (KOS_get_array_size(args_obj) != 1)
        KOS_raise_exception_cstring(ctx, str_err_not_generator);

    else {

        ret = KOS_array_read(ctx, args_obj, 0);
        if ( ! IS_BAD_PTR(ret)) {
            const KOS_TYPE type = GET_OBJ_TYPE(ret);

            if (type != OBJ_FUNCTION) {
                KOS_raise_exception_cstring(ctx, str_err_not_generator);
                ret = KOS_BADPTR;
            }
            else {
                const KOS_FUNCTION_STATE state = (KOS_FUNCTION_STATE)
                    OBJPTR(FUNCTION, ret)->state;

                switch (state) {

                    case KOS_FUN:
                        /* fall through */
                    case KOS_CTOR:
                        KOS_raise_exception_cstring(ctx, str_err_not_generator);
                        ret = KOS_BADPTR;
                        break;

                    case KOS_GEN_INIT:
                        break;

                    default:
                        ret = kos_copy_function(ctx, ret);

                        if (state > KOS_GEN_INIT)
                            OBJPTR(FUNCTION, ret)->state = KOS_GEN_INIT;
                }
            }
        }
    }

    return ret;
}

/* @item base exception()
 *
 *     exception([value])
 *
 * Exception object class.
 *
 * All caught exception objects have `exception.prototype` as their prototype.
 * This class gives access to that prototype.
 *
 * Calling this class throws an exception, it does not return
 * an exception object.  The thrown exception's `value` property can be set
 * to the optional `value` argument.  In other words, calling this class
 * is equivalent to throwing `value`.
 *
 * If `value` is not specified, `void` is thrown.
 *
 * The prototype of `exception.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID exception_constructor(KOS_CONTEXT ctx,
                                        KOS_OBJ_ID  this_obj,
                                        KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     exception = KOS_VOID;
    const uint32_t num_args  = KOS_get_array_size(args_obj);

    if (num_args > 0)
        exception = KOS_array_read(ctx, args_obj, 0);

    KOS_raise_exception(ctx, exception);
    return KOS_BADPTR;
}

/* @item base generator_end()
 *
 *     generator_end()
 *
 * Generator end object class.
 *
 * A generator end object is typically thrown when an iterator function is
 * called but has no more values to yield.  In other words, a thrown generator
 * end object indicates end of a generator.  The generator end object can
 * be caught and it becomes the `value` of the exception object caught.
 *
 * Calling this class throws an exception.
 *
 * The prototype of `generator_end.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID generator_end_constructor(KOS_CONTEXT ctx,
                                            KOS_OBJ_ID  this_obj,
                                            KOS_OBJ_ID  args_obj)
{
    KOS_raise_generator_end(ctx);
    return KOS_BADPTR;
}

/* @item base thread()
 *
 *     thread()
 *
 * Thread object class.
 *
 * Thread objects are created by calling `function.prototype.async()`.
 *
 * The purpose of this class is to be used with the `instanceof`
 * operator to detect thread objects.
 *
 * Calling this class directly throws an exception.
 *
 * The prototype of `thread.prototype` is `object.prototype`.
 */
static KOS_OBJ_ID thread_constructor(KOS_CONTEXT ctx,
                                     KOS_OBJ_ID  this_obj,
                                     KOS_OBJ_ID  args_obj)
{
    KOS_raise_exception_cstring(ctx, str_err_use_async);
    return KOS_BADPTR;
}

/* @item base function.prototype.apply()
 *
 *     function.prototype.apply(this_object, args_array)
 *
 * Invokes a function with the specified this object and arguments.
 *
 * Returns the value returned by the function.
 *
 * The `this_object` argument is the object which is bound to the function as
 * `this` for this invocation.  It can be any object or `void`.
 *
 * The `args_array` argument is an array (can be empty) containing arguments for
 * the function.
 *
 * Example:
 *
 *     > fun f(a) { return this + a }
 *     > f.apply(1, [2])
 *     3
 */
static KOS_OBJ_ID apply(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int        error    = KOS_SUCCESS;
    int        pushed   = 0;
    KOS_OBJ_ID ret      = KOS_BADPTR;
    KOS_OBJ_ID arg_args = KOS_BADPTR;
    KOS_OBJ_ID arg_this;

    arg_this = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg_this);

    arg_args = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(arg_args);

    TRY(KOS_push_locals(ctx, &pushed, 3, &this_obj, &arg_this, &arg_args));
    arg_args = KOS_array_slice(ctx, arg_args, 0, MAX_INT64);
    KOS_pop_locals(ctx, pushed);
    TRY_OBJID(arg_args);

    ret = KOS_apply_function(ctx, this_obj, arg_this, arg_args);

cleanup:
    return error ? KOS_BADPTR : ret;
}

static void thread_finalize(KOS_CONTEXT ctx,
                            void       *priv)
{
    if (priv)
        kos_thread_disown((KOS_THREAD *)priv);
}

/* @item base function.prototype.async()
 *
 *     function.prototype.async(this_object, args_array)
 *
 * Invokes a function asynchronously on a new thread.
 *
 * Returns the created thread object.
 *
 * The `this_object` argument is the object which is bound to the function as
 * `this` for this invocation.  It can be any object or `void`.
 *
 * The `args_array` argument is an array (can be empty) containing arguments for
 * the function.
 *
 * Example:
 *
 *     > fun f(a, b) { return a + b }
 *     > const t = f.async(void, [1, 2])
 *     > t.wait()
 *     3
 */
static KOS_OBJ_ID async(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID  thread_obj = KOS_BADPTR;
    KOS_OBJ_ID  arg_this;
    KOS_OBJ_ID  arg_args;
    KOS_THREAD *thread;
    int         error      = KOS_SUCCESS;
    int         pushed     = 0;

    if (GET_OBJ_TYPE(this_obj) != OBJ_FUNCTION) {
        KOS_raise_exception_cstring(ctx, str_err_not_function);
        return KOS_BADPTR;
    }

    TRY(KOS_push_locals(ctx, &pushed, 3, &this_obj, &args_obj, &thread_obj));

    thread_obj = KOS_new_object_with_prototype(ctx,
            ctx->inst->prototypes.thread_proto);
    TRY_OBJID(thread_obj);

    KOS_object_set_private_ptr(thread_obj, (void *)0);

    arg_this = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg_this);

    arg_args = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(arg_args);
    if (GET_OBJ_TYPE(arg_args) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_args_not_array);

    thread = kos_thread_create(ctx, this_obj, arg_this, arg_args);
    if ( ! thread) {
        error = KOS_ERROR_EXCEPTION;
        goto cleanup;
    }

    kos_thread_add_ref(thread);

    KOS_object_set_private_ptr(thread_obj, thread);

    OBJPTR(OBJECT, thread_obj)->finalize = thread_finalize;

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error ? KOS_BADPTR : thread_obj;
}

/* @item base thread.prototype.wait()
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
static KOS_OBJ_ID wait(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    KOS_THREAD         *thread;
    KOS_INSTANCE *const inst  = ctx->inst;
    KOS_OBJ_ID          retval;

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT) {
        KOS_raise_exception_cstring(ctx, str_err_not_thread);
        return KOS_BADPTR;
    }

    if ( ! KOS_has_prototype(ctx, this_obj, inst->prototypes.thread_proto)) {
        KOS_raise_exception_cstring(ctx, str_err_not_thread);
        return KOS_BADPTR;
    }

    thread = (KOS_THREAD *)KOS_object_get_private_ptr(this_obj);

    if (thread && kos_is_current_thread(thread)) {
        KOS_raise_exception_cstring(ctx, str_err_join_self);
        return KOS_BADPTR;
    }

    thread = (KOS_THREAD *)KOS_object_swap_private_ptr(this_obj, (void *)0);

    if ( ! thread) {
        KOS_raise_exception_cstring(ctx, str_err_already_joined);
        return KOS_BADPTR;
    }

    retval = kos_thread_join(ctx, thread);

    kos_thread_disown(thread);

    return retval;
}

/* @item base string.prototype.slice()
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

/* @item base array.prototype.slice()
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

/* @item base buffer.prototype.slice()
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
static KOS_OBJ_ID slice(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int        error;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    KOS_OBJ_ID a_obj;
    KOS_OBJ_ID b_obj;
    int64_t    idx_a = 0;
    int64_t    idx_b = 0;

    a_obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(a_obj);

    b_obj = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(b_obj);

    if (IS_NUMERIC_OBJ(a_obj))
        TRY(KOS_get_integer(ctx, a_obj, &idx_a));
    else if (READ_OBJ_TYPE(a_obj) == OBJ_VOID)
        idx_a = 0;
    else {
        KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }

    if (IS_NUMERIC_OBJ(b_obj))
        TRY(KOS_get_integer(ctx, b_obj, &idx_b));
    else if (READ_OBJ_TYPE(b_obj) == OBJ_VOID)
        idx_b = MAX_INT64;
    else {
        KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }

    if (GET_OBJ_TYPE(this_obj) == OBJ_STRING)
        ret = KOS_string_slice(ctx, this_obj, idx_a, idx_b);
    else if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_buffer_slice(ctx, this_obj, idx_a, idx_b);
    else
        ret = KOS_array_slice(ctx, this_obj, idx_a, idx_b);

cleanup:
    return ret;
}

static KOS_OBJ_ID expand_for_sort(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  iterable,
                                  KOS_OBJ_ID  key_func)
{
    int            error    = KOS_SUCCESS;
    int            pushed   = 0;
    uint32_t       i        = 0;
    uint32_t       i_dest   = 0;
    uint32_t       size;
    const uint32_t step     = (key_func == KOS_VOID) ? 2 : 3;
    KOS_OBJ_ID     ret      = KOS_BADPTR;
    KOS_OBJ_ID     key_args = KOS_BADPTR;
    KOS_OBJ_ID     src      = KOS_BADPTR;
    KOS_OBJ_ID     dest     = KOS_BADPTR;
    KOS_OBJ_ID     val      = KOS_BADPTR;

    assert(GET_OBJ_TYPE(iterable) == OBJ_ARRAY);

    TRY(KOS_push_locals(ctx, &pushed, 7,
                        &iterable, &key_func, &ret, &key_args, &src, &dest, &val));

    size = KOS_get_array_size(iterable);
    src  = kos_get_array_storage(iterable);

    ret  = KOS_new_array(ctx, size * step);
    TRY_OBJID(ret);

    dest = kos_get_array_storage(ret);

    if (key_func != KOS_VOID) {
        key_args = KOS_new_array(ctx, 1);
        TRY_OBJID(key_args);
    }

    while (i < size) {

        val = KOS_atomic_read_relaxed_obj(OBJPTR(ARRAY_STORAGE, src)->buf[i]);

        if (key_func == KOS_VOID) {
            KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, dest)->buf[i_dest],     val);
            KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, dest)->buf[i_dest + 1], TO_SMALL_INT(i));
        }
        else {
            KOS_OBJ_ID key;

            TRY(KOS_array_write(ctx, key_args, 0, val));
            key = KOS_call_function(ctx, key_func, KOS_VOID, key_args);
            TRY_OBJID(key);

            KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, dest)->buf[i_dest],     key);
            KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, dest)->buf[i_dest + 1], TO_SMALL_INT(i));
            KOS_atomic_write_relaxed_ptr(OBJPTR(ARRAY_STORAGE, dest)->buf[i_dest + 2], val);
        }

        ++i;
        i_dest += step;
    }

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error ? KOS_BADPTR : ret;
}

static int is_less_for_sort(KOS_OBJ_ID         left_key,
                            KOS_OBJ_ID         left_idx,
                            KOS_COMPARE_RESULT lt,
                            KOS_COMPARE_RESULT gt,
                            KOS_OBJ_ID         right_key,
                            KOS_OBJ_ID         right_idx)
{
    const KOS_COMPARE_RESULT cmp = KOS_compare(left_key, right_key);

    if (cmp == lt)
        return 1;

    if (cmp == gt)
        return 0;

    if (lt == KOS_LESS_THAN)
        return (intptr_t)left_idx < (intptr_t)right_idx;
    else
        return (intptr_t)left_idx > (intptr_t)right_idx;
}

static void sort_range(KOS_ATOMIC(KOS_OBJ_ID) *begin,
                       KOS_ATOMIC(KOS_OBJ_ID) *end,
                       int                     step,
                       int                     reverse)
{
    const KOS_OBJ_ID pivot_key = KOS_atomic_read_relaxed_obj(*(end - step));
    const KOS_OBJ_ID pivot_idx = KOS_atomic_read_relaxed_obj(*(end - step + 1));

    KOS_ATOMIC(KOS_OBJ_ID)  *mid = begin - step;
    KOS_ATOMIC(KOS_OBJ_ID)  *p   = begin;
    const KOS_COMPARE_RESULT lt  = reverse ? KOS_GREATER_THAN : KOS_LESS_THAN;
    const KOS_COMPARE_RESULT gt  = reverse ? KOS_LESS_THAN : KOS_GREATER_THAN;

    end -= step;

    while (p < end) {
        const KOS_OBJ_ID key = KOS_atomic_read_relaxed_obj(p[0]);
        const KOS_OBJ_ID idx = KOS_atomic_read_relaxed_obj(p[1]);

        if (is_less_for_sort(key, idx, lt, gt, pivot_key, pivot_idx)) {

            mid += step;

            KOS_atomic_write_relaxed_ptr(p[0], KOS_atomic_read_relaxed_obj(mid[0]));
            KOS_atomic_write_relaxed_ptr(p[1], KOS_atomic_read_relaxed_obj(mid[1]));

            KOS_atomic_write_relaxed_ptr(mid[0], key);
            KOS_atomic_write_relaxed_ptr(mid[1], idx);

            if (step == 3) {
                const KOS_OBJ_ID val = KOS_atomic_read_relaxed_obj(p[2]);

                KOS_atomic_write_relaxed_ptr(p[2], KOS_atomic_read_relaxed_obj(mid[2]));
                KOS_atomic_write_relaxed_ptr(mid[2], val);
            }
        }

        p += step;
    }

    mid += step;

    {
        const KOS_OBJ_ID key = KOS_atomic_read_relaxed_obj(mid[0]);
        const KOS_OBJ_ID idx = KOS_atomic_read_relaxed_obj(mid[1]);

        if (is_less_for_sort(pivot_key, pivot_idx, lt, gt, key, idx)) {
            KOS_atomic_write_relaxed_ptr(end[0], key);
            KOS_atomic_write_relaxed_ptr(end[1], idx);

            KOS_atomic_write_relaxed_ptr(mid[0], pivot_key);
            KOS_atomic_write_relaxed_ptr(mid[1], pivot_idx);

            if (step == 3) {
                const KOS_OBJ_ID pivot_val = KOS_atomic_read_relaxed_obj(end[2]);

                KOS_atomic_write_relaxed_ptr(end[2], KOS_atomic_read_relaxed_obj(mid[2]));
                KOS_atomic_write_relaxed_ptr(mid[2], pivot_val);
            }
        }
    }

    if (begin + step < mid)
        sort_range(begin, mid, step, reverse);
    if (mid + step < end)
        sort_range(mid + step, end + step, step, reverse);
}

static void copy_sort_results(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  ret,
                              KOS_OBJ_ID  sorted,
                              uint32_t    step)
{
    KOS_ATOMIC(KOS_OBJ_ID) *src;
    KOS_ATOMIC(KOS_OBJ_ID) *src_end;
    KOS_ATOMIC(KOS_OBJ_ID) *dest;

    assert(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
    assert(GET_OBJ_TYPE(sorted) == OBJ_ARRAY);
    assert(step == 2 || step == 3);

    src     = kos_get_array_buffer(OBJPTR(ARRAY, sorted));
    src_end = src + KOS_get_array_size(sorted);
    dest    = kos_get_array_buffer(OBJPTR(ARRAY, ret));

    assert(KOS_get_array_size(ret) * step == KOS_get_array_size(sorted));

    if (step == 3)
        src += 2;

    while (src < src_end) {

        const KOS_OBJ_ID val = KOS_atomic_read_relaxed_obj(*src);
        KOS_atomic_write_relaxed_ptr(*dest, val);

        src += step;
        ++dest;
    }
}

/* @item base array.prototype.sort()
 *
 *     array.prototype.sort(key=void, reverse=false)
 *     array.prototype.sort(reverse)
 *
 * Sorts array in-place.
 *
 * Returns the array being sorted (`this`).
 *
 * Uses a stable sorting algorithm, which preserves order of elements for
 * which sorting keys compare as equal.
 *
 * `key` is a single-argument function which produces a sorting key for each
 * element of the array.  The array elements are then sorted by the keys using
 * the '<' operator.  By default `key` is `void` and the elements themselves
 * are used as sorting keys.
 *
 * `reverse` defaults to `false`.  If `reverse` is specified as `true`,
 * the array elements are sorted in reverse order, i.e. in a descending key
 * order.
 *
 * Example:
 *
 *     > [8, 5, 6, 0, 10, 2].sort()
 *     [0, 2, 5, 6, 8, 10]
 */
static KOS_OBJ_ID sort(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int                     error       = KOS_SUCCESS;
    int                     pushed      = 0;
    const uint32_t          num_args    = KOS_get_array_size(args_obj);
    KOS_OBJ_ID              key         = KOS_VOID;
    KOS_OBJ_ID              reverse_id  = KOS_FALSE;
    KOS_ATOMIC(KOS_OBJ_ID) *src;

    if (GET_OBJ_TYPE(this_obj) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    if (num_args > 0) {

        KOS_TYPE type;

        key = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(key);

        type = GET_OBJ_TYPE(key);

        if (type == OBJ_BOOLEAN) {
            reverse_id = key;
            key        = KOS_VOID;
        }
        else {
            if (type != OBJ_VOID && type != OBJ_FUNCTION && type != OBJ_CLASS)
                RAISE_EXCEPTION(str_err_invalid_key_type);

            if (num_args > 1) {
                reverse_id = KOS_array_read(ctx, args_obj, 1);
                TRY_OBJID(reverse_id);
                if (reverse_id != KOS_TRUE && reverse_id != KOS_FALSE)
                    RAISE_EXCEPTION(str_err_invalid_reverse_type);
            }
        }
    }

    if (KOS_get_array_size(this_obj) > 1) {

        KOS_OBJ_ID aux = KOS_BADPTR;

        TRY(KOS_push_locals(ctx, &pushed, 3, &this_obj, &key, &aux));

        aux = expand_for_sort(ctx, this_obj, key);
        TRY_OBJID(aux);

        src = kos_get_array_buffer(OBJPTR(ARRAY, aux));

        sort_range(src,
                   src + KOS_get_array_size(aux),
                   (key == KOS_VOID) ? 2 : 3,
                   (int)KOS_get_bool(reverse_id));

        copy_sort_results(ctx, this_obj, aux, (key == KOS_VOID) ? 2 : 3);
    }

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

/* @item base array.prototype.size
 *
 *     array.prototype.size
 *
 * Read-only size of the array (integer).
 *
 * Example:
 *
 *     > [1, 10, 100].size
 *     3
 */
static KOS_OBJ_ID get_array_size(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_ARRAY)
        ret = KOS_new_int(ctx, (int64_t)KOS_get_array_size(this_obj));
    else {
        KOS_raise_exception_cstring(ctx, str_err_not_array);
        ret = KOS_BADPTR;
    }

    return ret;
}

/* @item base buffer.prototype.size
 *
 *     buffer.prototype.size
 *
 * Read-only size of the buffer (integer).
 *
 * Example:
 *
 *     > buffer([1, 10, 100]).size
 *     3
 */
static KOS_OBJ_ID get_buffer_size(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER)
        ret = KOS_new_int(ctx, (int64_t)KOS_get_buffer_size(this_obj));
    else {
        KOS_raise_exception_cstring(ctx, str_err_not_buffer);
        ret = KOS_BADPTR;
    }

    return ret;
}

/* @item base array.prototype.resize()
 *
 *     array.prototype.resize(size, value = void)
 *
 * Resizes an array.
 *
 * Returns the array being resized (`this`).
 *
 * `size` is the new size of the array.
 *
 * If `size` is greater than the current array size, `value` elements are
 * appended to expand the array.
 *
 * Example:
 *
 *     > const a = []
 *     > a.resize(5)
 *     [void, void, void, void, void]
 */

/* @item base buffer.prototype.resize()
 *
 *     buffer.prototype.resize(size, value = 0)
 *
 * Resizes a buffer.
 *
 * Returns the buffer being resized (`this`).
 *
 * `size` is the new size of the buffer.
 *
 * If `size` is greater than the current buffer size, `value` elements are
 * appended to expand the buffer.
 *
 * Example:
 *
 *     > const a = buffer()
 *     > b.resize(5)
 *     <00 00 00 00 00>
 */
static KOS_OBJ_ID resize(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  this_obj,
                         KOS_OBJ_ID  args_obj)
{
    int        error     = KOS_SUCCESS;
    int        pushed    = 0;
    KOS_OBJ_ID value_obj = KOS_VOID;
    KOS_OBJ_ID size_obj  = KOS_VOID;
    int64_t    size;

    TRY(KOS_push_locals(ctx, &pushed, 3, &this_obj, &args_obj, &value_obj));

    size_obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(size_obj);

    TRY(KOS_get_integer(ctx, size_obj, &size));

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        const uint32_t old_size = KOS_get_buffer_size(this_obj);
        int64_t        value    = 0;

        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception_cstring(ctx, str_err_invalid_buffer_size);
            return KOS_BADPTR;
        }

        if (KOS_get_array_size(args_obj) > 1) {
            value_obj = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(value_obj);

            if (!IS_NUMERIC_OBJ(value_obj))
                RAISE_EXCEPTION(str_err_cannot_convert_to_buffer);

            TRY(KOS_get_integer(ctx, value_obj, &value));

            if (value < 0 || value > 255)
                RAISE_EXCEPTION(str_err_cannot_convert_to_buffer);
        }
        else
            value_obj = TO_SMALL_INT(0);

        TRY(KOS_buffer_resize(ctx, this_obj, (uint32_t)size));

        if (size > old_size)
            memset(KOS_buffer_data_volatile(this_obj) + old_size,
                   (int)value,
                   (uint32_t)(size - old_size));
    }
    else {
        uint32_t old_size;

        if (GET_OBJ_TYPE(this_obj) != OBJ_ARRAY)
            RAISE_EXCEPTION(str_err_not_array);

        if (size < 0 || size > INT_MAX) {
            KOS_raise_exception_cstring(ctx, str_err_invalid_array_size);
            return KOS_BADPTR;
        }

        if (KOS_get_array_size(args_obj) > 1) {
            value_obj = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(value_obj);
        }

        old_size = KOS_get_array_size(this_obj);

        TRY(KOS_array_resize(ctx, this_obj, (uint32_t)size));

        if ((uint32_t)size > old_size && value_obj != KOS_VOID)
            TRY(KOS_array_fill(ctx, this_obj, old_size, size, value_obj));
    }

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

/* @item base array.prototype.fill()
 *
 *     array.prototype.fill(value)
 *     array.prototype.fill(begin, value)
 *     array.prototype.fill(begin, end, value)
 *
 * Fills specified portion of the array with a value.
 *
 * Returns the array object being filled (`this`).
 *
 * `value` is the object to fill the array with.
 *
 * `begin` is the index at which to start filling the array.  `begin` defaults
 * to `void`.  `void` is equivalent to index `0`.  If `begin` is negative, it
 * is an offset from the end of the array.
 *
 * `end` is the index at which to stop filling the array, the element at this
 * index will not be overwritten.  `end` defaults to `void`.  `void` is
 * equivalent to the size of the array.  If `end` is negative, it is an offset
 * from the end of the array.
 *
 * Example:
 *
 *     > const a = array(5)
 *     > a.fill("foo")
 *     ["foo", "foo", "foo", "foo", "foo"]
 */

/* @item base buffer.prototype.fill()
 *
 *     buffer.prototype.fill(value)
 *     buffer.prototype.fill(begin, value)
 *     buffer.prototype.fill(begin, end, value)
 *
 * Fills specified portion of the buffer with a value.
 *
 * Returns the buffer object being filled (`this`).
 *
 * `value` is the byte value to fill the buffer with.  It must be a number from
 * `0` to `255`, inclusive.  Float numbers are rounded using floor mode.
 *
 * `begin` is the index at which to start filling the buffer.  `begin` defaults
 * to `void`.  `void` is equivalent to index `0`.  If `begin` is negative, it
 * is an offset from the end of the buffer.
 *
 * `end` is the index at which to stop filling the buffer, the element at this
 * index will not be overwritten.  `end` defaults to `void`.  `void` is
 * equivalent to the size of the buffer.  If `end` is negative, it is an offset
 * from the end of the buffer.
 *
 * Example:
 *
 *     > const b = buffer(5)
 *     > b.fill(0x20)
 *     <20 20 20 20 20>
 */
static KOS_OBJ_ID fill(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     arg      = KOS_array_read(ctx, args_obj, 0);
    int64_t        begin    = 0;
    int64_t        end      = 0;

    if (num_args > 2) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &begin));
        else if (READ_OBJ_TYPE(arg) == OBJ_VOID)
            begin = 0;
        else {
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &end));
        else if (READ_OBJ_TYPE(arg) == OBJ_VOID)
            end = MAX_INT64;
        else {
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        arg = KOS_array_read(ctx, args_obj, 2);
        TRY_OBJID(arg);
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &begin));
        else if (READ_OBJ_TYPE(arg) == OBJ_VOID)
            begin = 0;
        else {
            KOS_raise_exception_cstring(ctx, str_err_unsup_operand_types);
            return KOS_BADPTR;
        }

        end = MAX_INT64;

        arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);
    }
    else {

        begin = 0;
        end   = MAX_INT64;
    }

    if (GET_OBJ_TYPE(this_obj) == OBJ_ARRAY)
        error = KOS_array_fill(ctx, this_obj, begin, end, arg);

    else {

        int64_t        value;

        TRY(KOS_get_integer(ctx, arg, &value));

        if (value < 0 || value > 255)
            RAISE_EXCEPTION(str_err_invalid_byte_value);

        error = KOS_buffer_fill(ctx, this_obj, begin, end, (uint8_t)value);
    }

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

struct KOS_PACK_FORMAT_S {
    KOS_OBJ_ID fmt_str;
    KOS_OBJ_ID data;
    int        idx;
    int        big_end;
};

typedef int (*KOS_PACK_FORMAT_FUNC)(KOS_CONTEXT               ctx,
                                    struct KOS_PACK_FORMAT_S *fmt,
                                    KOS_OBJ_ID                buffer_obj,
                                    char                      value_fmt,
                                    unsigned                  size,
                                    unsigned                  count);

static int is_whitespace(unsigned char_code)
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

static void pack_format_skip_spaces(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  fmt_str,
                                    unsigned   *i_ptr)
{
    const unsigned size = KOS_get_string_length(fmt_str);
    unsigned       i    = *i_ptr;
    unsigned       c;

    if (i >= size)
        return;

    do {
        c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);
        assert(c != ~0U);
    }
    while (i < size && is_whitespace(c));

    if (i < size || ! is_whitespace(c))
        i--;

    *i_ptr = i;
}

static unsigned pack_format_get_count(KOS_CONTEXT ctx,
                                      KOS_OBJ_ID  fmt_str,
                                      unsigned   *i_ptr)
{
    const unsigned size  = KOS_get_string_length(fmt_str);
    unsigned       i     = *i_ptr;
    unsigned       c;
    unsigned       count;

    assert(i < size);

    c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);

    assert(c >= '0' && c <= '9');

    count = c - (unsigned)'0';

    while (i < size) {

        c = KOS_string_get_char_code(ctx, fmt_str, (int)i++);

        assert(c != ~0U);

        if (c < '0' || c > '9') {
            i--;
            break;
        }

        if (count >= 429496729) {
            i--;
            break;
        }

        count = count * 10 + (c - (unsigned)'0');
    }

    *i_ptr = i;
    return count;
}

static int process_pack_format(KOS_CONTEXT               ctx,
                               KOS_OBJ_ID                buffer_obj,
                               KOS_PACK_FORMAT_FUNC      handler,
                               struct KOS_PACK_FORMAT_S *fmt)
{
    int            error    = KOS_SUCCESS;
    const unsigned fmt_size = KOS_get_string_length(fmt->fmt_str);
    unsigned       i_fmt    = 0;

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 1, &buffer_obj));
    }

    while (i_fmt < fmt_size) {

        unsigned count = 1;
        unsigned size  = 1;
        unsigned c;

        pack_format_skip_spaces(ctx, fmt->fmt_str, &i_fmt);

        if (i_fmt >= fmt_size)
            break;

        c = KOS_string_get_char_code(ctx, fmt->fmt_str, (int)i_fmt++);
        assert(c != ~0U);

        if (c >= '0' && c <= '9') {
            --i_fmt;
            count = pack_format_get_count(ctx, fmt->fmt_str, &i_fmt);
            assert(count != ~0U);

            pack_format_skip_spaces(ctx, fmt->fmt_str, &i_fmt);

            if (i_fmt >= fmt_size)
                RAISE_EXCEPTION(str_err_invalid_pack_format);

            c = KOS_string_get_char_code(ctx, fmt->fmt_str, (int)i_fmt++);
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
                pack_format_skip_spaces(ctx, fmt->fmt_str, &i_fmt);
                next_c = (i_fmt < fmt_size)
                         ? KOS_string_get_char_code(ctx, fmt->fmt_str, (int)i_fmt)
                         : ~0U;
                if (next_c >= '0' && next_c <= '9') {
                    size = pack_format_get_count(ctx, fmt->fmt_str, &i_fmt);
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
            TRY(handler(ctx, fmt, buffer_obj, (char)c, size, count));
    }

cleanup:
    return error;
}

static int pack_format(KOS_CONTEXT               ctx,
                       struct KOS_PACK_FORMAT_S *fmt,
                       KOS_OBJ_ID                buffer_obj,
                       char                      value_fmt,
                       unsigned                  size,
                       unsigned                  count)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    int        big_end;
    uint8_t   *dst    = 0;
    KOS_VECTOR str_buf;

    kos_vector_init(&str_buf);

    TRY(KOS_push_locals(ctx, &pushed, 1, &buffer_obj));

    if (fmt->idx < 0) {
        KOS_OBJ_ID obj = fmt->data;

        fmt->idx = 1;

        if (KOS_get_array_size(obj) > 1) {

            obj = KOS_array_read(ctx, obj, 1);
            TRY_OBJID(obj);

            if (GET_OBJ_TYPE(obj) == OBJ_ARRAY) {
                fmt->data = obj;
                fmt->idx  = 0;
            }
        }
    }

    assert(size != ~0U || value_fmt == 's');

    if (size != ~0U && size && count) {
        dst = KOS_buffer_make_room(ctx, buffer_obj, size * count);
        if ( ! dst)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    else if (size == ~0U && value_fmt != 's')
        RAISE_EXCEPTION(str_err_invalid_pack_format);

    big_end = fmt->big_end;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);

            if (dst && count)
                memset(dst, 0, count);
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
                KOS_OBJ_ID value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);

                TRY_OBJID(value_obj);

                if ( ! IS_NUMERIC_OBJ(value_obj))
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                TRY(KOS_get_integer(ctx, value_obj, &value));

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
                KOS_OBJ_ID value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                double     value     = 0;
                uint64_t   out_val;

                TRY_OBJID(value_obj);

                if (IS_SMALL_INT(value_obj))
                    value = (double)GET_SMALL_INT(value_obj);

                else switch (READ_OBJ_TYPE(value_obj)) {

                    case OBJ_INTEGER:
                        value = (double)(OBJPTR(INTEGER, value_obj)->value);
                        break;

                    case OBJ_FLOAT:
                        value = OBJPTR(FLOAT, value_obj)->value;
                        break;

                    default:
                        RAISE_EXCEPTION(str_err_bad_pack_value);
                        break;
                }

                if (size == 4)
                    out_val = kos_float_to_uint32_t((float)value);
                else
                    out_val = kos_double_to_uint64_t(value);

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
                KOS_OBJ_ID value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                uint8_t   *data      = 0;
                uint32_t   data_size;
                uint32_t   copy_size;

                TRY_OBJID(value_obj);

                if (GET_OBJ_TYPE(value_obj) != OBJ_BUFFER)
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                data_size = KOS_get_buffer_size(value_obj);
                if (data_size)
                    data = KOS_buffer_data_volatile(value_obj);

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
                KOS_OBJ_ID value_obj = KOS_array_read(ctx, fmt->data, fmt->idx++);
                uint32_t   copy_size;

                TRY_OBJID(value_obj);

                if (GET_OBJ_TYPE(value_obj) != OBJ_STRING)
                    RAISE_EXCEPTION(str_err_bad_pack_value);

                TRY(KOS_string_to_cstr_vec(ctx, value_obj, &str_buf));

                copy_size = size > str_buf.size-1 ? (uint32_t)str_buf.size-1 : size;

                if (size == ~0U) {
                    dst = KOS_buffer_make_room(ctx, buffer_obj, copy_size);
                    if ( ! dst)
                        RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }
                else {
                    assert(dst || ! copy_size);
                }

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

cleanup:
    KOS_pop_locals(ctx, pushed);
    kos_vector_destroy(&str_buf);
    return error;
}

static int unpack_format(KOS_CONTEXT               ctx,
                         struct KOS_PACK_FORMAT_S *fmt,
                         KOS_OBJ_ID                buffer_obj,
                         char                      value_fmt,
                         unsigned                  size,
                         unsigned                  count)
{
    int            error     = KOS_SUCCESS;
    int            pushed    = 0;
    int            offs;
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

    if ( ! count)
        return KOS_SUCCESS;

    TRY(KOS_push_locals(ctx, &pushed, 1, &buffer_obj));

    assert(data_size || ! size);
    assert( ! size || KOS_buffer_data_volatile(buffer_obj));

    offs = fmt->idx;

    switch (value_fmt) {

        case 'x':
            assert(size == 1);
            offs += size * count;
            break;

        case 'f':
            /* fall through */
        case 'i':
            /* fall through */
        case 'u': {
            if (size != 1 && size != 2 && size != 4 && size != 8)
                RAISE_EXCEPTION(str_err_invalid_pack_format);

            for ( ; count; count--) {
                uint64_t value = 0;
                unsigned i;

                for (i = 0; i < size; i++) {
                    const unsigned rel_offs = big_end ? i : (size - 1 - i);
                    value = (value << 8) | KOS_buffer_data_volatile(buffer_obj)[offs + rel_offs];
                }

                if (value_fmt == 'i' && size < 8) {
                    const unsigned shift = 64 - 8 * size;
                    const int64_t  ival  = (int64_t)(value << shift);
                    obj = KOS_new_int(ctx, ival >> shift);
                }
                else if (value_fmt == 'f') {
                    double fvalue;
                    if (size == 4) {
                        union {
                            float    f;
                            uint32_t u;
                        } u2f;
                        u2f.u  = (uint32_t)value;
                        fvalue = (double)u2f.f;
                    }
                    else {
                        union {
                            double   f;
                            uint64_t u;
                        } u2f;
                        u2f.u  = value;
                        fvalue = u2f.f;
                    }
                    obj = KOS_new_float(ctx, fvalue);
                }
                else
                    obj = KOS_new_int(ctx, (int64_t)value);

                TRY_OBJID(obj);

                TRY(KOS_array_push(ctx, fmt->data, obj, 0));

                offs += size;
            }
            break;
        }

        case 'b': {
            for ( ; count; count--) {
                obj = KOS_new_buffer(ctx, size);

                TRY_OBJID(obj);

                if (size)
                    memcpy(KOS_buffer_data_volatile(obj),
                           &KOS_buffer_data_volatile(buffer_obj)[offs],
                           size);

                TRY(KOS_array_push(ctx, fmt->data, obj, 0));

                offs += size;
            }
            break;
        }

        case 's':
            /* fall through */
        default: {
            assert(value_fmt == 's');
            for ( ; count; count--) {
                if (size)
                    obj = KOS_new_string_from_buffer(ctx, buffer_obj, offs, offs + size);
                else
                    obj = KOS_new_string(ctx, 0, 0);

                TRY_OBJID(obj);

                TRY(KOS_array_push(ctx, fmt->data, obj, 0));

                offs += size;
            }
            break;
        }
    }

    fmt->idx = offs;

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

/* @item base buffer.prototype.pack()
 *
 *     buffer.prototype.pack(format, args...)
 *
 * Convert parameters to binary form and appends them to a buffer.
 *
 * Returns the buffer which has been modified.
 *
 * `format` is a string, which describes how values are to be packed.
 *
 * TODO - refine format
 */
static KOS_OBJ_ID pack(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int                      error;
    struct KOS_PACK_FORMAT_S fmt;

    fmt.fmt_str = KOS_array_read(ctx, args_obj, 0);
    fmt.data    = args_obj;
    fmt.idx     = -1;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(fmt.fmt_str));

    {
        int pushed = 0;
        if (KOS_push_locals(ctx, &pushed, 3, &this_obj, &fmt.fmt_str, &fmt.data))
            return KOS_BADPTR;
    }

    if (GET_OBJ_TYPE(fmt.fmt_str) == OBJ_STRING)
        error = process_pack_format(ctx, this_obj, pack_format, &fmt);
    else {
        KOS_raise_exception_cstring(ctx, str_err_not_string);
        error = KOS_ERROR_EXCEPTION;
    }

    return error ? KOS_BADPTR : this_obj;
}

/* @item base buffer.prototype.unpack()
 *
 *     buffer.prototype.unpack(pos, format)
 *     buffer.prototype.unpack(format)
 *
 * Unpacks values from their binary form from a buffer.
 *
 * Returns an array containing unpacked values.
 *
 * `pos` is the position in the buffer at which to start extracting the values.
 * `pos` defaults to `0`.
 *
 * `format` is a string, which describes how values are to be unpacked.
 *
 * TODO - refine format
 */
static KOS_OBJ_ID unpack(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  this_obj,
                         KOS_OBJ_ID  args_obj)
{
    int                      error;
    struct KOS_PACK_FORMAT_S fmt;

    fmt.fmt_str = KOS_BADPTR;
    fmt.data    = KOS_BADPTR;
    fmt.idx     = 0;
    fmt.big_end = 0;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_BUFFER)
        RAISE_EXCEPTION(str_err_not_buffer);

    fmt.fmt_str = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(fmt.fmt_str);

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 4, &this_obj, &args_obj, &fmt.fmt_str, &fmt.data));
    }

    fmt.data = KOS_new_array(ctx, 0);
    TRY_OBJID(fmt.data);

    if (IS_NUMERIC_OBJ(fmt.fmt_str)) {
        int64_t idx = 0;

        TRY(KOS_get_integer(ctx, fmt.fmt_str, &idx));

        idx = kos_fix_index(idx, KOS_get_buffer_size(this_obj));

        fmt.idx = (int)idx;

        fmt.fmt_str = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(fmt.fmt_str);
    }

    if (GET_OBJ_TYPE(fmt.fmt_str) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    TRY(process_pack_format(ctx, this_obj, unpack_format, &fmt));

cleanup:
    return error ? KOS_BADPTR : fmt.data;
}

/* @item base buffer.prototype.copy_buffer()
 *
 *     buffer.prototype.copy_buffer(src_buf)
 *     buffer.prototype.copy_buffer(src_buf, src_begin)
 *     buffer.prototype.copy_buffer(src_buf, src_begin, src_end)
 *     buffer.prototype.copy_buffer(dst_begin, src_buf)
 *     buffer.prototype.copy_buffer(dst_begin, src_buf, src_begin)
 *     buffer.prototype.copy_buffer(dst_begin, src_buf, src_begin, src_end)
 *
 * Copies a range of bytes from source buffer to a buffer.
 *
 * Returns the destination buffer being modified (`this`).
 *
 * Stops copying once the last byte in the destination buffer is overwritten,
 * the destination buffer is not grown even if more bytes from the source
 * buffer could be copied.
 *
 * `dst_begin` is the position at which to start placing bytes from the source
 * buffer.  `dst_begin` defaults to `0`.  If it is `void`, it is equivalent
 * to `0`.  If it is negative, it is an offset from the end of the destination
 * buffer.
 *
 * `src_buf` is the source buffer to copy from.
 *
 * `src_begin` is the offset of the first byte in the source buffer to start
 * copying from.  `src_begin` defaults to `0`.  If it is `void`, it is
 * equivalent to `0`.  If it is negative, it is an offset from the end of
 * the source buffer.
 *
 * `src_end` is the offset of the byte at which to stop copying from the
 * source buffer.  This byte is not copied.  `src_end` defaults to the size
 * of the source buffer.  If it is `void`, it is equivalent to the size
 * of the source buffer.  If it is negative, it is an offset from the end
 * of the source buffer.
 *
 * Example:
 *
 *     > const dst = buffer([1, 1, 1, 1, 1])
 *     > const src = buffer([2, 2, 2, 2, 2])
 *     > dst.copy_buffer(2, src)
 *     <01 01 02 02 02>
 */
static KOS_OBJ_ID copy_buffer(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    KOS_OBJ_ID     arg        = KOS_array_read(ctx, args_obj, 0);
    int64_t        dest_begin = 0;
    int64_t        src_begin  = 0;
    int64_t        src_end    = MAX_INT64;
    KOS_OBJ_ID     src;

    if (num_args > 3) {

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &dest_begin));
        else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        src = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(src);

        arg = KOS_array_read(ctx, args_obj, 2);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &src_begin));
        else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        arg = KOS_array_read(ctx, args_obj, 3);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &src_end));
        else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);
    }
    else if (num_args > 2) {

        int arg_idx = 1;

        if (IS_NUMERIC_OBJ(arg) || READ_OBJ_TYPE(arg) == OBJ_VOID) {

            arg_idx = 2;

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(ctx, arg, &dest_begin));

            src = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(src);
        }
        else
            src = arg;

        arg = KOS_array_read(ctx, args_obj, arg_idx);
        TRY_OBJID(arg);

        if (IS_NUMERIC_OBJ(arg))
            TRY(KOS_get_integer(ctx, arg, &src_begin));
        else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
            RAISE_EXCEPTION(str_err_unsup_operand_types);

        if (arg_idx == 1) {

            arg = KOS_array_read(ctx, args_obj, arg_idx+1);
            TRY_OBJID(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(ctx, arg, &src_end));
            else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
                RAISE_EXCEPTION(str_err_unsup_operand_types);
        }
    }
    else if (num_args > 1) {

        if (IS_NUMERIC_OBJ(arg) || READ_OBJ_TYPE(arg) == OBJ_VOID) {

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(ctx, arg, &dest_begin));

            src = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(src);
        }
        else {

            src = arg;

            arg = KOS_array_read(ctx, args_obj, 1);
            TRY_OBJID(arg);

            if (IS_NUMERIC_OBJ(arg))
                TRY(KOS_get_integer(ctx, arg, &src_begin));
            else if (READ_OBJ_TYPE(arg) != OBJ_VOID)
                RAISE_EXCEPTION(str_err_unsup_operand_types);
        }
    }
    else {

        src        = arg;
        dest_begin = 0;
        src_begin  = 0;
        src_end    = MAX_INT64;
    }

    error = KOS_buffer_copy(ctx, this_obj, dest_begin, src, src_begin, src_end);

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

/* @item base array.prototype.reserve()
 *
 *     array.prototype.reserve(size)
 *
 * Allocate array storage without resizing the array.
 *
 * The function has no visible effect, but can be used for optimization
 * to avoid reallocating array storage when resizing it or continuously
 * adding more elements.
 *
 * Returns the array object itself (`this`).
 */

/* @item base buffer.prototype.reserve()
 *
 *     buffer.prototype.reserve(size)
 *
 * Allocate buffer storage without resizing the buffer.
 *
 * The function has no visible effect, but can be used for optimization
 * to avoid reallocating buffer storage when resizing it.
 *
 * Returns the buffer object itself (`this`).
 */
static KOS_OBJ_ID reserve(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    int64_t    size;
    KOS_OBJ_ID size_obj;

    TRY(KOS_push_locals(ctx, &pushed, 2, &this_obj, &args_obj));

    size_obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(size_obj);

    TRY(KOS_get_integer(ctx, size_obj, &size));

    if (GET_OBJ_TYPE(this_obj) == OBJ_BUFFER) {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_buffer_size);

        TRY(KOS_buffer_reserve(ctx, this_obj, (uint32_t)size));
    }
    else {
        if (size < 0 || size > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_array_size);

        TRY(KOS_array_reserve(ctx, this_obj, (uint32_t)size));
    }

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

/* @item base array.prototype.cas()
 *
 *     array.prototype.cas(pos, old_val, new_val)
 *
 * Atomic compare-and-swap for an array element.
 *
 * If array element at index `pos` equals to `old_val`, it is swapped
 * with element `new_val`.  If the current array element at that index
 * is not `old_val`, it is left unchanged.
 *
 * The compare-and-swap operation is performed atomically, but without any ordering
 * guarantees.
 *
 * The element comparison is done by comparing object reference, not contents.
 *
 * Returns the element stored in the array at index `pos`.
 */
static KOS_OBJ_ID array_cas(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int            error = KOS_SUCCESS;
    int64_t        pos   = 0;
    KOS_OBJ_ID     ret   = KOS_BADPTR;
    KOS_OBJ_ID     pos_obj;
    KOS_OBJ_ID     old_val;
    KOS_OBJ_ID     new_val;

    if (GET_OBJ_TYPE(this_obj) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    pos_obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(pos_obj);

    old_val = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(pos_obj);

    new_val = KOS_array_read(ctx, args_obj, 2);
    TRY_OBJID(pos_obj);

    if (!IS_NUMERIC_OBJ(pos_obj))
        RAISE_EXCEPTION(str_err_unsup_operand_types);

    TRY(KOS_get_integer(ctx, pos_obj, &pos));

    ret = KOS_array_cas(ctx, this_obj, (int)pos, old_val, new_val);


cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base array.prototype.insert_array()
 *
 *     array.prototype.insert_array(pos, array)
 *     array.prototype.insert_array(begin, end, array)
 *
 * Inserts elements from one array into `this` array, possibly replacing
 * existing elements.
 *
 * This function is identical in behavior to `array.prototype.insert()`.  In
 * most circumstances `array.prototype.insert()` is recommended instead.
 * `array.prototype.insert_array()` requires the iterable argument to be
 * an array.
 */
static KOS_OBJ_ID insert_array(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    int            pushed   = 0;
    KOS_OBJ_ID     begin_obj;
    KOS_OBJ_ID     end_obj;
    KOS_OBJ_ID     src_obj;
    int64_t        begin    = 0;
    int64_t        end      = 0;
    int64_t        src_len;

    TRY(KOS_push_locals(ctx, &pushed, 2, &this_obj, &args_obj));

    begin_obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(begin_obj);

    end_obj = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(end_obj);

    if (num_args > 2) {
        src_obj = KOS_array_read(ctx, args_obj, 2);
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
        TRY(KOS_get_integer(ctx, begin_obj, &begin));
    else if (READ_OBJ_TYPE(begin_obj) == OBJ_VOID)
        begin = num_args == 2 ? MAX_INT64 : 0;
    else
        RAISE_EXCEPTION(str_err_unsup_operand_types);

    if (IS_NUMERIC_OBJ(end_obj))
        TRY(KOS_get_integer(ctx, end_obj, &end));
    else if (READ_OBJ_TYPE(end_obj) == OBJ_VOID)
        end = MAX_INT64;
    else
        RAISE_EXCEPTION(str_err_unsup_operand_types);

    src_len = MAX_INT64;

    TRY(KOS_array_insert(ctx, this_obj, begin, end, src_obj, 0, src_len));

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

/* @item base array.prototype.pop()
 *
 *     array.prototype.pop(num_elements = 1)
 *
 * Removes elements from the end of array.
 *
 * `num_elements` is the number of elements to remove and it defaults to `1`.
 *
 * If `num_elements` is `1`, returns the element removed.
 * If `num_elements` is `0`, returns `void`.
 * If `num_elements` is greater than `1`, returns an array
 * containing the elements removed.
 *
 * Throws if the array is empty or if more elements are being removed
 * than the array already contains.
 *
 * Example:
 *
 *     > [1, 2, 3, 4, 5].pop()
 *     5
 */
static KOS_OBJ_ID pop(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  this_obj,
                      KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    KOS_OBJ_ID     ret      = KOS_BADPTR;
    const uint32_t num_args = KOS_get_array_size(args_obj);

    if (num_args == 0)
        ret = KOS_array_pop(ctx, this_obj);

    else {
        int64_t    num    = 0;
        int        pushed = 0;
        int        idx;
        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &num));

        if (num < 0 || num > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_array_size);

        TRY(KOS_push_locals(ctx, &pushed, 3, &this_obj, &ret, &arg));

        if (num == 0)
            ret = KOS_VOID;
        else
            ret = KOS_new_array(ctx, (unsigned)num);
        TRY_OBJID(ret);

        for (idx = (int)(num - 1); idx >= 0; idx--) {
            arg = KOS_array_pop(ctx, this_obj);
            TRY_OBJID(arg);

            TRY(KOS_array_write(ctx, ret, idx, arg));
        }
    }

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base array.prototype.push()
 *
 *     array.prototype.push(values...)
 *
 * Appends every value argument to the array.
 *
 * Returns the old array size before the first element was inserted.
 * If one or more elements are specified to insert, the returned value
 * is equivalent to the index of the first element inserted.
 *
 * Example:
 *
 *     > [1, 1, 1].push(10, 20)
 *     3
 */
static KOS_OBJ_ID push(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    int            pushed   = 0;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    uint32_t       i;
    KOS_OBJ_ID     ret      = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 2, &this_obj, &args_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    ret = KOS_new_int(ctx, (int64_t)KOS_get_array_size(this_obj));
    TRY_OBJID(ret);

    if (num_args > 1)
        TRY(KOS_array_reserve(ctx, this_obj,
                    KOS_get_array_size(this_obj) + num_args));

    for (i = 0; i < num_args; i++) {
        uint32_t   idx      = ~0U;
        KOS_OBJ_ID elem_obj = KOS_array_read(ctx, args_obj, (int)i);
        TRY_OBJID(elem_obj);

        TRY(KOS_array_push(ctx, this_obj, elem_obj, &idx));

        if (i == 0) {
            ret = KOS_new_int(ctx, (int64_t)idx);
            TRY_OBJID(ret);
        }
    }

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base string.prototype.ends_with()
 *
 *     string.prototype.ends_with(str)
 *
 * Determines if a string ends with `str`.
 *
 * `str` is a string which is matched against the end of the current string
 * (`this`).
 *
 * Returns `true` if the current string ends with `str` or `false` otherwise.
 *
 * Examples:
 *
 *     > "foobar".ends_with("bar")
 *     true
 *     > "foobar".ends_with("foo")
 *     false
 */
static KOS_OBJ_ID ends_with(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    unsigned   this_len;
    unsigned   arg_len;

    arg = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(arg) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    this_len = KOS_get_string_length(this_obj);
    arg_len  = KOS_get_string_length(arg);

    if (arg_len > this_len)
        ret = KOS_FALSE;
    else
        ret = KOS_BOOL( ! KOS_string_compare_slice(this_obj,
                                                   this_len - arg_len,
                                                   this_len,
                                                   arg,
                                                   0,
                                                   arg_len));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base string.prototype.repeat()
 *
 *     string.prototype.repeat(num)
 *
 * Creates a repeated string.
 *
 * `num` is a non-negative number of times to repeat the string.
 *
 * If `num` is a float, it is converted to integer using floor mode.
 *
 * Examples:
 *
 *     > "-".repeat(10)
 *     "----------"
 *     > "foo".repeat(5)
 *     "foofoofoofoofoo"
 */
static KOS_OBJ_ID repeat(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  this_obj,
                         KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg   = KOS_array_read(ctx, args_obj, 0);
    KOS_OBJ_ID ret   = KOS_BADPTR;
    int64_t    num;
    unsigned   text_len;

    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    TRY(KOS_get_integer(ctx, arg, &num));

    text_len = KOS_get_string_length(this_obj);

    if (num < 0 || num > 0xFFFFU || (num * text_len) > 0xFFFFU)
        RAISE_EXCEPTION(str_err_too_many_repeats);

    ret = KOS_string_repeat(ctx, this_obj, (unsigned)num);

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base string.prototype.find()
 *
 *     string.prototype.find(substr, pos = 0)
 *
 * Searches for a substring in a string from left to right.
 *
 * Returns index of the first substring found or `-1` if the substring was not
 * found.
 *
 * `substr` is the substring to search for.  The search is case sensitive and
 * an exact match must be found.
 *
 * `pos` is the index in the string at which to begin the search.  It defaults
 * to `0`.  If it is a float, it is converted to integer using floor mode.
 * If it is negative, it is an offset from the end of the string.
 *
 * Examples:
 *
 *     > "kos".find("foo")
 *     -1
 *     > "language".find("gu")
 *     3
 *     > "language".find("g", -3)
 *     6
 */
static KOS_OBJ_ID find(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int        error   = KOS_SUCCESS;
    int        pos     = 0;
    KOS_OBJ_ID pattern = KOS_array_read(ctx, args_obj, 0);

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t        pos64;
        const unsigned len = KOS_get_string_length(this_obj);

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &pos64));

        pos = (int)kos_fix_index(pos64, len);
    }

    TRY(KOS_string_find(ctx, this_obj, pattern, KOS_FIND_FORWARD, &pos));

cleanup:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

/* @item base string.prototype.rfind()
 *
 *     string.prototype.rfind(substr, pos = -1)
 *
 * Performs a reverse search for a substring in a string, i.e. from right to
 * left.
 *
 * Returns index of the first substring found or `-1` if the substring was not
 * found.
 *
 * `substr` is the substring to search for.  The search is case sensitive and
 * an exact match must be found.
 *
 * `pos` is the index in the string at which to begin the search.  It defaults
 * to `-1`, which means the search by default starts from the last character of
 * the string.  If `pos` is a float, it is converted to integer using floor
 * mode.  If it is negative, it is an offset from the end of the string.
 *
 * Examples:
 *
 *     > "kos".rfind("foo")
 *     -1
 *     > "language".rfind("a")
 *     5
 *     > "language".find("a", 4)
 *     1
 */
static KOS_OBJ_ID rfind(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int        error   = KOS_SUCCESS;
    int        pos     = -1;
    KOS_OBJ_ID pattern = KOS_array_read(ctx, args_obj, 0);
    unsigned   text_len;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    text_len = KOS_get_string_length(this_obj);
    pos      = (int)(text_len - KOS_get_string_length(pattern));

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t pos64;

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &pos64));

        if (pos64 < -(int64_t)text_len)
            pos = -1;
        else {
            const int new_pos = (int)kos_fix_index(pos64, text_len);

            if (new_pos < pos)
                pos = new_pos;
        }
    }

    TRY(KOS_string_find(ctx, this_obj, pattern, KOS_FIND_REVERSE, &pos));

cleanup:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

/* @item base string.prototype.scan()
 *
 *     string.prototype.scan(chars, inclusive)
 *     string.prototype.scan(chars, pos = 0, inclusive = true)
 *
 * Scans the string for any matching characters from left to right.
 *
 * Returns the position of the first matching character found or `-1` if no
 * matching characters were found.
 *
 * `chars` is a string containing zero or more characters to be matched.
 * The search starts at position `pos` and stops as soon as any character
 * from `chars` is found.
 *
 * `pos` is the index in the string at which to begin the search.  It defaults
 * to `0`.  If it is a float, it is converted to integer using floor mode.
 * If it is negative, it is an offset from the end of the string.
 *
 * If `inclusive` is `true` (the default), characters in `chars` are sought.
 * If `inclusive` is `false`, then the search stops as soon as any character
 * *not* in `chars` is found.
 *
 * Examples:
 *
 *     > "kos".scan("")
 *     0
 *     > "kos".scan("s")
 *     2
 *     > "language".scan("uga", -5, false)
 *     7
 */
static KOS_OBJ_ID scan(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int                     error   = KOS_SUCCESS;
    int                     pos     = 0;
    KOS_OBJ_ID              pattern = KOS_array_read(ctx, args_obj, 0);
    enum KOS_SCAN_INCLUDE_E include = KOS_SCAN_INCLUDE;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t        pos64;
        const unsigned len = KOS_get_string_length(this_obj);

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        if (GET_OBJ_TYPE(arg) == OBJ_BOOLEAN) {
            if ( ! KOS_get_bool(arg))
                include = KOS_SCAN_EXCLUDE;
        }
        else {

            TRY(KOS_get_integer(ctx, arg, &pos64));

            pos = (int)kos_fix_index(pos64, len);

            if (KOS_get_array_size(args_obj) > 2) {

                arg = KOS_array_read(ctx, args_obj, 2);
                TRY_OBJID(arg);

                if (GET_OBJ_TYPE(arg) == OBJ_BOOLEAN) {
                    if ( ! KOS_get_bool(arg))
                        include = KOS_SCAN_EXCLUDE;
                }
                else
                    RAISE_EXCEPTION(str_err_not_boolean);
            }
        }
    }

    TRY(KOS_string_scan(ctx, this_obj, pattern, KOS_FIND_FORWARD, include, &pos));

cleanup:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

/* @item base string.prototype.rscan()
 *
 *     string.prototype.rscan(chars, inclusive)
 *     string.prototype.rscan(chars, pos = 0, inclusive = true)
 *
 * Scans the string for any matching characters in reverse direction, i.e. from
 * right to left.
 *
 * Returns the position of the first matching character found or `-1` if no
 * matching characters were found.
 *
 * `chars` is a string containing zero or more characters to be matched.
 * The search starts at position `pos` and stops as soon as any character
 * from `chars` is found.
 *
 * `pos` is the index in the string at which to begin the search.  It defaults
 * to `-1`, which means the search by default starts from the last character of
 * the string.  If `pos` is a float, it is converted to integer using floor
 * mode.  If it is negative, it is an offset from the end of the string.
 *
 * If `inclusive` is `true` (the default), characters in `chars` are sought.
 * If `inclusive` is `false`, then the search stops as soon as any character
 * *not* in `chars` is found.
 *
 * Examples:
 *
 *     > "language".rscan("g")
 *     6
 *     > "language".rscan("uga", -2, false)
 *     2
 */
static KOS_OBJ_ID rscan(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int                     error   = KOS_SUCCESS;
    int                     pos     = -1;
    KOS_OBJ_ID              pattern = KOS_array_read(ctx, args_obj, 0);
    enum KOS_SCAN_INCLUDE_E include = KOS_SCAN_INCLUDE;
    unsigned                text_len;

    TRY_OBJID(pattern);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(pattern) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    text_len = KOS_get_string_length(this_obj);
    pos      = (int)(text_len - 1);

    if (KOS_get_array_size(args_obj) > 1) {

        int64_t pos64;

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(arg);

        if (GET_OBJ_TYPE(arg) == OBJ_BOOLEAN) {
            if ( ! KOS_get_bool(arg))
                include = KOS_SCAN_EXCLUDE;
        }
        else {

            TRY(KOS_get_integer(ctx, arg, &pos64));

            if (pos64 < -(int64_t)text_len)
                pos = -1;
            else {
                const int new_pos = (int)kos_fix_index(pos64, text_len);

                if (new_pos < pos)
                    pos = new_pos;
            }

            if (KOS_get_array_size(args_obj) > 2) {

                arg = KOS_array_read(ctx, args_obj, 2);
                TRY_OBJID(arg);

                if (GET_OBJ_TYPE(arg) == OBJ_BOOLEAN) {
                    if ( ! KOS_get_bool(arg))
                        include = KOS_SCAN_EXCLUDE;
                }
                else
                    RAISE_EXCEPTION(str_err_not_boolean);
            }
        }
    }

    TRY(KOS_string_scan(ctx, this_obj, pattern, KOS_FIND_REVERSE, include, &pos));

cleanup:
    return error ? KOS_BADPTR : TO_SMALL_INT(pos);
}

/* @item base string.prototype.code()
 *
 *     string.prototype.code(pos = 0)
 *
 * Returns code point of a character at a given position in a string.
 *
 * `pos` is the position of the character for which the code point is returned.
 * `pos` defaults to `0`.  If `pos` is a float, it is converted to integer
 * using floor method.  If `pos` is negative, it is an offset from the end of
 * the string.
 *
 * Examples:
 *
 *     > "a".code()
 *     97
 *     > "kos".code(2)
 *     115
 *     > "language".code(-2)
 *     103
 */
static KOS_OBJ_ID code(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    int64_t    idx   = 0;
    unsigned   char_code;

    if (KOS_get_array_size(args_obj) > 0) {

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);
        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &idx));

        if (idx < INT_MIN || idx > INT_MAX)
            RAISE_EXCEPTION(str_err_invalid_string_idx);
    }

    char_code = KOS_string_get_char_code(ctx, this_obj, (int)idx);
    if (char_code == ~0U)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    ret = KOS_new_int(ctx, (int64_t)char_code);

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base string.prototype.starts_with()
 *
 *     string.prototype.starts_with(str)
 *
 * Determines if a string begins with `str`.
 *
 * `str` is a string which is matched against the beginning of the current
 * string (`this`).
 *
 * Returns `true` if the current string begins with `str` or `false` otherwise.
 *
 * Examples:
 *
 *     > "foobar".starts_with("foo")
 *     true
 *     > "foobar".starts_with("bar")
 *     false
 */
static KOS_OBJ_ID starts_with(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID arg;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    unsigned   this_len;
    unsigned   arg_len;

    arg = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(this_obj) != OBJ_STRING || GET_OBJ_TYPE(arg) != OBJ_STRING)
        RAISE_EXCEPTION(str_err_not_string);

    this_len = KOS_get_string_length(this_obj);
    arg_len  = KOS_get_string_length(arg);

    if (arg_len > this_len)
        ret = KOS_FALSE;
    else
        ret = KOS_BOOL( ! KOS_string_compare_slice(this_obj,
                                                   0,
                                                   arg_len,
                                                   arg,
                                                   0,
                                                   arg_len));

cleanup:
    return error ? KOS_BADPTR : ret;
}

/* @item base string.prototype.lowercase()
 *
 *     string.prototype.lowercase(str)
 *
 * Returns a copy of the string with all alphabetical characters converted to
 * lowercase.
 *
 * Examples:
 *
 *     > "Kos".lowercase()
 *     "kos"
 *     > "Text 123 stRIng".lowercase()
 *     "text 123 string"
 */
static KOS_OBJ_ID lowercase(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    return KOS_string_lowercase(ctx, this_obj);
}

/* @item base string.prototype.uppercase()
 *
 *     string.prototype.uppercase(str)
 *
 * Returns a copy of the string with all alphabetical characters converted to
 * uppercase.
 *
 * Examples:
 *
 *     > "Kos".uppercase()
 *     "KOS"
 *     > "Text 123 stRIng".uppercase()
 *     "TEXT 123 STRING"
 */
static KOS_OBJ_ID uppercase(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    return KOS_string_uppercase(ctx, this_obj);
}

/* @item base string.prototype.size
 *
 *     string.prototype.size
 *
 * Read-only size of the string (integer).
 *
 * Example:
 *
 *     > "rain\x{2601}".size
 *     5
 */
static KOS_OBJ_ID get_string_size(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_STRING)
        ret = KOS_new_int(ctx, (int64_t)KOS_get_string_length(this_obj));
    else {
        KOS_raise_exception_cstring(ctx, str_err_not_string);
        ret = KOS_BADPTR;
    }

    return ret;
}

/* @item base string.prototype.reverse()
 *
 *     string.prototype.reverse()
 *
 * Returns a reversed string.
 *
 * Example:
 *
 *     > "kos".reverse()
 *     "sok"
 */
static KOS_OBJ_ID reverse(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    return KOS_string_reverse(ctx, this_obj);
}

/* @item base function.prototype.line
 *
 *     function.prototype.line
 *
 * Read-only line at which the function was defined in the source code.
 */
static KOS_OBJ_ID get_function_line(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func = OBJPTR(FUNCTION, this_obj);
        unsigned      line = 0U;

        if ( ! IS_BAD_PTR(func->module) && func->instr_offs != ~0U)
            line = KOS_module_addr_to_func_line(OBJPTR(MODULE, func->module),
                                                func->instr_offs);
        ret = TO_SMALL_INT((int64_t)line);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base function.prototype.name
 *
 *     function.prototype.name
 *
 * Read-only function name.
 *
 * Example:
 *
 *     > count.name
 *     "count"
 */
static KOS_OBJ_ID get_function_name(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func = OBJPTR(FUNCTION, this_obj);

        /* TODO add builtin function name */
        if (IS_BAD_PTR(func->module) || func->instr_offs == ~0U)
            ret = KOS_STR_XBUILTINX;
        else
            ret = KOS_module_addr_to_func_name(ctx,
                                               OBJPTR(MODULE, func->module),
                                               func->instr_offs);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base function.prototype.offset
 *
 *     function.prototype.offset
 *
 * Read-only offset of function's bytecode.
 *
 * Zero, if this is a built-in function.
 *
 * Example:
 *
 *     > count.offset
 *     2973
 */
static KOS_OBJ_ID get_function_offs(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  this_obj,
                                    KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func = OBJPTR(FUNCTION, this_obj);

        uint32_t offs = func->instr_offs;

        if (offs == ~0U)
            offs = 0;

        ret = KOS_new_int(ctx, offs);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base function.prototype.instructions
 *
 *     function.prototype.instructions
 *
 * Read-only number of bytecode instructions generated for this function.
 *
 * Zero, if this is a built-in function.
 *
 * Example:
 *
 *     > count.instructions
 *     26
 */
static KOS_OBJ_ID get_instructions(KOS_CONTEXT ctx,
                                   KOS_OBJ_ID  this_obj,
                                   KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func      = OBJPTR(FUNCTION, this_obj);
        uint32_t      num_instr = 0;

        if ( ! IS_BAD_PTR(func->module))
            num_instr = KOS_module_func_get_num_instr(OBJPTR(MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(ctx, (int64_t)num_instr);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base function.prototype.size
 *
 *     function.prototype.size
 *
 * Read-only size of bytecode generated for this function, in bytes.
 *
 * Zero, if this is a built-in function.
 *
 * Example:
 *
 *     > count.size
 *     133
 */
static KOS_OBJ_ID get_code_size(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func      = OBJPTR(FUNCTION, this_obj);
        uint32_t      code_size = 0;

        if ( ! IS_BAD_PTR(func->module))
            code_size = KOS_module_func_get_code_size(OBJPTR(MODULE, func->module),
                                                      func->instr_offs);

        ret = KOS_new_int(ctx, (int64_t)code_size);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base class.prototype.prototype
 *
 *     class.prototype.prototype
 *
 * Allows reading and setting prototype on class objects.
 *
 * The prototype set or retrieved is the prototype used when creating
 * new objects of this class.
 */

/* @item base function.prototype.registers
 *
 *     function.prototype.registers
 *
 * Read-only number of registers used by the function.
 *
 * Zero, if this is a built-in function.
 *
 * Example:
 *
 *     > count.registers
 *     5
 */
static KOS_OBJ_ID get_registers(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID     ret  = KOS_BADPTR;
    const KOS_TYPE type = GET_OBJ_TYPE(this_obj);

    if (type == OBJ_FUNCTION || type == OBJ_CLASS) {

        KOS_FUNCTION *func = OBJPTR(FUNCTION, this_obj);

        ret = KOS_new_int(ctx, (int64_t)func->num_regs);
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_function);

    return ret;
}

/* @item base exception.prototype.print()
 *
 *     exception.prototype.print()
 *
 * Prints the exception object on stdout.
 */
static KOS_OBJ_ID print_exception(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    int error;

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 1, &this_obj));
    }

    KOS_raise_exception(ctx, this_obj);

    KOS_print_exception(ctx, KOS_STDOUT);

    if (KOS_is_exception_pending(ctx))
        error = KOS_ERROR_EXCEPTION;

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

int kos_module_base_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;

    TRY(KOS_push_locals(ctx, &pushed, 1, &module));

    TRY_ADD_FUNCTION( ctx, module, "print",      print,     0);
    TRY_ADD_FUNCTION( ctx, module, "stringify",  stringify, 0);
    TRY_ADD_GENERATOR(ctx, module, "deep",       deep,      1);
    TRY_ADD_GENERATOR(ctx, module, "shallow",    shallow,   1);

    TRY_ADD_GLOBAL(   ctx, module, "args",       ctx->inst->args);

    TRY_CREATE_CONSTRUCTOR(array,         module);
    TRY_CREATE_CONSTRUCTOR(boolean,       module);
    TRY_CREATE_CONSTRUCTOR(buffer,        module);
    TRY_CREATE_CONSTRUCTOR(class,         module);
    TRY_CREATE_CONSTRUCTOR(exception,     module);
    TRY_CREATE_CONSTRUCTOR(float,         module);
    TRY_CREATE_CONSTRUCTOR(function,      module);
    TRY_CREATE_CONSTRUCTOR(generator,     module);
    TRY_CREATE_CONSTRUCTOR(generator_end, module);
    TRY_CREATE_CONSTRUCTOR(integer,       module);
    TRY_CREATE_CONSTRUCTOR(number,        module);
    TRY_CREATE_CONSTRUCTOR(object,        module);
    TRY_CREATE_CONSTRUCTOR(string,        module);
    TRY_CREATE_CONSTRUCTOR(thread,        module);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "cas",           array_cas,         3);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "insert_array",  insert_array,      2);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "fill",          fill,              1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "pop",           pop,               0);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "push",          push,              0);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "reserve",       reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "resize",        resize,            1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "slice",         slice,             2);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(array),      "sort",          sort,              0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(array),      "size",          get_array_size,    0);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "copy_buffer",   copy_buffer,       1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "fill",          fill,              1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "pack",          pack,              1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "reserve",       reserve,           1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "resize",        resize,            1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "slice",         slice,             2);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(buffer),     "unpack",        unpack,            1);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(buffer),     "size",          get_buffer_size,   0);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(exception),  "print",         print_exception,   0);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(function),   "apply",         apply,             2);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(function),   "async",         async,             2);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "instructions",  get_instructions,  0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "line",          get_function_line, 0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "name",          get_function_name, 0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "offset",        get_function_offs, 0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "registers",     get_registers,     0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(function),   "size",          get_code_size,     0);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "ends_with",     ends_with,         1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "find",          find,              1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "code",          code,              0);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "lowercase",     lowercase,         0);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "repeat",        repeat,            1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "rfind",         rfind,             1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "rscan",         rscan,             1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "reverse",       reverse,           0);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "scan",          scan,              1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "slice",         slice,             2);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "starts_with",   starts_with,       1);
    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(string),     "uppercase",     uppercase,         0);
    TRY_ADD_MEMBER_PROPERTY( ctx, module, PROTO(string),     "size",          get_string_size,   0);

    TRY_ADD_MEMBER_FUNCTION( ctx, module, PROTO(thread),     "wait",          wait,              0);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

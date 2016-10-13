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

#include "../inc/kos.h"

namespace kos {

template<typename T>
T numeric_from_object_ptr(context& ctx, KOS_OBJ_PTR objptr)
{
    T ret = 0;

    assert( ! IS_BAD_PTR(objptr));
    if (IS_SMALL_INT(objptr))
        /* TODO check range */
        return static_cast<T>(GET_SMALL_INT(objptr));
    else switch (GET_OBJ_TYPE(objptr)) {

        case OBJ_INTEGER: {
            const int64_t number = OBJPTR(KOS_INTEGER, objptr)->number;
            /* TODO check range */
            ret = static_cast<T>(number);
            break;
        }

        case OBJ_FLOAT: {
            const double number = OBJPTR(KOS_FLOAT, objptr)->number;
            /* TODO check range */
            ret = static_cast<T>(number);
            break;
        }

        default: {
            ctx.raise("source type is not a number");
            break;
        }
    }

    return ret;
}

template<>
int value_from_object_ptr<int>(context& ctx, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<int>(ctx, objptr);
}

template<>
int64_t value_from_object_ptr<int64_t>(context& ctx, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<int64_t>(ctx, objptr);
}

template<>
double value_from_object_ptr<double>(context& ctx, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<double>(ctx, objptr);
}

template<>
bool value_from_object_ptr<bool>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_BOOLEAN, objptr))
        ctx.raise("source type is not a boolean");

    return !! KOS_get_bool(objptr);
}

template<>
std::string value_from_object_ptr<std::string>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_STRING_OBJ(objptr))
        ctx.raise("source type is not a string");

    const unsigned len = KOS_string_to_utf8(objptr, 0, 0);
    std::string    str(static_cast<size_t>(len), '\0');

    KOS_string_to_utf8(objptr, &str[0], len);
    return str;
}

template<>
string value_from_object_ptr<string>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_STRING_OBJ(objptr))
        ctx.raise("source type is not a string");

    return string(objptr);
}

template<>
object value_from_object_ptr<object>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_OBJECT, objptr))
        ctx.raise("source type is not an object");

    return object(ctx, objptr);
}

template<>
array value_from_object_ptr<array>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_ARRAY, objptr))
        ctx.raise("source type is not an array");

    return array(ctx, objptr);
}

template<>
buffer value_from_object_ptr<buffer>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_BUFFER, objptr))
        ctx.raise("source type is not a buffer");

    return buffer(ctx, objptr);
}

template<>
function value_from_object_ptr<function>(context& ctx, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_FUNCTION, objptr))
        ctx.raise("source type is not a function");

    return function(ctx, objptr);
}

void context::raise(const char* desc)
{
    KOS_raise_exception(&_ctx, KOS_new_cstring(&_ctx, desc));
    signal_error();
}

std::string exception::get_exception_string(context& ctx)
{
    KOS_OBJ_PTR obj = KOS_get_exception(ctx);
    assert( ! IS_BAD_PTR(obj));

    if ( ! IS_STRING_OBJ(obj)) {

        static KOS_ASCII_STRING(str_value, "value");

        obj = KOS_get_property(ctx, obj, TO_OBJPTR(&str_value));

        assert( ! IS_BAD_PTR(obj));

        obj = KOS_object_to_string(ctx, obj);

        assert( ! IS_BAD_PTR(obj));
    }

    return from_object_ptr(ctx, obj);
}

} // namespace kos

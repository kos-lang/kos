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

#include "../inc/kos.h"

namespace kos {

template<typename T>
T numeric_from_object_ptr(stack_frame frame, KOS_OBJ_PTR objptr)
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
            frame.raise_and_signal_error("source type is not a number");
            break;
        }
    }

    return ret;
}

template<>
int value_from_object_ptr<int>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<int>(frame, objptr);
}

template<>
int64_t value_from_object_ptr<int64_t>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<int64_t>(frame, objptr);
}

template<>
double value_from_object_ptr<double>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    return numeric_from_object_ptr<double>(frame, objptr);
}

template<>
bool value_from_object_ptr<bool>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_BOOLEAN, objptr))
        frame.raise_and_signal_error("source type is not a boolean");

    return !! KOS_get_bool(objptr);
}

template<>
std::string value_from_object_ptr<std::string>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_STRING_OBJ(objptr))
        frame.raise_and_signal_error("source type is not a string");

    const unsigned len = KOS_string_to_utf8(objptr, 0, 0);
    std::string    str(static_cast<size_t>(len), '\0');

    KOS_string_to_utf8(objptr, &str[0], len);
    return str;
}

template<>
string value_from_object_ptr<string>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_STRING_OBJ(objptr))
        frame.raise_and_signal_error("source type is not a string");

    return string(objptr);
}

template<>
void_ value_from_object_ptr<void_>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_VOID, objptr))
        frame.raise_and_signal_error("source type is not a void");

    return void_(objptr);
}

template<>
object value_from_object_ptr<object>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_OBJECT, objptr))
        frame.raise_and_signal_error("source type is not an object");

    return object(frame, objptr);
}

template<>
array value_from_object_ptr<array>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_ARRAY, objptr))
        frame.raise_and_signal_error("source type is not an array");

    return array(frame, objptr);
}

template<>
buffer value_from_object_ptr<buffer>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_BUFFER, objptr))
        frame.raise_and_signal_error("source type is not a buffer");

    return buffer(frame, objptr);
}

template<>
function value_from_object_ptr<function>(stack_frame frame, KOS_OBJ_PTR objptr)
{
    assert( ! IS_BAD_PTR(objptr));
    if ( ! IS_TYPE(OBJ_FUNCTION, objptr))
        frame.raise_and_signal_error("source type is not a function");

    return function(frame, objptr);
}

void stack_frame::raise(const char* desc)
{
    KOS_raise_exception(_frame, KOS_new_cstring(_frame, desc));
}

std::string exception::get_exception_string(stack_frame frame)
{
    KOS_OBJ_PTR obj = KOS_get_exception(frame);
    assert( ! IS_BAD_PTR(obj));

    if ( ! IS_STRING_OBJ(obj)) {

        static KOS_ASCII_STRING(str_value, "value");

        obj = KOS_get_property(frame, obj, TO_OBJPTR(&str_value));

        assert( ! IS_BAD_PTR(obj));

        obj = KOS_object_to_string(frame, obj);

        assert( ! IS_BAD_PTR(obj));
    }

    return from_object_ptr(frame, obj);
}

} // namespace kos

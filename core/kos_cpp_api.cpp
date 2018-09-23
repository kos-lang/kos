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

#include "../inc/kos.h"

namespace kos {

template<typename T>
T numeric_from_object_ptr(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    T ret = 0;

    assert( ! IS_BAD_PTR(obj_id));

    if (IS_SMALL_INT(obj_id)) {
        /* TODO check range */
        return static_cast<T>(GET_SMALL_INT(obj_id));
    }
    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER: {
            const int64_t number = OBJPTR(INTEGER, obj_id)->value;
            /* TODO check range */
            ret = static_cast<T>(number);
            break;
        }

        case OBJ_FLOAT: {
            const double number = OBJPTR(FLOAT, obj_id)->value;
            /* TODO check range */
            ret = static_cast<T>(number);
            break;
        }

        default:
            yarn.raise_and_signal_error("source type is not a number");
            break;
    }

    return ret;
}

template<>
int value_from_object_ptr<int>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<int>(yarn, obj_id);
}

template<>
int64_t value_from_object_ptr<int64_t>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<int64_t>(yarn, obj_id);
}

template<>
integer value_from_object_ptr<integer>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if ( ! IS_SMALL_INT(obj_id) && GET_OBJ_TYPE(obj_id) != OBJ_INTEGER)
        yarn.raise_and_signal_error("source type is not an integer");

    return integer(obj_id);
}

template<>
double value_from_object_ptr<double>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<double>(yarn, obj_id);
}

template<>
floating value_from_object_ptr<floating>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_FLOAT)
        yarn.raise_and_signal_error("source type is not a float");

    return floating(obj_id);
}

template<>
bool value_from_object_ptr<bool>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BOOLEAN)
        yarn.raise_and_signal_error("source type is not a boolean");

    return !! KOS_get_bool(obj_id);
}

template<>
boolean value_from_object_ptr<boolean>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BOOLEAN)
        yarn.raise_and_signal_error("source type is not a boolean");

    return boolean(obj_id);
}

template<>
std::string value_from_object_ptr<std::string>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        yarn.raise_and_signal_error("source type is not a string");

    const unsigned len = KOS_string_to_utf8(obj_id, 0, 0);
    std::string    str(static_cast<size_t>(len), '\0');

    KOS_string_to_utf8(obj_id, &str[0], len);
    return str;
}

template<>
string value_from_object_ptr<string>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        yarn.raise_and_signal_error("source type is not a string");

    return string(obj_id);
}

template<>
void_type value_from_object_ptr<void_type>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_VOID)
        yarn.raise_and_signal_error("source type is not a void");

    return void_type(obj_id);
}

template<>
object value_from_object_ptr<object>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_OBJECT)
        yarn.raise_and_signal_error("source type is not an object");

    return object(yarn, obj_id);
}

template<>
array value_from_object_ptr<array>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        yarn.raise_and_signal_error("source type is not an array");

    return array(yarn, obj_id);
}

template<>
buffer value_from_object_ptr<buffer>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        yarn.raise_and_signal_error("source type is not a buffer");

    return buffer(yarn, obj_id);
}

template<>
function value_from_object_ptr<function>(stack_frame yarn, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_FUNCTION)
        yarn.raise_and_signal_error("source type is not a function");

    return function(yarn, obj_id);
}

void stack_frame::raise(const char* desc)
{
    KOS_raise_exception(_yarn, KOS_new_cstring(_yarn, desc));
}

std::string exception::get_exception_string(stack_frame yarn)
{
    KOS_OBJ_ID obj_id = KOS_get_exception(yarn);
    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {

        static const char str_value[] = "value";

        obj_id = KOS_get_property(yarn, obj_id, KOS_instance_get_cstring(yarn, str_value));

        assert( ! IS_BAD_PTR(obj_id));

        obj_id = KOS_object_to_string(yarn, obj_id);

        assert( ! IS_BAD_PTR(obj_id));
    }

    return from_object_ptr(yarn, obj_id);
}

object::const_iterator::const_iterator(stack_frame           yarn,
                                       KOS_OBJ_ID            obj_id,
                                       KOS_OBJECT_WALK_DEPTH depth)
    : _yarn(yarn)
{
    _elem.key   = KOS_BADPTR;
    _elem.value = KOS_BADPTR;
    _walk       = yarn.check_error(KOS_new_object_walk(yarn, obj_id, depth));
    _elem       = KOS_object_walk(_yarn, _walk);
}

object::const_iterator& object::const_iterator::operator=(const const_iterator& it)
{
    _yarn       = it._yarn;
    _walk       = KOS_new_object_walk_copy(_yarn, it._walk);
    _elem.key   = it._elem.key;
    _elem.value = it._elem.value;
    return *this;
}

} // namespace kos

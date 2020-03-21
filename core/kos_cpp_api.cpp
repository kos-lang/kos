/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos.h"
#include "kos_const_strings.h"

namespace kos {

template<typename T>
T numeric_from_object_ptr(context ctx, KOS_OBJ_ID obj_id)
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
            ctx.raise_and_signal_error("source type is not a number");
            break;
    }

    return ret;
}

template<>
int value_from_object_ptr<int>(context ctx, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<int>(ctx, obj_id);
}

template<>
int64_t value_from_object_ptr<int64_t>(context ctx, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<int64_t>(ctx, obj_id);
}

template<>
integer value_from_object_ptr<integer>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if ( ! IS_SMALL_INT(obj_id) && GET_OBJ_TYPE(obj_id) != OBJ_INTEGER)
        ctx.raise_and_signal_error("source type is not an integer");

    return integer(ctx, obj_id);
}

template<>
double value_from_object_ptr<double>(context ctx, KOS_OBJ_ID obj_id)
{
    return numeric_from_object_ptr<double>(ctx, obj_id);
}

template<>
floating value_from_object_ptr<floating>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_FLOAT)
        ctx.raise_and_signal_error("source type is not a float");

    return floating(ctx, obj_id);
}

template<>
bool value_from_object_ptr<bool>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BOOLEAN)
        ctx.raise_and_signal_error("source type is not a boolean");

    return !! KOS_get_bool(obj_id);
}

template<>
boolean value_from_object_ptr<boolean>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BOOLEAN)
        ctx.raise_and_signal_error("source type is not a boolean");

    return boolean(ctx, obj_id);
}

template<>
std::string value_from_object_ptr<std::string>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        ctx.raise_and_signal_error("source type is not a string");

    const unsigned len = KOS_string_to_utf8(obj_id, 0, 0);
    if (len == ~0U)
        ctx.raise_and_signal_error("invalid string");

    std::string str(static_cast<size_t>(len), '\0');

    (void)KOS_string_to_utf8(obj_id, &str[0], len);
    return str;
}

kos::string::operator std::string() const
{
    const unsigned len = KOS_string_to_utf8(*this, 0, 0);
    if (len == ~0U)
        throw std::runtime_error("invalid string");
        //ctx.raise_and_signal_error("invalid string");

    std::string str(static_cast<size_t>(len), '\0');

    (void)KOS_string_to_utf8(*this, &str[0], len);
    return str;
}

template<>
string value_from_object_ptr<string>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        ctx.raise_and_signal_error("source type is not a string");

    return string(ctx, obj_id);
}

template<>
void_type value_from_object_ptr<void_type>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_VOID)
        ctx.raise_and_signal_error("source type is not a void");

    return void_type(ctx, obj_id);
}

template<>
object value_from_object_ptr<object>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_OBJECT)
        ctx.raise_and_signal_error("source type is not an object");

    return object(ctx, obj_id);
}

template<>
array value_from_object_ptr<array>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_ARRAY)
        ctx.raise_and_signal_error("source type is not an array");

    return array(ctx, obj_id);
}

template<>
buffer value_from_object_ptr<buffer>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BUFFER)
        ctx.raise_and_signal_error("source type is not a buffer");

    return buffer(ctx, obj_id);
}

template<>
function value_from_object_ptr<function>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_FUNCTION)
        ctx.raise_and_signal_error("source type is not a function");

    return function(ctx, obj_id);
}

void context::raise(const char* desc)
{
    KOS_raise_exception(ctx_, KOS_new_cstring(ctx_, desc));
}

std::string exception::get_exception_string(context ctx)
{
    KOS_OBJ_ID obj_id = KOS_get_exception(ctx);
    assert( ! IS_BAD_PTR(obj_id));

    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {

        obj_id = KOS_get_property(ctx, obj_id, KOS_STR_VALUE);

        assert( ! IS_BAD_PTR(obj_id));

        obj_id = KOS_object_to_string(ctx, obj_id);

        assert( ! IS_BAD_PTR(obj_id));
    }

    return from_object_ptr(ctx, obj_id);
}

object::const_iterator::const_iterator(context                 ctx,
                                       KOS_OBJ_ID              obj_id,
                                       KOS_OBJECT_WALK_DEPTH_E depth)
    : walk_(ctx, ctx.check_error(KOS_new_object_walk(ctx, obj_id, depth)))
{
    operator++();
}

object::const_iterator::const_iterator(const const_iterator& it)
    : elem_(it.elem_)
{
    context ctx(it.walk_.get_context());
    walk_ = handle(ctx, ctx.check_error(KOS_new_object_walk_copy(ctx, it.walk_)));
}

object::const_iterator& object::const_iterator::operator=(const const_iterator& it)
{
    context ctx(walk_.get_context());
    walk_ = handle(ctx, ctx.check_error(KOS_new_object_walk_copy(ctx, it.walk_)));
    elem_ = it.elem_;
    return *this;
}

object::const_iterator& object::const_iterator::operator++()
{
    context ctx(walk_.get_context());
    if (KOS_object_walk(ctx, walk_))
        elem_ = value_type();
    else
        elem_ = std::make_pair<string, handle>(string(ctx, KOS_get_walk_key(walk_)),
                                               handle(ctx, KOS_get_walk_value(walk_)));
    return *this;
}

} // namespace kos

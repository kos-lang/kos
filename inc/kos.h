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

#ifndef __KOS_H
#define __KOS_H

#include "kos_array.h"
#include "kos_buffer.h"
#include "kos_context.h"
#include "kos_modules_init.h"
#include "kos_object_base.h"
#include "kos_object.h"
#include "kos_string.h"

#ifdef __cplusplus

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace kos {

class array_obj;
class buffer_obj;
class context;
class function_obj;
class object;

template<typename T>
T value_from_object_ptr(context& ctx, KOS_OBJ_PTR objptr)
{
    // The default implementation will cause a compile-time error
    // when this function is not specialized for a particular type.
    return static_cast<typename T::type_not_supported>(objptr);
}

template<>
inline KOS_OBJ_PTR value_from_object_ptr<KOS_OBJ_PTR>(context& ctx, KOS_OBJ_PTR objptr)
{
    return objptr;
}

class objptr_converter
{
    public:
        objptr_converter(context& ctx, KOS_OBJ_PTR objptr) : _ctx(ctx), _objptr(objptr) { }

        template<typename T>
        operator T() const {
            return value_from_object_ptr<T>(_ctx, _objptr);
        }

    private:
        context&    _ctx;
        KOS_OBJ_PTR _objptr;
};

inline objptr_converter from_object_ptr(context& ctx, KOS_OBJ_PTR objptr)
{
    return objptr_converter(ctx, objptr);
}

class context
{
    public:
        context() {
            int error = KOS_context_init(&_ctx);
            if (error)
                throw std::runtime_error("Failed to initialize Kos context");

            error = KOS_modules_init(&_ctx);
            if (error) {
                KOS_context_destroy(&_ctx);
                throw std::runtime_error("Failed to initialize Kos modules");
            }
        }

        ~context() {
            KOS_context_destroy(&_ctx);
        }

        operator KOS_CONTEXT*() {
            return &_ctx;
        }

        // Error handling
        // ==============

        void check_error(int error) {
            if (error)
                signal_error();
        }

        KOS_OBJ_PTR check_error(KOS_OBJ_PTR objptr) {
            if (IS_BAD_PTR(objptr))
                signal_error();
            return objptr;
        }

        void signal_error();

        // Object creation
        // ===============

        object new_object();

        template<typename T>
        object new_object(T* priv);

        array_obj new_array(unsigned length);

        buffer_obj new_buffer(unsigned size);

#ifdef KOS_CPP11
        template<typename... Args>
        array_obj make_array(Args... args);
#endif

        // Invoke Kos function from C++
        // ============================

        KOS_OBJ_PTR call_v(KOS_OBJ_PTR func_obj, KOS_OBJ_PTR args_obj) {
            return check_error(KOS_call_function(&_ctx, func_obj, KOS_VOID, args_obj));
        }

        KOS_OBJ_PTR call(KOS_OBJ_PTR func_obj, KOS_OBJ_PTR this_obj, KOS_OBJ_PTR args_obj) {
            return check_error(KOS_call_function(&_ctx, func_obj, this_obj, args_obj));
        }

        // Register C++ function in Kos
        // ============================

        function_obj new_function(KOS_FUNCTION_HANDLER handler, int min_args);

        // Something like N3601 (automatic template arg deduction) would simplify this interface
        template<typename T, T fun>
        function_obj new_function();

#ifdef KOS_CPP11
        template<typename Ret, typename... Args>
        KOS_OBJ_PTR invoke_native(Ret (*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args);

        template<typename T, typename Ret, typename... Args>
        KOS_OBJ_PTR invoke_native(Ret (T::*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args);

        template<typename T, typename Ret, typename... Args>
        KOS_OBJ_PTR invoke_native(Ret (T::*fun)(Args...) const, KOS_OBJ_PTR this_obj, array_obj args);
#else
        template<typename T>
        KOS_OBJ_PTR invoke_native(T fun, KOS_OBJ_PTR this_obj, array_obj args);
#endif

    private:
        // Non-copyable
        context(const context&);
        context& operator=(const context&);

        KOS_CONTEXT _ctx;
};

class object_base
{
    public:
        object_base(KOS_OBJ_PTR objptr): _objptr(objptr) {
            assert( ! IS_BAD_PTR(objptr));
        }

        operator KOS_OBJ_PTR() const {
            return _objptr;
        }

    protected:
        KOS_OBJ_PTR _objptr;
};

class integer_obj: public object_base
{
    public:
        integer_obj(KOS_OBJ_PTR objptr)
        : object_base(objptr)
        {
            assert(IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) == OBJ_INTEGER);
        }

        integer_obj(int v)
        : object_base(TO_SMALL_INT(v))
        {
        }

        operator int64_t() const {
            if (IS_SMALL_INT(_objptr))
                return GET_SMALL_INT(_objptr);
            else
                return OBJPTR(KOS_INTEGER, _objptr)->number;
        }
};

class float_obj: public object_base
{
    public:
        float_obj(KOS_OBJ_PTR objptr)
        : object_base(objptr)
        {
            assert( ! IS_SMALL_INT(objptr) && GET_OBJ_TYPE(objptr) == OBJ_FLOAT);
        }

        operator double() const {
            return OBJPTR(KOS_FLOAT, _objptr)->number;
        }
};

class string_obj: public object_base
{
    public:
        string_obj(KOS_OBJ_PTR objptr)
        : object_base(objptr)
        {
            assert(IS_STRING_OBJ(objptr));
        }

        string_obj(KOS_STRING& v)
        : object_base(TO_OBJPTR(&v))
        {
        }

        operator std::string() const {
            const unsigned len = KOS_string_to_utf8(_objptr, 0, 0);
            std::string    str(static_cast<size_t>(len), '\0');

            KOS_string_to_utf8(_objptr, &str[0], len);
            return str;
        }
};

class boolean_obj: public object_base
{
    public:
        boolean_obj(KOS_OBJ_PTR objptr)
        : object_base(objptr)
        {
            assert( ! IS_SMALL_INT(objptr) && GET_OBJ_TYPE(objptr) == OBJ_BOOLEAN);
        }

        boolean_obj(bool v)
        : object_base(KOS_BOOL(static_cast<int>(v)))
        {
        }

        operator bool() const {
            return !! KOS_get_bool(_objptr);
        }

        bool operator!() const {
            return ! KOS_get_bool(_objptr);
        }
};

class void_obj: public object_base
{
    public:
        void_obj(KOS_OBJ_PTR objptr)
        : object_base(objptr)
        {
            assert( ! IS_SMALL_INT(objptr) && GET_OBJ_TYPE(objptr) == OBJ_VOID);
        }

        void_obj()
        : object_base(KOS_VOID)
        {
        }
};

class object: public object_base
{
    public:
        object(context& ctx, KOS_OBJ_PTR objptr)
        : object_base(objptr),
          _ctx(&ctx)
        {
        }

        class property
        {
            public:
                property(context* ctx, KOS_OBJ_PTR objptr, const string_obj key)
                : _ctx(ctx),
                  _objptr(objptr),
                  _key(key)
                {
                }

                template<typename T>
                operator T() const {
                    return value_from_object_ptr<T>(*_ctx, _ctx->check_error(KOS_get_property(*_ctx, _objptr, _key)));
                }

                template<typename T>
                property& operator=(const T& obj) {
                    _ctx->check_error(KOS_set_property(*_ctx, _objptr, _key, to_object_ptr(*_ctx, obj)));
                    return *this;
                }

                void erase() {
                    _ctx->check_error(KOS_delete_property(*_ctx, _objptr, _key));
                }

            private:
                context*         _ctx;
                KOS_OBJ_PTR      _objptr;
                const string_obj _key;
        };

        template<typename T>
        const property operator[](const T& key) const {
            return property(_ctx, _objptr, to_object_ptr(*_ctx, key));
        }

        template<typename T>
        property operator[](const T& key) {
            return property(_ctx, _objptr, to_object_ptr(*_ctx, key));
        }

        /* TODO C++ iterators */

    protected:
        context* _ctx;
};

class array_obj: public object
{
    public:
        array_obj(context& ctx, KOS_OBJ_PTR objptr)
        : object(ctx, objptr)
        {
            assert(GET_OBJ_TYPE(objptr) == OBJ_ARRAY);
        }

        void reserve(uint32_t capacity) {
            _ctx->check_error(KOS_array_reserve(*_ctx, _objptr, capacity));
        }

        void resize(uint32_t length) {
            _ctx->check_error(KOS_array_resize(*_ctx, _objptr, length));
        }

        uint32_t size() const {
            return KOS_get_array_size(_objptr);
        }

        class element
        {
            public:
                element(context* ctx, KOS_OBJ_PTR objptr, int idx)
                : _ctx(ctx),
                  _objptr(objptr),
                  _idx(idx)
                {
                }

                template<typename T>
                operator T() const {
                    return value_from_object_ptr<T>(*_ctx, _ctx->check_error(KOS_array_read(*_ctx, _objptr, _idx)));
                }

                template<typename T>
                element& operator=(const T& v) {
                    _ctx->check_error(KOS_array_write(*_ctx, _objptr, _idx, to_object_ptr(*_ctx, v)));
                    return *this;
                }

            private:
                context*    _ctx;
                KOS_OBJ_PTR _objptr;
                int         _idx;
        };

        const element operator[](int idx) const {
            return element(_ctx, _objptr, idx);
        }

        element operator[](int idx) {
            return element(_ctx, _objptr, idx);
        }

        array_obj slice(int64_t begin, int64_t end) const {
            return array_obj(*_ctx, _ctx->check_error(KOS_array_slice(*_ctx, _objptr, begin, end)));
        }

        /* TODO C++ iterators */
};

class buffer_obj: public object
{
    public:
        buffer_obj(context& ctx, KOS_OBJ_PTR objptr)
        : object(ctx, objptr)
        {
            assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);
        }

        void reserve(uint32_t capacity) {
            _ctx->check_error(KOS_buffer_reserve(*_ctx, _objptr, capacity));
        }

        void resize(uint32_t length) {
            _ctx->check_error(KOS_buffer_resize(*_ctx, _objptr, length));
        }

        uint32_t size() const {
            return KOS_get_buffer_size(_objptr);
        }

        class element
        {
            public:
                element(context* ctx, KOS_OBJ_PTR objptr, int idx)
                : _ctx(ctx),
                  _objptr(objptr),
                  _idx(idx)
                {
                }

                operator char() const {
                    const uint32_t size = KOS_get_buffer_size(_objptr);
                    const uint32_t idx = static_cast<uint32_t>(_idx < 0 ? (_idx + static_cast<int>(size)) : _idx);
                    if (idx >= size) {
                        throw std::out_of_range("buffer index out of range");
                    }
                    uint8_t* const buf = KOS_buffer_data(*_ctx, _objptr);
                    assert(buf);
                    return static_cast<char>(buf[idx]);
                }

                element& operator=(char v) {
                    const uint32_t size = KOS_get_buffer_size(_objptr);
                    const uint32_t idx = static_cast<uint32_t>(_idx < 0 ? (_idx + static_cast<int>(size)) : _idx);
                    if (idx >= size) {
                        throw std::out_of_range("buffer index out of range");
                    }
                    uint8_t* const buf = KOS_buffer_data(*_ctx, _objptr);
                    assert(buf);
                    buf[idx] = static_cast<uint8_t>(v);
                    return *this;
                }

            private:
                context*    _ctx;
                KOS_OBJ_PTR _objptr;
                int         _idx;
        };

        const element operator[](int idx) const {
            return element(_ctx, _objptr, idx);
        }

        element operator[](int idx) {
            return element(_ctx, _objptr, idx);
        }

        /* TODO C++ iterators */
};

#ifdef KOS_CPP11
inline void unpack_args(array_obj args_obj, int idx)
{
}

template<typename Arg1, typename... Args>
void unpack_args(array_obj args_obj, int idx, Arg1 arg1, Args... args)
{
    args_obj[idx] = arg1;
    unpack_args(args_obj, idx+1, args...);
}

template<typename... Args>
array_obj context::make_array(Args... args)
{
    array_obj array(new_array(static_cast<unsigned>(sizeof...(args))));
    unpack_args(array, 0, args...);
    return array;
}
#endif

class function_obj: public object
{
    public:
        function_obj(context& ctx, KOS_OBJ_PTR objptr)
        : object(ctx, objptr)
        {
            assert(GET_OBJ_TYPE(objptr) == OBJ_FUNCTION);
        }

        objptr_converter call_v(array_obj args) const {
            return objptr_converter(*_ctx, _ctx->call_v(_objptr, args));
        }

        objptr_converter call(object_base this_obj, array_obj args) const {
            return objptr_converter(*_ctx, _ctx->call(_objptr, this_obj, args));
        }

#ifdef KOS_CPP11
        template<typename... Args>
        objptr_converter operator()(Args... args) const {
            return call_v(_ctx->make_array(args...));
        }

        template<typename... Args>
        objptr_converter invoke(object_base this_obj, Args... args) const {
            return call(this_obj, _ctx->make_array(args...));
        }
#else
        objptr_converter operator()() const {
            return call_v(_ctx->new_array(0));
        }

        template<typename T1>
        objptr_converter operator()(T1 arg1) const {
            array_obj args(_ctx->new_array(1));
            args[0] = arg1;
            return call_v(args);
        }

        template<typename T1, typename T2>
        objptr_converter operator()(T1 arg1, T2 arg2) const {
            array_obj args(_ctx->new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call_v(args);
        }

        template<typename T1, typename T2, typename T3>
        objptr_converter operator()(T1 arg1, T2 arg2, T3 arg3) const {
            array_obj args(_ctx->new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call_v(args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        objptr_converter operator()(T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array_obj args(_ctx->new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call_v(args);
        }

        objptr_converter invoke(object_base this_obj) const {
            return call(this_obj, _ctx->new_array(0));
        }

        template<typename T1>
        objptr_converter invoke(object_base this_obj, T1 arg1) const {
            array_obj args(_ctx->new_array(1));
            args[0] = arg1;
            return call(this_obj, args);
        }

        template<typename T1, typename T2>
        objptr_converter invoke(object_base this_obj, T1 arg1, T2 arg2) const {
            array_obj args(_ctx->new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3>
        objptr_converter invoke(object_base this_obj, T1 arg1, T2 arg2, T3 arg3) const {
            array_obj args(_ctx->new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        objptr_converter invoke(object_base this_obj, T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array_obj args(_ctx->new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call(this_obj, args);
        }
#endif
};

class exception: public std::runtime_error
{
    public:
        explicit exception(context& ctx)
        : std::runtime_error(get_exception_string(ctx)),
          _obj(KOS_get_exception(ctx))
        {
            KOS_clear_exception(ctx);
        }

        operator object_base() const {
            return _obj;
        }

        static std::string get_exception_string(context& ctx);

    private:
        KOS_OBJ_PTR _obj;
};

inline void context::signal_error()
{
    throw exception(*this);
}

inline object context::new_object()
{
    return object(*this, check_error(KOS_new_object(&_ctx)));
}

template<typename T>
object context::new_object(T* priv)
{
    const object obj = new_object();
    KOS_object_set_private(*OBJPTR(KOS_OBJECT, obj), priv);
    return obj;
}

inline array_obj context::new_array(unsigned length)
{
    return array_obj(*this, check_error(KOS_new_array(&_ctx, length)));
}

inline function_obj context::new_function(KOS_FUNCTION_HANDLER handler, int min_args)
{
    return function_obj(*this, check_error(KOS_new_builtin_function(&_ctx, handler, min_args)));
}

template<typename T>
struct remove_reference
{
    typedef T type;
};

template<typename T>
struct remove_reference<T&>
{
    typedef T type;
};

template<int i, typename T>
typename remove_reference<T>::type extract_arg(context& ctx, array_obj& args_obj)
{
    return from_object_ptr(ctx, args_obj[i]);
}

template<typename T>
T* get_priv(KOS_OBJ_PTR obj)
{
    assert( ! IS_SMALL_INT(obj) && GET_OBJ_TYPE(obj) == OBJ_OBJECT);
    return static_cast<T*>(KOS_object_get_private(*OBJPTR(KOS_OBJECT, obj)));
}

#ifdef KOS_CPP11
template<int...>
struct seq
{
};

template<int n, int... indices>
struct idx_seq: idx_seq<n-1, n-1, indices...>
{
};

template<int... indices>
struct idx_seq<0, indices...>
{
    typedef seq<indices...> type;
};

// Prevents compiler warnings about unused args
template<typename T>
void unused(T&)
{
}

template<typename Ret, typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    return to_object_ptr(ctx, fun(extract_arg<indices, Args>(ctx, args)...));
}

template<typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    fun(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(Args...) const, KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(Args...) const, KOS_OBJ_PTR this_obj, array_obj args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename Ret, typename... Args>
KOS_OBJ_PTR context::invoke_native(Ret (*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_PTR context::invoke_native(Ret (T::*fun)(Args...), KOS_OBJ_PTR this_obj, array_obj args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_PTR context::invoke_native(Ret (T::*fun)(Args...) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}
#else
template<typename Ret>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(), KOS_OBJ_PTR this_obj, array_obj args)
{
    return to_object_ptr(ctx, fun());
}

template<typename Ret, typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(T1), KOS_OBJ_PTR this_obj, array_obj args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args)));
}

template<typename Ret, typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(T1, T2), KOS_OBJ_PTR this_obj, array_obj args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(T1, T2, T3), KOS_OBJ_PTR this_obj, array_obj args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args),
                                  extract_arg<2, T3>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (*fun)(T1, T2, T3, T4), KOS_OBJ_PTR this_obj, array_obj args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args),
                                  extract_arg<2, T3>(ctx, args),
                                  extract_arg<3, T4>(ctx, args)));
}

inline KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(), KOS_OBJ_PTR this_obj, array_obj args)
{
    fun();
    return KOS_VOID;
}

template<typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(T1), KOS_OBJ_PTR this_obj, array_obj args)
{
    fun(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(T1, T2), KOS_OBJ_PTR this_obj, array_obj args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(T1, T2, T3), KOS_OBJ_PTR this_obj, array_obj args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, void (*fun)(T1, T2, T3, T4), KOS_OBJ_PTR this_obj, array_obj args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args),
        extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2, T3), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2, T3, T4), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args),
                                          extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2, T3), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2, T3, T4), KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)() const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2, T3) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, Ret (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args),
                                          extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)() const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2, T3) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_PTR invoke_internal(context& ctx, void (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_PTR this_obj, array_obj args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T>
KOS_OBJ_PTR context::invoke_native(T fun, KOS_OBJ_PTR this_obj, array_obj args)
{
    return invoke_internal(*this, fun, this_obj, args);
}
#endif

#ifdef KOS_CPP11
template<typename Ret, typename... Args>
constexpr int num_args(Ret (*)(Args...))
{
    return static_cast<int>(sizeof...(Args));
}

template<typename T, typename Ret, typename... Args>
constexpr int num_args(Ret (T::*)(Args...))
{
    return static_cast<int>(sizeof...(Args));
}

template<typename T, typename Ret, typename... Args>
constexpr int num_args(Ret (T::*)(Args...) const)
{
    return static_cast<int>(sizeof...(Args));
}
#else
template<typename Ret>
int num_args(Ret (*)())
{
    return 0;
}

template<typename Ret, typename T1>
int num_args(Ret (*)(T1))
{
    return 1;
}

template<typename Ret, typename T1, typename T2>
int num_args(Ret (*)(T1, T2))
{
    return 2;
}

template<typename Ret, typename T1, typename T2, typename T3>
int num_args(Ret (*)(T1, T2, T3))
{
    return 3;
}

template<typename Ret, typename T1, typename T2, typename T3, typename T4>
int num_args(Ret (*)(T1, T2, T3, T4))
{
    return 4;
}

template<typename T, typename Ret>
int num_args(Ret (T::*)())
{
    return 0;
}

template<typename T, typename Ret, typename T1>
int num_args(Ret (T::*)(T1))
{
    return 1;
}

template<typename T, typename Ret, typename T1, typename T2>
int num_args(Ret (T::*)(T1, T2))
{
    return 2;
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
int num_args(Ret (T::*)(T1, T2, T3))
{
    return 3;
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
int num_args(Ret (T::*)(T1, T2, T3, T4))
{
    return 4;
}

template<typename T, typename Ret>
int num_args(Ret (T::*)() const)
{
    return 0;
}

template<typename T, typename Ret, typename T1>
int num_args(Ret (T::*)(T1) const)
{
    return 1;
}

template<typename T, typename Ret, typename T1, typename T2>
int num_args(Ret (T::*)(T1, T2) const)
{
    return 2;
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
int num_args(Ret (T::*)(T1, T2, T3) const)
{
    return 3;
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
int num_args(Ret (T::*)(T1, T2, T3, T4) const)
{
    return 4;
}
#endif

template<typename T, T fun>
KOS_OBJ_PTR wrapper(KOS_CONTEXT* ctx_ptr, KOS_OBJ_PTR this_obj, KOS_OBJ_PTR args_obj)
{
    context& ctx = *reinterpret_cast<context*>(ctx_ptr);
    array_obj args(ctx, args_obj);
    return ctx.invoke_native(fun, this_obj, args);
}

template<typename T, T fun>
function_obj context::new_function()
{
    return new_function(wrapper<T, fun>, num_args(fun));
}

// value -> object ptr
// ===================

inline object_base to_object_ptr(context& ctx, KOS_OBJ_PTR objptr)
{
    return objptr;
}

inline object_base to_object_ptr(context& ctx, objptr_converter objptr)
{
    return static_cast<KOS_OBJ_PTR>(objptr);
}

inline integer_obj to_object_ptr(context& ctx, int v)
{
    return ctx.check_error(KOS_new_int(ctx, v));
}

inline integer_obj to_object_ptr(context& ctx, unsigned v)
{
    return ctx.check_error(KOS_new_int(ctx, static_cast<int64_t>(v)));
}

inline integer_obj to_object_ptr(context& ctx, int64_t v)
{
    return ctx.check_error(KOS_new_int(ctx, v));
}

inline float_obj to_object_ptr(context& ctx, double v)
{
    return ctx.check_error(KOS_new_float(ctx, v));
}

inline string_obj to_object_ptr(context& ctx, const char* v)
{
    return ctx.check_error(KOS_new_cstring(ctx, v));
}

inline string_obj to_object_ptr(context& ctx, const std::string& v)
{
    return ctx.check_error(KOS_new_string(ctx, v.c_str(), (unsigned)v.length()));
}

inline string_obj to_object_ptr(context& ctx, KOS_STRING& v)
{
    return string_obj(v);
}

inline boolean_obj to_object_ptr(context& ctx, bool v)
{
    return boolean_obj(v);
}

template<typename T>
array_obj to_object_ptr(context& ctx, const std::vector<T>& v)
{
    array_obj array(ctx, ctx.check_error(KOS_new_array(ctx, (unsigned)v.size())));
    ctx.check_error(KOS_array_resize(ctx, array, (uint32_t)v.size()));
    for (size_t i = 0; i < v.size(); i++)
        ctx.check_error(KOS_array_write(ctx, array, static_cast<int>(i), to_object_ptr(ctx, v[i])));
    return array;
}

// object ptr -> value
// ===================

template<>
int value_from_object_ptr<int>(context& ctx, KOS_OBJ_PTR objptr);

template<>
int64_t value_from_object_ptr<int64_t>(context& ctx, KOS_OBJ_PTR objptr);

template<>
double value_from_object_ptr<double>(context& ctx, KOS_OBJ_PTR objptr);

template<>
bool value_from_object_ptr<bool>(context& ctx, KOS_OBJ_PTR objptr);

template<>
std::string value_from_object_ptr<std::string>(context& ctx, KOS_OBJ_PTR objptr);

template<>
string_obj value_from_object_ptr<string_obj>(context& ctx, KOS_OBJ_PTR objptr);

template<>
object value_from_object_ptr<object>(context& ctx, KOS_OBJ_PTR objptr);

template<>
array_obj value_from_object_ptr<array_obj>(context& ctx, KOS_OBJ_PTR objptr);

template<>
function_obj value_from_object_ptr<function_obj>(context& ctx, KOS_OBJ_PTR objptr);

} // namespace kos

#ifdef KOS_CPP11
#define NEW_FUNCTION(fun) new_function<decltype(fun), (fun)>()
#endif

#endif /* __cplusplus */

#endif

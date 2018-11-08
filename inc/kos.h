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

#ifndef __KOS_H
#define __KOS_H

#include "kos_array.h"
#include "kos_buffer.h"
#include "kos_instance.h"
#include "kos_module.h"
#include "kos_modules_init.h"
#include "kos_object_base.h"
#include "kos_object.h"
#include "kos_string.h"
#include "kos_utils.h"
#include "kos_version.h"

#ifdef __cplusplus

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef KOS_CPP11
#   define NOEXCEPT noexcept
#elif defined(_MSC_VER)
#   define NOEXCEPT __declspec(nothrow)
#else
#   define NOEXCEPT throw()
#endif

namespace kos {

class array;
class buffer;
class function;
class object;

template<typename T>
struct remove_reference {
    typedef T type;
};

template<typename T>
struct remove_reference<T&> {
    typedef T type;
};

template<typename T>
struct remove_const {
    typedef T type;
};

template<typename T>
struct remove_const<const T> {
    typedef T type;
};

struct void_ {
};

/* TODO rename this to context */
class stack_frame {
    public:
        stack_frame(KOS_CONTEXT ctx)
            : _ctx(ctx) { }

        operator KOS_CONTEXT() const {
            return _ctx;
        }

        // Error handling
        // ==============

        void check_error(int error) {
            if (error)
                signal_error();
        }

        KOS_OBJ_ID check_error(KOS_OBJ_ID obj_id) {
            if (IS_BAD_PTR(obj_id))
                signal_error();
            return obj_id;
        }

        void signal_error();

        void raise(const char* desc);

        void raise_and_signal_error(const char* desc) {
            raise(desc);
            signal_error();
        }

        // Object creation
        // ===============

        object new_object();

        template<typename T>
        object new_object(T* priv);

        array new_array(unsigned size);

        buffer new_buffer(unsigned size);

#ifdef KOS_CPP11
        template<typename... Args>
        array make_array(Args... args);
#endif

        // Globals
        // =======

        void add_global(KOS_OBJ_ID module, KOS_OBJ_ID name, KOS_OBJ_ID value, unsigned* idx = 0) {
            check_error(KOS_module_add_global(_ctx, module, name, value, idx));
        }

        KOS_OBJ_ID get_global(KOS_OBJ_ID module, KOS_OBJ_ID name, unsigned* idx = 0) {
            KOS_OBJ_ID ret = KOS_BADPTR;
            check_error(KOS_module_get_global(_ctx, module, name, &ret, idx));
            return ret;
        }

        // Invoke Kos function from C++
        // ============================

        KOS_OBJ_ID call(KOS_OBJ_ID func_obj, KOS_OBJ_ID args_obj) {
            return check_error(KOS_call_function(_ctx, func_obj, KOS_VOID, args_obj));
        }

        KOS_OBJ_ID call(KOS_OBJ_ID func_obj, KOS_OBJ_ID args_obj, KOS_OBJ_ID this_obj) {
            return check_error(KOS_call_function(_ctx, func_obj, this_obj, args_obj));
        }

        // Register C++ function in Kos
        // ============================

        function new_function(KOS_FUNCTION_HANDLER handler, int min_args);

        // Something like N3601 (automatic template arg deduction) would simplify this interface
        template<typename T, T fun>
        function new_function();

#ifdef KOS_CPP11
        template<typename Ret, typename... Args>
        KOS_OBJ_ID invoke_native(Ret (*fun)(Args...), KOS_OBJ_ID this_obj, array args);

        template<typename T, typename Ret, typename... Args>
        KOS_OBJ_ID invoke_native(Ret (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args);

        template<typename T, typename Ret, typename... Args>
        KOS_OBJ_ID invoke_native(Ret (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args);
#else
        template<typename T>
        KOS_OBJ_ID invoke_native(T fun, KOS_OBJ_ID this_obj, array args);
#endif

    private:
        KOS_CONTEXT _ctx;
};

template<typename T>
T value_from_object_ptr(stack_frame ctx, KOS_OBJ_ID obj_id)
{
    // The default implementation will cause a compile-time error
    // when this function is not specialized for a particular type.
    return static_cast<typename T::type_not_supported>(obj_id);
}

template<>
inline KOS_OBJ_ID value_from_object_ptr<KOS_OBJ_ID>(stack_frame ctx, KOS_OBJ_ID obj_id)
{
    return obj_id;
}

class obj_id_converter {
    public:
        obj_id_converter(stack_frame ctx, KOS_OBJ_ID obj_id)
            : _ctx(ctx), _obj_id(obj_id) { }

        template<typename T>
        operator T() const {
            return value_from_object_ptr<T>(_ctx, _obj_id);
        }

    private:
        stack_frame _ctx;
        KOS_OBJ_ID  _obj_id;
};

inline obj_id_converter from_object_ptr(stack_frame ctx, KOS_OBJ_ID obj_id)
{
    return obj_id_converter(ctx, obj_id);
}

class instance {
    public:
        instance() {
            KOS_CONTEXT ctx;

            int error = KOS_instance_init(&_inst, &ctx);
            if (error)
                throw std::runtime_error("failed to initialize Kos instance");

            error = KOS_modules_init(ctx);
            if (error) {
                KOS_instance_destroy(&_inst);
                throw std::runtime_error("failed to initialize Kos modules");
            }
        }

        ~instance() {
            KOS_instance_destroy(&_inst);
        }

        operator KOS_INSTANCE*() {
            return &_inst;
        }

        operator KOS_CONTEXT() {
            return &_inst.threads.main_thread;
        }

    private:
        // Non-copyable
        instance(const instance&);
        instance& operator=(const instance&);

        KOS_INSTANCE _inst;
};

class thread_ctx {
    public:
        explicit thread_ctx(instance& inst) {
            KOS_instance_register_thread(inst, &_thread_ctx);
        }

        ~thread_ctx() {
            KOS_instance_unregister_thread(_thread_ctx.inst, &_thread_ctx);
        }

        operator KOS_CONTEXT() {
            return &_thread_ctx;
        }

    private:
        struct _KOS_THREAD_CONTEXT _thread_ctx;
};

class object_base {
    public:
        object_base(KOS_OBJ_ID obj_id)
            : _obj_id(obj_id)
        {
            assert( ! IS_BAD_PTR(obj_id));
        }

        operator KOS_OBJ_ID() const {
            return _obj_id;
        }

        KOS_TYPE type() const {
            return GET_OBJ_TYPE(_obj_id);
        }

    protected:
        KOS_OBJ_ID _obj_id;
};

class integer: public object_base {
    public:
        integer(KOS_OBJ_ID obj_id)
            : object_base(obj_id)
        {
            assert(IS_SMALL_INT(obj_id) || READ_OBJ_TYPE(obj_id) == OBJ_INTEGER);
        }

        integer(int v)
        : object_base(TO_SMALL_INT(v))
        {
        }

        operator int64_t() const {
            if (IS_SMALL_INT(_obj_id))
                return GET_SMALL_INT(_obj_id);
            else
                return OBJPTR(INTEGER, _obj_id)->value;
        }
};

class floating: public object_base {
    public:
        floating(KOS_OBJ_ID obj_id)
            : object_base(obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_FLOAT);
        }

        operator double() const {
            return OBJPTR(FLOAT, _obj_id)->value;
        }
};

class string: public object_base {
    public:
        string(KOS_OBJ_ID obj_id)
            : object_base(obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
        }

        string(KOS_STRING& v)
            : object_base(OBJID(STRING, &v))
        {
        }

        operator std::string() const {
            const unsigned len = KOS_string_to_utf8(_obj_id, 0, 0);
            std::string    str(static_cast<size_t>(len), '\0');

            KOS_string_to_utf8(_obj_id, &str[0], len);
            return str;
        }
};

class boolean: public object_base {
    public:
        boolean(KOS_OBJ_ID obj_id)
            : object_base(obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_BOOLEAN);
        }

        operator bool() const {
            return !! KOS_get_bool(_obj_id);
        }

        bool operator!() const {
            return ! KOS_get_bool(_obj_id);
        }
};

class void_type: public object_base {
    public:
        void_type(KOS_OBJ_ID obj_id)
            : object_base(obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_VOID);
        }
};

class object: public object_base {
    public:
        object(stack_frame ctx, KOS_OBJ_ID obj_id)
            : object_base(obj_id),
              _ctx(ctx)
        {
        }

        class const_property {
            public:
                const_property(stack_frame ctx, KOS_OBJ_ID obj_id, const string& key)
                    : _ctx(ctx),
                      _obj_id(obj_id),
                      _key(key)
                {
                }

                template<typename T>
                operator T() const {
                    return value_from_object_ptr<T>(_ctx, _ctx.check_error(KOS_get_property(_ctx, _obj_id, _key)));
                }

                void erase() {
                    _ctx.check_error(KOS_delete_property(_ctx, _obj_id, _key));
                }

            protected:
                mutable stack_frame _ctx;
                KOS_OBJ_ID          _obj_id;
                const string        _key;
        };

        class property: public const_property {
            public:
                property(stack_frame ctx, KOS_OBJ_ID obj_id, const string& key)
                    : const_property(ctx, obj_id, key)
                {
                }

                template<typename T>
                property& operator=(const T& obj) {
                    _ctx.check_error(KOS_set_property(_ctx, _obj_id, _key, to_object_ptr(_ctx, obj)));
                    return *this;
                }
        };

        template<typename T>
        const_property operator[](const T& key) const {
            return const_property(_ctx, _obj_id, to_object_ptr(_ctx, key));
        }

        template<typename T>
        property operator[](const T& key) {
            return property(_ctx, _obj_id, to_object_ptr(_ctx, key));
        }

        class const_iterator {
            public:
                typedef std::forward_iterator_tag         iterator_category;
                typedef std::pair<KOS_OBJ_ID, KOS_OBJ_ID> value_type;
                typedef void                              difference_type;
                typedef const value_type*                 pointer;
                typedef const value_type&                 reference;

                const_iterator()
                    : _ctx(0), _walk(KOS_BADPTR), _elem(KOS_BADPTR, KOS_BADPTR)
                {
                }

                const_iterator(stack_frame ctx, KOS_OBJ_ID obj_id, KOS_OBJECT_WALK_DEPTH depth);

                const_iterator(const const_iterator& it)
                    : _ctx(0), _walk(KOS_BADPTR), _elem(KOS_BADPTR, KOS_BADPTR)
                {
                    *this = it;
                }

                const_iterator& operator=(const const_iterator& it);

                bool operator==(const const_iterator& it) const {
                    return _elem.first == it._elem.first;
                }

                bool operator!=(const const_iterator& it) const {
                    return ! (_elem.first == it._elem.first);
                }

                const_iterator& operator++() {
                    if (KOS_object_walk(_ctx, _walk))
                        _elem = value_type(KOS_BADPTR, KOS_BADPTR);
                    else
                        _elem = value_type(KOS_get_walk_key(_walk), KOS_get_walk_value(_walk));
                    return *this;
                }

                const_iterator operator++(int) {
                    const_iterator tmp(*this);
                    operator++();
                    return tmp;
                }

                reference operator*() const {
                    return _elem;
                }

                pointer operator->() const {
                    return &_elem;
                }

            private:
                mutable stack_frame  _ctx;
                KOS_OBJ_ID           _walk;
                value_type           _elem;
        };

        const_iterator        begin()  const { return const_iterator(_ctx, _obj_id, KOS_SHALLOW); }
        static const_iterator end()          { return const_iterator(); }
        const_iterator        cbegin() const { return const_iterator(_ctx, _obj_id, KOS_SHALLOW); }
        static const_iterator cend()         { return const_iterator(); }

    protected:
        mutable stack_frame _ctx;
};

template<typename element_type>
class random_access_iterator {
    public:
        typedef std::random_access_iterator_tag           iterator_category;
        typedef typename remove_const<element_type>::type value_type;
        typedef int                                       difference_type;
        typedef element_type*                             pointer;   // TODO pointer to actual element
        typedef element_type&                             reference; // TODO reference to actual element

        random_access_iterator()
            : _elem(0, KOS_BADPTR, 0)
        {
        }

        random_access_iterator(stack_frame ctx, KOS_OBJ_ID obj_id, int idx)
            : _elem(ctx, obj_id, idx)
        {
        }

        template<typename other_element_type>
        random_access_iterator(const random_access_iterator<other_element_type>& it)
            : _elem(*it)
        {
        }

        operator random_access_iterator<const value_type>() const {
            return random_access_iterator<const value_type>(_elem.ctx(), _elem.object(), _elem.index());
        }

        random_access_iterator& operator++() {
            ++_elem;
            return *this;
        }

        random_access_iterator& operator--() {
            --_elem;
            return *this;
        }

        random_access_iterator operator++(int) {
            random_access_iterator tmp(_elem.ctx(), _elem.object(), _elem.index());
            operator++();
            return tmp;
        }

        random_access_iterator operator--(int) {
            random_access_iterator tmp(_elem.ctx(), _elem.object(), _elem.index());
            operator--();
            return tmp;
        }

        random_access_iterator operator+(int delta) const {
            return random_access_iterator(_elem.ctx(), _elem.object(), _elem.index() + delta);
        }

        random_access_iterator operator-(int delta) const {
            return random_access_iterator(_elem.ctx(), _elem.object(), _elem.index() - delta);
        }

        random_access_iterator& operator+=(int delta) {
            _elem += delta;
            return *this;
        }

        random_access_iterator& operator-=(int delta) {
            _elem -= delta;
            return *this;
        }

        difference_type operator-(const random_access_iterator& it) const {
            return _elem.index() - it._elem.index();
        }

        bool operator==(const random_access_iterator& it) const {
            return _elem.object() == it._elem.object() &&
                   _elem.index()  == it._elem.index();
        }

        bool operator!=(const random_access_iterator& it) const {
            return _elem.object() != it._elem.object() ||
                   _elem.index()  != it._elem.index();
        }

        bool operator<(const random_access_iterator& it) const {
            return _elem.object() == it._elem.object() &&
                   _elem.index()  <  it._elem.index();
        }

        bool operator>(const random_access_iterator& it) const {
            return _elem.object() == it._elem.object() &&
                   _elem.index()  >  it._elem.index();
        }

        bool operator<=(const random_access_iterator& it) const {
            return _elem.object() == it._elem.object() &&
                   _elem.index()  <= it._elem.index();
        }

        bool operator>=(const random_access_iterator& it) const {
            return _elem.object() == it._elem.object() &&
                   _elem.index()  >= it._elem.index();
        }

        // TODO reference to actual element
        reference operator*() const {
            assert(static_cast<KOS_CONTEXT>(_elem.ctx()));
            return _elem;
        }

    private:
        mutable value_type _elem;
};

class array: public object {
    public:
        array(stack_frame ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
        }

        void reserve(uint32_t capacity) {
            _ctx.check_error(KOS_array_reserve(_ctx, _obj_id, capacity));
        }

        void resize(uint32_t length) {
            _ctx.check_error(KOS_array_resize(_ctx, _obj_id, length));
        }

        uint32_t size() const {
            return KOS_get_array_size(_obj_id);
        }

        class const_element {
            public:
                const_element(stack_frame ctx, KOS_OBJ_ID obj_id, int idx)
                    : _ctx(ctx),
                      _obj_id(obj_id),
                      _idx(idx)
                {
                }

                template<typename T>
                operator T() const {
                    return value_from_object_ptr<T>(_ctx, _ctx.check_error(KOS_array_read(_ctx, _obj_id, _idx)));
                }

                stack_frame ctx() const {
                    return _ctx;
                }

                KOS_OBJ_ID object() const {
                    return _obj_id;
                }

                int index() const {
                    return _idx;
                }

                const_element& operator++() {
                    ++_idx;
                    return *this;
                }

                const_element& operator--() {
                    --_idx;
                    return *this;
                }

                const_element& operator+=(int delta) {
                    _idx += delta;
                    return *this;
                }

                const_element& operator-=(int delta) {
                    _idx -= delta;
                    return *this;
                }

            protected:
                mutable stack_frame _ctx;
                KOS_OBJ_ID          _obj_id;
                int                 _idx;

#ifdef KOS_CPP11
            public:
                const_element() = delete;
#else
            private:
                const_element();
#endif
        };

        class element: public const_element {
            public:
                element(stack_frame ctx, KOS_OBJ_ID obj_id, int idx)
                    : const_element(ctx, obj_id, idx)
                {
                }

                template<typename T>
                element& operator=(const T& v) {
                    _ctx.check_error(KOS_array_write(_ctx, _obj_id, _idx, to_object_ptr(_ctx, v)));
                    return *this;
                }

                element& operator++() {
                    ++_idx;
                    return *this;
                }

                element& operator--() {
                    --_idx;
                    return *this;
                }

                element& operator+=(int delta) {
                    _idx += delta;
                    return *this;
                }

                element& operator-=(int delta) {
                    _idx -= delta;
                    return *this;
                }
        };

        const_element operator[](int idx) const {
            return const_element(_ctx, _obj_id, idx);
        }

        element operator[](int idx) {
            return element(_ctx, _obj_id, idx);
        }

        array slice(int64_t begin, int64_t end) const {
            return array(_ctx, _ctx.check_error(KOS_array_slice(_ctx, _obj_id, begin, end)));
        }

        typedef random_access_iterator<element>       iterator;
        typedef random_access_iterator<const_element> const_iterator;

        iterator       begin()        { return iterator(_ctx, _obj_id, 0); }
        iterator       end()          { return iterator(_ctx, _obj_id, static_cast<int>(size())); }
        const_iterator begin()  const { return cbegin(); }
        const_iterator end()    const { return cend(); }
        const_iterator cbegin() const { return const_iterator(_ctx, _obj_id, 0); }
        const_iterator cend()   const { return const_iterator(_ctx, _obj_id, static_cast<int>(size())); }

        typedef std::reverse_iterator<iterator>       reverse_iterator;
        typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

        reverse_iterator       rbegin()        { return reverse_iterator(end()); }
        reverse_iterator       rend()          { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin()  const { return const_reverse_iterator(cend()); }
        const_reverse_iterator rend()    const { return const_reverse_iterator(cbegin()); }
        const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
        const_reverse_iterator crend()   const { return const_reverse_iterator(cbegin()); }
};

class buffer: public object {
    public:
        buffer(stack_frame ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
        }

        void reserve(uint32_t capacity) {
            _ctx.check_error(KOS_buffer_reserve(_ctx, _obj_id, capacity));
        }

        void resize(uint32_t length) {
            _ctx.check_error(KOS_buffer_resize(_ctx, _obj_id, length));
        }

        uint32_t size() const {
            return KOS_get_buffer_size(_obj_id);
        }

        class const_element {
            public:
                const_element(stack_frame ctx, KOS_OBJ_ID obj_id, int idx)
                    : _ctx(ctx),
                      _obj_id(obj_id),
                      _idx(idx)
                {
                }

                operator char() const {
                    const uint32_t size = KOS_get_buffer_size(_obj_id);
                    const uint32_t idx = static_cast<uint32_t>(_idx < 0 ? (_idx + static_cast<int>(size)) : _idx);
                    if (idx >= size)
                        throw std::out_of_range("buffer index out of range");
                    uint8_t* const buf = KOS_buffer_data(_obj_id);
                    assert(buf);
                    return static_cast<char>(buf[idx]);
                }

                stack_frame ctx() const {
                    return _ctx;
                }

                KOS_OBJ_ID object() const {
                    return _obj_id;
                }

                int index() const {
                    return _idx;
                }

                const_element& operator++() {
                    ++_idx;
                    return *this;
                }

                const_element& operator--() {
                    --_idx;
                    return *this;
                }

                const_element& operator+=(int delta) {
                    _idx += delta;
                    return *this;
                }

                const_element& operator-=(int delta) {
                    _idx -= delta;
                    return *this;
                }

            protected:
                stack_frame _ctx;
                KOS_OBJ_ID  _obj_id;
                int         _idx;

#ifdef KOS_CPP11
            public:
                const_element() = delete;
#else
            private:
                const_element();
#endif
        };

        class element: public const_element {
            public:
                element(stack_frame ctx, KOS_OBJ_ID obj_id, int idx)
                    : const_element(ctx, obj_id, idx)
                {
                }

                element& operator=(char v) {
                    const uint32_t size = KOS_get_buffer_size(_obj_id);
                    const uint32_t idx = static_cast<uint32_t>(_idx < 0 ? (_idx + static_cast<int>(size)) : _idx);
                    if (idx >= size)
                        throw std::out_of_range("buffer index out of range");
                    uint8_t* const buf = KOS_buffer_data(_obj_id);
                    assert(buf);
                    buf[idx] = static_cast<uint8_t>(v);
                    return *this;
                }

                element& operator++() {
                    ++_idx;
                    return *this;
                }

                element& operator--() {
                    --_idx;
                    return *this;
                }

                element& operator+=(int delta) {
                    _idx += delta;
                    return *this;
                }

                element& operator-=(int delta) {
                    _idx -= delta;
                    return *this;
                }
        };

        const_element operator[](int idx) const {
            return const_element(_ctx, _obj_id, idx);
        }

        element operator[](int idx) {
            return element(_ctx, _obj_id, idx);
        }

        typedef random_access_iterator<element>       iterator;
        typedef random_access_iterator<const_element> const_iterator;

        iterator       begin()        { return iterator(_ctx, _obj_id, 0); }
        iterator       end()          { return iterator(_ctx, _obj_id, static_cast<int>(size())); }
        const_iterator begin()  const { return cbegin(); }
        const_iterator end()    const { return cend(); }
        const_iterator cbegin() const { return const_iterator(_ctx, _obj_id, 0); }
        const_iterator cend()   const { return const_iterator(_ctx, _obj_id, static_cast<int>(size())); }

        typedef std::reverse_iterator<iterator>       reverse_iterator;
        typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

        reverse_iterator       rbegin()        { return reverse_iterator(end()); }
        reverse_iterator       rend()          { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin()  const { return const_reverse_iterator(cend()); }
        const_reverse_iterator rend()    const { return const_reverse_iterator(cbegin()); }
        const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
        const_reverse_iterator crend()   const { return const_reverse_iterator(cbegin()); }
};

#ifdef KOS_CPP11
inline void unpack_args(array args_obj, int idx)
{
}

template<typename Arg1, typename... Args>
void unpack_args(array args_obj, int idx, Arg1 arg1, Args... args)
{
    args_obj[idx] = arg1;
    unpack_args(args_obj, idx+1, args...);
}

template<typename... Args>
array stack_frame::make_array(Args... args)
{
    array array(new_array(static_cast<unsigned>(sizeof...(args))));
    unpack_args(array, 0, args...);
    return array;
}
#endif

class function: public object {
    public:
        function(stack_frame ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION);
        }

        obj_id_converter call(array args) const {
            return obj_id_converter(_ctx, _ctx.call(_obj_id, args));
        }

        obj_id_converter call(object_base this_obj, array args) const {
            return obj_id_converter(_ctx, _ctx.call(_obj_id, args, this_obj));
        }

#ifdef KOS_CPP11
        template<typename... Args>
        obj_id_converter operator()(Args... args) const {
            return call(_ctx.make_array(args...));
        }

        template<typename... Args>
        obj_id_converter apply(object_base this_obj, Args... args) const {
            return call(this_obj, _ctx.make_array(args...));
        }
#else
        obj_id_converter operator()() const {
            return call(_ctx.new_array(0));
        }

        template<typename T1>
        obj_id_converter operator()(T1 arg1) const {
            array args(_ctx.new_array(1));
            args[0] = arg1;
            return call(args);
        }

        template<typename T1, typename T2>
        obj_id_converter operator()(T1 arg1, T2 arg2) const {
            array args(_ctx.new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call(args);
        }

        template<typename T1, typename T2, typename T3>
        obj_id_converter operator()(T1 arg1, T2 arg2, T3 arg3) const {
            array args(_ctx.new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call(args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        obj_id_converter operator()(T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array args(_ctx.new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call(args);
        }

        obj_id_converter apply(object_base this_obj) const {
            return call(this_obj, _ctx.new_array(0));
        }

        template<typename T1>
        obj_id_converter apply(object_base this_obj, T1 arg1) const {
            array args(_ctx.new_array(1));
            args[0] = arg1;
            return call(this_obj, args);
        }

        template<typename T1, typename T2>
        obj_id_converter apply(object_base this_obj, T1 arg1, T2 arg2) const {
            array args(_ctx.new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3>
        obj_id_converter apply(object_base this_obj, T1 arg1, T2 arg2, T3 arg3) const {
            array args(_ctx.new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        obj_id_converter apply(object_base this_obj, T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array args(_ctx.new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call(this_obj, args);
        }
#endif
};

class exception: public std::runtime_error {
    public:
        explicit exception(stack_frame ctx)
            : std::runtime_error(get_exception_string(ctx)),
              _obj(KOS_get_exception(ctx))
        {
            KOS_clear_exception(ctx);
        }

        operator object_base() const {
            return _obj;
        }

        static std::string get_exception_string(stack_frame ctx);

    private:
        KOS_OBJ_ID _obj;
};

inline void stack_frame::signal_error()
{
    throw exception(*this);
}

inline object stack_frame::new_object()
{
    return object(*this, check_error(KOS_new_object(_ctx)));
}

template<typename T>
object stack_frame::new_object(T* priv)
{
    const object obj = new_object();
    KOS_object_set_private(*OBJPTR(OBJECT, obj), priv);
    return obj;
}

inline array stack_frame::new_array(unsigned size)
{
    return array(*this, check_error(KOS_new_array(_ctx, size)));
}

inline buffer stack_frame::new_buffer(unsigned size)
{
    return buffer(*this, check_error(KOS_new_buffer(_ctx, size)));
}

inline function stack_frame::new_function(KOS_FUNCTION_HANDLER handler, int min_args)
{
    return function(*this, check_error(KOS_new_builtin_function(_ctx, handler, min_args)));
}

template<int i, typename T>
typename remove_reference<T>::type extract_arg(stack_frame ctx, array& args_obj)
{
    return from_object_ptr(ctx, args_obj[i]);
}

template<typename T>
T* get_priv(KOS_OBJ_ID obj)
{
    assert(GET_OBJ_TYPE(obj) == OBJ_OBJECT);
    return static_cast<T*>(KOS_object_get_private(*OBJPTR(OBJECT, obj)));
}

#ifdef KOS_CPP11
template<int...>
struct seq {
};

template<int n, int... indices>
struct idx_seq: idx_seq<n-1, n-1, indices...> {
};

template<int... indices>
struct idx_seq<0, indices...> {
    typedef seq<indices...> type;
};

// Prevents compiler warnings about unused args
template<typename T>
void unused(T&)
{
}

template<typename Ret, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    return to_object_ptr(ctx, fun(extract_arg<indices, Args>(ctx, args)...));
}

template<typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    fun(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename Ret, typename... Args>
KOS_OBJ_ID stack_frame::invoke_native(Ret (*fun)(Args...), KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_ID stack_frame::invoke_native(Ret (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_ID stack_frame::invoke_native(Ret (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}
#else
template<typename Ret>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun());
}

template<typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args)));
}

template<typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                   extract_arg<1, T2>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                   extract_arg<1, T2>(ctx, args),
                                   extract_arg<2, T3>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                   extract_arg<1, T2>(ctx, args),
                                   extract_arg<2, T3>(ctx, args),
                                   extract_arg<3, T4>(ctx, args)));
}

inline KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(), KOS_OBJ_ID this_obj, array args)
{
    fun();
    return KOS_VOID;
}

template<typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args),
        extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args),
                                           extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args),
                                           extract_arg<2, T3>(ctx, args),
                                           extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)() const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2, T3) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args),
                                           extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, Ret (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                           extract_arg<1, T2>(ctx, args),
                                           extract_arg<2, T3>(ctx, args),
                                           extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)() const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2, T3) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(stack_frame ctx, void (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = get_priv<T>(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T>
KOS_OBJ_ID stack_frame::invoke_native(T fun, KOS_OBJ_ID this_obj, array args)
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
KOS_OBJ_ID wrapper(KOS_CONTEXT frame_ptr, KOS_OBJ_ID this_obj, KOS_OBJ_ID args_obj) NOEXCEPT
{
    stack_frame ctx = frame_ptr;
    try {
        array args(ctx, args_obj);
        return ctx.invoke_native(fun, this_obj, args);
    }
    catch (exception&) {
        assert(KOS_is_exception_pending(frame_ptr));
    }
    catch (std::exception& e) {
        ctx.raise(e.what());
    }
    catch (...) {
        ctx.raise("native exception");
    }
    return KOS_BADPTR;
}

template<typename T, T fun>
function stack_frame::new_function()
{
    return new_function(wrapper<T, fun>, num_args(fun));
}

// value -> object ptr
// ===================

inline object_base to_object_ptr(stack_frame ctx, KOS_OBJ_ID obj_id)
{
    return obj_id;
}

inline object_base to_object_ptr(stack_frame ctx, obj_id_converter obj_id)
{
    return static_cast<KOS_OBJ_ID>(obj_id);
}

inline integer to_object_ptr(stack_frame ctx, int v)
{
    return ctx.check_error(KOS_new_int(ctx, v));
}

inline integer to_object_ptr(stack_frame ctx, unsigned v)
{
    return ctx.check_error(KOS_new_int(ctx, static_cast<int64_t>(v)));
}

inline integer to_object_ptr(stack_frame ctx, int64_t v)
{
    return ctx.check_error(KOS_new_int(ctx, v));
}

inline floating to_object_ptr(stack_frame ctx, double v)
{
    return ctx.check_error(KOS_new_float(ctx, v));
}

inline string to_object_ptr(stack_frame ctx, const char* v)
{
    return ctx.check_error(KOS_new_cstring(ctx, v));
}

inline string to_object_ptr(stack_frame ctx, const std::string& v)
{
    return ctx.check_error(KOS_new_string(ctx, v.c_str(), static_cast<unsigned>(v.length())));
}

inline string to_object_ptr(stack_frame ctx, KOS_STRING& v)
{
    return string(v);
}

inline boolean to_object_ptr(stack_frame ctx, bool v)
{
    return boolean(KOS_BOOL(v));
}

inline void_type to_object_ptr(stack_frame ctx, void_)
{
    return void_type(KOS_VOID);
}

template<typename T>
array to_object_ptr(stack_frame ctx, const std::vector<T>& v)
{
    array array(ctx, ctx.check_error(KOS_new_array(ctx, static_cast<unsigned>(v.size()))));
    ctx.check_error(KOS_array_resize(ctx, array, static_cast<uint32_t>(v.size())));
    for (size_t i = 0; i < v.size(); i++)
        ctx.check_error(KOS_array_write(ctx, array, static_cast<int>(i), to_object_ptr(ctx, v[i])));
    return array;
}

// object ptr -> value
// ===================

template<>
int value_from_object_ptr<int>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
int64_t value_from_object_ptr<int64_t>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
integer value_from_object_ptr<integer>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
double value_from_object_ptr<double>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
floating value_from_object_ptr<floating>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
bool value_from_object_ptr<bool>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
boolean value_from_object_ptr<boolean>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
std::string value_from_object_ptr<std::string>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
string value_from_object_ptr<string>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
void_type value_from_object_ptr<void_type>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
object value_from_object_ptr<object>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
array value_from_object_ptr<array>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
buffer value_from_object_ptr<buffer>(stack_frame ctx, KOS_OBJ_ID obj_id);

template<>
function value_from_object_ptr<function>(stack_frame ctx, KOS_OBJ_ID obj_id);

} // namespace kos

#ifdef KOS_CPP11
#define NEW_FUNCTION(fun) new_function<decltype(fun), (fun)>()
#endif

#endif /* __cplusplus */

#endif

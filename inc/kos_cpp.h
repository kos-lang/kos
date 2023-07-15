/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#ifndef KOS_CPP_H_INCLUDED
#define KOS_CPP_H_INCLUDED

#ifndef __cplusplus
#   error "This file can only be included from C++ sources/headers"
#endif

#include "kos.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#if __cplusplus >= 202002L
#   include <compare>
#endif

// Detect compiler support for exceptions
#if defined(__cpp_exceptions) && __cpp_exceptions
#   define KOS_USE_EXCEPTIONS 1
#elif defined(_MSC_VER) && defined(_CPPUNWIND)
#   define KOS_USE_EXCEPTIONS 1
#elif defined(__GNUC__) && defined(__EXCEPTIONS)
#   define KOS_USE_EXCEPTIONS 1
#else
#   define KOS_USE_EXCEPTIONS 0
#endif

#if !KOS_USE_EXCEPTIONS
#   define NOEXCEPT
#elif defined(KOS_CPP11)
#   define NOEXCEPT noexcept
#elif defined(_MSC_VER)
#   define NOEXCEPT __declspec(nothrow)
#else
#   define NOEXCEPT throw()
#endif

namespace kos {

class handle;
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

class context {
    public:
        context(KOS_CONTEXT ctx)
            : ctx_(ctx) { }

        operator KOS_CONTEXT() const {
            return ctx_;
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

        void raise(const char* desc) {
            const KOS_OBJ_ID err_id = KOS_new_cstring(ctx_, desc);
            if ( ! IS_BAD_PTR(err_id))
                KOS_raise_exception(ctx_, err_id);
        }

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
        array make_array(Args&&... args);
#endif

        // Globals
        // =======

        void add_global(KOS_OBJ_ID module, KOS_OBJ_ID name, KOS_OBJ_ID value, unsigned* idx = KOS_NULL) {
            check_error(KOS_module_add_global(ctx_, module, name, value, idx));
        }

        handle get_global(KOS_OBJ_ID module, KOS_OBJ_ID name, unsigned* idx = KOS_NULL);

        // Invoke Kos function from C++
        // ============================

        KOS_OBJ_ID call(KOS_OBJ_ID func_obj, KOS_OBJ_ID args_obj) {
            return check_error(KOS_call_function(ctx_, func_obj, KOS_VOID, args_obj));
        }

        KOS_OBJ_ID call(KOS_OBJ_ID func_obj, KOS_OBJ_ID args_obj, KOS_OBJ_ID this_obj) {
            return check_error(KOS_call_function(ctx_, func_obj, this_obj, args_obj));
        }

        // Register C++ function in Kos
        // ============================

        function new_function(const char* name, KOS_FUNCTION_HANDLER handler, int min_args);

#if __cplusplus >= 201703L
        template<auto fun>
        function new_function(const char* name);
#endif

        template<typename T, T fun>
        function new_function98(const char* name);

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
        KOS_CONTEXT ctx_;
};

// object ptr -> value
// ===================

template<typename T>
T value_from_object_ptr(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != static_cast<int>(T::id))
        ctx.raise_and_signal_error("invalid type");

    return T(ctx, obj_id);
}

template<>
inline KOS_OBJ_ID value_from_object_ptr<KOS_OBJ_ID>(context ctx, KOS_OBJ_ID obj_id)
{
    return obj_id;
}

template<>
inline std::string value_from_object_ptr<std::string>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_STRING)
        ctx.raise_and_signal_error("source type is not a string");

    const unsigned len = KOS_string_to_utf8(obj_id, KOS_NULL, 0);
    if (len == ~0U)
        ctx.raise_and_signal_error("invalid string");

    std::string str(static_cast<size_t>(len), '\0');

    (void)KOS_string_to_utf8(obj_id, &str[0], len);
    return str;
}

class obj_id_converter {
    public:
        obj_id_converter(context ctx, KOS_OBJ_ID obj_id)
            : ctx_(ctx), obj_id_(obj_id) { }

        template<typename T>
        operator T() const {
            return value_from_object_ptr<T>(ctx_, obj_id_);
        }

    private:
        context    ctx_;
        KOS_OBJ_ID obj_id_;
};

inline obj_id_converter from_object_ptr(context ctx, KOS_OBJ_ID obj_id)
{
    return obj_id_converter(ctx, obj_id);
}

class instance {
    public:
        explicit instance(uint32_t flags = 0) {
            KOS_CONTEXT ctx;

            int error = KOS_instance_init(&inst_, flags, &ctx);
            if (error)
                throw std::runtime_error("failed to initialize Kos instance");

            error = KOS_modules_init(ctx);
            if (error) {
                KOS_instance_destroy(&inst_);
                throw std::runtime_error("failed to initialize Kos modules");
            }
        }

        ~instance() NOEXCEPT {
            KOS_instance_destroy(&inst_);
        }

        operator KOS_INSTANCE*() {
            return &inst_;
        }

        operator KOS_CONTEXT() {
            return &inst_.threads.main_thread;
        }

    private:
        // Non-copyable
        instance(const instance&);
        instance& operator=(const instance&);

        KOS_INSTANCE inst_;
};

class thread_ctx {
    public:
        explicit thread_ctx(instance& inst) {
            KOS_instance_register_thread(inst, &thread_ctx_);
            // TODO throw kos::exception on error
        }

        ~thread_ctx() NOEXCEPT {
            KOS_instance_unregister_thread(thread_ctx_.inst, &thread_ctx_);
        }

        operator KOS_CONTEXT() {
            return &thread_ctx_;
        }

    private:
        struct KOS_THREAD_CONTEXT_S thread_ctx_;
};

class handle {
    public:
        handle() : ctx_(KOS_NULL) {
            local_.next = KOS_NULL;
            local_.prev = KOS_NULL;
            local_.o    = KOS_BADPTR;
        }

        handle(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id) : ctx_(ctx) {
            KOS_init_ulocal(ctx, &local_);
            local_.o = obj_id;
        }

        ~handle() NOEXCEPT {
            KOS_destroy_ulocal(ctx_, &local_);
        }

        handle(const handle& h): ctx_(h.ctx_) {
            KOS_init_ulocal(h.ctx_, &local_);
            local_.o = h.local_.o;
        }

        handle& operator=(const handle& h) {
            if ( ! ctx_ && h.ctx_) {
                KOS_init_ulocal(h.ctx_, &local_);
                ctx_ = h.ctx_;
            }
            assert(ctx_ == h.ctx_ || ! h.ctx_);
            local_.o = h.local_.o;
            return *this;
        }

        operator KOS_OBJ_ID() const {
            return local_.o;
        }

        context get_context() const {
            return ctx_;
        }

        KOS_TYPE type() const {
            return GET_OBJ_TYPE(local_.o);
        }

    private:
        KOS_ULOCAL  local_;
        KOS_CONTEXT ctx_;
};

class object: public handle {
    public:
        enum own_id {
            id = OBJ_OBJECT
        };

#ifdef KOS_CPP11
        object() = default;
#else
        object() { }
#endif

        object(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : handle(ctx, obj_id)
        {
        }

        explicit object(const handle& h)
            : handle(h)
        {
        }

        object(const object& h)
            : handle(h)
        {
        }

        object& operator=(const object& h) {
            handle::operator=(h);
            return *this;
        }

        class const_property;

        class property;

        template<typename T>
        const_property operator[](const T& key) const;

        template<typename T>
        property operator[](const T& key);

        class const_iterator;

        const_iterator        begin()  const;
        static const_iterator end();
        const_iterator        cbegin() const;
        static const_iterator cend();
};

class integer: public object {
    public:
        enum own_id {
            id = OBJ_INTEGER
        };

#ifdef KOS_CPP11
        integer() = default;
#else
        integer() { }
#endif

        integer(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(IS_SMALL_INT(obj_id) || READ_OBJ_TYPE(obj_id) == OBJ_INTEGER);
        }

        integer(KOS_CONTEXT ctx, int value)
            : object(ctx, TO_SMALL_INT(value))
        {
        }

        explicit integer(const handle& h)
            : object(h)
        {
            assert(IS_SMALL_INT(*this) || READ_OBJ_TYPE(*this) == OBJ_INTEGER);
        }

        integer(const integer& h)
            : object(h)
        {
        }

        integer& operator=(const integer& h) {
            object::operator=(h);
            return *this;
        }

        operator int64_t() const {
            const KOS_OBJ_ID obj_id = *this;
            if (IS_SMALL_INT(obj_id))
                return GET_SMALL_INT(obj_id);
            else
                return OBJPTR(INTEGER, obj_id)->value;
        }
};

class floating: public object {
    public:
        enum own_id {
            id = OBJ_FLOAT
        };

#ifdef KOS_CPP11
        floating() = default;
#else
        floating() { }
#endif

        floating(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_FLOAT);
        }

        explicit floating(const handle& h): object(h) {
            assert(GET_OBJ_TYPE(*this) == OBJ_FLOAT);
        }

        floating(const floating& h)
            : object(h)
        {
        }

        floating& operator=(const floating& h) {
            object::operator=(h);
            return *this;
        }

        operator double() const {
            return OBJPTR(FLOAT, *this)->value;
        }
};

class string: public object {
    public:
        enum own_id {
            id = OBJ_STRING
        };

#ifdef KOS_CPP11
        string() = default;
#else
        string() { }
#endif

        string(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);
        }

        explicit string(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_STRING);
        }

        string(const string& h)
            : object(h)
        {
        }

        string& operator=(const string& h) {
            object::operator=(h);
            return *this;
        }

        operator std::string() const {
            const unsigned len = KOS_string_to_utf8(*this, KOS_NULL, 0);
            if (len == ~0U)
                get_context().raise_and_signal_error("invalid string");

            std::string str(static_cast<size_t>(len), '\0');

            (void)KOS_string_to_utf8(*this, &str[0], len);
            return str;
        }

        bool operator==(const string& s) const {
            return KOS_string_compare(*this, s) == 0;
        }

#if __cplusplus >= 202002L
        std::strong_ordering operator<=>(const string& s) const {
            const int ret = KOS_string_compare(*this, s);
            return (ret == 0) ? std::strong_ordering::equal :
                   (ret <  0) ? std::strong_ordering::less  :
                                std::strong_ordering::greater;
        }
#else
        bool operator!=(const string& s) const {
            return KOS_string_compare(*this, s) != 0;
        }

        bool operator<(const string& s) const {
            return KOS_string_compare(*this, s) < 0;
        }

        bool operator<=(const string& s) const {
            return KOS_string_compare(*this, s) <= 0;
        }

        bool operator>(const string& s) const {
            return KOS_string_compare(*this, s) > 0;
        }

        bool operator>=(const string& s) const {
            return KOS_string_compare(*this, s) >= 0;
        }
#endif
};

class boolean: public object {
    public:
        enum own_id {
            id = OBJ_BOOLEAN
        };

#ifdef KOS_CPP11
        boolean() = default;
#else
        boolean() { }
#endif

        boolean(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_BOOLEAN);
        }

        boolean(KOS_CONTEXT ctx, bool b)
            : object(ctx, KOS_BOOL(b))
        {
        }

        explicit boolean(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_BOOLEAN);
        }

        boolean(const boolean& h)
            : object(h)
        {
        }

        boolean& operator=(const boolean& h) {
            object::operator=(h);
            return *this;
        }

        operator bool() const {
            return !! KOS_get_bool(*this);
        }

        bool operator!() const {
            return ! KOS_get_bool(*this);
        }
};

class void_type: public object {
    public:
        enum own_id {
            id = OBJ_VOID
        };

#ifdef KOS_CPP11
        void_type() = default;
#else
        void_type() { }
#endif

        void_type(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_VOID);
        }

        void_type(KOS_CONTEXT ctx, void_)
            : object(ctx, KOS_VOID)
        {
        }

        explicit void_type(KOS_CONTEXT ctx)
            : object(ctx, KOS_VOID)
        {
        }

        explicit void_type(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_VOID);
        }

        void_type(const void_type& h)
            : object(h)
        {
        }

        void_type& operator=(const void_type& h) {
            object::operator=(h);
            return *this;
        }
};

template<typename element_type>
class random_access_iterator {
    public:
        typedef std::random_access_iterator_tag           iterator_category;
        typedef typename remove_const<element_type>::type value_type;
        typedef int                                       difference_type;
        typedef element_type*                             pointer;   // TODO pointer to actual element
        typedef element_type&                             reference; // TODO reference to actual element

#ifdef KOS_CPP11
        random_access_iterator() = default;
#else
        random_access_iterator() { }
#endif

        random_access_iterator(handle obj, int idx)
            : elem_(obj, idx)
        {
        }

        template<typename other_element_type>
        random_access_iterator(const random_access_iterator<other_element_type>& it)
            : elem_(*it)
        {
        }

        operator random_access_iterator<const value_type>() const {
            return random_access_iterator<const value_type>(get_object(), get_index());
        }

        random_access_iterator& operator++() {
            ++elem_;
            return *this;
        }

        random_access_iterator& operator--() {
            --elem_;
            return *this;
        }

        random_access_iterator operator++(int) {
            random_access_iterator tmp(get_object(), get_index());
            operator++();
            return tmp;
        }

        random_access_iterator operator--(int) {
            random_access_iterator tmp(get_object(), get_index());
            operator--();
            return tmp;
        }

        random_access_iterator operator+(int delta) const {
            return random_access_iterator(get_object(), get_index() + delta);
        }

        random_access_iterator operator-(int delta) const {
            return random_access_iterator(get_object(), get_index() - delta);
        }

        random_access_iterator& operator+=(int delta) {
            elem_ += delta;
            return *this;
        }

        random_access_iterator& operator-=(int delta) {
            elem_ -= delta;
            return *this;
        }

        difference_type operator-(const random_access_iterator<element_type>& it) const {
            return get_index() - it.get_index();
        }

        template<typename other_element_type>
        bool operator==(const random_access_iterator<other_element_type>& it) const {
            return get_object() == it.get_object() &&
                   get_index()  == it.get_index();
        }

        template<typename other_element_type>
        bool operator!=(const random_access_iterator<other_element_type>& it) const {
            return get_object() != it.get_object() ||
                   get_index()  != it.get_index();
        }

        template<typename other_element_type>
        bool operator<(const random_access_iterator<other_element_type>& it) const {
            return get_object() == it.get_object() &&
                   get_index()  <  it.get_index();
        }

        template<typename other_element_type>
        bool operator>(const random_access_iterator<other_element_type>& it) const {
            return get_object() == it.get_object() &&
                   get_index()  >  it.get_index();
        }

        template<typename other_element_type>
        bool operator<=(const random_access_iterator<other_element_type>& it) const {
            return get_object() == it.get_object() &&
                   get_index()  <= it.get_index();
        }

        template<typename other_element_type>
        bool operator>=(const random_access_iterator<other_element_type>& it) const {
            return get_object() == it.get_object() &&
                   get_index()  >= it.get_index();
        }

        // TODO reference to actual element
        reference operator*() const {
            assert(static_cast<KOS_CONTEXT>(elem_.get_context()));
            return elem_;
        }

        const handle& get_object() const {
            return elem_.get_object();
        }

        int get_index() const {
            return elem_.get_index();
        }

    private:
        mutable value_type elem_;
};

class array: public object {
    public:
        enum own_id {
            id = OBJ_ARRAY
        };

#ifdef KOS_CPP11
        array() = default;
#else
        array() { }
#endif

        array(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
        }

        explicit array(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_ARRAY);
        }

        array(const array& h)
            : object(h)
        {
        }

        array& operator=(const array& h) {
            object::operator=(h);
            return *this;
        }

        void reserve(uint32_t capacity) {
            context ctx(get_context());
            ctx.check_error(KOS_array_reserve(ctx, *this, capacity));
        }

        void resize(uint32_t length) {
            context ctx(get_context());
            ctx.check_error(KOS_array_resize(ctx, *this, length));
        }

        uint32_t size() const {
            return KOS_get_array_size(*this);
        }

        class const_element {
            public:
                const_element(handle obj, int idx)
                    : elem_(obj),
                      idx_(idx)
                {
                }

                template<typename T>
                operator T() const {
                    context ctx(elem_.get_context());
                    return value_from_object_ptr<T>(ctx, ctx.check_error(KOS_array_read(ctx, elem_, idx_)));
                }

                context get_context() const {
                    return elem_.get_context();
                }

                const handle& get_object() const {
                    return elem_;
                }

                int get_index() const {
                    return idx_;
                }

                const_element& operator++() {
                    ++idx_;
                    return *this;
                }

                const_element& operator--() {
                    --idx_;
                    return *this;
                }

                const_element& operator+=(int delta) {
                    idx_ += delta;
                    return *this;
                }

                const_element& operator-=(int delta) {
                    idx_ -= delta;
                    return *this;
                }

            protected:
                handle elem_;
                int    idx_;

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
                element(handle obj, int idx)
                    : const_element(obj, idx)
                {
                }

                template<typename T>
                element& operator=(const T& v) {
                    context ctx(elem_.get_context());
                    handle value = to_object_ptr(ctx, v);
                    ctx.check_error(KOS_array_write(ctx, elem_, idx_, value));
                    return *this;
                }

                element& operator++() {
                    ++idx_;
                    return *this;
                }

                element& operator--() {
                    --idx_;
                    return *this;
                }

                element& operator+=(int delta) {
                    idx_ += delta;
                    return *this;
                }

                element& operator-=(int delta) {
                    idx_ -= delta;
                    return *this;
                }
        };

        const_element operator[](int idx) const {
            return const_element(*this, idx);
        }

        element operator[](int idx) {
            return element(*this, idx);
        }

        array slice(int64_t begin_idx, int64_t end_idx) const {
            context ctx(get_context());
            return array(ctx, ctx.check_error(KOS_array_slice(ctx, *this, begin_idx, end_idx)));
        }

        typedef random_access_iterator<element>       iterator;
        typedef random_access_iterator<const_element> const_iterator;

        iterator       begin()        { return iterator(*this, 0); }
        iterator       end()          { return iterator(*this, static_cast<int>(size())); }
        const_iterator begin()  const { return cbegin(); }
        const_iterator end()    const { return cend(); }
        const_iterator cbegin() const { return const_iterator(*this, 0); }
        const_iterator cend()   const { return const_iterator(*this, static_cast<int>(size())); }

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
        enum own_id {
            id = OBJ_BUFFER
        };

#ifdef KOS_CPP11
        buffer() = default;
#else
        buffer() { }
#endif

        buffer(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
        }

        explicit buffer(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_BUFFER);
        }

        buffer(const buffer& h)
            : object(h)
        {
        }

        buffer& operator=(const buffer& h) {
            object::operator=(h);
            return *this;
        }

        void reserve(uint32_t capacity) {
            context ctx(get_context());
            ctx.check_error(KOS_buffer_reserve(ctx, *this, capacity));
        }

        void resize(uint32_t length) {
            context ctx(get_context());
            ctx.check_error(KOS_buffer_resize(ctx, *this, length));
        }

        uint32_t size() const {
            return KOS_get_buffer_size(*this);
        }

        class const_element {
            public:
                const_element(handle obj, int idx)
                    : elem_(obj),
                      idx_(idx)
                {
                }

                operator char() const {
                    const uint32_t size = KOS_get_buffer_size(elem_);
                    const uint32_t idx = static_cast<uint32_t>(idx_ < 0 ? (idx_ + static_cast<int>(size)) : idx_);
                    if (idx >= size)
                        throw std::out_of_range("buffer index out of range");
                    const uint8_t* const buf = KOS_buffer_data_const(elem_);
                    assert(buf);
                    return static_cast<char>(buf[idx]);
                }

                context get_context() const {
                    return elem_.get_context();
                }

                const handle& get_object() const {
                    return elem_;
                }

                int get_index() const {
                    return idx_;
                }

                const_element& operator++() {
                    ++idx_;
                    return *this;
                }

                const_element& operator--() {
                    --idx_;
                    return *this;
                }

                const_element& operator+=(int delta) {
                    idx_ += delta;
                    return *this;
                }

                const_element& operator-=(int delta) {
                    idx_ -= delta;
                    return *this;
                }

            protected:
                handle elem_;
                int    idx_;

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
                element(handle obj, int idx)
                    : const_element(obj, idx)
                {
                }

                element& operator=(char v) {
                    const uint32_t size = KOS_get_buffer_size(elem_);
                    const uint32_t idx = static_cast<uint32_t>(idx_ < 0 ? (idx_ + static_cast<int>(size)) : idx_);
                    if (idx >= size)
                        throw std::out_of_range("buffer index out of range");
                    uint8_t* const buf = KOS_buffer_data_volatile(elem_.get_context(), elem_);
                    assert(buf);
                    if ( ! buf)
                        elem_.get_context().signal_error();
                    buf[idx] = static_cast<uint8_t>(v);
                    return *this;
                }

                element& operator++() {
                    ++idx_;
                    return *this;
                }

                element& operator--() {
                    --idx_;
                    return *this;
                }

                element& operator+=(int delta) {
                    idx_ += delta;
                    return *this;
                }

                element& operator-=(int delta) {
                    idx_ -= delta;
                    return *this;
                }
        };

        const_element operator[](int idx) const {
            return const_element(*this, idx);
        }

        element operator[](int idx) {
            return element(*this, idx);
        }

        typedef random_access_iterator<element>       iterator;
        typedef random_access_iterator<const_element> const_iterator;

        iterator       begin()        { return iterator(*this, 0); }
        iterator       end()          { return iterator(*this, static_cast<int>(size())); }
        const_iterator begin()  const { return cbegin(); }
        const_iterator end()    const { return cend(); }
        const_iterator cbegin() const { return const_iterator(*this, 0); }
        const_iterator cend()   const { return const_iterator(*this, static_cast<int>(size())); }

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
inline void unpack_args(array& args_obj, int idx)
{
}

template<typename Arg1, typename... Args>
void unpack_args(array& args_obj, int idx, Arg1 arg1, Args&&... args)
{
    args_obj[idx] = arg1;
    unpack_args(args_obj, idx + 1, std::forward<Args>(args)...);
}

template<typename... Args>
array context::make_array(Args&&... args)
{
    array array = new_array(static_cast<unsigned>(sizeof...(args)));
    unpack_args(array, 0, std::forward<Args>(args)...);
    return array;
}
#endif

class function: public object {
    public:
        enum own_id {
            id = OBJ_FUNCTION
        };

#ifdef KOS_CPP11
        function() = default;
#else
        function() { }
#endif

        function(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
            : object(ctx, obj_id)
        {
            assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION || GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
        }

        explicit function(const handle& h)
            : object(h)
        {
            assert(GET_OBJ_TYPE(*this) == OBJ_FUNCTION || GET_OBJ_TYPE(*this) == OBJ_CLASS);
        }

        function(const function& h)
            : object(h)
        {
        }

        function& operator=(const function& h) {
            object::operator=(h);
            return *this;
        }

        obj_id_converter call(array args) const {
            return obj_id_converter(get_context(), get_context().call(*this, args));
        }

        obj_id_converter call(handle this_obj, array args) const {
            return obj_id_converter(get_context(), get_context().call(*this, args, this_obj));
        }

#ifdef KOS_CPP11
        template<typename... Args>
        obj_id_converter operator()(Args&&... args) const {
            return call(get_context().make_array(std::forward<Args>(args)...));
        }

        template<typename... Args>
        obj_id_converter apply(handle this_obj, Args&&... args) const {
            return call(this_obj, get_context().make_array(std::forward<Args>(args)...));
        }
#else
        obj_id_converter operator()() const {
            return call(get_context().new_array(0));
        }

        template<typename T1>
        obj_id_converter operator()(T1 arg1) const {
            array args(get_context().new_array(1));
            args[0] = arg1;
            return call(args);
        }

        template<typename T1, typename T2>
        obj_id_converter operator()(T1 arg1, T2 arg2) const {
            array args(get_context().new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call(args);
        }

        template<typename T1, typename T2, typename T3>
        obj_id_converter operator()(T1 arg1, T2 arg2, T3 arg3) const {
            array args(get_context().new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call(args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        obj_id_converter operator()(T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array args(get_context().new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call(args);
        }

        obj_id_converter apply(handle this_obj) const {
            return call(this_obj, get_context().new_array(0));
        }

        template<typename T1>
        obj_id_converter apply(handle this_obj, T1 arg1) const {
            array args(get_context().new_array(1));
            args[0] = arg1;
            return call(this_obj, args);
        }

        template<typename T1, typename T2>
        obj_id_converter apply(handle this_obj, T1 arg1, T2 arg2) const {
            array args(get_context().new_array(2));
            args[0] = arg1;
            args[1] = arg2;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3>
        obj_id_converter apply(handle this_obj, T1 arg1, T2 arg2, T3 arg3) const {
            array args(get_context().new_array(3));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            return call(this_obj, args);
        }

        template<typename T1, typename T2, typename T3, typename T4>
        obj_id_converter apply(handle this_obj, T1 arg1, T2 arg2, T3 arg3, T4 arg4) const {
            array args(get_context().new_array(4));
            args[0] = arg1;
            args[1] = arg2;
            args[2] = arg3;
            args[3] = arg4;
            return call(this_obj, args);
        }
#endif
};

class exception: public std::runtime_error, public handle {
    public:
        explicit exception(context ctx)
            : std::runtime_error(get_exception_string(ctx)),
              handle(ctx, KOS_get_exception(ctx))
        {
            KOS_clear_exception(ctx);
        }

        static std::string get_exception_string(context ctx) {
            KOS_OBJ_ID obj_id = KOS_get_exception(ctx);
            assert( ! IS_BAD_PTR(obj_id));

            if (GET_OBJ_TYPE(obj_id) != OBJ_STRING) {

                KOS_DECLARE_STATIC_CONST_STRING(str_value, "value");

                obj_id = KOS_get_property(ctx, obj_id, KOS_CONST_ID(str_value));

                assert( ! IS_BAD_PTR(obj_id));

                obj_id = KOS_object_to_string(ctx, obj_id);

                assert( ! IS_BAD_PTR(obj_id));
            }

            return from_object_ptr(ctx, obj_id);
        }
};

inline void context::signal_error()
{
    assert(KOS_is_exception_pending(*this));
    throw exception(*this);
}

inline handle context::get_global(KOS_OBJ_ID module, KOS_OBJ_ID name, unsigned* idx)
{
    KOS_OBJ_ID ret;
    check_error(KOS_module_get_global(ctx_, module, name, &ret, idx));
    return handle(ctx_, ret);
}

inline object context::new_object()
{
    return object(*this, check_error(KOS_new_object(ctx_)));
}

inline array context::new_array(unsigned size)
{
    return array(*this, check_error(KOS_new_array(ctx_, size)));
}

inline buffer context::new_buffer(unsigned size)
{
    return buffer(*this, check_error(KOS_new_buffer(ctx_, size)));
}

inline function context::new_function(const char* name, KOS_FUNCTION_HANDLER handler, int min_args)
{
    assert((min_args >= 0) && (min_args < 256));
    const KOS_OBJ_ID name_obj = check_error(KOS_new_cstring(ctx_, name));
    function fn(*this, check_error(KOS_new_builtin_function(ctx_, name_obj, handler, KOS_NULL)));
    OBJPTR(FUNCTION, fn)->opts.min_args = (uint8_t)min_args;
    return fn;
}

template<int i, typename T>
typename remove_reference<T>::type extract_arg(context ctx, array& args_obj)
{
    return from_object_ptr(ctx, args_obj[i]);
}

template<typename T>
class private_class {
    public:
        static T* get(KOS_OBJ_ID obj) {
            assert(GET_OBJ_TYPE(obj) == OBJ_OBJECT);
            return static_cast<T*>(KOS_object_get_private(obj, &priv_class));
        }

    private:
        friend class context;
        static const struct KOS_PRIVATE_CLASS_S priv_class;
};

template<typename T>
const struct KOS_PRIVATE_CLASS_S private_class<T>::priv_class = { 0 };

template<typename T>
object context::new_object(T* priv)
{
    // TODO add finalize and prototype
    const object obj = object(*this, check_error(
                KOS_new_object_with_private(ctx_, KOS_VOID, &private_class<T>::priv_class, KOS_NULL)));
    KOS_object_set_private_ptr(obj, priv);
    return obj;
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
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    return to_object_ptr(ctx, fun(extract_arg<indices, Args>(ctx, args)...));
}

template<typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    fun(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename T, typename Ret, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<indices, Args>(ctx, args)...));
}

template<typename T, typename... Args, int... indices>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args, seq<indices...>)
{
    unused(args);
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<indices, Args>(ctx, args)...);
    return KOS_VOID;
}

template<typename Ret, typename... Args>
KOS_OBJ_ID context::invoke_native(Ret (*fun)(Args...), KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_ID context::invoke_native(Ret (T::*fun)(Args...), KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}

template<typename T, typename Ret, typename... Args>
KOS_OBJ_ID context::invoke_native(Ret (T::*fun)(Args...) const, KOS_OBJ_ID this_obj, array args)
{
    unused(args);
    return invoke_internal(*this, fun, this_obj, args, typename idx_seq<sizeof...(Args)>::type());
}
#else
template<typename Ret>
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun());
}

template<typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args)));
}

template<typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args),
                                  extract_arg<2, T3>(ctx, args)));
}

template<typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, Ret (*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    return to_object_ptr(ctx, fun(extract_arg<0, T1>(ctx, args),
                                  extract_arg<1, T2>(ctx, args),
                                  extract_arg<2, T3>(ctx, args),
                                  extract_arg<3, T4>(ctx, args)));
}

inline KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(), KOS_OBJ_ID this_obj, array args)
{
    fun();
    return KOS_VOID;
}

template<typename T1>
KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, void (*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    fun(extract_arg<0, T1>(ctx, args),
        extract_arg<1, T2>(ctx, args),
        extract_arg<2, T3>(ctx, args),
        extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args),
                                          extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2, T3), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2, T3, T4), KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename Ret>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)() const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)());
}

template<typename T, typename Ret, typename T1>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2, T3) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args)));
}

template<typename T, typename Ret, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, Ret (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    return to_object_ptr(ctx, (obj->*fun)(extract_arg<0, T1>(ctx, args),
                                          extract_arg<1, T2>(ctx, args),
                                          extract_arg<2, T3>(ctx, args),
                                          extract_arg<3, T4>(ctx, args)));
}

template<typename T>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)() const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)();
    return KOS_VOID;
}

template<typename T, typename T1>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2, T3) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args));
    return KOS_VOID;
}

template<typename T, typename T1, typename T2, typename T3, typename T4>
KOS_OBJ_ID invoke_internal(context ctx, void (T::*fun)(T1, T2, T3, T4) const, KOS_OBJ_ID this_obj, array args)
{
    T* const obj = private_class<T>::get(this_obj);
    (obj->*fun)(extract_arg<0, T1>(ctx, args),
                extract_arg<1, T2>(ctx, args),
                extract_arg<2, T3>(ctx, args),
                extract_arg<3, T4>(ctx, args));
    return KOS_VOID;
}

template<typename T>
KOS_OBJ_ID context::invoke_native(T fun, KOS_OBJ_ID this_obj, array args)
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
    context ctx = frame_ptr;
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

#if __cplusplus >= 201703L
template<auto fun>
function context::new_function(const char* name)
{
    return new_function(name, wrapper<decltype(fun), fun>, num_args(fun));
}
#endif

template<typename T, T fun>
function context::new_function98(const char* name)
{
    return new_function(name, wrapper<T, fun>, num_args(fun));
}

// object functions
// ================

class object::const_property {
    public:
        const_property(handle obj, const string& key)
            : handle_(obj), key_(key)
        {
        }

        template<typename T>
        operator T() const {
            context ctx(handle_.get_context());
            return value_from_object_ptr<T>(ctx, ctx.check_error(
                        KOS_get_property(ctx, handle_, key_)));
        }

        void erase() {
            context ctx(handle_.get_context());
            ctx.check_error(KOS_delete_property(ctx, handle_, key_));
        }

    protected:
        handle       handle_;
        const string key_;
};

class object::property: public const_property {
    public:
        property(handle obj, const string& key)
            : const_property(obj, key)
        {
        }

        template<typename T>
        property& operator=(const T& obj) {
            context ctx(handle_.get_context());
            handle value = to_object_ptr(ctx, obj);
            ctx.check_error(KOS_set_property(ctx, handle_, key_, value));
            return *this;
        }
};

template<typename T>
inline object::const_property object::operator[](const T& key) const {
    context ctx(get_context());
    return const_property(*this, to_object_ptr(ctx, key));
}

template<typename T>
inline object::property object::operator[](const T& key) {
    context ctx(get_context());
    return property(*this, to_object_ptr(ctx, key));
}

class object::const_iterator {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef std::pair<string, handle> value_type;
        typedef void                      difference_type;
        typedef const value_type*         pointer;
        typedef const value_type&         reference;

#ifdef KOS_CPP11
        const_iterator() = default;
#else
        const_iterator() { }
#endif

        const_iterator(context     ctx,
                       KOS_OBJ_ID  obj_id,
                       KOS_DEPTH_E depth)
            : walk_(ctx, ctx.check_error(KOS_new_iterator(ctx, obj_id, depth)))
        {
            operator++();
        }

        const_iterator(const const_iterator& it)
            : elem_(it.elem_)
        {
            context ctx(it.walk_.get_context());
            walk_ = handle(ctx, ctx.check_error(KOS_new_iterator_copy(ctx, it.walk_)));
        }

        const_iterator& operator=(const const_iterator& it) {
            context ctx(walk_.get_context());
            walk_ = handle(ctx, ctx.check_error(KOS_new_iterator_copy(ctx, it.walk_)));
            elem_ = it.elem_;
            return *this;
        }

        bool operator==(const const_iterator& it) const {
            const KOS_OBJ_ID left  = elem_.first;
            const KOS_OBJ_ID right = it.elem_.first;
            return left == right;
        }

        bool operator!=(const const_iterator& it) const {
            return ! operator==(it);
        }

        const_iterator& operator++() {
            context ctx(walk_.get_context());
            const int error = KOS_iterator_next(ctx, walk_);
            if (error == KOS_ERROR_NOT_FOUND)
                elem_ = value_type();
            else {
                ctx.check_error(error);
                elem_ = std::make_pair<string, handle>(string(ctx, KOS_get_walk_key(walk_)),
                                                       handle(ctx, KOS_get_walk_value(walk_)));
            }
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp(*this);
            operator++();
            return tmp;
        }

        reference operator*() const {
            return elem_;
        }

        pointer operator->() const {
            return &elem_;
        }

    private:
        handle     walk_;
        value_type elem_;
};

inline object::const_iterator object::begin() const
{
    return const_iterator(get_context(), *this, KOS_SHALLOW);
}

inline object::const_iterator object::end()
{
    return const_iterator();
}

inline object::const_iterator object::cbegin() const
{
    return const_iterator(get_context(), *this, KOS_SHALLOW);
}

inline object::const_iterator object::cend()
{
    return const_iterator();
}

// value -> object ptr
// ===================

inline handle to_object_ptr(context ctx, KOS_OBJ_ID obj_id)
{
    return handle(ctx, obj_id);
}

inline handle to_object_ptr(context ctx, obj_id_converter obj_id)
{
    return handle(ctx, static_cast<KOS_OBJ_ID>(obj_id));
}

inline integer to_object_ptr(context ctx, int v)
{
    return integer(ctx, ctx.check_error(KOS_new_int(ctx, v)));
}

inline integer to_object_ptr(context ctx, unsigned v)
{
    return integer(ctx, ctx.check_error(KOS_new_int(ctx, static_cast<int64_t>(v))));
}

inline integer to_object_ptr(context ctx, int64_t v)
{
    return integer(ctx, ctx.check_error(KOS_new_int(ctx, v)));
}

inline floating to_object_ptr(context ctx, double v)
{
    return floating(ctx, ctx.check_error(KOS_new_float(ctx, v)));
}

inline string to_object_ptr(context ctx, const char* v)
{
    return string(ctx, ctx.check_error(KOS_new_cstring(ctx, v)));
}

inline string to_object_ptr(context ctx, const std::string& v)
{
    return string(ctx, ctx.check_error(KOS_new_string(ctx, v.c_str(), static_cast<unsigned>(v.length()))));
}

inline boolean to_object_ptr(context ctx, bool v)
{
    return boolean(ctx, KOS_BOOL(v));
}

inline void_type to_object_ptr(context ctx, void_)
{
    return void_type(ctx, KOS_VOID);
}

template<typename T>
array to_object_ptr(context ctx, const std::vector<T>& v)
{
    array array(ctx, ctx.check_error(KOS_new_array(ctx, static_cast<unsigned>(v.size()))));
    ctx.check_error(KOS_array_resize(ctx, array, static_cast<uint32_t>(v.size())));
    for (size_t i = 0; i < v.size(); i++)
        ctx.check_error(KOS_array_write(ctx, array, static_cast<int>(i), to_object_ptr(ctx, v[i])));
    return array;
}

// object ptr -> value
// ===================

namespace detail {

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
}

template<>
inline int value_from_object_ptr<int>(context ctx, KOS_OBJ_ID obj_id)
{
    return detail::numeric_from_object_ptr<int>(ctx, obj_id);
}

template<>
inline int64_t value_from_object_ptr<int64_t>(context ctx, KOS_OBJ_ID obj_id)
{
    return detail::numeric_from_object_ptr<int64_t>(ctx, obj_id);
}

template<>
inline double value_from_object_ptr<double>(context ctx, KOS_OBJ_ID obj_id)
{
    return detail::numeric_from_object_ptr<double>(ctx, obj_id);
}

template<>
inline bool value_from_object_ptr<bool>(context ctx, KOS_OBJ_ID obj_id)
{
    assert( ! IS_BAD_PTR(obj_id));
    if (GET_OBJ_TYPE(obj_id) != OBJ_BOOLEAN)
        ctx.raise_and_signal_error("source type is not a boolean");

    return !! KOS_get_bool(obj_id);
}

} // namespace kos

#ifdef KOS_CPP11
#   if __cplusplus >= 201703L
#       define NEW_FUNCTION(fun) new_function<(fun)>(#fun)
#   else
#       define NEW_FUNCTION(fun) new_function98<decltype(fun), (fun)>(#fun)
#   endif
#endif

#endif

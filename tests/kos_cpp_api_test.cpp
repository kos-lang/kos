/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos.h"
#include <iostream>

int64_t add_func(bool second, int a, int64_t b)
{
    return second ? b + 2 : static_cast<int64_t>(a) + 1;
}

std::string global_str;

void set_global(const std::string& s)
{
    global_str = s;
}

class test_class {
    public:
        test_class(int a, const char* b) : a_(a), b_(b) { }
        int                get_a() const { return a_; }
        const std::string& get_b() const { return b_; }
        int64_t            add_a(bool b, int v) { if (b) a_ += v; return a_; }

    private:
        int         a_;
        std::string b_;
};

void throw_string(const std::string& str)
{
    if (!str.empty())
        throw std::runtime_error(str);
}

#define TEST(test) do { if ( ! (test)) { std::cout << "Failed: line " << __LINE__ << ": " << #test "\n"; return 1; } } while (0)

static int main_inner(kos::context ctx)
{
    {
        const int a = from_object_ptr(ctx, TO_SMALL_INT(123));
        TEST(a == 123);
    }

    {
        const int64_t a = from_object_ptr(ctx, to_object_ptr(ctx, (int64_t)1 << 62));
        TEST(a == (int64_t)1 << 62);
    }

    {
        const kos::integer a = from_object_ptr(ctx, to_object_ptr(ctx, (int64_t)1 << 62));
        TEST(static_cast<int64_t>(a) == (int64_t)1 << 62);
    }

    {
        const double a = from_object_ptr(ctx, to_object_ptr(ctx, 1.0));
        TEST(a == 1);
    }

    {
        const kos::floating a = from_object_ptr(ctx, to_object_ptr(ctx, 1.5));
        TEST(static_cast<double>(a) == 1.5);
    }

    {
        const kos::boolean a = from_object_ptr(ctx, to_object_ptr(ctx, true));
        TEST(static_cast<bool>(a));
    }

    {
        const kos::buffer a = from_object_ptr(ctx, KOS_new_buffer(ctx, 0));
        TEST(a.type() == OBJ_BUFFER);
    }

    {
        bool exception = false;
        try {
            const double a = from_object_ptr(ctx, to_object_ptr(ctx, "1.0"));
            TEST(a == 2);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a number")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const bool a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(a == true);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a boolean")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const kos::void_type a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(a.type() == OBJ_VOID);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const std::string a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(a == "2");
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a string")
                exception = true;
        }
        TEST(exception);
    }

    {
        const kos::string str = from_object_ptr(ctx, to_object_ptr(ctx, "uv"));
        TEST(std::string(str) == "uv");
    }

    {
        bool exception = false;
        try {
            const kos::string a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(std::string(a) == "2");
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::array a = ctx.new_array(2);
        a[0] = 100;
        a[1] = kos::void_();
        kos::array a2 = from_object_ptr(ctx, a);
        TEST(static_cast<int>(a2[0]) == 100);
    }

    {
        bool exception = false;
        try {
            const kos::array a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(static_cast<int64_t>(a[0]) == 0);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const kos::buffer a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(static_cast<char>(a[0]));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::object o = ctx.new_object();
        o[std::string("a")] = 24;
        kos::object o2 = from_object_ptr(ctx, o);
        TEST(static_cast<int>(o2["a"]) == 24);
    }

    {
        bool exception = false;
        try {
            const kos::object a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(static_cast<bool>(a[""]));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const kos::function a = from_object_ptr(ctx, TO_SMALL_INT(0));
            TEST(static_cast<bool>(a()));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "invalid type")
                exception = true;
        }
        TEST(exception);
    }

    {
        const std::string a = from_object_ptr(ctx, to_object_ptr(ctx, "abc"));
        TEST(a == "abc");
    }

    {
        kos::array a = ctx.new_array(100);
        TEST(a.size() == 100);
        TEST(static_cast<KOS_OBJ_ID>(a[0] ) == KOS_VOID);
        TEST(static_cast<KOS_OBJ_ID>(a[99]) == KOS_VOID);
    }

    {
        kos::array a = ctx.new_array(5);
        for (int i = 0; i < 5; i++) {
            a[i] = i + 10;
        }

        const kos::array& ca = a;

        {
            kos::array::const_iterator it = ca.begin();
            TEST(static_cast<int>(*(it++)) == 10);
            TEST(static_cast<int>(*(it--)) == 11);

            kos::array::const_iterator it3 = it  + 3;
            kos::array::const_iterator it1 = it3 - 2;
            TEST(static_cast<int>(*it1) == 11);
            TEST(static_cast<int>(*it3) == 13);
            TEST(it3 - it1 == 2);

            TEST(static_cast<int>(*(it += 4)) == 14);
            TEST(static_cast<int>(*(it -= 2)) == 12);
        }

        {
            kos::array::iterator it = a.begin();
            TEST(static_cast<int>(*(it++)) == 10);
            TEST(static_cast<int>(*(it--)) == 11);

            kos::array::iterator it3 = it  + 3;
            kos::array::iterator it1 = it3 - 2;
            TEST(static_cast<int>(*it1) == 11);
            TEST(static_cast<int>(*it3) == 13);

            TEST(static_cast<int>(*(it += 4)) == 14);
            TEST(static_cast<int>(*(it -= 2)) == 12);

            kos::array::const_iterator cit = it;
            TEST(static_cast<int>(*cit) == 12);
        }

        int i = 0;
        for (kos::array::const_iterator it = a.begin(); it != a.end(); ++it, ++i) {
            TEST(static_cast<int>(*it) == i + 10);
        }

        i = 0;
        for (kos::array::const_iterator it = ca.begin(); it != ca.end(); ++it, ++i) {
            TEST(static_cast<int>(*it) == i + 10);
        }

        i = 0;
        for (kos::array::iterator it = a.begin(); it != a.end(); ++it, ++i) {
            *it = i + 20;
            TEST(static_cast<int>(*it) == i + 20);
        }

        /* TODO make reference to actual element, otherwise reverse operator references temporary
        i = 4;
        for (kos::array::const_reverse_iterator it = ca.rbegin(); it != ca.rend(); ++it, --i) {
            TEST(static_cast<int>(*it) == i + 20);
        }

        i = 4;
        for (kos::array::reverse_iterator it = a.rbegin(); it != a.rend(); ++it, --i) {
            TEST(static_cast<int>(*it) == i + 20);
        }
        */

#ifdef KOS_CPP11
        i = 0;
        for (const auto& elem : ca) {
            TEST(static_cast<int>(elem) == (i++ + 20));
        }

        i = 0;
        for (auto& elem : a) {
            elem = i + 30;
            TEST(static_cast<int>(elem) == (i + 30));
            ++i;
        }
#endif
    }

    {
        kos::array a = ctx.new_array(2);
        a[0] = "hello";
        a[1] = "world";

        std::string a0 = a[0];
        std::string a1 = a[1];

        TEST(a0 == "hello");
        TEST(a1 == "world");
    }

    {
        kos::buffer b = ctx.new_buffer(10);

        for (int i = 0; i < static_cast<int>(b.size()); i++) {
            b[i] = static_cast<char>(0xF0 + i);
        }

        for (int i = 0; i < static_cast<int>(b.size()); i++) {
            TEST(b[i] == static_cast<char>(0xF0 + i));
        }

        int i = 0;
        for (kos::buffer::const_iterator it = b.begin(); it != b.end(); ++it, ++i) {
            TEST(*it == static_cast<char>(0xF0 + i));
        }

        /* TODO make reference to actual element, otherwise reverse operator references temporary
        i = 9;
        for (kos::buffer::const_reverse_iterator it = b.rbegin(); it != b.rend(); ++it, --i) {
            TEST(*it == static_cast<char>(0xF0 + i));
        }
        */
    }

    {
#ifdef KOS_CPP11
        kos::function add = ctx.NEW_FUNCTION(&add_func);
#else
        kos::function add = ctx.new_function98<int64_t (*)(bool, int, int64_t), add_func>("add_func");
#endif
        /* Test basic, full invocation */
        {
            const int a6 = add(false, 5, 10);
            TEST(a6 == 6);

            const int a12 = add(true, 5, 10);
            TEST(a12 == 12);
        }

        /* Test insufficient number of args */
        {
            bool exception = false;
            try {
                add(true, 5);
            }
            catch (const kos::exception& e) {
                if (std::string(e.what()) == "not enough arguments passed to a function")
                    exception = true;
            }
            TEST(exception);
        }

        /* Set up default args and argument map for testing */
        {
#ifdef KOS_CPP11
            kos::array defaults = ctx.make_array(100, 200);
#else
            kos::array defaults = ctx.new_array(2);
            defaults[0] = 100;
            defaults[1] = 200;
#endif

            kos::object arg_map = ctx.new_object();
            arg_map["second"] = 0;
            arg_map["a"]      = 1;
            arg_map["b"]      = 2;

            OBJPTR(FUNCTION, add)->opts.min_args     = 1;
            OBJPTR(FUNCTION, add)->opts.num_def_args = 2;
            OBJPTR(FUNCTION, add)->defaults          = defaults;
            OBJPTR(FUNCTION, add)->arg_map           = arg_map;
        }

        /* Test default args */
        {
            const int a101 = add(false);
            TEST(a101 == 101);

            const int a202 = add(true);
            TEST(a202 == 202);

            const int a301 = add(false, 300);
            TEST(a301 == 301);

            const int a202_again = add(true, 300);
            TEST(a202_again == 202);
        }

        /* Test named args - all args */
        {
            kos::object args = ctx.new_object();
            args["second"] = true;
            args["a"]      = 10;
            args["b"]      = 20;

            const int a22 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a22 == 22);

            args["second"] = false;

            const int a11 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a11 == 11);
        }

        /* Test named args - one non-default and first default arg */
        {
            kos::object args = ctx.new_object();
            args["second"] = true;
            args["a"]      = 10;

            const int a202 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a202 == 202);

            args["second"] = false;

            const int a11 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a11 == 11);
        }

        /* Test named args - one non-default and second default arg */
        {
            kos::object args = ctx.new_object();
            args["second"] = true;
            args["b"]      = 10;

            const int a12 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a12 == 12);

            args["second"] = false;

            const int a101 = kos::obj_id_converter(ctx, ctx.call(add, args));
            TEST(a101 == 101);
        }

        /* Test named args - missing non-default arg */
        {
            kos::object args = ctx.new_object();
            args["a"]      = 10;
            args["b"]      = 20;

            bool exception = false;
            try {
                ctx.call(add, args);
            }
            catch (const kos::exception& e) {
                if (std::string(e.what()) == "missing function parameter: 'second'")
                    exception = true;
            }
            TEST(exception);
        }

        /* Lookup invalid arg index */
        TEST(IS_BAD_PTR(KOS_get_named_arg(ctx, add, 3)));
        TEST(KOS_is_exception_pending(ctx));
        KOS_clear_exception(ctx);
    }

    {
#ifdef KOS_CPP11
        kos::function set = ctx.NEW_FUNCTION(&set_global);
#else
        kos::function set = ctx.new_function98<void (*)(const std::string&), set_global>("set_global");
#endif
        set("some string");
        TEST(global_str == "some string");
    }

    {
        test_class  myobj(42, "42");
        kos::object o  = ctx.new_object(&myobj);
#ifdef KOS_CPP11
        kos::function fa = ctx.NEW_FUNCTION(&test_class::get_a);
        kos::function fb = ctx.NEW_FUNCTION(&test_class::get_b);
        kos::function fx = ctx.NEW_FUNCTION(&test_class::add_a);
#else
        kos::function fa = ctx.new_function98<int                (test_class::*)() const,    &test_class::get_a>("get_a");
        kos::function fb = ctx.new_function98<const std::string& (test_class::*)() const,    &test_class::get_b>("get_b");
        kos::function fx = ctx.new_function98<int64_t            (test_class::*)(bool, int), &test_class::add_a>("add_a");
#endif
        kos::function fy = from_object_ptr(ctx, fx);

        const int a = fa.apply(o);
        TEST(a == 42);

        const std::string b = fb.apply(o);
        TEST(b == "42");

        int x = fy.apply(o, true, 1);
        TEST(x == 43);
        x = fy.apply(o, false, 1);
        TEST(x == 43);
        x = fy.apply(o, true, 7);
        TEST(x == 50);
    }

    {
        kos::object o = ctx.new_object();
        o["i"] = 1.5;
        o["f"] = int64_t(1) << 32;
        o["s"] = "abc";
#ifdef KOS_CPP11
        o["a"] = ctx.make_array(true, 2, 3, 4);
#endif

        const double i = o["i"];
        TEST(i == 1.5);

        const int64_t f = o["f"];
        TEST(f == int64_t(1) << 32);

        std::string s = o["s"];
        TEST(s == "abc");

#ifdef KOS_CPP11
        kos::array a = o["a"];
        TEST(a.size() == 4);
        const bool a0 = a[0];
        TEST(a0);
        const int a1 = a[1];
        TEST(a1 == 2);
#endif
    }

    {
        kos::object o = ctx.new_object();
        o["1"] = 1;
        o["2"] = 2;
        o["3"] = 3;

        int sum = 0;
        for (kos::object::const_iterator it = o.begin(); it != o.end(); ++it) {
            kos::object::const_iterator a_it = it;
            kos::object::const_iterator b_it = a_it++;
            TEST(a_it != b_it);
            ++b_it;
            TEST(a_it == b_it);

            const std::string key   = kos::string(it->first);
            const int         value = static_cast<int>(kos::integer(it->second));

            sum += value;

            switch (value) {
                case 1:  TEST(key == "1"); break;
                case 2:  TEST(key == "2"); break;
                case 3:  TEST(key == "3"); break;
                default: TEST(false);      break;
            }
        }
        TEST(sum == 6);

#ifdef KOS_CPP11
        sum = 0;
        for (const auto& kv : o) {
            const std::string key   = kos::string(kv.first);
            const int         value = static_cast<int>(kos::integer(kv.second));

            sum += value;

            switch (value) {
                case 1:  TEST(key == "1"); break;
                case 2:  TEST(key == "2"); break;
                case 3:  TEST(key == "3"); break;
                default: TEST(false);      break;
            }
        }
        TEST(sum == 6);
#endif
    }

    {
        bool exception = false;
        try {
            kos::object o = ctx.new_object();
            o["value"] = "hello, world!";
            KOS_raise_exception(ctx, o);
            throw kos::exception(ctx);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "hello, world!")
                exception = true;
        }
        TEST(exception);
    }

    {
#if __cplusplus >= 201703L
        kos::function f = ctx.new_function<throw_string>("throw_string");
#else
        kos::function f = ctx.new_function98<void (*)(const std::string&), throw_string>("throw_string");
#endif

        kos::void_type v = f("");
        TEST(v.type() == OBJ_VOID);
        TEST(v == KOS_VOID);

        bool exception = false;
        try {
            f("stuff");
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "stuff")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::string name = to_object_ptr(ctx, "my_global");

        /* TODO replace this with a new module object */
        KOS_OBJ_ID module = static_cast<KOS_CONTEXT>(ctx)->inst->modules.init_module;

        ctx.add_global(module, name, TO_SMALL_INT(42));

        unsigned idx = ~0U;
        kos::integer value(ctx.get_global(module, name, &idx));

        TEST(idx == 0);
        TEST(value == 42);
    }

    /* Test signal_error() */
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_test, "test");

        KOS_raise_exception(ctx, KOS_CONST_ID(str_test));

        try {
            ctx.signal_error();
        }
        catch (const std::exception& e) {
            TEST(std::string(e.what()) == "test");
        }
    }

    /* Test context ctor */
    {
        KOS_CONTEXT ctx2 = ctx;

        kos::context octx(ctx2);

        const KOS_OBJ_ID str = octx.check_error(KOS_new_cstring(octx, "test"));

        kos::handle hstr(ctx2, str);
        TEST(hstr == str);
    }

    /* Test check_error(obj) */
    {
        kos::array a = ctx.new_array(0);
        bool exception = false;
        try {
            ctx.check_error(KOS_array_read(ctx, a, 0));
        }
        catch (const std::exception&) {
            exception = true;
        }
        TEST(exception);
    }

    return 0;
}

int main()
{
    kos::instance inst;
    kos::context  ctx(inst);

    try {
        return main_inner(ctx);
    }
    catch (const std::exception& e) {
        std::cout << "exception: " << e.what() << "\n";
        return 1;
    }
}

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
        test_class(int a, const char* b) : _a(a), _b(b) { }
        int                get_a() const { return _a; }
        const std::string& get_b() const { return _b; }
        int64_t            add_a(bool b, int v) { if (b) _a += v; return _a; }

    private:
        int         _a;
        std::string _b;
};

void throw_string(const std::string& str)
{
    if (!str.empty())
        throw std::runtime_error(str);
}

#define TEST(test) do { if ( ! (test)) { std::cout << "Failed: line " << __LINE__ << ": " << #test "\n"; return 1; } } while (0)

int main()
try {
    kos::context     ctx;
    kos::stack_frame frame(ctx);

    {
        const int a = from_object_ptr(frame, TO_SMALL_INT(123));
        TEST(a == 123);
    }

    {
        const int64_t a = from_object_ptr(frame, to_object_ptr(frame, (int64_t)1 << 62));
        TEST(a == (int64_t)1 << 62);
    }

    {
        const double a = from_object_ptr(frame, to_object_ptr(frame, 1.0));
        TEST(a == 1);
    }

    {
        bool exception = false;
        try {
            const double a = from_object_ptr(frame, to_object_ptr(frame, "1.0"));
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
            const bool a = from_object_ptr(frame, TO_SMALL_INT(0));
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
            const std::string a = from_object_ptr(frame, TO_SMALL_INT(0));
            TEST(a == "2");
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a string")
                exception = true;
        }
        TEST(exception);
    }

    {
        const kos::string str = from_object_ptr(frame, to_object_ptr(frame, "uv"));
        TEST(std::string(str) == "uv");
    }

    {
        bool exception = false;
        try {
            const kos::string a = from_object_ptr(frame, TO_SMALL_INT(0));
            TEST(std::string(a) == "2");
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a string")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::array a = frame.new_array(2);
        a[0] = 100;
        a[1] = kos::void_();
        kos::array a2 = from_object_ptr(frame, a);
        TEST(static_cast<int>(a2[0]) == 100);
    }

    {
        bool exception = false;
        try {
            const kos::array a = from_object_ptr(frame, TO_SMALL_INT(0));
            TEST(static_cast<bool>(a[0]));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not an array")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::object o = frame.new_object();
        o[std::string("a")] = 24;
        kos::object o2 = from_object_ptr(frame, o);
        TEST(static_cast<int>(o2["a"]) == 24);
    }

    {
        bool exception = false;
        try {
            const kos::object a = from_object_ptr(frame, TO_SMALL_INT(0));
            TEST(static_cast<bool>(a[""]));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not an object")
                exception = true;
        }
        TEST(exception);
    }

    {
        bool exception = false;
        try {
            const kos::function a = from_object_ptr(frame, TO_SMALL_INT(0));
            TEST(static_cast<bool>(a()));
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "source type is not a function")
                exception = true;
        }
        TEST(exception);
    }

    {
        const std::string a = from_object_ptr(frame, to_object_ptr(frame, "abc"));
        TEST(a == "abc");
    }

    {
        kos::array a = frame.new_array(100);
        TEST(a.size() == 100);
        TEST(static_cast<KOS_OBJ_PTR>(a[0] ) == KOS_VOID);
        TEST(static_cast<KOS_OBJ_PTR>(a[99]) == KOS_VOID);
    }

    {
#ifdef KOS_CPP11
        kos::function add = frame.NEW_FUNCTION(&add_func);
#else
        kos::function add = frame.new_function<int64_t (*)(bool, int, int64_t), add_func>();
#endif
        const int a6 = add(false, 5, 10);
        TEST(a6 == 6);

        const int a12 = add(true, 5, 10);
        TEST(a12 == 12);
    }

    {
#ifdef KOS_CPP11
        kos::function set = frame.NEW_FUNCTION(&set_global);
#else
        kos::function set = frame.new_function<void (*)(const std::string&), set_global>();
#endif
        set("some string");
        TEST(global_str == "some string");
    }

    {
        test_class        myobj(42, "42");
        kos::object       o  = frame.new_object(&myobj);
#ifdef KOS_CPP11
        kos::function fa = frame.NEW_FUNCTION(&test_class::get_a);
        kos::function fb = frame.NEW_FUNCTION(&test_class::get_b);
        kos::function fx = frame.NEW_FUNCTION(&test_class::add_a);
#else
        kos::function fa = frame.new_function<int                (test_class::*)() const,    &test_class::get_a>();
        kos::function fb = frame.new_function<const std::string& (test_class::*)() const,    &test_class::get_b>();
        kos::function fx = frame.new_function<int64_t            (test_class::*)(bool, int), &test_class::add_a>();
#endif
        kos::function fy = from_object_ptr(frame, fx);

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
        kos::object o = frame.new_object();
        o["i"] = 1.5;
        o["f"] = int64_t(1) << 32;
        o["s"] = "abc";
#ifdef KOS_CPP11
        o["a"] = frame.make_array(true, 2, 3, 4);
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
        kos::object o = frame.new_object();
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

            const std::string key   = kos::string(it->key);
            const int         value = kos::integer(it->value);

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
            const std::string key   = kos::string(kv.key);
            const int         value = kos::integer(kv.value);

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
            kos::object o = frame.new_object();
            o["value"] = "hello, world!";
            KOS_raise_exception(frame, o);
            throw kos::exception(frame);
        }
        catch (const kos::exception& e) {
            if (std::string(e.what()) == "hello, world!")
                exception = true;
        }
        TEST(exception);
    }

    {
        kos::function f = frame.new_function<void (*)(const std::string&), throw_string>();

        kos::void_ v = f("");
        TEST(v.type() == OBJ_VOID);

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

    return 0;
}
catch (const std::exception& e) {
    std::cout << "exception: " << e.what() << "\n";
    return 1;
}

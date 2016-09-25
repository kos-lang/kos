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

#define TEST(test) do { if ( ! (test)) { std::cout << "Failed: line " << __LINE__ << ": " << #test "\n"; return 1; } } while (0)

int main()
try {
    kos::context ctx;

    {
        const int a = from_object_ptr(ctx, TO_SMALL_INT(123));
        TEST(a == 123);
    }

    {
        const std::string a = from_object_ptr(ctx, to_object_ptr(ctx, "abc"));
        TEST(a == "abc");
    }

    {
        kos::array_obj a = ctx.new_array(100);
        TEST(a.size() == 100);
        TEST(static_cast<KOS_OBJ_PTR>(a[0] ) == KOS_VOID);
        TEST(static_cast<KOS_OBJ_PTR>(a[99]) == KOS_VOID);
    }

    {
#if __cplusplus >= 201103L
        kos::function_obj add = ctx.NEW_FUNCTION(&add_func);
#else
        kos::function_obj add = ctx.new_function<int64_t (*)(bool, int, int64_t), add_func>();
#endif
        const int a6 = add(false, 5, 10);
        TEST(a6 == 6);

        const int a12 = add(true, 5, 10);
        TEST(a12 == 12);
    }

    {
#if __cplusplus >= 201103L
        kos::function_obj set = ctx.NEW_FUNCTION(&set_global);
#else
        kos::function_obj set = ctx.new_function<void (*)(const std::string&), set_global>();
#endif
        set("some string");
        TEST(global_str == "some string");
    }

    {
        test_class        myobj(42, "42");
        kos::object       o  = ctx.new_object(&myobj);
#if __cplusplus >= 201103L
        kos::function_obj fa = ctx.NEW_FUNCTION(&test_class::get_a);
        kos::function_obj fb = ctx.NEW_FUNCTION(&test_class::get_b);
        kos::function_obj fx = ctx.NEW_FUNCTION(&test_class::add_a);
#else
        kos::function_obj fa = ctx.new_function<int                (test_class::*)() const,    &test_class::get_a>();
        kos::function_obj fb = ctx.new_function<const std::string& (test_class::*)() const,    &test_class::get_b>();
        kos::function_obj fx = ctx.new_function<int64_t            (test_class::*)(bool, int), &test_class::add_a>();
#endif

        const int a = fa.invoke(o);
        TEST(a == 42);

        const std::string b = fb.invoke(o);
        TEST(b == "42");

        int x = fx.invoke(o, true, 1);
        TEST(x == 43);
        x = fx.invoke(o, false, 1);
        TEST(x == 43);
        x = fx.invoke(o, true, 7);
        TEST(x == 50);
    }

    {
        kos::object o = ctx.new_object();
        o["i"] = 1.5;
        o["f"] = int64_t(1) << 32;
        o["s"] = "abc";
#if __cplusplus >= 201103L
        o["a"] = ctx.make_array(1, 2, 3, 4);
#endif

        const double i = o["i"];
        TEST(i == 1.5);

        const int64_t f = o["f"];
        TEST(f == int64_t(1) << 32);

        std::string s = o["s"];
        TEST(s == "abc");

#if __cplusplus >= 201103L
        kos::array_obj a = o["a"];
        TEST(a.size() == 4);
#endif
    }

    return 0;
}
catch (const std::exception& e) {
    std::cout << "exception: " << e.what() << "\n";
    return 1;
}

Kos is straightforward
======================

Study the examples below to get up to speed with Kos in no time.

Please refer to the [Tutorial](tutorial.md) if you need more thorough
information.


Comments
--------

    # This is a single-line comment.

    // This is also a single-line comment.

    /* This is a
       multi-line comment. */


Variables, constants and types
------------------------------

This is a variable:

    var a = 0
    a += 1         # Variables can be modified.
    a =  "two"     # Variables can be reassigned.

This is a constant:

    const c = 1    # Cannot be assigned to again.
    c = 2          # BOOM! This will not compile.

These constants are of immutable types (cannot be modified):

    const integer  = 42
    const float    = 42.0
    const boolean  = true
    const string   = "Hello!"
    const _void_   = void    # void is a keyword, cannot name a variable 'void'
    const function = fun { } # Note: immutable, but can set prototype

These are mutable types (can be modified, but not reassigned):

    const object   = { prop1: 1, prop2: 2, "prop 3": 3 }
    const array    = [ 1, 2, "string" ]
    const buffer   = new lang.buffer # Refer to the tutorial for details


Functions
---------

This is the most common way to declare functions.  Please note that
`add_two_numbers` identifier is just a constant of `function` type.

    fun add_two_numbers(a, b)
    {
        return a + b
    }

    lang.print(add_two_numbers(1, 5), "\n") # Prints: 6

And here is a function declared in an alternative way.  Functions are just
objects, like everything else.  Function identifiers are really constants.

    const multiply_two_numbers = fun(a, b) { return a * b }

    lang.print(multiply_two_numbers(3, 4), "\n") # Prints: 12

And this is the third way to declare functions.  The parentheses contain the
expression which is treated like a return statement.

    const subtract_two_numbers = fun(a, b) -> (a - b)

    lang.print(subtract_two_numbers(7, 5), "\n") # Prints: 2


Semicolons are optional
-----------------------

Semicolons can be used to mark ends of statements, but they are optional
in most situations.

Semicolons are only required inside the `for` loop header (see below) and in
rare cases where you want to cram multiple statements into one line.

In cases where there is an ambiguity, Kos will refuse to compile the program
and will exit with an error.  For example, when `-`, `+`, `(` or `[` occurs on
a new line, but could be part of the previous statement:

    fun my_func(b)
    {
        const a = b
        [0]   # Ambiguous: it could be a new array or it could be b[0]
    }

Another example:

    fun my_func2
    {
        const a = my_func
        (fun { /* ... */ })() # Ambiguous: could be an invocation of my_func
    }

The above examples can be fixed by either moving the '[' and '(',
respectively, to the previous line, or by adding a semicolon, depending on
which behavior is required.


Properties and elements
-----------------------

Object properties can be accessed in two ways:

    const obj  = { }
    obj.one    = 1
    obj["two"] = 2
    lang.print("one:", obj["one"], ", two:", obj.two, "\n")
    # Prints: one: 1, two: 2

Everything is an object, so integers, functions, booleans and arrays also have
properties (from their prototype).  Any property can be accessed as a string.
However, properties can only be added or modified in object type.

In addition to that, arrays, strings and buffers have elements, indexable with
a number.  The first element always has index zero.

    const array = [ 'a', 'b', 'c' ]
    lang.print("element 0:", array[0], "\n")
    # Prints: element 0: a

A number can be used to index only arrays, strings and buffers.  Attempting
to index any other type with a number results in an exception.


Control flow
------------

The control flow statements are quite similar to equivalent statements in
many other programming languages.

`if` statement:

    fun compare(a, b)
    {
        if a < b {
            return -1
        }
        else if a > b {
            return 1
        }
        else {
            return 0
        }
    }

The expressions executed conditionally and governed by the if statement must
always be enclosed in curly braces.  The following example shows incorrect
code:

    if a < b
        return -1 # ERROR: missing curly braces

Curly braces are also required in all the other control flow statements below.

`switch` statement:

    fun select_color(num)
    {
        switch num {
            0   { return "black" }
            1   { return "green" }
            2   fallthrough        # the same as 3
            3   { return "blue"  }
            ... { return "white" } # anything else, "default"
        }
    }

`while` loop:

    var a = 42
    while a > 0 {
        lang.print(a, "\n")
        a -= 10
    }
    # Prints: 42 32 22 12 2 (in consecutive lines)

`do`..`while` loop:

    var a = 42
    do {
        lang.print(a, "\n")
        a -= 10
    } while a > 0
    # Prints: 42 32 22 12 2 (in consecutive lines)

`for` loop:

    for var i = 0; i < 10; i += 1 {
        lang.print(i, "\n")
    }
    # Prints consecutive integers from 0 to 9, inclusive

`for`..`in` loop:

    const a = [ 1, 2, 4 ]
    for var number in a {
        lang.print(number, "\n")
    }
    # Prints: 1 2 4 (in consecutive lines)


Modules
-------

By default in a new script no module is imported and the global namespace is
clean.  In order to do something useful, modules must be imported explicitly.

This applies even to the `lang` module, which is conceptually like a standard
library.  In other languages, several built-in symbols are readily available
in every script.  In Kos, the `lang` module has to be imported explicitly.

Each module is executed only once.  For example, if another module imports
`lang`, it is setup once and another module will only reference its symbols,
but will not cause the `lang` module to be reloaded.

    import lang

    lang.print("Hello, World!\n")

You can explicitly import individual symbols, rather than the entire module:

    import lang.print
    import lang.range

    for var i in range(5) {
        print(i, "\n")
    }
    # Prints: 0 1 2 3 4 (in consecutive lines)


Conversion
----------

Interpolated strings make it easy to convert anything to a string:

    import lang.print

    const a = 1
    const b = true
    print("One=\(a), Two=\(a+1), True=\(b)\n")
    # Prints: One=1, Two=2, True=true

Converting between integers, floats and strings:

    import lang.integer
    import lang.float
    import lang.string

    const integer_10 = integer("10")
    const integer_20 = integer(20.0)
    const float_30   = float("30")
    const float_40   = float(40)
    const string_50  = string(50)
    const string_60  = string(60.0)

In the case of the type constructors from the `lang` module, they can also
be used with the `new` operator:

    const integer_70 = new integer("70")
    const float_80   = new float("80")
    const string_90  = new string(90)


Generators
----------

A function containing a `yield` statement is a generator.

    fun odd_numbers(max)
    {
        for var i = 1; i <= max; i += 1 {
            yield i
        }
    }

This function will actually return another function, which can be called
multiple times to obtain subsequent values, for example:

    import lang.print

    const iterator = odd_numbers(7)
    print(iterator(), "\n")  # Prints: 1
    print(iterator(), "\n")  # Prints: 3
    print(iterator(), "\n")  # Prints: 5
    print(iterator(), "\n")  # Prints: 7
    iterator()               # Throws an exception!

After the generator finishes, attempting to call the iterator function
again will throw an exception.

Another way to use a generator is to use the `for`..`in` loop:

    for var value in odd_numbers(7) {
        print(value, "\n")
    }
    # Prints: 1 3 5 7 (in consecutive lines)

There is no exception thrown in this case, the loop automatically detects
the end of the generator.

In fact, the lang.range() function is a generator, too.

The output from the generator can be turned into an array, like so:

    import lang.array

    const odd_1_3_5_7 = array(odd_numbers(7))

    # Equivalent to:
    const equivalent = [ 1, 3, 5, 7 ]


Resource management
-------------------

The `with` statement ensures that the file is closed, even if a `return`
or an exception occurs inside it.

    import file

    with const f = file.open("myfile") {
        const buf = f.read()
        # Do something with buffer buf
    }

Another way to clean up explicitly is to use the `defer` statement:

    {
        const f = file.open("myfile")
        defer {
            f.close() # This will be executed at the end of current scope
        }
        const buf = f.read()
        # ...
    } # File will be closed here

The `defer` statement is executed at the end of scope, even if a `return`
or an exception occurs.


Buffers
-------

This writes 256 bytes with values of 0 through 255 to a newly created file:

    import file
    import lang.buffer
    import lang.range

    const buf = new buffer(256) # Buffer of size 256, filled with 0s

    for var i in range(256) {
        buf[i] = i
    }

    with const f = file.open("newfile", file.create) {
        f.write(buf)
    }

Buffers are conceptually like arrays, but their elements are unsigned integers
from 0 through 255 (bytes).


Assertions
----------

The `assert` statement can be used for runtime checks:

    assert 1 == 1.0 # OK
    assert false    # Throws an exception!


Exceptions
----------

An exception can be thrown and caught, like so:

    import lang.print

    try {
        throw "Hello, exception!\n"
    }
    catch const e {
        print(e.value) # Prints: Hello, exception!
    }


Methods
-------

Objects in Kos can have methods:

    import lang.print

    const counter = {
        count:     0,
        get:       fun -> (this.count),
        increment: fun    { this.count += 1 },
        add:       fun(n) { this.count += n }
    }

    print(counter.get(), "\n")     # Prints: 0
    counter.increment()
    print(counter.get(), "\n")     # Prints: 1
    counter.add(5)
    print(counter.get(), "\n")     # Prints: 6


Constructors and prototypal inheritance
---------------------------------------

    import lang.print

    # Conceptually a constructor
    fun counter(init)
    {
        this.count = init
    }

    counter.prototype.print = fun
    {
        print(this.count, "\n")
    }

    counter.prototype.add = fun(n)
    {
        this.count += n
    }

    const ctr = new counter(5)
    ctr.print()                # Prints: 5
    ctr.add(3)
    ctr.print()                # Prints: 8

Every object of class `counter` has a unique property `count`, but it also
inherits all properties from the `counter.prototype`, which is common for
all objects constructed with `counter`.


Want to learn more?
-------------------

Please refer to the [Tutorial](tutorial.md) to learn more about Kos!

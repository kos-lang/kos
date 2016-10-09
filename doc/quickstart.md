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

    var a = 0;
    a += 1;         # Variables can be modified.

This is a constant:

    const c = 1;    # Cannot be assigned to again.

These constants are of immutable types:

    const integer  = 42;
    const float    = 42.0;
    const boolean  = true;
    const string   = "Hello!";
    const _void_   = void; # void is a keyword, cannot name a variable 'void'

These are mutable types:

    const array    = [ 1, 2, "string" ];
    const object   = { negative_one: -1, positive_one: 1 };
    const function = fun { };
    const buffer   = new lang.buffer; # We will show buffers later


Functions
---------

This is the most common way to declare functions.  Please note that
`add_two_numbers` has the same status as any other variable would have at this
spot.  Functions declared in this manner are not treated in any special way.

    fun add_two_numbers(a, b)
    {
        return a + b;
    } # No semicolon needed here

    lang.print(add_two_numbers(1, 5), "\n"); # Prints: 6

This function is declared in an alternative way.  Functions are just objects,
like everything else.

    const multiply_two_numbers = fun(a, b) { return a * b; };
    # Notice the semicolon denoting end of expression

    lang.print(multiply_two_numbers(3, 4), "\n"); # Prints: 12

This is the third way to declare functions.  The parentheses contain the
expression which is treated like a return statement.

    const subtract_two_numbers = fun(a, b) -> (a - b);

    lang.print(subtract_two_numbers(7, 5), "\n"); # Prints: 2


Control flow
------------

`if` statement:

    fun compare(a, b)
    {
        if a < b {
            return -1;
        }
        else if a > b {
            return 1;
        }
        else {
            return 0;
        }
    }

`switch` statement:

    fun select_color(num)
    {
        switch num {
            0   { return "black"; }
            1   { return "green"; }
            2   fallthrough;        # the same as 3
            3   { return "blue";  }
            ... { return "white"; } # anything else, "default"
        }
    }

`while` loop:

`do`..`while` loop:

`for` loop:

    for var i = 0; i < 10; i += 1 {
        lang.print(i, "\n");
    }
    # Prints consecutive integers from 0 to 9, inclusive

`for`..`in` loop:

    const a = [ 1, 2, 4 ];
    for number in a {
        lang.print(number, "\n");
    }
    # Prints: 1 2 4 (in consecutive lines)

The Kos Programming Language - Tutorial
=======================================

Kos is a dynamically typed scripting language.  It is lightweight and easy to
use.  Some use cases where it is useful include stand-alone scripts executable
as programs and built-in scripting in applications.  Kos also supports full
multithreading, which makes it easy to be used in multithreaded applications.

The reference Kos interpreter is written in C, giving it maximum portability.
It also comes with C++ hooks, making it very easy to embed in C++ programs.


Scripts
-------

A stand-alone script written in Kos typically has the `.kos` extension.  This
also applies to imported Kos modules.

Kos scripts use UTF-8 character encoding, which is a superset of ASCII.

On UNIX-like operating systems (e.g. Linux, OS X), the first line of a script
can start with a hash character, followed by an exclamation mark, followed by
path to the interpreter.  This will allow the shell to automatically recognize
that this is a script and run the correct interpreter for it.  Such runnable
script does not have to have the `.kos` extension.  Typically the first line
of an executable stand-alone Kos script looks like this:

    #!/usr/bin/env kos

Alternatively, a Kos script can be executed by directly invoking the Kos
interpreter, like so:

    kos myscript.kos


Comments
--------

Single line comments in Kos start with `//` or `#` and end at the end of the line.

    # This is a comment.
    // This is also a comment.

Multi-line comments in Kos start with `/*` and end with `*/`.

    /* This is
    one comment. */

Multi-line comments do not support nesting.

    /* This is a /* comment, but */
    this is a syntax error. */


Variables
---------

Variables in Kos are declared using the `var` keyword.

    var a = 1      // This is a variable containing an integer.
    var b = "abc"  // This is a variable containing a string.

The type of the data contained by a variable can change over time.  You can
assign data with a different type to a variable.

    var c = 1      // This variable starts off with an integer.
    c     = "abc"  // But now it has a string.

Variables follow scope.  They can only be accessed in the scope where they were
declared or its inner scopes.

    var d = 1
    repeat {           // Inner scope.
        var e = d + 1
    } while false
    d = e + 1      // ERROR!!! Variable e was not declared in this scope.

When variables are declared, they always must be assigned a value.

    var f          // ERROR!!! Variable must be assigned a value.

The `void` keyword can be used to assign an opaque "void" value when
a variable needs to be declared, but a value for it is not known yet.

    var g = void

Variable names consist of ASCII letters, digits and underscore `_`, but the
first character of a variable name cannot be a digit.  Unicode letters are not
supported in variable names.

The `const` keyword can be used to create a variable, which cannot be assigned
to again.  The `const` does not make a variable immutable though.  Mutablility
is a type trait, so if a variable contains data of a mutable type (e.g. array),
the variable's contents can be modified.

    const h = 300
    h       = 0    // ERROR!!! Cannot assign to a const variable.

    const i = ["a"]
    i[1]    = "b"  // OK, array can be modified.


Types
-----

The following table lists all the types in Kos and their properties.

| Type     | Mutable | Properties | Prototype | Notes                   |
|----------|---------|------------|-----------|-------------------------|
| integer  | no      | no         | number    | 64-bit signed integer   |
| float    | no      | no         | number    | 64-bit floating point   |
| string   | no      | no         | object    | Character string        |
| void     | no      | no         | -         | An "empty" type         |
| boolean  | no      | no         | object    | `true` or `false`       |
| array    | yes     | no         | object    | Regular array           |
| buffer   | yes     | no         | object    | Array of bytes          |
| object   | yes     | yes        | void      | Object with properties  |
| function | no      | no         | object    | Regular function        |
| class    | yes\*   | yes        | function  | Constructor for objects |

Most types in Kos are immutable.  The "class" type is mutable in the sense that
it has properties which can be added/modified/deleted, just like in the "object"
type, however the underlying function cannot be modified.

The `typeof` unary operator can be used to determine the type of data at run
time.  It returns a string which is the literal name of the type, as in the
first column in the above table.

Example use of the `typeof` operator:

    var itype = typeof 42     // "integer"
    var stype = typeof "word" // "string"


Numbers
-------

There are two numeric types in Kos: integer and floating-point.  Integer
numbers have 64-bit signed precision.  Floating-point numbers also have 64-bit
precision, as defined by the IEEE 754 standard.

An integer numeric constant is either a sole `0`, or it is a sequence of
digits, but the first digit in that sequence must not be a `0`.

    var zero             = 0
    var largest_integer  = 9223372036854775807
    var smallest_integer = -9223372036854775808

    var invalid          = 0123 // ERROR!!! Invalid numeric constant.

Integer numbers can be specified in hexadecimal form, with prefix `0x` or `0X`:

    var some_hex = 0x1234
    var max_hex  = 0x7FFFFFFFFFFFFFFF
    var min_hex  = -0x8000000000000000

Integer numbers can also be specified in binary form, with prefix `0b` or `0B`:

    var eleven = 0b1011

A floating-point numeric constant consists of a base followed by a mantissa,
exponent or both.

    var float_zero    = 0.0
    var float_one     = 1.0
    var float_pi      = 3.14159265359
    var one_thousand  = 1e3  // 1*10^3
    var one_hundredth = 1e-2 // 1*10^-2


Operations on numbers
---------------------

The following binary artithmetic operators are available for numeric types:

* `+` add
* `-` subtract
* `*` multiply
* `/` divide
* `%` modulo

When two numbers, to which the above arithmetic operations are applied, are of
the same numeric type, the result is also of that type.  When the two numbers
are of a different type, i.e. one is integer and the other one is
floating-point, the integer is promoted to a floating-point and the result is
also floating-point.

    var two_int    = 1 + 1     // 2   (int)
    var two_float  = 1 + 1.0   // 2.0 (float)

    var one_int    = 2 - 1     // 1   (int)

    var four_float = 2.0 * 2.0 // 4.0 (float)

    var two_int    = 5 / 2     // 2   (int)
    var one_int    = 5 % 2     // 1   (int)

    var twopt5_flt = 5.0 / 2.0 // 2.5 (float)

The promotion from integer to floating-point is lossy.  Very high and very low
integers which require more than 53 bits to represent (excluding sign) loose
precision when converted to a floating-point value.

The following binary bitwise operators are available for integer numbers:

* `&` bitwise and
* `|` bitwise or
* `^` bitwise xor
* `<<` bitwise shift left
* `>>` bitwise shift right (signed)
* `>>>` bitwise shift right (unsigned)

The `-` unary operator can be applied to any number to negate its sign.

The `~` unary operator can be applied to an integer number to flip all bits.

    var minus_one = ~0         // -1, all bits set

When any operand to the binary bitwise operators or to the unary `~` operator
is a floating-point number, it is converted to an integer using round-to-zero
mode.

    var three = 1.1 ^ 2.9

All of the above operators can be paired with assignment operator `=` to
modify the first operand in-place.

    var x =   1
    x     +=  2    // 3
    x     -=  1    // 2
    x     *=  3    // 6
    x     /=  2    // 3
    x     %=  2    // 1
    x     |=  2    // 3
    x     &=  ~1   // 2
    x     ^=  4    // 6
    x     <<= 1    // 12
    x     >>= 2    // 3


Operator precedence
-------------------

Unary operators are always evaluated from right to left, and before any binary
operators.

An expression with all binary arithmetic operators does not need parentheses.
The `*`, `/` and `%` operators have all the same level of precedence, and have
a higher precedence level than `+` and `-`.  The `+` and `-` operators have
the same level of precedence.  Operators of the same precedence level are
evaluated from left to right.

    var seventeen = 1 + 2 * 3 * 4 - 5 - 6 / 2 // equals 17
    // equivalent to 1 + (2*3*4) - 5 - (6/2)

Bitwise operators `&`, `|`, `^`, `<<`, `>>`, `>>>` cannot be mixed with
each other nor with arithmetic operators.  In addition, `&`, `|` and `^`
bitwise operators can be chained, but the shift operators cannot be.

    var nine   = 15 & ~4 & ~2 // equals 9
    var eight  = 2 << 2       // equals 8

    var error1 = 13 & 4 | 2  // ERROR!!! Cannot mix & and |
    var error2 = 1 << 2 << 3 // ERROR!!! Cannot mix shift operators

In cases where multiple types of operators are used, parentheses are necessary.


Comparison operators
--------------------

The `==` and `!=` operators can be used to test object equality and
inequality.

For integers and floating-point numbers, these operators will actually compare
the numbers.  If the compared numbers are of a different numeric type, i.e. one
is integer and one is floating-point, the integer number is promoted to
floating-point before comparison.

If one of the numbers being compared is NaN (floating-point not-a-number), the
`==` and `!=` operators always return `false`.

If one of the compared objects is not of a numeric type, then the operators
check if both objects are of the same type.  If the objects are of a different
type, the `==` operator evaluates to `false` and the `!=` operator evaluates to
`true`.  In other words, two objects of a different type are always not equal
(except for integer vs. floating-point).

If the compared objects are of the same, non-numeric type, then:

* For strings, the strings are actually compared.  The `==` operator returns
  `true` if the strings are the same and `false` otherwise.
* For voids, the `==` operator returns `true`.
* For booleans, the `==` operator returns `true` if the boolean values are the
  same, and `false` otherwise.
* For all other types - arrays, buffers, objects, classes and functions - the `==`
  operator returns `true` if the same object is being compared against itself,
  and `false` otherwise, even if the contents of the objects being compared
  are the same.

Except for number comparison, when one of the numbers is NaN, the `!=`
operator always returns the negated value of the `==` operator.


The `<`, `<=`, `>` and `>=` operators can be used to compare numbers and
strings.  For numbers, if the numbers are of a different type, the integer
number is promoted to floating-point before the comparison.  If one of the
numbers is NaN, every comparison operator returns `false`.  When two strings
are being compared, they are compared according to the rules of the current
locale settings.


Number conversion
-----------------

Floating-point numbers are automatically converted to integers using the
`floor` rounding mode in the following situations:

* Referencing an array, buffer or string element.
* Referencing a range using slice operator.
* Passing a floating-point number to a bitwise operator: `~`, `&`, `|`, `^`,
  `<<`, `>>` or `>>>`.
* Various module functions convert floating-point numbers to integers using
  this method, for example array `resize()`, file `set_file_pos()`, etc.


Boolean conversion
------------------

There are two values of boolean type, denoted by `true` and `false` literals.

The following values are falsy, i.e. they are implicitly treated as `false`
by logical operators and in the `if` statement:

* `false`
* `void`
* integer zero `0`
* floating-point zero `0.0`

Any other values are truthy.


Logical operators
-----------------

TODO: ! && || ?:


Semicolons
----------

TODO


Strings
-------

Strings in Kos are delimited by `"` characters.

Strings can contain any characters, including any Unicode characters.

    var s = "abc"
    var t = "\"def\""  // Double quotes inside a string

Strings are immutable.  The only way to modify a string is to create a new,
modified one.

Individual characters can be extracted from a string using the array operator.
There is also the slicing operator, which allows extracting ranges of
characters.  Negative indexes indicate counting from the end of the string.

    var hello_world = "Hello, World!"
    var h           = hello_world[0]    // "H"
    var bang        = hello_world[-1]   // "!"
    var w           = hello_world[7]    // "W"
    var o           = hello_world[-5]   // "o"
    var world       = hello_world[7:12] // "World"
    var hello       = hello_world[:5]   // "Hello"
    var world_bang  = hello_world[-6:]  // "World!"

Strings can be added together, to concatenate them.

    var hello       = "Hello"
    var world       = "World"
    var hello_world = hello ++ ", " ++ world ++ "!" // "Hello, World!"

Escape sequences can be used to encode characters which are difficult to
obtain otherwise.

    var cr           = "\r"        // "\x0D"
    var lf           = "\n"        // "\x0A"
    var double_quote = "\""
    var m            = "\x6D"      // "m"
    var copyright    = "\xA9"      // Copyright sign
    var pi           = "\x{3C0}"   // Greek lowercase PI letter


Arrays
------

    var empty_array  = []
    var five_numbers = [ 3, 5, 7, 11, 13 ]
    var three_items  = [ "abc", 1, false ]

    var letters = [ "a", "b", "c", "d", "e", "f" ]
    var a       = letters[0]
    var f       = letters[-1]
    var def     = letters[3:]
    var abc     = letters[:3]

    var a    = [ 1, 2, 3, 4, 5 ]
    a.length = 3   // [ 1, 2, 3 ]
    a.length = 5   // [ 1, 2, 3, void, void ]

    var b = [ 1 ]
    v[3]  = 2      // [ 1, void, void, 2 ]


Objects
-------

    const t_circle = 1
    const t_box    = 2

    var circle = {
        name:   "circle",
        type:   t_circle,
        center: { x: 10, y: 8 },
        radius: 3
    }


Functions
---------

    fun sum(x, y)
    {
        return x + y
    }

    var sum = fun(x, y) {
        return x + y
    }

    var sum = (x, y) => x + y


Constructors
------------

    import math

    class Vector
    {
        constructor(x, y)
        {
            this.x = x
            this.y = y
        }

        fun length
        {
            return math.sqrt(this.x * this.x + this.y * this.y)
        }
    }

    var v1     = Vector(3, 4)
    var v1_len = v1.length()       // 5


Built-in type constructors
--------------------------

    import base

    var integer = base.integer("1")    // 1
    var float   = base.float("2")      // 2.0
    var string  = base.string(3)       // "3"
    var boolean = base.boolean(0)      // false
    var _void   = base.void
    var array   = base.array(void, 2)  // [ void, 2 ]
    var buffer  = base.buffer(10)      // buffer of 10 bytes
    var object  = base.object          // { }
    var func    = base.function((x,y)=>x+y)
    var _class  = base.class(class { })


Buffers
-------

Buffers in Kos are useful for efficiently handling and processing sequences and
streams of bytes, such as file contents.

A buffer is in many ways like an array, it works like an array in principle.
However, unlike an array, a buffer only contains integer numbers from 0 through
255.  Only integer numbers can be assigned to the buffer elements
(floating-point numbers are converted to integers using round-to-zero mode).

Attempt to write a value less than 0 or greater than 255 triggers an exception.

To create a new buffer of size 100, filled with zeroes:

    import base
    var buf = base.buffer(100)

To load a file:

    import io
    var buf = io.open("myfile").read()


Exceptions
----------


Generators
----------


Function composition with stream operator
-----------------------------------------


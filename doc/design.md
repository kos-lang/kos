Design of Kos
=============

No surprises
------------

The main goal behind Kos design is to avoid surprises both in the language and
in the library, as much as possible.  Any language or library feature, which is
ambiguous, or can be understood by the programmer in different ways, would lead
to bugs.


Scope
-----

All variables in Kos must be declared before the first use.  A variable
declared in a particular scope is only visible in that scope and its
child scopes.

Scopes provide a natural mechanism for grouping code into sections in imperative
languages.

A variable can only be declared once, therefore there is no risk of mistakenly
redeclaring a variable in a different way.


No undefined variables
----------------------

All variables must be initialized when they are declared.  This guarantees
that no variable is ever uninitialized.

When a variable needs to be declared, but there is no reasonable initial value
for it, `void` can be used as initial value.


Const
-----

Variables can be declared as `const`, which prevents them from being ever
assigned to again.  Constants of mutable types (object, array, buffer)
can still be modified, but cannot be changed to reference another object.


Immutable types
---------------

Integer, float, string, boolean, void and function types are immutable,
meaning that value of a variable of these types cannot be modified.  A variable
holding a value of any of these types can only be assigned a new value.

Their prototypes, however, can still be modified.


Tabs
----

Source code needs to be viewable in many different ways: in a browser, in a
terminal window, etc.  The only way to guarantee correctness of presentation
is to forbid the use of tab characters, which every programmer sets up to
a different width.  With varying tab widths, indentation breaks easily and
the source code becomes unreadable.

For this reason, Kos does not support tab characters.  Any tab character
appearing instead of whitespace is treated as an error.  Tab characters are
still allowed in string literals and comments.


Operator precedence
-------------------

It is impossible to mix certain types of operators in Kos.  For example,
binary arithmetic (such as `+`) and bitwise (such as `&`) operators cannot be
mixed together, they require parentheses to explicitly specify their correct
order in an expression.

Binary bitwise operators: `&`, `|`, `^`, `<<`, `>>` and `>>>` cannot be mixed
in a single expression without parentheses.  The same applies to logical
operators: `&&` and `||`.  Otherwise, precedence of these operators would not
be immediately apparent to most programmers.


One assignment per expression, only in expression statements
------------------------------------------------------------

Assignment operators, such as `=` or `+=`, can only be used in an assignment
expression, which allows only a single assignment and does not produce any
value.  So every assignment must be performed as a separate, distinct
expression.  This prevents mistakenly using `=` instead of `==` and reduces
possibilities of writing unreadable code.


Clear order of expression evaluation
------------------------------------

There are no operators in Kos, which modify a variable in place, such as `++`
and `--`.  The `+=` operator can be used to increment a value.  This reduces
chances of writing unreadable code, such as:

    b = ++a + 2 * ++a; // What is the order of evaluation?


Predictable function invocation
-------------------------------

Function arguments are always evaluated from left to right.  Each argument is
fully evaluated before the next one.

In this example, bar() will be invoked before baz():

    foo(bar(), baz());


Modules
-------

All modules must be imported explicitly for their global variables and
functions to be accessible, including the `lang` module, which conceptually
contains "built-in" functionality.  This way the global scope is not polluted
with unnecessary and potentially unwanted variables.

While variables and functions declared in the global scope of a module can
be read from other modules, they cannot be explcitly assigned to from other
modules.

Any functions or variables declared in any inner (non-global) scope are
internal and invisible to other modules.


Prototypal inheritance and object hierarchy
-------------------------------------------

Everything in Kos is an object.  This applies to all built-in types.

Every object has a prototype.  If a property is read from an object,
but it does not exist in the object itself, it is then read from the
prototype.

A prototype is an object itself, and can further have its own prototype.

Here is how to obtain access to prototypes of individual types:

    import lang;

    const integer_prototype  = lang.integer.prototype;
    const float_prototype    = lang.float.prototype;
    const boolean_prototype  = lang.boolean.prototype;
    const void_prototype     = lang.void.prototype;
    const string_prototype   = lang.string.prototype;
    const array_prototype    = lang.array.prototype;
    const buffer_prototype   = lang.buffer.prototype;
    const function_prototype = lang.function.prototype;
    const object_prototype   = lang.object.prototype;

Most of the above objects have `lang.object.prototype` as their prototype,
except integer and float prototype, which have number prototype as their
common prototype:

    const number_prototype   = lang.number.prototype;


Iterators
---------

The `for`..`in` loop in Kos invokes `iterator()` function on an object in
order to retrieve the generator for the object's contents, then performs
iteration until the generator is exhausted.

Objects of various types provide their own `iterator()` function:

* Integer, float, boolean, void: their `iterator()` function generates the
  object's own value.

* String, array, buffer: their `iterator()` function generates all of their
  elements.

* Function's `iterator()` returns the function itself, so that a generator's
  return value, which is also a function, can be passed to the `for`..`in`
  loop.  A regular function passed to `for`..`in` loop triggers an exception.

* Object's `iterator()` function generates shallow object keys, excluding
  keys found only in the object's prototypes.


Optional semicolons
-------------------

Although semicolons are part of the Kos syntax, in most places semicolons are
not required to mark end of statements.  The Kos parser deduces semicolons from
line breaks.

It is not possible to create a program which will be ambiguous, because any
ambiguity is signaled by the parser as error.

For example, if the `return` keyword occurs alone on the line, but it is
followed by an expression in the next line, that expression is treated as part
of the `return` statement, otherwise it does not make any sense to put any code
behind the `return` statement as it would be unreachable.

Another example is when the `-`, `+`, `[` or `(` character occurs as first
non-whitespace character on a new line, but it could be interpreted as part
of the expression in the previous line.  In this case the parser signals an
error, because such expression would be ambiguous.

In order to avoid surprises, Kos eliminates situations which could be ambiguous.

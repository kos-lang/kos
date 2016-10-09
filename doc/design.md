Design of Kos
=============

Scope
-----

All variables in Kos must be declared before the first use.  A variable
declared in a particular scope is only visible in that scope and its
child scopes.

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
assigned to.  Constants of mutable types (object, array, buffer, function)
can still be modified, but cannot be changed to reference another object.


Immutable types
---------------

Integer, float, string, boolean and void types are immutable, meaning that
value of a variable of these types cannot be modified.  A variable holding
a value of any of these types can only be assigned a new value.

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

It is impossible to mix certain types of operators.  For example, binary
arithmetic (such as `+`) and bitwise (such as `&`) operators cannot be mixed
together, they require parentheses to specify their correct order in
an expression.

Binary bitwise operators: `&`, `|`, `^`, `<<` and `>>` cannot be mixed in
a single expression without parentheses.  The same applies to logical
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

There are no operators in Kos, which modify a variable in place, such as `++`.
The `+=` operator can be used to increment a value.  This reduces chances of
writing unreadable code, such as:

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

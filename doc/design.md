Design of Kos
=============

Variable declaration and scope
------------------------------

All variables in Kos must be declared before the first use.  A variable
declared in a particular scope is only visible from this scope and its
child scopes.

No undefined variables
----------------------

All variables must be initialized when they are declared.  This guarantees
that no variable is ever in an uninitialized state.  Uninitialized
variable are a source of common types of bugs.

When a variable must be declared but there is no reasonable initial value
for it, `void` can be used as initial value.

Tabs
----

Source code needs to be viewable in many different ways: in a browser, in a
terminal window, etc.  The only way to guarantee correctness of presentation
is to forbid the use of tab characters, which every programmer sets up to
a different width.  With varying tab widths, indentation breaks easily and
the source code becomes unreadable.

For this reason, Kos does not support tab characters.  Any tab character
appearing instead of whitespace is treated as an error.  Tab characters are
still allowed in string literals.

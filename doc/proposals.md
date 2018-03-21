Enhancement proposals
=====================

* Add syntax coloring for Sublime, Eclipse, IntelliJ IDEA

* Change division to produce float, add integer division operator //

* ? Add buffer strings b""

* ? Empty array, buffer, string, object (shallow) are all falsy

* ? if, while, switch variable declarations

* ? remove C-style for loop

* `private` keyword to make globals module-private

* ? Support multi-line strings the Python way or the C way?

* set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

* Constructor functions

    - Write test to use constructors with map function.
    - Determine interaction with generators and iterators.

* Class improvements

    - Add class inheritance.
    - ? Add 'static' functions in classes.
    - ? Add 'super'.

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

* Optimizations:

    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.

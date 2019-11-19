Enhancement proposals
=====================

* Add syntax coloring for Eclipse, IntelliJ IDEA

* ? Change division to produce float, add integer division operator, e.g. //

* ? Add buffer strings b"" or buffer literals <01 fa>

* ? Empty array, buffer, string, object (shallow) are all falsy

* if, while, switch variable declarations

* ? remove C-style for loop

* ? Support multi-line strings the Python way or the C way?

* set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

* Constructor functions - determine interaction with generators and iterators.

* Add 'static' functions in classes.

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

* Optimizations:

    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.
    - Replace binds for non-global functions with LOAD.CONST

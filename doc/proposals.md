Enhancement proposals
=====================

* if, switch variable declarations

* Use _ as operator for empty extraction instead of void, like in Python

* Add syntax coloring for Eclipse, IntelliJ IDEA

* ? Support multi-line strings the Python way

* ? Remove //-style single-line comments

* ? Change division to produce float, add integer division operator, e.g. //

* ? Add buffer strings b"" or buffer literals <01 fa>

* ? Empty array, buffer, string, object (shallow) are all falsy

* ? remove C-style for loop

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

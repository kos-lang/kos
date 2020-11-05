Enhancement proposals
=====================

* Add { } support for switch case instead of : (retain : ???)

* Hook Ctrl-C in interpreter to interrupt all threads

* Object ctor: accept some form of input args, e.g. pairs of [key, value]

* Add warnings about variable shadowing

* ? Callstack from shallow is missing built-in function

* if, switch variable declarations

* Use _ as operator for empty extraction instead of void, like in Python

* Now that we handle x... - throw error if too many args are provided!

* Reduce need for passing module objects, e.g. when creating builtin dyn props

* Add string.prototype.swapcase()

* Close generators by throwing an exception through the generator code during GC

* Allow import anywhere

* Add exit/help as special commands in REPL or as functions in a special module

* Add way to list modules and globals in modules, manipulate globals

* Comparison operators for arrays and objects

* ? Add delete global in REPL (only), set BADPTR on global, functions using globals should then fail

* Expand object in-place, e.g. { a: 1, b..., c: 2 }

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

* Allow invoking functions declared anywhere in current and outer scopes.

* Optimizations:

    - Add object iterator instructions, add universal iterator internal object.
    - Add compile-time resolution for range() (e.g. using special instruction).
    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.
    - Replace binds for non-global functions with LOAD.CONST

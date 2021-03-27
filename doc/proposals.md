Enhancement proposals
=====================

* Add { } support for switch case instead of : and make : optional before {

* Hook Ctrl-C in interpreter to interrupt all threads

* Add warnings about variable shadowing

* ? Callstack from shallow is missing built-in function

* Allow _ in parameters of the target of -> stream operator

* Now that we handle x... - throw error if too many args are provided!

* Reduce need for passing module objects, e.g. when creating builtin dyn props

* Allow referencing module objects from the language, add a way to extract list of modules

* Add string.prototype.swapcase()

* Close generators by throwing an exception through the generator code during GC

* Allow import anywhere

* Add exit/help as special commands in REPL or as functions in a special module

* Add way to list modules and globals in modules, manipulate globals

* Comparison operators for arrays and objects

* ? Add match expression

* ? Add delete global in REPL (only), set BADPTR on global, functions using globals should then fail

* Expand object in-place, e.g. { a: 1, b..., c: 2 }

* Add syntax coloring for VSCode, Eclipse, IntelliJ IDEA

* ? Support multi-line strings the Python way

* ? Remove //-style single-line comments

* ? Change division to produce float, add integer division operator, e.g. //

* ? Add buffer strings b"" or buffer literals <01 fa>

* ? Empty array, buffer, string, object (shallow) are all falsy

* ? set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

* Constructor functions - determine interaction with generators and iterators.

* Add 'static' functions in classes.

* Allow invoking functions declared anywhere in current and outer scopes.

* Optimizations:

    - Add compile-time resolution for range() (e.g. using special instruction).
    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.
    - Replace binds for non-global functions with LOAD.CONST

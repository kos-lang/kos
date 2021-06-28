Enhancement proposals
=====================

* Add dsl (domain-specific language) literals; what keyword?:

        const buf = dsl(buffer)(1234abc)
        const re1 = dsl(re)/.*/
        const re2 = dsl(re):.*:
        const re3 = dsl(re){.*}
        const re4 = dsl(re)<.*>
        const re5 = dsl(re)(.*)
        const sh  = dsl("glsl"){ main() { outColor = vec3(1, 0, 0.5) } }

* Add 'static' members in classes.

        class fruit {
            static var type = "apple" # fruit.type = "apple"
            static fun get_value { return 0 } # fruit.get_value = fun { ... }
        }

* Add 'enum' syntax sugar for multiple, consecutive integer constants.

* Allow invoking functions declared anywhere in current and outer scopes.

* Optimizations:

    - Add compile-time resolution for range() (e.g. using special instruction).
    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.
    - Replace binds for non-global functions with LOAD.CONST

* Hook Ctrl-C in interpreter to interrupt all threads; add a flag to interrupt all threads
  for GC, Ctrl-C and debugger break/stop

* Close generators by throwing an exception through the generator code during GC

* Allow _ in parameters of the target of -> stream operator

* Add warnings about variable shadowing

* ? Callstack from shallow is missing built-in function

* ? Now that we handle x... - throw error if too many args are provided!

* Reduce need for passing module objects, e.g. when creating builtin dyn props

* Allow referencing module objects from the language, add a way to extract list of modules

* Add string.prototype.swapcase()

* Allow import anywhere

* Add exit/help as special commands in REPL or as functions in a special module

* Add way to list modules and globals in modules, manipulate globals

* Comparison operators for arrays and objects

* ? Add match expression

* ? Add delete global in REPL (only), set BADPTR on global, functions using globals should then fail

* Expand object in-place, e.g. { a: 1, b..., c: 2 }

* Add syntax coloring for VSCode, Eclipse, IntelliJ IDEA

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

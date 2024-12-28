Enhancement proposals
=====================

Strings
-------

* Add separate operator for string concatenation, e.g. `++`.

* Add dsl (domain-specific language) literals; what keyword?:

        const buf = dsl(buffer)(1234abc)
        const re1 = dsl(re)/.*/
        const re2 = dsl(re):.*:
        const re3 = dsl(re){.*}
        const re4 = dsl(re)<.*>
        const re5 = dsl(re)(.*)
        const sh  = dsl("glsl"){ main() { outColor = vec3(1, 0, 0.5) } }

* Add `string.prototype.swapcase()`.

* Allow `r""` raw string literals to be multi-line.

* Add buffer strings `b""` and buffer literals `<01 fa>`.

* Add tagged raw strings, e.g.: `R"glsl(....)glsl"`.

* Fix string comparison, compare by code point value.

* Add unicode module for string comparison, uppercase/lowercase, etc.
  See [unicode tables](https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt).

Threads
-------

* Remove async keyword, spawn threads via `function.prototype.async()`.

* ? Ctrl-C does not interrupt a mutex.

* Add a way to detect mutex recursion and throw exception.

* Also add a way to detect deadlocks etc.

* Unable to create more than 32 threads, until previous threads are joined.
  This happens even if all threads finish.
  It should be possible to create more threads once previous threads finish,
  without the need to join them.

Optimizations
-------------

* Improve function invocation speed by not using arrays for arguments.
  Pass arguments via pointer and size.  For built-in functions, could pass
  pointer directly to stack, because stack is non-movable by GC, but be
  careful of callee modifying the contents.  If needed, allocate from a
  pool of non-movable objects, return objects back to pool on function
  return for reuse.

* Optimizations:

    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.
    - Hoist constants outside of loops.  Reuse constants.
    - Function inlining for simple functions, esp. inside loops.
    - Stack reuse in tail calls.
    - Replace binds for non-global functions with LOAD.CONST
    - Init arrays/objects from a constant, by doing a deep copy (only works for some
      initializers, which don't reference other objects, etc.)
    - Make r255 a VOID object (although how will this help?).
    - References 127 constants via r128..r254 (LOAD.CONST 0 is then constant 127).

Syntax
------

* Add `static` members in classes.

        class fruit {
            static var type = "apple" # fruit.type = "apple"
            static fun get_value { return 0 } # fruit.get_value = fun { ... }
        }

* Add `enum` syntax sugar for multiple, consecutive integer constants.

* Allow invoking functions declared anywhere in current and outer scopes.

* Allow `_` or `...` in parameters of the target of `->` stream operator

        input -> map(integer, _) -> each(print, _)
        input -> map(integer, ...) -> each(print, ...)

* Add warnings about variable shadowing, consider making shadowing an error.

* Expand object in-place, e.g. { a: 1, b..., c: 2 }

* Add match expression.

* Add object and iterable destructuring.

* ? Remove //-style single-line comments

* ? Change division to produce float, add integer division operator, e.g. //

* ? set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

Other
-----

* Add `base.unzip()`

        const left, right = base.unzip([ [1, 2], [3, 4], [5, 6] ])
        left  -> each(print) # prints 1, 3, 5
        right -> each(print) # prints 2, 4, 6

* Add `base.head(n)` and `base.tail(n)`.

* Close generators by throwing an exception through the generator code during GC.  Example:

        fun { yield 1; print("done") } () -> first -> print

  The above function will never print "done".  This is fine in this case, but when a with
  statement is used, e.g. mutex or open file, the finally clause is never executed.

* Add a way to extract a list of modules.

* Add exit/help as special commands in REPL or as functions in a special module.

* ? Add delete global in REPL (only), set BADPTR on global, functions using globals should then fail

* ? Comparison operators for objects.

* Add syntax coloring for Eclipse, IntelliJ IDEA

* ? Empty array, buffer, string, object (shallow) are all falsy

* Constructor functions - determine interaction with generators and iterators.

* Copy paths in `kos.execute` from current interpreter.

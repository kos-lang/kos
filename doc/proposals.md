Enhancement proposals
=====================

* Change division to produce float, add integer division operator //

* Add buffer strings b""

* ? Empty array, buffer, string, object (shallow) are all falsy

* if and switch variable declarations

* Spread operator for arguments in invocation:

        const a = [ {}, 1.5, false ]
        some_func(1, a..., 2, "abc"...)

        # Is equivalent to
        some_func(1, {}, 1.5, false, 2, "a", "b", "c")

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

* Class-like prototypes in conjunction with real constructor functions

    - A class object is essentially a function object, with the 'constructor'
      function being the body of the function.  All the remaining functions
      declared become members of the prototype.

            class Base {
                constructor(x, y) {
                    this.x = x
                    this.y = y
                }
                get_x = () => (this.x)
                get_y = () => (this.y)
            }

            assert typeof Base == "function"
            var b = Base(1, 2)
            print("\(b.get_x()) \(b.get_y())")

            class Derived : Base {
                // how to call base class constructor?
                set_xy(x, y) { this.x = x; this.y = y }
            }

    - Add static?
    - Add super?

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

* Optimizations:

    - Find line where a variable is no longer used, release register.
    - Don't reload constants if they are already in registers.

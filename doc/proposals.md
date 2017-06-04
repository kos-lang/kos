Enhancement proposals
=====================

* ? Support multi-line strings the Python way or the C way?

* Partial application:

        const sum3 = fun(a, b, c) -> ( a + b + c )
        const sum2 = add3(0)    # sum2 is fun(b, c) -> ( 0 + b + c )
        const add1 = add3(0, 1) # add1 is fun(c) -> ( 0 + 1 + c )

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
                get_x -> (this.x)
                get_y -> (this.y)
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

* Spread operator

        var str = "hello";
        var a   = [ str ... ]; // [ "h", "e", "l", "l", "o" ]
        some_func(str, a ...); // some_func(str, "h", "e", "l", "l", "o");

* Range spread operator

        var a = [ 0 ... 5 ]; // [ 0, 1, 2, 3, 4, 5 ]

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

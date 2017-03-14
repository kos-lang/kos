Enhancement proposals
=====================

* ? Support multi-line strings the Python way or the C way?

* set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

* Class-like prototypes

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
            var b = new Base(1, 2)
            print("\(b.get_x()) \(b.get_y())")

            class Derived : Base {
                // how to call base class constructor?
                set_xy(x, y) { this.x = x; this.y = y }
            }

    - Add static?
    - Add super?

* Spread operator

        var str = "hello";
        var a   = [ str ... ]; // [ 'h', 'e', 'l', 'l', 'o' ]
        some_func(str, a ...); // some_func(str, 'h', 'e', 'l', 'l', 'o');

* Range spread operator

        var a = [ 0 ... 5 ]; // [ 0, 1, 2, 3, 4, 5 ]

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

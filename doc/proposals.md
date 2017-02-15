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
    - local variables become this members
    - no const local variables (can be circumvented through 'this')

            prototype Constructor(x) {
                var a = void;
                var b = "str";
                var c = 10;
                fun d(x) { return c + x; }
            }
            typeof Constructor == "function";
            var c = new Constructor;

            prototype Base {
                private var x = 0;
                private fun GetX() { return x; }
                fun GetXP1() { return GetX() + 1; }
            }
            prototype Derived(y) : Base {
                var y = y;
                fun GetXP1() { return this.y + 1; }
            }

* Private and public prototype and module members.  What should be default?
    - Private global var in module becomes just a local var, with all side
      effects, including closures.
    - Private var/fun in prototype becomes a constructor's local variable,
      also with all side effects.
    - Maybe public should be default and private should be explicit?

* Spread operator

        var str = "hello";
        var a   = [ str ... ]; // [ 'h', 'e', 'l', 'l', 'o' ]
        some_func(str, a ...); // some_func(str, 'h', 'e', 'l', 'l', 'o');

* Range spread operator

        var a = [ 0 ... 5 ]; // [ 0, 1, 2, 3, 4, 5 ]

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

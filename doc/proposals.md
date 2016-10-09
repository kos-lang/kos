Enhancement proposals
=====================

* U+221E infinity symbol

* Stream operator ->

        for var i in range(100) -> map(λ(x)->(x+1)) -> filter(λ(x)->(x & 1)) {
            print i;
        }

        for var line in file.open("myfile.txt") -> file.read_lines -> strip {
            print line;
        }

* ? Support multi-line strings the Python way or the C way?

* set/get:

        var myobj = {
            prop: set fun { },
            prop: get fun { }
        };
        myobj.prop = set fun { };
        myobj.prop = get fun { };

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

* Semicolon insertion
    - statements affected:
      - EmptyStatement
      - ExpressionStatement (including assignment and variable declaration)
      - DoStatement
      - ContinueStatement
      - BreakStatement
      - ReturnStatement
      - ThrowStatement
    - if required, before EOL, EOF or }
    - EOL after return just treated as semicolon?
    - no semicolon insertion inside for header
    - no semicolon should be inserted for a function call split across lines: `my_func<LF>(some, args);`
    - watch the `-` operator starting on a new line!!!
    - problem: array literal starting on new line can be treated as refinement!

            var a = b       # No semicolon
            [1,2,3].map(a)  # [ treated as refinement

* Spread operator

        var str = "hello";
        var a   = [ str ... ]; // [ 'h', 'e', 'l', 'l', 'o' ]
        some_func(str, a ...); // some_func(str, 'h', 'e', 'l', 'l', 'o');

* Range spread operator

        var a = [ 0 ... 5 ]; // [ 0, 1, 2, 3, 4, 5 ]

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

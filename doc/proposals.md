Enhancement proposals
=====================

* Syntax for currying functions and a curry instruction.
    - The instruction would simply set up the additional args and then jump
      to the actual function being curried.  It would also automatically
      look at the required number of args of the destination function
      during setup.

    - It is possible without special syntax or special instruction:

            fun add_2(a, b) { return a + b; }
            fun add_2(a) { return add_1(1, a); }

    - Nope, we should probably just rely on TAIL.CALL.

* Stream composition operator ->

        for var line in file.read_lines("myfile.txt") -> map(λ(line) -> (line.strip())) {
            print line;
        }

        for var line in os.shell("ls") -> sed.filter("a.*b") -> map(λ(line)->(line[0]=='\t'?line[1:]:line))

* ? Support multi-line strings the Python way or the C way?

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
    - any code following 'return' keyword on new line should be treated as
      expression for the return statement
    - (proposed) no semicolon insertion inside `for` header
    - problems:

            # Split function call
            func
            (my, args)

            # Even worse
            var f = fun { }
            (fun() { })()

            # Another example of split function call
            a + b
            (c())

            # Split expression
            a = b
            - c

            # Array literal stariting on new line treated as refinement
            var a = b
            [1, 2, 3].map(a)

    - Proposed solution: if EOL is encountered where a semicolon could be and
      the next token is `(`, `[`, `+` or `-`, fail compilation complaining that
      the notation is ambiguous and indicate that the code should be changed
      to remove the ambiguity.

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

* U+221E infinity symbol

* Spread operator

        var str = "hello";
        var a   = [ str ... ]; // [ 'h', 'e', 'l', 'l', 'o' ]
        some_func(str, a ...); // some_func(str, 'h', 'e', 'l', 'l', 'o');

* Range spread operator

        var a = [ 0 ... 5 ]; // [ 0, 1, 2, 3, 4, 5 ]

* Invocation with explicit argument names, e.g.:

        var f = file.open(name="xyz", flags="r");

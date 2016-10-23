Lexical grammar
===============

Program
-------

A program consists of modules.

Each module can import more modules at the beginning of its source file.

Modules are executed when they are imported.

If a module is imported multiple times, it is executed only the first time.

Module
------

A module's source file consists of UTF-8 characters.

A module's source file is processed in the following steps:

1. The lexer converts the source file to a series of tokens.

2. The parser reads in the tokens from the lexer and converts them to an
   intermediate form, such as an abstract syntax tree.

3. The intermediate from can be optimized and converted to bytecode for future
   execution, or it can be directly executed.

Module's global scope is executed the first time the module is imported.

If a module is imported several times by multiple modules, its global
scope is not executed again.

Lexer input
-----------

The input to the lexer is a source file, which consists of UTF-8 characters.

If the first character of the source file is byte order mark U+FEFF
(bytes 0xEF 0xBB 0xBF), it is ignored.

Lexer output
------------

The output of the lexer consists of a list of tokens.

Token types
-----------

The lexer recognizes and outputs the following types of tokens:

* Whitespace
* Comment
* Identifier
* Keyword
* Decimal integer number
* Decimal floating-point number
* Hexadecimal integer number
* Binary integer number
* String
* Separator
* Operator

Whitespaces
-----------

A contiguous series of whitespace non-EOL characters is treated as a single
whitespace token.  Whitespace characters include:

* U+0000 null
* U+000B vertical tab
* U+000C form feed
* U+0020 space
* U+00A0 non-breaking space
* U+2028 line separator
* U+2029 paragraph separator
* U+FEFF byte order mark

Character U+0009 (horizontal tab) is not treated as a whitespace character and
is not allowed, except in string literals.

Each EOL character sequence is treated as a single whitespace token.  An EOL
character sequence is either `LF`, `CR-LF`, or `CR`, if it's not followed by
`LF`.

    Eol                ::= ( "\r" "\n" ) | "\r" | "\n"

    WhitespaceChar     ::= " " | "\0" | "\f" | "\v"
                         | "\x{A0}" | "\x{2028}" | "\x{2029}" | "\x{FEFF}"

    WHITESPACE_LITERAL ::= ( WhitespaceChar ( WhitespaceChar )* ) | Eol

Comments
--------

A single-line comment starts with a `#` character or a sequence of `//` and
ends at the end of line.  The initial characters and the EOL sequence are
treated as part of the comment token.

A multi-line comment starts with the `/*` sequence and ends with the `*/`
sequence.  It can include EOL characters.

    LineCommentChar  ::= UTF8_CHARACTER except ( "\r" | "\n" )

    BlockCommentChar ::= ( UTF8_CHARACTER except "*" )
                       | ( "*" not followed by "/" )

    LineComment      ::= "/" "/" ( LineCommentChar )* Eol

    HashLineComment  ::= "#" ( LineCommentChar )* Eol

    BlockComment     ::= "/" "*" BlockCommentChar "*" "/"

    COMMENT_LITERAL  ::= LineComment | HashLineComment | BlockComment

Identifiers
-----------

An identifier can consist of lowercase letters, uppercase letters, digits and
an underscore `_`.

The first character of an identifier must be a letter or underscore, it cannot
be a digit.

If the identifier token matches one of the reserved keywords, it is treated
as a keyword token.

    LowercaseLetter      ::= "a" .. "z"

    UppercaseLetter      ::= "A" .. "Z"

    DecimalDigit         ::= "0" .. "9"

    Underscore           ::= "_"

    Letter               ::= LowercaseLetter | UppercaseLetter

    LetterOrUnderscore   ::= Letter | Underscore

    AlphanumOrUnderscore ::= Letter | DecimalDigit | Underscore

    IDENTIFIER_NAME      ::= LetterOrUnderscore ( AlphanumOrUnderscore )*

Keywords
--------

Keywords are identifiers matching one of the reserved keywords.
The following reserved keywords are defined:

* `__line__`
* `assert`
* `break`
* `catch`
* `const`
* `continue`
* `delete`
* `defer`
* `do`
* `else`
* `fallthrough`
* `false`
* `finally`
* `for`
* `fun`
* `get` (reserved)
* `if`
* `import`
* `in`
* `instanceof`
* `new`
* `private` (reserved)
* `prototype` (reserved)
* `public` (reserved)
* `return`
* `set` (reserved)
* `switch`
* `this`
* `throw`
* `true`
* `try`
* `typeof`
* `var`
* `void`
* `while`
* `with`
* `yield`
* `λ`

Keywords `true`, `false` and `void` are output by the lexer as special literal
types.

    TrueLiteral     ::= "t" "r" "u" "e"
    
    FalseLiteral    ::= "f" "a" "l" "s" "e"

    BOOLEAN_LITERAL ::= TrueLiteral | FalseLiteral

    VOID_LITERAL    ::= "v" "o" "i" "d"

Three keywords are currently reserved - `get`, `private`, `prototype`,
`public` and `set`.

The `λ` keyword is the unicode Greek letter "lambda", code U+03BB, which in
UTF-8 consists of two bytes: `0xCE 0xBB`.  This keyword can be used in place
of the `fun` keyword in function literals, but not in function statements.

The main difference between keywords and non-keyword literals is that keywords
cannot be used as variable names.  However, keywords can still be used as
object property names.

Decimal numbers
---------------

A decimal number is either an integer or a floating-point number.

A decimal integer number is either a `0` or a sequence of digits starting
with a non-zero digit.

A decimal floating-point number consists of base, mantissa and exponent.

Base is a `0` or a decimal integer number.  Base is followed by a `.`.

The optional mantissa following the `.` conists of any number of digits.

Exponent is `e`, `E`, `p` or `P` followed by an optional sign `+` or `-`,
followed by a decimal number.

If `e` or `E` is specified, the exponent is a power of 10.
If `p` or `P` is specified, the exponent is a power of 2.

    NonZeroDecimalDigit  ::= "1" .. "9"

    NonZeroDecimalNumber ::= NonZeroDecimalDigit ( DecimalDigit )*

    DecimalNumber        ::= "0" | NonZeroDecimalNumber

    Base                 ::= DecimalNumber

    Mantissa             ::= "." ( DecimalDigit )*

    ExponentSpecifier    ::= "e" | "E" | "p" | "P"

    Exponent             ::= ExponentSpecifier [ "+" | "-" ] DecimalNumber

    DEC_INTEGER_LITERAL  ::= Base

    DEC_FLOAT_LITERAL    ::= Base [ Mantissa ] [ Exponent ]

Hexadecimal numbers
-------------------

A hexadecimal number consists of a `0` followed by `x` or `X`, followed by at
least one hexadecimal digit.

    LowercaseHexDigit    ::= "a" .. "f"

    UppercaseHexDigitExt ::= "A" .. "F"

    HexDigit             ::= DecimalDigit
                           | LowercaseHexDigit
                           | UppercaseHexDigitExt

    HEX_INTEGER_LITERAL  ::= "0" ( "x" | "X" ) HexDigit ( HexDigit )*

Binary numbers
--------------

A binary number consists of a `0` followed by `b` or `B`, followed by at
least one binary digit.

    BinaryDigit         ::= "0" | "1"

    BIN_INTEGER_LITERAL ::= "0" ( "b" | "B" ) BinaryDigit ( BinaryDigit )*

Strings
-------

A string is delimited by `"` or `'` characters.  Both the beginning and the end
of the string must be delimited by the same character.

UTF-8 characters with code greater than 127 are legal string components.

A `\` character occuring inside a string indicates the beginning of an escape
sequence.

    EscapeSequence       ::= "f"
                           | "n"
                           | "r"
                           | "t"
                           | "v"
                           | "\"
                           | "'"
                           | """
                           | "0"
                           | ( "x" HexDigit HexDigit )
                           | ( "x" "{" HexDigit ( HexDigit )* "}" )

    EscapedChar          ::= "\" EscapeSequence

    UnescapedStringChar  ::= UTF8_CHARACTER except ( "'" |  """ | "\" )

    StringChar           ::= UnescapedStringChar | EscapedChar

    SingleQStringLiteral ::= "'" ( StringChar | """ )* "'"

    DoubleQStringLiteral ::= """ ( StringChar | "'" )* """

    STRING_LITERAL       ::= SingleQStringLiteral | DoubleQStringLiteral

An escaped opening parenthesis begins a string interpolation expression.  The
lexer stops fetching further characters and returns the current token as a
string.  The parser expects a right hand side expression to follow.  The right
hand side expression is terminated with a mandatory closing parenthesis.  The
parser then puts the lexer back in string extraction mode, indicating to the
lexer that all further characters are a continuation of a string.

The string interpolation syntax creates a dependency between the parser and the
lexer, but it is a trade-off for a very useful syntax.

    S_Q_STRING_LITERAL_BEGIN ::= "'" ( StringChar | """ )* "\" "("

    S_Q_STRING_LITERAL_CONT  ::= ( StringChar | """ )* "\" "("

    S_Q_STRING_LITERAL_END   ::= ( StringChar | """ )* "'"

    D_Q_STRING_LITERAL_BEGIN ::= """ ( StringChar | "'" )* "\" "("

    D_Q_STRING_LITERAL_CONT  ::= ( StringChar | "'" )* "\" "("

    D_Q_STRING_LITERAL_END   ::= ( StringChar | "'" )* """

Separators
----------

A separator is a single character from the separator set specified below:

    SEPARATOR_LITERAL ::= "["
                        | "]"
                        | "("
                        | ")"
                        | "{"
                        | "}"
                        | ","
                        | ";"
                        | ":"

Operators
---------

If a character belongs to the list of characters which constitute operators,
it is treated as the beginning of an operator.  The lexer tries to match as
many contiguous characters as possible to operators.

For example if the lexer finds a `<` character, it will treat is as a `<`
operator only if it is not followed by `<` or `=` character, otherwise it will
treat it as either `<<` or `<=` operator.

    OPERATOR_LITERAL ::= "+"
                       | "-"
                       | "*"
                       | "/"
                       | "%"
                       | "&"
                       | "|"
                       | "^"
                       | "!"
                       | "~"
                       | "="
                       | ( "+" "=" )
                       | ( "-" "=" )
                       | ( "*" "=" )
                       | ( "/" "-" )
                       | ( "%" "=" )
                       | ( "&" "=" )
                       | ( "|" "=" )
                       | ( "^" "=" )
                       | ( "<" "<" "=" )
                       | ( ">" ">" "=" )
                       | ( ">" ">" ">" "=" )
                       | "<"
                       | ">"
                       | "?"
                       | "."
                       | "..."
                       | ( "<" "=" )
                       | ( ">" "=" )
                       | ( "<" "<" )
                       | ( ">" ">" )
                       | ( ">" ">" ">" )
                       | ( "!" "=" )
                       | ( "=" "=" )
                       | ( "&" "&" )
                       | ( "|" "|" )
                       | ( "-" ">" )

Syntax
======

Parser operation
----------------

The parser retrieves tokens from the lexer.

The parser ignores all whitespace and comment tokens, because these tokens are
meaningless for syntactic analysis.

The remaining tokens are parsed according to the following rules.

Module source file
------------------

From the parser's perspective, a source file consists of zero or more
statements.

An empty source file or a source file which contains only spaces or comments
is still a valid source file.

    SourceFile ::= ( ImportStatement )* ( Statement )*

Statement
---------

    Statement ::= EmptyStatement
                | ExpressionStatement
                | FunctionDeclaration
                | IfStatement
                | TryStatement
                | DeferStatement
                | WithStatement
                | SwitchStatement
                | DoStatement
                | WhileStatement
                | ForStatement
                | ContinueStatement
                | BreakStatement
                | ReturnStatement
                | ThrowStatement
                | AssertStatement

Import statement
----------------

    ImportStatement ::= ( MandatoryImport | TryImport ) ";"

    MandatoryImport ::= "import" Identifier [ "." ( Identifier | "*" ) ]

    TryImport       ::= "try" "import" Identifier

Compound statement
------------------

A compound statement is a list of zero or more statements enclosed in curly
braces.

    CompoundStatement ::= "{" ( Statement )* "}"

Empty statement
---------------

Empty statement consists of a sole semicolon.

    EmptyStatement ::= ";"

Expression statement
--------------------

Expression statement is an expression followed by a semicolon.

    ExpressionStatement ::= Expression ";"

Function statement
------------------

Function statement declares a local variable and assigns a function object
to it.  The variable cannot be assigned to again.

The function statement begins with a `fun` keyword, followed
by identifier, which is the variable name, followed by parameter list,
followed by the function body in the form of a compound statement.

    FunctionDeclaration ::= "fun" Identifier [ ParameterList ] CompoundStatement

The following statements are equivalent:

    fun Sum(x, y)
    {
        return x + y;
    }

    const Sum = fun(x, y)
    {
        return x + y;
    };

    const Sum = λ(x,y)->(x+y);

Function arguments
------------------

Function arguments are a list of comma-separated identifiers, which are
names of the argument variables.  The list of arguments is delimited by
parentheses.  The list of arguments is optional.

TODO - no default values right now

Arguments can have default values specified after an `=` assignment sign.
The default values are computed at the place of function declaration.

The last argument name can be followed by ellipsis `...`, indicating that
the function takes a variable number of arguments.  That argument becomes
a list containing all remaining function arguments (zero or more).

Argument variables are assignable inside the function body.

    ParameterList       ::= "(" [ OneOrMoreParameters ] ")"

    OneOrMoreParameters ::= ListParameter | ManyParameters

    ManyParameters      ::= Parameter ( "," Parameter )* [ "," ListParameter ]

    Parameter           ::= Identifier

    ListParameter       ::= Identifier "..."

If statement
------------

The if statement has a condition and a compound statement.

A non-compound statement is not allowed, meaning that curly braces must be used
around the conditionally executed code.

The condition is a right-hand-side expression.  The parentheses around the
condition are optional - they are part of the condition expression.

The compound statement may optionally be followed by any number of else-if
blocks, which have their own conditions.  These blocks are executed only if
the condition from the preceding blocks were falsy.

Optionally an else block may follow, which will be executed only
if all of the preceding conditions were falsy.

    IfStatement ::= "if" RHSExpression CompoundStatement
                    ( "else" "if" RHSExpression CompoundStatement )*
                    [ "else" CompoundStatement ]

Try statement
-------------

The try statement wraps a compound statement, followed by a catch block and/or
a finally block.

If the try block completes successfuly without an exception thrown out of it,
the finally block is executed (if it exists).

If an exception is thrown inside the try block and is not caught inside it,
the catch block is executed (if it exists).  The exception object is assigned
to the variable declared by the catch block.  The parentheses around the
variable declaration are optional.  The lifetime of that variable is limited
to the catch block.  After the catch block finishes, the finally block is
executed (if it exists).

If there is an unhandled or thrown exception or return statement in the try
block or in the catch block, the finally block will be executed (if it exists).

An exception can be rethrown using the throw statement in the catch block.

    TryStatement        ::= ( TryClause CatchClause )
                          | ( TryClause CatchClause FinallyClause )
                          | ( TryClause FinallyClause )

    TryClause           ::= "try" CompoundStatement

    CatchClause         ::= "catch" CatchExceptionSpec CompoundStatement

    FinallyClause       ::= "finally" CompoundStatement

    CatchExceptionSpec  ::= CatchExceptionInner
                         | ( "(" CatchExceptionInner ")" )

    CatchExceptionInner ::= ( "var" | "const" ) Identifier

Defer statement
---------------

The defer statement defers the execution of its compound statement until the
current (enclosing) scope finishes.  The compound statement is executed
regardless of whether the enclosing scope finishes normally or is interrupted
via return, break or continue statement or via exception.

    DeferStatement ::= "defer" CompoundStatement

If there are multiple defer statements in a scope, all of them are executed.

Conceptually, the defer statement is equivalent to a finally section.

The following two examples are equivalent:

    {
        a();
        defer { b(); }
        c();
        defer { d(); }
        e();
    }

    {
        a();
        try {
            c();
            try {
                e();
            }
            finally {
                d();
            }
        }
        finally {
            b();
        }
    }

With statement
--------------

The with statement is used as a shorthand for acquiring and releasing
resources.  The statement consists of object definitions and a compound
statement.

The parentheses around the object definitions are optional.

There can be multiple comma-separated object definitions specified.

Some object definitions can be assigned to constant variables.

The `acquire` function is invoked on each object definition, if it exists,
before the compound statement is executed.

The `release` function is invoked on each object definition after the compound
statement finishes.  The `release` function is always invoked, even if there
is a return statement or an exception is thrown from within the compound
statement.  If the `release` function does not exist, an exception is
thrown.

    WithStatement      ::= "with" WithExpression CompoundStatement

    WithExpression     ::= WithExprInner
                         | ( "(" WithExprInner ")" )

    WithExprInner      ::= WithObjExpression ( "," WithObjExpression )*

    WithObjExpression  ::= [ "const" Identifier "=" ] RHSExpression

The following three examples are equivalent:

    with lock, const f = file.open(filename) {
        // ...
    }

    // Effectively the same, but using defer
    {
        if "acquire" in lock {
            lock.acquire();
        }
        defer { lock.release(); }

        const f = file.open(filename);
        if "acquire" in f {
            f.acquire();
        }
        defer { f.release(); }

        // ...
    }

    // Effectively the same, but using try/finally
    {
        if "acquire" in lock {
            lock.acquire();
        }
        try {
            const f = file.open(filename);
            if "acquire" in f {
                f.acquire();
            }
            try {
                // ...
            }
            finally {
                f.release();
            }
        }
        finally {
            lock.release();
        }
    }
            
Switch statement
----------------

The switch statement defines multiple execution paths depending of the value of
an evaluated expression.

The switch statement consists of an expression and case sections executed
depending on the value of that expression.  The parentheses around the
expression are optional (they are treated as part of the expression).

Each new case section begins with a right-hand-side expression or with
ellipsis `...`, and is followed by a compound statement, a fallthrough
statement or both (in that order).

The right-hand-side expression is evaluated at compile time and it must
evaluate to a constant.

The resulting constant can only be of immutable type: integer, float, string,
boolean, void or function.

The resulting evaluated constant value cannot repeat again in that switch
statement.

The ellipsis `...` case is optional.

There can be only one ellipsis `...` case defined for a given switch statement.

The ellipsis `...` case does not have to be specified last.

If the value of the switch expression is equal to one of the right-hand-side
expressions, the corresponding compound statement is executed.

If the value of the switch expressions does not match any right-hand-side
expression, then the compound statement following the ellipsis `...` is
executed, if it exists, otherwise the switch statement terminates.

If there is no fallthrough statement following a compound statement, then
the switch statement terminates after that compound statement is executed.

If a fallthrough statement is executed following a compound statement or
following a matching right-hand-side expression, the switch statement does not
terminate, but the execution continues in the next defined case section, as if
the right-hand-side expression (or ellipsis `...`) for that section matched.

    SwitchStatement ::= "switch" RHSExpression
                        "{" ( SwitchCase )* [ DefaultCase ] ( SwitchCase )* "}"

    SwitchCase      ::= RHSExpression CaseStatement

    DefaultCase     ::= "..." CaseStatement

    CaseStatement   ::= ( CompoundStatement [ Fallthrough ] )
                        | Fallthrough

    Fallthrough     ::= "fallthrough" ";"

Do-while statement
------------------

The do-while statement consists of a compound statement and a condition.
It executes the compound statement as long as the condition is truthy.
The condition is evaluated after each iteration of the compound statement.

The parentheses around the condition expression are optional - they are part
of the expression.

    DoWhileStatement ::= "do" CompoundStatement
                         "while" RHSExpression ";"

While statement
---------------

The while statement consists of a condition and a compound statement.
It executes the compound statement as long as the condition is truthy.
The condition is evaluated before each iteration of the compound statement.

The parentheses around the condition expression are optional - they are part
of the expression.

    WhileStatement ::= "while" RHSExpression CompoundStatement

For statement
-------------

The for statement consists of the loop control definition and a compound
statement.  The loop control definition determines how the for statement
loops over the compound statement.

    ForStatement ::= "for" ForLoopControl CompoundStatement

There are two variants of the loop control definition.

    ForLoopControl ::= ForCondition
                     | ForInExpression
                     | ( "(" ForCondition ")" )
                     | ( "(" ForInExpression ")" )

The first variant consists of a loop initialization expression list, loop
condition and a step expression.

    ForCondition          ::= [ ExpressionList ] ";" [ RHSExpression  ] ";" [ ForStepExpressionList ]

    ExpressionList        ::= Expression ( "," Expression )*

    ForStepExpressionList ::= ForStepExpression ( "," ForStepExpression )*

The initialization expression list is executed once before the loop.

Any variables declared in the loop initialization expression list are visible
only within the loop control definition and the for compound statement.
The values of these variables are retained across loops.

The loop condition is evaluated before every loop, including the first loop.
Consecutive loops are only executed if the loop condition is truthy.  If the
loop condition is falsy, the loop is interrupted.  If the loop condition is
empty, the loop executes forever, unless there is a break statement in it.

The step expression list is executed after every loop.  Variable or constant
definitions are not allowed in the step expression list.

In the variant without parentheses, which are optional, the initialization
expression cannot begin with an open parenthesis and the step expression cannot
begin with an object literal, unless the object literal is in parentheses,
otherwise the loop control definition will be interpreted incorrectly and
the parser will fail with an error.

The second variant of the for statement declares variables and the expression,
which shall produce a container over which the loop will iterate.

The declared variables are visible in the compound statement.

Before the first loop commences, the expression is evaluated and
a generator is extracted from its result.  For example, if the resulting
container is an array, the generator will go over all elements of the array.

If a generator cannot be extracted, an exception is thrown.

For every loop, a new item from the generator is assigned to the variables.

If there is a single variable, the item is assigned to it.  If there are
multiple variables, consecutive elements of the item are assigned to them using
array indexing operator.  If the item does not support extracting elements,
an exception is thrown.

    ForInExpression ::= VarList "in" RHSExpression

Continue statement
------------------

The continue statement is legal only inside for, while or do-while loops.
If used in the global scope or in a function outside of the loop
statements, it will produce a compilation error.

The statement causes a jump to the end of the compound statement of
the innermost loop when executed.

    ContinueStatement ::= "continue" ";"

Break statement
---------------

The break statement is legal only inside for, while or do-while loops.
If used in the global scope or in a function outside of the loop
statements, it will produce a compilation error.

If used in a loop, the statement interrupts the loop as if it ended
immediately, no matter what the loop condition is.

    BreakStatement ::= "break" ";"

Return statement
----------------

The return statement can be used only in a function.
It is illegal in the global scope, if used in the global scope
it will produce a compilation error.

The return statement interrupts execution of a function.

The optional right-hand-side expression used in the return statement
is evaluated and it's outcome is used as the function's return value.
If the expression is omitted, the return value of the function is
`void`.

If the optional right-hand-side expression occurs in a generator function,
its value is ignored.

If the optional right-hand-side expression occurs in a constructor
function invoked with the new operator and it is an object, it is returned
instead of the new object/this by the new expression. If the
right-hand-side expression is of a non-object type, the new object/this
will be returned by the new expression.

    ReturnStatement ::= "return" [ RHSExpression ] ";"

Throw statement
---------------

The throw statement takes the provided object and initiates an exception.

During an exception, the control is passed out of the currently executing
function.

If an exception is thrown inside a try block, it can be caught by a catch
block accompanying that try block, provided it matches the catch block's
expression.

If an exception is thrown inside a catch block, it will not be processed
by this or any other catch blocks of the accompanying try block.

If an exception is thrown inside an iterator function and not caught
inside it, it is propagated to the caller. The caller sees the iterator
throw the exception.

    ThrowStatement ::= "throw" RHSExpression ";"

Assert statement
----------------

The assert statement checks a condition expression, and if the condition is
not truthy, it throws an exception.

    AssertStatement :== "assert" RHSExpression ";"

The following two pieces of code are equivalent:

    assert n > 0;

    if ! (n > 0) {
        throw "Assertion failed: n > 0";
    }

Expressions
-----------

    Expression                   ::= AssignmentExpression |
                                     ArithAssignmentExpression |
                                     VariableDefinitionExpression |
                                     RHSExpression

    ForStepExpression            ::= AssignmentExpression
                                   | ArithAssignmentExpression
                                   | RHSExpression

    AssignmentExpression         ::= MemberList AssignmentOperator RHSExpression

    ArithAssignmentExpression    ::= MemberExpression ArithAssignmentOperator RHSExpression

    VariableDefinitionExpression ::= ( VarList | ConstList ) "=" RHSExpression

    VarList ::= "var" IdentifierList

    ConstList ::= "const" IdentifierList

    IdentifierList ::= Identifier ( "," Identifier )*

    MemberList ::= MemberExpression ( "," MemberExpression )*

    RHSExpression ::= YieldExpression

    YieldExpression ::= StreamExpression
                      | ( "yield" StreamExpression )

    StreamExpression ::= [ StreamExpression "->" ] ConditionalExpression

    ConditionalExpression ::= LogicalExpression
                              [ ConditionalOperator ConditionalExpression
                              ":" ConditionalExpression ]

    LogicalExpression ::= LogicalAndExpression | LogicalOrExpression

    LogicalAndExpression ::= ComparisonExpression
                             LogicalAndOperator ComparisonExpression
                             ( LogicalAndOperator ComparisonExpression )*

    LogicalOrExpression ::= ComparisonExpression
                            ( LogicalOrOperator ComparisonExpression )*

    ComparisonExpression ::= ArithBitwiseExpression
                             [ ComparisonOperator ArithBitwiseExpression ]

    ArithBitwiseExpression ::= ArithExpression
                             | BitwiseExpression

    BitwiseExpression ::= BitwiseOrExpression
                        | BitwiseAndExpression
                        | BitwiseXorExpression
                        | BitwiseShiftExpression

    BitwiseOrExpression ::= UnaryExpression BitwiseOrOperator UnaryExpression
                            ( BitwiseOrOperator UnaryExpression )*
                            
    BitwiseAndExpression ::= UnaryExpression BitwiseAndOperator UnaryExpression
                             ( BitwiseAndOperator UnaryExpression )*
                            
    BitwiseXorExpression ::= UnaryExpression BitwiseXorOperator UnaryExpression
                             ( BitwiseXorOperator UnaryExpression )*
                            
    BitwiseShiftExpression ::= UnaryExpression BitwiseShiftOperator UnaryExpression
                               ( BitwiseShiftOperator UnaryExpression )*
                            
    ArithExpression ::= AdditiveExpression

    AdditiveExpression ::= MultiplicativeExpression
                           ( AdditiveOperator MultiplicativeExpression )*

    MultiplicativeExpression ::= UnaryExpression
                                 ( MultiplicativeOperator UnaryExpression )*

    UnaryExpression ::= UnaryOperatorExpression
                      | NewExpression

    UnaryOperatorExpression ::= ( UnaryOperator )* MemberExpression

    NewExpression ::= "new" MemberExpression

Operators
---------

    AssignmentOperator      ::= "="

    ArithAssignmentOperator ::= "+="
                              | "-="
                              | "*="
                              | "/="
                              | "%="
                              | "&="
                              | "|="
                              | "^="
                              | "<<="
                              | ">>="
                              | ">>>="

    ConditionalOperator     ::= "?"

    LogicalAndOperator      ::= "&&"

    LogicalOrOperator       ::= "||"

    ComparisonOperator      ::= "=="
                              | "!="
                              | "<="
                              | ">="
                              | "<"
                              | ">"
                              | "in"
                              | "instanceof"

    BitwiseOrOperator       ::= "|"

    BitwiseAndOperator      ::= "&"

    BitwiseXorOperator      ::= "^"

    BitwiseShiftOperator    ::= "<<"
                              | ">>"
                              | ">>>"

    AdditiveOperator        ::= "+"
                              | "-"

    MultiplicativeOperator  ::= "*"
                              | "/"
                              | "%"

    UnaryOperator           ::= "typeof"
                              | "delete"
                              | "+"
                              | "-"
                              | "~"
                              | "!"

Member specification
--------------------

    MemberExpression ::= PrimaryExpression
                       | FunctionExpression
                       | ( MemberExpression Invocation )
                       | ( MemberExpression Refinement )

    FunctionExpression         ::= SimpleFunctionExpression
                                 | CompoundFunctionExpression

    SimpleFunctionExpression   ::= ( "fun" | "λ" ) [ ParameterList ]
                                   "->" "(" RHSExpression ")"

    CompoundFunctionExpression ::= "fun" [ ParameterList ] CompoundStatement

    Invocation ::= "(" [ ArgumentList ] ")"

    ArgumentList ::= Argument ( "," Argument )*

    Argument ::= RHSExpression

    Refinement ::= ( "[" RHSExpression "]" )
                 | ( "[" [ RHSExpression ] ":" [ RHSExpression ] "]" )
                 | ( "." Identifier )
                 | ( "." StringLiteral )

    PrimaryExpression ::= "this"
                        | "__line__"
                        | Literal
                        | Identifier
                        | ArrayLiteral
                        | ObjectLiteral
                        | ( "(" RHSExpression ")" )

    Literal ::= DEC_INTEGER_LITERAL
              | DEC_FLOAT_LITERAL
              | HEX_INTEGER_LITERAL
              | BIN_INTEGER_LITERAL
              | StringLiteral
              | BOOLEAN_LITERAL
              | VOID_LITERAL

    Identifier ::= IDENTIFIER_NAME

    ArrayLiteral ::= "[" [ RHSExpression ( "," RHSExpression )* [ "," ] ] "]"

    ObjectLiteral ::= "{" [ PropertyList ] "}"

    PropertyList ::= PropertyDefinition ( "," [ PropertyDefinition ] )*

    PropertyDefinition ::= PropertyName ":" RHSExpression

    PropertyName ::= StringLiteral
                   | Identifier

    StringLiteral ::= STRING_LITERAL
                    | FormattedSingleQuotedString
                    | FormattedDoubleQuotedString

    FormattedSingleQuotedString ::= S_Q_STRING_LITERAL_BEGIN
                                    RHSExpression
                                    ( ")" S_Q_STRING_LITERAL_CONT RHSExpression )*
                                    ")" S_Q_STRING_LITERAL_END

    FormattedDoubleQuotedString ::= D_Q_STRING_LITERAL_BEGIN
                                    RHSExpression
                                    ( ")" D_Q_STRING_LITERAL_CONT RHSExpression )*
                                    ")" D_Q_STRING_LITERAL_END


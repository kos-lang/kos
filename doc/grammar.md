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
                         | "\x{2028}" | "\x{2029}"

    WhitespaceChar     ::= " " | "\f" | "\v"
                         | "\x{A0}" | "\x{FEFF}"

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

* `_`
* `__line__`
* `assert`
* `break`
* `case`
* `catch`
* `class`
* `const`
* `constructor`
* `continue`
* `default`
* `defer`
* `delete`
* `do`
* `else`
* `extends`
* `fallthrough`
* `false`
* `for`
* `fun`
* `get` (reserved)
* `if`
* `import`
* `in`
* `instanceof`
* `loop`
* `match` (reserved)
* `propertyof`
* `public`
* `repeat`
* `return`
* `set` (reserved)
* `static` (reserved)
* `super`
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

Keywords `true`, `false`, `void` and `_` are output by the lexer as special literal
types.

    TrueLiteral         ::= "t" "r" "u" "e"

    FalseLiteral        ::= "f" "a" "l" "s" "e"

    BOOLEAN_LITERAL     ::= TrueLiteral | FalseLiteral

    VOID_LITERAL        ::= "v" "o" "i" "d"

    PLACEHOLDER_LITERAL ::= "_"

The following keywords are currently reserved - `get`, `match`, `static` and `set`.

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

The optional mantissa following the `.` consists of any number of digits.

Exponent is `e`, `E`, `p` or `P` followed by an optional sign `+` or `-`,
followed by a decimal number.

If `e` or `E` is specified, the exponent is a power of 10.
If `p` or `P` is specified, the exponent is a power of 2.

    NonZeroDecimalDigit      ::= "1" .. "9"

    NonZeroDecimalNumber     ::= NonZeroDecimalDigit ( DecimalDigit | Underscore )*

    DecimalNumber            ::= "0" | NonZeroDecimalNumber

    Base                     ::= DecimalNumber

    Mantissa                 ::= "." ( DecimalDigit | Underscore )*

    ExponentSpecifier        ::= "e" | "E" | "p" | "P"

    Exponent                 ::= ExponentSpecifier [ "+" | "-" ] DecimalNumber

    DEC_INTEGER_LITERAL      ::= Base

    DEC_FLOAT_LITERAL        ::= Base [ Mantissa ] [ Exponent ]


Hexadecimal numbers
-------------------

A hexadecimal number consists of a `0` followed by `x` or `X`, followed by at
least one hexadecimal digit.

    LowercaseHexDigit    ::= "a" .. "f"

    UppercaseHexDigitExt ::= "A" .. "F"

    HexDigit             ::= DecimalDigit
                           | LowercaseHexDigit
                           | UppercaseHexDigitExt
                           | Underscore

    HEX_INTEGER_LITERAL  ::= "0" ( "x" | "X" ) HexDigit ( HexDigit )*


Binary numbers
--------------

A binary number consists of a `0` followed by `b` or `B`, followed by at
least one binary digit.

    BinaryDigit             ::= "0" | "1"

    BinaryDigitOrUnderscore ::= BinaryDigit | Underscore

    BIN_INTEGER_LITERAL     ::= "0" ( "b" | "B" ) BinaryDigitOrUnderscore
                                ( BinaryDigitOrUnderscore )*


Strings
-------

A string is delimited by `"` characters.  Both the beginning and the end
of the string must be delimited by the `"` character.

UTF-8 characters with code greater than 127 are legal string components.

A `\` character occuring inside a string indicates the beginning of an escape
sequence.

    EscapeSequence       ::= "f"
                           | "n"
                           | "r"
                           | "t"
                           | "v"
                           | "\"
                           | """
                           | "0"
                           | ( "x" HexDigit HexDigit )
                           | ( "x" "{" HexDigit ( HexDigit )* "}" )

    EscapedChar          ::= "\" EscapeSequence

    UnescapedStringChar  ::= UTF8_CHARACTER except ( """ | "\" | Eol )

    StringChar           ::= UnescapedStringChar | EscapedChar

    EscapedStringLiteral ::= """ ( StringChar )* """

    RawStringLiteral     ::= ( "r" | "R" ) """ ( UTF8_CHARACTER except """ )* """

    STRING_LITERAL       ::= EscapedStringLiteral | RawStringLiteral

A raw string literal, starting with `r` or `R`, does not contain any escape
sequences, the `\` character is treated literally.

If a double quote `"` character follows the `\` backslash character inside
a raw string literal, it is treated as part of the string lteral and does not
terminate the string literal.

An escaped opening parenthesis begins a string interpolation expression.  The
lexer stops fetching further characters and returns the current token as a
string.  The parser expects a right hand side expression to follow.  The right
hand side expression is terminated with a mandatory closing parenthesis.  The
parser then puts the lexer back in string extraction mode, indicating to the
lexer that all further characters are a continuation of a string.

The string interpolation syntax creates a dependency between the parser and the
lexer, but it is a trade-off for a very useful syntax.

    STRING_LITERAL_BEGIN ::= """ ( StringChar )* "\" "("

    STRING_LITERAL_CONT  ::= ( StringChar )* "\" "("

    STRING_LITERAL_END   ::= ( StringChar )* """


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
                       | ( "/" "=" )
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
                       | ( "." "." "." )
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
                       | ( "=" ">" )


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

    SourceFile ::= ( ImportStatement )* ( Statement | PublicStatement )*


Statement
---------

    Statement ::= EmptyStatement
                | ExpressionStatement
                | FunctionDeclaration
                | ClassDeclaration
                | DoStatement
                | IfStatement
                | TryStatement
                | DeferStatement
                | WithStatement
                | SwitchStatement
                | LoopStatement
                | RepeatWhileStatement
                | WhileStatement
                | ForStatement
                | ContinueStatement
                | BreakStatement
                | FallthroughStatement
                | ReturnStatement
                | ThrowStatement
                | AssertStatement


Semicolons
----------

Many statements are terminated with semicolons, but the semicolons are
optional in most cases.  The parser tries to parse as many tokens as it can,
but when it encounters a non-whitespace, non-comment token which cannot be
part of the parsed expression, and that token is first on the line, the
parser treats the preceding end of line as a semicolon.

    OptSemicolon ::= ";"
                   | WHITESPACE_LITERAL containing Eol
                   | COMMENT_LITERAL containing Eol


Import statement
----------------

    ImportStatement  ::= "import" ( ImportModule | ImportGlobal | ImportAllGlobals | ImportList ) OptSemicolon

    ImportModule     ::= ModulePath

    ImportGlobal     ::= ModulePath "." Identifier

    ImportAllGlobals ::= ModulePath "." "*"

    ImportList       ::= ModulePath ":" Identifier ( "," Identifier )*

    ModulePath       ::= ( Identifier "/" )* Identifier


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

    ExpressionStatement ::= Expression OptSemicolon


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
        return x + y
    }

    const Sum = fun(x, y)
    {
        return x + y
    }

    const Sum = (x, y) => x + y


Function arguments
------------------

Function arguments are a list of comma-separated identifiers, which are
names of the argument variables.  The list of arguments is delimited by
parentheses.  The list of arguments is optional.

Arguments can have default values specified after an `=` assignment sign.
The default values are computed at the place of function declaration.

The last argument name can be followed by ellipsis `...`, indicating that
the function takes a variable number of arguments.  That argument becomes
a list containing all remaining function arguments (zero or more).

Argument variables are assignable inside the function body.

    ParameterList       ::= "(" [ OneOrMoreParameters ] ")"

    OneOrMoreParameters ::= ListParameter                           |
                            DefaultParameters [ "," ListParameter ] |
                            Parameters [ "," DefaultParameters ] [ "," ListParameter ]

    Parameters          ::= Parameter ( "," Parameter )*

    DefaultParameters   ::= DefaultParameter ( "," DefaultParameter )*

    DefaultParameter    ::= ParameterName "=" RHSExpression

    ListParameter       ::= ParameterName "..."

    Parameter           ::= ParameterName | "_"

    ParameterName       ::= Identifier


Class statement
---------------

    ClassDeclaration   ::= "class" Identifier [ ExtendsDeclaration ] ClassBody

    ExtendsDeclaration ::= "extends" MemberExpression

    ClassBody          ::= "{" ( ClassMember )* "}"

    ClassMember        ::= ConstructorDeclaration | FunctionDeclaration | DataMember | ";"

    DataMember         ::= "var" Identifier "=" RHSExpression


Constructor literal
-------------------

Constructor function is a special type of function which serves the purpose of
creating new objects.

The constructor function is invoked when creating an object of the class to
which that constructor belongs.

When a constructor function is invoked, a new object is created and bound to
`this`, even if the construction function is invoked on an object, with the
exception of the `apply` function.  If the `apply` function is invoked on
a constructor function, the new object is not created, but `this` is bound
to the first argument of `apply`.

The `return` statement inside a constructor function can only return `this`
or nothing.  No other value can be returned.

The `yield` operator is not allowed inside a constructor function.

    ConstructorDeclaration ::= "constructor" [ ParameterList ] CompoundStatement


Do statement
------------

The `do` statement can be used between other statements inside a compound
statement to create an individual sub-scope.  Variables declared inside this
internal scope will not be visible outside of it.

    DoStatement ::= "do" CompoundStatement


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

The try statement wraps a compound statement, followed by a catch block.

If an exception is thrown inside the try block and is not caught inside it,
the catch block is executed.  The exception object is assigned to the variable
declared by the catch block.  The parentheses around the variable declaration
are optional.  The lifetime of that variable is limited to the catch block.

An exception can be rethrown using the throw statement in the catch block.

    TryStatement        ::= TryClause CatchClause

    TryClause           ::= "try" CompoundStatement

    CatchClause         ::= "catch" CatchExceptionSpec CompoundStatement

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

If there are multiple defer statements in a scope, all of them are executed
in the reverse order of declaration.


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

The following examples are equivalent:

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


Switch statement
----------------

The switch statement defines multiple execution paths depending of the value of
an evaluated expression.

The switch statement consists of an expression and case sections executed
depending on the value of that expression.  The parentheses around the
expression are optional (they are treated as part of the expression).

Each new case section begins with a "case" keyword followed by one or more
right-hand-side expressions separated with comas followed by a colon, or with
"default" keyword followed by a colon, and is followed by at least one statement.

Each case section constitutes a new scope.  Variables declared inside a case
section are not visible in other case sections.

The "default" case is optional.

There can be only one "default" case defined for a given switch statement.

The "default" case does not have to be specified last.

If the value of the switch expression is equal to one of the right-hand-side
expressions, the corresponding compound statement is executed.

If the value of the switch expressions does not match any right-hand-side
expression, then the compound statement following `default` is
executed, if it exists, otherwise the switch statement terminates.

If a fallthrough statement is found inside any case section, the switch
statement does not terminate, but the execution continues in the next case
section, regardless of whether the switch expression matches the condition
in that case statement or not.

A fallthrough statement is not allowed in the last case section.

    SwitchStatement ::= "switch" RHSExpression
                        "{" ( SwitchCase )* [ DefaultCase ] ( SwitchCase )* "}"

    SwitchCase      ::= "case" CaseSpec [ ":" ] "{" Statement ( Statement )* "}"

    DefaultCase     ::= "default" [ ":" ] "{" Statement ( Statement )* "}"

    CaseSpec        ::= RHSExpression ( "," RHSExpression )*


Loop statement
--------------

The loop statement has a compound statement, which is exectued forever in
a loop.  The loop can be interrupted with `break` or `continue`.

    LoopStatement ::= "loop" CompoundStatement


Repeat-while statement
------------------

The repeat-while statement consists of a compound statement and a condition.
It executes the compound statement as long as the condition is truthy.
The condition is evaluated after each iteration of the compound statement.

The parentheses around the condition expression are optional - they are part
of the expression.

    RepeatWhileStatement ::= "repeat" CompoundStatement
                             "while" RHSExpression OptSemicolon


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

The for statement consists of a control expression and a compound
statement.  The loop control expression declares variables and an expression,
which shall produce a container over which the loop will iterate.

    ForStatement ::= "for" ForLoopControl CompoundStatement

    ForLoopControl ::= ForInExpression
                     | ( "(" ForInExpression ")" )

    ForInExpression ::= ( VarList | ConstList | IdentifierList ) "in" RHSExpression

Variables declared in the loop control expression are visible
only within the compound statement.

Before the first loop commences, the expression is evaluated and
a generator is extracted from its result.  For example, if the resulting
container is an array, the generator will go over all elements of the array.

If a generator cannot be extracted, an exception is thrown.

For every loop, a new item from the generator is assigned to the variables.

If there is a single variable, the item is assigned to it.  If there are
multiple variables, consecutive elements of the item are assigned to them by
iterating over the item.  If the item does not support extracting elements,
an exception is thrown.


Continue statement
------------------

The continue statement is legal only inside for, loop, while or repeat-while
loops.  If used in the global scope or in a function outside of the loop
statements, it will produce a compilation error.

The statement causes a jump to the end of the compound statement of
the innermost loop when executed.

    ContinueStatement ::= "continue" OptSemicolon


Break statement
---------------

The break statement is legal only inside for, loop, while, repeat-while and
switch statement.  If used in the global scope or in a function outside of
one of the above statements, it will produce a compilation error.

The statement interrupts the loop or switch statement and causes the execution
flow to contine after the loop or switch statement.

    BreakStatement ::= "break" OptSemicolon


Fallthrough statement
---------------------

The fallthrough statement is legal only inside a switch statement.  It can
occur at any place inside a switch statement, except the last case/default
block.

The fallthrough statement causes execution to continue in the next case
block even if the switch's tested expression doesn't match the case.

    FallthroughStatement ::= "fallthrough" OptSemicolon


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

The optional right-hand-side expression can only be `this` if it is
inside a constructor function.

If the optional right-hand-side expression is specified in a generator
function, it can only be `void` or resolve to `void` at compile time.

    ReturnStatement ::= "return" [ RHSExpression ] OptSemicolon


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

    ThrowStatement ::= "throw" RHSExpression OptSemicolon


Assert statement
----------------

The assert statement checks a condition expression, and if the condition is
not truthy, it throws an exception.

    AssertStatement ::= "assert" RHSExpression OptSemicolon

The following two pieces of code are equivalent:

    assert n > 0;

    if ! (n > 0) {
        throw "Assertion failed: n > 0";
    }


Public statement
----------------

The public statement declares global variables exported from the module.
These variables can be accessed from other modules.

Any variables declared without the public statement are not visible
outside of the current module.

    PublicStatement ::= "public" PublicDeclaration

    PublicDeclaration ::= FunctionDeclaration
                        | ClassDeclaration
                        | VariableDefinitionExpression


Expressions
-----------

    Expression                   ::= AssignmentExpression |
                                     ArithAssignmentExpression |
                                     VariableDefinitionExpression |
                                     RHSExpression

    AssignmentExpression         ::= AssignmentTargetList AssignmentOperator RHSExpression

    ArithAssignmentExpression    ::= MutableAssignmentTarget ArithAssignmentOperator RHSExpression

    VariableDefinitionExpression ::= ( VarList | ConstList ) "=" RHSExpression

    VarList ::= "var" IdentifierList

    ConstList ::= "const" IdentifierList

    IdentifierList ::= Identifier
                     | MultipleIdentifiers

    MultipleIdentifiers ::= IdentifierOrPlaceholder "," IdentifierOrPlaceholder ( "," IdentifierOrPlaceholder )*

    IdentifierOrPlaceholder ::= Identifier
                              | PLACEHOLDER_LITERAL

    AssignmentTargetList ::= MutableAssignmentTarget
                           | MultiAssignmentTargetList

    MultiAssignmentTargetList ::= AssignmentTarget "," AssignmentTarget ( "," AssignmentTarget )*

    MutableAssignmentTarget ::= Identifier
                              | ( MemberExpression Refinement )

    AssignmentTarget ::= MutableAssignmentTarget |
                         PLACEHOLDER_LITERAL

    RHSExpression ::= StreamExpression
                    | YieldExpression

    YieldExpression ::= "yield" StreamExpression

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
                             | ConcatExpression

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

    ArithExpression ::= AdditiveExpression

    AdditiveExpression ::= MultiplicativeExpression
                           ( AdditiveOperator MultiplicativeExpression )*

    ConcatExpression ::= UnaryExpression ConcatOperator UnaryExpression
                         ( ConcatOperator UnaryExpression )*

To avoid ambiguities, the `AdditiveOperator` cannot be the first non-whitespace,
non-comment token on the line if the `AdditiveExpression` is part of the
outermost `RHSExpression`.

    MultiplicativeExpression ::= UnaryExpression
                                 ( MultiplicativeOperator UnaryExpression )*

    UnaryExpression ::= UnaryOperatorExpression

    UnaryOperatorExpression ::= ( UnaryOperator )* MemberExpression


Operators
---------

    AssignmentOperator      ::= "="

    ArithAssignmentOperator ::= "+="
                              | "-="
                              | "++="
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
                              | "propertyof"

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

    ConcatOperator          ::= "++"

    UnaryOperator           ::= "typeof"
                              | "delete"
                              | "+"
                              | "-"
                              | "~"
                              | "!"


Member specification
--------------------

    MemberExpression ::= PrimaryExpression
                       | FunctionLiteral
                       | ClassLiteral
                       | ( MemberExpression Invocation )
                       | ( MemberExpression Refinement )

    FunctionLiteral ::= SimpleFunctionLiteral
                      | CompoundFunctionLiteral

    SimpleFunctionLiteral   ::= ( Identifier | "_" | ParameterList ) "=>" RHSExpression

    CompoundFunctionLiteral ::= "fun" [ ParameterList ] CompoundStatement

    ClassLiteral ::= "class" [ ExtendsDeclaration ] ClassBody

    Invocation ::= "(" [ ArgumentList ] ")"

To avoid ambiguities, the opening parenthesis `(` cannot be the first
non-whitespace, non-comment token on the line if the `Invocation` is
part of the outermost `MemberExpression`.

    ArgumentList ::= NamedArgumentList | UnnamedArgumentList

    NamedArgumentList ::= NamedArgument ( "," NamedArgument )*

    NamedArgument ::= Identifier "=" RHSExpression

    UnnamedArgumentList ::= UnnamedArgument ( "," UnnamedArgument )*

    UnnamedArgument ::= RHSExpression [ "..." ]

    Refinement ::= ( "[" [ "?" ] RHSExpression "]" )
                 | ( "[" [ RHSExpression ] ":" [ RHSExpression ] "]" )
                 | ( "." [ "?" ] Identifier )
                 | ( "." [ "?" ] StringLiteral )

To avoid ambiguities, the opening square bracket `[` cannot be the first
non-whitespace, non-comment token on the line if the `Refinement` is
part of the outermost `MemberExpression`.

    PrimaryExpression ::= "this"
                        | "super"
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

    ArrayLiteral ::= "[" [ ArrayElement ( "," ArrayElement )* [ "," ] ] "]"

    ArrayElement ::= RHSExpression [ "..." ]

    ObjectLiteral ::= "{" [ PropertyList ] "}"

    PropertyList ::= PropertyDefinition ( "," [ PropertyDefinition ] )*

    PropertyDefinition ::= PropertyName ":" RHSExpression

    PropertyName ::= StringLiteral
                   | Identifier

    StringLiteral ::= STRING_LITERAL
                    | FormattedDoubleQuotedString

    FormattedDoubleQuotedString ::= STRING_LITERAL_BEGIN
                                    RHSExpression
                                    ( ")" STRING_LITERAL_CONT RHSExpression )*
                                    ")" STRING_LITERAL_END


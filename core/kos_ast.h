/*
 * Copyright (c) 2014-2019 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef KOS_AST_H_INCLUDED
#define KOS_AST_H_INCLUDED

#include "kos_lexer.h"

typedef enum KOS_NODE_TYPE_U {
    NT_EMPTY,
    NT_IMPORT,
    NT_SCOPE,               /* compound statement */
    NT_IF,
    NT_TRY_CATCH,
    NT_TRY_DEFER,
    NT_SWITCH,
    NT_REPEAT,
    NT_FOR,
    NT_FOR_IN,
    NT_CONTINUE,
    NT_BREAK,
    NT_RETURN,
    NT_THROW,
    NT_ASSERT,

    NT_REFINEMENT,
    NT_SLICE,
    NT_INVOCATION,
    NT_VAR,
    NT_CONST,
    NT_OPERATOR,
    NT_YIELD,
    NT_ASYNC,
    NT_ASSIGNMENT,          /* single variable assignment */
    NT_MULTI_ASSIGNMENT,    /* multiple variable assignment */
    NT_INTERPOLATED_STRING, /* string interpolation */

    NT_LEFT_HAND_SIDE,      /* first argument to assignment */
    NT_NAME,                /* function name, not referred to inside the function */
    NT_NAME_CONST,          /* function name, usable inside the function */
    NT_PARAMETERS,          /* function arguments list */
    NT_ELLIPSIS,            /* last, unbounded function argument */
    NT_EXPAND,              /* expanded argument or array element */
    NT_PROPERTY,            /* property definition in object literal */
    NT_IN,                  /* the 'in' part of the for-in loop */
    NT_EXPRESSION_LIST,     /* pre-loop or post-loop expressions in for loop */
    NT_CATCH,               /* catch clause in a try statement */
    NT_DEFAULT,             /* a default section in a switch statement */
    NT_CASE,                /* a specific case section in a switch statement */
    NT_FALLTHROUGH,         /* a fallthrough statement at the end of a case section */
    NT_LANDMARK,            /* auxiliary node to save location of other tokens, e.g. '{' */

    NT_IDENTIFIER,
    NT_NUMERIC_LITERAL,
    NT_STRING_LITERAL,
    NT_THIS_LITERAL,
    NT_SUPER_CTOR_LITERAL,
    NT_SUPER_PROTO_LITERAL,
    NT_LINE_LITERAL,
    NT_BOOL_LITERAL,
    NT_VOID_LITERAL,
    NT_FUNCTION_LITERAL,
    NT_CONSTRUCTOR_LITERAL,
    NT_CLASS_LITERAL,
    NT_ARRAY_LITERAL,
    NT_OBJECT_LITERAL
} KOS_NODE_TYPE;

typedef struct KOS_AST_NODE_S {
    struct KOS_AST_NODE_S *next;
    struct KOS_AST_NODE_S *children;
    struct KOS_AST_NODE_S *last_child;
    KOS_TOKEN              token;
    KOS_NODE_TYPE          type;
} KOS_AST_NODE;

#endif

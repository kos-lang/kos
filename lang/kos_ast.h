/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#ifndef __KOS_AST_H
#define __KOS_AST_H

#include "kos_lexer.h"

enum _KOS_NODE_TYPE {
    NT_EMPTY,
    NT_IMPORT,
    NT_TRY_IMPORT,
    NT_SCOPE,               /* compound statement */
    NT_IF,
    NT_TRY,
    NT_SWITCH,
    NT_DO,
    NT_WHILE,
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
    NT_ASSIGNMENT,          /* single variable assignment */
    NT_MULTI_ASSIGNMENT,    /* multiple variable assignment */
    NT_INTERPOLATED_STRING, /* string interpolation */

    NT_LEFT_HAND_SIDE,      /* first argument to assignment */
    NT_PARAMETERS,          /* function arguments list */
    NT_ELLIPSIS,            /* last, unbounded function argument */
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
    NT_LINE_LITERAL,
    NT_BOOL_LITERAL,
    NT_VOID_LITERAL,
    NT_FUNCTION_LITERAL,
    NT_ARRAY_LITERAL,
    NT_OBJECT_LITERAL
};

struct _KOS_AST_NODE {
    struct _KOS_AST_NODE *next;
    struct _KOS_AST_NODE *children;
    struct _KOS_AST_NODE *last_child;
    struct _KOS_TOKEN     token;
    enum _KOS_NODE_TYPE   type;
};

#endif

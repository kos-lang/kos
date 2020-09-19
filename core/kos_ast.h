/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
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
    NT_EXPORT,
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

struct KOS_VAR_S;
struct KOS_SCOPE_S;

typedef struct KOS_AST_NODE_S {
    struct KOS_AST_NODE_S *next;
    struct KOS_AST_NODE_S *children;
    struct KOS_AST_NODE_S *last_child;
    struct KOS_VAR_S      *var;
    struct KOS_SCOPE_S    *var_scope;
    KOS_TOKEN              token;
    KOS_NODE_TYPE          type         : 8;
    unsigned               is_local_var : 1;
} KOS_AST_NODE;

#endif

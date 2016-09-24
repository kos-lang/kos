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

#include "kos_compiler.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <string.h>

enum _KOS_TERMINATOR {
    TERM_NONE   = 0,
    TERM_BREAK  = 1,
    TERM_THROW  = 2,
    TERM_RETURN = 4
};

static int _visit_node(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *node,
                       int                   *is_terminal);

static void _collapse(struct _KOS_AST_NODE  *node,
                      enum _KOS_NODE_TYPE    node_type,
                      enum _KOS_TOKEN_TYPE   token_type,
                      enum _KOS_KEYWORD_TYPE keyword,
                      const char            *begin,
                      unsigned               length)
{
    node->children      = 0;
    node->type          = node_type;
    node->token.type    = token_type;
    node->token.keyword = keyword;
    node->token.op      = OT_NONE;
    node->token.sep     = ST_NONE;
    if (begin && length) {
        node->token.begin  = begin;
        node->token.length = length;
    }
}

static int _scope(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *node,
                  int                   *is_terminal)
{
    int error = KOS_SUCCESS;

    *is_terminal = TERM_NONE;

    for (node = node->children; node; node = node->next) {

        error = _visit_node(program, node, is_terminal);
        if (error)
            break;

        if (*is_terminal)
            /* TODO inform about unreachable code */
            node->next = 0;
    }

    return error;
}

static int _if(struct _KOS_COMP_UNIT *program,
               struct _KOS_AST_NODE  *node,
               int                   *is_terminal)
{
    int error = KOS_SUCCESS;
    int t1;
    int t2;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t1));

    /* TODO Optimize if statement if the condition is a constant */

    node = node->next;
    assert(node);

    *is_terminal = TERM_NONE;

    if (node->next) {
        assert( ! node->next->next);
        TRY(_visit_node(program, node, &t1));
        TRY(_visit_node(program, node->next, &t2));

        if (t1 && t2)
            *is_terminal = t1 | t2;
    }

_error:
    return error;
}

static int _do(struct _KOS_COMP_UNIT *program,
               struct _KOS_AST_NODE  *node,
               int                   *is_terminal)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);
    assert(node->next);
    assert( ! node->next->next);
    TRY(_visit_node(program, node, is_terminal));

    if (*is_terminal)
        _collapse(node->next, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
    else {
        int t;
        TRY(_visit_node(program, node->next, &t));
        assert(t == TERM_NONE);
    }

    if (*is_terminal & TERM_BREAK)
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _while(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *node,
                  int                   *is_terminal)
{
    int error = KOS_SUCCESS;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    /* TODO unreachable if condition is always falsy */

    node = node->next;
    assert(node);
    assert( ! node->next);
    TRY(_visit_node(program, node, is_terminal));

    /* TODO allow return/throw only if condition is always truthy
    if (*is_terminal & TERM_BREAK)
    */
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _for(struct _KOS_COMP_UNIT *program,
                struct _KOS_AST_NODE  *node,
                int                   *is_terminal)
{
    int error = KOS_SUCCESS;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));

    /* TODO unreachable if condition is always falsy */

    node = node->next;
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    node = node->next;
    assert( ! node->next);
    TRY(_visit_node(program, node, &t));

    /* TODO allow return/throw only if condition is always truthy
    if (*is_terminal & TERM_BREAK)
    */
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _try(struct _KOS_COMP_UNIT *program,
                struct _KOS_AST_NODE  *node,
                int                   *is_terminal)
{
    int error = KOS_SUCCESS;
    int t1, t2, t3;

    struct _KOS_AST_NODE *finally_node;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t1));

    node = node->next;
    assert(node);
    TRY(_visit_node(program, node, &t2));

    finally_node = node->next;
    assert(finally_node);
    assert( ! finally_node->next);
    TRY(_visit_node(program, finally_node, &t3));

    *is_terminal = TERM_NONE;

    if (finally_node->type == NT_EMPTY || ! t3) {
        if (t1 && t2)
            *is_terminal = (t1 & ~TERM_THROW) | t2;
    }
    else
        *is_terminal = t3;

_error:
    return error;
}

static int _switch(struct _KOS_COMP_UNIT *program,
                   struct _KOS_AST_NODE  *node,
                   int                   *is_terminal)
{
    int error          = KOS_SUCCESS;
    int num_cases      = 0;
    int num_terminated = 0;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    t = TERM_NONE;

    for (node = node->next; node; node = node->next) {

        int next_t;

        TRY(_visit_node(program, node, &next_t));

        ++num_cases;
        if (next_t) {
            ++num_terminated;
            t |= next_t;
        }
    }

    *is_terminal = (num_cases == num_terminated) ? t : TERM_NONE;

_error:
    return error;
}

static int _visit_child_nodes(struct _KOS_COMP_UNIT *program,
                              struct _KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;
    int t     = TERM_NONE;

    for (node = node->children; node; node = node->next)
        TRY(_visit_node(program, node, &t));

_error:
    return error;
}

static int _visit_node(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *node,
                       int                   *is_terminal)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {

        case NT_RETURN:
            error = _visit_child_nodes(program, node);
            *is_terminal = TERM_RETURN;
            break;

        case NT_THROW:
            error = _visit_child_nodes(program, node);
            *is_terminal = TERM_THROW;
            break;

        case NT_BREAK:
            /* fall through */
        case NT_CONTINUE:
            error = _visit_child_nodes(program, node);
            *is_terminal = TERM_BREAK;
            break;

        case NT_SCOPE:
            error = _scope(program, node, is_terminal);
            break;
        case NT_IF:
            error = _if(program, node, is_terminal);
            break;
        case NT_DO:
            error = _do(program, node, is_terminal);
            break;
        case NT_WHILE:
            error = _while(program, node, is_terminal);
            break;
        case NT_FOR:
            error = _for(program, node, is_terminal);
            break;
        case NT_TRY:
            error = _try(program, node, is_terminal);
            break;
        case NT_SWITCH:
            error = _switch(program, node, is_terminal);
            break;

        case NT_EMPTY:
            /* fall through */
        case NT_IMPORT:
            /* fall through */
        case NT_TRY_IMPORT:
            /* fall through */
        case NT_FOR_IN:
            /* fall through */
        case NT_ASSERT:
            /* fall through */
        case NT_REFINEMENT:
            /* fall through */
        case NT_SLICE:
            /* fall through */
        case NT_INVOCATION:
            /* fall through */
        case NT_VAR:
            /* fall through */
        case NT_CONST:
            /* fall through */
        case NT_OPERATOR:
            /* fall through */
        case NT_YIELD:
            /* fall through */
        case NT_ASSIGNMENT:
            /* fall through */
        case NT_MULTI_ASSIGNMENT:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            /* fall through */
        case NT_LEFT_HAND_SIDE:
            /* fall through */
        case NT_PARAMETERS:
            /* fall through */
        case NT_ELLIPSIS:
            /* fall through */
        case NT_PROPERTY:
            /* fall through */
        case NT_IN:
            /* fall through */
        case NT_EXPRESSION_LIST:
            /* fall through */
        case NT_CATCH:
            /* fall through */
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            /* fall through */
        case NT_FALLTHROUGH:
            /* fall through */
        case NT_LANDMARK:
            /* fall through */
        case NT_IDENTIFIER:
            /* fall through */
        case NT_NUMERIC_LITERAL:
            /* fall through */
        case NT_STRING_LITERAL:
            /* fall through */
        case NT_THIS_LITERAL:
            /* fall through */
        case NT_LINE_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            /* fall through */
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_ARRAY_LITERAL:
            /* fall through */
        case NT_OBJECT_LITERAL:
            error        = _visit_child_nodes(program, node);
            *is_terminal = TERM_NONE;
            break;

        default:
            assert(0);
            break;
    }

    return error;
}

int _KOS_optimize(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *ast)
{
    /* TODO optimize
       - fold constants
       - fold constant variables
     */
    int t;
    assert(ast->type == NT_SCOPE);
    return _visit_node(program, ast, &t);
}

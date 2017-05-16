/*
 * Copyright (c) 2014-2017 Chris Dragan
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
#include "kos_misc.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char str_err_div_by_zero[] = "division by zero";

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

static int _collapse_numeric(struct _KOS_COMP_UNIT     *program,
                             struct _KOS_AST_NODE      *node,
                             const struct _KOS_NUMERIC *value)
{
    int                  error;
    const unsigned       length = sizeof(struct _KOS_NUMERIC);
    struct _KOS_NUMERIC *store  = (struct _KOS_NUMERIC *)
                                  _KOS_mempool_alloc(&program->allocator, length);

    if (store) {

        *store = *value;

        _collapse(node, NT_NUMERIC_LITERAL, TT_NUMERIC_BINARY, KW_NONE, (const char *)store, length);

        ++program->num_optimizations;

        error = KOS_SUCCESS;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void _promote(struct _KOS_COMP_UNIT      *program,
                     struct _KOS_AST_NODE       *node,
                     const struct _KOS_AST_NODE *child)
{
    struct _KOS_SCOPE *scope = (struct _KOS_SCOPE *)
            _KOS_red_black_find(program->scopes, (void *)child, _KOS_scope_compare_item);

    assert( ! _KOS_red_black_find(program->scopes, (void *)node, _KOS_scope_compare_item));

    if (scope) {

        _KOS_red_black_delete(&program->scopes, (struct _KOS_RED_BLACK_NODE *)scope);

        assert(scope->scope_node == child);
        scope->scope_node = node;

        _KOS_red_black_insert(&program->scopes,
                              (struct _KOS_RED_BLACK_NODE *)scope,
                              _KOS_scope_compare_node);
    }

    node->children   = child->children;
    node->last_child = child->last_child;
    node->token      = child->token;
    node->type       = child->type;
}

static int _is_nonzero(const struct _KOS_TOKEN *token)
{
    struct _KOS_NUMERIC numeric;

    assert(token->length > 0);

    if (token->type == TT_NUMERIC_BINARY) {

        const struct _KOS_NUMERIC *value = (const struct _KOS_NUMERIC *)token->begin;

        assert(token->length == sizeof(struct _KOS_NUMERIC));

        numeric = *value;
    }
    else {

        const char c = *token->begin;

        assert(token->type == TT_NUMERIC || token->type == TT_NUMERIC_BINARY);

        if (c >= '1' && c <= '9')
            return 1;

        if (c == '0' && token->length == 1)
            return 0;

        if (KOS_SUCCESS != _KOS_parse_numeric(token->begin,
                                              token->begin + token->length,
                                              &numeric))
            return 1; /* Treat invalid or non-numeric value as non-zero */
    }

    if (numeric.type == KOS_INTEGER_VALUE)
        return numeric.u.i ? 1 : 0;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        return numeric.u.d == 0.0 ? 0 : 1;
    }
}

static void _lookup_var(struct _KOS_COMP_UNIT   *program,
                        const struct _KOS_TOKEN *token,
                        int                      only_active,
                        struct _KOS_VAR        **out_var,
                        int                     *is_local)
{
    struct _KOS_SCOPE *scope     = program->scope_stack;
    struct _KOS_SCOPE *fun_scope = 0;
    struct _KOS_VAR   *var       = 0;

    assert(scope);

    for (scope = program->scope_stack; scope->next && ! scope->is_function; scope = scope->next) {

        var = _KOS_find_var(scope->vars, token);

        if (var && (var->is_active || ! only_active)) {
            *out_var  = var;
            *is_local = 1;
            return;
        }
    }

    assert( ! scope->next || scope->is_function);

    if (scope->is_function)
        fun_scope = scope;

    *is_local = 0;

    if ( ! scope->next && ! only_active) {

        var = _KOS_find_var(scope->vars, token);

        if (var) {
            *is_local = 1; /* This is global scope, but mark it as "local" access */
            scope     = 0;
        }
    }

    for ( ; scope; scope = scope->next) {

        var = _KOS_find_var(scope->vars, token);

        if (var && var->is_active) {
            *is_local = scope == fun_scope ? 1 : 0; /* current func's arguments are "local" */
            break;
        }

        var = 0;
    }

    assert(var);

    *out_var = var;
}

const struct _KOS_AST_NODE *_KOS_get_const(struct _KOS_COMP_UNIT      *program,
                                           const struct _KOS_AST_NODE *node)
{
    struct _KOS_VAR *var = 0;
    int              is_local;

    if ( ! node || node->type != NT_IDENTIFIER)
        return node;

    _lookup_var(program, &node->token, 1, &var, &is_local);

    return var->is_const ? var->value : 0;
}

int _KOS_node_is_truthy(struct _KOS_COMP_UNIT      *program,
                        const struct _KOS_AST_NODE *node)
{
    node = _KOS_get_const(program, node);

    if ( ! node)
        return 0;

    if ((node->type == NT_BOOL_LITERAL    && node->token.keyword == KW_TRUE) ||
        (node->type == NT_NUMERIC_LITERAL && node->token.length == 1 && *node->token.begin != '0') ||
        (node->type == NT_NUMERIC_LITERAL && _is_nonzero(&node->token)) ||
        (node->type == NT_STRING_LITERAL)      ||
        (node->type == NT_FUNCTION_LITERAL)    ||
        (node->type == NT_CONSTRUCTOR_LITERAL) ||
        (node->type == NT_ARRAY_LITERAL)       ||
        (node->type == NT_OBJECT_LITERAL)      ||
        (node->type == NT_INTERPOLATED_STRING)) {

        return 1;
    }

    return 0;
}

int _KOS_node_is_falsy(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node)
{
    node = _KOS_get_const(program, node);

    if ( ! node)
        return 0;

    if ((node->type == NT_BOOL_LITERAL    && node->token.keyword == KW_FALSE) ||
        (node->type == NT_NUMERIC_LITERAL && node->token.length == 1 && *node->token.begin == '0') ||
        (node->type == NT_NUMERIC_LITERAL && ! _is_nonzero(&node->token)) ||
        (node->type == NT_VOID_LITERAL)) {

        return 1;
    }

    return 0;
}

static int _reset_var_state(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_VAR *var = (struct _KOS_VAR *)node;

    if (var->is_active == VAR_ACTIVE)
        var->is_active = VAR_INACTIVE;

    var->num_reads         = 0;
    var->num_assignments   = 0;
    var->local_reads       = 0;
    var->local_assignments = 0;

    return KOS_SUCCESS;
}

static struct _KOS_SCOPE *_push_scope(struct _KOS_COMP_UNIT      *program,
                                      const struct _KOS_AST_NODE *node)
{
    struct _KOS_SCOPE *scope = (struct _KOS_SCOPE *)
            _KOS_red_black_find(program->scopes, (void *)node, _KOS_scope_compare_item);

    assert(scope);

    assert(scope->next == program->scope_stack);

    _KOS_red_black_walk(scope->vars, _reset_var_state, 0);

    program->scope_stack = scope;

    scope->num_vars       = 0;
    scope->num_indep_vars = 0;
    scope->num_args       = 0;
    scope->num_indep_args = 0;

    return scope;
}

static int _count_and_update_vars(struct _KOS_RED_BLACK_NODE *node,
                                  void                       *cookie)
{
    struct _KOS_VAR   *var   = (struct _KOS_VAR *)node;
    struct _KOS_SCOPE *scope = (struct _KOS_SCOPE *)cookie;

    /* Change to const if the variable was never modified */
    if ((var->type & VAR_LOCALS_AND_ARGS) && ! var->is_const && ! var->num_assignments) {

        var->is_const = 1;
    }

    /* Demote independent vars and args if they are never accessed from closures */
    if ((var->type & VAR_INDEPENDENT)            &&
        var->num_reads       == var->local_reads &&
        var->num_assignments == var->local_assignments) {

        assert(var->type == VAR_INDEPENDENT_LOCAL || var->type == VAR_INDEPENDENT_ARGUMENT);
        var->type = (var->type == VAR_INDEPENDENT_ARGUMENT) ? VAR_ARGUMENT : VAR_LOCAL;
    }

    /* Count arguments */
    if (var->type & VAR_ARGUMENT) {
        assert(scope->is_function || ! scope->next);

        ++scope->num_args;

        if (var->type == VAR_INDEPENDENT_ARGUMENT)
            ++scope->num_indep_args;
    }
    /* Count only used local variables */
    else if ((var->type & VAR_LOCAL) && var->num_reads) {
        assert( ! (var->type & VAR_ARGUMENT) || var == scope->ellipsis);

        ++scope->num_vars;

        if (var->type == VAR_INDEPENDENT_LOCAL)
            ++scope->num_indep_vars;
    }

    return KOS_SUCCESS;
}

static void _pop_scope(struct _KOS_COMP_UNIT *program)
{
    struct _KOS_SCOPE *scope = program->scope_stack;

    assert(scope);

    _KOS_red_black_walk(scope->vars, _count_and_update_vars, scope);

    if ( ! scope->is_function && scope->next) {
        scope->next->num_vars       += scope->num_vars;
        scope->next->num_indep_vars += scope->num_indep_vars;
    }

    program->scope_stack = scope->next;
}

static int _scope(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *node,
                  int                   *is_terminal)
{
    int error = KOS_SUCCESS;

    _push_scope(program, node);

    *is_terminal = TERM_NONE;

    for (node = node->children; node; node = node->next) {

        error = _visit_node(program, node, is_terminal);
        if (error)
            break;

        if (*is_terminal && program->optimize && node->next) {
            node->next = 0;
            ++program->num_optimizations;
        }
    }

    _pop_scope(program);

    return error;
}

static int _if_stmt(struct _KOS_COMP_UNIT *program,
                    struct _KOS_AST_NODE  *node,
                    int                   *is_terminal)
{
    int error     = KOS_SUCCESS;
    int is_truthy = 0;
    int is_falsy  = 0;
    int t1;
    int t2;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t1));

    if (program->optimize) {
        is_truthy = _KOS_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : _KOS_node_is_falsy(program, node);
    }

    assert(node->next);

    if (is_truthy) {
        if (node->next->next) {
            node->next->next = 0;
            ++program->num_optimizations;
        }
    }
    else if (is_falsy) {
        _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_TRUE, 0, 0);
        if (node->next->next)
            node->next = node->next->next;
        else
            _collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
        is_truthy = 1;
    }

    node = node->next;

    *is_terminal = TERM_NONE;

    TRY(_visit_node(program, node, &t1));

    if (node->next) {
        assert( ! node->next->next);
        TRY(_visit_node(program, node->next, &t2));

        if (t1 && t2)
            *is_terminal = t1 | t2;
    }
    else if (is_truthy)
        *is_terminal = t1;

_error:
    return error;
}

static int _repeat_stmt(struct _KOS_COMP_UNIT *program,
                        struct _KOS_AST_NODE  *node,
                        int                   *is_terminal)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, is_terminal));

    node = node->next;
    assert(node);
    assert( ! node->next);

    if (*is_terminal && program->optimize) {
        if (node->token.keyword != KW_FALSE) {
            _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
            ++program->num_optimizations;
        }
    }
    else {
        int t;
        TRY(_visit_node(program, node, &t));
        assert(t == TERM_NONE);
    }

    if (*is_terminal & TERM_BREAK)
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _while_stmt(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *node,
                       int                   *is_terminal)
{
    int error     = KOS_SUCCESS;
    int is_truthy = 0;
    int is_falsy  = 0;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    if (program->optimize) {
        is_truthy = _KOS_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : _KOS_node_is_falsy(program, node);
    }

    node = node->next;
    assert(node);
    assert( ! node->next);

    if (is_falsy && node->type != NT_EMPTY) {
        _collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    TRY(_visit_node(program, node, is_terminal));

    if ( ! is_truthy || (*is_terminal & TERM_BREAK))
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _for_stmt(struct _KOS_COMP_UNIT *program,
                     struct _KOS_AST_NODE  *node,
                     int                   *is_terminal)
{
    int error     = KOS_SUCCESS;
    int is_truthy = 0;
    int is_falsy  = 0;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    if (program->optimize) {
        is_truthy = _KOS_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : _KOS_node_is_falsy(program, node);
    }

    node = node->next;
    assert(node);
    if (is_falsy && node->type != NT_EMPTY) {
        _collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    assert(node->next);
    assert( ! node->next->next);

    if (is_falsy && node->next->type != NT_EMPTY) {
        _collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    TRY(_visit_node(program, node->next, &t));

    if (*is_terminal && program->optimize && node->type != NT_EMPTY) {
        _collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    if ( ! is_truthy || (*is_terminal & TERM_BREAK))
        *is_terminal = TERM_NONE;

_error:
    return error;
}

static int _try_stmt(struct _KOS_COMP_UNIT *program,
                     struct _KOS_AST_NODE  *node,
                     int                   *is_terminal)
{
    int error = KOS_SUCCESS;
    int t1;
    int t2    = TERM_NONE;
    int t3;

    struct _KOS_AST_NODE *finally_node;

    _push_scope(program, node);

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t1));

    node = node->next;
    assert(node);
    if (node->type == NT_CATCH) {

        struct _KOS_AST_NODE *var_node = node->children;
        struct _KOS_AST_NODE *scope_node;
        struct _KOS_VAR      *var;
        int                   is_local;

        assert(var_node);
        assert(var_node->type == NT_VAR || var_node->type == NT_CONST);

        scope_node = var_node->next;
        assert(scope_node);
        assert( ! scope_node->next);
        assert(scope_node->type == NT_SCOPE);

        var_node = var_node->children;
        assert(var_node);
        assert( ! var_node->children);
        assert( ! var_node->next);
        assert(var_node->type == NT_IDENTIFIER);

        _lookup_var(program, &var_node->token, 0, &var, &is_local);

        assert(var);
        assert(var->is_active == VAR_INACTIVE);

        var->is_active = VAR_ACTIVE;

        TRY(_visit_node(program, scope_node, &t2));

        var->is_active = VAR_INACTIVE;
    }
    else {
        assert(node->type == NT_EMPTY);
    }

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

    _pop_scope(program);

_error:
    return error;
}

static int _switch_stmt(struct _KOS_COMP_UNIT *program,
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

static int _for_in_stmt(struct _KOS_COMP_UNIT *program,
                        struct _KOS_AST_NODE  *node)
{
    int error;

    _push_scope(program, node);

    assert(node->children);
    assert(node->children->children);
    _KOS_activate_new_vars(program, node->children->children);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    return error;
}

static int _function_literal(struct _KOS_COMP_UNIT *program,
                             struct _KOS_AST_NODE  *node)
{
    int error;

    _push_scope(program, node);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    return error;
}

static void _identifier(struct _KOS_COMP_UNIT      *program,
                        const struct _KOS_AST_NODE *node)
{
    struct _KOS_VAR *var;
    int              is_local;

    _lookup_var(program, &node->token, 1, &var, &is_local);

    ++var->num_reads;

    if (is_local)
        ++var->local_reads;
}

static int _assignment(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *node)
{
    int                   error     = KOS_SUCCESS;
    int                   is_lhs;
    int                   t;
    struct _KOS_AST_NODE *lhs_node  = node->children;
    struct _KOS_AST_NODE *rhs_node;
    enum _KOS_NODE_TYPE   assg_type = node->type;

    assert(lhs_node);
    assert(lhs_node->next);
    assert( ! lhs_node->next->next);

    is_lhs = lhs_node->type == NT_LEFT_HAND_SIDE ? 1 : 0;

    assert(lhs_node->type == NT_LEFT_HAND_SIDE ||
           lhs_node->type == NT_VAR            ||
           lhs_node->type == NT_CONST);

    assert((assg_type == NT_ASSIGNMENT       && ! lhs_node->children->next) ||
           (assg_type == NT_MULTI_ASSIGNMENT && lhs_node->children->next));

    node = lhs_node->children;
    assert(node);

    rhs_node = lhs_node->next;
    assert(rhs_node);
    assert( ! rhs_node->next);

    TRY(_visit_node(program, rhs_node, &t));
    assert(t == TERM_NONE);

    /* TODO
     * - optimize multi assignment from array literal (don't create array)
     * - optimize var/const if the variable is never used afterwards
     */

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            struct _KOS_VAR *var = 0;
            int              is_local;

            _lookup_var(program, &node->token, is_lhs, &var, &is_local);

            if ( ! is_lhs) {
                if (var->is_active == VAR_INACTIVE)
                    var->is_active = VAR_ACTIVE;

                if (assg_type == NT_ASSIGNMENT)
                    var->value = rhs_node;
            }
            else {
                assert( ! var->is_const);
                ++var->num_assignments;

                if (is_local)
                    ++var->local_assignments;
            }
        }
        else {
            assert(node->type != NT_LINE_LITERAL && node->type != NT_THIS_LITERAL);
            TRY(_visit_node(program, node, &t));
        }
    }

_error:
    return error;
}

static void _announce_div_by_zero(struct _KOS_COMP_UNIT      *program,
                                  const struct _KOS_AST_NODE *node)
{
    program->error_str   = str_err_div_by_zero;
    program->error_token = &node->token;
}

static int _optimize_binary_op(struct _KOS_COMP_UNIT      *program,
                               struct _KOS_AST_NODE       *node,
                               const struct _KOS_AST_NODE *a,
                               const struct _KOS_AST_NODE *b)
{
    int                     error;
    enum _KOS_OPERATOR_TYPE op = node->token.op;
    struct _KOS_NUMERIC     numeric_a;
    struct _KOS_NUMERIC     numeric_b;

    if (a->token.type == TT_NUMERIC_BINARY) {
        assert(a->token.length == sizeof(struct _KOS_NUMERIC));
        numeric_a = *(const struct _KOS_NUMERIC *)(a->token.begin);
    }
    else if (KOS_SUCCESS != _KOS_parse_numeric(a->token.begin,
                                               a->token.begin + a->token.length,
                                               &numeric_a))
        return KOS_SUCCESS;

    if (b->token.type == TT_NUMERIC_BINARY) {
        assert(b->token.length == sizeof(struct _KOS_NUMERIC));
        numeric_b = *(const struct _KOS_NUMERIC *)(b->token.begin);
    }
    else if (KOS_SUCCESS != _KOS_parse_numeric(b->token.begin,
                                               b->token.begin + b->token.length,
                                               &numeric_b))
        return KOS_SUCCESS;

    /* Convert to the same type, mimicking the VM */
    if (op & OT_ARITHMETIC) {
        assert(op == OT_ADD || op == OT_SUB || op == OT_MUL || op == OT_DIV || op == OT_MOD);

        if (numeric_a.type == KOS_FLOAT_VALUE || numeric_b.type == KOS_FLOAT_VALUE) {

            if (numeric_a.type == KOS_INTEGER_VALUE) {
                numeric_a.u.d  = (double)numeric_a.u.i;
                numeric_a.type = KOS_FLOAT_VALUE;
            }

            if (numeric_b.type == KOS_INTEGER_VALUE) {
                numeric_b.u.d  = (double)numeric_b.u.i;
                numeric_b.type = KOS_FLOAT_VALUE;
            }
        }
    }
    else {
        assert(op != OT_ADD && op != OT_SUB && op != OT_MUL && op != OT_DIV && op != OT_MOD);

        if (numeric_a.type == KOS_FLOAT_VALUE) {
            numeric_a.u.i = (int64_t)floor(numeric_a.u.d);
            numeric_a.type = KOS_INTEGER_VALUE;
        }

        if (numeric_b.type == KOS_FLOAT_VALUE) {
            numeric_b.u.i = (int64_t)floor(numeric_b.u.d);
            numeric_b.type = KOS_INTEGER_VALUE;
        }
    }

    assert(numeric_a.type == numeric_b.type);

    switch (op) {

        case OT_ADD:
            if (numeric_a.type == KOS_INTEGER_VALUE)
                numeric_a.u.i += numeric_b.u.i;
            else
                numeric_a.u.d += numeric_b.u.d;
            break;

        case OT_SUB:
            if (numeric_a.type == KOS_INTEGER_VALUE)
                numeric_a.u.i -= numeric_b.u.i;
            else
                numeric_a.u.d -= numeric_b.u.d;
            break;

        case OT_MUL:
            if (numeric_a.type == KOS_INTEGER_VALUE)
                numeric_a.u.i *= numeric_b.u.i;
            else
                numeric_a.u.d *= numeric_b.u.d;
            break;

        case OT_DIV:
            if (numeric_a.type == KOS_INTEGER_VALUE) {
                if ( ! numeric_b.u.i) {
                    _announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.i /= numeric_b.u.i;
            }
            else {
                if (numeric_b.u.d == 0) {
                    _announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.d /= numeric_b.u.d;
            }
            break;

        case OT_MOD:
            if (numeric_a.type == KOS_INTEGER_VALUE) {
                if ( ! numeric_b.u.i) {
                    _announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.i %= numeric_b.u.i;
            }
            else {
                if (numeric_b.u.d == 0) {
                    _announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.d = fmod(numeric_a.u.d, numeric_b.u.d);
            }
            break;

        case OT_AND:
            numeric_a.u.i &= numeric_b.u.i;
            break;

        case OT_OR:
            numeric_a.u.i |= numeric_b.u.i;
            break;

        case OT_XOR:
            numeric_a.u.i ^= numeric_b.u.i;
            break;

        case OT_SHL:
            if (numeric_b.u.i > 63 || numeric_b.u.i < -63)
                numeric_a.u.i = (numeric_a.u.i < 0 && numeric_b.u.i < 0) ? -1 : 0;
            else if (numeric_b.u.i < 0)
                numeric_a.u.i >>= -numeric_b.u.i;
            else
                numeric_a.u.i <<= numeric_b.u.i;
            break;

        case OT_SHR:
            if (numeric_b.u.i > 63 || numeric_b.u.i < -63)
                numeric_a.u.i = (numeric_a.u.i < 0 && numeric_b.u.i > 0) ? -1 : 0;
            else if (numeric_b.u.i < 0)
                numeric_a.u.i <<= -numeric_b.u.i;
            else
                numeric_a.u.i >>= numeric_b.u.i;
            break;

        case OT_SHRU:
            /* fall through */
        default:
            assert(op == OT_SHRU);
            if (numeric_b.u.i > 63 || numeric_b.u.i < -63)
                numeric_a.u.i = 0;
            else if (numeric_b.u.i < 0)
                numeric_a.u.i <<= -numeric_b.u.i;
            else
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i >> numeric_b.u.i);
            break;
    }

    error = _collapse_numeric(program, node, &numeric_a);

_error:
    return error;
}

static int _optimize_unary_op(struct _KOS_COMP_UNIT      *program,
                              struct _KOS_AST_NODE       *node,
                              const struct _KOS_AST_NODE *a)
{
    enum _KOS_OPERATOR_TYPE op = node->token.op;
    struct _KOS_NUMERIC     numeric_a;

    if (a->token.type == TT_NUMERIC_BINARY) {
        assert(a->token.length == sizeof(struct _KOS_NUMERIC));
        numeric_a = *(const struct _KOS_NUMERIC *)(a->token.begin);
    }
    else if (KOS_SUCCESS != _KOS_parse_numeric(a->token.begin,
                                               a->token.begin + a->token.length,
                                               &numeric_a))
        return KOS_SUCCESS;

    if (op & OT_ARITHMETIC) {
        assert(op == OT_ADD || op == OT_SUB);
    }
    else {
        assert(op == OT_NOT);

        if (numeric_a.type == KOS_FLOAT_VALUE) {
            numeric_a.u.i = (int64_t)floor(numeric_a.u.d);
            numeric_a.type = KOS_INTEGER_VALUE;
        }
    }

    switch (op) {

        case OT_ADD:
            break;

        case OT_SUB:
            if (numeric_a.type == KOS_INTEGER_VALUE)
                numeric_a.u.i = -numeric_a.u.i;
            else
                numeric_a.u.d = -numeric_a.u.d;
            break;

        case OT_NOT:
            /* fall through */
        default:
            assert(op == OT_NOT);
            numeric_a.u.i = ~numeric_a.u.i;
            break;
    }

    return _collapse_numeric(program, node, &numeric_a);
}

static int _add_strings(struct _KOS_COMP_UNIT      *program,
                        struct _KOS_AST_NODE       *node,
                        const struct _KOS_AST_NODE *a,
                        const struct _KOS_AST_NODE *b)
{
    char                      *str;
    const enum _KOS_TOKEN_TYPE a_type   = a->token.type;
    const enum _KOS_TOKEN_TYPE b_type   = b->token.type;
    unsigned                   a_length = a->token.length;
    unsigned                   b_length = b->token.length;

    assert(a->type == NT_STRING_LITERAL);
    assert(a_type == TT_STRING || a_type == TT_STRING_OPEN_SQ || a_type == TT_STRING_OPEN_DQ);
    assert((a_type == TT_STRING && a_length >= 2U) || a_length >= 3U);
    assert(b->type == NT_STRING_LITERAL);
    assert(b_type == TT_STRING || b_type == TT_STRING_OPEN_SQ || b_type == TT_STRING_OPEN_DQ);
    assert((b_type == TT_STRING && b_length >= 2U) || b_length >= 3U);

    a_length -= a_type == TT_STRING ? 2U : 3U;
    b_length -= b_type == TT_STRING ? 2U : 3U;

    _promote(program, node, a);

    str = (char *)_KOS_mempool_alloc(&program->allocator, a_length + b_length + 2U);

    if ( ! str)
        return KOS_ERROR_OUT_OF_MEMORY;

    str[0] = '"';
    memcpy(str + 1, a->token.begin + 1, a_length);
    memcpy(str + 1 + a_length, b->token.begin + 1, b_length);
    str[1+a_length+b_length] = '"';

    node->token.type   = TT_STRING;
    node->token.begin  = str;
    node->token.length = a_length + b_length + 2;

    ++program->num_optimizations;

    return KOS_SUCCESS;
}

static void _collapse_typeof(struct _KOS_COMP_UNIT      *program,
                             struct _KOS_AST_NODE       *node,
                             const struct _KOS_AST_NODE *a)
{
    const enum _KOS_NODE_TYPE a_type = a->type;
    const char               *type   = 0;

    switch (a_type) {

        case NT_NUMERIC_LITERAL:
            if (a->token.type == TT_NUMERIC_BINARY) {
                const struct _KOS_NUMERIC *value = (const struct _KOS_NUMERIC *)
                    a->token.begin;
                assert(a->token.length == sizeof(struct _KOS_NUMERIC));
                if (value->type == KOS_INTEGER_VALUE)
                    type = "\"integer\"";
                else {
                    assert(value->type == KOS_FLOAT_VALUE);
                    type = "\"float\"";
                }
            }
            else {
                if (_KOS_is_integer(a->token.begin, a->token.begin + a->token.length))
                    type = "\"integer\"";
                else
                    type = "\"float\"";
            }
            break;

        case NT_STRING_LITERAL:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            type = "\"string\"";
            break;

        case NT_BOOL_LITERAL:
            type = "\"boolean\"";
            break;

        case NT_VOID_LITERAL:
            type = "\"void\"";
            break;

        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            type = "\"function\"";
            break;

        case NT_ARRAY_LITERAL:
            type = "\"array\"";
            break;

        case NT_OBJECT_LITERAL:
            type = "\"object\"";
            break;

        default:
            break;
    }

    if (type) {
        _collapse(node, NT_STRING_LITERAL, TT_STRING, KW_NONE, type, (unsigned)strlen(type));
        ++program->num_optimizations;
    }
}

static int _operator(struct _KOS_COMP_UNIT *program,
                     struct _KOS_AST_NODE  *node)
{
    int                           error = KOS_SUCCESS;
    int                           t;
    struct _KOS_AST_NODE         *a     = node->children;
    struct _KOS_AST_NODE         *b;
    struct _KOS_AST_NODE         *c     = 0;
    const struct _KOS_AST_NODE   *ca;
    const struct _KOS_AST_NODE   *cb    = 0;
    enum _KOS_NODE_TYPE           a_type;
    enum _KOS_NODE_TYPE           b_type;

    assert(a);
    b = a->next;

    TRY(_visit_node(program, a, &t));
    assert(t == TERM_NONE);

    if (b) {
        TRY(_visit_node(program, b, &t));
        assert(t == TERM_NONE);

        c = b->next;
        if (c) {
            assert( ! c->next);
            TRY(_visit_node(program, c, &t));
            assert(t == TERM_NONE);
        }
    }

    if ( ! program->optimize)
        return KOS_SUCCESS;

    ca = _KOS_get_const(program, a);
    if (b)
        cb = _KOS_get_const(program, b);

    a_type = ca ? ca->type : NT_EMPTY;
    b_type = cb ? cb->type : NT_EMPTY;

    switch (node->token.op) {

        case OT_ADD:
            /* fall through */
        case OT_SUB: {
            if (b) {

                const enum _KOS_OPERATOR_TYPE op = node->token.op;

                if (a_type == NT_NUMERIC_LITERAL && b_type == NT_NUMERIC_LITERAL)
                    error = _optimize_binary_op(program, node, ca, cb);

                else if (op == OT_ADD && a_type == NT_STRING_LITERAL && b_type == NT_STRING_LITERAL)
                    error = _add_strings(program, node, ca, cb);
            }
            else {
                if (a_type == NT_NUMERIC_LITERAL)
                    error = _optimize_unary_op(program, node, ca);
            }
            break;
        }

        case OT_MUL:
            /* fall through */
        case OT_DIV:
            /* fall through */
        case OT_MOD:
            /* fall through */
        case OT_AND:
            /* fall through */
        case OT_OR:
            /* fall through */
        case OT_XOR:
            /* fall through */
        case OT_SHL:
            /* fall through */
        case OT_SHR:
            /* fall through */
        case OT_SHRU: {
            if (a_type == NT_NUMERIC_LITERAL && b_type == NT_NUMERIC_LITERAL)
                error = _optimize_binary_op(program, node, ca, cb);
            break;
        }

        case OT_NOT: {
            assert( ! b);
            if (a_type == NT_NUMERIC_LITERAL)
                error = _optimize_unary_op(program, node, ca);
            break;
        }

        case OT_LOGNOT: {
            if (_KOS_node_is_truthy(program, ca) && a->token.keyword != KW_FALSE) {
                _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
                ++program->num_optimizations;
            }
            else if (_KOS_node_is_falsy(program, ca) && a->token.keyword != KW_TRUE) {
                _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_TRUE, 0, 0);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGAND: {
            if (_KOS_node_is_truthy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (_KOS_node_is_falsy(program, ca)) {
                _promote(program, node, a);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGOR: {
            if (_KOS_node_is_truthy(program, ca)) {
                _promote(program, node, a);
                ++program->num_optimizations;
            }
            else if (_KOS_node_is_falsy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGTRI: {
            assert(b);
            if (_KOS_node_is_truthy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (_KOS_node_is_falsy(program, ca) && b) {
                assert(b->next);
                if (b->next) {
                    _promote(program, node, b->next);
                    ++program->num_optimizations;
                }
            }
            break;
        }

        case OT_EQ:
            /* fall through */
        case OT_NE:
            /* TODO */
            break;

        case OT_GE:
            /* fall through */
        case OT_GT:
            /* fall through */
        case OT_LE:
            /* fall through */
        case OT_LT:
            /* TODO */
            break;

        case OT_NONE:
            if (node->token.keyword == KW_TYPEOF && ca) {
                assert( ! b);
                _collapse_typeof(program, node, ca);
            }
            break;

        default:
            break;
    }

_error:
    return error;
}

static void _copy_node_as_string(struct _KOS_AST_NODE       *dest,
                                 const struct _KOS_AST_NODE *src)
{
    *dest = *src;

    dest->type          = NT_STRING_LITERAL;
    dest->token.type    = TT_STRING;
    dest->token.keyword = KW_NONE;
    dest->token.op      = OT_NONE;
    dest->token.sep     = ST_NONE;
}

static int _stringify(struct _KOS_COMP_UNIT       *program,
                      const struct _KOS_AST_NODE **node_ptr,
                      struct _KOS_AST_NODE        *tmp_node)
{
    const enum _KOS_NODE_TYPE type = (*node_ptr)->type;

    switch (type) {

        case NT_STRING_LITERAL:
            return 1;

        case NT_VOID_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL: {

            const enum _KOS_KEYWORD_TYPE kw = (*node_ptr)->token.keyword;

            _copy_node_as_string(tmp_node, *node_ptr);

            if (type == NT_VOID_LITERAL)
                tmp_node->token.begin = "\"void\"";
            else if (kw == KW_TRUE)
                tmp_node->token.begin = "\"true\"";
            else {
                assert(kw == KW_FALSE);
                tmp_node->token.begin = "\"false\"";
            }

            tmp_node->token.length = (unsigned)strlen(tmp_node->token.begin);
            *node_ptr              = tmp_node;
            return 1;
        }

        case NT_NUMERIC_LITERAL: {

            struct _KOS_NUMERIC      numeric;
            const struct _KOS_TOKEN *token    = &(*node_ptr)->token;
            char                    *store;
            const unsigned           max_size = 64;

            if (token->type == TT_NUMERIC_BINARY) {
                const struct _KOS_NUMERIC *value;

                assert(token->length == sizeof(struct _KOS_NUMERIC));

                value = (const struct _KOS_NUMERIC *)token->begin;

                numeric = *value;
            }
            else if (KOS_SUCCESS != _KOS_parse_numeric(token->begin,
                                                       token->begin + token->length,
                                                       &numeric))
                return 0; /* Parse errors are handled later */

            _copy_node_as_string(tmp_node, *node_ptr);

            store = (char *)_KOS_mempool_alloc(&program->allocator, max_size + 1);
            if ( ! store)
                return 0; /* Malloc errors are handled later */

            store[max_size] = 0;

            if (numeric.type == KOS_INTEGER_VALUE) {
                snprintf(store, max_size, "\"%" PRId64 "\"", numeric.u.i);
            }
            else {
                assert(numeric.type == KOS_FLOAT_VALUE);
                /* TODO make this consistent with KOS_object_to_string */
                snprintf(store, max_size, "\"%f\"", numeric.u.d);
            }

            tmp_node->token.begin  = store;
            tmp_node->token.length = (unsigned)strlen(store);
            *node_ptr              = tmp_node;
            return 1;
        }

        default:
            return 0;
    }
}

static int _interpolated_string(struct _KOS_COMP_UNIT *program,
                                struct _KOS_AST_NODE  *node)
{
    int                   error = KOS_SUCCESS;
    struct _KOS_AST_NODE *child = node->children;
    int                   t;

    assert(child);

    TRY(_visit_node(program, child, &t));
    assert(t == TERM_NONE);

    while (child && child->next) {

        struct _KOS_AST_NODE       *next = child->next;
        const struct _KOS_AST_NODE *ca   = _KOS_get_const(program, child);
        const struct _KOS_AST_NODE *cb;
        struct _KOS_AST_NODE        sa;
        struct _KOS_AST_NODE        sb;

        TRY(_visit_node(program, next, &t));
        assert(t == TERM_NONE);

        cb = _KOS_get_const(program, next);

        if (ca && cb &&
            _stringify(program, &ca, &sa) && _stringify(program, &cb, &sb)) {

            TRY(_add_strings(program, child, ca, cb));

            child->next = next->next;
        }
        else
            child = child->next;
    }

    assert(node->children);

    if ( ! node->children->next)
        _promote(program, node, node->children);

_error:
    return error;
}

static int _line(struct _KOS_COMP_UNIT *program,
                 struct _KOS_AST_NODE  *node)
{
    struct _KOS_NUMERIC numeric;

    assert( ! node->children);

    numeric.type = KOS_INTEGER_VALUE;
    numeric.u.i  = node->token.pos.line;

    return _collapse_numeric(program, node, &numeric);
}

static int _visit_node(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *node,
                       int                   *is_terminal)
{
    int error = KOS_ERROR_INTERNAL;

    *is_terminal = TERM_NONE;

    switch (node->type) {

        case NT_RETURN:
            error        = _visit_child_nodes(program, node);
            *is_terminal = TERM_RETURN;
            break;

        case NT_THROW:
            error        = _visit_child_nodes(program, node);
            *is_terminal = TERM_THROW;
            break;

        case NT_BREAK:
            /* fall through */
        case NT_CONTINUE:
            assert( ! node->children);
            error        = _visit_child_nodes(program, node);
            *is_terminal = TERM_BREAK;
            break;

        case NT_SCOPE:
            error = _scope(program, node, is_terminal);
            break;
        case NT_IF:
            error = _if_stmt(program, node, is_terminal);
            break;
        case NT_REPEAT:
            error = _repeat_stmt(program, node, is_terminal);
            break;
        case NT_WHILE:
            error = _while_stmt(program, node, is_terminal);
            break;
        case NT_FOR:
            error = _for_stmt(program, node, is_terminal);
            break;
        case NT_FOR_IN:
            error = _for_in_stmt(program, node);
            break;
        case NT_TRY:
            error = _try_stmt(program, node, is_terminal);
            break;
        case NT_SWITCH:
            error = _switch_stmt(program, node, is_terminal);
            break;
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            error = _function_literal(program, node);
            break;

        case NT_IDENTIFIER:
            _identifier(program, node);
            error = KOS_SUCCESS;
            break;

        case NT_ASSIGNMENT:
            /* fall through */
        case NT_MULTI_ASSIGNMENT:
            error = _assignment(program, node);
            break;

        case NT_OPERATOR:
            error = _operator(program, node);
            break;

        case NT_INTERPOLATED_STRING:
            error = _interpolated_string(program, node);
            break;

        case NT_PARAMETERS:
            /* fall through */
        case NT_IMPORT:
            error = KOS_SUCCESS;
            break;

        case NT_LINE_LITERAL:
            /* fall through */
        default:
            assert(node->type == NT_LINE_LITERAL);
            error = _line(program, node);
            break;

        case NT_EMPTY:
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
        case NT_YIELD:
            /* fall through */
        case NT_STREAM:
            /* fall through */
        case NT_ELLIPSIS:
            /* fall through */
        case NT_PROPERTY:
            /* fall through */
        case NT_IN:
            /* fall through */
        case NT_EXPRESSION_LIST:
            /* fall through */
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            /* fall through */
        case NT_FALLTHROUGH:
            /* fall through */
        case NT_LANDMARK:
            /* fall through */
        case NT_NUMERIC_LITERAL:
            /* fall through */
        case NT_STRING_LITERAL:
            /* fall through */
        case NT_THIS_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            /* fall through */
        case NT_ARRAY_LITERAL:
            /* fall through */
        case NT_OBJECT_LITERAL:
            error = _visit_child_nodes(program, node);
            break;
    }

    return error;
}

int _KOS_optimize(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *ast)
{
    int t;
    assert(ast->type == NT_SCOPE);
    return _visit_node(program, ast, &t);
}

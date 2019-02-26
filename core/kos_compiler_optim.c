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

enum KOS_TERMINATOR_E {
    TERM_NONE   = 0,
    TERM_BREAK  = 1,
    TERM_THROW  = 2,
    TERM_RETURN = 4
};

static int _visit_node(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node,
                       int           *is_terminal);

static void _collapse(KOS_AST_NODE    *node,
                      KOS_NODE_TYPE    node_type,
                      KOS_TOKEN_TYPE   token_type,
                      KOS_KEYWORD_TYPE keyword,
                      const char      *begin,
                      unsigned         length)
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

static int _collapse_numeric(KOS_COMP_UNIT     *program,
                             KOS_AST_NODE      *node,
                             const KOS_NUMERIC *value)
{
    int            error;
    const unsigned length = sizeof(KOS_NUMERIC);
    KOS_NUMERIC   *store  = (KOS_NUMERIC *)kos_mempool_alloc(&program->allocator, length);

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

static void _promote(KOS_COMP_UNIT      *program,
                     KOS_AST_NODE       *node,
                     const KOS_AST_NODE *child)
{
    KOS_SCOPE *scope = (KOS_SCOPE *)
            kos_red_black_find(program->scopes, (void *)child, kos_scope_compare_item);

    assert( ! kos_red_black_find(program->scopes, (void *)node, kos_scope_compare_item));

    if (scope) {

        kos_red_black_delete(&program->scopes, (KOS_RED_BLACK_NODE *)scope);

        assert(scope->scope_node == child);
        scope->scope_node = node;

        kos_red_black_insert(&program->scopes,
                             (KOS_RED_BLACK_NODE *)scope,
                             kos_scope_compare_node);
    }

    node->children   = child->children;
    node->last_child = child->last_child;
    node->token      = child->token;
    node->type       = child->type;
}

static int _get_nonzero(const KOS_TOKEN *token, int *non_zero)
{
    KOS_NUMERIC numeric;

    assert(token->length > 0);

    if (token->type == TT_NUMERIC_BINARY) {

        const KOS_NUMERIC *value = (const KOS_NUMERIC *)token->begin;

        assert(token->length == sizeof(KOS_NUMERIC));

        numeric = *value;
    }
    else {

        int        error;
        const char c = *token->begin;

        assert(token->type == TT_NUMERIC);

        if (c >= '1' && c <= '9') {
            *non_zero = 1;
            return KOS_SUCCESS;
        }

        if (c == '0' && token->length == 1) {
            *non_zero = 0;
            return KOS_SUCCESS;
        }

        error = kos_parse_numeric(token->begin,
                                  token->begin + token->length,
                                  &numeric);
        if (error)
            return error;
    }

    if (numeric.type == KOS_INTEGER_VALUE)
        *non_zero = numeric.u.i ? 1 : 0;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        *non_zero = numeric.u.d == 0.0 ? 0 : 1;
    }

    return KOS_SUCCESS;
}

static void _lookup_var(KOS_COMP_UNIT   *program,
                        const KOS_TOKEN *token,
                        int              only_active,
                        KOS_VAR        **out_var,
                        int             *is_local)
{
    KOS_SCOPE *scope     = program->scope_stack;
    KOS_SCOPE *fun_scope = 0;
    KOS_VAR   *var       = 0;

    assert(scope);

    for (scope = program->scope_stack; scope->next && ! scope->is_function; scope = scope->next) {

        var = kos_find_var(scope->vars, token);

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

        var = kos_find_var(scope->vars, token);

        if (var) {
            *is_local = 1; /* This is global scope, but mark it as "local" access */
            scope     = 0;
        }
    }

    for ( ; scope; scope = scope->next) {

        var = kos_find_var(scope->vars, token);

        if (var && var->is_active) {
            *is_local = scope == fun_scope || ! scope->next ? 1 : 0; /* current func's arguments are "local" */
            break;
        }

        var = 0;
    }

    assert(var);

    *out_var = var;
}

const KOS_AST_NODE *kos_get_const(KOS_COMP_UNIT      *program,
                                  const KOS_AST_NODE *node)
{
    KOS_VAR *var = 0;
    int      is_local;

    if ( ! node || node->type != NT_IDENTIFIER)
        return node;

    _lookup_var(program, &node->token, 1, &var, &is_local);

    return var->is_const ? var->value : 0;
}

int kos_node_is_truthy(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node)
{
    node = kos_get_const(program, node);

    if ( ! node)
        return 0;

    if ((node->type == NT_BOOL_LITERAL    && node->token.keyword == KW_TRUE) ||
        (node->type == NT_NUMERIC_LITERAL && node->token.length == 1 && *node->token.begin != '0') ||
        (node->type == NT_STRING_LITERAL)      ||
        (node->type == NT_FUNCTION_LITERAL)    ||
        (node->type == NT_CONSTRUCTOR_LITERAL) ||
        (node->type == NT_ARRAY_LITERAL)       ||
        (node->type == NT_OBJECT_LITERAL)      ||
        (node->type == NT_INTERPOLATED_STRING)) {

        return 1;
    }

    if (node->type == NT_NUMERIC_LITERAL) {
        int       value = 0;
        const int error = _get_nonzero(&node->token, &value);
        if ( ! error)
            return value;
    }

    return 0;
}

int kos_node_is_falsy(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node)
{
    node = kos_get_const(program, node);

    if ( ! node)
        return 0;

    if ((node->type == NT_BOOL_LITERAL    && node->token.keyword == KW_FALSE) ||
        (node->type == NT_NUMERIC_LITERAL && node->token.length == 1 && *node->token.begin == '0') ||
        (node->type == NT_VOID_LITERAL)) {

        return 1;
    }

    if (node->type == NT_NUMERIC_LITERAL) {
        int       value = 0;
        const int error = _get_nonzero(&node->token, &value);
        if ( ! error)
            return ! value;
    }

    return 0;
}

static int _reset_var_state(KOS_RED_BLACK_NODE *node,
                            void               *cookie)
{
    KOS_VAR *var = (KOS_VAR *)node;

    if (var->is_active == VAR_ACTIVE)
        var->is_active = VAR_INACTIVE;

    var->num_reads         = 0;
    var->num_assignments   = 0;
    var->local_reads       = 0;
    var->local_assignments = 0;

    return KOS_SUCCESS;
}

static KOS_SCOPE *_push_scope(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node)
{
    KOS_SCOPE *scope = (KOS_SCOPE *)
            kos_red_black_find(program->scopes, (void *)node, kos_scope_compare_item);

    assert(scope);

    assert(scope->next == program->scope_stack);

    kos_red_black_walk(scope->vars, _reset_var_state, 0);

    program->scope_stack = scope;

    scope->num_vars       = 0;
    scope->num_indep_vars = 0;

    return scope;
}

static int _count_and_update_vars(KOS_RED_BLACK_NODE *node,
                                  void               *cookie)
{
    KOS_VAR   *var   = (KOS_VAR *)node;
    KOS_SCOPE *scope = (KOS_SCOPE *)cookie;

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

    /* Count only used local variables */
    if ((var->type & VAR_LOCAL) && var->num_reads
        /* Count ellipsis only if it's independent, in which case it is relocated
         * to a local variable in the independent range */
        && (var != scope->ellipsis || var->type == VAR_INDEPENDENT_LOCAL)) {

        ++scope->num_vars;

        if (var->type == VAR_INDEPENDENT_LOCAL)
            ++scope->num_indep_vars;
    }
    else if (var->type & VAR_ARGUMENT) {
        assert(scope->is_function || ! scope->next);
    }

    return KOS_SUCCESS;
}

static void _pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope = program->scope_stack;

    assert(scope);

    kos_red_black_walk(scope->vars, _count_and_update_vars, scope);

    if ( ! scope->is_function && scope->next) {
        scope->next->num_vars       += scope->num_vars;
        scope->next->num_indep_vars += scope->num_indep_vars;
    }

    program->scope_stack = scope->next;
}

static int _scope(KOS_COMP_UNIT *program,
                  KOS_AST_NODE  *node,
                  int           *is_terminal)
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

static int _if_stmt(KOS_COMP_UNIT *program,
                    KOS_AST_NODE  *node,
                    int           *is_terminal)
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
        is_truthy = kos_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : kos_node_is_falsy(program, node);
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

cleanup:
    return error;
}

static int _repeat_stmt(KOS_COMP_UNIT *program,
                        KOS_AST_NODE  *node,
                        int           *is_terminal)
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

cleanup:
    return error;
}

static int _for_stmt(KOS_COMP_UNIT *program,
                     KOS_AST_NODE  *node,
                     int           *is_terminal)
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
        is_truthy = node->type == NT_EMPTY || kos_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : kos_node_is_falsy(program, node);
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

    TRY(_visit_node(program, node->next, is_terminal));

    if ( ! is_truthy || (*is_terminal & TERM_BREAK))
        *is_terminal = TERM_NONE;

    if (*is_terminal && program->optimize && node->type != NT_EMPTY) {
        _collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

cleanup:
    return error;
}

static int _try_stmt(KOS_COMP_UNIT *program,
                     KOS_AST_NODE  *node,
                     int           *is_terminal)
{
    int error = KOS_SUCCESS;
    int t1;
    int t2    = TERM_NONE;
    int t3    = TERM_NONE;

    const KOS_NODE_TYPE node_type    = node->type;
    KOS_AST_NODE       *finally_node = 0;

    _push_scope(program, node);

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t1));

    node = node->next;
    assert(node);
    assert( ! node->next);

    if (node_type == NT_TRY_CATCH) {

        KOS_AST_NODE *var_node = node->children;
        KOS_AST_NODE *scope_node;
        KOS_VAR      *var;
        int           is_local;

        assert(node->type == NT_CATCH);

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

        finally_node = node;

        TRY(_visit_node(program, finally_node, &t3));
    }

    *is_terminal = TERM_NONE;

    if ( ! finally_node || finally_node->type == NT_EMPTY || ! t3) {
        if (t1 && t2)
            *is_terminal = (t1 & ~TERM_THROW) | t2;
    }
    else
        *is_terminal = t3;

    _pop_scope(program);

cleanup:
    return error;
}

static int _switch_stmt(KOS_COMP_UNIT *program,
                        KOS_AST_NODE  *node,
                        int           *is_terminal)
{
    int error          = KOS_SUCCESS;
    int num_cases      = 0;
    int num_terminated = 0;
    int has_default    = 0;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    t = TERM_NONE;

    for (node = node->next; node; node = node->next) {

        int next_t;

        if (node->type == NT_DEFAULT)
            has_default = 1;

        TRY(_visit_node(program, node, &next_t));

        if (next_t & TERM_BREAK)
            next_t = TERM_NONE;

        ++num_cases;
        if (next_t) {
            ++num_terminated;
            t |= next_t;
        }
    }

    *is_terminal = (num_cases == num_terminated && has_default) ? t : TERM_NONE;

cleanup:
    return error;
}

static int _case_stmt(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node,
                      int           *is_terminal)
{
    int error = KOS_SUCCESS;
    int t;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &t));
    assert(t == TERM_NONE);

    node = node->next;
    assert(node);
    assert( ! node->next
           || node->next->type == NT_FALLTHROUGH
           || node->next->type == NT_EMPTY);

    TRY(_visit_node(program, node, is_terminal));

    if (*is_terminal && node->next)
        _collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);

cleanup:
    return error;
}

static int _visit_child_nodes(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;
    int t     = TERM_NONE;

    for (node = node->children; node; node = node->next)
        TRY(_visit_node(program, node, &t));

cleanup:
    return error;
}

static int _for_in_stmt(KOS_COMP_UNIT *program,
                        KOS_AST_NODE  *node)
{
    int error;

    _push_scope(program, node);

    assert(node->children);
    assert(node->children->children);
    kos_activate_new_vars(program, node->children->children);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    return error;
}

static int _parameter_defaults(KOS_COMP_UNIT *program,
                               KOS_AST_NODE  *node,
                               KOS_AST_NODE  *name_node)
{
    int      error   = KOS_SUCCESS;
    KOS_VAR *fun_var = 0;

    assert(name_node);
    if (name_node->type == NT_NAME_CONST) {
        assert(name_node->children);
        assert(name_node->children->type == NT_IDENTIFIER);
        assert(name_node->children->token.type == TT_IDENTIFIER);

        fun_var = kos_find_var(program->scope_stack->vars, &name_node->children->token);
        assert(fun_var);
        assert((fun_var->type & VAR_LOCAL) || fun_var->type == VAR_GLOBAL);

        if ((fun_var->type & VAR_LOCAL) && fun_var->is_active)
            fun_var->is_active = VAR_INACTIVE;
        else
            fun_var = 0;
    }

    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    for ( ; node && node->type != NT_ELLIPSIS; node = node->next) {

        if (node->type == NT_ASSIGNMENT) {

            KOS_AST_NODE *def_node = node->children;
            int           is_terminal;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            TRY(_visit_node(program, def_node, &is_terminal));
        }
    }

    if (fun_var)
        fun_var->is_active = VAR_ACTIVE;

cleanup:
    return error;
}

static int _function_literal(KOS_COMP_UNIT *program,
                             KOS_AST_NODE  *node)
{
    int           error;
    KOS_AST_NODE *name_node;

    _push_scope(program, node);

    name_node = node->children;
    assert(name_node);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    if ( ! error)
        error = _parameter_defaults(program, name_node->next, name_node);

    return error;
}

static void _identifier(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    KOS_VAR *var;
    int      is_local;

    _lookup_var(program, &node->token, 1, &var, &is_local);

    ++var->num_reads;

    if (is_local)
        ++var->local_reads;
}

static int _assignment(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    int               error     = KOS_SUCCESS;
    int               is_lhs;
    int               t;
    KOS_AST_NODE     *lhs_node  = node->children;
    KOS_AST_NODE     *rhs_node;
    KOS_NODE_TYPE     assg_type = node->type;
    KOS_OPERATOR_TYPE assg_op   = node->token.op;

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

    kos_activate_self_ref_func(program, lhs_node);

    TRY(_visit_node(program, rhs_node, &t));
    assert(t == TERM_NONE);

    /* TODO
     * - optimize multi assignment from array literal (don't create array)
     * - optimize var/const if the variable is never used afterwards
     */

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_VAR *var = 0;
            int      is_local;

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
                if (assg_op != OT_SET)
                    ++var->num_reads;

                if (is_local) {
                    ++var->local_assignments;
                    if (assg_op != OT_SET)
                        ++var->local_reads;
                }
            }
        }
        else {
            assert(node->type != NT_LINE_LITERAL &&
                   node->type != NT_THIS_LITERAL &&
                   node->type != NT_SUPER_PROTO_LITERAL);
            TRY(_visit_node(program, node, &t));
        }
    }

cleanup:
    return error;
}

static void _announce_div_by_zero(KOS_COMP_UNIT      *program,
                                  const KOS_AST_NODE *node)
{
    program->error_str   = str_err_div_by_zero;
    program->error_token = &node->token;
}

static int _optimize_binary_op(KOS_COMP_UNIT      *program,
                               KOS_AST_NODE       *node,
                               const KOS_AST_NODE *a,
                               const KOS_AST_NODE *b)
{
    int               error;
    KOS_OPERATOR_TYPE op = node->token.op;
    KOS_NUMERIC       numeric_a;
    KOS_NUMERIC       numeric_b;

    if (a->token.type == TT_NUMERIC_BINARY) {
        assert(a->token.length == sizeof(KOS_NUMERIC));
        numeric_a = *(const KOS_NUMERIC *)(a->token.begin);
    }
    else if (KOS_SUCCESS != kos_parse_numeric(a->token.begin,
                                              a->token.begin + a->token.length,
                                              &numeric_a))
        return KOS_SUCCESS;

    if (b->token.type == TT_NUMERIC_BINARY) {
        assert(b->token.length == sizeof(KOS_NUMERIC));
        numeric_b = *(const KOS_NUMERIC *)(b->token.begin);
    }
    else if (KOS_SUCCESS != kos_parse_numeric(b->token.begin,
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
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i << numeric_b.u.i);
            break;

        case OT_SHR:
            if (numeric_b.u.i > 63 || numeric_b.u.i < -63)
                numeric_a.u.i = (numeric_a.u.i < 0 && numeric_b.u.i > 0) ? -1 : 0;
            else if (numeric_b.u.i < 0)
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i << -numeric_b.u.i);
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
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i << -numeric_b.u.i);
            else
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i >> numeric_b.u.i);
            break;
    }

    error = _collapse_numeric(program, node, &numeric_a);

cleanup:
    return error;
}

static int _optimize_unary_op(KOS_COMP_UNIT      *program,
                              KOS_AST_NODE       *node,
                              const KOS_AST_NODE *a)
{
    KOS_OPERATOR_TYPE op = node->token.op;
    KOS_NUMERIC       numeric_a;

    if (a->token.type == TT_NUMERIC_BINARY) {
        assert(a->token.length == sizeof(KOS_NUMERIC));
        numeric_a = *(const KOS_NUMERIC *)(a->token.begin);
    }
    else if (KOS_SUCCESS != kos_parse_numeric(a->token.begin,
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

static int _is_raw(const KOS_AST_NODE *node)
{
    if (node->type == NT_STRING_LITERAL && node->token.type == TT_STRING) {
        const char *const begin = node->token.begin;

        if (*begin == 'r' || *begin == 'R') {
            assert(begin[1] == '"');
            assert(node->token.length >= 3U);
            return 1;
        }
    }

    return 0;
}

static int _add_strings(KOS_COMP_UNIT      *program,
                        KOS_AST_NODE       *node,
                        const KOS_AST_NODE *a,
                        const KOS_AST_NODE *b)
{
    char                *str;
    const KOS_TOKEN_TYPE a_type   = a->token.type;
    const KOS_TOKEN_TYPE b_type   = b->token.type;
    const char          *a_begin  = a->token.begin + 1;
    const char          *b_begin  = b->token.begin + 1;
    unsigned             a_length = a->token.length;
    unsigned             b_length = b->token.length;
    const int            is_raw   = _is_raw(a);
    unsigned             pos;
    unsigned             new_length;

    assert(a->type == NT_STRING_LITERAL);
    assert(a_type == TT_STRING || a_type == TT_STRING_OPEN);
    assert((a_type == TT_STRING && a_length >= 2U) || a_length >= 3U);
    assert(b->type == NT_STRING_LITERAL);
    assert(b_type == TT_STRING || b_type == TT_STRING_OPEN);
    assert((b_type == TT_STRING && b_length >= 2U) || b_length >= 3U);
    assert(_is_raw(b) == is_raw);

    a_length -= a_type == TT_STRING ? 2U : 3U;
    b_length -= b_type == TT_STRING ? 2U : 3U;

    if (is_raw) {
        ++a_begin;
        --a_length;
        ++b_begin;
        --b_length;
    }

    _promote(program, node, a);

    new_length = a_length + b_length + (is_raw ? 3U : 2U);

    str = (char *)kos_mempool_alloc(&program->allocator, new_length);

    if ( ! str)
        return KOS_ERROR_OUT_OF_MEMORY;

    pos = 0;
    if (is_raw)
        str[pos++] = 'r';
    str[pos++] = '"';
    memcpy(str + pos, a_begin, a_length);
    pos += a_length;
    memcpy(str + pos, b_begin, b_length);
    pos += b_length;
    str[pos++] = '"';
    assert(pos == new_length);

    node->token.type   = TT_STRING;
    node->token.begin  = str;
    node->token.length = new_length;

    ++program->num_optimizations;

    return KOS_SUCCESS;
}

static void _collapse_typeof(KOS_COMP_UNIT      *program,
                             KOS_AST_NODE       *node,
                             const KOS_AST_NODE *a)
{
    const KOS_NODE_TYPE a_type = a->type;
    const char         *type   = 0;

    switch (a_type) {

        case NT_NUMERIC_LITERAL:
            if (a->token.type == TT_NUMERIC_BINARY) {
                const KOS_NUMERIC *value = (const KOS_NUMERIC *)
                    a->token.begin;
                assert(a->token.length == sizeof(KOS_NUMERIC));
                if (value->type == KOS_INTEGER_VALUE)
                    type = "\"integer\"";
                else {
                    assert(value->type == KOS_FLOAT_VALUE);
                    type = "\"float\"";
                }
            }
            else {
                if (kos_is_integer(a->token.begin, a->token.begin + a->token.length))
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

        default:
            break;
    }

    if (type) {
        _collapse(node, NT_STRING_LITERAL, TT_STRING, KW_NONE, type, (unsigned)strlen(type));
        ++program->num_optimizations;
    }
}

static int _operator(KOS_COMP_UNIT *program,
                     KOS_AST_NODE  *node)
{
    int                 error = KOS_SUCCESS;
    int                 t;
    KOS_AST_NODE       *a     = node->children;
    KOS_AST_NODE       *b;
    KOS_AST_NODE       *c     = 0;
    const KOS_AST_NODE *ca;
    const KOS_AST_NODE *cb    = 0;
    KOS_NODE_TYPE       a_type;
    KOS_NODE_TYPE       b_type;

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

    ca = kos_get_const(program, a);
    if (b)
        cb = kos_get_const(program, b);

    a_type = ca ? ca->type : NT_EMPTY;
    b_type = cb ? cb->type : NT_EMPTY;

    switch (node->token.op) {

        case OT_ADD:
            /* fall through */
        case OT_SUB: {
            if (b) {

                const KOS_OPERATOR_TYPE op = node->token.op;

                if (a_type == NT_NUMERIC_LITERAL && b_type == NT_NUMERIC_LITERAL)
                    error = _optimize_binary_op(program, node, ca, cb);

                else if (op == OT_ADD && a_type == NT_STRING_LITERAL && b_type == NT_STRING_LITERAL)
                    if (_is_raw(ca) == _is_raw(cb))
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
            if (kos_node_is_truthy(program, ca) && a->token.keyword != KW_FALSE) {
                _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && a->token.keyword != KW_TRUE) {
                _collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_TRUE, 0, 0);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGAND: {
            if (kos_node_is_truthy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca)) {
                _promote(program, node, a);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGOR: {
            if (kos_node_is_truthy(program, ca)) {
                _promote(program, node, a);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGTRI: {
            assert(b);
            if (kos_node_is_truthy(program, ca) && b) {
                _promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && b) {
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

cleanup:
    return error;
}

static void _copy_node_as_string(KOS_AST_NODE       *dest,
                                 const KOS_AST_NODE *src)
{
    *dest = *src;

    dest->type          = NT_STRING_LITERAL;
    dest->token.type    = TT_STRING;
    dest->token.keyword = KW_NONE;
    dest->token.op      = OT_NONE;
    dest->token.sep     = ST_NONE;
}

static int _stringify(KOS_COMP_UNIT       *program,
                      const KOS_AST_NODE **node_ptr,
                      KOS_AST_NODE        *tmp_node)
{
    const KOS_NODE_TYPE type = (*node_ptr)->type;

    switch (type) {

        case NT_STRING_LITERAL:
            return 1;

        case NT_VOID_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL: {

            const KOS_KEYWORD_TYPE kw = (*node_ptr)->token.keyword;

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

            KOS_NUMERIC      numeric;
            const KOS_TOKEN *token    = &(*node_ptr)->token;
            char            *store;
            const unsigned   max_size = 34;

            if (token->type == TT_NUMERIC_BINARY) {
                const KOS_NUMERIC *value;

                assert(token->length == sizeof(KOS_NUMERIC));

                value = (const KOS_NUMERIC *)token->begin;

                numeric = *value;
            }
            else if (KOS_SUCCESS != kos_parse_numeric(token->begin,
                                                      token->begin + token->length,
                                                      &numeric))
                return 0; /* Parse errors are handled later */

            _copy_node_as_string(tmp_node, *node_ptr);

            store = (char *)kos_mempool_alloc(&program->allocator, max_size + 1);
            if ( ! store)
                return 0; /* Malloc errors are handled later */

            store[max_size] = 0;

            if (numeric.type == KOS_INTEGER_VALUE) {
                snprintf(store, max_size, "\"%" PRId64 "\"", numeric.u.i);
            }
            else {
                unsigned size;

                assert(numeric.type == KOS_FLOAT_VALUE);

                store[0] = '"';

                size = kos_print_float(store + 1, max_size - 2, numeric.u.d);
                assert(size < max_size - 1);

                store[size + 1] = '"';
                store[size + 2] = 0;
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

static int _interpolated_string(KOS_COMP_UNIT *program,
                                KOS_AST_NODE  *node)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *child = node->children;
    int           t;

    assert(child);

    TRY(_visit_node(program, child, &t));
    assert(t == TERM_NONE);

    while (child && child->next) {

        KOS_AST_NODE       *next = child->next;
        const KOS_AST_NODE *ca   = kos_get_const(program, child);
        const KOS_AST_NODE *cb;
        KOS_AST_NODE        sa;
        KOS_AST_NODE        sb;

        TRY(_visit_node(program, next, &t));
        assert(t == TERM_NONE);

        cb = kos_get_const(program, next);

        if (ca && cb &&
            _stringify(program, &ca, &sa) && _stringify(program, &cb, &sb) &&
            _is_raw(ca) == _is_raw(cb)) {

            TRY(_add_strings(program, child, ca, cb));

            child->next = next->next;
        }
        else
            child = child->next;
    }

    assert(node->children);

    if ( ! node->children->next)
        _promote(program, node, node->children);

cleanup:
    return error;
}

static int _line(KOS_COMP_UNIT *program,
                 KOS_AST_NODE  *node)
{
    KOS_NUMERIC numeric;

    assert( ! node->children);

    numeric.type = KOS_INTEGER_VALUE;
    numeric.u.i  = node->token.pos.line;

    return _collapse_numeric(program, node, &numeric);
}

static int _visit_node(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node,
                       int           *is_terminal)
{
    int error = KOS_ERROR_INTERNAL;

    *is_terminal = TERM_NONE;

    switch (node->type) {

        case NT_RETURN:
            /* fall through */
        default:
            assert(node->type == NT_RETURN);
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
        case NT_FOR:
            error = _for_stmt(program, node, is_terminal);
            break;
        case NT_FOR_IN:
            error = _for_in_stmt(program, node);
            break;
        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = _try_stmt(program, node, is_terminal);
            break;
        case NT_SWITCH:
            error = _switch_stmt(program, node, is_terminal);
            break;
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            error = _case_stmt(program, node, is_terminal);
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

        case NT_LINE_LITERAL:
            error = _line(program, node);
            break;

        case NT_EMPTY:
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
        case NT_SUPER_CTOR_LITERAL:
            /* fall through */
        case NT_SUPER_PROTO_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            assert( ! node->children);
            /* fall through */
        case NT_PARAMETERS:
            /* fall through */
        case NT_IMPORT:
            /* fall through */
        case NT_NAME:
            /* fall through */
        case NT_NAME_CONST:
            error = KOS_SUCCESS;
            break;

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
        case NT_ASYNC:
            /* fall through */
        case NT_ELLIPSIS:
            /* fall through */
        case NT_PROPERTY:
            /* fall through */
        case NT_EXPAND:
            /* fall through */
        case NT_IN:
            /* fall through */
        case NT_EXPRESSION_LIST:
            /* fall through */
        case NT_ARRAY_LITERAL:
            /* fall through */
        case NT_OBJECT_LITERAL:
            /* fall through */
        case NT_CLASS_LITERAL:
            error = _visit_child_nodes(program, node);
            break;
    }

    return error;
}

int kos_optimize(KOS_COMP_UNIT *program,
                 KOS_AST_NODE  *ast)
{
    int t;
    assert(ast->type == NT_SCOPE);
    return _visit_node(program, ast, &t);
}

/*
 * Copyright (c) 2014-2018 Chris Dragan
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
#include "kos_config.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>

static int _visit_node(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node);

static void _update_scope_ref(KOS_COMP_UNIT *program,
                              int            var_type,
                              KOS_SCOPE     *closure)
{
    KOS_SCOPE *scope;

    /* Find function owning the variable's scope */
    while (closure->next && ! closure->is_function)
        closure = closure->next;

    /* Reference the function in all inner scopes which use it */
    for (scope = program->scope_stack; scope != closure; scope = scope->next)
        if (scope->is_function) {

            KOS_SCOPE_REF *ref;

            assert(scope->has_frame);

            ref = kos_find_scope_ref((KOS_FRAME *)scope, closure);

            assert(ref);

            if (var_type == VAR_INDEPENDENT_ARGUMENT)
                ++ref->exported_args;
            else {
                assert(var_type == VAR_INDEPENDENT_LOCAL ||
                       var_type == VAR_INDEPENDENT_ARG_IN_REG);
                ++ref->exported_locals;
            }
        }
}

static void _lookup_var(KOS_COMP_UNIT   *program,
                        const KOS_TOKEN *token,
                        int              only_active,
                        KOS_VAR        **out_var)
{
    KOS_SCOPE *scope     = program->scope_stack;
    KOS_SCOPE *fun_scope = 0;
    KOS_VAR   *var       = 0;
    int        is_local  = 0;

    assert(scope);

    for (scope = program->scope_stack; scope->next && ! scope->is_function; scope = scope->next) {

        var = kos_find_var(scope->vars, token);

        if (var && (var->is_active || ! only_active)) {
            *out_var = var;
            return;
        }
    }

    assert( ! scope->next || scope->is_function);

    if (scope->is_function)
        fun_scope = scope;

    if ( ! scope->next && ! only_active) {

        var = kos_find_var(scope->vars, token);

        if (var) {
            is_local = 1; /* This is global scope, but mark it as "local" access */
            scope    = 0;
        }
    }

    for ( ; scope; scope = scope->next) {

        var = kos_find_var(scope->vars, token);

        if (var && var->is_active) {
            is_local = scope == fun_scope || ! scope->next ? 1 : 0; /* current func's arguments are "local" */
            break;
        }

        var = 0;
    }

    assert(var);

    if ( ! is_local && var) {

        assert(var->type == VAR_INDEPENDENT_LOCAL    ||
               var->type == VAR_INDEPENDENT_ARGUMENT ||
               var->type == VAR_INDEPENDENT_ARG_IN_REG);

        assert(var->num_reads || var->num_assignments);

        _update_scope_ref(program, var->type, scope);
    }

    *out_var = var;
}

static KOS_SCOPE *_push_scope(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node)
{
    KOS_SCOPE *scope = (KOS_SCOPE *)
            kos_red_black_find(program->scopes, (void *)node, kos_scope_compare_item);

    assert(scope);

    assert(scope->next == program->scope_stack);

    kos_deactivate_vars(scope);

    program->scope_stack = scope;

    return scope;
}

static void _pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope = program->scope_stack;

    assert(scope);

    program->scope_stack = scope->next;
}

static int _visit_child_nodes(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node; node = node->next) {
        error = _visit_node(program, node);
        if (error)
            break;
    }

    return error;
}

static int _scope(KOS_COMP_UNIT *program,
                  KOS_AST_NODE  *node)
{
    int error;

    _push_scope(program, node);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    return error;
}

static void _update_arguments(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *node)
{
    int           num_non_def    = 0;
    int           num_def        = 0;
    int           have_ellipsis  = 0;
    int           max_used       = -1;
    int           num_args       = 0;
    int           max_indep_arg  = -1;
    unsigned      have_rest      = 0;
    int           i;
    KOS_SCOPE    *scope          = program->scope_stack;
    KOS_AST_NODE *arg_node;

    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    for (i = 0, arg_node = node; arg_node; arg_node = arg_node->next, ++i) {

        KOS_AST_NODE *ident_node;
        KOS_VAR      *var;

        switch (arg_node->type) {

            case NT_IDENTIFIER:
                ident_node = arg_node;
                ++num_non_def;
                break;

            case NT_ASSIGNMENT:
                assert( ! arg_node->next || arg_node->next->type != NT_IDENTIFIER);
                assert(arg_node->children);
                ident_node = arg_node->children;
                assert(ident_node->type == NT_IDENTIFIER);
                ++num_def;
                break;

            default:
                assert(arg_node->type == NT_ELLIPSIS);
                assert( ! arg_node->next);
                assert(arg_node->children);
                ident_node = arg_node->children;
                assert(ident_node->type == NT_IDENTIFIER);
                break;
        }

        var = kos_find_var(scope->vars, &ident_node->token);
        assert(var);

        if (var->num_reads || var->num_assignments) {
            if (arg_node->type == NT_ELLIPSIS)
                have_ellipsis = 1;
            else
                max_used = i;
        }
    }

    num_args  = have_ellipsis ? num_non_def + num_def : max_used + 1;
    have_rest = num_args > (int)_KOS_MAX_ARGS_IN_REGS;

    for (i = 0, arg_node = node; arg_node && arg_node->type != NT_ELLIPSIS; arg_node = arg_node->next, ++i) {

        KOS_AST_NODE *ident_node = arg_node->type == NT_IDENTIFIER ? arg_node : arg_node->children;
        KOS_VAR      *var        = kos_find_var(scope->vars, &ident_node->token);

        assert(ident_node->type == NT_IDENTIFIER);
        assert(var);

        assert(var->type == VAR_ARGUMENT || var->type == VAR_INDEPENDENT_ARGUMENT);

        if ( ! have_rest || (i < (int)_KOS_MAX_ARGS_IN_REGS - 1)) {

            if (var->type == VAR_INDEPENDENT_ARGUMENT) {
                assert(var->num_reads || var->num_assignments);
                var->type     = VAR_INDEPENDENT_ARG_IN_REG;
                max_indep_arg = i;
            }
            else
                var->type = VAR_ARGUMENT_IN_REG;
        }
        else
            var->array_idx -= (int)_KOS_MAX_ARGS_IN_REGS - 1;
    }

    scope->num_args       = num_args;
    scope->num_indep_args = max_indep_arg + 1;
    scope->have_rest      = have_rest;
    if ( ! have_ellipsis)
        scope->ellipsis = 0;
    assert( ! have_ellipsis || scope->ellipsis);
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

    while (node && node->type == NT_IDENTIFIER)
        node = node->next;

    for ( ; node && node->type != NT_ELLIPSIS; node = node->next) {

        KOS_AST_NODE *def_node = node->children;

        assert(node->type == NT_ASSIGNMENT);

        assert(def_node);
        assert(def_node->type == NT_IDENTIFIER);
        def_node = def_node->next;
        assert(def_node);
        assert( ! def_node->next);

        TRY(_visit_node(program, def_node));
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

    _update_arguments(program, name_node->next);

    error = _visit_child_nodes(program, node);

    _pop_scope(program);

    if ( ! error)
        error = _parameter_defaults(program, name_node->next, name_node);

    return error;
}

static void _identifier(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    KOS_VAR *var = 0;
    _lookup_var(program, &node->token, 1, &var);
    assert(var);
}

static int _assignment(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    int           error     = KOS_SUCCESS;
    int           is_lhs;
    KOS_AST_NODE *lhs_node  = node->children;
    KOS_AST_NODE *rhs_node;

    assert(lhs_node);
    assert(lhs_node->next);
    assert( ! lhs_node->next->next);

    is_lhs = lhs_node->type == NT_LEFT_HAND_SIDE ? 1 : 0;

    assert(lhs_node->type == NT_LEFT_HAND_SIDE ||
           lhs_node->type == NT_VAR            ||
           lhs_node->type == NT_CONST);

    assert((node->type == NT_ASSIGNMENT       && ! lhs_node->children->next) ||
           (node->type == NT_MULTI_ASSIGNMENT && lhs_node->children->next));

    node = lhs_node->children;
    assert(node);

    rhs_node = lhs_node->next;
    assert(rhs_node);
    assert( ! rhs_node->next);

    kos_activate_self_ref_func(program, lhs_node);

    TRY(_visit_node(program, rhs_node));

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_VAR *var = 0;

            _lookup_var(program, &node->token, is_lhs, &var);

            if ( ! is_lhs && var->is_active == VAR_INACTIVE)
                var->is_active = VAR_ACTIVE;
        }
        else {
            assert(node->type != NT_LINE_LITERAL && node->type != NT_THIS_LITERAL);
            TRY(_visit_node(program, node));
        }
    }

cleanup:
    return error;
}

static int _try_stmt(KOS_COMP_UNIT *program,
                     KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    const KOS_NODE_TYPE node_type = node->type;

    _push_scope(program, node);

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node));

    node = node->next;
    assert(node);
    assert( ! node->next);
    if (node_type == NT_TRY_CATCH) {

        KOS_AST_NODE *var_node = node->children;
        KOS_AST_NODE *scope_node;
        KOS_VAR      *var;

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

        _lookup_var(program, &var_node->token, 0, &var);

        assert(var);
        assert(var->is_active == VAR_INACTIVE);

        var->is_active = VAR_ACTIVE;

        TRY(_visit_node(program, scope_node));

        var->is_active = VAR_INACTIVE;
    }
    else
        TRY(_visit_node(program, node));

    _pop_scope(program);

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

static int _visit_node(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {

        case NT_SCOPE:
            /* fall through */
        default:
            assert(node->type == NT_SCOPE);
            error = _scope(program, node);
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

        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = _try_stmt(program, node);
            break;

        case NT_FOR_IN:
            error = _for_in_stmt(program, node);
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
        case NT_LINE_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            /* fall through */
        case NT_BREAK:
            /* fall through */
        case NT_CONTINUE:
            assert( ! node->children);
            /* fall through */
        case NT_PARAMETERS:
            /* fall through */
        case NT_ELLIPSIS:
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
            /* fall through */
        case NT_RETURN:
            /* fall through */
        case NT_THROW:
            /* fall through */
        case NT_IF:
            /* fall through */
        case NT_REPEAT:
            /* fall through */
        case NT_FOR:
            /* fall through */
        case NT_SWITCH:
            /* fall through */
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            /* fall through */
        case NT_OPERATOR:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            error = _visit_child_nodes(program, node);
            break;
    }

    return error;
}

int kos_allocate_args(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *ast)
{
    assert(ast->type == NT_SCOPE);
    return _visit_node(program, ast);
}

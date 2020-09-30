/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_compiler.h"
#include "kos_config.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node);

static void update_scope_ref(KOS_COMP_UNIT *program,
                             int            var_type,
                             KOS_SCOPE     *closure)
{
    KOS_SCOPE *scope;

    /* Find function owning the variable's scope */
    while (closure->parent_scope && ! closure->is_function)
        closure = closure->parent_scope;

    /* Reference the function in all inner scopes which use it */
    for (scope = program->scope_stack; scope != closure; scope = scope->parent_scope)
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

static void lookup_var(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       int                 only_active,
                       KOS_VAR           **out_var)
{
    KOS_VAR *var = node->u.var;

    assert( ! node->is_scope);
    assert(node->is_var);
    assert(var);

    if ( ! node->is_local_var && ! node->is_const_fun) {

        if (only_active) {
            assert(var->is_active);
        }

        assert(var->type == VAR_INDEPENDENT_LOCAL    ||
               var->type == VAR_INDEPENDENT_ARGUMENT ||
               var->type == VAR_INDEPENDENT_ARG_IN_REG);

        assert(var->num_reads || var->num_assignments);

        update_scope_ref(program, var->type, var->scope);
    }

    *out_var = var;
}

static KOS_SCOPE *push_scope(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node)
{
    KOS_SCOPE *const scope = node->u.scope;

    assert(node->is_scope);
    assert(scope);

    assert(scope->parent_scope == program->scope_stack);

    kos_deactivate_vars(scope);

    program->scope_stack = scope;

    if (scope->has_frame)
        program->cur_frame = (KOS_FRAME *)scope;

    return scope;
}

static void pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope = program->scope_stack;

    assert(scope);

    program->scope_stack = scope->parent_scope;

    if (scope->has_frame)
        program->cur_frame = ((KOS_FRAME *)scope)->parent_frame;
}

static int visit_child_nodes(KOS_COMP_UNIT *program,
                             KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node && ! error; node = node->next)
        error = visit_node(program, node);

    return error;
}

static int process_scope(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node)
{
    int error;

    push_scope(program, node);

    error = visit_child_nodes(program, node);

    pop_scope(program);

    return error;
}

static void update_arguments(KOS_COMP_UNIT *program,
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
    node     = node->children;
    arg_node = node;

    for (i = 0; arg_node; ++i) {

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

        assert( ! ident_node->is_scope);
        assert(ident_node->is_var);
        var = ident_node->u.var;
        assert(var);
        assert(var == kos_find_var(scope->vars, &ident_node->token));

        if (var->num_reads || var->num_assignments) {
            if (arg_node->type == NT_ELLIPSIS)
                have_ellipsis = 1;
            else
                max_used = i;
        }

        arg_node = arg_node->next;
    }

    num_args  = have_ellipsis ? num_non_def + num_def : max_used + 1;
    have_rest = num_args > (int)KOS_MAX_ARGS_IN_REGS;
    arg_node  = node;

    for (i = 0; arg_node && (arg_node->type != NT_ELLIPSIS); ++i) {

        KOS_AST_NODE *ident_node = arg_node->type == NT_IDENTIFIER ? arg_node : arg_node->children;
        KOS_VAR      *var        = ident_node->u.var;

        assert( !  ident_node->is_scope);
        assert(ident_node->is_var);
        assert(ident_node->type == NT_IDENTIFIER);
        assert(var);
        assert(var == kos_find_var(scope->vars, &ident_node->token));

        assert(var->type == VAR_ARGUMENT || var->type == VAR_INDEPENDENT_ARGUMENT);

        if ( ! have_rest || (i < (int)KOS_MAX_ARGS_IN_REGS - 1)) {

            if (var->type == VAR_INDEPENDENT_ARGUMENT) {
                assert(var->num_reads || var->num_assignments);
                var->type     = VAR_INDEPENDENT_ARG_IN_REG;
                max_indep_arg = i;
            }
            else
                var->type = VAR_ARGUMENT_IN_REG;
        }
        else
            var->array_idx -= (int)KOS_MAX_ARGS_IN_REGS - 1;

        arg_node = arg_node->next;
    }

    scope->num_args       = num_args;
    scope->num_indep_args = max_indep_arg + 1;
    scope->have_rest      = have_rest;
    if ( ! have_ellipsis)
        scope->ellipsis = 0;
    assert( ! have_ellipsis || scope->ellipsis);
}

static int parameter_defaults(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *node,
                              KOS_AST_NODE  *name_node)
{
    int error = KOS_SUCCESS;

    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    while (node && node->type == NT_IDENTIFIER)
        node = node->next;

    for ( ; node && (node->type != NT_ELLIPSIS); node = node->next) {

        KOS_AST_NODE *def_node = node->children;

        assert(node->type == NT_ASSIGNMENT);

        assert(def_node);
        assert(def_node->type == NT_IDENTIFIER);
        def_node = def_node->next;
        assert(def_node);
        assert( ! def_node->next);

        TRY(visit_node(program, def_node));
    }

cleanup:
    return error;
}

static int function_literal(KOS_COMP_UNIT *program,
                            KOS_AST_NODE  *node,
                            KOS_VAR       *fun_var)
{
    int           error;
    KOS_AST_NODE *name_node;

    push_scope(program, node);

    name_node = node->children;
    assert(name_node);

    update_arguments(program, name_node->next);

    node = name_node->next;
    assert(node);
    assert(node->type == NT_PARAMETERS);

    node = node->next;
    assert(node);
    assert(node->type == NT_LANDMARK);

    node = node->next;
    assert(node);
    assert(node->type == NT_SCOPE);
    assert(node->next);
    assert(node->next->type == NT_LANDMARK);
    assert( ! node->next->next);

    kos_activate_self_ref_func(program, fun_var);

    error = visit_node(program, node);

    kos_deactivate_self_ref_func(program, fun_var);

    pop_scope(program);

    if ( ! error)
        error = parameter_defaults(program, name_node->next, name_node);

    return error;
}

static int class_literal(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node,
                         KOS_VAR       *fun_var)
{
    int           error;
    KOS_AST_NODE *ctor_node;
    KOS_AST_NODE *prop_node;

    assert(node->type == NT_CLASS_LITERAL);

    /* extends clause */
    node = node->children;
    assert(node);
    TRY(visit_node(program, node));

    /* Prototype */
    node = node->next;
    assert(node);
    assert(node->type == NT_OBJECT_LITERAL);
    ctor_node = node->next;
    for (prop_node = node->children; prop_node; prop_node = prop_node->next) {
        assert(prop_node->type == NT_PROPERTY);

        node = prop_node->children;
        assert(node);
        assert(node->type == NT_STRING_LITERAL);
        TRY(visit_node(program, node));

        node = node->next;
        assert(node);
        assert( ! node->next);
        assert(node->type != NT_CONSTRUCTOR_LITERAL);
        if (node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, node, fun_var));
        else
            TRY(visit_node(program, node));
    }

    /* Constructor */
    assert(ctor_node);
    assert(ctor_node->type == NT_CONSTRUCTOR_LITERAL);
    assert( ! ctor_node->next);
    TRY(function_literal(program, ctor_node, fun_var));

cleanup:
    return error;
}

static void identifier(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node)
{
    KOS_VAR *var = 0;
    lookup_var(program, node, 1, &var);
    assert(var);
}

static int assignment(KOS_COMP_UNIT *program,
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

    if (kos_is_self_ref_func(lhs_node)) {

        KOS_VAR *fun_var = lhs_node->children->u.var;
        assert( ! lhs_node->children->is_scope);
        assert(lhs_node->children->is_var);
        assert(fun_var);
        assert( ! fun_var->is_active);

        if (rhs_node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, rhs_node, fun_var));
        else {
            assert(rhs_node->type == NT_CLASS_LITERAL);
            TRY(class_literal(program, rhs_node, fun_var));
        }
    }
    else
        TRY(visit_node(program, rhs_node));

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_VAR *var = 0;

            lookup_var(program, node, is_lhs, &var);

            if ( ! is_lhs && var->is_active == VAR_INACTIVE)
                var->is_active = VAR_ACTIVE;
        }
        else {
            assert(node->type != NT_LINE_LITERAL &&
                   node->type != NT_THIS_LITERAL &&
                   node->type != NT_SUPER_PROTO_LITERAL);
            TRY(visit_node(program, node));
        }
    }

cleanup:
    return error;
}

static int try_stmt(KOS_COMP_UNIT *program,
                    KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    const KOS_NODE_TYPE node_type = (KOS_NODE_TYPE)node->type;

    push_scope(program, node);

    node = node->children;
    assert(node);
    TRY(visit_node(program, node));

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

        lookup_var(program, var_node, 0, &var);

        assert(var);
        assert(var->is_active == VAR_INACTIVE);

        var->is_active = VAR_ACTIVE;

        TRY(visit_node(program, scope_node));

        var->is_active = VAR_INACTIVE;
    }
    else
        TRY(visit_node(program, node));

    pop_scope(program);

cleanup:
    return error;
}

static int for_in_stmt(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    int error;

    push_scope(program, node);

    assert(node->children);
    assert(node->children->children);
    kos_activate_new_vars(program, node->children->children);

    error = visit_child_nodes(program, node);

    pop_scope(program);

    return error;
}

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {

        case NT_SCOPE:
            /* fall through */
        default:
            assert(node->type == NT_SCOPE);
            error = process_scope(program, node);
            break;

        case NT_FUNCTION_LITERAL:
            error = function_literal(program, node, 0);
            break;

        case NT_CLASS_LITERAL:
            error = class_literal(program, node, 0);
            break;

        case NT_IDENTIFIER:
            identifier(program, node);
            error = KOS_SUCCESS;
            break;

        case NT_ASSIGNMENT:
            /* fall through */
        case NT_MULTI_ASSIGNMENT:
            error = assignment(program, node);
            break;

        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = try_stmt(program, node);
            break;

        case NT_FOR_IN:
            error = for_in_stmt(program, node);
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
            error = visit_child_nodes(program, node);
            break;
    }

    return error;
}

int kos_allocate_args(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *ast)
{
    PROF_ZONE(COMPILER)

    assert(ast->type == NT_SCOPE);

    return visit_node(program, ast);
}

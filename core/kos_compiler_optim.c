/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_compiler.h"
#include "kos_misc.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char str_err_div_by_zero[]             = "division by zero";
static const char str_err_number_out_of_range[]     = "number out of range";
static const char str_err_sum_of_strings_too_long[] = "sum of two strings exceeds 65535 characters";

enum KOS_TERMINATOR_E {
    TERM_NONE   = 0,
    TERM_BREAK  = 1,
    TERM_THROW  = 2,
    TERM_RETURN = 4
};

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node,
                      int           *is_terminal);

static void collapse(KOS_AST_NODE    *node,
                     KOS_NODE_TYPE    node_type,
                     KOS_TOKEN_TYPE   token_type,
                     KOS_KEYWORD_TYPE keyword,
                     const char      *begin,
                     uint16_t         length)
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

static int collapse_numeric(KOS_COMP_UNIT     *program,
                            KOS_AST_NODE      *node,
                            const KOS_NUMERIC *value)
{
    int            error;
    const uint16_t length = sizeof(KOS_NUMERIC);
    KOS_NUMERIC   *store  = (KOS_NUMERIC *)kos_mempool_alloc(&program->allocator, length);

    if (store) {

        *store = *value;

        collapse(node, NT_NUMERIC_LITERAL, TT_NUMERIC_BINARY, KW_NONE, (const char *)store, length);

        ++program->num_optimizations;

        error = KOS_SUCCESS;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void promote(KOS_COMP_UNIT      *program,
                    KOS_AST_NODE       *node,
                    const KOS_AST_NODE *child)
{
    KOS_SCOPE *const scope = child->u.scope;

    assert( ! node->is_scope);

    if (child->is_scope) {
        assert(scope->scope_node == child);
        scope->scope_node = node;
        node->u.scope     = scope;
        node->is_scope    = 1;
        node->is_var      = 0;
    }
    else {
        node->u.var    = child->u.var;
        node->is_scope = 0;
        node->is_var   = 1;
    }

    node->children     = child->children;
    node->token        = child->token;
    node->type         = child->type;
    node->is_local_var = child->is_local_var;
    node->is_const_fun = child->is_const_fun;
}

static int get_nonzero(const KOS_TOKEN *token, int *non_zero)
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

static void lookup_var(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       int                 only_active,
                       KOS_VAR           **out_var,
                       int                *is_local)
{
    KOS_VAR *var = node->u.var;

    assert( ! node->is_scope);
    assert(node->is_var);

    if (only_active) {
        assert(var->is_active);
    }

    *out_var = var;
    if (is_local)
        *is_local = node->is_local_var;
}

const KOS_AST_NODE *kos_get_const(KOS_COMP_UNIT      *program,
                                  const KOS_AST_NODE *node)
{
    KOS_VAR *var = 0;

    if ( ! node || node->type != NT_IDENTIFIER)
        return node;

    lookup_var(program, node, 1, &var, 0);

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
        (node->type == NT_CLASS_LITERAL)       ||
        (node->type == NT_ARRAY_LITERAL)       ||
        (node->type == NT_OBJECT_LITERAL)      ||
        (node->type == NT_INTERPOLATED_STRING)) {

        return 1;
    }

    if (node->type == NT_NUMERIC_LITERAL) {
        int       value = 0;
        const int error = get_nonzero(&node->token, &value);
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
        const int error = get_nonzero(&node->token, &value);
        if ( ! error)
            return ! value;
    }

    return 0;
}

static int reset_var_state(KOS_RED_BLACK_NODE *node,
                           void               *cookie)
{
    KOS_VAR *var = (KOS_VAR *)node;

    if (var->is_active == VAR_ACTIVE)
        var->is_active = VAR_INACTIVE;

    var->num_reads_prev    = var->num_reads;
    var->num_reads         = 0;
    var->num_assignments   = 0;
    var->local_reads       = 0;
    var->local_assignments = 0;

    return KOS_SUCCESS;
}

static KOS_SCOPE *push_scope(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node)
{
    KOS_SCOPE *const scope = node->u.scope;

    assert(node->is_scope);
    assert(scope);

    assert(scope->parent_scope == program->scope_stack);

    kos_red_black_walk(scope->vars, reset_var_state, 0);

    program->scope_stack = scope;

    scope->num_vars       = 0;
    scope->num_indep_vars = 0;

    if (scope->has_frame) {
        KOS_FRAME *const frame = (KOS_FRAME *)scope;
        program->cur_frame     = frame;
        frame->num_binds       = 0;
        frame->uses_base_proto = 0;
        frame->is_open         = 1;
    }

    return scope;
}

typedef struct UPDATE_VARS_S {
    KOS_COMP_UNIT *program;
    KOS_SCOPE     *scope;
} UPDATE_VARS;

static int count_and_update_vars(KOS_RED_BLACK_NODE *node,
                                 void               *cookie)
{
    KOS_VAR   *var              = (KOS_VAR *)node;
    KOS_SCOPE *scope            = ((UPDATE_VARS *)cookie)->scope;
    int        trigger_opt_pass = 0;

    /* Change to const if the variable was never modified */
    if ((var->type & VAR_LOCALS_AND_ARGS) && ! var->is_const && ! var->num_assignments) {

        trigger_opt_pass = 1;
        var->is_const    = 1;
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
        assert(scope->is_function || ! scope->parent_scope);
    }

    /* Trigger another optimization pass if variable is not needed */
    if ((var->num_assignments || var->num_reads_prev != var->num_reads) && ! var->num_reads && var->type != VAR_GLOBAL)
        trigger_opt_pass = 1;

    ((UPDATE_VARS *)cookie)->program->num_optimizations += trigger_opt_pass;

    return KOS_SUCCESS;
}

static int is_dummy_load(KOS_AST_NODE *node)
{
    switch (node->type) {

        case NT_IDENTIFIER:
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
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            /* fall through */
        case NT_CLASS_LITERAL:
            /* fall through */
            return 1;

        default:
            break;
    }
    return 0;
}

static void pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE  *scope = program->scope_stack;
    UPDATE_VARS update_ctx;

    update_ctx.program = program;
    update_ctx.scope   = scope;

    assert(scope);

    kos_red_black_walk(scope->vars, count_and_update_vars, &update_ctx);

    if ( ! scope->is_function && scope->parent_scope) {
        scope->parent_scope->num_vars       += scope->num_vars;
        scope->parent_scope->num_indep_vars += scope->num_indep_vars;
    }

    program->scope_stack = scope->parent_scope;

    if (scope->has_frame) {
        KOS_FRAME *const frame = program->cur_frame;
        assert(frame);

        /* Record a potential for optimizing a function load for self-referencing function */
        if (frame->num_self_refs && ! frame->num_binds && frame->num_binds_prev)
            ++program->num_optimizations;

        frame->is_open        =  0;
        frame->num_binds_prev =  frame->num_binds;

        if (frame->num_binds)
            frame->num_binds += frame->num_self_refs;

        program->cur_frame = ((KOS_FRAME *)scope)->parent_frame;
    }
}

static int process_scope(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node,
                         int           *is_terminal)
{
    int            error    = KOS_SUCCESS;
    KOS_AST_NODE **node_ptr = &node->children;
    const int      global   = ! program->scope_stack;

    push_scope(program, node);

    *is_terminal = TERM_NONE;

    for (;;) {

        node = *node_ptr;

        if ( ! node)
            break;

        /* Remove unneeded references to constants or variables */
        if (is_dummy_load(node) && (node->next || ! global)) {
            *node_ptr = node->next;
            continue;
        }

        /* TODO change array and object literals to their contents only */

        error = visit_node(program, node, is_terminal);
        if (error)
            break;

        if (*is_terminal && program->optimize && node->next) {
            node->next = 0;
            ++program->num_optimizations;
        }

        node_ptr = &node->next;
    }

    pop_scope(program);

    return error;
}

static int if_stmt(KOS_COMP_UNIT *program,
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
    TRY(visit_node(program, node, &t1));

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
        collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_TRUE, 0, 0);
        if (node->next->next)
            node->next = node->next->next;
        else
            collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
        is_truthy = 1;
    }

    node = node->next;

    *is_terminal = TERM_NONE;

    TRY(visit_node(program, node, &t1));

    if (node->next) {
        assert( ! node->next->next);
        TRY(visit_node(program, node->next, &t2));

        if (t1 && t2)
            *is_terminal = t1 | t2;
    }
    else if (is_truthy)
        *is_terminal = t1;

cleanup:
    return error;
}

static int repeat_stmt(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node,
                       int           *is_terminal)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);
    TRY(visit_node(program, node, is_terminal));

    node = node->next;
    assert(node);
    assert( ! node->next);

    if (*is_terminal && program->optimize) {
        if (node->token.keyword != KW_FALSE) {
            collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
            ++program->num_optimizations;
        }
    }
    else {
        int t;
        TRY(visit_node(program, node, &t));
        assert(t == TERM_NONE);
    }

    if (*is_terminal & TERM_BREAK)
        *is_terminal = TERM_NONE;

cleanup:
    return error;
}

static int for_stmt(KOS_COMP_UNIT *program,
                    KOS_AST_NODE  *node,
                    int           *is_terminal)
{
    int error     = KOS_SUCCESS;
    int is_truthy = 0;
    int is_falsy  = 0;
    int t;

    node = node->children;
    assert(node);
    TRY(visit_node(program, node, &t));
    assert(t == TERM_NONE);

    if (program->optimize) {
        is_truthy = node->type == NT_EMPTY || kos_node_is_truthy(program, node);
        is_falsy  = is_truthy ? 0 : kos_node_is_falsy(program, node);
    }

    node = node->next;
    assert(node);
    if (is_falsy && node->type != NT_EMPTY) {
        collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    TRY(visit_node(program, node, &t));
    assert(t == TERM_NONE);

    assert(node->next);
    assert( ! node->next->next);

    if (is_falsy && node->next->type != NT_EMPTY) {
        collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

    TRY(visit_node(program, node->next, is_terminal));

    if ( ! is_truthy || (*is_terminal & TERM_BREAK))
        *is_terminal = TERM_NONE;

    if (*is_terminal && program->optimize && node->type != NT_EMPTY) {
        collapse(node, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);
        ++program->num_optimizations;
    }

cleanup:
    return error;
}

static int try_stmt(KOS_COMP_UNIT *program,
                    KOS_AST_NODE  *node,
                    int           *is_terminal)
{
    int error = KOS_SUCCESS;
    int t1;
    int t2    = TERM_NONE;
    int t3    = TERM_NONE;

    const KOS_NODE_TYPE node_type    = (KOS_NODE_TYPE)node->type;
    KOS_AST_NODE       *finally_node = 0;

    push_scope(program, node);

    node = node->children;
    assert(node);
    TRY(visit_node(program, node, &t1));

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

        lookup_var(program, var_node, 0, &var, 0);

        assert(var);
        assert(var->is_active == VAR_INACTIVE);

        var->is_active = VAR_ACTIVE;

        TRY(visit_node(program, scope_node, &t2));

        var->is_active = VAR_INACTIVE;
    }
    else {

        finally_node = node;

        TRY(visit_node(program, finally_node, &t3));
    }

    *is_terminal = TERM_NONE;

    if ( ! finally_node || finally_node->type == NT_EMPTY || ! t3) {
        if (t1 && t2)
            *is_terminal = (t1 & ~TERM_THROW) | t2;
    }
    else
        *is_terminal = t3;

    pop_scope(program);

cleanup:
    return error;
}

static int switch_stmt(KOS_COMP_UNIT *program,
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
    TRY(visit_node(program, node, &t));
    assert(t == TERM_NONE);

    t = TERM_NONE;

    for (node = node->next; node; node = node->next) {

        int next_t;

        if (node->type == NT_DEFAULT)
            has_default = 1;

        TRY(visit_node(program, node, &next_t));

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

static int case_stmt(KOS_COMP_UNIT *program,
                     KOS_AST_NODE  *node,
                     int           *is_terminal)
{
    int error = KOS_SUCCESS;
    int t;

    node = node->children;
    assert(node);
    TRY(visit_node(program, node, &t));
    assert(t == TERM_NONE);

    node = node->next;
    assert(node);
    assert( ! node->next
           || node->next->type == NT_FALLTHROUGH
           || node->next->type == NT_EMPTY);

    TRY(visit_node(program, node, is_terminal));

    if (*is_terminal && node->next)
        collapse(node->next, NT_EMPTY, TT_IDENTIFIER, KW_NONE, 0, 0);

cleanup:
    return error;
}

static int visit_child_nodes(KOS_COMP_UNIT *program,
                             KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;
    int t     = TERM_NONE;

    for (node = node->children; node; node = node->next)
        TRY(visit_node(program, node, &t));

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

static int parameter_defaults(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *node,
                              KOS_AST_NODE  *name_node,
                              int           *out_num_def_used)
{
    int error        = KOS_SUCCESS;
    int num_def      = 0;
    int num_def_used = 0;

    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    for ( ; node && node->type != NT_ELLIPSIS; node = node->next) {

        if (node->type == NT_ASSIGNMENT) {

            KOS_VAR      *var;
            KOS_AST_NODE *def_node = node->children;
            int           is_terminal;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            TRY(visit_node(program, def_node, &is_terminal));

            lookup_var(program, node->children, 1, &var, 0);
            assert(var);

            ++num_def;

            assert(var->num_reads || ! var->num_assignments);

            if (var->num_reads)
                num_def_used = num_def;
        }
    }

    if (node && (node->type == NT_ELLIPSIS)) {

        KOS_VAR *var;

        lookup_var(program, node->children, 1, &var, 0);
        assert(var);

        assert(var->num_reads || ! var->num_assignments);

        if (var->num_reads)
            num_def_used = num_def;
    }

    *out_num_def_used = num_def_used;

cleanup:
    return error;
}

static int function_literal(KOS_COMP_UNIT *program,
                            KOS_AST_NODE  *fun_node,
                            KOS_VAR       *fun_var)
{
    int           error;
    int           t;
    int           num_optimizations = program->num_optimizations;
    KOS_AST_NODE *node;
    KOS_AST_NODE *name_node;

    name_node = fun_node->children;
    assert(name_node);

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

    do {
        int        num_def_used = 0;
        KOS_FRAME *frame;

        program->num_optimizations = 0;

        frame = (KOS_FRAME *)push_scope(program, fun_node);
        assert(frame->scope.has_frame);

        kos_activate_self_ref_func(program, fun_var);

        error = visit_node(program, node, &t);

        kos_deactivate_self_ref_func(program, fun_var);

        pop_scope(program);

        if ( ! error)
            error = parameter_defaults(program, name_node->next, name_node, &num_def_used);

        num_optimizations += program->num_optimizations;

        if (num_def_used < frame->num_def_used)
            ++num_optimizations;

        frame->num_def_used = num_def_used;

    } while ( ! error && program->num_optimizations);

    program->num_optimizations = num_optimizations;

    return error;
}

static int class_literal(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node,
                         KOS_VAR       *fun_var)
{
    int           error;
    int           t;
    KOS_AST_NODE *ctor_node;
    KOS_AST_NODE *prop_node;

    assert(node->type == NT_CLASS_LITERAL);

    /* extends clause */
    node = node->children;
    assert(node);
    TRY(visit_node(program, node, &t));
    assert(t == TERM_NONE);

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
        TRY(visit_node(program, node, &t));
        assert(t == TERM_NONE);

        node = node->next;
        assert(node);
        assert( ! node->next);
        assert(node->type != NT_CONSTRUCTOR_LITERAL);
        if (node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, node, fun_var));
        else {
            TRY(visit_node(program, node, &t));
            assert(t == TERM_NONE);
        }
    }

    /* Constructor */
    assert(ctor_node);
    assert(ctor_node->type == NT_CONSTRUCTOR_LITERAL);
    assert( ! ctor_node->next);
    TRY(function_literal(program, ctor_node, fun_var));

cleanup:
    return error;
}

static int is_const_fun(KOS_VAR *var)
{
    const KOS_AST_NODE *const fun_node = var->value;
    KOS_FRAME                *frame;

    assert(var->is_const);
    assert(fun_node);
    assert(fun_node->type == NT_FUNCTION_LITERAL ||
           fun_node->type == NT_CONSTRUCTOR_LITERAL);

    /* TODO Add support for constructors */
    if (fun_node->type == NT_CONSTRUCTOR_LITERAL)
        return 0;

    /* Function which requires binding defaults must be passed through a closure */
    assert(fun_node->is_scope);
    frame = (KOS_FRAME *)fun_node->u.scope;
    assert(frame);
    assert(frame->scope.has_frame);

    if (frame->num_def_used)
        return 0;

    /* Function which uses independent variables from outer scopes must be passed
     * through a closure */
    if (frame->num_binds)
        return 0;

    /* For self-referencing functions, make sure that there are no independent variable
     * references after referencing the function. */
    if (frame->is_open && frame->num_binds_prev)
        return 0;

    return 1;
}

static int check_self_ref_fun(KOS_VAR *var)
{
    const KOS_AST_NODE *const fun_node = var->value;
    KOS_FRAME                *frame;

    if ( ! var->is_const || ! fun_node)
        return 0;

    if (fun_node->type != NT_FUNCTION_LITERAL &&
        fun_node->type != NT_CONSTRUCTOR_LITERAL)
        return 0;

    assert(fun_node->is_scope);
    frame = (KOS_FRAME *)fun_node->u.scope;
    assert(frame);
    assert(frame->scope.has_frame);

    return frame->is_open;
}

static void mark_binds(KOS_COMP_UNIT *program,
                       KOS_VAR       *var)
{
    assert((var->type != VAR_LOCAL) && (var->type != VAR_ARGUMENT));
    assert(var->scope);

    if (var->type & VAR_INDEPENDENT) {

        KOS_FRAME       *frame           = program->cur_frame;
        KOS_FRAME *const target_frame    = var->scope->owning_frame;
        const int        is_self_ref_fun = check_self_ref_fun(var);

        assert(frame != target_frame);
        assert(frame);
        assert(target_frame);

        do {
            if (is_self_ref_fun)
                ++frame->num_self_refs;
            else
                ++frame->num_binds;

            frame = frame->parent_frame;
            assert(frame);
        } while (frame != target_frame);
    }
}

static void identifier(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    KOS_VAR *var;
    int      is_local;

    lookup_var(program, node, 1, &var, &is_local);

    if ( ! is_local && var->is_const && var->value) {

        const KOS_AST_NODE *const const_node = var->value;

        switch (const_node->type) {

            case NT_NUMERIC_LITERAL:
                /* fall through */
            case NT_STRING_LITERAL:
                /* fall through */
            case NT_BOOL_LITERAL:
                /* fall through */
            case NT_VOID_LITERAL:
                collapse(node,
                         (KOS_NODE_TYPE)const_node->type,
                         (KOS_TOKEN_TYPE)const_node->token.type,
                         (KOS_KEYWORD_TYPE)const_node->token.keyword,
                         const_node->token.begin,
                         const_node->token.length);

                ++program->num_optimizations;
                return;

            case NT_FUNCTION_LITERAL:
                /* fall through */
            case NT_CONSTRUCTOR_LITERAL:
                if (is_const_fun(var)) {
                    if ( ! node->is_const_fun) {
                        node->is_const_fun = 1;
                        ++program->num_optimizations;
                    }
                    is_local = 1; /* Treat as local variable */
                }
                break;

            default:
                break;
        }
    }

    ++var->num_reads;

    if (is_local)
        ++var->local_reads;
    else
        mark_binds(program, var);
}

static int assignment(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node)
{
    int               error     = KOS_SUCCESS;
    int               is_lhs;
    int               t;
    int               num_used  = 0;
    KOS_AST_NODE     *lhs_node  = node->children;
    KOS_AST_NODE     *rhs_node;
    KOS_AST_NODE     *assg_node = node;
    KOS_NODE_TYPE     assg_type = (KOS_NODE_TYPE)node->type;
    KOS_OPERATOR_TYPE assg_op   = (KOS_OPERATOR_TYPE)node->token.op;

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
    else {
        TRY(visit_node(program, rhs_node, &t));
        assert(t == TERM_NONE);
    }

    /* TODO optimize multi assignment from array literal (don't create array) */

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_VAR *var = 0;
            int      is_local;

            lookup_var(program, node, is_lhs, &var, &is_local);

            if ( ! is_lhs) {
                if (var->is_active == VAR_INACTIVE)
                    var->is_active = VAR_ACTIVE;

                if (assg_type == NT_ASSIGNMENT)
                    var->value = rhs_node;
            }

            if ( ! var->num_reads_prev && var->type != VAR_GLOBAL) {
                collapse(node, NT_VOID_LITERAL, TT_KEYWORD, KW_VOID, 0, 0);
                ++program->num_optimizations;
            }
            else {

                ++num_used;

                if (is_lhs) {
                    assert( ! var->is_const);
                    ++var->num_assignments;
                    if (assg_op != OT_SET)
                        ++var->num_reads;

                    if (is_local) {
                        ++var->local_assignments;
                        if (assg_op != OT_SET)
                            ++var->local_reads;
                    }
                    else
                        mark_binds(program, var);
                }
            }
        }
        else if (node->type != NT_VOID_LITERAL) {
            assert(node->type != NT_LINE_LITERAL &&
                   node->type != NT_THIS_LITERAL &&
                   node->type != NT_SUPER_PROTO_LITERAL);
            ++num_used;
            TRY(visit_node(program, node, &t));
        }
    }

    if (num_used == 0)
        promote(program, assg_node, rhs_node);

cleanup:
    return error;
}

static void announce_div_by_zero(KOS_COMP_UNIT      *program,
                                 const KOS_AST_NODE *node)
{
    program->error_str   = str_err_div_by_zero;
    program->error_token = &node->token;
}

static int optimize_binary_op(KOS_COMP_UNIT      *program,
                              KOS_AST_NODE       *node,
                              const KOS_AST_NODE *a,
                              const KOS_AST_NODE *b)
{
    int               error;
    KOS_OPERATOR_TYPE op = (KOS_OPERATOR_TYPE)node->token.op;
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
                const double value = (double)numeric_a.u.i;
                numeric_a.u.d  = value;
                numeric_a.type = KOS_FLOAT_VALUE;
            }

            if (numeric_b.type == KOS_INTEGER_VALUE) {
                const double value = (double)numeric_b.u.i;
                numeric_b.u.d  = value;
                numeric_b.type = KOS_FLOAT_VALUE;
            }
        }
    }
    else {
        assert(op != OT_ADD && op != OT_SUB && op != OT_MUL && op != OT_DIV && op != OT_MOD);

        if (numeric_a.type == KOS_FLOAT_VALUE) {
            if (numeric_a.u.d <= -9223372036854775808.0 || numeric_a.u.d >= 9223372036854775808.0) {
                program->error_str   = str_err_number_out_of_range;
                program->error_token = &a->token;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
            numeric_a.u.i = (int64_t)floor(numeric_a.u.d);
            numeric_a.type = KOS_INTEGER_VALUE;
        }

        if (numeric_b.type == KOS_FLOAT_VALUE) {
            if (numeric_b.u.d <= -9223372036854775808.0 || numeric_b.u.d >= 9223372036854775808.0) {
                program->error_str   = str_err_number_out_of_range;
                program->error_token = &b->token;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
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
                    announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.i /= numeric_b.u.i;
            }
            else {
                if (numeric_b.u.d == 0) {
                    announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.d /= numeric_b.u.d;
            }
            break;

        case OT_MOD:
            if (numeric_a.type == KOS_INTEGER_VALUE) {
                if ( ! numeric_b.u.i) {
                    announce_div_by_zero(program, node);
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
                numeric_a.u.i %= numeric_b.u.i;
            }
            else {
                if (numeric_b.u.d == 0) {
                    announce_div_by_zero(program, node);
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
            if (numeric_b.u.i > 63 || numeric_b.u.i < -62)
                numeric_a.u.i = (numeric_a.u.i < 0 && numeric_b.u.i < 0) ? -1 : 0;
            else if (numeric_b.u.i < 0)
                numeric_a.u.i >>= -numeric_b.u.i;
            else
                numeric_a.u.i = (int64_t)((uint64_t)numeric_a.u.i << numeric_b.u.i);
            break;

        case OT_SHR:
            if (numeric_b.u.i > 62 || numeric_b.u.i < -63)
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

    error = collapse_numeric(program, node, &numeric_a);

cleanup:
    return error;
}

static int optimize_unary_op(KOS_COMP_UNIT      *program,
                             KOS_AST_NODE       *node,
                             const KOS_AST_NODE *a)
{
    KOS_OPERATOR_TYPE op = (KOS_OPERATOR_TYPE)node->token.op;
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

    return collapse_numeric(program, node, &numeric_a);
}

static int is_raw_str(const KOS_AST_NODE *node)
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

static int add_strings(KOS_COMP_UNIT      *program,
                       KOS_AST_NODE       *node,
                       const KOS_AST_NODE *a,
                       const KOS_AST_NODE *b)
{
    char                *str;
    const KOS_TOKEN_TYPE a_type   = (KOS_TOKEN_TYPE)a->token.type;
    const KOS_TOKEN_TYPE b_type   = (KOS_TOKEN_TYPE)b->token.type;
    const char          *a_begin  = a->token.begin + 1;
    const char          *b_begin  = b->token.begin + 1;
    unsigned             a_length = a->token.length;
    unsigned             b_length = b->token.length;
    const int            is_raw   = is_raw_str(a);
    unsigned             pos;
    unsigned             new_length;

    assert(a->type == NT_STRING_LITERAL);
    assert(a_type == TT_STRING || a_type == TT_STRING_OPEN);
    assert((a_type == TT_STRING && a_length >= 2U) || a_length >= 3U);
    assert(b->type == NT_STRING_LITERAL);
    assert(b_type == TT_STRING || b_type == TT_STRING_OPEN);
    assert((b_type == TT_STRING && b_length >= 2U) || b_length >= 3U);
    assert(is_raw_str(b) == is_raw);

    a_length -= a_type == TT_STRING ? 2U : 3U;
    b_length -= b_type == TT_STRING ? 2U : 3U;

    if (is_raw) {
        ++a_begin;
        --a_length;
        ++b_begin;
        --b_length;
    }

    promote(program, node, a);

    new_length = a_length + b_length + (is_raw ? 3U : 2U);

    if (new_length >= 0xFFFFU) {
        program->error_str   = str_err_sum_of_strings_too_long;
        program->error_token = &node->token;
        return KOS_ERROR_COMPILE_FAILED;
    }

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
    node->token.length = (uint16_t)new_length;

    ++program->num_optimizations;

    return KOS_SUCCESS;
}

static void collapse_typeof(KOS_COMP_UNIT      *program,
                            KOS_AST_NODE       *node,
                            const KOS_AST_NODE *a)
{
    const KOS_NODE_TYPE a_type   = (KOS_NODE_TYPE)a->type;
    const char         *type     = 0;
    uint16_t            type_len = 0;

    switch (a_type) {

        case NT_NUMERIC_LITERAL: {
            static const char quoted_integer[] = "\"integer\"";
            static const char quoted_float[]   = "\"float\"";

            if (a->token.type == TT_NUMERIC_BINARY) {
                const KOS_NUMERIC *value = (const KOS_NUMERIC *)
                    a->token.begin;
                assert(a->token.length == sizeof(KOS_NUMERIC));
                if (value->type == KOS_INTEGER_VALUE) {
                    type     = quoted_integer;
                    type_len = sizeof(quoted_integer) - 1;
                }
                else {
                    assert(value->type == KOS_FLOAT_VALUE);
                    type     = quoted_float;
                    type_len = sizeof(quoted_float) - 1;
                }
            }
            else {
                if (kos_is_integer(a->token.begin, a->token.begin + a->token.length)) {
                    type     = quoted_integer;
                    type_len = sizeof(quoted_integer) - 1;
                }
                else {
                    type     = quoted_float;
                    type_len = sizeof(quoted_float) - 1;
                }
            }
            break;
        }

        case NT_STRING_LITERAL:
            /* fall through */
        case NT_INTERPOLATED_STRING: {
            static const char quoted_string[] = "\"string\"";
            type     = quoted_string;
            type_len = sizeof(quoted_string) - 1;
            break;
        }

        case NT_BOOL_LITERAL: {
            static const char quoted_boolean[] = "\"boolean\"";
            type     = quoted_boolean;
            type_len = sizeof(quoted_boolean) - 1;
            break;
        }

        case NT_VOID_LITERAL: {
            static const char quoted_void[] = "\"void\"";
            type     = quoted_void;
            type_len = sizeof(quoted_void) - 1;
            break;
        }

        default:
            break;
    }

    if (type) {
        collapse(node, NT_STRING_LITERAL, TT_STRING, KW_NONE, type, type_len);
        ++program->num_optimizations;
    }
}

static int operator_token(KOS_COMP_UNIT *program,
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

    TRY(visit_node(program, a, &t));
    assert(t == TERM_NONE);

    if (b) {
        TRY(visit_node(program, b, &t));
        assert(t == TERM_NONE);

        c = b->next;
        if (c) {
            assert( ! c->next);
            TRY(visit_node(program, c, &t));
            assert(t == TERM_NONE);
        }
    }

    if ( ! program->optimize)
        return KOS_SUCCESS;

    ca = kos_get_const(program, a);
    if (b)
        cb = kos_get_const(program, b);

    a_type = ca ? (KOS_NODE_TYPE)ca->type : NT_EMPTY;
    b_type = cb ? (KOS_NODE_TYPE)cb->type : NT_EMPTY;

    switch (node->token.op) {

        case OT_ADD:
            /* fall through */
        case OT_SUB: {
            if (b) {

                const KOS_OPERATOR_TYPE op = (KOS_OPERATOR_TYPE)node->token.op;

                if (a_type == NT_NUMERIC_LITERAL && b_type == NT_NUMERIC_LITERAL)
                    error = optimize_binary_op(program, node, ca, cb);

                else if (op == OT_ADD && a_type == NT_STRING_LITERAL && b_type == NT_STRING_LITERAL)
                    if (is_raw_str(ca) == is_raw_str(cb))
                        error = add_strings(program, node, ca, cb);
            }
            else {
                if (a_type == NT_NUMERIC_LITERAL)
                    error = optimize_unary_op(program, node, ca);
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
                error = optimize_binary_op(program, node, ca, cb);
            break;
        }

        case OT_NOT: {
            assert( ! b);
            if (a_type == NT_NUMERIC_LITERAL)
                error = optimize_unary_op(program, node, ca);
            break;
        }

        case OT_LOGNOT: {
            if (kos_node_is_truthy(program, ca) && a->token.keyword != KW_FALSE) {
                collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_FALSE, 0, 0);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && a->token.keyword != KW_TRUE) {
                collapse(node, NT_BOOL_LITERAL, TT_KEYWORD, KW_TRUE, 0, 0);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGAND: {
            if (kos_node_is_truthy(program, ca) && b) {
                promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca)) {
                promote(program, node, a);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGOR: {
            if (kos_node_is_truthy(program, ca)) {
                promote(program, node, a);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && b) {
                promote(program, node, b);
                ++program->num_optimizations;
            }
            break;
        }

        case OT_LOGTRI: {
            assert(b);
            if (kos_node_is_truthy(program, ca) && b) {
                promote(program, node, b);
                ++program->num_optimizations;
            }
            else if (kos_node_is_falsy(program, ca) && b) {
                assert(b->next);
                if (b->next) {
                    promote(program, node, b->next);
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
                collapse_typeof(program, node, ca);
            }
            break;

        default:
            break;
    }

cleanup:
    return error;
}

static void copy_node_as_string(KOS_AST_NODE       *dest,
                                const KOS_AST_NODE *src)
{
    *dest = *src;

    dest->type          = NT_STRING_LITERAL;
    dest->token.type    = TT_STRING;
    dest->token.keyword = KW_NONE;
    dest->token.op      = OT_NONE;
    dest->token.sep     = ST_NONE;
}

static int stringify(KOS_COMP_UNIT       *program,
                     const KOS_AST_NODE **node_ptr,
                     KOS_AST_NODE        *tmp_node)
{
    const KOS_NODE_TYPE type = (KOS_NODE_TYPE)(*node_ptr)->type;

    switch (type) {

        case NT_STRING_LITERAL:
            return 1;

        case NT_VOID_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL: {

            const KOS_KEYWORD_TYPE kw = (KOS_KEYWORD_TYPE)(*node_ptr)->token.keyword;

            copy_node_as_string(tmp_node, *node_ptr);

            if (type == NT_VOID_LITERAL) {
                static const char quoted_void[] = "\"void\"";
                tmp_node->token.begin  = quoted_void;
                tmp_node->token.length = sizeof(quoted_void) - 1;
            }
            else if (kw == KW_TRUE) {
                static const char quoted_true[] = "\"true\"";
                tmp_node->token.begin  = quoted_true;
                tmp_node->token.length = sizeof(quoted_true) - 1;
            }
            else {
                static const char quoted_false[] = "\"false\"";
                assert(kw == KW_FALSE);
                tmp_node->token.begin  = quoted_false;
                tmp_node->token.length = sizeof(quoted_false) - 1;
            }

            *node_ptr = tmp_node;
            return 1;
        }

        case NT_NUMERIC_LITERAL: {

            KOS_NUMERIC      numeric;
            const KOS_TOKEN *token    = &(*node_ptr)->token;
            char            *store;
            unsigned         len;
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

            copy_node_as_string(tmp_node, *node_ptr);

            store = (char *)kos_mempool_alloc(&program->allocator, max_size + 1);
            if ( ! store)
                return 0; /* Malloc errors are handled later */

            if (numeric.type == KOS_INTEGER_VALUE) {
                len = snprintf(store, max_size, "\"%" PRId64 "\"", numeric.u.i);
                if (len >= max_size)
                    len = max_size - 1;
            }
            else {
                unsigned size;

                assert(numeric.type == KOS_FLOAT_VALUE);

                store[0] = '"';

                size = kos_print_float(store + 1, max_size - 2, numeric.u.d);
                assert(size < max_size - 1);

                store[size + 1] = '"';
                store[size + 2] = 0;

                len = size + 2;
            }

            assert(len <= 0xFFFFU);

            tmp_node->token.begin  = store;
            tmp_node->token.length = (uint16_t)len;
            *node_ptr              = tmp_node;
            return 1;
        }

        default:
            return 0;
    }
}

static void remove_empty_strings(KOS_AST_NODE **node_ptr)
{
    unsigned num_children = 0;

    while (*node_ptr) {

        KOS_AST_NODE *node = *node_ptr;

        if (node->type == NT_STRING_LITERAL && (node->next || num_children)) {
            if ((node->token.type == TT_STRING      && node->token.length == 2) ||
                (node->token.type == TT_STRING_OPEN && node->token.length == 3)) {

                *node_ptr = node->next;
                continue;
            }
        }

        node_ptr = &node->next;
        ++num_children;
    }
}

static int interpolated_string(KOS_COMP_UNIT *program,
                               KOS_AST_NODE  *node)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *child = node->children;
    int           t;

    assert(child);

    TRY(visit_node(program, child, &t));
    assert(t == TERM_NONE);

    while (child && child->next) {

        KOS_AST_NODE       *next = child->next;
        const KOS_AST_NODE *ca   = kos_get_const(program, child);
        const KOS_AST_NODE *cb;
        KOS_AST_NODE        sa;
        KOS_AST_NODE        sb;

        TRY(visit_node(program, next, &t));
        assert(t == TERM_NONE);

        cb = kos_get_const(program, next);

        if (ca && cb &&
            stringify(program, &ca, &sa) && stringify(program, &cb, &sb) &&
            is_raw_str(ca) == is_raw_str(cb)) {

            TRY(add_strings(program, child, ca, cb));

            child->next = next->next;
        }
        else
            child = child->next;
    }

    assert(node->children);

    remove_empty_strings(&node->children);

    assert(node->children);

    if ( ! node->children->next && node->children->type == NT_STRING_LITERAL)
        promote(program, node, node->children);

cleanup:
    return error;
}

static int line(KOS_COMP_UNIT *program,
                KOS_AST_NODE  *node)
{
    KOS_NUMERIC numeric;

    assert( ! node->children);

    numeric.type = KOS_INTEGER_VALUE;
    numeric.u.i  = node->token.line;

    return collapse_numeric(program, node, &numeric);
}

static void super_proto_literal(KOS_COMP_UNIT *program)
{
    KOS_FRAME *const frame = program->cur_frame;

    assert(frame && frame->scope.is_function);

    frame->uses_base_proto = 1;
    ++frame->num_binds;
}

static int visit_node(KOS_COMP_UNIT *program,
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
            error        = visit_child_nodes(program, node);
            *is_terminal = TERM_RETURN;
            break;

        case NT_THROW:
            error        = visit_child_nodes(program, node);
            *is_terminal = TERM_THROW;
            break;

        case NT_BREAK:
            /* fall through */
        case NT_CONTINUE:
            assert( ! node->children);
            error        = KOS_SUCCESS;
            *is_terminal = TERM_BREAK;
            break;

        case NT_SCOPE:
            error = process_scope(program, node, is_terminal);
            break;
        case NT_IF:
            error = if_stmt(program, node, is_terminal);
            break;
        case NT_REPEAT:
            error = repeat_stmt(program, node, is_terminal);
            break;
        case NT_FOR:
            error = for_stmt(program, node, is_terminal);
            break;
        case NT_FOR_IN:
            error = for_in_stmt(program, node);
            break;
        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = try_stmt(program, node, is_terminal);
            break;
        case NT_SWITCH:
            error = switch_stmt(program, node, is_terminal);
            break;
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            error = case_stmt(program, node, is_terminal);
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

        case NT_OPERATOR:
            error = operator_token(program, node);
            break;

        case NT_INTERPOLATED_STRING:
            error = interpolated_string(program, node);
            break;

        case NT_LINE_LITERAL:
            error = line(program, node);
            break;

        case NT_SUPER_PROTO_LITERAL:
            super_proto_literal(program);
            error = KOS_SUCCESS;
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
            error = visit_child_nodes(program, node);
            break;
    }

    return error;
}

int kos_optimize(KOS_COMP_UNIT *program,
                 KOS_AST_NODE  *ast)
{
    PROF_ZONE(COMPILER)

    int t;

    assert(ast->type == NT_SCOPE);

    return visit_node(program, ast, &t);
}

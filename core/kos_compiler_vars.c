/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_compiler.h"
#include "kos_ast.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>

static const char str_err_const_assignment[]       = "const variable is not assignable";
static const char str_err_module_global_conflict[] = "unable to import module, a global variable with this name already exists";
static const char str_err_redefined_var[]          = "redefined variable";
static const char str_err_undefined_var[]          = "undeclared identifier";
static const char str_err_unexpected_global_this[] = "'this' not allowed in global scope";
static const char str_err_unexpected_yield[]       = "'yield' not allowed in global scope";

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node);

static int compare_tokens(const KOS_TOKEN *token_a,
                          const KOS_TOKEN *token_b)
{
    const unsigned len_a = token_a->length;
    const unsigned len_b = token_b->length;

    const unsigned min_len = (len_a <= len_b) ? len_a : len_b;

    int result = memcmp(token_a->begin, token_b->begin, min_len);

    if (result == 0)
        result = (int)len_a - (int)len_b;

    return result;
}

static int var_compare_node(KOS_RED_BLACK_NODE *a,
                            KOS_RED_BLACK_NODE *b)
{
    const KOS_TOKEN *token_a = ((const KOS_VAR *)a)->token;
    const KOS_TOKEN *token_b = ((const KOS_VAR *)b)->token;

    return compare_tokens(token_a, token_b);
}

static int var_compare_item(void               *what,
                            KOS_RED_BLACK_NODE *node)
{
    const KOS_TOKEN *token_a = (const KOS_TOKEN *)what;
    const KOS_TOKEN *token_b = ((const KOS_VAR *)node)->token;

    return compare_tokens(token_a, token_b);
}

static int scope_ref_compare_item(void               *what,
                                  KOS_RED_BLACK_NODE *node)
{
    const KOS_SCOPE     *closure = (const KOS_SCOPE *)            what;
    const KOS_SCOPE_REF *ref     = (const KOS_SCOPE_REF *)node;

    return (int)((intptr_t)closure - (intptr_t)ref->closure);
}

static int scope_ref_compare_node(KOS_RED_BLACK_NODE *a,
                                  KOS_RED_BLACK_NODE *b)
{
    const KOS_SCOPE_REF *ref_a = (const KOS_SCOPE_REF *)a;
    const KOS_SCOPE_REF *ref_b = (const KOS_SCOPE_REF *)b;

    return (int)((intptr_t)ref_a->closure - (intptr_t)ref_b->closure);
}

static KOS_VAR *alloc_var(KOS_COMP_UNIT      *program,
                          unsigned            is_const,
                          const KOS_AST_NODE *node)
{
    KOS_VAR *var = (KOS_VAR *)
        kos_mempool_alloc(&program->allocator, sizeof(KOS_VAR));

    if (var) {
        memset(var, 0, sizeof(*var));

        var->scope        = program->scope_stack;
        var->token        = &node->token;
        var->type         = VAR_LOCAL;
        var->is_const     = is_const;
        var->is_active    = VAR_ALWAYS_ACTIVE;
        var->num_reads    = -1;

        kos_red_black_insert(&program->scope_stack->vars,
                             (KOS_RED_BLACK_NODE *)var,
                             var_compare_node);
    }

    return var;
}

static int init_global_scope(KOS_COMP_UNIT *program)
{
    int error = KOS_SUCCESS;

    /* Register built-in module globals */
    KOS_PRE_GLOBAL *global = program->pre_globals;

    for ( ; global; global = global->next) {

        KOS_VAR *var = alloc_var(program, global->is_const, &global->node);

        if (var) {
            var->type        = global->type;
            var->array_idx   = global->idx;
            var->next        = program->globals;
            program->globals = var;
            ++program->num_globals;
        }
        else {
            error  = KOS_ERROR_OUT_OF_MEMORY;
            break;
        }
    }

    return error;
}

static int push_scope(KOS_COMP_UNIT *program,
                      int            alloc_frame,
                      KOS_AST_NODE  *node)
{
    int          error = KOS_SUCCESS;
    const size_t size  = alloc_frame ? sizeof(KOS_FRAME) : sizeof(KOS_SCOPE);

    KOS_SCOPE *const scope = (KOS_SCOPE *)kos_mempool_alloc(&program->allocator, size);

    if ( ! scope)
        error = KOS_ERROR_OUT_OF_MEMORY;

    else {

        memset(scope, 0, size);

        if (alloc_frame)
            scope->has_frame = 1;

        node->is_scope       = 1;
        node->u.scope        = scope;
        scope->scope_node    = node;
        scope->parent_scope  = program->scope_stack;
        program->scope_stack = scope;

        if ( ! scope->parent_scope)
            error = init_global_scope(program);
    }

    return error;
}

static void pop_scope(KOS_COMP_UNIT *program)
{
    program->scope_stack = program->scope_stack->parent_scope;
}

static int push_function(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node)
{
    int error = push_scope(program, 1, node);

    if ( ! error)
        program->scope_stack->is_function = 1;

    return error;
}

KOS_SCOPE *kos_get_frame_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope;

    if (program->cur_frame)
        scope = &program->cur_frame->scope;
    else {
        KOS_SCOPE *parent_scope;

        scope = program->scope_stack;
        assert(scope);

        parent_scope = scope->parent_scope;
        while (parent_scope && ! scope->is_function) {
            scope        = parent_scope;
            parent_scope = scope->parent_scope;
        }

        assert((scope->parent_scope && scope->is_function) ||
               ( ! scope->parent_scope && ! scope->is_function));

        assert(scope->has_frame || ! scope->is_function);
    }

    return scope;
}

KOS_VAR *kos_find_var(KOS_RED_BLACK_NODE *rb_root,
                      const KOS_TOKEN    *token)
{
    return (KOS_VAR *)kos_red_black_find(rb_root, (void *)token, var_compare_item);
}

static int lookup_local_var(KOS_COMP_UNIT *program,
                            KOS_AST_NODE  *node,
                            KOS_VAR      **out_var)
{
    int        error = KOS_ERROR_INTERNAL;
    KOS_SCOPE *scope = program->scope_stack;
    KOS_SCOPE *parent_scope;

    assert(scope);
    parent_scope = scope->parent_scope;

    /* Stop at function and global scope.
     * Function scope contains arguments, not variables.
     * Function and global scopes are handled by lookup_and_mark_var(). */

    for ( ; parent_scope && ! scope->is_function; parent_scope = scope->parent_scope, scope = parent_scope) {

        KOS_VAR *var = kos_find_var(scope->vars, &node->token);

        if (var && var->is_active) {
            assert(var->scope == scope);
            assert( ! node->is_scope);
            assert( ! node->is_var);
            node->u.var        = var;
            node->is_var       = 1;
            node->is_local_var = 1;
            *out_var           = var;
            error              = KOS_SUCCESS;
            break;
        }
    }

    return error;
}

KOS_SCOPE_REF *kos_find_scope_ref(KOS_FRAME *frame,
                                  KOS_SCOPE *closure)
{
    return (KOS_SCOPE_REF *)kos_red_black_find(frame->closures,
                                               closure,
                                               scope_ref_compare_item);
}

static int add_scope_ref(KOS_COMP_UNIT *program,
                         int            var_type,
                         KOS_SCOPE     *inner_scope,
                         KOS_SCOPE     *outer_closure)
{
    int error = KOS_SUCCESS;

    KOS_SCOPE_REF *ref;

    assert(inner_scope->has_frame);

    ref = kos_find_scope_ref((KOS_FRAME *)inner_scope, outer_closure);

    if ( ! ref) {

        ref = (KOS_SCOPE_REF *)kos_mempool_alloc(&program->allocator, sizeof(KOS_SCOPE_REF));

        if (ref) {

            ref->closure         = outer_closure;
            ref->vars_reg        = 0;
            ref->args_reg        = 0;
            ref->exported_locals = 0;
            ref->exported_args   = 0;

            kos_red_black_insert(&((KOS_FRAME *)inner_scope)->closures,
                                 (KOS_RED_BLACK_NODE *)ref,
                                 scope_ref_compare_node);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    return error;
}

static int lookup_and_mark_var(KOS_COMP_UNIT *program,
                               KOS_AST_NODE  *node,
                               KOS_VAR      **out_var)
{
    int        error;
    KOS_VAR   *var             = 0;
    KOS_SCOPE *scope           = kos_get_frame_scope(program);
    KOS_SCOPE *local_fun_scope = scope;

    assert(scope);

    /* Browse outer scopes (closures, global) to find the variable */
    for ( ; scope; scope = scope->parent_scope) {
        var = kos_find_var(scope->vars, &node->token);
        if (var && var->is_active)
            break;
        var = 0;
    }

    /* Mark variable as independent */
    if (var) {
        if (var->type & VAR_LOCALS_AND_ARGS) {

            /* Don't mark own args */
            if (scope != local_fun_scope) {

                KOS_SCOPE *closure = scope;
                KOS_SCOPE *inner;

                if (var->type & VAR_LOCAL)
                    var->type = VAR_INDEPENDENT_LOCAL;
                else {
                    assert(var->type & VAR_ARGUMENT);
                    var->type = VAR_INDEPENDENT_ARGUMENT;
                }

                /* Find function owning the variable's scope */
                while (closure->parent_scope && ! closure->is_function)
                    closure = closure->parent_scope;

                /* Reference the function in all inner scopes which use it */
                for (inner = program->scope_stack; inner != closure; inner = inner->parent_scope)
                    if (inner->is_function)
                        TRY(add_scope_ref(program, var->type, inner, closure));
            }
        }

        /* Mark own args, globals and modules as local */
        if (scope == local_fun_scope || var->type == VAR_GLOBAL || var->type == VAR_MODULE)
            node->is_local_var = 1;

        assert(var->scope == scope);
        assert( ! node->is_scope);
        assert( ! node->is_var);
        node->is_var = 1;
        node->u.var  = var;
        *out_var     = var;
        error        = KOS_SUCCESS;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_undefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

cleanup:
    return error;
}

enum DEFINE_VAR_CONST {
    VARIABLE,
    CONSTANT
};

enum DEFINE_VAR_GLOBAL {
    LOCAL,
    GLOBAL
};

static int define_var(KOS_COMP_UNIT         *program,
                      enum DEFINE_VAR_CONST  is_const,
                      enum DEFINE_VAR_GLOBAL global,
                      KOS_AST_NODE          *node,
                      KOS_VAR              **out_var)
{
    int      error  = KOS_SUCCESS;
    KOS_VAR *var;

    assert(node->type == NT_IDENTIFIER);
    assert(program->scope_stack);

    if (node->children) {
        assert(node->children->type == NT_EXPORT);
        assert( ! node->children->next);
        assert( ! program->scope_stack->parent_scope);
        global = GLOBAL;
    }

    if (program->is_interactive && ! program->scope_stack->parent_scope)
        global = GLOBAL;

    if (kos_find_var(program->scope_stack->vars, &node->token)) {
        program->error_token = &node->token;
        program->error_str   = str_err_redefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

    var = alloc_var(program, is_const, node);
    if (!var)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    assert(var->scope == program->scope_stack);
    assert( ! node->is_scope);
    assert( ! node->is_var);
    node->u.var        = var;
    node->is_var       = 1;
    node->is_local_var = 1;
    *out_var           = var;

    if (global) {
        var->type        = VAR_GLOBAL;
        var->array_idx   = program->num_globals++;
        var->next        = program->globals;
        program->globals = var;
    }
    else {
        KOS_SCOPE *scope = kos_get_frame_scope(program);

        var->next            = scope->fun_vars_list;
        scope->fun_vars_list = var;
    }

cleanup:
    return error;
}

static int visit_child_nodes(KOS_COMP_UNIT *program,
                             KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node && ! error; node = node->next)
        error = visit_node(program, node);

    return error;
}

typedef struct KOS_IMPORT_INFO_V_S {
    KOS_COMP_UNIT      *program;
    const KOS_AST_NODE *node;
} KOS_IMPORT_INFO_V;

static int import_global(const char *global_name,
                         uint16_t    global_length,
                         int         module_idx,
                         int         global_idx,
                         void       *cookie)
{
    int                 error  = KOS_SUCCESS;
    KOS_IMPORT_INFO_V  *info   = (KOS_IMPORT_INFO_V *)cookie;
    KOS_AST_NODE *const g_node = (KOS_AST_NODE *)
        kos_mempool_alloc(&info->program->allocator, sizeof(KOS_AST_NODE) + global_length);

    if (g_node) {

        KOS_TOKEN *token = &g_node->token;
        KOS_VAR   *var;

        memset(g_node, 0, sizeof(*g_node));

        token->begin   = (char *)g_node + sizeof(KOS_AST_NODE);
        token->length  = global_length;
        token->file_id = info->node->token.file_id;
        token->column  = info->node->token.column;
        token->line    = info->node->token.line;
        token->type    = TT_IDENTIFIER;

        memcpy((void *)token->begin, global_name, global_length);

        g_node->type = NT_IDENTIFIER;

        error = define_var(info->program, CONSTANT, GLOBAL, g_node, &var);
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int import(KOS_COMP_UNIT *program,
                  KOS_AST_NODE  *node)
{
    int error;
    int module_idx;

    assert(program->scope_stack);
    assert( ! program->scope_stack->parent_scope);

    node = node->children;
    assert(node);

    TRY(kos_comp_import_module(program->ctx,
                               node->token.begin,
                               node->token.length,
                               &module_idx));

    if ( ! node->next) {

        KOS_VAR *var = kos_find_var(program->scope_stack->vars, &node->token);

        /* Importing the same module multiple times is allowed. */
        if (var) {
            if (var->type != VAR_MODULE) {
                program->error_token = &node->token;
                program->error_str   = str_err_module_global_conflict;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
        }
        else {
            var = alloc_var(program, 1, node);
            if (!var)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            var->type        = VAR_MODULE;
            var->array_idx   = module_idx;
            var->next        = program->modules;
            program->modules = var;
        }
        assert( ! node->is_scope);
        assert( ! node->is_var);
        node->u.var  = var;
        node->is_var = 1;
    }

    node = node->next;

    if (node) {
        if (node->token.op == OT_MUL) {
            KOS_IMPORT_INFO_V info;

            info.program = program;
            info.node    = node;

            error = kos_comp_walk_globals(program->ctx,
                                          module_idx,
                                          import_global,
                                          &info);
        }
        else {
            for ( ; node; node = node->next) {

                KOS_VAR *var;

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                error = define_var(program, CONSTANT, GLOBAL, node, &var);
            }
        }
    }

cleanup:
    return error;
}

static int process_scope(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node)
{
    int error;

    TRY(push_scope(program, program->scope_stack ? 0 : 1, node));

    TRY(visit_child_nodes(program, node));

    pop_scope(program);

cleanup:
    return error;
}

static int yield(KOS_COMP_UNIT *program,
                 KOS_AST_NODE  *node)
{
    int        error;
    KOS_SCOPE *scope = kos_get_frame_scope(program);

    if (scope->is_function) {
        if ( ! ((KOS_FRAME *)scope)->yield_token)
            ((KOS_FRAME *)scope)->yield_token = &node->token;
        error = KOS_SUCCESS;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_unexpected_yield;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

    if (!error)
        error = visit_child_nodes(program, node);

    return error;
}

static int var_node(KOS_COMP_UNIT *program,
                    KOS_AST_NODE  *node)
{
    int                         error    = KOS_SUCCESS;
    const enum DEFINE_VAR_CONST is_const = node->type == NT_CONST ? CONSTANT : VARIABLE;
    KOS_VAR                    *var;

    for (node = node->children; node; node = node->next) {
        TRY(define_var(program, is_const, LOCAL, node, &var));
        var->is_active = VAR_INACTIVE;
    }

cleanup:
    return error;
}

static int left_hand_side(KOS_COMP_UNIT *program,
                          KOS_AST_NODE  *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_VAR *var = 0;

            if (lookup_local_var(program, node, &var) != KOS_SUCCESS)
                TRY(lookup_and_mark_var(program, node, &var));

            if (var->is_const) {
                program->error_token = &node->token;
                program->error_str   = str_err_const_assignment;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
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

static int identifier(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node)
{
    int      error = KOS_SUCCESS;
    KOS_VAR *var;

    if (lookup_local_var(program, node, &var) != KOS_SUCCESS)
        TRY(lookup_and_mark_var(program, node, &var));

cleanup:
    return error;
}

static int this_literal(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    int        error;
    KOS_SCOPE *scope = kos_get_frame_scope(program);

    if (scope->is_function) {
        scope->uses_this = 1;
        error            = KOS_SUCCESS;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_unexpected_global_this;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

    return error;
}

static void super_ctor_literal(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = kos_get_frame_scope(program);

    assert(scope && scope->is_function);

    ((KOS_FRAME *)scope)->uses_base_ctor = 1;
}

static void super_proto_literal(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = kos_get_frame_scope(program);

    assert(scope && scope->is_function);

    ((KOS_FRAME *)scope)->uses_base_proto = 1;
}

static int parameter_defaults(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node,
                              const KOS_AST_NODE *name_node)
{
    int error = KOS_SUCCESS;

    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    for ( ; node && node->type != NT_ELLIPSIS; node = node->next) {

        if (node->type == NT_ASSIGNMENT) {

            KOS_AST_NODE *def_node = node->children;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            TRY(visit_node(program, def_node));
        }
    }

cleanup:
    return error;
}

static int function_literal(KOS_COMP_UNIT *program,
                            KOS_AST_NODE  *node,
                            KOS_VAR       *fun_var)
{
    int                 error;
    int                 i;
    int                 ellipsis = 0;
    KOS_AST_NODE       *arg_node;
    const KOS_AST_NODE *name_node;

    TRY(push_function(program, node));

    name_node = node->children;
    assert(name_node);
    assert(name_node->type == NT_NAME || name_node->type == NT_NAME_CONST);

    node = name_node->next;
    assert(node);
    assert(node->type == NT_PARAMETERS);

    for (i = 0, arg_node = node->children; arg_node; i++, arg_node = arg_node->next) {

        KOS_VAR      *var;
        KOS_AST_NODE *ident_node = arg_node;

        assert(arg_node->type == NT_IDENTIFIER ||
               arg_node->type == NT_ASSIGNMENT ||
               (arg_node->type == NT_ELLIPSIS && ! arg_node->next));

        if (arg_node->type == NT_ASSIGNMENT) {
            ident_node = arg_node->children;
            assert(ident_node);
            assert(ident_node->type == NT_IDENTIFIER);
        }
        else if (arg_node->type == NT_ELLIPSIS) {
            ellipsis   = 1;
            arg_node   = arg_node->children;
            ident_node = arg_node;
            assert( ! arg_node->next);
            assert(arg_node->type == NT_IDENTIFIER);
        }

        TRY(define_var(program, VARIABLE, LOCAL, ident_node, &var));
        assert( ! ident_node->is_scope);
        assert(ident_node->is_var);
        assert(ident_node->u.var == var);

        if (ellipsis)
            program->scope_stack->ellipsis = var;
        else {
            var->type      = VAR_ARGUMENT;
            var->array_idx = i;

            if (arg_node->type == NT_ASSIGNMENT)
                var->has_defaults = 1;
        }
    }

    arg_node = node;

    node = node->next;
    assert(node);
    assert(node->type == NT_LANDMARK);
    node = node->next;
    assert(node);
    assert(node->type == NT_SCOPE);

    kos_activate_self_ref_func(program, fun_var);

    TRY(visit_node(program, node));

    kos_deactivate_self_ref_func(program, fun_var);

    node = node->next;
    assert(node->type == NT_LANDMARK);
    assert(!node->next);

    pop_scope(program);

    TRY(parameter_defaults(program, arg_node, name_node));

cleanup:
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

static int catch_clause(KOS_COMP_UNIT *program,
                        KOS_AST_NODE  *node)
{
    int      error;
    KOS_VAR *var;

    node = node->children;

    assert(node);
    assert(node->type == NT_CONST || node->type == NT_VAR);

    TRY(visit_node(program, node));

    assert(node->children);
    assert(node->children->type == NT_IDENTIFIER);
    assert( ! node->children->next);

    assert( ! node->children->is_scope);
    assert(node->children->is_var);
    var = node->children->u.var;
    assert(var);
    assert(var == kos_find_var(program->scope_stack->vars, &node->children->token));

    assert(var->is_active == VAR_INACTIVE);
    var->is_active = VAR_ACTIVE;

    for (node = node->next; node; node = node->next)
        TRY(visit_node(program, node));

    var->is_active = VAR_INACTIVE;

cleanup:
    return error;
}

static int assert_stmt(KOS_COMP_UNIT *program,
                       KOS_AST_NODE  *node)
{
    node = node->children;
    assert(node);
    assert(node->next);
    assert(node->next->type == NT_LANDMARK);
    assert( ! node->next->next);

    return visit_node(program, node);
}

int kos_is_self_ref_func(const KOS_AST_NODE *node)
{
    if (node->type != NT_CONST)
        return 0;

    assert(node->children);
    assert(node->next);

    if (node->next->type != NT_FUNCTION_LITERAL &&
        node->next->type != NT_CLASS_LITERAL)
        return 0;

    assert(node->children->type == NT_IDENTIFIER ||
           node->children->type == NT_VOID_LITERAL);

    /* Multi-assignment */
    if (node->children->next) {
        assert(node->children->next->type == NT_IDENTIFIER ||
               node->children->next->type == NT_VOID_LITERAL);
        return 0;
    }

    return 1;
}

static int assignment(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *input_node)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *node  = input_node;

    assert(node->type == NT_ASSIGNMENT);

    node = node->children;

    assert(node);
    assert(node->next);

    if (kos_is_self_ref_func(node)) {

        KOS_VAR *fun_var;

        TRY(visit_node(program, node));

        assert( ! node->children->is_scope);
        assert(node->children->is_var);
        fun_var = node->children->u.var;
        assert(fun_var);
        assert( ! fun_var->is_active);

        node = node->next;
        assert( ! node->next);

        if (node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, node, fun_var));
        else {
            assert(node->type == NT_CLASS_LITERAL);
            TRY(class_literal(program, node, fun_var));
        }
    }
    else
        TRY(visit_child_nodes(program, input_node));

    kos_activate_new_vars(program, input_node->children);

cleanup:
    return error;
}

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {
        case NT_IMPORT:
            /* fall through */
        default:
            assert(node->type == NT_IMPORT);
            error = import(program, node);
            break;
        case NT_YIELD:
            error = yield(program, node);
            break;
        case NT_LEFT_HAND_SIDE:
            error = left_hand_side(program, node);
            break;
        case NT_IDENTIFIER:
            error = identifier(program, node);
            break;
        case NT_THIS_LITERAL:
            error = this_literal(program, node);
            break;
        case NT_SUPER_CTOR_LITERAL:
            super_ctor_literal(program);
            error = KOS_SUCCESS;
            break;
        case NT_SUPER_PROTO_LITERAL:
            super_proto_literal(program);
            error = KOS_SUCCESS;
            break;
        case NT_FUNCTION_LITERAL:
            error = function_literal(program, node, 0);
            break;
        case NT_CLASS_LITERAL:
            error = class_literal(program, node, 0);
            break;
        case NT_ASSIGNMENT:
            error = assignment(program, node);
            break;
        case NT_CATCH:
            error = catch_clause(program, node);
            break;
        case NT_ASSERT:
            error = assert_stmt(program, node);
            break;

        case NT_VAR:
            /* fall through */
        case NT_CONST:
            error = var_node(program, node);
            break;

        case NT_MULTI_ASSIGNMENT:
            /* fall through */
        case NT_IN:
            error = visit_child_nodes(program, node);
            if ( ! error)
                kos_activate_new_vars(program, node->children);
            break;

        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            /* fall through */
        case NT_FOR_IN: /* Has its own scope for the iterator variable */
            /* fall through */
        case NT_SCOPE:
            /* fall through */
        case NT_CONTINUE:    /* Create fake scope just for catch refs */
            /* fall through */
        case NT_BREAK:       /* Create fake scope just for catch refs */
            /* fall through */
        case NT_FALLTHROUGH: /* Create fake scope just for catch refs */
            error = process_scope(program, node);
            break;

        case NT_EMPTY:
            /* fall through */
        case NT_NUMERIC_LITERAL:
            /* fall through */
        case NT_STRING_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            /* fall through */
        case NT_LINE_LITERAL:
            assert( ! node->children);
            error = KOS_SUCCESS;
            break;

        case NT_IF:
            /* fall through */
        case NT_RETURN:
            /* fall through */
        case NT_THROW:
            /* fall through */
        case NT_REPEAT:
            /* fall through */
        case NT_FOR:
            /* fall through */
        case NT_REFINEMENT:
            /* fall through */
        case NT_SLICE:
            /* fall through */
        case NT_INVOCATION:
            /* fall through */
        case NT_OPERATOR:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            /* fall through */
        case NT_PROPERTY:
            /* fall through */
        case NT_EXPAND:
            /* fall through */
        case NT_EXPRESSION_LIST:
            /* fall through */
        case NT_SWITCH:
            /* fall through */
        case NT_CASE:
            /* fall through */
        case NT_DEFAULT:
            /* fall through */
        case NT_ARRAY_LITERAL:
            /* fall through */
        case NT_OBJECT_LITERAL:
            /* fall through */
        case NT_ASYNC:
            error = visit_child_nodes(program, node);
            break;
    }

    return error;
}

void kos_activate_var(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node)
{
    KOS_VAR *var;

    /* Result of optimization */
    if (node->type == NT_VOID_LITERAL)
        return;

    assert(node->type == NT_IDENTIFIER);

    assert( ! node->is_scope);
    assert(node->is_var);
    var = node->u.var;
    assert(var);
    assert(var == kos_find_var(program->scope_stack->vars, &node->token));

    if ( ! var->is_active)
        var->is_active = VAR_ACTIVE;
}

void kos_activate_new_vars(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node)
{
    assert(node);

    if (node->type == NT_VAR || node->type == NT_CONST) {

        node = node->children;

        assert(node);

        for ( ; node; node = node->next)
            kos_activate_var(program, node);
    }
    else {
        assert(node->type == NT_LEFT_HAND_SIDE);
    }
}

void kos_activate_self_ref_func(KOS_COMP_UNIT *program,
                                KOS_VAR       *fun_var)
{
    if (fun_var) {
        assert( ! fun_var->is_active);
        fun_var->is_active = VAR_ACTIVE;
    }
}

void kos_deactivate_self_ref_func(KOS_COMP_UNIT *program,
                                  KOS_VAR       *fun_var)
{
    if (fun_var) {
        assert(fun_var->is_active == VAR_ACTIVE);
        fun_var->is_active = VAR_INACTIVE;
    }
}

static int deactivate(KOS_RED_BLACK_NODE *node,
                      void               *cookie)
{
    KOS_VAR *var = (KOS_VAR *)node;

    if (var->is_active == VAR_ACTIVE)
        var->is_active = VAR_INACTIVE;

    return 0;
}

void kos_deactivate_vars(KOS_SCOPE *scope)
{
    kos_red_black_walk(scope->vars, deactivate, 0);
}

int kos_compiler_process_vars(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *ast)
{
    PROF_ZONE(COMPILER)

    assert(ast->type == NT_SCOPE);

    return visit_node(program, ast);
}

static int predefine_global(KOS_COMP_UNIT      *program,
                            const char         *name,
                            uint16_t            name_len,
                            int                 idx,
                            int                 is_const,
                            enum KOS_VAR_TYPE_E type)
{
    int error = KOS_SUCCESS;

    KOS_PRE_GLOBAL *global = (KOS_PRE_GLOBAL *)
        kos_mempool_alloc(&program->allocator, sizeof(KOS_PRE_GLOBAL) + name_len);

    if (global) {
        memset(&global->node, 0, sizeof(global->node));
        memcpy(global->name_buf, name, name_len + 1);

        global->next               = program->pre_globals;
        global->type               = type;
        global->idx                = idx;
        global->is_const           = is_const;
        global->node.type          = NT_IDENTIFIER;
        global->node.token.begin   = global->name_buf;
        global->node.token.length  = name_len;
        global->node.token.file_id = program->file_id;
        global->node.token.type    = TT_IDENTIFIER;
        program->pre_globals       = global;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

int kos_compiler_predefine_global(KOS_COMP_UNIT *program,
                                  const char    *name,
                                  uint16_t       name_len,
                                  int            idx,
                                  int            is_const)
{
    return predefine_global(program, name, name_len, idx, is_const, VAR_GLOBAL);
}

int kos_compiler_predefine_module(KOS_COMP_UNIT *program,
                                  const char    *name,
                                  uint16_t       name_len,
                                  int            idx)
{
    return predefine_global(program, name, name_len, idx, 1, VAR_MODULE);
}

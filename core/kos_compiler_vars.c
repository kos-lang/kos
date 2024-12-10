/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "kos_compiler.h"
#include "kos_ast.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>

static const char str_err_const_assignment[]        = "const variable is not assignable";
static const char str_err_module_global_conflict[]  = "unable to import module, a global variable with this name already exists";
static const char str_err_no_such_module_variable[] = "no such global in module";
static const char str_err_redefined_var[]           = "redefined variable";
static const char str_err_too_many_modules[]        = "too many modules imported";
static const char str_err_undefined_var[]           = "undeclared identifier";
static const char str_err_unexpected_global_this[]  = "'this' not allowed in global scope";
static const char str_err_unexpected_yield[]        = "'yield' not allowed in global scope";

static int visit_node(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *node);

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
                          unsigned            type,
                          unsigned            is_const,
                          const KOS_AST_NODE *node)
{
    KOS_VAR *var = (KOS_VAR *)
        KOS_mempool_alloc(&program->allocator, sizeof(KOS_VAR));

    if (var) {
        memset(var, 0, sizeof(*var));

        var->scope     = program->scope_stack;
        var->token     = &node->token;
        var->type      = type;
        var->is_const  = is_const;
        var->num_reads = -1;

        var->scope_next = program->scope_stack->vars;
        program->scope_stack->vars = var;
    }

    return var;
}

static int enable_var(KOS_COMP_UNIT *program, KOS_VAR *var)
{
    return kos_add_to_hash_table(&program->variables, var);
}

static void disable_var(KOS_COMP_UNIT *program, KOS_VAR *var)
{
    kos_remove_from_hash_table(&program->variables, var);
}

static int activate_var(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    KOS_VAR *var;
    int      error = KOS_SUCCESS;

    /* Result of optimization */
    if (node->type == NT_PLACEHOLDER)
        return KOS_SUCCESS;

    assert(node->type == NT_IDENTIFIER);

    assert( ! node->is_scope);
    assert(node->is_var);
    var = node->u.var;
    assert(var);

    error = enable_var(program, var);

    return error;
}

static int activate_new_vars(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    assert(node);

    if (node->type == NT_VAR || node->type == NT_CONST) {

        node = node->children;

        assert(node);

        for ( ; node && ! error; node = node->next)
            error = activate_var(program, node);
    }
    else {
        assert(node->type == NT_LEFT_HAND_SIDE);
    }

    return error;
}

static int init_global_scope(KOS_COMP_UNIT *program)
{
    int error = KOS_SUCCESS;

    /* Register built-in module globals */
    KOS_PRE_GLOBAL *global = program->pre_globals;

    for ( ; global && ! error; global = global->next) {

        KOS_VAR *var = alloc_var(program, global->type, global->is_const, &global->node);

        if (var) {
            var->array_idx   = global->idx;
            var->next        = program->globals;
            program->globals = var;
            ++program->num_globals;

            error = enable_var(program, var);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    return error;
}

static int push_scope(KOS_COMP_UNIT *program,
                      int            alloc_frame,
                      KOS_AST_NODE  *node)
{
    const size_t size  = alloc_frame ? sizeof(KOS_FRAME) : sizeof(KOS_SCOPE);
    int          error = KOS_SUCCESS;
    size_t       i;

    KOS_SCOPE *const scope = (KOS_SCOPE *)KOS_mempool_alloc(&program->allocator, size);

    if ( ! scope)
        return KOS_ERROR_OUT_OF_MEMORY;

    memset(scope, 0, size);

    for (i = 0; i < sizeof(scope->catch_ref.catch_entry) / sizeof(uint32_t); i++)
        scope->catch_ref.catch_entry[i] = KOS_NO_JUMP;

    assert(program->scope_stack || alloc_frame);

    if (alloc_frame)
        scope->has_frame = 1;

    node->is_scope       = 1;
    node->u.scope        = scope;
    scope->scope_node    = node;
    scope->parent_scope  = program->scope_stack;
    program->scope_stack = scope;

    if ( ! scope->parent_scope)
        error = init_global_scope(program);

    if (alloc_frame) {
        KOS_FRAME *const frame = (KOS_FRAME *)scope;

        frame->parent_frame   = program->cur_frame;
        frame->num_binds_prev = 1; /* Updated during optimization */
        frame->num_def_used   = 1; /* Updated during optimization */
        program->cur_frame    = frame;
        scope->owning_frame   = frame;
    }
    else
        scope->owning_frame = program->cur_frame;

    return error;
}

static void pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = program->scope_stack;
    KOS_VAR         *var;

    program->scope_stack = scope->parent_scope;

    if (scope->has_frame)
        program->cur_frame = ((KOS_FRAME *)scope)->parent_frame;

    for (var = scope->vars; var; var = var->scope_next)
        disable_var(program, var);
}

static int push_function(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *node)
{
    int error = push_scope(program, 1, node);

    if ( ! error)
        program->scope_stack->is_function = 1;

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

        ref = (KOS_SCOPE_REF *)KOS_mempool_alloc(&program->allocator, sizeof(KOS_SCOPE_REF));

        if (ref) {

            ref->closure         = outer_closure;
            ref->vars_reg        = KOS_NULL;
            ref->args_reg        = KOS_NULL;
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
    KOS_VAR *const var   = kos_lookup_var(&program->variables, &node->token);
    int            error = KOS_SUCCESS;

    if (var) {
        KOS_SCOPE *const local_fun_scope  = &program->cur_frame->scope;
        KOS_SCOPE *const var_scope        = var->scope;
        KOS_SCOPE *const owning_fun_scope = &var_scope->owning_frame->scope;

        assert(local_fun_scope->has_frame);
        assert(local_fun_scope->is_function || ! local_fun_scope->parent_scope);
        assert(owning_fun_scope->has_frame);
        assert(owning_fun_scope->is_function || ! owning_fun_scope->parent_scope);
        assert( ! node->is_scope);
        assert( ! node->is_var);
        node->is_var = 1;
        node->u.var  = var;

        /* Local variable or local function argument */
        if (owning_fun_scope == local_fun_scope) {
            node->is_local_var = 1;
        }
        /* Mark a non-independent variable as "local" */
        else if (var->type & (VAR_GLOBAL | VAR_MODULE | VAR_IMPORTED)) {
            node->is_local_var = 1;
        }
        /* Mark variable as independent */
        else if (var->type & VAR_LOCALS_AND_ARGS) {

            KOS_SCOPE *inner;

            assert((var->type & VAR_LOCAL) || (var->type & VAR_ARGUMENT));
            var->type |= VAR_INDEPENDENT;

            /* Reference the function in all inner scopes which use it */
            for (inner = program->scope_stack; inner != owning_fun_scope; inner = inner->parent_scope)
                if (inner->is_function)
                    TRY(add_scope_ref(program, var->type, inner, owning_fun_scope));
        }

        if (out_var)
            *out_var = var;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_undefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

cleanup:
    return error;
}

static int find_existing_local_var(KOS_COMP_UNIT   *program,
                                   const KOS_TOKEN *token)
{
    const KOS_VAR *const var = kos_lookup_var(&program->variables, token);

    if (var) {

        KOS_SCOPE *const var_scope = var->scope;
        KOS_SCOPE       *scope     = program->scope_stack;

        do {
            /* Variable is re-declared in current scope */
            if (scope == var_scope)
                return 1;

            /* Special case: a scope in a generated try section for a defer statement.
             * In this case, look up the variable in the parent scope,
             * because in the source code this is the same scope.
             */
            if (scope->scope_node->token.keyword != KW_DEFER)
                break;

            scope = scope->parent_scope;
        } while (scope && ! scope->is_function);
    }

    return 0;
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
                      KOS_AST_NODE          *node,
                      KOS_VAR              **out_var)
{
    KOS_VAR               *var;
    enum DEFINE_VAR_GLOBAL global = LOCAL;
    int                    error  = KOS_SUCCESS;

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

    if (find_existing_local_var(program, &node->token)) {
        program->error_token = &node->token;
        program->error_str   = str_err_redefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
        goto cleanup;
    }

    var = alloc_var(program, global ? VAR_GLOBAL : VAR_LOCAL, is_const, node);
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
        var->array_idx   = program->num_globals++;
        var->next        = program->globals;
        program->globals = var;
    }
    else {
        KOS_SCOPE *scope = &program->cur_frame->scope;

        if ( ! program->scope_stack->parent_scope)
            TRY(kos_comp_check_private_global(program->ctx, &node->token));

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

static uint16_t get_module_path_len(const KOS_AST_NODE *node)
{
    uint32_t total_len = 0;

    for ( ; node; node = node->next) {
        assert(node->type == NT_IDENTIFIER);

        /* Include path separators */
        if (total_len)
            ++total_len;

        total_len += node->token.length;
    }

    if (total_len > 0xFFFFU)
        total_len = 0;

    return (uint16_t)total_len;
}

int kos_get_module_path_name(KOS_COMP_UNIT       *program,
                             const KOS_AST_NODE  *node,
                             const char         **module_name,
                             uint16_t            *name_len,
                             const KOS_AST_NODE **mod_name_node)
{
    assert(node->type == NT_ARRAY_LITERAL);

    node = node->children;

    assert(node);
    assert(node->type == NT_IDENTIFIER);

    if (node->next) {

        static char empty[1] = "";
        char *path = empty;
        char *dst;

        *name_len = get_module_path_len(node);

        path = (char *)KOS_mempool_alloc(&program->allocator, *name_len + 1);
        dst  = path;

        if ( ! path)
            return KOS_ERROR_OUT_OF_MEMORY;

        for ( ; node; node = node->next) {
            const uint16_t len = node->token.length;

            if (path != dst)
                *(dst++) = '/';

            memcpy(dst, node->token.begin, len);
            dst += len;

            *mod_name_node = node;
        }

        assert((uint32_t)(dst - path) == *name_len);

        *module_name = path;
        return KOS_SUCCESS;
    }

    assert(node->type == NT_IDENTIFIER);

    *module_name   = node->token.begin;
    *name_len      = node->token.length;
    *mod_name_node = node;

    return KOS_SUCCESS;
}

typedef struct KOS_IMPORT_INFO_V_S {
    KOS_COMP_UNIT *program;
    KOS_AST_NODE  *node;
    KOS_AST_NODE **tail;
} KOS_IMPORT_INFO_V;

static int import_global(const char *global_name,
                         uint16_t    global_length,
                         int         module_idx,
                         int         global_idx,
                         void       *cookie)
{
    KOS_IMPORT_INFO_V *info   = (KOS_IMPORT_INFO_V *)cookie;
    KOS_AST_NODE      *g_node = info->node;
    KOS_VAR           *var    = KOS_NULL;
    int                error  = KOS_SUCCESS;

    if (info->node->token.op == OT_MUL) {

        KOS_TOKEN *token;

        g_node = (KOS_AST_NODE *)
            KOS_mempool_alloc(&info->program->allocator, sizeof(KOS_AST_NODE) + global_length);

        if ( ! g_node)
            return KOS_ERROR_OUT_OF_MEMORY;

        memset(g_node, 0, sizeof(*g_node));

        token = &g_node->token;

        token->begin   = (char *)g_node + sizeof(KOS_AST_NODE);
        token->length  = global_length;
        token->file_id = info->node->token.file_id;
        token->column  = info->node->token.column;
        token->line    = info->node->token.line;
        token->type    = TT_IDENTIFIER;

        memcpy((void *)token->begin, global_name, global_length);

        g_node->type = NT_IDENTIFIER;

        /* Chain the new node for register allocation */
        assert(info->tail);
        *info->tail = g_node;
        info->tail  = &g_node->next;
    }

    error = define_var(info->program, CONSTANT, g_node, &var);

    if ( ! error) {
        if (var->type != VAR_GLOBAL) {
            var->type       = VAR_IMPORTED;
            var->module_idx = module_idx;
            var->array_idx  = global_idx;
        }

        error = enable_var(info->program, var);
    }

    return error;
}

static int import(KOS_COMP_UNIT *program,
                  KOS_AST_NODE  *node)
{
    KOS_AST_NODE *mod_name_node = KOS_NULL;
    const char   *module_name;
    int           error;
    int           module_idx;
    uint16_t      name_len;

    assert(program->scope_stack);
    assert( ! program->scope_stack->parent_scope);

    node = node->children;
    assert(node);

    TRY(kos_get_module_path_name(program, node, &module_name, &name_len, (const KOS_AST_NODE **)&mod_name_node));

    TRY(kos_comp_import_module(program->ctx,
                               module_name,
                               name_len,
                               &module_idx));

    if (module_idx < 0 || module_idx > 0xFFFF) {
        program->error_token = &mod_name_node->token;
        program->error_str   = str_err_too_many_modules;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }

    if ( ! node->next) {

        KOS_VAR *var = kos_lookup_var(&program->variables, &mod_name_node->token);

        /* Importing the same module multiple times is allowed. */
        if (var) {
            if (var->type != VAR_MODULE) {
                program->error_token = &mod_name_node->token;
                program->error_str   = str_err_module_global_conflict;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
        }
        else {
            var = alloc_var(program, VAR_MODULE, 1, mod_name_node);
            if (!var)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            var->array_idx   = module_idx;
            var->next        = program->modules;
            program->modules = var;

            error = enable_var(program, var);
            if (error)
                goto cleanup;
        }
        assert( ! mod_name_node->is_scope);
        assert( ! mod_name_node->is_var);
        mod_name_node->u.var  = var;
        mod_name_node->is_var = 1;
    }

    node = node->next;

    if (node) {
        KOS_IMPORT_INFO_V info;

        info.program = program;
        info.tail    = KOS_NULL;

        if (node->token.op == OT_MUL) {
            info.node = node;
            info.tail = &node->children;

            assert( ! node->children);

            error = kos_comp_walk_globals(program->ctx,
                                          module_idx,
                                          import_global,
                                          &info);
        }
        else {
            for ( ; node; node = node->next) {

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                info.node = node;

                error = kos_comp_resolve_global(program->ctx,
                                                module_idx,
                                                node->token.begin,
                                                node->token.length,
                                                import_global,
                                                &info);
                if (error) {
                    if (error == KOS_ERROR_COMPILE_FAILED || error == KOS_ERROR_OUT_OF_MEMORY)
                        goto cleanup;
                    program->error_token = &node->token;
                    program->error_str   = str_err_no_such_module_variable;
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }
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
    KOS_SCOPE *scope = &program->cur_frame->scope;

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
        if (node->type != NT_PLACEHOLDER) {
            TRY(define_var(program, is_const, node, &var));
        }
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

            KOS_VAR *var = KOS_NULL;

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
    return lookup_and_mark_var(program, node, KOS_NULL);
}

static int this_literal(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    int        error;
    KOS_SCOPE *scope = &program->cur_frame->scope;

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
    KOS_FRAME *const frame = program->cur_frame;

    assert(frame && frame->scope.is_function);

    frame->uses_base_ctor = 1;
}

static void super_proto_literal(KOS_COMP_UNIT *program)
{
    KOS_FRAME *const frame = program->cur_frame;

    assert(frame && frame->scope.is_function);

    frame->uses_base_proto = 1;
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

static int activate_self_ref_func(KOS_COMP_UNIT *program,
                                  KOS_VAR       *fun_var)
{
    if ( ! fun_var)
        return 0;

    return enable_var(program, fun_var);
}

static void deactivate_self_ref_func(KOS_COMP_UNIT *program,
                                     KOS_VAR       *fun_var)
{
    if (fun_var)
        disable_var(program, fun_var);
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
               arg_node->type == NT_PLACEHOLDER ||
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

        if (ident_node->type != NT_PLACEHOLDER) {
            TRY(define_var(program, VARIABLE, ident_node, &var));
            assert( ! ident_node->is_scope);
            assert(ident_node->is_var);
            assert(ident_node->u.var == var);

            TRY(enable_var(program, var));

            if (ellipsis)
                program->scope_stack->ellipsis = var;
            else {
                var->type      = VAR_ARGUMENT;
                var->array_idx = i;

                if (arg_node->type == NT_ASSIGNMENT)
                    var->has_defaults = 1;
            }
        }
    }

    arg_node = node;

    node = node->next;
    assert(node);
    assert(node->type == NT_LANDMARK);
    node = node->next;
    assert(node);
    assert(node->type == NT_SCOPE);

    TRY(activate_self_ref_func(program, fun_var));

    TRY(visit_node(program, node));

    deactivate_self_ref_func(program, fun_var);

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

    /* Note: catch variable is disabled by the scope */
    TRY(enable_var(program, var));

    for (node = node->next; node; node = node->next)
        TRY(visit_node(program, node));

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
           node->children->type == NT_PLACEHOLDER);

    /* Multi-assignment */
    if (node->children->next) {
        assert(node->children->next->type == NT_IDENTIFIER ||
               node->children->next->type == NT_PLACEHOLDER);
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

    error = activate_new_vars(program, input_node->children);

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
            error = function_literal(program, node, KOS_NULL);
            break;
        case NT_CLASS_LITERAL:
            error = class_literal(program, node, KOS_NULL);
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
                error = activate_new_vars(program, node->children);
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
        case NT_PLACEHOLDER:
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
        case NT_WHILE:
            /* fall through */
        case NT_REFINEMENT:
            /* fall through */
        case NT_OPT_REFINEMENT:
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
        case NT_NAMED_ARGUMENTS:
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
        KOS_mempool_alloc(&program->allocator, sizeof(KOS_PRE_GLOBAL) + name_len);

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
                                  int            idx)
{
    return predefine_global(program, name, name_len, idx, program->is_interactive ? 0 : 1, VAR_GLOBAL);
}

int kos_compiler_predefine_module(KOS_COMP_UNIT *program,
                                  const char    *name,
                                  uint16_t       name_len,
                                  int            idx)
{
    return predefine_global(program, name, name_len, idx, 1, VAR_MODULE);
}

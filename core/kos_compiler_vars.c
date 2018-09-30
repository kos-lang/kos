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
#include "kos_ast.h"
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

static int _visit_node(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node);

int _KOS_scope_compare_node(struct _KOS_RED_BLACK_NODE *a,
                            struct _KOS_RED_BLACK_NODE *b)
{
    const struct _KOS_SCOPE *scope_a = (const struct _KOS_SCOPE *)a;
    const struct _KOS_SCOPE *scope_b = (const struct _KOS_SCOPE *)b;

    return (int)((intptr_t)scope_a->scope_node - (intptr_t)scope_b->scope_node);
}

static int _compare_tokens(const struct _KOS_TOKEN *token_a,
                           const struct _KOS_TOKEN *token_b)
{
    const unsigned len_a = token_a->length;
    const unsigned len_b = token_b->length;

    const unsigned min_len = (len_a <= len_b) ? len_a : len_b;

    int result = memcmp(token_a->begin, token_b->begin, min_len);

    if (result == 0)
        result = (int)len_a - (int)len_b;

    return result;
}

static int _var_compare_node(struct _KOS_RED_BLACK_NODE *a,
                             struct _KOS_RED_BLACK_NODE *b)
{
    const struct _KOS_TOKEN *token_a = ((const struct _KOS_VAR *)a)->token;
    const struct _KOS_TOKEN *token_b = ((const struct _KOS_VAR *)b)->token;

    return _compare_tokens(token_a, token_b);
}

static int _var_compare_item(void                       *what,
                             struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_TOKEN *token_a = (const struct _KOS_TOKEN *)what;
    const struct _KOS_TOKEN *token_b = ((const struct _KOS_VAR *)node)->token;

    return _compare_tokens(token_a, token_b);
}

static int _scope_ref_compare_item(void                       *what,
                                   struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_SCOPE     *closure = (const struct _KOS_SCOPE *)    what;
    const struct _KOS_SCOPE_REF *ref     = (const struct _KOS_SCOPE_REF *)node;

    return (int)((intptr_t)closure - (intptr_t)ref->closure);
}

static int _scope_ref_compare_node(struct _KOS_RED_BLACK_NODE *a,
                                   struct _KOS_RED_BLACK_NODE *b)
{
    const struct _KOS_SCOPE_REF *ref_a = (const struct _KOS_SCOPE_REF *)a;
    const struct _KOS_SCOPE_REF *ref_b = (const struct _KOS_SCOPE_REF *)b;

    return (int)((intptr_t)ref_a->closure - (intptr_t)ref_b->closure);
}

static struct _KOS_VAR *_alloc_var(struct _KOS_COMP_UNIT      *program,
                                   unsigned                    is_const,
                                   const struct _KOS_AST_NODE *node)
{
    struct _KOS_VAR *var = (struct _KOS_VAR *)
        _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_VAR));

    if (var) {
        memset(var, 0, sizeof(*var));

        var->token        = &node->token;
        var->type         = VAR_LOCAL;
        var->is_const     = is_const;
        var->is_active    = VAR_ALWAYS_ACTIVE;
        var->has_defaults = 0;

        _KOS_red_black_insert(&program->scope_stack->vars,
                              (struct _KOS_RED_BLACK_NODE *)var,
                              _var_compare_node);
    }

    return var;
}

static int _init_global_scope(struct _KOS_COMP_UNIT *program)
{
    int error = KOS_SUCCESS;

    /* Register built-in module globals */
    struct _KOS_PRE_GLOBAL *global = program->pre_globals;

    for ( ; global; global = global->next) {

        struct _KOS_VAR *var = _alloc_var(program, global->is_const, &global->node);

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

static int _push_scope(struct _KOS_COMP_UNIT      *program,
                       int                         alloc_frame,
                       const struct _KOS_AST_NODE *node)
{
    int          error = KOS_SUCCESS;
    const size_t size  = alloc_frame ? sizeof(struct _KOS_FRAME) : sizeof(struct _KOS_SCOPE);

    struct _KOS_SCOPE *const scope = (struct _KOS_SCOPE *)
        _KOS_mempool_alloc(&program->allocator, size);
    struct _KOS_FRAME *const frame = alloc_frame ? (struct _KOS_FRAME *)scope : 0;

    if ( ! scope)
        error = KOS_ERROR_OUT_OF_MEMORY;

    else {

        memset(scope, 0, size);

        if (frame)
            scope->has_frame = 1;

        scope->scope_node = node;

        _KOS_red_black_insert(&program->scopes,
                              (struct _KOS_RED_BLACK_NODE *)scope,
                              _KOS_scope_compare_node);

        scope->next          = program->scope_stack;
        program->scope_stack = scope;

        if (!scope->next)
            error = _init_global_scope(program);
    }

    return error;
}

static void _pop_scope(struct _KOS_COMP_UNIT *program)
{
    program->scope_stack = program->scope_stack->next;
}

static int _push_function(struct _KOS_COMP_UNIT      *program,
                          const struct _KOS_AST_NODE *node)
{
    int error = _push_scope(program, 1, node);

    if ( ! error)
        program->scope_stack->is_function = 1;

    return error;
}

struct _KOS_VAR *_KOS_find_var(struct _KOS_RED_BLACK_NODE *rb_root,
                               const struct _KOS_TOKEN    *token)
{
    return (struct _KOS_VAR *)_KOS_red_black_find(rb_root, (void *)token, _var_compare_item);
}

static int _lookup_local_var(struct _KOS_COMP_UNIT   *program,
                             const struct _KOS_TOKEN *token,
                             struct _KOS_VAR        **out_var)
{
    int error = KOS_ERROR_INTERNAL;

    struct _KOS_SCOPE *scope = program->scope_stack;

    assert(scope);

    /* Stop at function and global scope.
     * Function scope contains arguments, not variables.
     * Function and global scopes are handled by _lookup_and_mark_var(). */

    for (scope = program->scope_stack; scope->next && ! scope->is_function; scope = scope->next) {

        struct _KOS_VAR *var = _KOS_find_var(scope->vars, token);

        if (var && var->is_active) {
            *out_var = var;
            error    = KOS_SUCCESS;
            break;
        }

        if (scope->is_function)
            break;
    }

    return error;
}

struct _KOS_SCOPE_REF *_KOS_find_scope_ref(struct _KOS_FRAME *frame,
                                           struct _KOS_SCOPE *closure)
{
    return (struct _KOS_SCOPE_REF *)_KOS_red_black_find(frame->closures,
                                                        closure,
                                                        _scope_ref_compare_item);
}

static int _add_scope_ref(struct _KOS_COMP_UNIT *program,
                          int                    var_type,
                          struct _KOS_SCOPE     *inner_scope,
                          struct _KOS_SCOPE     *outer_closure)
{
    int error = KOS_SUCCESS;

    struct _KOS_SCOPE_REF *ref;

    assert(inner_scope->has_frame);

    ref = _KOS_find_scope_ref((struct _KOS_FRAME *)inner_scope, outer_closure);

    if ( ! ref) {

        ref = (struct _KOS_SCOPE_REF *)
            _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_SCOPE_REF));

        if (ref) {

            ref->closure         = outer_closure;
            ref->vars_reg        = 0;
            ref->args_reg        = 0;
            ref->exported_locals = 0;
            ref->exported_args   = 0;

            _KOS_red_black_insert(&((struct _KOS_FRAME *)inner_scope)->closures,
                                  (struct _KOS_RED_BLACK_NODE *)ref,
                                  _scope_ref_compare_node);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    return error;
}

static int _lookup_and_mark_var(struct _KOS_COMP_UNIT      *program,
                                const struct _KOS_AST_NODE *node,
                                struct _KOS_VAR           **out_var)
{
    int                error;
    struct _KOS_VAR   *var   = 0;
    struct _KOS_SCOPE *scope = program->scope_stack;
    struct _KOS_SCOPE *local_fun_scope;

    assert(scope);

    /* Skip local scopes */
    while (scope->next && ! scope->is_function)
        scope = scope->next;
    local_fun_scope = scope;

    /* Browse outer scopes (closures, global) to find the variable */
    for ( ; scope; scope = scope->next) {
        var = _KOS_find_var(scope->vars, &node->token);
        if (var && var->is_active)
            break;
        var = 0;
    }

    /* Mark variable as independent */
    if (var) {
        if (var->type & VAR_LOCALS_AND_ARGS) {

            /* Ignore own args */
            if (scope != local_fun_scope) {

                struct _KOS_SCOPE *closure = scope;

                if (var->type & VAR_LOCAL)
                    var->type = VAR_INDEPENDENT_LOCAL;
                else {
                    assert(var->type & VAR_ARGUMENT);
                    var->type = VAR_INDEPENDENT_ARGUMENT;
                }

                /* Find function owning the variable's scope */
                while (closure->next && ! closure->is_function)
                    closure = closure->next;

                /* Reference the function in all inner scopes which use it */
                for (scope = program->scope_stack; scope != closure; scope = scope->next)
                    if (scope->is_function)
                        TRY(_add_scope_ref(program, var->type, scope, closure));
            }
        }

        *out_var = var;
        error    = KOS_SUCCESS;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_undefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

_error:
    return error;
}

static int _define_local_var(struct _KOS_COMP_UNIT      *program,
                             unsigned                    is_const,
                             const struct _KOS_AST_NODE *node,
                             struct _KOS_VAR           **out_var)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VAR   *var;
    struct _KOS_SCOPE *scope;
    int                global;

    assert(node->type == NT_IDENTIFIER);
    assert(program->scope_stack);

    global = ! program->scope_stack->next;

    if (_KOS_find_var(program->scope_stack->vars, &node->token)) {
        program->error_token = &node->token;
        program->error_str   = str_err_redefined_var;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

    var = _alloc_var(program, is_const, node);
    if (!var)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    *out_var = var;

    if (global) {
        var->type        = VAR_GLOBAL;
        var->array_idx   = program->num_globals++;
        var->next        = program->globals;
        program->globals = var;
    }
    else {
        scope = program->scope_stack;
        while (scope->next && ! scope->is_function)
            scope = scope->next;

        var->next            = scope->fun_vars_list;
        scope->fun_vars_list = var;
    }

_error:
    return error;
}

static int _visit_child_nodes(struct _KOS_COMP_UNIT      *program,
                              const struct _KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node && ! error; node = node->next)
        error = _visit_node(program, node);

    return error;
}

struct _IMPORT_INFO {
    struct _KOS_COMP_UNIT      *program;
    const struct _KOS_AST_NODE *node;
};

static int _import_global(const char *global_name,
                          unsigned    global_length,
                          int         module_idx,
                          int         global_idx,
                          void       *cookie)
{
    int                         error  = KOS_SUCCESS;
    struct _IMPORT_INFO        *info   = (struct _IMPORT_INFO *)cookie;
    struct _KOS_AST_NODE *const g_node = (struct _KOS_AST_NODE *)
        _KOS_mempool_alloc(&info->program->allocator, sizeof(struct _KOS_AST_NODE) + global_length);

    if (g_node) {

        struct _KOS_TOKEN *token = &g_node->token;
        struct _KOS_VAR   *var;

        memset(g_node, 0, sizeof(*g_node));

        token->begin    = (char *)g_node + sizeof(struct _KOS_AST_NODE);
        token->length   = global_length;
        token->pos      = info->node->token.pos;
        token->type     = TT_IDENTIFIER;

        memcpy((void *)token->begin, global_name, global_length);

        g_node->type = NT_IDENTIFIER;

        error = _define_local_var(info->program, 1, g_node, &var);
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int _import(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int error;
    int module_idx;

    assert(program->scope_stack);
    assert(!program->scope_stack->next);

    node = node->children;
    assert(node);

    assert(program->import_module);
    TRY(program->import_module(program->ctx,
                               node->token.begin,
                               node->token.length,
                               &module_idx));

    if (!node->next) {

        struct _KOS_VAR *var = _KOS_find_var(program->scope_stack->vars, &node->token);

        /* Importing the same module multiple times is allowed. */
        if (var) {
            if (var->type != VAR_MODULE) {
                program->error_token = &node->token;
                program->error_str   = str_err_module_global_conflict;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
        }
        else {
            var = _alloc_var(program, 1, node);
            if (!var)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            var->type        = VAR_MODULE;
            var->array_idx   = module_idx;
            var->next        = program->modules;
            program->modules = var;
        }
    }

    node = node->next;

    if (node) {
        if (node->token.op == OT_MUL) {
            struct _IMPORT_INFO info;
            info.program = program;
            info.node    = node;

            assert(program->walk_globals);
            error = program->walk_globals(program->ctx,
                                          module_idx,
                                          _import_global,
                                          &info);
        }
        else {
            for ( ; node; node = node->next) {

                struct _KOS_VAR *var;

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                error = _define_local_var(program, 1, node, &var);
            }
        }
    }

_error:
    return error;
}

static int _scope(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int error;

    TRY(_push_scope(program, program->scope_stack ? 0 : 1, node));

    TRY(_visit_child_nodes(program, node));

    _pop_scope(program);

_error:
    return error;
}

static int _yield(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int                error;
    struct _KOS_SCOPE *scope;

    for (scope = program->scope_stack; scope && ! scope->is_function; scope = scope->next);

    if (scope) {
        if ( ! ((struct _KOS_FRAME *)scope)->yield_token)
            ((struct _KOS_FRAME *)scope)->yield_token = &node->token;
        error = KOS_SUCCESS;
    }
    else {
        program->error_token = &node->token;
        program->error_str   = str_err_unexpected_yield;
        error                = KOS_ERROR_COMPILE_FAILED;
    }

    if (!error)
        error = _visit_child_nodes(program, node);

    return error;
}

static int _var(struct _KOS_COMP_UNIT      *program,
                const struct _KOS_AST_NODE *node)
{
    int              error    = KOS_SUCCESS;
    const unsigned   is_const = node->type == NT_CONST ? 1U : 0U;
    struct _KOS_VAR *var;

    for (node = node->children; node; node = node->next) {
        TRY(_define_local_var(program, is_const, node, &var));
        var->is_active = VAR_INACTIVE;
    }

_error:
    return error;
}

static int _left_hand_side(struct _KOS_COMP_UNIT      *program,
                           const struct _KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    for (node = node->children; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            struct _KOS_VAR *var = 0;

            if (_lookup_local_var(program, &node->token, &var) != KOS_SUCCESS)
                TRY(_lookup_and_mark_var(program, node, &var));

            if (var->is_const) {
                program->error_token = &node->token;
                program->error_str   = str_err_const_assignment;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }
        }
        else {
            assert(node->type != NT_LINE_LITERAL && node->type != NT_THIS_LITERAL);
            TRY(_visit_node(program, node));
        }
    }

_error:
    return error;
}

static int _identifier(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node)
{
    int              error = KOS_SUCCESS;
    struct _KOS_VAR *var;

    if (_lookup_local_var(program, &node->token, &var) != KOS_SUCCESS)
        TRY(_lookup_and_mark_var(program, node, &var));

_error:
    return error;
}

static int _this_literal(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *node)
{
    int                error;
    struct _KOS_SCOPE *scope;

    for (scope = program->scope_stack; scope && ! scope->is_function; scope = scope->next);

    if (scope) {
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

static int _parameter_defaults(struct _KOS_COMP_UNIT      *program,
                               const struct _KOS_AST_NODE *node,
                               const struct _KOS_AST_NODE *name_node)
{
    int              error   = KOS_SUCCESS;
    struct _KOS_VAR *fun_var = 0;

    assert(name_node);
    if (name_node->type == NT_NAME_CONST) {
        assert(name_node->children);
        assert(name_node->children->type == NT_IDENTIFIER);
        assert(name_node->children->token.type == TT_IDENTIFIER);

        fun_var = _KOS_find_var(program->scope_stack->vars, &name_node->children->token);
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

            struct _KOS_AST_NODE *def_node = node->children;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            TRY(_visit_node(program, def_node));
        }
    }

    if (fun_var)
        fun_var->is_active = VAR_ACTIVE;

_error:
    return error;
}

static int _function_literal(struct _KOS_COMP_UNIT      *program,
                             const struct _KOS_AST_NODE *node)
{
    int                         error;
    int                         i;
    int                         ellipsis = 0;
    const struct _KOS_AST_NODE *arg_node;
    const struct _KOS_AST_NODE *name_node;

    TRY(_push_function(program, node));

    name_node = node->children;
    assert(name_node);
    assert(name_node->type == NT_NAME || name_node->type == NT_NAME_CONST);

    node = name_node->next;
    assert(node);
    assert(node->type == NT_PARAMETERS);

    for (i = 0, arg_node = node->children; arg_node; i++, arg_node = arg_node->next) {

        struct _KOS_VAR            *var;
        const struct _KOS_AST_NODE *ident_node = arg_node;

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

        TRY(_define_local_var(program, 0, ident_node, &var));

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

    TRY(_visit_node(program, node));

    node = node->next;
    assert(node->type == NT_LANDMARK);
    assert(!node->next);

    _pop_scope(program);

    TRY(_parameter_defaults(program, arg_node, name_node));

_error:
    return error;
}

static int _catch(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int              error;
    struct _KOS_VAR *var;

    node = node->children;

    assert(node);
    assert(node->type == NT_CONST || node->type == NT_VAR);

    TRY(_visit_node(program, node));

    assert(node->children);
    assert(node->children->type == NT_IDENTIFIER);
    assert( ! node->children->next);

    var = _KOS_find_var(program->scope_stack->vars, &node->children->token);
    assert(var);

    assert(var->is_active == VAR_INACTIVE);
    var->is_active = VAR_ACTIVE;

    for (node = node->next; node; node = node->next)
        TRY(_visit_node(program, node));

    var->is_active = VAR_INACTIVE;

_error:
    return error;
}

static int _assert(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    node = node->children;
    assert(node);
    assert(node->next);
    assert(node->next->type == NT_LANDMARK);
    assert( ! node->next->next);

    return _visit_node(program, node);
}

static int _is_self_ref_func(const struct _KOS_AST_NODE *node)
{
    if (node->type != NT_CONST)
        return 0;

    assert(node->children);
    assert(node->next);

    if (node->next->type != NT_FUNCTION_LITERAL    &&
        node->next->type != NT_CONSTRUCTOR_LITERAL &&
        node->next->type != NT_CLASS_LITERAL)
        return 0;

    assert(node->children->type == NT_IDENTIFIER);
    assert( ! node->children->next);

    return 1;
}

static int _assignment(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *input_node)
{
    int                         error = KOS_SUCCESS;
    const struct _KOS_AST_NODE *node  = input_node;

    assert(node->type == NT_ASSIGNMENT);

    node = node->children;

    assert(node);
    assert(node->next);

    if (_is_self_ref_func(node)) {

        TRY(_visit_node(program, node));

        _KOS_activate_self_ref_func(program, node);

        node = node->next;
        assert( ! node->next);

        TRY(_visit_node(program, node));
    }
    else {

        TRY(_visit_child_nodes(program, input_node));

        _KOS_activate_new_vars(program, node);
    }

_error:
    return error;
}

static int _visit_node(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {
        case NT_IMPORT:
            /* fall through */
        default:
            assert(node->type == NT_IMPORT);
            error = _import(program, node);
            break;
        case NT_YIELD:
            error = _yield(program, node);
            break;
        case NT_LEFT_HAND_SIDE:
            error = _left_hand_side(program, node);
            break;
        case NT_IDENTIFIER:
            error = _identifier(program, node);
            break;
        case NT_THIS_LITERAL:
            error = _this_literal(program, node);
            break;
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            error = _function_literal(program, node);
            break;
        case NT_ASSIGNMENT:
            error = _assignment(program, node);
            break;
        case NT_CATCH:
            error = _catch(program, node);
            break;
        case NT_ASSERT:
            error = _assert(program, node);
            break;

        case NT_VAR:
            /* fall through */
        case NT_CONST:
            error = _var(program, node);
            break;

        case NT_MULTI_ASSIGNMENT:
            /* fall through */
        case NT_IN:
            error = _visit_child_nodes(program, node);
            if ( ! error)
                _KOS_activate_new_vars(program, node->children);
            break;

        case NT_TRY:
            /* fall through */
        case NT_FOR_IN: /* Has its own scope for the iterator variable */
            /* fall through */
        case NT_SCOPE:
            /* fall through */
        case NT_CONTINUE: /* Create fake scope just for catch refs */
            /* fall through */
        case NT_BREAK:    /* Create fake scope just for catch refs */
            error = _scope(program, node);
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
            /* fall through */
        case NT_FALLTHROUGH:
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
        case NT_STREAM:
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
        case NT_CLASS_LITERAL:
            /* fall through */
        case NT_ASYNC:
            error = _visit_child_nodes(program, node);
            break;
    }

    return error;
}

void _KOS_activate_var(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node)
{

    struct _KOS_VAR *var;

    assert(node->type == NT_IDENTIFIER);

    var = _KOS_find_var(program->scope_stack->vars, &node->token);
    assert(var);

    if ( ! var->is_active)
        var->is_active = VAR_ACTIVE;
}

void _KOS_activate_new_vars(struct _KOS_COMP_UNIT      *program,
                            const struct _KOS_AST_NODE *node)
{
    assert(node);

    if (node->type == NT_VAR || node->type == NT_CONST) {

        node = node->children;

        assert(node);

        for ( ; node; node = node->next)
            _KOS_activate_var(program, node);
    }
    else {
        assert(node->type == NT_LEFT_HAND_SIDE);
    }
}

void _KOS_activate_self_ref_func(struct _KOS_COMP_UNIT      *program,
                                 const struct _KOS_AST_NODE *node)
{
    if (_is_self_ref_func(node)) {

        struct _KOS_VAR *var;

        var = _KOS_find_var(program->scope_stack->vars, &node->children->token);
        assert(var);

        var->is_active = VAR_ACTIVE;
    }
}

static int _deactivate(struct _KOS_RED_BLACK_NODE *node,
                       void                       *cookie)
{
    struct _KOS_VAR *var = (struct _KOS_VAR *)node;

    if (var->is_active == VAR_ACTIVE)
        var->is_active = VAR_INACTIVE;

    return 0;
}

void _KOS_deactivate_vars(struct _KOS_SCOPE *scope)
{
    _KOS_red_black_walk(scope->vars, _deactivate, 0);
}

int _KOS_compiler_process_vars(struct _KOS_COMP_UNIT      *program,
                               const struct _KOS_AST_NODE *ast)
{
    assert(ast->type == NT_SCOPE);
    return _visit_node(program, ast);
}

static int _predefine_global(struct _KOS_COMP_UNIT *program,
                             const char            *name,
                             int                    idx,
                             int                    is_const,
                             enum _KOS_VAR_TYPE     type)
{
    int            error = KOS_SUCCESS;
    const unsigned len   = (unsigned)strlen(name);

    struct _KOS_PRE_GLOBAL *global = (struct _KOS_PRE_GLOBAL *)
        _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_PRE_GLOBAL) + len);

    if (global) {
        memset(&global->node, 0, sizeof(global->node));
        memcpy(global->name_buf, name, len+1);

        global->next                   = program->pre_globals;
        global->type                   = type;
        global->idx                    = idx;
        global->is_const               = is_const;
        global->node.type              = NT_IDENTIFIER;
        global->node.token.begin       = global->name_buf;
        global->node.token.length      = len;
        global->node.token.pos.file_id = (unsigned)program->file_id;
        global->node.token.type        = TT_IDENTIFIER;
        program->pre_globals           = global;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

int _KOS_compiler_predefine_global(struct _KOS_COMP_UNIT *program,
                                   const char            *name,
                                   int                    idx,
                                   int                    is_const)
{
    return _predefine_global(program, name, idx, is_const, VAR_GLOBAL);
}

int _KOS_compiler_predefine_module(struct _KOS_COMP_UNIT *program,
                                   const char            *name,
                                   int                    idx)
{
    return _predefine_global(program, name, idx, 1, VAR_MODULE);
}

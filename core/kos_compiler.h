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

#ifndef __KOS_COMPILER_H
#define __KOS_COMPILER_H

#include "../inc/kos_bytecode.h"
#include "kos_ast.h"
#include "kos_memory.h"
#include "kos_red_black.h"
#include "kos_utf8.h"
#include <stdint.h>

struct _KOS_REG {
    struct _KOS_REG *next;
    struct _KOS_REG *prev;
    int              reg;
    int              tmp;
};

enum _KOS_VAR_TYPE {
    VAR_LOCAL                  = 1,
    VAR_ARGUMENT               = 2,
    VAR_INDEPENDENT            = 4,
    VAR_INDEPENDENT_LOCAL      = 5,
    VAR_INDEPENDENT_ARGUMENT   = 6,
    VAR_LOCALS_AND_ARGS        = 3,
    VAR_ARGUMENT_IN_REG        = 8, /* Argument stored directly in a register */
    VAR_INDEPENDENT_ARG_IN_REG = 12,
    VAR_GLOBAL                 = 16,
    VAR_MODULE                 = 32
};

enum _KOS_VAR_ACTIVE {
    VAR_INACTIVE,
    VAR_ACTIVE,
    VAR_ALWAYS_ACTIVE
};

struct _KOS_VAR {
    struct _KOS_RED_BLACK_NODE  rb_tree_node;

    struct _KOS_VAR            *next;
    const struct _KOS_TOKEN    *token;
    struct _KOS_REG            *reg;
    const struct _KOS_AST_NODE *value;
    int                         num_reads;         /* Number of reads from a variable (including closures) */
    int                         num_assignments;   /* Number of writes to a variable (including closures)  */
    int                         local_reads;       /* Number of local reads from a variable                */
    int                         local_assignments; /* Number of local writes to a variable                 */
    int                         array_idx;
    int                         type         : 7;
    int                         is_active    : 3;  /* Becomes active/searchable after the node */
                                                   /* which declares it. */
    unsigned                    is_const     : 1;
    unsigned                    has_defaults : 1;
};

struct _KOS_BREAK_OFFS {
    struct _KOS_BREAK_OFFS *next;
    int                     offs;
    enum _KOS_NODE_TYPE     type;
};

struct _KOS_RETURN_OFFS {
    struct _KOS_RETURN_OFFS *next;
    int                      offs;
};

struct _KOS_FRAME {
    struct _KOS_REG            *free_regs; /* Allocated registers which are currently unused */
    struct _KOS_REG            *used_regs;
    struct _KOS_REG            *this_reg;
    struct _KOS_REG            *args_reg;
    struct _KOS_RED_BLACK_NODE *closures;
    struct _KOS_FRAME          *parent_frame;
    const struct _KOS_TOKEN    *fun_token;
    struct _KOS_BREAK_OFFS     *break_offs;
    struct _KOS_RETURN_OFFS    *return_offs; /* For return statements inside finally clause (defer) */
    struct _KOS_SCOPE          *last_try_scope;
    const struct _KOS_TOKEN    *yield_token;
    struct _KOS_COMP_FUNCTION  *constant;   /* The template for the constant object, used with LOAD.CONST */
    int                         num_regs;
    uint32_t                    num_instr;
};

struct _KOS_CATCH_REF {
    struct _KOS_SCOPE *next;           /* Used by child_scopes */
    struct _KOS_SCOPE *child_scopes;   /* List of child scopes which need to update catch offset to this scope */
    struct _KOS_REG   *catch_reg;      /* Exception register used in this scope, or -1 if no catch */
    int                catch_offs[6];  /* Catch instructions offsets in this scope, which update catch offsets for the parent scope */
    int                finally_active; /* For return statements inside try/catch */
};

struct _KOS_SCOPE {
    struct _KOS_RED_BLACK_NODE  rb_tree_node;

    const struct _KOS_AST_NODE *scope_node;
    struct _KOS_SCOPE          *next;
    struct _KOS_RED_BLACK_NODE *vars;
    struct _KOS_FRAME          *frame;
    struct _KOS_VAR            *fun_vars_list;
    struct _KOS_VAR            *ellipsis;
    int                         num_vars;
    int                         num_indep_vars;
    int                         num_args;
    int                         num_indep_args;
    struct _KOS_CATCH_REF       catch_ref; /* For catch references between scopes */
    unsigned                    is_function    : 1;
    unsigned                    uses_this      : 1;
    unsigned                    have_rest      : 1; /* Has more args than fit in registers */
};

struct _KOS_SCOPE_REF {
    struct _KOS_RED_BLACK_NODE rb_tree_node;

    struct _KOS_SCOPE         *closure;
    struct _KOS_REG           *vars_reg;
    struct _KOS_REG           *args_reg;
    unsigned                   exported_locals;
    unsigned                   exported_args;
};

enum _KOS_COMP_CONST_TYPE {
    KOS_COMP_CONST_INTEGER,
    KOS_COMP_CONST_FLOAT,
    KOS_COMP_CONST_STRING,
    KOS_COMP_CONST_FUNCTION,
    KOS_COMP_CONST_PROTOTYPE
};

enum _KOS_COMP_FUNC_FLAGS {
    KOS_COMP_FUN_ELLIPSIS  = 1,
    KOS_COMP_FUN_GENERATOR = 2,
    KOS_COMP_FUN_CLASS     = 4
};

struct _KOS_COMP_CONST {
    struct _KOS_RED_BLACK_NODE rb_tree_node;
    enum _KOS_COMP_CONST_TYPE  type;
    uint32_t                   index;
    struct _KOS_COMP_CONST    *next;
};

struct _KOS_COMP_INTEGER {
    struct _KOS_COMP_CONST header;
    int64_t                value;
};

struct _KOS_COMP_FLOAT {
    struct _KOS_COMP_CONST header;
    double                 value;
};

struct _KOS_COMP_STRING {
    struct _KOS_COMP_CONST header;
    const char            *str;
    unsigned               length;
    enum _KOS_UTF8_ESCAPE  escape;
};

struct _KOS_COMP_FUNCTION {
    struct _KOS_COMP_CONST header;
    uint32_t               offset;
    uint8_t                num_regs;
    uint8_t                args_reg;
    uint8_t                num_args;
    uint8_t                flags;
};

struct _KOS_PRE_GLOBAL {
    struct _KOS_PRE_GLOBAL *next;
    struct _KOS_AST_NODE    node;
    enum _KOS_VAR_TYPE      type;
    int                     idx;
    int                     is_const;
    char                    name_buf[1];
};

struct _KOS_COMP_ADDR_TO_LINE {
    uint32_t offs;
    uint32_t line;
};

struct _KOS_COMP_ADDR_TO_FUNC {
    uint32_t offs;
    uint32_t line;
    uint32_t str_idx;
    uint32_t num_instr;
    uint32_t code_size;
};

enum _KOS_IMPORT_TYPE {
    KOS_IMPORT_INDIRECT,
    KOS_IMPORT_DIRECT
};

typedef int (*KOS_COMP_IMPORT_MODULE)(void       *vframe,
                                      const char *name,
                                      unsigned    length,
                                      int        *module_idx);

typedef int (*KOS_COMP_GET_GLOBAL_IDX)(void       *vframe,
                                       int         module_idx,
                                       const char *name,
                                       unsigned    length,
                                       int        *global_idx);

typedef int (*KOS_COMP_WALL_GLOBALS_CALLBACK)(const char *global_name,
                                              unsigned    global_length,
                                              int         module_idx,
                                              int         global_idx,
                                              void       *cookie);

typedef int (*KOS_COMP_WALK_GLOBALS)(void                          *vframe,
                                     int                            module_idx,
                                     KOS_COMP_WALL_GLOBALS_CALLBACK callback,
                                     void                          *cookie);

struct _KOS_COMP_UNIT {
    const struct _KOS_TOKEN    *error_token;
    const char                 *error_str;

    int                         optimize;
    int                         num_optimizations;

    int                         file_id;
    int                         cur_offs;
    struct _KOS_FRAME          *cur_frame;

    struct _KOS_REG            *unused_regs; /* Register objects reusable without allocating memory */

    struct _KOS_RED_BLACK_NODE *scopes;
    struct _KOS_SCOPE          *scope_stack;

    struct _KOS_PRE_GLOBAL     *pre_globals;
    struct _KOS_VAR            *globals;
    int                         num_globals;

    void                       *frame;
    KOS_COMP_IMPORT_MODULE      import_module;
    KOS_COMP_GET_GLOBAL_IDX     get_global_idx;
    KOS_COMP_WALK_GLOBALS       walk_globals;

    struct _KOS_VAR            *modules;
    int                         num_modules;

    struct _KOS_RED_BLACK_NODE *constants;
    struct _KOS_COMP_CONST     *first_constant;
    struct _KOS_COMP_CONST     *last_constant;
    int                         num_constants;

    struct _KOS_MEMPOOL         allocator;

    struct _KOS_VECTOR          code_buf;
    struct _KOS_VECTOR          code_gen_buf;

    struct _KOS_VECTOR          addr2line_buf;
    struct _KOS_VECTOR          addr2line_gen_buf;
    struct _KOS_VECTOR          addr2func_buf;
};

struct _KOS_AST_NODE;

void _KOS_compiler_init(struct _KOS_COMP_UNIT *program,
                        int                    file_id);

int _KOS_compiler_predefine_global(struct _KOS_COMP_UNIT *program,
                                   const char            *name,
                                   int                    idx,
                                   int                    is_const);

int _KOS_compiler_predefine_module(struct _KOS_COMP_UNIT *program,
                                   const char            *name,
                                   int                    idx);

int _KOS_compiler_compile(struct _KOS_COMP_UNIT *program,
                          struct _KOS_AST_NODE  *ast);

void _KOS_compiler_destroy(struct _KOS_COMP_UNIT *program);

const struct _KOS_AST_NODE *_KOS_get_const(struct _KOS_COMP_UNIT      *program,
                                           const struct _KOS_AST_NODE *node);

int _KOS_node_is_truthy(struct _KOS_COMP_UNIT      *program,
                        const struct _KOS_AST_NODE *node);

int _KOS_node_is_falsy(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node);

int _KOS_compiler_process_vars(struct _KOS_COMP_UNIT      *program,
                               const struct _KOS_AST_NODE *ast);

int _KOS_optimize(struct _KOS_COMP_UNIT *program,
                  struct _KOS_AST_NODE  *ast);

int _KOS_allocate_args(struct _KOS_COMP_UNIT *program,
                       struct _KOS_AST_NODE  *ast);

struct _KOS_VAR *_KOS_find_var(struct _KOS_RED_BLACK_NODE *rb_root,
                               const struct _KOS_TOKEN    *token);

void _KOS_activate_var(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node);

void _KOS_activate_new_vars(struct _KOS_COMP_UNIT      *program,
                            const struct _KOS_AST_NODE *node);

void _KOS_activate_self_ref_func(struct _KOS_COMP_UNIT      *program,
                                 const struct _KOS_AST_NODE *node);

void _KOS_deactivate_vars(struct _KOS_SCOPE *scope);

int _KOS_scope_compare_node(struct _KOS_RED_BLACK_NODE *a,
                            struct _KOS_RED_BLACK_NODE *b);

int _KOS_scope_compare_item(void                       *what,
                            struct _KOS_RED_BLACK_NODE *node);

struct _KOS_SCOPE_REF *_KOS_find_scope_ref(struct _KOS_FRAME *frame,
                                           struct _KOS_SCOPE *closure);

#endif

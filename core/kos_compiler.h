/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_COMPILER_H_INCLUDED
#define KOS_COMPILER_H_INCLUDED

#include "../inc/kos_bytecode.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_utf8.h"
#include "kos_ast.h"
#include "kos_compiler_hash.h"
#include "kos_red_black.h"
#include <stdint.h>

/* base module is always at index 0 */
#define KOS_BASE_MODULE_IDX 0

typedef struct KOS_REG_S {
    struct KOS_REG_S *next;
    struct KOS_REG_S *prev;
    int               reg;
    int               tmp;
} KOS_REG;

enum KOS_VAR_TYPE_E {
    VAR_LOCAL                  = 1,
    VAR_ARGUMENT               = 2,
    VAR_INDEPENDENT            = 4,
    VAR_INDEPENDENT_LOCAL      = 5,
    VAR_INDEPENDENT_ARGUMENT   = 6,
    VAR_LOCALS_AND_ARGS        = 3,
    VAR_ARGUMENT_IN_REG        = 8, /* Argument stored directly in a register */
    VAR_INDEPENDENT_ARG_IN_REG = 12,
    VAR_GLOBAL                 = 16,
    VAR_MODULE                 = 32,
    VAR_IMPORTED               = 64
};

typedef struct KOS_VAR_S {
    struct KOS_VAR_S   *scope_next;        /* Pointer to next variable on the list of variables in parent scope */
    struct KOS_VAR_S   *next;              /* Pointer to next variable on the list of variables in frame scope */
    struct KOS_SCOPE_S *scope;             /* Parent scope where this variable was declared                    */
    const KOS_TOKEN    *token;
    KOS_REG            *reg;
    const KOS_AST_NODE *value;
    struct KOS_VAR_S   *shadowed_var;      /* Shadowed variable in the hash table                   */
    uint32_t            hash;              /* Hash used as index in the hash table                  */
    int                 num_reads;         /* Number of reads from a variable (including closures)  */
    int                 num_reads_prev;    /* num_reads from prev pass, for assignment optimization */
    int                 num_assignments;   /* Number of writes to a variable (including closures)   */
    int                 local_reads;       /* Number of local reads from a variable                 */
    int                 local_assignments; /* Number of local writes to a variable                  */
    int                 module_idx;        /* Index of module when type == VAR_IMPORTED             */
    int                 array_idx;
    unsigned            type         : 7;
    unsigned            is_const     : 1;
    unsigned            has_defaults : 1;
} KOS_VAR;

#define KOS_NO_JUMP (~0u)

typedef struct KOS_BREAK_OFFS_S {
    struct KOS_BREAK_OFFS_S *next;
    uint32_t                 entry;
    KOS_NODE_TYPE            type;
} KOS_BREAK_OFFS;

typedef struct KOS_RETURN_OFFS_S {
    struct KOS_RETURN_OFFS_S *next;
    uint32_t                  entry;
} KOS_RETURN_OFFS;

typedef struct KOS_CATCH_REF_S {
    struct KOS_SCOPE_S *next;            /* Used by child_scopes */
    struct KOS_SCOPE_S *child_scopes;    /* List of child scopes which need to update catch offset to this scope */
    KOS_REG            *catch_reg;       /* Exception register used in this scope, or -1 if no catch */
    int                 finally_active;  /* For return statements inside try/catch */
    int                 num_catch_offs;  /* Number of active offsets in catch_offs */
    uint32_t            catch_entry[25]; /* Catch instructions offsets in this scope, which update catch offsets for the parent scope */
} KOS_CATCH_REF;

typedef struct KOS_SCOPE_S {
    const KOS_AST_NODE *scope_node;
    struct KOS_SCOPE_S *parent_scope;
    struct KOS_FRAME_S *owning_frame;
    KOS_VAR            *vars;
    KOS_VAR            *fun_vars_list;
    KOS_VAR            *ellipsis;
    int                 num_vars;
    int                 num_indep_vars;
    int                 num_args;
    int                 num_indep_args;
    KOS_CATCH_REF       catch_ref; /* For catch references between scopes */
    unsigned            has_frame   : 1;
    unsigned            is_function : 1;
    unsigned            uses_this   : 1;
    unsigned            have_rest   : 1; /* Has more args than fit in registers */
} KOS_SCOPE;

typedef struct KOS_FRAME_S {
    KOS_SCOPE                   scope;
    KOS_REG                    *free_regs;        /* Allocated registers which are currently unused */
    KOS_REG                    *used_regs;
    KOS_REG                    *this_reg;
    KOS_REG                    *args_reg;
    KOS_REG                    *base_ctor_reg;
    KOS_REG                    *base_proto_reg;
    KOS_RED_BLACK_NODE         *closures;
    struct KOS_FRAME_S         *parent_frame;
    const KOS_TOKEN            *fun_token;
    KOS_BREAK_OFFS             *break_offs;
    KOS_RETURN_OFFS            *return_offs;      /* For return statements inside finally clause (defer) */
    KOS_SCOPE                  *last_try_scope;
    const KOS_TOKEN            *yield_token;
    struct KOS_COMP_FUNCTION_S *constant;         /* The template for the constant object, used with LOAD.CONST */
    int                         num_def_used;     /* Number of used default args, for optimization              */
    int                         num_binds;        /* Number of closure accesses, for optimization               */
    int                         num_binds_prev;   /* Number of closure accesses in previous optimization cycle  */
    int                         num_self_refs;    /* Number of function's self-references                       */
    int                         num_regs;
    uint32_t                    num_instr;
    unsigned                    uses_base_ctor  : 1;
    unsigned                    uses_base_proto : 1;
    unsigned                    is_open         : 1; /* Set to 1 when processing this frame */
} KOS_FRAME;

typedef struct KOS_SCOPE_REF_S {
    KOS_RED_BLACK_NODE rb_tree_node;

    KOS_SCOPE         *closure;
    KOS_REG           *vars_reg;
    KOS_REG           *args_reg;
    int                vars_reg_idx;
    int                args_reg_idx;
    unsigned           exported_locals;
    unsigned           exported_args;
} KOS_SCOPE_REF;

enum KOS_COMP_CONST_TYPE_E {
    KOS_COMP_CONST_INTEGER,
    KOS_COMP_CONST_FLOAT,
    KOS_COMP_CONST_STRING,
    KOS_COMP_CONST_FUNCTION,
    KOS_COMP_CONST_PROTOTYPE
};

enum KOS_COMP_FUNC_FLAGS_E {
    KOS_COMP_FUN_CLOSURE   = 1,
    KOS_COMP_FUN_ELLIPSIS  = 2,
    KOS_COMP_FUN_GENERATOR = 4,
    KOS_COMP_FUN_CLASS     = 8
};

typedef struct KOS_COMP_CONST_S {
    KOS_RED_BLACK_NODE         rb_tree_node;
    enum KOS_COMP_CONST_TYPE_E type;
    uint32_t                   index;
    struct KOS_COMP_CONST_S   *next;
} KOS_COMP_CONST;

typedef struct KOS_COMP_INTEGER_S {
    KOS_COMP_CONST header;
    int64_t        value;
} KOS_COMP_INTEGER;

typedef struct KOS_COMP_FLOAT_S {
    KOS_COMP_CONST header;
    double         value;
} KOS_COMP_FLOAT;

typedef struct KOS_COMP_STRING_S {
    KOS_COMP_CONST  header;
    const char     *str;
    uint16_t        length;
    KOS_UTF8_ESCAPE escape;
} KOS_COMP_STRING;

typedef struct KOS_COMP_FUNCTION_S {
    KOS_COMP_CONST header;
    uint32_t       bytecode_offset;     /* Offset in the buffer where bytecode is generated */
    uint32_t       bytecode_size;       /* Size of bytecode */
    uint32_t       addr2line_offset;    /* Offset in the buffer where address-to-line translation is stored */
    uint32_t       addr2line_size;      /* Size of address-to-line translation in bytes */
    uint32_t       name_str_idx;        /* Index of constant string with function's name */
    uint32_t       def_line;            /* First line in source code where the function is defined */
    uint32_t       num_instr;           /* Number of instructions in the function */
    uint8_t        flags;               /* KOS_COMP_FUN_* flags */
    uint8_t        num_regs;            /* Number of registers used by the function */
    uint8_t        closure_size;        /* Number of registers preserved for a closure */

    uint8_t        load_instr;          /* Instruction used for loading the function */

    uint8_t        min_args;            /* Number of args without default values */
    uint8_t        num_decl_def_args;   /* Number of args with default values declared in source code */
    uint8_t        num_used_def_args;   /* Number of args with default values allocated in function */

    uint8_t        num_binds;           /* Number of binds */

    uint8_t        args_reg;            /* Register where first argument is stored */
    uint8_t        rest_reg;            /* Register containing rest args */
    uint8_t        ellipsis_reg;        /* Register containing ellipsis */
    uint8_t        this_reg;            /* Register containing 'this' */
    uint8_t        bind_reg;            /* First bound register */

    uint8_t        num_named_args;

    uint32_t       arg_name_str_idx[1]; /* Array of constant indexes containing argument names */
} KOS_COMP_FUNCTION;

typedef struct KOS_PRE_GLOBAL_S {
    struct KOS_PRE_GLOBAL_S *next;
    KOS_AST_NODE             node;
    enum KOS_VAR_TYPE_E      type;
    int                      idx;
    int                      is_const;
    char                     name_buf[1];
} KOS_PRE_GLOBAL;

struct KOS_COMP_ADDR_TO_LINE_S {
    uint32_t offs;
    uint32_t line;
};

enum KOS_IMPORT_TYPE_E {
    KOS_IMPORT_INDIRECT,
    KOS_IMPORT_DIRECT
};

typedef struct KOS_COMP_UNIT_S {
    const KOS_TOKEN     *error_token;
    const char          *error_str;

    int                  optimize;
    int                  num_optimizations;

    uint16_t             file_id;
    int                  cur_offs;
    KOS_FRAME           *cur_frame;

    KOS_REG             *unused_regs; /* Register objects reusable without allocating memory */

    KOS_SCOPE           *scope_stack;
    KOS_VAR_HASH_TABLE   variables;

    KOS_PRE_GLOBAL      *pre_globals;
    KOS_VAR             *globals;
    int                  num_globals;

    void                *ctx;
    int                  is_interactive; /* Forces top-level scope vars to be globals */

    KOS_VAR             *modules;
    int                  num_modules;

    KOS_RED_BLACK_NODE  *constants;
    KOS_COMP_CONST      *first_constant;
    KOS_COMP_CONST      *last_constant;
    int                  num_constants;

    KOS_MEMPOOL          allocator;

    KOS_VECTOR           code_buf;
    KOS_VECTOR           code_gen_buf;

    KOS_VECTOR           addr2line_buf;
    KOS_VECTOR           addr2line_gen_buf;

    KOS_VECTOR           jump_buf;
} KOS_COMP_UNIT;

void kos_compiler_init(KOS_COMP_UNIT *program,
                       uint16_t       file_id);

int kos_compiler_predefine_global(KOS_COMP_UNIT *program,
                                  const char    *name,
                                  uint16_t       name_len,
                                  int            idx);

int kos_compiler_predefine_module(KOS_COMP_UNIT *program,
                                  const char    *name,
                                  uint16_t       name_len,
                                  int            idx);

int kos_compiler_compile(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *ast,
                         unsigned      *num_opt_passes);

void kos_compiler_destroy(KOS_COMP_UNIT *program);

const KOS_AST_NODE *kos_get_const(KOS_COMP_UNIT      *program,
                                  const KOS_AST_NODE *node);

int kos_node_is_truthy(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node);

int kos_node_is_falsy(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node);

int kos_compiler_process_vars(KOS_COMP_UNIT *program,
                              KOS_AST_NODE  *ast);

int kos_optimize(KOS_COMP_UNIT *program,
                 KOS_AST_NODE  *ast);

int kos_allocate_args(KOS_COMP_UNIT *program,
                      KOS_AST_NODE  *ast);

int kos_is_self_ref_func(const KOS_AST_NODE *node);

KOS_SCOPE_REF *kos_find_scope_ref(KOS_FRAME *frame,
                                  KOS_SCOPE *closure);

int kos_get_module_path_name(KOS_COMP_UNIT       *program,
                             const KOS_AST_NODE  *node,
                             const char         **module_name,
                             uint16_t            *name_len,
                             const KOS_AST_NODE **mod_name_node);

/* Interface between the compiler and the code driving it */

int kos_comp_import_module(void       *ctx,
                           const char *name,
                           uint16_t    name_len,
                           int        *module_idx);

typedef int (*KOS_COMP_WALK_GLOBALS_CALLBACK)(const char *global_name,
                                              uint16_t    global_length,
                                              int         module_idx,
                                              int         global_idx,
                                              void       *cookie);

int kos_comp_resolve_global(void                          *vframe,
                            int                            module_idx,
                            const char                    *name,
                            uint16_t                       length,
                            KOS_COMP_WALK_GLOBALS_CALLBACK callback,
                            void                          *cookie);

int kos_comp_walk_globals(void                          *ctx,
                          int                            module_idx,
                          KOS_COMP_WALK_GLOBALS_CALLBACK callback,
                          void                          *cookie);

int kos_comp_check_private_global(void            *ctx,
                                  const KOS_TOKEN *token);

#endif

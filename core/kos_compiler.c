/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "kos_compiler.h"
#include "kos_ast.h"
#include "kos_config.h"
#include "kos_disasm.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "../inc/kos_error.h"
#include "../inc/kos_bytecode.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#define KOS_MAX_REGS 255

static const char str_err_cannot_invoke_void_ctor[]   = "cannot invoke void constructor";
static const char str_err_catch_nesting_too_deep[]    = "too many nesting levels of 'try'/'defer'/'with' statements";
static const char str_err_duplicate_property[]        = "duplicate object property";
static const char str_err_expected_refinement[]       = "expected .identifier or '[' in argument to 'delete'";
static const char str_err_expected_refinement_ident[] = "expected identifier";
static const char str_err_invalid_index[]             = "index out of range";
static const char str_err_invalid_numeric_literal[]   = "invalid numeric literal";
static const char str_err_no_such_module_variable[]   = "no such global in module";
static const char str_err_operand_not_numeric[]       = "operand is not a numeric constant";
static const char str_err_operand_not_string[]        = "operand is not a string";
static const char str_err_return_in_generator[]       = "complex return statement in a generator function, return value always ignored";
static const char str_err_too_many_args[]             = "too many arguments passed to a function";
static const char str_err_too_many_constants[]        = "generated too many constants";
static const char str_err_too_many_registers[]        = "register capacity exceeded, try to refactor the program";
static const char str_err_too_many_vars_for_range[]   = "too many variables specified for a range, only one variable is supported";
static const char str_err_unexpected_super[]          = "'super' cannot be used in this context";
static const char str_err_unexpected_underscore[]     = "'_' cannot be used in this context";

enum KOS_BOOL_E {
    KOS_FALSE_VALUE,
    KOS_TRUE_VALUE
};

static int visit_node(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg);

static int invocation(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg,
                      KOS_BYTECODE_INSTR  instr);

static int function_literal(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_VAR            *fun_var,
                            KOS_REG            *base_ctor_reg,
                            KOS_REG            *base_proto_reg,
                            KOS_REG           **reg);

static int class_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_VAR            *class_var,
                         KOS_REG           **reg);

static int object_literal(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg,
                          KOS_VAR            *class_var,
                          KOS_REG            *prototype);

static int gen_new_reg(KOS_COMP_UNIT *program,
                       KOS_REG      **out_reg)
{
    int        error = KOS_SUCCESS;
    KOS_FRAME *frame = program->cur_frame;
    KOS_REG   *reg;

    assert(frame->num_regs <= KOS_MAX_REGS);
    assert(frame->scope.scope_node);

    if (frame->num_regs == KOS_MAX_REGS) {
        program->error_token = &frame->scope.scope_node->token;
        program->error_str   = str_err_too_many_registers;
        return KOS_ERROR_COMPILE_FAILED;
    }

    if (program->unused_regs) {
        reg                  = program->unused_regs;
        program->unused_regs = reg->next;
    }
    else {
        reg = (KOS_REG *)KOS_mempool_alloc(&program->allocator, sizeof(KOS_REG));

        if ( ! reg)
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (reg)
        reg->reg = frame->num_regs++;

    *out_reg = reg;

    return error;
}

static void mark_reg_as_used(KOS_FRAME *frame,
                             KOS_REG   *reg)
{
    reg->prev = KOS_NULL;
    reg->next = frame->used_regs;

    if (frame->used_regs)
        frame->used_regs->prev = reg;

    frame->used_regs = reg;
}

static int gen_reg(KOS_COMP_UNIT *program,
                   KOS_REG      **out_reg)
{
    int error = KOS_SUCCESS;

    if (!*out_reg) {
        KOS_FRAME *frame = program->cur_frame;
        KOS_REG   *reg   = frame->free_regs;

        if ( ! reg) {
            error = gen_new_reg(program, &reg);
            assert(error || reg);
        }

        if ( ! error) {
            if (frame->free_regs == reg)
                frame->free_regs = reg->next;

            mark_reg_as_used(frame, reg);

            reg->tmp = 1;

            *out_reg = reg;
        }
    }

    return error;
}

static int gen_reg_range(KOS_COMP_UNIT *program,
                         KOS_REG      **out_reg,
                         int            num_regs)
{
    int        error     = KOS_SUCCESS;
    KOS_FRAME *frame     = program->cur_frame;
    KOS_REG  **first_reg = &frame->free_regs;
    KOS_REG   *reg       = frame->free_regs;
    int        count     = reg ? 1 : 0;

    assert(num_regs > 1);

    if (reg) for (;;) {

        KOS_REG *next = reg->next;

        if ( ! next)
            break;

        if (next->reg == reg->reg + 1) {
            if (++count == num_regs)
                break;
        }
        else {
            first_reg = &reg->next;
            count = 1;
        }

        reg = next;
    }

    if ((count == num_regs) ||
        (count && ((*first_reg)->reg + count == frame->num_regs))) {

        reg = *first_reg;

        for ( ; count; --count, --num_regs) {
            KOS_REG *next = reg->next;

            mark_reg_as_used(frame, reg);

            *(out_reg++) = reg;
            *first_reg   = next;
            reg          = next;
        }
    }

    while (num_regs--) {
        reg = KOS_NULL;

        error = gen_new_reg(program, &reg);
        if (error)
            break;

        mark_reg_as_used(frame, reg);

        reg->tmp = 1;

        *(out_reg++) = reg;
    }

    return error;
}

static int gen_dest_reg(KOS_COMP_UNIT *program,
                        KOS_REG      **dest,
                        KOS_REG       *src_reg)
{
    int      error    = KOS_SUCCESS;
    KOS_REG *dest_reg = *dest;

    assert(src_reg);

    if ( ! src_reg->tmp && (src_reg == dest_reg || ! dest_reg)) {
        *dest = KOS_NULL;
        error = gen_reg(program, dest);
    }
    else if ( ! dest_reg)
        *dest = src_reg;

    return error;
}

static void free_reg(KOS_COMP_UNIT *program,
                     KOS_REG       *reg)
{
    assert(reg);
    if (reg->tmp) {
        KOS_FRAME *frame = program->cur_frame;
        KOS_REG  **reg_ptr;

        if (reg->prev)
            reg->prev->next = reg->next;
        else
            frame->used_regs = reg->next;
        if (reg->next)
            reg->next->prev = reg->prev;

        /* Keep free regs sorted */
        reg_ptr = &frame->free_regs;
        while (*reg_ptr && reg->reg > (*reg_ptr)->reg)
            reg_ptr = &(*reg_ptr)->next;
        assert(*reg_ptr != reg);
        reg->next = *reg_ptr;
        reg->prev = KOS_NULL;
        *reg_ptr  = reg;
    }
}

static void free_all_regs(KOS_COMP_UNIT *program,
                          KOS_REG       *reg)
{
    if (reg) {
        KOS_REG *first_reg = reg;

        while (reg->next)
            reg = reg->next;

        reg->next            = program->unused_regs;
        program->unused_regs = first_reg;
    }
}

#ifndef NDEBUG
static int count_used_regs(KOS_COMP_UNIT *program)
{
    int        count = 0;
    KOS_FRAME *frame = program->cur_frame;
    KOS_REG   *reg   = frame->used_regs;

    for ( ; reg; reg = reg->next)
        ++count;

    return count;
}
#endif

/* Lookup variable in local scopes.
 * Arguments in registers, including ellipsis, are treated as local variables.
 * Skip other arguments, globals and modules. */
static int lookup_local_var_even_inactive(KOS_COMP_UNIT      *program,
                                          const KOS_AST_NODE *node,
                                          int                 only_active,
                                          KOS_REG           **reg)
{
    int error = KOS_SUCCESS;

    if (node->is_local_var) {

        KOS_VAR *var = node->u.var;

        assert( ! node->is_scope);
        assert(node->is_var);
        assert(var);

        if (var->type & VAR_LOCAL) {

            if (var == var->scope->ellipsis) {
                assert(var->is_active);
                assert(var->reg);
                *reg = var->reg;
            }
            else if (var->is_active || ! only_active) {

                if ( ! var->reg) {
                    error = gen_reg(program, &var->reg);
                    if ( ! error)
                        var->reg->tmp = 0;
                }

                *reg = var->reg;
            }
        }
        else if (var->type & VAR_ARGUMENT_IN_REG) {
            assert(var->scope->is_function);
            assert(var->reg);
            *reg = var->reg;
        }
    }

    return error;
}

static int lookup_local_var(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    return lookup_local_var_even_inactive(program, node, 1, reg);
}

static int lookup_var(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_VAR           **out_var,
                      KOS_REG           **reg)
{
    KOS_VAR *var = node->is_var ? node->u.var : KOS_NULL;

    assert( ! node->is_scope);

    if (var && ! var->is_active)
        var = KOS_NULL;

    if (var) {
        const int  is_var        = var->type == VAR_INDEPENDENT_LOCAL;
        const int  is_arg_in_reg = var->type == VAR_INDEPENDENT_ARG_IN_REG;
        const int  is_local_arg  = node->is_local_var &&
                                   (var->type & (VAR_ARGUMENT | VAR_ARGUMENT_IN_REG));
        KOS_SCOPE *scope         = var->scope;
        const int  skip_reg      = (var->type & (VAR_GLOBAL | VAR_MODULE | VAR_IMPORTED)) || node->is_const_fun;

        *out_var = var;

        if (is_local_arg) {
            if (reg) {
                assert((var->type & VAR_ARGUMENT) && ! (var->type & VAR_ARGUMENT_IN_REG));
                assert(program->cur_frame->args_reg);
                *reg = program->cur_frame->args_reg;
            }
        }
        else if ( ! skip_reg) {

            KOS_SCOPE_REF *ref;

            assert(is_var ? ( ! scope->is_function || scope->ellipsis == var) : scope->is_function);

            /* Find function scope for this variable */
            while (scope->parent_scope && ! scope->is_function)
                scope = scope->parent_scope;

            ref = kos_find_scope_ref(program->cur_frame, scope);

            assert(ref);
            if (is_var || is_arg_in_reg) {
                assert(ref->exported_locals);
            }
            else {
                assert(ref->exported_args);
            }

            if (reg) {
                if (is_var || is_arg_in_reg)
                    *reg = ref->vars_reg;
                else
                    *reg = ref->args_reg;
            }
        }
    }
    else
        program->error_token = &node->token;

    return var ? KOS_SUCCESS : KOS_ERROR_INTERNAL;
}

static int compare_strings(const char *a, unsigned len_a, KOS_UTF8_ESCAPE escape_a,
                           const char *b, unsigned len_b, KOS_UTF8_ESCAPE escape_b)
{
    const unsigned min_len = (len_a <= len_b) ? len_a : len_b;
    int            result;

    /* TODO compare escaped vs. non-escaped */
    if (escape_a != escape_b)
        return escape_a ? 1 : -1;

    /* TODO do proper unicode compare */
    result = memcmp(a, b, min_len);

    if (result == 0)
        result = (int)len_a - (int)len_b;

    return result;
}

static void get_token_str(const KOS_TOKEN *token,
                          const char     **out_begin,
                          uint16_t        *out_length,
                          KOS_UTF8_ESCAPE *out_escape)
{
    const char *begin  = token->begin;
    uint16_t    length = token->length;

    *out_escape = KOS_UTF8_WITH_ESCAPE;

    if (token->type == TT_STRING || token->type == TT_STRING_OPEN) { /* TT_STRING* */
        if (*begin == 'r' || *begin == 'R') {
            *out_escape = KOS_UTF8_NO_ESCAPE;
            ++begin;
            --length;
        }
        ++begin;
        length -= 2;
        if (token->type > TT_STRING) /* TT_STRING_OPEN_* */
            --length;
        else {
            assert(token->type == TT_STRING);
        }
    }
    else {
        assert(token->type == TT_IDENTIFIER ||
               token->type == TT_KEYWORD    ||
               token->op   == OT_LAMBDA);
    }

    *out_begin  = begin;
    *out_length = length;
}

static int numbers_compare_item(void               *what,
                                KOS_RED_BLACK_NODE *node)
{
    const KOS_NUMERIC    *numeric  = (const KOS_NUMERIC *)what;
    const KOS_COMP_CONST *constant = (const KOS_COMP_CONST *)node;

    const enum KOS_COMP_CONST_TYPE_E type =
        numeric->type == KOS_INTEGER_VALUE ? KOS_COMP_CONST_INTEGER : KOS_COMP_CONST_FLOAT;

    if (type != constant->type)
        return (int)type < (int)constant->type ? -1 : 1;

    if (numeric->type == KOS_INTEGER_VALUE)
        return numeric->u.i < ((const KOS_COMP_INTEGER *)constant)->value ? -1 :
               numeric->u.i > ((const KOS_COMP_INTEGER *)constant)->value ? 1 : 0;
    else
        return numeric->u.d == ((const KOS_COMP_FLOAT *)constant)->value ? 0 :
               numeric->u.d <  ((const KOS_COMP_FLOAT *)constant)->value ? -1 : 1;
}

static int strings_compare_item(void               *what,
                                KOS_RED_BLACK_NODE *node)
{
    const KOS_TOKEN       *token = (const KOS_TOKEN       *)what;
    const KOS_COMP_STRING *str   = (const KOS_COMP_STRING *)node;
    const char            *begin;
    uint16_t               length;
    KOS_UTF8_ESCAPE        escape;

    if (str->header.type != KOS_COMP_CONST_STRING)
        return (int)KOS_COMP_CONST_STRING < (int)str->header.type ? -1 : 1;

    get_token_str(token, &begin, &length, &escape);

    return compare_strings(begin,    length,      escape,
                           str->str, str->length, str->escape);
}

static int constants_compare_node(KOS_RED_BLACK_NODE *a,
                                  KOS_RED_BLACK_NODE *b)
{
    const KOS_COMP_CONST *const_a = (const KOS_COMP_CONST *)a;
    const KOS_COMP_CONST *const_b = (const KOS_COMP_CONST *)b;

    if (const_a->type != const_b->type)
        return (int)const_a->type < (int)const_b->type ? -1 : 0;

    else switch (const_a->type) {

        default:
            assert(const_a->type == KOS_COMP_CONST_INTEGER);
            return ((const KOS_COMP_INTEGER *)const_a)->value
                 < ((const KOS_COMP_INTEGER *)const_b)->value
                 ? -1 : 0;

        case KOS_COMP_CONST_FLOAT:
            return ((const KOS_COMP_FLOAT *)const_a)->value
                 < ((const KOS_COMP_FLOAT *)const_b)->value
                 ? -1 : 0;

        case KOS_COMP_CONST_STRING: {
            const KOS_COMP_STRING *str_a = (const KOS_COMP_STRING *)const_a;
            const KOS_COMP_STRING *str_b = (const KOS_COMP_STRING *)const_b;
            return compare_strings(str_a->str, str_a->length, str_a->escape,
                                   str_b->str, str_b->length, str_b->escape);
        }

        case KOS_COMP_CONST_FUNCTION:
            return ((const KOS_COMP_FUNCTION *)const_a)->bytecode_offset
                 < ((const KOS_COMP_FUNCTION *)const_b)->bytecode_offset
                 ? -1 : 0;

        case KOS_COMP_CONST_PROTOTYPE:
            return const_a->index < const_b->index ? -1 : 0;
    }
}

static void add_constant(KOS_COMP_UNIT  *program,
                         KOS_COMP_CONST *constant)
{
    constant->index = program->num_constants++;
    constant->next  = KOS_NULL;

    if (program->last_constant)
        program->last_constant->next = constant;
    else
        program->first_constant = constant;

    program->last_constant = constant;

    kos_red_black_insert(&program->constants,
                          &constant->rb_tree_node,
                          constants_compare_node);
}

static int gen_str_esc(KOS_COMP_UNIT   *program,
                       const KOS_TOKEN *token,
                       KOS_UTF8_ESCAPE  escape,
                       int             *str_idx)
{
    int error = KOS_SUCCESS;

    KOS_COMP_STRING *str =
            (KOS_COMP_STRING *)kos_red_black_find(program->constants,
                                                  (KOS_TOKEN *)token,
                                                  strings_compare_item);

    if (!str) {

        str = (KOS_COMP_STRING *)
            KOS_mempool_alloc(&program->allocator, sizeof(KOS_COMP_STRING));

        if (str) {

            const char     *begin;
            uint16_t        length;
            KOS_UTF8_ESCAPE tok_escape;

            get_token_str(token, &begin, &length, &tok_escape);

            if (tok_escape == KOS_UTF8_NO_ESCAPE)
                escape = KOS_UTF8_NO_ESCAPE;

            str->header.type = KOS_COMP_CONST_STRING;
            str->str         = begin;
            str->length      = length;
            str->escape      = escape;

            add_constant(program, (KOS_COMP_CONST *)str);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (!error)
        *str_idx = str->header.index;

    return error;
}

static int gen_str(KOS_COMP_UNIT   *program,
                   const KOS_TOKEN *token,
                   int             *str_idx)
{
    return gen_str_esc(program, token, KOS_UTF8_WITH_ESCAPE, str_idx);
}

static uint16_t calc_assert_str_len(const char *begin,
                                    const char *end)
{
    uint16_t length         = 0;
    int      last_printable = 0;

    for ( ; begin < end; ++begin) {

        const uint8_t c = (uint8_t)*begin;

        const int printable = c > 0x20U ? 1 : 0;

        if (printable || last_printable)
            ++length;

        last_printable = printable;
    }

    return length;
}

static void get_assert_str(const char *begin,
                           const char *end,
                           char       *buf,
                           char *const buf_end)
{
    int last_printable = 0;

    for ( ; begin < end && buf < buf_end; ++begin) {

        const char c = *begin;

        const int printable = (uint8_t)c > 0x20U ? 1 : 0;

        if (printable)
            *(buf++) = c;
        else if (last_printable)
            *(buf++) = ' ';

        last_printable = printable;
    }
}

static int gen_assert_str(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          int                *str_idx)
{
    int            error = KOS_SUCCESS;
    const char    *begin;
    const char    *end;
    char          *buf;
    uint16_t       length;
    const uint16_t max_length = 64;

    static const char assertion_failed[] = "Assertion failed: ";

    begin = node->token.begin;
    end   = node->children->next->token.begin;

    /* TODO skip comments */

    /* Skip assert keyword */
    assert(begin + 6 < end);
    assert(begin[0] == 'a' && begin[5] == 't');
    begin += 6;

    /* Skip spaces after the assert keyword */
    for ( ; begin < end && (uint8_t)*begin <= 0x20; ++begin);

    /* Ensure that there is still some expression */
    assert(begin < end);
    assert((uint8_t)*begin > 0x20);

    length = calc_assert_str_len(begin, end) + sizeof(assertion_failed) - 1;

    if (length > max_length)
        length = max_length;

    buf = (char *)KOS_mempool_alloc(&program->allocator, length);

    if (buf) {

        KOS_TOKEN token;

        memcpy(buf, assertion_failed, sizeof(assertion_failed) - 1);
        get_assert_str(begin, end, buf + sizeof(assertion_failed) - 1, buf + length);

        memset(&token, 0, sizeof(token));
        token.begin  = buf;
        token.length = length;
        token.type   = TT_IDENTIFIER;

        error = gen_str_esc(program, &token, KOS_UTF8_NO_ESCAPE, str_idx);
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int add_addr2line(KOS_COMP_UNIT   *program,
                         const KOS_TOKEN *token,
                         enum KOS_BOOL_E  force)
{
    int                            error;
    KOS_VECTOR                    *addr2line = &program->addr2line_gen_buf;
    struct KOS_COMP_ADDR_TO_LINE_S new_loc;

    new_loc.offs = (uint32_t)program->cur_offs;
    new_loc.line = token->line;

    if (addr2line->size && ! force) {

        struct KOS_COMP_ADDR_TO_LINE_S *const last =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (addr2line->buffer + (addr2line->size - sizeof(struct KOS_COMP_ADDR_TO_LINE_S)));

        if (last->offs == new_loc.offs) {
            if (new_loc.line > last->line)
                last->line = new_loc.line;
            return KOS_SUCCESS;
        }
    }

    error = KOS_vector_resize(addr2line, addr2line->size + sizeof(new_loc));

    if ( ! error) {

        struct KOS_COMP_ADDR_TO_LINE_S *const last =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (addr2line->buffer + (addr2line->size - sizeof(new_loc)));

        *last = new_loc;
    }

    return error;
}

static int gen_instr(KOS_COMP_UNIT *program,
                     int            num_args,
                     ...)
{
    int cur_offs = program->cur_offs;
    int error    = KOS_vector_resize(&program->code_gen_buf,
                                    (size_t)cur_offs + 1 + 4 * num_args); /* Over-estimate */

    if (program->code_buf.size + program->code_gen_buf.size > KOS_MAX_CODE_SIZE)
        error = KOS_ERROR_OUT_OF_MEMORY;

    if (!error) {

        KOS_BYTECODE_INSTR instr = INSTR_BREAKPOINT;

        va_list  args;
        int      i;
        uint8_t *buf = (uint8_t *)program->code_gen_buf.buffer;

        va_start(args, num_args);
        for (i = 0; i <= num_args; i++) {

            int32_t value = (int32_t)va_arg(args, int32_t);
            int     size  = 1;

            if (i == 0)
                instr = (KOS_BYTECODE_INSTR)value;
            else
                size = kos_get_operand_size(instr, i-1);

            if (size == 1) {
                if ( ! kos_is_register(instr, i-1)) {
                    if (kos_is_signed_op(instr, i-1)) {
                        assert((uint32_t)(value + 128) < 256U);
                    }
                    else {
                        assert((uint32_t)value < 256U);
                    }
                }
                buf[cur_offs++] = (uint8_t)value;
            }
            else {
                int ibyte;
                for (ibyte = 0; ibyte < size; ibyte++) {
                    buf[cur_offs++] = (uint8_t)value;
                    value >>= 8;
                }
            }
        }
        va_end(args);

        program->cur_offs = cur_offs;

        ++program->cur_frame->num_instr;
    }

    return error;
}

static int gen_instr1(KOS_COMP_UNIT *program,
                      int            opcode,
                      int32_t        operand1)
{
    return gen_instr(program, 1, opcode, operand1);
}

static int gen_instr2(KOS_COMP_UNIT *program,
                      int            opcode,
                      int32_t        operand1,
                      int32_t        operand2)
{
    return gen_instr(program, 2, opcode, operand1, operand2);
}

static int gen_instr3(KOS_COMP_UNIT *program,
                      int            opcode,
                      int32_t        operand1,
                      int32_t        operand2,
                      int32_t        operand3)
{
    return gen_instr(program, 3, opcode, operand1, operand2, operand3);
}

static int gen_instr4(KOS_COMP_UNIT *program,
                      int            opcode,
                      int32_t        operand1,
                      int32_t        operand2,
                      int32_t        operand3,
                      int32_t        operand4)
{
    return gen_instr(program, 4, opcode, operand1, operand2, operand3, operand4);
}

static int gen_instr5(KOS_COMP_UNIT *program,
                      int            opcode,
                      int32_t        operand1,
                      int32_t        operand2,
                      int32_t        operand3,
                      int32_t        operand4,
                      int32_t        operand5)
{
    return gen_instr(program, 5, opcode, operand1, operand2, operand3, operand4, operand5);
}

static int gen_instr_load_const(KOS_COMP_UNIT      *program,
                                const KOS_AST_NODE *node,
                                int                 opcode,
                                int32_t             operand1,
                                int32_t             operand2)
{
    assert((opcode == INSTR_LOAD_CONST) || (opcode == INSTR_LOAD_FUN));

    if (operand2 < 0 || operand2 > 0xFFFF) {
        program->error_str   = str_err_too_many_constants;
        program->error_token = &node->token;
        return KOS_ERROR_COMPILE_FAILED;
    }
    else if (operand2 < 256)
        opcode = (opcode == INSTR_LOAD_CONST) ? INSTR_LOAD_CONST8 : INSTR_LOAD_FUN8;

    return gen_instr2(program, opcode, operand1, operand2);
}

static int gen_load_const(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          int32_t             operand1,
                          int32_t             operand2)
{
    return gen_instr_load_const(program, node, INSTR_LOAD_CONST, operand1, operand2);
}

static void write_jump_offs(KOS_COMP_UNIT *program,
                            KOS_VECTOR    *vec,
                            int            jump_instr_offs,
                            int            target_offs)
{
    uint8_t *buf;
    uint8_t *end;
    uint8_t  opcode;
    int32_t  jump_offs;
    int      jump_instr_size;

    assert((unsigned)jump_instr_offs <  vec->size);
    assert((unsigned)target_offs     <= vec->size);

    buf = (uint8_t *)&vec->buffer[jump_instr_offs];

    opcode = *buf;

    assert(opcode == INSTR_CATCH     ||
           opcode == INSTR_NEXT_JUMP ||
           opcode == INSTR_JUMP      ||
           opcode == INSTR_JUMP_COND ||
           opcode == INSTR_JUMP_NOT_COND);

    switch (opcode) {

        case INSTR_JUMP:
            jump_instr_size = 5;
            break;

        case INSTR_NEXT_JUMP:
            jump_instr_size = 7;
            break;

        default:
            jump_instr_size = 6;
            break;
    }

    jump_offs = target_offs - (jump_instr_offs + jump_instr_size);

    buf += (opcode == INSTR_CATCH) ? 2 : (opcode == INSTR_NEXT_JUMP) ? 3 : 1;

    for (end = buf+4; buf < end; ++buf) {
        *buf      =   (uint8_t)jump_offs;
        jump_offs >>= 8;
    }
}

static void update_jump_offs(KOS_COMP_UNIT *program,
                             int            jump_instr_offs,
                             int            target_offs)
{
    assert(jump_instr_offs <  program->cur_offs);
    assert(target_offs     <= program->cur_offs);

    write_jump_offs(program, &program->code_gen_buf, jump_instr_offs, target_offs);
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

static int free_scope_regs(KOS_RED_BLACK_NODE *node,
                           void               *cookie)
{
    KOS_VAR       *var     = (KOS_VAR *)node;
    KOS_COMP_UNIT *program = (KOS_COMP_UNIT *)cookie;

    if (var->reg && var->type != VAR_INDEPENDENT_LOCAL) {
        var->reg->tmp = 1;
        free_reg(program, var->reg);
        var->reg = KOS_NULL;
    }

    return KOS_SUCCESS;
}

static void pop_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = program->scope_stack;

    assert(scope);

    if (scope->vars)
        kos_red_black_walk(scope->vars, free_scope_regs, (void *)program);

    program->scope_stack = scope->parent_scope;

    if (scope->has_frame)
        program->cur_frame = ((KOS_FRAME *)scope)->parent_frame;
}

typedef struct KOS_IMPORT_INFO_S {
    KOS_COMP_UNIT *program;
    KOS_FILE_POS   pos;
} KOS_IMPORT_INFO;

static int import_global(const char *global_name,
                         uint16_t    global_length,
                         int         module_idx,
                         int         global_idx,
                         void       *cookie)
{
    int              error = KOS_SUCCESS;
    KOS_IMPORT_INFO *info  = (KOS_IMPORT_INFO *)cookie;
    KOS_REG         *reg   = KOS_NULL;
    KOS_VAR         *var;
    KOS_TOKEN        token;

    memset(&token, 0, sizeof(token));

    token.begin   = global_name;
    token.length  = global_length;
    token.file_id = info->pos.file_id;
    token.column  = info->pos.column;
    token.line    = info->pos.line;
    token.type    = TT_IDENTIFIER;

    var = kos_find_var(info->program->scope_stack->vars, &token);

    assert(var);

    if (var->type == VAR_IMPORTED)
        return KOS_SUCCESS;

    assert(var->type == VAR_GLOBAL);

    TRY(gen_reg(info->program, &reg));

    TRY(gen_instr3(info->program, INSTR_GET_MOD_ELEM, reg->reg, module_idx, global_idx));

    TRY(gen_instr2(info->program, INSTR_SET_GLOBAL, var->array_idx, reg->reg));

    free_reg(info->program, reg);

cleanup:
    return error;
}

static int import(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);

    if (node->next) {

        int             module_idx;
        KOS_IMPORT_INFO info;

        info.program = program;

        TRY(kos_comp_import_module(program->ctx,
                                   node->token.begin,
                                   node->token.length,
                                   &module_idx));

        node = node->next;

        if (node->token.op == OT_MUL) {

            info.pos = get_token_pos(&node->token);

            error = kos_comp_walk_globals(program->ctx,
                                          module_idx,
                                          import_global,
                                          &info);
        }
        else {

            for ( ; node; node = node->next) {

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                info.pos = get_token_pos(&node->token);

                error = kos_comp_resolve_global(program->ctx,
                                                module_idx,
                                                node->token.begin,
                                                node->token.length,
                                                import_global,
                                                &info);
                if (error && (error != KOS_ERROR_OUT_OF_MEMORY)) {
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

static int append_frame(KOS_COMP_UNIT     *program,
                        KOS_COMP_FUNCTION *func_constant,
                        int                fun_start_offs,
                        size_t             addr2line_start_offs)
{
    int          error;
    const size_t fun_end_offs = (size_t)program->cur_offs;
    const size_t fun_size     = fun_end_offs - fun_start_offs;
    const size_t fun_new_offs = program->code_buf.size;
    const size_t a2l_new_offs = program->addr2line_buf.size;
    size_t       a2l_size;

    /* Remove last address-to-line entry if it points beyond the end of the function */
    if (program->addr2line_gen_buf.size) {
        const size_t cur_a2l_size = program->addr2line_gen_buf.size;

        struct KOS_COMP_ADDR_TO_LINE_S *last_ptr =
            (struct KOS_COMP_ADDR_TO_LINE_S *)(program->addr2line_gen_buf.buffer + cur_a2l_size);

        --last_ptr;

        if (last_ptr->offs == fun_end_offs)
            TRY(KOS_vector_resize(&program->addr2line_gen_buf,
                                  cur_a2l_size - sizeof(struct KOS_COMP_ADDR_TO_LINE_S)));
    }

    a2l_size = program->addr2line_gen_buf.size - addr2line_start_offs;
    assert( ! (a2l_size % sizeof(struct KOS_COMP_ADDR_TO_LINE_S)));

    TRY(KOS_vector_resize(&program->code_buf, fun_new_offs + fun_size));

    TRY(KOS_vector_resize(&program->addr2line_buf, a2l_new_offs + a2l_size));

    memcpy(program->code_buf.buffer + fun_new_offs,
           program->code_gen_buf.buffer + fun_start_offs,
           fun_size);

    func_constant->bytecode_offset = (uint32_t)fun_new_offs;
    func_constant->bytecode_size   = (uint32_t)fun_size;

    TRY(KOS_vector_resize(&program->code_gen_buf, (size_t)fun_start_offs));

    program->cur_offs = fun_start_offs;

    memcpy(program->addr2line_buf.buffer + a2l_new_offs,
           program->addr2line_gen_buf.buffer + addr2line_start_offs,
           a2l_size);

    func_constant->addr2line_offset = (uint32_t)a2l_new_offs;
    func_constant->addr2line_size   = (uint32_t)a2l_size;
    func_constant->num_instr        = program->cur_frame->num_instr;
    func_constant->def_line         = 1;
    if (program->cur_frame->fun_token)
        func_constant->def_line = program->cur_frame->fun_token->line;

    TRY(KOS_vector_resize(&program->addr2line_gen_buf, addr2line_start_offs));

    /* Update addr2line offsets for this function */
    {
        struct KOS_COMP_ADDR_TO_LINE_S *ptr =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (program->addr2line_buf.buffer + a2l_new_offs);
        struct KOS_COMP_ADDR_TO_LINE_S *const end =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (program->addr2line_buf.buffer + program->addr2line_buf.size);

        for ( ; ptr < end; ptr++)
            ptr->offs -= fun_start_offs;
    }

cleanup:
    return error;
}

static KOS_COMP_FUNCTION *alloc_func_constant(KOS_COMP_UNIT *program,
                                              KOS_FRAME     *frame,
                                              uint32_t       str_idx,
                                              uint8_t        num_named_args)
{
    KOS_COMP_FUNCTION *constant;
    const size_t       alloc_size = sizeof(KOS_COMP_FUNCTION) - sizeof(uint32_t)
                                    + (num_named_args * sizeof(uint32_t));

    constant = (KOS_COMP_FUNCTION *)KOS_mempool_alloc(&program->allocator, alloc_size);

    if (constant) {

        assert(frame->num_regs <= KOS_MAX_REGS);

        memset(constant, 0, alloc_size);

        constant->header.type    = KOS_COMP_CONST_FUNCTION;
        constant->name_str_idx   = str_idx;
        constant->num_regs       = (uint8_t)frame->num_regs;
        constant->load_instr     = INSTR_BREAKPOINT;
        constant->args_reg       = KOS_NO_REG;
        constant->rest_reg       = KOS_NO_REG;
        constant->ellipsis_reg   = KOS_NO_REG;
        constant->this_reg       = KOS_NO_REG;
        constant->bind_reg       = KOS_NO_REG;
        constant->num_named_args = num_named_args;
    }

    return constant;
}

static int finish_global_scope(KOS_COMP_UNIT *program,
                               KOS_REG       *reg)
{
    int                error;
    int                str_idx;
    KOS_COMP_FUNCTION *constant;
    KOS_TOKEN          global;
    static const char  str_global[] = "<global>";

    if ( ! reg) {
        TRY(gen_reg(program, &reg));
        TRY(gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    TRY(gen_instr1(program, INSTR_RETURN, reg->reg));

    free_reg(program, reg);
    reg = KOS_NULL;

    memset(&global, 0, sizeof(global));
    global.begin  = str_global;
    global.length = sizeof(str_global) - 1;
    global.type   = TT_IDENTIFIER;

    TRY(gen_str(program, &global, &str_idx));

    constant = alloc_func_constant(program, program->cur_frame, (uint32_t)str_idx, 0);
    if ( ! constant)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    if (program->scope_stack->num_indep_vars) {
        assert(program->scope_stack->num_indep_vars <= KOS_MAX_REGS);
        constant->flags       |= KOS_COMP_FUN_CLOSURE;
        constant->closure_size = (uint8_t)program->scope_stack->num_indep_vars;
    }

    add_constant(program, &constant->header);

    program->cur_frame->constant = constant;

    TRY(append_frame(program, constant, 0, 0));

    assert(program->code_gen_buf.size == 0);

cleanup:
    return error;
}

static int gen_indep_vars(KOS_COMP_UNIT *program,
                          KOS_SCOPE     *scope,
                          int           *last_reg)
{
    int      error = KOS_SUCCESS;
    KOS_VAR *var;

    assert(scope->has_frame);

    for (var = scope->fun_vars_list; var; var = var->next)
        if (var->type == VAR_INDEPENDENT_LOCAL &&
            (var->num_reads       > var->local_reads ||
             var->num_assignments > var->local_assignments)) {

            TRY(gen_reg(program, &var->reg));
            var->reg->tmp  = 0;
            var->array_idx = var->reg->reg;

            ++*last_reg;
            assert(var->reg->reg == *last_reg);
        }

cleanup:
    return error;
}

static int process_scope(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node)
{
    int       error  = KOS_SUCCESS;
    const int global = program->scope_stack == KOS_NULL;

    const KOS_AST_NODE *child = node->children;

    if (child || global) {

        KOS_REG *reg      = KOS_NULL;
#ifndef NDEBUG
        int      skip_tmp = 0;
#endif

        push_scope(program, node);

        /* Init global scope */
        if (global) {

            int last_reg = -1;

            assert(program->scope_stack->has_frame);
            assert(program->cur_frame);
            assert( ! program->cur_frame->parent_frame);

            /* Generate registers for local (non-global) independent variables */
            TRY(gen_indep_vars(program, program->scope_stack, &last_reg));
        }

        /* Process inner nodes */
        for ( ; child; child = child->next) {

#ifndef NDEBUG
            const int initial_used_regs = count_used_regs(program) - skip_tmp;
#endif

            TRY(add_addr2line(program, &child->token, KOS_FALSE_VALUE));

            if (reg) {
                free_reg(program, reg);
                reg = KOS_NULL;
            }

            TRY(visit_node(program, child, &reg));

#ifndef NDEBUG
            skip_tmp = (reg && reg->tmp) ? 1 : 0;

            if ((child->type != NT_ASSIGNMENT && child->type != NT_MULTI_ASSIGNMENT) ||
                (child->children->type == NT_LEFT_HAND_SIDE)) {

                const int used_regs = count_used_regs(program);
                assert(used_regs == initial_used_regs + skip_tmp);
            }
#endif
        }

        if (global)
            TRY(finish_global_scope(program, reg));
        else if (reg)
            free_reg(program, reg);

        pop_scope(program);
    }

cleanup:
    return error;
}

static int visit_cond_node(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           KOS_REG           **reg,
                           unsigned           *negated)
{
    unsigned neg = 0;

    while ((node->type == NT_OPERATOR) && (node->token.op == OT_LOGNOT)) {
        node = node->children;
        assert(node);
        assert( ! node->next);
        neg ^= 1;
    }

    *negated = neg;

    return visit_node(program, node, reg);
}

typedef struct JUMP_ARRAY_S {
    uint16_t num_jumps;
    uint16_t capacity;
    int     *offs;
    int      tmp[4];
} JUMP_ARRAY;

static void init_jump_array(JUMP_ARRAY *jump_array)
{
    jump_array->num_jumps = 0;
    jump_array->capacity  = (uint16_t)(sizeof(jump_array->tmp) / sizeof(jump_array->tmp[0]));
    jump_array->offs      = &jump_array->tmp[0];
}

static void reset_jump_array(JUMP_ARRAY *jump_array)
{
    jump_array->num_jumps = 0;
}

static void update_jump_array(KOS_COMP_UNIT *program,
                              JUMP_ARRAY    *jump_array,
                              int            target_offs)
{
    int       *offs = jump_array->offs;
    int *const end  = offs + jump_array->num_jumps;

    for (; offs < end; offs++)
        update_jump_offs(program, *offs, target_offs);
}

static int push_jump_array(KOS_COMP_UNIT *program,
                           JUMP_ARRAY    *jump_array,
                           int            offs)
{
    if (jump_array->num_jumps >= jump_array->capacity) {
        const uint16_t new_capacity = jump_array->capacity * 2;
        int     *const new_buf      = (int *)KOS_mempool_alloc(&program->allocator,
                                                               sizeof(int) * new_capacity);

        if ( ! new_buf)
            return KOS_ERROR_OUT_OF_MEMORY;

        if (jump_array->num_jumps)
            memcpy(new_buf, jump_array->offs, sizeof(int) * jump_array->num_jumps);

        jump_array->offs     = new_buf;
        jump_array->capacity = new_capacity;
    }

    jump_array->offs[jump_array->num_jumps++] = offs;

    return KOS_SUCCESS;
}

static int negate_jump_opcode(int opcode)
{
    assert((opcode == INSTR_JUMP_COND) || (opcode == INSTR_JUMP_NOT_COND));

    return (opcode == INSTR_JUMP_COND) ? INSTR_JUMP_NOT_COND : INSTR_JUMP_COND;
}

static int gen_cond_jump_inner(KOS_COMP_UNIT      *program,
                               const KOS_AST_NODE *node,
                               int                 opcode,
                               JUMP_ARRAY         *jump_array,
                               JUMP_ARRAY         *near_jump_array)
{
    int error     = KOS_SUCCESS;
    int jump_offs = -1;
    int is_truthy = 0;
    int is_falsy  = 0;

    while (node->type == NT_OPERATOR) {

        const unsigned op = node->token.op;

        if (op == OT_LOGNOT) {
            node = node->children;
            assert(node);

            opcode = negate_jump_opcode(opcode);
        }
        else if (((op == OT_LOGOR)  && (opcode == INSTR_JUMP_COND)) ||
                 ((op == OT_LOGAND) && (opcode == INSTR_JUMP_NOT_COND))) {

            node = node->children;
            assert(node);
            TRY(gen_cond_jump_inner(program, node, opcode, jump_array, near_jump_array));

            node = node->next;
            assert(node);
            assert( ! node->next);
        }
        else if (((op == OT_LOGAND) && (opcode == INSTR_JUMP_COND)) ||
                 ((op == OT_LOGOR)  && (opcode == INSTR_JUMP_NOT_COND))) {

            node = node->children;
            assert(node);
            TRY(gen_cond_jump_inner(program, node, negate_jump_opcode(opcode), near_jump_array, jump_array));

            node = node->next;
            assert(node);
            assert( ! node->next);
        }
        else
            break;
    }

    if (program->optimize) {
        is_truthy = kos_node_is_truthy(program, node);
        if ( ! is_truthy)
            is_falsy = kos_node_is_falsy(program, node);
    }

    if ((is_truthy && (opcode == INSTR_JUMP_COND)) ||
        (is_falsy  && (opcode == INSTR_JUMP_NOT_COND))) {

        jump_offs = program->cur_offs;
        TRY(gen_instr1(program, INSTR_JUMP, 0));
    }
    else if ((is_truthy && (opcode == INSTR_JUMP_NOT_COND)) ||
             (is_falsy  && (opcode == INSTR_JUMP_COND))) {
        /* Skip generating jump instruction, since it will never jump anyway */
    }
    else {

        KOS_REG *reg = KOS_NULL;

        TRY(visit_node(program, node, &reg));

        jump_offs = program->cur_offs;
        TRY(gen_instr2(program, opcode, 0, reg->reg));

        free_reg(program, reg);
    }

    if (jump_offs >= 0)
        TRY(push_jump_array(program, jump_array, jump_offs));

cleanup:
    return error;
}

static int gen_cond_jump(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         int                 opcode,
                         JUMP_ARRAY         *jump_array)
{
    JUMP_ARRAY near_jump_array;
    int        error;

    init_jump_array(&near_jump_array);

    error = gen_cond_jump_inner(program, node, opcode, jump_array, &near_jump_array);
    if (error)
        return error;

    update_jump_array(program, &near_jump_array, program->cur_offs);

    return KOS_SUCCESS;
}

static int if_stmt(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node)
{
    KOS_REG   *reg   = KOS_NULL;
    JUMP_ARRAY jump_array;
    int        error = KOS_SUCCESS;

    init_jump_array(&jump_array);

    TRY(add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    node = node->children;
    assert(node);

    /* TODO simplify jump if the body has only break or continue */
    TRY(gen_cond_jump(program, node, INSTR_JUMP_NOT_COND, &jump_array));

    node = node->next;
    assert(node);
    TRY(visit_node(program, node, &reg));
    assert(!reg);

    node = node->next;
    if (node) {

        const int jump_offs = program->cur_offs;
        TRY(gen_instr1(program, INSTR_JUMP, 0));

        update_jump_array(program, &jump_array, program->cur_offs);

        reset_jump_array(&jump_array);
        TRY(push_jump_array(program, &jump_array, jump_offs));

        TRY(visit_node(program, node, &reg));
        assert(!reg);

        assert(!node->next);
    }

    update_jump_array(program, &jump_array, program->cur_offs);

cleanup:
    return error;
}

static KOS_SCOPE *find_try_scope(KOS_SCOPE *scope)
{
    while (scope && ! scope->is_function && ! scope->catch_ref.catch_reg)
        scope = scope->parent_scope;

    if (scope && (scope->is_function || ! scope->catch_ref.catch_reg))
        scope = KOS_NULL;

    return scope;
}

static uint8_t get_closure_size(KOS_COMP_UNIT *program)
{
    int        closure_size;
    KOS_SCOPE *scope = &program->cur_frame->scope;

    closure_size = scope->num_indep_vars + scope->num_indep_args;

    assert(closure_size <= KOS_MAX_REGS);
    return (uint8_t)(unsigned)closure_size;
}

static int gen_return(KOS_COMP_UNIT *program,
                      int            reg)
{
    int        error = KOS_SUCCESS;
    KOS_SCOPE *scope = find_try_scope(program->scope_stack);

    while (scope && ! scope->catch_ref.finally_active)
        scope = find_try_scope(scope->parent_scope);

    if (scope) {

        const int return_reg = scope->catch_ref.catch_reg->reg;

        KOS_RETURN_OFFS *const return_offs = (KOS_RETURN_OFFS *)
            KOS_mempool_alloc(&program->allocator, sizeof(KOS_RETURN_OFFS));

        if ( ! return_offs)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

        if (reg != return_reg)
            TRY(gen_instr2(program, INSTR_MOVE, return_reg, reg));

        return_offs->next               = program->cur_frame->return_offs;
        return_offs->offs               = program->cur_offs;
        program->cur_frame->return_offs = return_offs;

        TRY(gen_instr1(program, INSTR_JUMP, 0));
    }
    else
        TRY(gen_instr1(program, INSTR_RETURN, reg));

cleanup:
    return error;
}

static int is_generator(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = &program->cur_frame->scope;

    assert(scope);

    return scope->is_function && ((KOS_FRAME *)scope)->yield_token;
}

static int return_stmt(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node)
{
    int        error;
    KOS_REG   *reg       = KOS_NULL;
    KOS_SCOPE *try_scope = find_try_scope(program->scope_stack);
    int        tail_call = 0;

    if (try_scope)
        reg = try_scope->catch_ref.catch_reg;

    if (node->children) {

        if (node->children->type != NT_VOID_LITERAL && is_generator(program)) {
            program->error_token = &node->token;
            program->error_str   = str_err_return_in_generator;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        if ( ! try_scope && node->children->type == NT_INVOCATION) {
            tail_call = 1;
            TRY(invocation(program, node->children, &reg, INSTR_TAIL_CALL));
            assert( ! reg);
        }
        else {
            TRY(visit_node(program, node->children, &reg));
            assert(reg);
        }
    }
    else {
        TRY(gen_reg(program, &reg));
        TRY(gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    if (tail_call) {
        assert( ! try_scope);
        assert( ! reg);
    }
    else {

        error = gen_return(program, reg->reg);

        if ( ! try_scope || reg != try_scope->catch_ref.catch_reg)
            free_reg(program, reg);
    }

cleanup:
    return error;
}

static int yield(KOS_COMP_UNIT      *program,
                 const KOS_AST_NODE *node,
                 KOS_REG           **reg)
{
    int      error;
    KOS_REG *src = *reg;

    assert(node->children);

    TRY(visit_node(program, node->children, &src));
    assert(src);

    TRY(gen_dest_reg(program, reg, src));

    TRY(gen_instr2(program, INSTR_YIELD, (*reg)->reg, src->reg));

    if (src != *reg)
        free_reg(program, src);

cleanup:
    return error;
}

static int throw_op(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node)
{
    int      error;
    KOS_REG *reg = KOS_NULL;

    assert(node->children);

    TRY(visit_node(program, node->children, &reg));
    assert(reg);

    TRY(gen_instr1(program, INSTR_THROW, reg->reg));

    free_reg(program, reg);

cleanup:
    return error;
}

static int assert_stmt(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node)
{
    JUMP_ARRAY jump_array;
    KOS_REG   *reg     = KOS_NULL;
    int        error;
    int        str_idx;

    assert(node->children);

    init_jump_array(&jump_array);

    TRY(gen_cond_jump(program, node->children, INSTR_JUMP_COND, &jump_array));

    assert(node->children);
    assert(node->children->next);
    assert(node->children->next->type == NT_LANDMARK);
    assert( ! node->children->next->next);

    TRY(gen_assert_str(program, node, &str_idx));

    TRY(gen_reg(program, &reg));

    TRY(gen_load_const(program,
                       node,
                       reg->reg,
                       str_idx));

    TRY(gen_instr1(program, INSTR_THROW, reg->reg));

    update_jump_array(program, &jump_array, program->cur_offs);

    free_reg(program, reg);

cleanup:
    return error;
}

static void finish_break_continue(KOS_COMP_UNIT  *program,
                                  int             continue_tgt_offs,
                                  KOS_BREAK_OFFS *old_break_offs)
{
    KOS_BREAK_OFFS     *break_offs      = program->cur_frame->break_offs;
    const int           break_tgt_offs  = program->cur_offs;
    const KOS_NODE_TYPE unsup_node_type = continue_tgt_offs >= 0 ? NT_FALLTHROUGH : NT_CONTINUE;

    while (break_offs) {
        KOS_BREAK_OFFS *next = break_offs->next;

        assert(break_offs->type == NT_BREAK    ||
               break_offs->type == NT_CONTINUE ||
               break_offs->type == NT_FALLTHROUGH);
        assert(break_offs->type != NT_FALLTHROUGH || continue_tgt_offs >= 0);

        if (break_offs->type == unsup_node_type) {
            break_offs->next = old_break_offs;
            old_break_offs   = break_offs;
        }
        else
            update_jump_offs(program, break_offs->offs,
                    break_offs->type != NT_BREAK
                    ? continue_tgt_offs : break_tgt_offs);

        break_offs = next;
    }

    program->cur_frame->break_offs = old_break_offs;
}

static void finish_fallthrough(KOS_COMP_UNIT *program)
{
    KOS_BREAK_OFFS **remaining_offs       = &program->cur_frame->break_offs;
    KOS_BREAK_OFFS  *break_offs           = *remaining_offs;
    const int        fallthrough_tgt_offs = program->cur_offs;

    *remaining_offs = KOS_NULL;

    while (break_offs) {

        KOS_BREAK_OFFS *next = break_offs->next;

        if (break_offs->type == NT_FALLTHROUGH)
            update_jump_offs(program, break_offs->offs, fallthrough_tgt_offs);
        else {
            break_offs->next = *remaining_offs;
            *remaining_offs  = break_offs;
        }

        break_offs = next;
    }
}

/* Saves last try scope before the loop, used for restoring catch offset */
static KOS_SCOPE *push_try_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *prev_try_scope = program->cur_frame->last_try_scope;
    KOS_SCOPE *scope          = find_try_scope(program->scope_stack);

    if (scope)
        program->cur_frame->last_try_scope = scope;

    return prev_try_scope;
}

static int repeat(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node)
{
    int             error;
    int             test_instr_offs;
    const int       loop_start_offs = program->cur_offs;
    KOS_REG        *reg             = KOS_NULL;
    KOS_BREAK_OFFS *old_break_offs  = program->cur_frame->break_offs;
    KOS_SCOPE      *prev_try_scope  = push_try_scope(program);

    program->cur_frame->break_offs = KOS_NULL;

    node = node->children;
    assert(node);
    TRY(visit_node(program, node, &reg));
    assert(!reg);

    TRY(add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    node = node->next;
    assert(node);

    TRY(add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    test_instr_offs = program->cur_offs;

    if ( ! kos_node_is_falsy(program, node)) {

        JUMP_ARRAY jump_array;

        init_jump_array(&jump_array);

        assert( ! node->next);

        TRY(gen_cond_jump(program, node, INSTR_JUMP_COND, &jump_array));

        update_jump_array(program, &jump_array, loop_start_offs);
    }

    finish_break_continue(program, test_instr_offs, old_break_offs);

    if (reg)
        free_reg(program, reg);

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int while_stmt(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node)
{
    int                 error          = KOS_SUCCESS;
    const KOS_AST_NODE *cond_node      = node->children;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    KOS_SCOPE          *prev_try_scope = push_try_scope(program);

    assert(cond_node);

    if ( ! kos_node_is_falsy(program, cond_node)) {

        KOS_REG   *reg = KOS_NULL;
        JUMP_ARRAY start_jump_array;
        JUMP_ARRAY end_jump_array;
        int        loop_start_offs;
        int        continue_tgt_offs;

        program->cur_frame->break_offs = KOS_NULL;

        init_jump_array(&start_jump_array);
        init_jump_array(&end_jump_array);

        continue_tgt_offs = program->cur_offs;

        TRY(gen_cond_jump(program, cond_node, INSTR_JUMP_NOT_COND, &end_jump_array));

        loop_start_offs = program->cur_offs;

        node = cond_node->next;
        assert(node);
        assert( ! node->next);

        TRY(visit_node(program, node, &reg));
        assert( ! reg);

        TRY(add_addr2line(program, &node->token, KOS_FALSE_VALUE));

        /* TODO simplify jumps if body ends with a break */
        TRY(gen_cond_jump(program, cond_node, INSTR_JUMP_COND, &start_jump_array));

        update_jump_array(program, &start_jump_array, loop_start_offs);
        update_jump_array(program, &end_jump_array, program->cur_offs);
        finish_break_continue(program, continue_tgt_offs, old_break_offs);
    }

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int gen_get_prop_instr(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node,
                              int                 rdest,
                              int                 robj,
                              int                 str_idx)
{
    int error;

    if (str_idx > 255) {
        KOS_REG *tmp   = KOS_NULL;
        int      rprop = rdest;

        if (rprop == robj) {
            TRY(gen_reg(program, &tmp));
            rprop = tmp->reg;
        }

        TRY(gen_load_const(program, node, rprop, str_idx));

        TRY(gen_instr3(program, INSTR_GET, rdest, robj, rprop));

        if (tmp)
            free_reg(program, tmp);
    }
    else
        error = gen_instr3(program, INSTR_GET_PROP8, rdest, robj, str_idx);

cleanup:
    return error;
}

static int gen_set_prop_instr(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node,
                              int                 rdest,
                              int                 str_idx,
                              int                 rsrc)
{
    int error;

    if (str_idx > 255) {
        KOS_REG *tmp = KOS_NULL;

        TRY(gen_reg(program, &tmp));

        TRY(gen_load_const(program, node, tmp->reg, str_idx));

        TRY(gen_instr3(program, INSTR_SET, rdest, tmp->reg, rsrc));

        free_reg(program, tmp);
    }
    else
        error = gen_instr3(program, INSTR_SET_PROP8, rdest, str_idx, rsrc);

cleanup:
    return error;
}

static int is_for_range(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    assert(node->type == NT_FOR_IN);

    node = node->children;
    assert(node);
    assert(node->type == NT_IN);

    node = node->children;
    assert(node);
    assert(node->next);

    node = node->next;
    assert(node);
    if (node->type != NT_INVOCATION)
        return 0;

    node = node->children;
    assert(node);

    if ((node->type == NT_IDENTIFIER) && node->is_var) {

        const KOS_VAR *const var = node->u.var;

        assert( ! node->is_scope);
        assert(var);

        switch (var->type) {

            /* Detect range imported via import.range etc. */
            case VAR_IMPORTED:
                if (var->module_idx != KOS_BASE_MODULE_IDX)
                    return 0;
                break;

            /* Detect global range function inside base module */
            case VAR_GLOBAL:
                if (program->file_id != KOS_BASE_MODULE_IDX)
                    return 0;
                break;

            default:
                return 0;
        }

        if (strncmp(var->token->begin, "range", var->token->length) != 0)
            return 0;
    }
    /* Detect reference via base.range() */
    else if (node->type == NT_REFINEMENT) {

        const KOS_AST_NODE *ref_node = node->children;
        const KOS_VAR      *var;

        assert(ref_node);

        if ((ref_node->type != NT_IDENTIFIER) || ! ref_node->is_var)
            return 0;

        var = ref_node->u.var;
        assert(var);

        if ((var->type != VAR_MODULE) || (var->array_idx != KOS_BASE_MODULE_IDX))
            return 0;

        ref_node = ref_node->next;
        assert(ref_node);

        if ((ref_node->type != NT_IDENTIFIER) && (ref_node->type != NT_STRING_LITERAL))
            return 0;

        if (strncmp(ref_node->token.begin, "range", ref_node->token.length) != 0)
            return 0;
    }
    else
        return 0;

    /* First argument (mandatory) */
    node = node->next;
    if (node) {

        /* Expaned or named args not supported */
        if ((node->type == NT_EXPAND) || (node->type == NT_NAMED_ARGUMENTS))
            return 0;

        /* Second argument (optional) */
        node = node->next;
        if ( ! node)
            return 1;

        /* Expaned or named args not supported */
        if ((node->type == NT_EXPAND) || (node->type == NT_NAMED_ARGUMENTS))
            return 0;

        /* Third argument (optional) */
        node = node->next;
        if ( ! node)
            return 1;

        /* The third argument must be a numeric constant in order to be optimized */
        node = kos_get_const(program, node);
        if (node && (node->type == NT_NUMERIC_LITERAL))
            return 1;
    }

    return 0;
}

static int for_in(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node)
{
    int                 error;
    int                 initial_jump_offs;
    int                 loop_start_offs;
    int                 next_jump_offs;
    const KOS_AST_NODE *var_node;
    const KOS_AST_NODE *expr_node;
    const KOS_AST_NODE *assg_node;
    KOS_REG            *reg            = KOS_NULL;
    KOS_REG            *iter_reg       = KOS_NULL;
    KOS_REG            *item_reg       = KOS_NULL;
    KOS_REG            *item_cont_reg  = KOS_NULL;
    KOS_VAR            *item_var       = KOS_NULL;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    KOS_SCOPE          *prev_try_scope = push_try_scope(program);

    program->cur_frame->break_offs = KOS_NULL;

    push_scope(program, node);

    assg_node = node->children;
    assert(assg_node);
    assert(assg_node->type == NT_IN);

    var_node = assg_node->children;
    assert(var_node);
    assert(var_node->type == NT_VAR   ||
           var_node->type == NT_CONST ||
           var_node->type == NT_LEFT_HAND_SIDE);

    expr_node = var_node->next;
    assert(expr_node);
    assert(!expr_node->next);

    var_node = var_node->children;
    assert(var_node);

    TRY(visit_node(program, expr_node, &iter_reg));
    assert(iter_reg);

    kos_activate_new_vars(program, assg_node->children);

    if (iter_reg->tmp)
        reg = iter_reg;
    else {
        reg      = iter_reg;
        iter_reg = KOS_NULL;
        TRY(gen_reg(program, &iter_reg));
    }

    TRY(add_addr2line(program, &assg_node->token, KOS_FALSE_VALUE));

    TRY(gen_instr2(program, INSTR_LOAD_ITER, iter_reg->reg, reg->reg));

    reg = KOS_NULL;

    if ((var_node->type == NT_IDENTIFIER) || var_node->next) {
        if ( ! var_node->next) {

            TRY(lookup_local_var(program, var_node, &item_reg));

            if ( ! item_reg) {
                TRY(lookup_var(program, var_node, &item_var, &item_cont_reg));

                assert(item_var->type != VAR_LOCAL);
                assert(item_var->type != VAR_ARGUMENT_IN_REG);
                assert(item_var->type != VAR_MODULE);
            }
        }
        else
            TRY(gen_reg(program, &item_reg));
    }

    initial_jump_offs = program->cur_offs;
    TRY(gen_instr1(program, INSTR_JUMP, 0));

    loop_start_offs = program->cur_offs;

    if (var_node->next) {

        const KOS_AST_NODE *cur_var_node = var_node;

        assert(item_reg->tmp);
        assert( ! item_var);
        assert( ! item_cont_reg);

        TRY(gen_instr2(program, INSTR_LOAD_ITER, item_reg->reg, item_reg->reg));

        for ( ; cur_var_node; cur_var_node = cur_var_node->next) {

            KOS_REG *dst_reg = KOS_NULL;

            if (cur_var_node->type == NT_IDENTIFIER) {

                TRY(lookup_local_var(program, cur_var_node, &dst_reg));

                if ( ! dst_reg) {
                    TRY(lookup_var(program, cur_var_node, &item_var, &item_cont_reg));

                    assert(item_var->type != VAR_LOCAL);
                    assert(item_var->type != VAR_ARGUMENT_IN_REG);
                    assert(item_var->type != VAR_MODULE);
                }
            }
            else {
                assert(cur_var_node->type == NT_PLACEHOLDER);
            }

            TRY(gen_reg(program, &dst_reg));

            TRY(gen_instr2(program, INSTR_NEXT, dst_reg->reg, item_reg->reg));

            if (item_var) {
                if (item_var->type == VAR_GLOBAL)
                    TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, dst_reg->reg));
                else
                    TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, dst_reg->reg));
            }

            free_reg(program, dst_reg);

            item_var      = KOS_NULL;
            item_cont_reg = KOS_NULL;
        }
    }
    else if (item_var) {
        assert( ! item_reg);
        TRY(gen_reg(program, &item_reg));

        if (item_var->type == VAR_GLOBAL)
            TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, item_reg->reg));
        else
            TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, item_reg->reg));
    }

    node = assg_node->next;
    assert(node);
    assert(!node->next);

    TRY(visit_node(program, node, &reg));
    assert(!reg);

    TRY(add_addr2line(program, &assg_node->token, KOS_FALSE_VALUE));

    TRY(gen_reg(program, &item_reg));

    next_jump_offs = program->cur_offs;
    TRY(gen_instr3(program, INSTR_NEXT_JUMP, item_reg->reg, iter_reg->reg, 0));

    update_jump_offs(program, initial_jump_offs, next_jump_offs);
    update_jump_offs(program, next_jump_offs, loop_start_offs);
    finish_break_continue(program, next_jump_offs, old_break_offs);

    if (var_node->next) {

        const KOS_AST_NODE *cur_var_node = var_node;

        for ( ; cur_var_node; cur_var_node = cur_var_node->next) {

            KOS_REG *dst_reg = KOS_NULL;

            if (cur_var_node->type != NT_IDENTIFIER)
                continue;

            TRY(lookup_local_var(program, cur_var_node, &dst_reg));

            if (dst_reg) {
                free_reg(program, dst_reg);
                continue;
            }

            assert( ! item_var);
            assert( ! item_cont_reg);
            TRY(lookup_var(program, cur_var_node, &item_var, &item_cont_reg));

            assert(item_var->type != VAR_LOCAL);
            assert(item_var->type != VAR_ARGUMENT_IN_REG);
            assert(item_var->type != VAR_MODULE);

            TRY(gen_reg(program, &dst_reg));

            TRY(gen_instr1(program, INSTR_LOAD_VOID, dst_reg->reg));

            if (item_var->type == VAR_GLOBAL)
                TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, dst_reg->reg));
            else
                TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, dst_reg->reg));

            free_reg(program, dst_reg);

            item_var      = KOS_NULL;
            item_cont_reg = KOS_NULL;
        }
    }
    else if (item_var) {
        assert(item_reg);
        TRY(gen_instr1(program, INSTR_LOAD_VOID, item_reg->reg));

        if (item_var->type == VAR_GLOBAL)
            TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, item_reg->reg));
        else
            TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, item_reg->reg));
    }

    free_reg(program, item_reg);
    item_reg = KOS_NULL;
    free_reg(program, iter_reg);
    iter_reg = KOS_NULL;

    pop_scope(program);

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int for_range(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node)
{
    const KOS_AST_NODE *var_node;
    const KOS_AST_NODE *assg_node;
    KOS_REG            *reg            = KOS_NULL;
    KOS_REG            *item_reg       = KOS_NULL;
    KOS_REG            *iter_reg       = KOS_NULL;
    KOS_REG            *end_reg        = KOS_NULL;
    KOS_REG            *step_reg       = KOS_NULL;
    KOS_REG            *item_cont_reg  = KOS_NULL;
    KOS_VAR            *item_var       = KOS_NULL;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    KOS_SCOPE          *prev_try_scope = push_try_scope(program);
    KOS_NODE_TYPE       lhs_type;
    int                 decrement      = 0;
    int                 initial_jump_offs;
    int                 loop_start_offs;
    int                 next_jump_offs;
    int                 loop_jump_offs;
    int                 error;

    program->cur_frame->break_offs = KOS_NULL;

    push_scope(program, node);

    assg_node = node->children;
    assert(assg_node);
    assert(assg_node->type == NT_IN);

    var_node = assg_node->children;
    assert(var_node);
    lhs_type = (KOS_NODE_TYPE)var_node->type;
    assert(lhs_type == NT_VAR || lhs_type == NT_CONST || lhs_type == NT_LEFT_HAND_SIDE);

    node = var_node->next;
    assert(node);
    assert(!node->next);
    assert(node->type == NT_INVOCATION);
    node = node->children;
    /* First child is range function, skip it */
    assert(node);
    node = node->next;
    assert(node);

    kos_activate_new_vars(program, var_node);

    var_node = var_node->children;
    assert(var_node);
    if (var_node->next) {
        program->error_token = &var_node->next->token;
        program->error_str   = str_err_too_many_vars_for_range;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }
    assert((var_node->type == NT_IDENTIFIER) || (var_node->type == NT_PLACEHOLDER));

    if (var_node->type == NT_IDENTIFIER) {
        assert(var_node->is_var);
        assert(var_node->u.var);

        TRY(lookup_local_var(program, var_node, &item_reg));

        if ( ! item_reg) {
            assert( ! var_node->u.var->is_const);

            TRY(lookup_var(program, var_node, &item_var, &item_cont_reg));

            assert(item_var->type != VAR_LOCAL);
            assert(item_var->type != VAR_ARGUMENT_IN_REG);
            assert(item_var->type != VAR_MODULE);
        }

        if (var_node->u.var->is_const)
            iter_reg = item_reg;
    }

    if ( ! iter_reg)
        TRY(gen_reg(program, &iter_reg));

    TRY(gen_reg(program, &end_reg));

    /* range(end) */
    if ( ! node->next) {
        reg = end_reg;

        TRY(visit_node(program, node, &reg));

        if (reg != end_reg) {
            TRY(gen_instr2(program, INSTR_MOVE, end_reg->reg, reg->reg));
            free_reg(program, reg);
        }
        reg = KOS_NULL;

        TRY(gen_reg(program, &iter_reg));
        TRY(gen_instr2(program, INSTR_LOAD_INT8, iter_reg->reg, 0));

        TRY(gen_reg(program, &step_reg));
        TRY(gen_instr2(program, INSTR_LOAD_INT8, step_reg->reg, 1));
    }
    /* range(begin, end, ...) */
    else {
        reg = iter_reg;

        TRY(visit_node(program, node, &reg));

        if (reg != iter_reg) {
            TRY(gen_instr2(program, INSTR_MOVE, iter_reg->reg, reg->reg));
            free_reg(program, reg);
        }

        reg = end_reg;

        node = node->next;
        TRY(visit_node(program, node, &reg));

        if (reg != end_reg) {
            TRY(gen_instr2(program, INSTR_MOVE, end_reg->reg, reg->reg));
            free_reg(program, reg);
        }
        reg = KOS_NULL;

        node = node->next;
        if (node) {
            TRY(visit_node(program, node, &step_reg));

            node = kos_get_const(program, node);
            assert(node);
            assert(node->type == NT_NUMERIC_LITERAL);

            if (node->token.type == TT_NUMERIC_BINARY) {

                const KOS_NUMERIC *numeric = (const KOS_NUMERIC *)node->token.begin;

                if (numeric->type == KOS_INTEGER_VALUE) {
                    if (numeric->u.i < 0)
                        decrement = 1;
                }
                else {
                    assert(numeric->type == KOS_FLOAT_VALUE);
                    if (numeric->u.d < 0)
                        decrement = 1;
                }
            }
            else {
                assert(node->token.type == TT_NUMERIC);

                if (*node->token.begin == '-')
                    decrement = 1;
            }
        }
        else {
            TRY(gen_reg(program, &step_reg));
            TRY(gen_instr2(program, INSTR_LOAD_INT8, step_reg->reg, 1));
        }
    }

    initial_jump_offs = program->cur_offs;
    TRY(gen_instr1(program, INSTR_JUMP, 0));

    loop_start_offs = program->cur_offs;

    node = assg_node->next;
    assert(node);
    assert(!node->next);

    TRY(visit_node(program, node, &reg));
    assert(!reg);

    TRY(add_addr2line(program, &assg_node->token, KOS_FALSE_VALUE));

    next_jump_offs = program->cur_offs;
    TRY(gen_instr3(program, INSTR_ADD, iter_reg->reg, iter_reg->reg, step_reg->reg));

    update_jump_offs(program, initial_jump_offs, program->cur_offs);

    if ((iter_reg != item_reg) && item_reg)
        TRY(gen_instr2(program, INSTR_MOVE, item_reg->reg, iter_reg->reg));
    else if (item_var) {
        if (item_var->type == VAR_GLOBAL)
            TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, iter_reg->reg));
        else
            TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, iter_reg->reg));
    }

    TRY(gen_reg(program, &reg));
    TRY(gen_instr3(program, INSTR_CMP_LT, reg->reg,
                   decrement ? end_reg->reg : iter_reg->reg,
                   decrement ? iter_reg->reg : end_reg->reg));

    loop_jump_offs = program->cur_offs;
    TRY(gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));

    update_jump_offs(program, loop_jump_offs, loop_start_offs);

    if ((lhs_type == NT_LEFT_HAND_SIDE) && item_reg)
        TRY(gen_instr1(program, INSTR_LOAD_VOID, item_reg->reg));
    else if (item_var) {
        assert(iter_reg);
        assert(iter_reg->tmp);
        TRY(gen_instr1(program, INSTR_LOAD_VOID, iter_reg->reg));
        if (item_var->type == VAR_GLOBAL)
            TRY(gen_instr2(program, INSTR_SET_GLOBAL, item_var->array_idx, iter_reg->reg));
        else
            TRY(gen_instr3(program, INSTR_SET_ELEM, item_cont_reg->reg, item_var->array_idx, iter_reg->reg));
    }

    finish_break_continue(program, next_jump_offs, old_break_offs);

    free_reg(program, reg);
    if (item_reg)
        free_reg(program, item_reg);
    if (iter_reg != item_reg)
        free_reg(program, iter_reg);
    free_reg(program, end_reg);
    free_reg(program, step_reg);

    pop_scope(program);

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int restore_catch(KOS_COMP_UNIT *program,
                         KOS_SCOPE     *outer_scope)
{
    KOS_SCOPE *cur_scope = program->scope_stack;
    int        error;

    assert(cur_scope);
    assert( ! cur_scope->is_function);

    if (outer_scope && outer_scope->catch_ref.catch_reg) {

        KOS_CATCH_REF *cur_catch_ref = &cur_scope->catch_ref;
        int            offs_idx      = cur_catch_ref->num_catch_offs;

        if ((size_t)offs_idx == sizeof(cur_catch_ref->catch_offs) / sizeof(int)) {
            program->error_token = &cur_scope->scope_node->token;
            program->error_str   = str_err_catch_nesting_too_deep;
            return KOS_ERROR_COMPILE_FAILED;
        }

        assert(cur_catch_ref->catch_offs[offs_idx] == 0);

        cur_catch_ref->catch_offs[offs_idx] = program->cur_offs;
        cur_catch_ref->num_catch_offs       = offs_idx + 1;

        if (offs_idx == 0) {
            assert(!cur_catch_ref->next);
            assert(outer_scope->catch_ref.child_scopes != cur_scope);
            cur_catch_ref->next                 = outer_scope->catch_ref.child_scopes;
            outer_scope->catch_ref.child_scopes = cur_scope;
        }

        error = gen_instr2(program, INSTR_CATCH, outer_scope->catch_ref.catch_reg->reg, 0);
    }
    else
        error = gen_instr(program, 0, INSTR_CANCEL);

    return error;
}

static int restore_parent_scope_catch(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope = program->scope_stack;

    assert(scope && ! scope->is_function);

    scope = find_try_scope(scope->parent_scope);

    return restore_catch(program, scope);
}

static int push_break_offs(KOS_COMP_UNIT *program,
                           KOS_NODE_TYPE  type)
{
    int                   error      = KOS_SUCCESS;
    KOS_BREAK_OFFS *const break_offs = (KOS_BREAK_OFFS *)
        KOS_mempool_alloc(&program->allocator, sizeof(KOS_BREAK_OFFS));

    if (break_offs) {
        break_offs->next               = program->cur_frame->break_offs;
        break_offs->type               = type;
        program->cur_frame->break_offs = break_offs;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int break_continue_fallthrough(KOS_COMP_UNIT      *program,
                                      const KOS_AST_NODE *node)
{
    int error;

    TRY(push_break_offs(program, (KOS_NODE_TYPE)node->type));

    if (program->cur_frame->last_try_scope) {

        push_scope(program, node);

        TRY(restore_catch(program, program->cur_frame->last_try_scope));

        pop_scope(program);
    }

    program->cur_frame->break_offs->offs = program->cur_offs;
    TRY(gen_instr1(program, INSTR_JUMP, 0));

cleanup:
    return error;
}

typedef struct KOS_SWITCH_CASE_S {
    int to_jump_offs;
    int final_jump_offs;
} KOS_SWITCH_CASE;

static int count_siblings(const KOS_AST_NODE *node)
{
    int count = 0;

    while (node) {
        ++count;
        node = node->next;
    }

    return count;
}

static int switch_stmt(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node)
{
    int                 error;
    KOS_REG            *value_reg       = KOS_NULL;
    const KOS_AST_NODE *first_case_node;
    int                 num_cases;
    int                 i_case;
    int                 i_default_case  = -1;
    int                 final_jump_offs = -1;
    KOS_SWITCH_CASE    *cases           = KOS_NULL;
    KOS_BREAK_OFFS     *old_break_offs  = program->cur_frame->break_offs;

    program->cur_frame->break_offs = KOS_NULL;

    node = node->children;
    assert(node);

    TRY(visit_node(program, node, &value_reg));
    assert(value_reg);

    node = node->next;

    if ( ! node) {
        free_reg(program, value_reg);
        return error;
    }

    num_cases = count_siblings(node);

    assert(num_cases);

    cases = (KOS_SWITCH_CASE *)KOS_mempool_alloc(
            &program->allocator, sizeof(KOS_SWITCH_CASE) * num_cases);

    if ( ! cases)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    if (node->type == NT_DEFAULT && num_cases == 1) {
        node = node->children;
        assert(node->type == NT_EMPTY);

        node = node->next;
        assert( ! node->next || (node->next->type == NT_FALLTHROUGH && ! node->next->next));

        free_reg(program, value_reg);
        value_reg = KOS_NULL;

        if (node->type != NT_FALLTHROUGH) {
            TRY(visit_node(program, node, &value_reg));
            assert( ! value_reg);
        }

        return error;
    }

    first_case_node = node;

    for (i_case = 0; node; node = node->next, ++i_case) {

        if (node->type == NT_CASE) {

            KOS_REG            *case_reg   = KOS_NULL;
            KOS_REG            *result_reg = KOS_NULL;
            const KOS_AST_NODE *case_node;

            assert(node->children);
            assert(node->children->type != NT_EMPTY);

            case_node = kos_get_const(program, node->children);

            if ( ! case_node)
                case_node = node->children;

            switch (case_node->type) {

                case NT_IDENTIFIER:
                    /* fall-through */
                case NT_NUMERIC_LITERAL:
                    /* fall-through */
                case NT_STRING_LITERAL:
                    /* fall-through */
                case NT_THIS_LITERAL:
                    /* fall-through */
                case NT_BOOL_LITERAL:
                    /* fall-through */
                case NT_VOID_LITERAL:
                    /* TODO ensure unique */
                    break;

                default:
                    break;
            }

            TRY(visit_node(program, node->children, &case_reg));
            assert(case_reg);

            if (case_reg->tmp)
                result_reg = case_reg;
            else
                TRY(gen_reg(program, &result_reg));

            TRY(gen_instr3(program, INSTR_CMP_EQ, result_reg->reg, value_reg->reg, case_reg->reg));

            cases[i_case].to_jump_offs = program->cur_offs;

            TRY(gen_instr2(program, INSTR_JUMP_COND, 0, result_reg->reg));

            free_reg(program, case_reg);
            if (case_reg != result_reg)
                free_reg(program, result_reg);
        }
        else {

            assert(node->type == NT_DEFAULT);
            assert(node->children);
            assert(node->children->type == NT_EMPTY);

            i_default_case = i_case;
            cases[i_case].to_jump_offs = -1;
        }
    }

    free_reg(program, value_reg);
    value_reg = KOS_NULL;

    if (i_default_case >= 0)
        cases[i_default_case].to_jump_offs = program->cur_offs;
    else
        final_jump_offs = program->cur_offs;

    TRY(gen_instr1(program, INSTR_JUMP, 0));

    node = first_case_node;

    for (i_case = 0; node; node = node->next, ++i_case) {

        const KOS_AST_NODE *child_node = node->children;

        assert(child_node->next);

        child_node = child_node->next;

        assert(cases[i_case].to_jump_offs > 0);

        update_jump_offs(program, cases[i_case].to_jump_offs, program->cur_offs);

        if (i_case)
            finish_fallthrough(program);

        cases[i_case].final_jump_offs = -1;

        if (child_node->type != NT_FALLTHROUGH) {

            TRY(visit_node(program, child_node, &value_reg));
            assert( ! value_reg);

            if ( ! child_node->next) {
                cases[i_case].final_jump_offs = program->cur_offs;

                TRY(gen_instr1(program, INSTR_JUMP, 0));
            }
            else {
                assert(child_node->next->type == NT_FALLTHROUGH ||
                       child_node->next->type == NT_EMPTY);
                assert( ! child_node->next->next);
            }
        }
        else {
            assert( ! child_node->next);
        }
    }

    if (final_jump_offs >= 0)
        update_jump_offs(program, final_jump_offs, program->cur_offs);

    for (i_case = 0; i_case < num_cases; ++i_case) {

        const int offs = cases[i_case].final_jump_offs;

        if (offs >= 0)
            update_jump_offs(program, offs, program->cur_offs);
    }

    finish_break_continue(program, -1, old_break_offs);

cleanup:
    return error;
}

static void update_child_scope_catch(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope     = program->scope_stack;
    const int  dest_offs = program->cur_offs;

    scope = scope->catch_ref.child_scopes;

    for ( ; scope; scope = scope->catch_ref.next) {

        const int num_catch_offs = scope->catch_ref.num_catch_offs;
        int       i;

        for (i = 0; i < num_catch_offs; i++) {
            const int instr_offs = scope->catch_ref.catch_offs[i];
            if (instr_offs)
                update_jump_offs(program, instr_offs, dest_offs);
        }
    }

    program->scope_stack->catch_ref.child_scopes = KOS_NULL;
}

static int try_stmt(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node)
{
    int                 error;
    int                 catch_offs;
    KOS_REG            *except_reg     = KOS_NULL;
    KOS_VAR            *except_var     = KOS_NULL;
    KOS_RETURN_OFFS    *return_offs    = program->cur_frame->return_offs;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    const KOS_NODE_TYPE node_type      = (KOS_NODE_TYPE)node->type;
    const KOS_AST_NODE *try_node       = node->children;
    const KOS_AST_NODE *catch_node     = KOS_NULL;
    const KOS_AST_NODE *defer_node     = KOS_NULL;
    KOS_SCOPE          *scope;

    scope = push_scope(program, node);

    program->cur_frame->break_offs = KOS_NULL;

    assert(try_node);
    assert(try_node->next);
    assert(!try_node->next->next);

    if (node_type == NT_TRY_CATCH) {

        KOS_AST_NODE *variable;

        catch_node = try_node->next;

        assert(catch_node->type == NT_CATCH);

        node = catch_node->children;
        assert(node);
        assert(node->type == NT_VAR || node->type == NT_CONST);

        variable = node->children;
        assert(variable);
        assert(variable->type == NT_IDENTIFIER);
        assert(!variable->children);
        assert(!variable->next);

        assert( ! variable->is_scope);
        assert(variable->is_var);
        except_var = variable->u.var;
        assert(except_var);
        assert(except_var == kos_find_var(program->scope_stack->vars, &variable->token));

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(lookup_local_var(program, variable, &except_reg));
        assert(except_reg);

        except_var->is_active = VAR_INACTIVE;

        scope->catch_ref.catch_reg = except_reg;
    }
    else {

        defer_node = try_node->next;

        assert(node_type == NT_TRY_DEFER);
        assert(defer_node->type == NT_SCOPE);

        TRY(gen_reg(program, &except_reg));

        scope->catch_ref.catch_reg = except_reg;

        scope->catch_ref.finally_active = 1;
        program->cur_frame->return_offs = KOS_NULL;

        TRY(gen_instr1(program, INSTR_LOAD_VOID, except_reg->reg));
    }

    /* Try section */

    catch_offs = program->cur_offs;
    TRY(gen_instr2(program, INSTR_CATCH, except_reg->reg, 0));

    assert(try_node->type == NT_SCOPE);
    TRY(process_scope(program, try_node));

    /* We're done with the try scope, prevent find_try_scope() from finding this
     * catch target again when inside catch or defer clause. */
    scope->catch_ref.catch_reg = KOS_NULL;

    /* Catch section */

    if (node_type == NT_TRY_CATCH) {

        int jump_end_offs;

        TRY(restore_parent_scope_catch(program));

        jump_end_offs = program->cur_offs;
        TRY(gen_instr1(program, INSTR_JUMP, 0));

        update_child_scope_catch(program);

        update_jump_offs(program, catch_offs, program->cur_offs);

        TRY(restore_parent_scope_catch(program));

        node = node->next;
        assert(node);
        assert(!node->next);
        assert(node->type == NT_SCOPE);

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(process_scope(program, node));

        except_var->is_active = VAR_INACTIVE;

        update_jump_offs(program, jump_end_offs, program->cur_offs);
    }

    /* Defer section */

    else {

        int             skip_throw_offs;
        KOS_BREAK_OFFS *try_break_offs = program->cur_frame->break_offs;

        program->cur_frame->break_offs = old_break_offs;
        old_break_offs                 = KOS_NULL;

        {
            KOS_RETURN_OFFS *tmp            = program->cur_frame->return_offs;
            program->cur_frame->return_offs = return_offs;
            return_offs                     = tmp;
            scope->catch_ref.finally_active = 0;
        }

        update_child_scope_catch(program);

        update_jump_offs(program, catch_offs, program->cur_offs);

        TRY(restore_parent_scope_catch(program));

        TRY(process_scope(program, defer_node));

        skip_throw_offs = program->cur_offs;

        TRY(gen_instr2(program, INSTR_JUMP_NOT_COND, 0, except_reg->reg));

        TRY(gen_instr1(program, INSTR_THROW, except_reg->reg));

        /* Defer section for break, continue and fallthrough */

        if (try_break_offs) {

            KOS_BREAK_OFFS *break_offs    = try_break_offs;
            int             jump_offs[3]  = { 0, 0, 0 };
            KOS_NODE_TYPE   node_types[3] = { NT_BREAK, NT_CONTINUE, NT_FALLTHROUGH };
            int             i;

            while (break_offs) {

                assert(break_offs->type == NT_CONTINUE ||
                       break_offs->type == NT_BREAK    ||
                       break_offs->type == NT_FALLTHROUGH);

                for (i = 0; i < (int)(sizeof(jump_offs) / sizeof(jump_offs[0])); i++)
                    if (break_offs->type == node_types[i]) {
                        jump_offs[i] = 1;
                        break;
                    }

                break_offs = break_offs->next;
            }

            for (i = 0; i < (int)(sizeof(jump_offs) / sizeof(jump_offs[0])); i++) {

                const KOS_NODE_TYPE type = node_types[i];

                if ( ! jump_offs[i])
                    continue;

                for (break_offs = try_break_offs; break_offs; break_offs = break_offs->next)
                    if (break_offs->type == type)
                        update_jump_offs(program, break_offs->offs, program->cur_offs);

                TRY(restore_parent_scope_catch(program));

                TRY(process_scope(program, defer_node));

                TRY(push_break_offs(program, type));
                program->cur_frame->break_offs->offs = program->cur_offs;

                TRY(gen_instr1(program, INSTR_JUMP, 0));
            }
        }

        /* Defer section for return statement */

        if (return_offs) {

            for ( ; return_offs; return_offs = return_offs->next)
                update_jump_offs(program, return_offs->offs, program->cur_offs);

            TRY(restore_parent_scope_catch(program));

            TRY(process_scope(program, defer_node));

            TRY(gen_return(program, except_reg->reg));
        }

        update_jump_offs(program, skip_throw_offs, program->cur_offs);
    }

    if (old_break_offs) {
        if (program->cur_frame->break_offs) {

            KOS_BREAK_OFFS **break_offs = &program->cur_frame->break_offs;

            while (*break_offs)
                break_offs = &(*break_offs)->next;

            *break_offs = old_break_offs;
        }
        else
            program->cur_frame->break_offs = old_break_offs;
    }

    free_reg(program, except_reg);

    pop_scope(program);

cleanup:
    return error;
}

static KOS_REG *super_prototype(KOS_COMP_UNIT      *program,
                                const KOS_AST_NODE *node)
{
    assert(program->cur_frame->base_proto_reg);
    assert(program->cur_frame->uses_base_proto);

    return program->cur_frame->base_proto_reg;
}

static int get_global_idx(const char *global_name,
                          uint16_t    global_length,
                          int         module_idx,
                          int         global_idx,
                          void       *cookie)
{
    *(int *)cookie = global_idx;
    return KOS_SUCCESS;
}

static int refinement_module(KOS_COMP_UNIT      *program,
                             KOS_VAR            *module_var,
                             const KOS_AST_NODE *node, /* the second child of the refinement node */
                             KOS_REG           **reg)
{
    int        error;
    KOS_VECTOR cstr;

    KOS_vector_init(&cstr);

    if (node->type == NT_STRING_LITERAL) {

        int             global_idx;
        const char     *begin;
        uint16_t        length;
        KOS_UTF8_ESCAPE escape;

        /* TODO this does not work for escaped strings, kos_comp_resolve_global assumes NO_ESCAPE */
        get_token_str(&node->token, &begin, &length, &escape);

        error = kos_comp_resolve_global(program->ctx,
                                        module_var->array_idx,
                                        begin,
                                        length,
                                        get_global_idx,
                                        &global_idx);
        if (error && (error != KOS_ERROR_OUT_OF_MEMORY)) {
            program->error_token = &node->token;
            program->error_str   = str_err_no_such_module_variable;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(gen_reg(program, reg));

        TRY(gen_instr3(program, INSTR_GET_MOD_ELEM, (*reg)->reg, module_var->array_idx, global_idx));
    }
    else {

        KOS_REG *prop = KOS_NULL;

        TRY(visit_node(program, node, &prop));
        assert(prop);

        TRY(gen_dest_reg(program, reg, prop));

        TRY(gen_instr3(program, INSTR_GET_MOD_GLOBAL, (*reg)->reg, module_var->array_idx, prop->reg));

        if (*reg != prop)
            free_reg(program, prop);
    }

cleanup:
    KOS_vector_destroy(&cstr);

    return error;
}

static int maybe_int(const KOS_AST_NODE *node,
                     int64_t            *value)
{
    KOS_NUMERIC numeric;

    if (node->type != NT_NUMERIC_LITERAL)
        return 0;

    if (node->token.type == TT_NUMERIC_BINARY) {

        const KOS_NUMERIC *ptr = (const KOS_NUMERIC *)node->token.begin;

        assert(node->token.length == sizeof(KOS_NUMERIC));

        numeric = *ptr;
    }
    else {

        if (KOS_SUCCESS != kos_parse_numeric(node->token.begin,
                                             node->token.begin + node->token.length,
                                             &numeric))
            return 0;
    }

    if (numeric.type == KOS_INTEGER_VALUE)
        *value = numeric.u.i;
    else {
        assert(numeric.type == KOS_FLOAT_VALUE);
        *value = (int64_t)floor(numeric.u.d);
    }

    return 1;
}

static int refinement_object(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node,
                             KOS_REG           **reg, /* the first child of the refinement node */
                             KOS_REG           **out_obj)
{
    int      error;
    KOS_REG *obj = KOS_NULL;
    int64_t  idx;

    if (node->type == NT_SUPER_PROTO_LITERAL)
        obj = super_prototype(program, node);
    else
        TRY(visit_node(program, node, &obj));
    assert(obj);

    /* Object is requested only for invocation */
    if (out_obj) {
        /* When invoking a function on super, use this as object */
        if (node->type == NT_SUPER_PROTO_LITERAL) {
            assert(program->cur_frame->scope.uses_this);
            assert(program->cur_frame->this_reg);

            *out_obj = program->cur_frame->this_reg;
        }
        else
            *out_obj = obj;

        TRY(gen_reg(program, reg));
    }
    else
        TRY(gen_dest_reg(program, reg, obj));

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {

        int str_idx;
        TRY(gen_str(program, &node->token, &str_idx));

        TRY(gen_get_prop_instr(program, node, (*reg)->reg, obj->reg, str_idx));
    }
    else if (maybe_int(node, &idx)) {

        if (idx > INT_MAX || idx < INT_MIN) {
            program->error_token = &node->token;
            program->error_str   = str_err_invalid_index;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(gen_instr3(program, INSTR_GET_ELEM, (*reg)->reg, obj->reg, (int)idx));
    }
    else {

        KOS_REG *prop = KOS_NULL;

        TRY(visit_node(program, node, &prop));
        assert(prop);

        TRY(gen_instr3(program, INSTR_GET, (*reg)->reg, obj->reg, prop->reg));

        free_reg(program, prop);
    }

    if (obj != *reg && ! out_obj)
        free_reg(program, obj);

cleanup:
    return error;
}

static int refinement(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg,
                      KOS_REG           **out_obj)
{
    int      error;
    KOS_VAR *module_var = KOS_NULL;

    node = node->children;
    assert(node);

    if ((node->type == NT_IDENTIFIER) && node->is_var) {
        module_var = node->u.var;
        if (module_var->type != VAR_MODULE)
            module_var = KOS_NULL;
    }

    if (module_var) {
        node = node->next;
        assert(node);
        assert(!node->next);

        error = refinement_module(program, module_var, node, reg);
    }
    else
        error = refinement_object(program, node, reg, out_obj);

    return error;
}

static int maybe_refinement(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg,
                            KOS_REG           **out_obj)
{
    int error;

    assert(out_obj);

    if (node->type == NT_REFINEMENT)
        error = refinement(program, node, reg, out_obj);
    else {
        error    = visit_node(program, node, reg);
        *out_obj = KOS_NULL;
    }

    assert(error || *reg);

    return error;
}

static int slice(KOS_COMP_UNIT      *program,
                 const KOS_AST_NODE *node,
                 KOS_REG           **reg)
{
    int      error;
    KOS_REG *obj_reg   = KOS_NULL;
    KOS_REG *begin_reg = KOS_NULL;
    KOS_REG *end_reg   = KOS_NULL;

    node = node->children;
    assert(node);

    TRY(visit_node(program, node, &obj_reg));
    assert(obj_reg);

    node = node->next;
    assert(node);

    TRY(visit_node(program, node, &begin_reg));
    assert(begin_reg);

    node = node->next;
    assert(node);
    assert( ! node->next);

    TRY(visit_node(program, node, &end_reg));
    assert(end_reg);

    if ( ! *reg) {
        if (obj_reg->tmp)
            *reg = obj_reg;
        else
            TRY(gen_reg(program, reg));
    }

    TRY(gen_instr4(program, INSTR_GET_RANGE, (*reg)->reg, obj_reg->reg,
                                              begin_reg->reg, end_reg->reg));

    free_reg(program, end_reg);
    free_reg(program, begin_reg);
    if (obj_reg != *reg)
        free_reg(program, obj_reg);

cleanup:
    return error;
}

static int find_var_by_reg(KOS_RED_BLACK_NODE *node,
                           void               *cookie)
{
    KOS_VAR *var = (KOS_VAR *)node;
    KOS_REG *reg = (KOS_REG *)cookie;

    /* Handle local variables, arguments in registers and ellipsis. */
    /* Ignore rest arguments, which are not stored in registers.    */
    if (var->reg == reg && ! (var->type & VAR_ARGUMENT)) {

        /* Technically this is not an error, but it will stop tree iteration */
        return KOS_SUCCESS_RETURN;
    }

    return KOS_SUCCESS;
}

static int is_var_used(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG            *reg)
{
    if ( ! reg || reg->tmp)
        return 0;

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_SCOPE *scope;

            for (scope = program->scope_stack; scope && scope->parent_scope; scope = scope->parent_scope) {

                const int error = kos_red_black_walk(scope->vars, find_var_by_reg, reg);

                if (error == KOS_SUCCESS_RETURN)
                    return 1;

                if (scope->is_function)
                    break;
            }
        }

        if (is_var_used(program, node->children, reg))
            return 1;
    }

    return 0;
}

static int count_non_expanded_siblings(const KOS_AST_NODE *node)
{
    int count = 0;

    while (node && node->type != NT_EXPAND) {
        ++count;
        node = node->next;
    }

    return count;
}

#define MAX_CONTIG_REGS 4

static int count_contig_arg_siblings(const KOS_AST_NODE *node)
{
    int count = 0;

    while (node) {

        if (node->type == NT_EXPAND)
            return MAX_CONTIG_REGS + 1;

        ++count;
        node = node->next;
    }

    return count;
}

static int gen_load_number(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           int32_t             reg,
                           const KOS_NUMERIC  *numeric)
{
    KOS_COMP_CONST *constant;

    if (numeric->type == KOS_INTEGER_VALUE && (uint64_t)((numeric->u.i >> 7) + 1) <= 1U)
        return gen_instr2(program, INSTR_LOAD_INT8, reg, (int32_t)numeric->u.i);

    constant = (KOS_COMP_CONST *)kos_red_black_find(program->constants,
                                                    &numeric,
                                                    numbers_compare_item);

    if ( ! constant) {
        constant = (KOS_COMP_CONST *)
            KOS_mempool_alloc(&program->allocator,
                              KOS_max(sizeof(KOS_COMP_INTEGER),
                                      sizeof(KOS_COMP_FLOAT)));

        if ( ! constant)
            return KOS_ERROR_OUT_OF_MEMORY;

        if (numeric->type == KOS_INTEGER_VALUE) {
            constant->type = KOS_COMP_CONST_INTEGER;
            ((KOS_COMP_INTEGER *)constant)->value = numeric->u.i;
        }
        else {
            constant->type = KOS_COMP_CONST_FLOAT;
            ((KOS_COMP_FLOAT *)constant)->value = numeric->u.d;
        }

        add_constant(program, constant);
    }

    return gen_load_const(program,
                          node,
                          reg,
                          (int32_t)constant->index);
}

static int gen_load_array(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          int32_t             operand1,
                          int32_t             operand2)
{
    static const char str_resize[] = "resize";

    KOS_NUMERIC numeric;
    KOS_TOKEN   token;
    KOS_REG    *resize_fun = KOS_NULL;
    KOS_REG    *const_size = KOS_NULL;
    int         str_idx    = 0;
    int         error      = KOS_SUCCESS;

    if (operand2 < 256)
        return gen_instr2(program, INSTR_LOAD_ARRAY, operand1, operand2);

    TRY(gen_instr2(program, INSTR_LOAD_ARRAY, operand1, 0));

    memset(&token, 0, sizeof(token));
    token.begin  = str_resize;
    token.length = sizeof(str_resize) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(gen_str(program, &token, &str_idx));

    TRY(gen_reg(program, &resize_fun));
    TRY(gen_reg(program, &const_size));

    TRY(gen_get_prop_instr(program, node, resize_fun->reg, operand1, str_idx));

    numeric.type = KOS_INTEGER_VALUE;
    numeric.u.i  = operand2;
    TRY(gen_load_number(program, node, const_size->reg, &numeric));

    TRY(gen_instr5(program, INSTR_CALL_N, resize_fun->reg, resize_fun->reg, operand1, const_size->reg, 1));

    free_reg(program, const_size);
    free_reg(program, resize_fun);

cleanup:
    return error;
}

static int gen_array(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node,
                     KOS_REG           **reg)
{
    int       error;
    int       i;
    const int num_fixed = count_non_expanded_siblings(node);

    if (is_var_used(program, node, *reg))
        *reg = KOS_NULL;

    TRY(gen_reg(program, reg));
    TRY(gen_load_array(program, node, (*reg)->reg, num_fixed));

    for (i = 0; node; node = node->next, ++i) {

        KOS_REG  *arg    = KOS_NULL;
        const int expand = node->type == NT_EXPAND ? 1 : 0;

        if (expand) {
            assert(node->children);
            assert( ! node->children->next);
            assert(node->children->type != NT_EXPAND);
            assert(i >= num_fixed);
            TRY(visit_node(program, node->children, &arg));
        }
        else
            TRY(visit_node(program, node, &arg));

        assert(arg);

        if (i < num_fixed)
            TRY(gen_instr3(program, INSTR_SET_ELEM, (*reg)->reg, i, arg->reg));
        else if (expand)
            TRY(gen_instr2(program, INSTR_PUSH_EX, (*reg)->reg, arg->reg));
        else
            TRY(gen_instr2(program, INSTR_PUSH, (*reg)->reg, arg->reg));

        free_reg(program, arg);
    }

cleanup:
    return error;
}

static int gen_named_args(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg)
{
    assert(node);
    assert(node->type == NT_NAMED_ARGUMENTS);

    node = node->children;
    assert(node);
    assert(node->type == NT_OBJECT_LITERAL);
    assert( ! node->next);

    /* Special message inside object_literal() for named arguments */
    return object_literal(program, node, reg, KOS_NULL, KOS_NULL);
}

static int gen_args_array(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg)
{
    if (node && (node->type == NT_NAMED_ARGUMENTS))
        return gen_named_args(program, node, reg);
    else
        return gen_array(program, node, reg);
}

static int super_invocation(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    static const char   str_apply[]   = "apply";
    int                 error         = KOS_SUCCESS;
    int                 str_idx       = 0;
    KOS_TOKEN           token;
    KOS_REG            *args_regs[2]  = { KOS_NULL, KOS_NULL };
    KOS_REG            *apply_fun     = KOS_NULL;
    KOS_REG            *base_ctor_reg;
    const KOS_AST_NODE *inv_node      = node;

    TRY(gen_reg_range(program, &args_regs[0], 2));

    if (node) {
        node = node->children;

        assert(node);
        assert(node->type == NT_SUPER_CTOR_LITERAL);
        node = node->next;

        TRY(gen_args_array(program, node, &args_regs[1]));
    }
    else
        TRY(gen_instr2(program, INSTR_LOAD_ARRAY, args_regs[1]->reg, 0));

    memset(&token, 0, sizeof(token));
    token.begin  = str_apply;
    token.length = sizeof(str_apply) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(gen_str(program, &token, &str_idx));

    if ( ! program->cur_frame->base_ctor_reg) {
        assert(inv_node);
        program->error_token = &inv_node->children->token;
        program->error_str   = str_err_cannot_invoke_void_ctor;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }

    base_ctor_reg = program->cur_frame->base_ctor_reg;

    if (reg && is_var_used(program, inv_node, *reg)) {
        TRY(gen_reg(program, &apply_fun));
        assert(apply_fun);
        assert(*reg);
    }
    else {
        if ( ! reg || ! *reg) {
            TRY(gen_reg(program, &apply_fun));
            if (reg)
                *reg = apply_fun;
        }
        else
            apply_fun = *reg;
    }

    TRY(gen_get_prop_instr(program, inv_node, apply_fun->reg, base_ctor_reg->reg, str_idx));

    assert(program->cur_frame->this_reg);
    TRY(gen_instr2(program, INSTR_MOVE, args_regs[0]->reg, program->cur_frame->this_reg->reg));

    /* Use apply() to prevent the base class' constructor from creating
     * a new 'this' object and force it to use the current 'this' object
     * being created by the derived class' constructor.
     */
    TRY(gen_instr5(program, INSTR_CALL_N, apply_fun->reg, apply_fun->reg, base_ctor_reg->reg, args_regs[0]->reg, 2));

cleanup:
    if (args_regs[0])
        free_reg(program, args_regs[0]);
    if (args_regs[1])
        free_reg(program, args_regs[1]);
    if (apply_fun && ( ! reg || apply_fun != *reg))
        free_reg(program, apply_fun);

    return error;
}

static int invocation(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg,
                      KOS_BYTECODE_INSTR  instr)
{
    int      error;
    KOS_REG *args;
    KOS_REG *obj        = KOS_NULL;
    KOS_REG *fun        = KOS_NULL;
    int      interp_str = node->type == NT_INTERPOLATED_STRING;
    int      num_contig_args;

    assert(node->children);

    if (node->children->type == NT_SUPER_CTOR_LITERAL) {
        assert(instr == INSTR_CALL);
        assert(program->cur_frame->this_reg);
        return super_invocation(program, node, reg);
    }

    args = is_var_used(program, node, *reg) ? KOS_NULL : *reg;

    node = node->children;

    if (interp_str) {

        static const char str_string[] = "stringify";
        int               string_idx   = 0;

        error = kos_comp_resolve_global(program->ctx,
                                        0,
                                        str_string,
                                        sizeof(str_string)-1,
                                        get_global_idx,
                                        &string_idx);
        if (error && (error != KOS_ERROR_OUT_OF_MEMORY)) {
            program->error_token = &node->token;
            program->error_str   = str_err_no_such_module_variable;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(gen_reg(program, &fun));

        TRY(gen_instr3(program, INSTR_GET_MOD_ELEM, fun->reg, 0, string_idx));
    }
    else {

        TRY(maybe_refinement(program, node, &fun, &obj));

        node = node->next;
    }

    if (node && (node->type == NT_NAMED_ARGUMENTS))
        num_contig_args = MAX_CONTIG_REGS + 1;
    else
        num_contig_args = count_contig_arg_siblings(node);

    if (num_contig_args <= MAX_CONTIG_REGS) {

        KOS_REG *argn[MAX_CONTIG_REGS] = { KOS_NULL, KOS_NULL, KOS_NULL, KOS_NULL };
        int      i;

        if (num_contig_args > 1)
            TRY(gen_reg_range(program, &argn[0], num_contig_args));

        for (i = 0; node; node = node->next, i++) {

            KOS_REG *arg = argn[i];

            assert(i == 0 || arg);
            assert(i == 0 || arg->reg == argn[i-1]->reg + 1);

            TRY(visit_node(program, node, &arg));
            assert(arg);

            if ( ! argn[i]) {
                assert(num_contig_args == 1);
                argn[i] = arg;
            }
            else if (arg != argn[i]) {
                assert( ! arg->tmp);
                TRY(gen_instr2(program, INSTR_MOVE, argn[i]->reg, arg->reg));
            }
        }

        /* TODO ignore moves if all args are existing, contiguous registers */

        if (instr == INSTR_CALL) {
            int32_t rdest;

            if ( ! *reg) {
                for (i = 0; i < num_contig_args; i++) {
                    if (argn[i]->tmp) {
                        *reg = argn[i];
                        break;
                    }
                }
            }
            if ( ! *reg)
                TRY(gen_reg(program, reg));

            rdest = (*reg)->reg;

            if (obj) {
                TRY(gen_instr5(program, INSTR_CALL_N, rdest, fun->reg, obj->reg,
                               num_contig_args ? (unsigned)argn[0]->reg : KOS_NO_REG,
                               num_contig_args));
            }
            else {
                TRY(gen_instr4(program, INSTR_CALL_FUN, rdest, fun->reg,
                               num_contig_args ? (unsigned)argn[0]->reg : KOS_NO_REG,
                               num_contig_args));
            }
        }
        else {
            if (obj) {
                TRY(gen_instr4(program, INSTR_TAIL_CALL_N, fun->reg, obj->reg,
                               num_contig_args ? (unsigned)argn[0]->reg : KOS_NO_REG,
                               num_contig_args));
            }
            else {
                TRY(gen_instr3(program, INSTR_TAIL_CALL_FUN, fun->reg,
                               num_contig_args ? (unsigned)argn[0]->reg : KOS_NO_REG,
                               num_contig_args));
            }
        }

        while (num_contig_args) {
            KOS_REG *arg = argn[--num_contig_args];
            if (arg != *reg)
                free_reg(program, arg);
        }
    }
    else {

        TRY(gen_args_array(program, node, &args));

        if ( ! *reg && instr == INSTR_CALL)
            *reg = args;

        if ( ! obj) {
            TRY(gen_reg(program, &obj));
            TRY(gen_instr1(program, INSTR_LOAD_VOID, obj->reg));
        }

        if (instr == INSTR_CALL)
            TRY(gen_instr4(program, instr, (*reg)->reg, fun->reg, obj->reg, args->reg));
        else {
            assert(instr == INSTR_TAIL_CALL);
            TRY(gen_instr3(program, instr, fun->reg, obj->reg, args->reg));
        }

        if (args != *reg)
            free_reg(program, args);
    }

    free_reg(program, fun);
    if (obj)
        free_reg(program, obj);

cleanup:
    return error;
}

static int async_op(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node,
                    KOS_REG           **reg)
{
    int               error       = KOS_SUCCESS;
    KOS_REG          *argn[2]     = { KOS_NULL, KOS_NULL };
    KOS_REG          *fun         = KOS_NULL;
    KOS_REG          *async       = KOS_NULL;
    KOS_REG          *obj;
    int               str_idx;
    KOS_TOKEN         token;
    static const char str_async[] = "async";

    node = node->children;
    assert(node);
    assert( ! node->next);
    assert(node->type == NT_INVOCATION);

    TRY(gen_reg_range(program, &argn[0], 2));

    obj = argn[0];

    if (*reg)
        fun = *reg;

    node = node->children;
    assert(node);

    TRY(maybe_refinement(program, node, &fun, &obj));

    node = node->next;

    TRY(gen_args_array(program, node, &argn[1]));

    memset(&token, 0, sizeof(token));
    token.begin  = str_async;
    token.length = sizeof(str_async) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(gen_str(program, &token, &str_idx));

    if (*reg && fun != *reg)
        async = *reg;

    if (obj && obj != argn[0]) {
        TRY(gen_instr2(program, INSTR_MOVE, argn[0]->reg, obj->reg));
        free_reg(program, obj);
    }

    TRY(gen_reg(program, &async));

    if ( ! *reg)
        *reg = async;

    TRY(gen_get_prop_instr(program, node, async->reg, fun->reg, str_idx));

    if ( ! obj)
        TRY(gen_instr1(program, INSTR_LOAD_VOID, argn[0]->reg));

    TRY(gen_instr5(program, INSTR_CALL_N, (*reg)->reg, async->reg, fun->reg, argn[0]->reg, 2));

    if (fun != *reg)
        free_reg(program, fun);
    if (async != *reg)
        free_reg(program, async);
    free_reg(program, argn[0]);
    free_reg(program, argn[1]);

cleanup:
    return error;
}

enum CHECK_TYPE_E {
    CHECK_NUMERIC           = 1,
    CHECK_STRING            = 2,
    CHECK_NUMERIC_OR_STRING = 3
};

static int check_const_literal(KOS_COMP_UNIT      *program,
                               const KOS_AST_NODE *node,
                               enum CHECK_TYPE_E   expected_type)
{
    KOS_NODE_TYPE       cur_node_type;
    const KOS_AST_NODE *const_node = kos_get_const(program, node);

    if ( ! const_node)
        return KOS_SUCCESS;

    cur_node_type = (KOS_NODE_TYPE)const_node->type;

    if ((expected_type & CHECK_NUMERIC) && (cur_node_type == NT_NUMERIC_LITERAL))
        return KOS_SUCCESS;

    if ((expected_type & CHECK_STRING) && (cur_node_type == NT_STRING_LITERAL))
        return KOS_SUCCESS;

    switch (cur_node_type) {

        case NT_NUMERIC_LITERAL:
            /* fall through */
        case NT_STRING_LITERAL:
            /* fall through */
        case NT_BOOL_LITERAL:
            /* fall through */
        case NT_VOID_LITERAL:
            /* fall through */
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            /* fall through */
        case NT_ARRAY_LITERAL:
            /* fall through */
        case NT_OBJECT_LITERAL:
            program->error_str   = (expected_type & CHECK_NUMERIC)
                                   ? str_err_operand_not_numeric : str_err_operand_not_string;
            program->error_token = &node->token;
            return KOS_ERROR_COMPILE_FAILED;

        default:
            return KOS_SUCCESS;
    }
}

static int pos_neg(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node,
                   KOS_REG           **reg)
{
    int                     error;
    const KOS_OPERATOR_TYPE op  = (KOS_OPERATOR_TYPE)node->token.op;
    KOS_REG                *src = *reg;

    assert(op == OT_ADD || op == OT_SUB);

    node = node->children;
    assert(node);
    assert(!node->next);

    TRY(check_const_literal(program, node, CHECK_NUMERIC));

    TRY(visit_node(program, node, &src));
    assert(src);

    if (op == OT_SUB) {

        KOS_REG *val = KOS_NULL;

        TRY(gen_dest_reg(program, reg, src));

        TRY(gen_reg(program, &val));
        TRY(gen_instr2(program, INSTR_LOAD_INT8, val->reg, 0));

        TRY(gen_instr3(program,
                       INSTR_SUB,
                       (*reg)->reg,
                       val->reg,
                       src->reg));

        free_reg(program, val);

        if (src != *reg)
            free_reg(program, src);
    }
    else
        /* TODO: enforce numeric */
        *reg = src;

cleanup:
    return error;
}

static int log_not(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node,
                   KOS_REG           **reg)
{
    int      error;
    int      jump_offs;
    unsigned negated = 0;
    KOS_REG *src     = KOS_NULL;

    node = node->children;
    assert(node);
    assert(!node->next);

    /* TODO use gen_cond_jump() instead of visit_cond_node() */
    TRY(visit_cond_node(program, node, &src, &negated));
    assert(src);

    TRY(gen_reg(program, reg));

    if (src == *reg) {
        src = KOS_NULL;

        TRY(gen_reg(program, &src));

        TRY(gen_instr2(program, INSTR_MOVE, src->reg, (*reg)->reg));
    }

    TRY(gen_instr1(program, INSTR_LOAD_FALSE, (*reg)->reg));

    jump_offs = program->cur_offs;
    TRY(gen_instr2(program, negated ? INSTR_JUMP_NOT_COND : INSTR_JUMP_COND, 0, src->reg));

    TRY(gen_instr1(program, INSTR_LOAD_TRUE, (*reg)->reg));

    update_jump_offs(program, jump_offs, program->cur_offs);

    free_reg(program, src);

cleanup:
    return error;
}

static int log_and_or(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg)
{
    int                     error;
    int                     left_offs;
    int                     right_offs = 0;
    const KOS_OPERATOR_TYPE op         = (KOS_OPERATOR_TYPE)node->token.op;
    KOS_REG                *left       = KOS_NULL;
    KOS_REG                *right      = KOS_NULL;

    assert(op == OT_LOGAND || op == OT_LOGOR);

    node = node->children;
    assert(node);
    assert(node->next);

    TRY(visit_node(program, node, &left));
    assert(left);

    node = node->next;
    assert(node);
    assert(!node->next);

    left_offs = program->cur_offs;

    TRY(gen_instr2(program,
                   (op == OT_LOGAND) ? INSTR_JUMP_NOT_COND : INSTR_JUMP_COND,
                   0,
                   left->reg));

    if (left->tmp) {
        right = left;

        if ( ! *reg)
            *reg = left;
    }

    TRY(visit_node(program, node, &right));
    assert(right);

    if ( ! *reg) {
        if (right->tmp)
            *reg = right;
        else
            TRY(gen_reg(program, reg));
    }

    if (*reg != right && left != right) {
        TRY(gen_instr2(program, INSTR_MOVE, (*reg)->reg, right->reg));
        free_reg(program, right);
    }

    if (*reg != left && left != right) {
        right_offs = program->cur_offs;

        TRY(gen_instr1(program, INSTR_JUMP, 0));
    }

    update_jump_offs(program, left_offs, program->cur_offs);

    if (*reg != left) {
        TRY(gen_instr2(program, INSTR_MOVE, (*reg)->reg, left->reg));
        free_reg(program, left);
    }

    if (right_offs)
        update_jump_offs(program, right_offs, program->cur_offs);

cleanup:
    return error;
}

static int check_if_node_is_register(const KOS_AST_NODE *node,
                                     KOS_REG            *reg)
{
    return reg &&
           node->type == NT_IDENTIFIER &&
           node->is_local_var &&
           node->u.var->reg == reg;
}

static int log_tri(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node,
                   KOS_REG           **reg)
{
    JUMP_ARRAY          jump_array;
    const KOS_AST_NODE *cond_node;
    const KOS_AST_NODE *true_node;
    const KOS_AST_NODE *false_node;
    int                 true_node_is_dest;
    int                 false_node_is_dest;
    int                 final_jump_offs = -1;
    int                 error           = KOS_SUCCESS;

    cond_node = node->children;
    assert(cond_node);

    true_node = cond_node->next;
    assert(true_node);

    false_node = true_node->next;
    assert(false_node);
    assert( ! false_node->next);

    true_node_is_dest  = check_if_node_is_register(true_node,  *reg);
    false_node_is_dest = check_if_node_is_register(false_node, *reg);

    init_jump_array(&jump_array);

    if (true_node_is_dest && false_node_is_dest) {

        KOS_REG *tmp = KOS_NULL;

        TRY(visit_node(program, cond_node, &tmp));
        assert(tmp);

        free_reg(program, tmp);
    }
    else
        TRY(gen_cond_jump(program, cond_node, true_node_is_dest ? INSTR_JUMP_COND : INSTR_JUMP_NOT_COND, &jump_array));

    if ( ! true_node_is_dest) {

        KOS_REG *src = *reg;

        TRY(visit_node(program, true_node, &src));
        assert(src);

        if (src != *reg) {
            if ( ! *reg)
                TRY(gen_dest_reg(program, reg, src));

            TRY(gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

            if (src != *reg)
                free_reg(program, src);
        }

        if ( ! false_node_is_dest) {
            final_jump_offs = program->cur_offs;
            TRY(gen_instr1(program, INSTR_JUMP, 0));
        }

        update_jump_array(program, &jump_array, program->cur_offs);
    }

    if ( ! false_node_is_dest) {

        KOS_REG *src = *reg;

        TRY(visit_node(program, false_node, &src));
        assert(src);

        if (src != *reg) {
            if ( ! *reg)
                TRY(gen_dest_reg(program, reg, src));

            TRY(gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

            if (src != *reg)
                free_reg(program, src);
        }
    }

    if (true_node_is_dest)
        update_jump_array(program, &jump_array, program->cur_offs);

    if (final_jump_offs >= 0)
        update_jump_offs(program, final_jump_offs, program->cur_offs);

cleanup:
    return error;
}

static int has_prop(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node,
                    KOS_REG           **reg,
                    int                 instr)
{
    int      error;
    int      str_idx;
    KOS_REG *src = *reg;

    TRY(visit_node(program, node->children, &src));
    assert(src);

    TRY(gen_dest_reg(program, reg, src));

    TRY(gen_str(program, &node->children->next->token, &str_idx));

    if (str_idx > 255) {
        KOS_REG *tmp   = KOS_NULL;
        int      rprop = (*reg)->reg;

        if (rprop == src->reg) {
            TRY(gen_reg(program, &tmp));
            rprop = tmp->reg;
        }

        TRY(gen_load_const(program, node->children->next, rprop, str_idx));

        instr = (instr == INSTR_HAS_DP_PROP8) ? INSTR_HAS_DP : INSTR_HAS_SH;

        TRY(gen_instr3(program, instr, (*reg)->reg, src->reg, rprop));

        if (tmp)
            free_reg(program, tmp);
    }
    else
        TRY(gen_instr3(program, instr, (*reg)->reg, src->reg, str_idx));

    if (src != *reg)
        free_reg(program, src);

cleanup:
    return error;
}

static int delete_op(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node,
                     KOS_REG           **reg)
{
    int      error;
    KOS_REG *obj = KOS_NULL;

    assert(node->children);

    if (node->children->type != NT_REFINEMENT) {
        program->error_token = &node->children->token;
        program->error_str   = str_err_expected_refinement;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }

    node = node->children;
    assert(!node->next);
    node = node->children;
    assert(node);

    TRY(visit_node(program, node, &obj));
    assert(obj);

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {

        int str_idx;
        TRY(gen_str(program, &node->token, &str_idx));

        if (str_idx > 255) {
            TRY(gen_reg(program, reg));

            TRY(gen_load_const(program, node, (*reg)->reg, str_idx));

            TRY(gen_instr2(program, INSTR_DEL, obj->reg, (*reg)->reg));
        }
        else
            TRY(gen_instr2(program, INSTR_DEL_PROP8, obj->reg, str_idx));
    }
    else if (node->type == NT_NUMERIC_LITERAL) {

        program->error_token = &node->token;
        program->error_str   = str_err_expected_refinement_ident;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }
    else {

        KOS_REG *prop_reg = KOS_NULL;

        TRY(visit_node(program, node, &prop_reg));
        assert(prop_reg);

        TRY(gen_instr2(program, INSTR_DEL, obj->reg, prop_reg->reg));

        free_reg(program, prop_reg);
    }

    free_reg(program, obj);

    TRY(gen_reg(program, reg));
    TRY(gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg));

cleanup:
    return error;
}

static int process_operator(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    int                     error    = KOS_SUCCESS;
    const KOS_OPERATOR_TYPE op       = (KOS_OPERATOR_TYPE)node->token.op;
    const KOS_KEYWORD_TYPE  kw       = (KOS_KEYWORD_TYPE)node->token.keyword;
    KOS_REG                *reg1     = KOS_NULL;
    KOS_REG                *reg2     = KOS_NULL;
    int                     opcode   = 0;
    int                     operands = 0;
    int                     swap     = 0;

    assert(node);
    assert(node->children);

    switch (op) {

        case OT_LOGNOT:
            /* fall through */
        default:
            assert(op == OT_LOGNOT);
            return log_not(program, node, reg);

        case OT_LOGAND:
            /* fall through */
        case OT_LOGOR:
            return log_and_or(program, node, reg);

        case OT_LOGTRI:
            return log_tri(program, node, reg);

        case OT_NONE:
            switch (kw) {

                case KW_TYPEOF:
                    opcode   = INSTR_TYPE;
                    operands = 1;
                    break;

                case KW_DELETE:
                    return delete_op(program, node, reg);

                case KW_IN:
                    {
                        const KOS_AST_NODE *second = node->children->next;
                        if (second && second->type == NT_STRING_LITERAL)
                            return has_prop(program, node, reg, INSTR_HAS_SH_PROP8);
                    }
                    opcode   = INSTR_HAS_SH;
                    operands = 2;
                    break;

                case KW_PROPERTYOF:
                    {
                        const KOS_AST_NODE *second = node->children->next;
                        if (second && second->type == NT_STRING_LITERAL)
                            return has_prop(program, node, reg, INSTR_HAS_DP_PROP8);
                    }
                    opcode   = INSTR_HAS_DP;
                    operands = 2;
                    break;

                case KW_INSTANCEOF:
                    /* fall through */
                default:
                    assert(kw == KW_INSTANCEOF);
                    opcode   = INSTR_INSTANCEOF;
                    operands = 2;
                    break;
            }
            break;

        case OT_ADD:
            if (!node->children->next)
                return pos_neg(program, node, reg);
            opcode   = INSTR_ADD;
            operands = 2;
            break;

        case OT_SUB:
            if (!node->children->next)
                return pos_neg(program, node, reg);
            opcode   = INSTR_SUB;
            operands = 2;
            break;

        case OT_MUL:  opcode = INSTR_MUL;    operands = 2; break;
        case OT_DIV:  opcode = INSTR_DIV;    operands = 2; break;
        case OT_MOD:  opcode = INSTR_MOD;    operands = 2; break;
        case OT_NOT:  opcode = INSTR_NOT;    operands = 1; break;
        case OT_AND:  opcode = INSTR_AND;    operands = 2; break;
        case OT_OR:   opcode = INSTR_OR;     operands = 2; break;
        case OT_XOR:  opcode = INSTR_XOR;    operands = 2; break;
        case OT_SHL:  opcode = INSTR_SHL;    operands = 2; break;
        case OT_SHR:  opcode = INSTR_SHR;    operands = 2; break;
        case OT_SHRU: opcode = INSTR_SHRU;   operands = 2; break;
        case OT_EQ:   opcode = INSTR_CMP_EQ; operands = 2; break;
        case OT_NE:   opcode = INSTR_CMP_NE; operands = 2; break;
        case OT_GE:   opcode = INSTR_CMP_LE; operands = 2; swap = 1; break;
        case OT_GT:   opcode = INSTR_CMP_LT; operands = 2; swap = 1; break;
        case OT_LE:   opcode = INSTR_CMP_LE; operands = 2; break;
        case OT_LT:   opcode = INSTR_CMP_LT; operands = 2; break;
    }

    node = node->children;

    switch (op) {

        case OT_ADD:
            if (operands == 2) {
                const KOS_AST_NODE *const_a = kos_get_const(program, node);
                const KOS_AST_NODE *const_b;
                assert(node->next);
                const_b = kos_get_const(program, node->next);

                if (const_a) {
                    if (const_b) {
                        const KOS_NODE_TYPE a_type = (KOS_NODE_TYPE)const_a->type;
                        const KOS_NODE_TYPE b_type = (KOS_NODE_TYPE)const_b->type;

                        if (a_type == NT_STRING_LITERAL ||
                            (a_type != NT_NUMERIC_LITERAL && b_type == NT_STRING_LITERAL)) {
                            TRY(check_const_literal(program, node,       CHECK_STRING));
                            TRY(check_const_literal(program, node->next, CHECK_STRING));
                        }
                        else {
                            TRY(check_const_literal(program, node,       CHECK_NUMERIC));
                            TRY(check_const_literal(program, node->next, CHECK_NUMERIC));
                        }
                    }
                    else
                        TRY(check_const_literal(program, node, CHECK_NUMERIC_OR_STRING));
                }
                else
                    TRY(check_const_literal(program, node->next, CHECK_NUMERIC_OR_STRING));

                break;
            }
            /* fall through */
        case OT_SUB:
            /* fall through */
        case OT_MUL:
            /* fall through */
        case OT_DIV:
            /* fall through */
        case OT_MOD:
            /* fall through */
        case OT_NOT:
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
        case OT_SHRU:
            TRY(check_const_literal(program, node, CHECK_NUMERIC));

            if (node->next)
                TRY(check_const_literal(program, node->next, CHECK_NUMERIC));
            break;

        default:
            break;
    }

    TRY(visit_node(program, node, &reg1));
    assert(reg1);

    node = node->next;
    if (operands == 2) {

        assert(node);

        TRY(visit_node(program, node, &reg2));
        assert(reg2);

        assert(!node->next);
    }
    else {
        assert( ! node);
    }

    /* Reuse another temporary register */
    if ( ! *reg) {
        if (reg1->tmp)
            *reg = reg1;
        else if (operands == 2 && reg2->tmp)
            *reg = reg2;
        else
            TRY(gen_reg(program, reg));
    }

    if (operands == 2) {
        if (swap) {
            KOS_REG *tmp = reg1;
            reg1         = reg2;
            reg2         = tmp;
        }
        error = gen_instr3(program, opcode, (*reg)->reg, reg1->reg, reg2->reg);
    }
    else {
        assert( ! swap);
        error = gen_instr2(program, opcode, (*reg)->reg, reg1->reg);
    }

    if (*reg != reg1)
        free_reg(program, reg1);
    if (reg2 && *reg != reg2)
        free_reg(program, reg2);

cleanup:
    return error;
}

static int assign_instr(KOS_OPERATOR_TYPE op)
{
    switch (op) {
        case OT_SETADD: return INSTR_ADD;
        case OT_SETSUB: return INSTR_SUB;
        case OT_SETMUL: return INSTR_MUL;
        case OT_SETDIV: return INSTR_DIV;
        case OT_SETMOD: return INSTR_MOD;
        case OT_SETAND: return INSTR_AND;
        case OT_SETOR:  return INSTR_OR;
        case OT_SETXOR: return INSTR_XOR;
        case OT_SETSHL: return INSTR_SHL;
        case OT_SETSHR: return INSTR_SHR;
        case OT_SETSHRU:
            /* fall through */
        default:        assert(op == OT_SETSHRU);
                        return INSTR_SHRU;
    }
}

static int assign_member(KOS_COMP_UNIT      *program,
                         KOS_OPERATOR_TYPE   assg_op,
                         const KOS_AST_NODE *node,
                         KOS_REG            *src)
{
    int      error;
    int      str_idx = 0;
    int64_t  idx     = 0;
    KOS_REG *obj     = KOS_NULL;
    KOS_REG *tmp_reg = KOS_NULL;

    assert(node->type == NT_REFINEMENT);

    node = node->children;
    assert(node);

    TRY(visit_node(program, node, &obj));
    assert(obj);

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {
        TRY(gen_str(program, &node->token, &str_idx));

        if (assg_op != OT_SET) {

            TRY(gen_reg(program, &tmp_reg));

            TRY(gen_get_prop_instr(program, node, tmp_reg->reg, obj->reg, str_idx));

            TRY(gen_instr3(program, assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(gen_set_prop_instr(program, node, obj->reg, str_idx, src->reg));
    }
    else if (maybe_int(node, &idx)) {

        assert(node->type == NT_NUMERIC_LITERAL);

        if (idx > INT_MAX || idx < INT_MIN) {
            program->error_token = &node->token;
            program->error_str   = str_err_invalid_index;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        if (assg_op != OT_SET) {

            TRY(gen_reg(program, &tmp_reg));

            TRY(gen_instr3(program, INSTR_GET_ELEM, tmp_reg->reg, obj->reg, (int)idx));

            TRY(gen_instr3(program, assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(gen_instr3(program, INSTR_SET_ELEM, obj->reg, (int)idx, src->reg));
    }
    else {

        KOS_REG *prop = KOS_NULL;

        TRY(visit_node(program, node, &prop));
        assert(prop);

        if (assg_op != OT_SET) {

            TRY(gen_reg(program, &tmp_reg));

            TRY(gen_instr3(program, INSTR_GET, tmp_reg->reg, obj->reg, prop->reg));

            TRY(gen_instr3(program, assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(gen_instr3(program, INSTR_SET, obj->reg, prop->reg, src->reg));

        free_reg(program, prop);
    }

    if (tmp_reg)
        free_reg(program, tmp_reg);

    free_reg(program, obj);

cleanup:
    return error;
}

static int assign_non_local(KOS_COMP_UNIT      *program,
                            KOS_OPERATOR_TYPE   assg_op,
                            const KOS_AST_NODE *node,
                            KOS_REG            *src)
{
    int      error;
    KOS_VAR *var     = KOS_NULL;
    KOS_REG *tmp_reg = KOS_NULL;
    KOS_REG *container_reg;

    assert(node->type == NT_IDENTIFIER);

    TRY(lookup_var(program, node, &var, &container_reg));

    assert(var->type != VAR_LOCAL);
    assert(var->type != VAR_ARGUMENT_IN_REG);
    assert(var->type != VAR_MODULE);

    if (assg_op != OT_SET) {

        TRY(gen_reg(program, &tmp_reg));

        if (var->type == VAR_GLOBAL)
            TRY(gen_instr2(program, INSTR_GET_GLOBAL, tmp_reg->reg, var->array_idx));
        else
            TRY(gen_instr3(program, INSTR_GET_ELEM, tmp_reg->reg, container_reg->reg, var->array_idx));

        TRY(gen_instr3(program, assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

        src = tmp_reg;
    }

    if (var->type == VAR_GLOBAL)
        TRY(gen_instr2(program, INSTR_SET_GLOBAL, var->array_idx, src->reg));
    else
        TRY(gen_instr3(program, INSTR_SET_ELEM, container_reg->reg, var->array_idx, src->reg));

    if (tmp_reg)
        free_reg(program, tmp_reg);

cleanup:
    return error;
}

static int assign_slice(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_REG            *src)
{
    int                 error;
    int                 str_idx;
    const KOS_AST_NODE *obj_node     = KOS_NULL;
    KOS_REG            *argn[3]      = { KOS_NULL, KOS_NULL, KOS_NULL };
    KOS_REG            *obj_reg      = KOS_NULL;
    KOS_REG            *func_reg     = KOS_NULL;
    const int           src_reg      = src->reg;
    static const char   str_insert[] = "insert";
    KOS_TOKEN           token;

    free_reg(program, src);

    TRY(gen_reg_range(program, &argn[0], 3));

    if (src_reg != argn[2]->reg)
        TRY(gen_instr2(program, INSTR_MOVE, argn[2]->reg, src_reg));

    node = node->children;
    assert(node);

    obj_node = node;
    node     = node->next;
    assert(node);

    obj_reg = argn[0];
    TRY(visit_node(program, node, &obj_reg));
    assert(obj_reg);

    if (obj_reg != argn[0]) {
        TRY(gen_instr2(program, INSTR_MOVE, argn[0]->reg, obj_reg->reg));
        free_reg(program, obj_reg);
    }

    node = node->next;
    assert(node);
    assert( ! node->next);

    obj_reg = argn[1];
    TRY(visit_node(program, node, &obj_reg));
    assert(obj_reg);

    if (obj_reg != argn[1]) {
        TRY(gen_instr2(program, INSTR_MOVE, argn[1]->reg, obj_reg->reg));
        free_reg(program, obj_reg);
    }

    obj_reg = KOS_NULL;
    TRY(visit_node(program, obj_node, &obj_reg));
    assert(obj_reg);

    memset(&token, 0, sizeof(token));
    token.begin  = str_insert;
    token.length = sizeof(str_insert) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(gen_str(program, &token, &str_idx));

    TRY(gen_reg(program, &func_reg));

    TRY(gen_get_prop_instr(program, obj_node, func_reg->reg, obj_reg->reg, str_idx));

    TRY(gen_instr5(program, INSTR_CALL_N, func_reg->reg, func_reg->reg, obj_reg->reg, argn[0]->reg, 3));

    free_reg(program, argn[2]);
    free_reg(program, argn[1]);
    free_reg(program, argn[0]);
    free_reg(program, func_reg);
    free_reg(program, obj_reg);

cleanup:
    return error;
}

static int assignment(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *assg_node)
{
    int                 error;
    int                 is_lhs;
    const KOS_AST_NODE *node;
    const KOS_AST_NODE *lhs_node;
    const KOS_AST_NODE *rhs_node;
    KOS_REG            *reg       = KOS_NULL;
    KOS_REG            *rhs       = KOS_NULL;
    const KOS_NODE_TYPE node_type = (KOS_NODE_TYPE)assg_node->type;

    assert(node_type == NT_ASSIGNMENT || node_type == NT_MULTI_ASSIGNMENT);

    node = assg_node->children;
    assert(node);
    lhs_node = node;

    rhs_node = node->next;
    assert(rhs_node);
    assert(!rhs_node->next);

    assert(node->type == NT_LEFT_HAND_SIDE ||
           node->type == NT_VAR ||
           node->type == NT_CONST);

    is_lhs = (node->type == NT_LEFT_HAND_SIDE) ? 1 : 0;

    assert(is_lhs ||
            (node->children &&
                (node->children->type == NT_IDENTIFIER ||
                 node->children->type == NT_PLACEHOLDER)));

    node = node->children;
    assert(node);

    if (node_type == NT_ASSIGNMENT) {

        assert( ! node->next);
        assert(node->type != NT_PLACEHOLDER);

        if (assg_node->token.op != OT_SET)
            /* TODO check lhs variable type */
            TRY(check_const_literal(program, rhs_node,
                    assg_node->token.op == OT_SETADD ? CHECK_NUMERIC_OR_STRING : CHECK_NUMERIC));

        if (node->type == NT_IDENTIFIER)
            TRY(lookup_local_var_even_inactive(program, node, is_lhs, &reg));

        if (reg && assg_node->token.op == OT_SET)
            rhs = reg;
    }

    if (kos_is_self_ref_func(lhs_node)) {
        KOS_VAR *fun_var = lhs_node->children->u.var;
        assert( ! lhs_node->children->is_scope);
        assert(lhs_node->children->is_var);
        assert(fun_var);
        assert( ! fun_var->is_active);

        if (rhs_node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, rhs_node, fun_var, KOS_NULL, KOS_NULL, &rhs));
        else {
            assert(rhs_node->type == NT_CLASS_LITERAL);
            TRY(class_literal(program, rhs_node, fun_var, &rhs));
        }
    }
    else
        TRY(visit_node(program, rhs_node, &rhs));
    assert(rhs);

    if (node_type == NT_MULTI_ASSIGNMENT) {
        KOS_REG *rsrc = KOS_NULL;

        if (rhs->tmp)
            rsrc = rhs;
        else {
            rsrc = rhs;
            rhs  = KOS_NULL;
            TRY(gen_reg(program, &rhs));
        }

        TRY(gen_instr2(program, INSTR_LOAD_ITER, rhs->reg, rsrc->reg));
    }

    for ( ; node; node = node->next) {

        if ( ! reg && node->type == NT_IDENTIFIER)
            TRY(lookup_local_var_even_inactive(program, node, is_lhs, &reg));

        if (reg) {

            if (assg_node->token.op == OT_SET) {

                if (node_type == NT_MULTI_ASSIGNMENT) {

                    assert(reg != rhs);

                    TRY(gen_instr2(program, INSTR_NEXT, reg->reg, rhs->reg));
                }
                else {

                    if (rhs != reg) {
                        TRY(gen_instr2(program, INSTR_MOVE, reg->reg, rhs->reg));
                        free_reg(program, rhs);
                    }
                }
            }
            else {

                assert(node_type == NT_ASSIGNMENT);

                TRY(gen_instr3(program,
                               assign_instr((KOS_OPERATOR_TYPE)assg_node->token.op),
                               reg->reg,
                               reg->reg,
                               rhs->reg));

                free_reg(program, rhs);
            }

            if ( ! is_lhs)
                kos_activate_var(program, node);
        }
        else {

            if ( ! is_lhs)
                kos_activate_var(program, node);

            if (node_type == NT_MULTI_ASSIGNMENT) {

                TRY(gen_reg(program, &reg));

                TRY(gen_instr2(program, INSTR_NEXT, reg->reg, rhs->reg));
            }
            else
                reg = rhs;

            if (node->type == NT_REFINEMENT)
                TRY(assign_member(program, (KOS_OPERATOR_TYPE)assg_node->token.op, node, reg));

            else if (node->type == NT_IDENTIFIER)
                TRY(assign_non_local(program, (KOS_OPERATOR_TYPE)assg_node->token.op, node, reg));

            else if (node->type != NT_PLACEHOLDER) {

                assert(node->type == NT_SLICE);
                assert(assg_node->token.op == OT_SET);
                TRY(assign_slice(program, node, reg));
                reg = KOS_NULL; /* assign_slice frees the register */
            }

            if (reg)
                free_reg(program, reg);
        }

        reg = KOS_NULL;
    }

    if (node_type == NT_MULTI_ASSIGNMENT)
        free_reg(program, rhs);

cleanup:
    return error;
}

static int identifier(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg)
{
    int      error   = KOS_SUCCESS;
    KOS_REG *src_reg = KOS_NULL;

    TRY(lookup_local_var(program, node, &src_reg));

    if (src_reg)
        *reg = src_reg;
    else if (node->is_const_fun) {
        KOS_FRAME         *fun_frame;
        KOS_COMP_FUNCTION *constant;
        KOS_VAR           *var = node->u.var;
        assert(node->is_var);
        assert(var);
        assert(var->is_const);

        node = var->value;
        assert(node->type == NT_FUNCTION_LITERAL);
        assert(node->is_scope);
        fun_frame = (KOS_FRAME *)node->u.scope;
        assert(fun_frame->scope.has_frame);

        constant = fun_frame->constant;
        assert(constant);
        assert(constant->header.type == KOS_COMP_CONST_FUNCTION);
        assert(constant->load_instr == INSTR_LOAD_CONST);

        TRY(gen_reg(program, reg));

        TRY(gen_load_const(program,
                           node,
                           (*reg)->reg,
                           (int32_t)constant->header.index));
    }
    else {

        KOS_VAR *var           = KOS_NULL;
        KOS_REG *container_reg = KOS_NULL;

        TRY(gen_reg(program, reg));

        TRY(lookup_var(program, node, &var, &container_reg));

        assert(var->type != VAR_LOCAL);
        assert(var->type != VAR_ARGUMENT_IN_REG);

        switch (var->type) {

            case VAR_GLOBAL:
                TRY(gen_instr2(program, INSTR_GET_GLOBAL, (*reg)->reg, var->array_idx));
                break;

            case VAR_IMPORTED:
                TRY(gen_instr3(program, INSTR_GET_MOD_ELEM, (*reg)->reg, var->module_idx, var->array_idx));
                break;

            case VAR_MODULE:
                TRY(gen_instr2(program, INSTR_GET_MOD, (*reg)->reg, var->array_idx));
                break;

            default:
                assert(container_reg);
                TRY(gen_instr3(program, INSTR_GET_ELEM, (*reg)->reg, container_reg->reg, var->array_idx));
                break;
        }
    }

cleanup:
    return error;
}

static int numeric_literal(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           KOS_REG           **reg)
{
    KOS_NUMERIC numeric;
    int         error;

    TRY(gen_reg(program, reg));

    if (node->token.type == TT_NUMERIC_BINARY) {

        const KOS_NUMERIC *value = (const KOS_NUMERIC *)node->token.begin;

        assert(node->token.length == sizeof(KOS_NUMERIC));

        numeric = *value;
    }
    else
        error = kos_parse_numeric(node->token.begin,
                                  node->token.begin + node->token.length,
                                  &numeric);

    if (error) {
        program->error_token = &node->token;
        program->error_str   = str_err_invalid_numeric_literal;
        error = KOS_ERROR_COMPILE_FAILED;
    }
    else
        error = gen_load_number(program,
                                node,
                                (*reg)->reg,
                                &numeric);

cleanup:
    return error;
}

static int string_literal(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg)
{
    int error;
    int str_idx;

    error = gen_str(program, &node->token, &str_idx);

    if (!error) {
        error = gen_reg(program, reg);

        if (!error)
            error = gen_load_const(program,
                                   node,
                                   (*reg)->reg,
                                   str_idx);
    }

    return error;
}

static int this_literal(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_REG           **reg)
{
    assert(program->cur_frame->this_reg);
    *reg = program->cur_frame->this_reg;
    return KOS_SUCCESS;
}

static int bool_literal(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_REG           **reg)
{
    int error = gen_reg(program, reg);

    const int opcode = node->token.keyword == KW_TRUE ? INSTR_LOAD_TRUE : INSTR_LOAD_FALSE;

    if (!error)
        error = gen_instr1(program, opcode, (*reg)->reg);

    return error;
}

static int void_literal(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_REG           **reg)
{
    int error = gen_reg(program, reg);

    if (!error)
        error = gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg);

    return error;
}

typedef struct KOS_GEN_CLOSURE_ARGS_S {
    KOS_COMP_UNIT *program;
    uint8_t        num_binds;
    uint8_t        bind_reg;
} KOS_GEN_CLOSURE_ARGS;

static int gen_closure_regs(KOS_RED_BLACK_NODE *node,
                            void               *cookie)
{
    int error = KOS_SUCCESS;

    KOS_SCOPE_REF        *ref  = (KOS_SCOPE_REF *)node;
    KOS_GEN_CLOSURE_ARGS *args = (KOS_GEN_CLOSURE_ARGS *)cookie;

    if (ref->exported_locals) {
        ++args->num_binds;
        error = gen_reg(args->program, &ref->vars_reg);
        if ( ! error) {
            ref->vars_reg->tmp = 0;
            ref->vars_reg_idx  = ref->vars_reg->reg;
            if (args->bind_reg == KOS_NO_REG)
                args->bind_reg = (uint8_t)ref->vars_reg_idx;
            else {
                assert((unsigned)ref->vars_reg_idx == args->bind_reg + args->num_binds - 1U);
            }
        }
    }

    if ( ! error && ref->exported_args) {
        ++args->num_binds;
        error = gen_reg(args->program, &ref->args_reg);
        if ( ! error) {
            ref->args_reg->tmp = 0;
            ref->args_reg_idx  = ref->args_reg->reg;
            if (args->bind_reg == KOS_NO_REG)
                args->bind_reg = (uint8_t)ref->args_reg_idx;
            else {
                assert((unsigned)ref->args_reg_idx == args->bind_reg + args->num_binds - 1U);
            }
        }
    }

    return error;
}

typedef struct KOS_BIND_ARGS_S {
    KOS_COMP_UNIT *program;
    KOS_REG       *func_reg;
    KOS_FRAME     *parent_frame;
    int            delta;
} KOS_BIND_ARGS;

static int gen_binds(KOS_RED_BLACK_NODE *node,
                     void               *cookie)
{
    int error = KOS_SUCCESS;

    KOS_SCOPE_REF *ref     = (KOS_SCOPE_REF *)node;
    KOS_BIND_ARGS *args    = (KOS_BIND_ARGS *)cookie;
    KOS_COMP_UNIT *program = args->program;
    const int      delta   = args->delta;

    if (ref->exported_locals) {

        assert(ref->vars_reg);
        assert(ref->vars_reg_idx >= delta);
        assert(ref->closure->has_frame);

        if (args->parent_frame == (KOS_FRAME *)(ref->closure))
            TRY(gen_instr2(program,
                           INSTR_BIND_SELF,
                           args->func_reg->reg,
                           ref->vars_reg_idx - delta));
        else {

            KOS_SCOPE_REF *other_ref =
                    kos_find_scope_ref(args->parent_frame, ref->closure);

            assert(args->parent_frame->num_binds);

            TRY(gen_instr3(program,
                           INSTR_BIND,
                           args->func_reg->reg,
                           ref->vars_reg_idx - delta,
                           other_ref->vars_reg_idx));
        }
    }

    if (ref->exported_args) {

        int src_reg = -1;

        assert(ref->args_reg);
        assert(ref->args_reg_idx >= delta);
        assert(ref->closure->has_frame);

        if (args->parent_frame == (KOS_FRAME *)(ref->closure)) {
            assert(args->parent_frame->args_reg);
            src_reg = args->parent_frame->args_reg->reg;
        }
        else {

            KOS_SCOPE_REF *other_ref =
                    kos_find_scope_ref(args->parent_frame, ref->closure);

            assert(args->parent_frame->num_binds);

            src_reg = other_ref->args_reg_idx;
        }

        TRY(gen_instr3(program,
                       INSTR_BIND,
                       args->func_reg->reg,
                       ref->args_reg_idx - delta,
                       src_reg));
    }

cleanup:
    return error;
}

static int free_arg_regs(KOS_RED_BLACK_NODE *node,
                         void               *cookie)
{
    KOS_VAR       *var     = (KOS_VAR *)node;
    KOS_COMP_UNIT *program = (KOS_COMP_UNIT *)cookie;

    if ((var->type & VAR_ARGUMENT_IN_REG) && var->reg && var->reg->tmp) {
        free_reg(program, var->reg);
        var->reg = KOS_NULL;
    }

    return KOS_SUCCESS;
}

static int prealloc_arg_names(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node)
{
    int count = 0;

    node = node->children;
    assert(node);
    assert(node->type == NT_NAME || node->type == NT_NAME_CONST);
    node = node->next;
    assert(node->type == NT_PARAMETERS);
    node = node->children;

    while (node && node->type != NT_ELLIPSIS) {

        int arg_str_idx;

        const KOS_AST_NODE *const ident_node =
            node->type == NT_IDENTIFIER ? node : node->children;

        if (gen_str(program, &ident_node->token, &arg_str_idx))
            return 0;

        node = node->next;
        ++count;
        if (count == KOS_MAX_REGS)
            break;
    }

    return count;
}

static int gen_function(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_VAR            *fun_var,
                        int                 needs_super_ctor,
                        KOS_SCOPE         **out_scope)
{
    int        error        = KOS_SUCCESS;
    KOS_SCOPE *scope;
    KOS_FRAME *frame;
    KOS_VAR   *var;
    KOS_REG   *scope_reg    = KOS_NULL;
    int        fun_start_offs;
    size_t     addr2line_start_offs;
    int        last_reg     = -1;
    int        str_idx;

    const KOS_AST_NODE *fun_node  = node;
    const KOS_AST_NODE *open_node;
    const KOS_AST_NODE *name_node = fun_node->children;
    KOS_COMP_FUNCTION  *constant;

    assert(node->is_scope);
    scope = node->u.scope;
    frame = (KOS_FRAME *)scope;

    *out_scope = scope;

    if (frame->constant)
        return KOS_SUCCESS;

    {
        const KOS_TOKEN *name_token;

        assert(name_node);
        assert(name_node->type == NT_NAME || name_node->type == NT_NAME_CONST);
        if (name_node->children)
            name_token = &name_node->children->token;
        else
            name_token = &fun_node->token;

        TRY(gen_str(program, name_token, &str_idx));
    }

    /* Create constant template for LOAD.CONST */
    {
        /* Generate constants with argument names before the function constant */
        const int num_args = prealloc_arg_names(program, fun_node);;
        if (num_args < 0)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

        assert(num_args <= KOS_MAX_REGS);

        constant = alloc_func_constant(program, frame, (uint32_t)str_idx, (uint8_t)num_args);
        if ( ! constant)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    }

    frame->constant = constant;

    add_constant(program, &constant->header);

    /* Create constant placeholder for class prototype */
    if (fun_node->type == NT_CONSTRUCTOR_LITERAL) {

        KOS_COMP_CONST *proto_const = (KOS_COMP_CONST *)
                KOS_mempool_alloc(&program->allocator, sizeof(KOS_COMP_CONST));

        if ( ! proto_const)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

        proto_const->type = KOS_COMP_CONST_PROTOTYPE;

        add_constant(program, proto_const);
    }

    /* Choose instruction for loading the function */
    constant->load_instr = (uint8_t)
        (((fun_node->type == NT_CONSTRUCTOR_LITERAL) || frame->num_def_used || (frame->num_binds > frame->num_self_refs))
             ? INSTR_LOAD_FUN : INSTR_LOAD_CONST);

    push_scope(program, node);

    frame->fun_token = &fun_node->token;

    /* Generate registers for local independent variables */
    TRY(gen_indep_vars(program, scope, &last_reg));
    assert(last_reg + 1 == scope->num_indep_vars);

    /* Generate registers for function arguments */
    {
        KOS_AST_NODE *arg_node     = fun_node->children;
        int           rest_used    = 0;
        int           have_rest    = 0;
        int           num_def_args = 0;
        int           i;

        assert(arg_node);
        assert(arg_node->type == NT_NAME || arg_node->type == NT_NAME_CONST);
        arg_node = arg_node->next;
        assert(arg_node->type == NT_PARAMETERS);
        arg_node = arg_node->children;

        for (i = 0; arg_node && arg_node->type != NT_ELLIPSIS; arg_node = arg_node->next, ++i) {

            KOS_AST_NODE *ident_node =
                arg_node->type == NT_IDENTIFIER ? arg_node : arg_node->children;
            int           arg_str_idx = 0;

            if (i >= KOS_MAX_REGS) {
                program->error_token = &ident_node->token;
                program->error_str   = str_err_too_many_args;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }

            assert( ! ident_node->is_scope);
            assert(ident_node->is_var);
            var = ident_node->u.var;
            assert(var);
            assert(var == kos_find_var(scope->vars, &ident_node->token));

            assert(var->num_reads || ! var->num_assignments);

            TRY(gen_str(program, &ident_node->token, &arg_str_idx));
            assert(i < constant->num_named_args);
            constant->arg_name_str_idx[i] = arg_str_idx;

            /* Enumerate all args with default values, even if they are unused,
             * because we need to execute code used for initializing them,
             * even if these args will not be default-assigned in practice. */
            if (arg_node->type != NT_IDENTIFIER) {
                ++num_def_args;
                constant->num_decl_def_args = (uint8_t)num_def_args;
                if (var->num_reads || scope->ellipsis)
                    constant->num_used_def_args = (uint8_t)num_def_args;
            }

            /* Process all args up to the last one which is being used,
             * effectively ignoring the tail of unused arguments. */
            if (i < scope->num_args) {

                /* This counts only arguments which don't have a default value
                 * up to the last argument which is used.  The tail of the arguments
                 * list which are not used is discarded and not enumerated.
                 * However, any following default arguments will still be counted. */
                if (arg_node->type == NT_IDENTIFIER)
                    ++constant->min_args;

                if (var->type & VAR_ARGUMENT_IN_REG) {

                    assert( ! var->reg);

                    TRY(gen_reg(program, &var->reg));

                    ++last_reg;
                    assert(var->reg->reg == last_reg);

                    if (var->num_reads)
                        var->reg->tmp = 0;

                    var->array_idx = (var->type & VAR_INDEPENDENT) ? var->reg->reg : 0;
                }
                else {
                    have_rest = 1;
                    assert(scope->have_rest);

                    if (var->num_reads)
                        rest_used = 1;
                }
            }
            else {
                assert( ! var->num_reads);
            }
        }

        /* Generate register for the remaining args */
        if (have_rest) {
            TRY(gen_reg(program, &frame->args_reg));
            if (rest_used)
                frame->args_reg->tmp = 0;
            /* TODO don't generate rest reg if unused */
            constant->rest_reg = (uint8_t)frame->args_reg->reg;
            ++last_reg;
            assert(frame->args_reg->reg == last_reg);
        }
    }
    assert(scope->num_args <= KOS_MAX_REGS);

    /* Generate register for 'this' */
    if (scope->uses_this) {
        TRY(gen_reg(program, &frame->this_reg));
        ++last_reg;
        assert(frame->this_reg->reg == last_reg);
        frame->this_reg->tmp = 0;
        constant->this_reg = (uint8_t)frame->this_reg->reg;
    }
    else {
        assert( ! frame->this_reg);
        assert(constant->this_reg == KOS_NO_REG);
    }

    /* Generate register for ellipsis */
    if (scope->ellipsis) {
        if (scope->ellipsis->type == VAR_INDEPENDENT_LOCAL) {
            assert(scope->ellipsis->reg);
            constant->ellipsis_reg = (uint8_t)scope->ellipsis->reg->reg;
        }
        else {
            assert( ! scope->ellipsis->reg);
            TRY(gen_reg(program, &scope->ellipsis->reg));
            scope->ellipsis->reg->tmp = 0;
            ++last_reg;
            assert(scope->ellipsis->reg->reg == last_reg);
            constant->ellipsis_reg = (uint8_t)scope->ellipsis->reg->reg;
        }
    }

    /* Generate registers for closures */
    {
        KOS_GEN_CLOSURE_ARGS args;
        args.program   = program;
        args.bind_reg  = KOS_NO_REG;
        args.num_binds = 0;

        assert( ! constant->num_binds);

        /* Base class constructor */
        if (needs_super_ctor) {
            TRY(gen_reg(program, &frame->base_ctor_reg));
            frame->base_ctor_reg->tmp = 0;
            ++last_reg;
            assert(frame->base_ctor_reg->reg == last_reg);
            args.bind_reg = (uint8_t)frame->base_ctor_reg->reg;
            ++args.num_binds;
        }

        /* Base class prototype */
        if (frame->uses_base_proto) {
            TRY(gen_reg(program, &frame->base_proto_reg));
            frame->base_proto_reg->tmp = 0;
            ++last_reg;
            assert(frame->base_proto_reg->reg == last_reg);
            if (args.bind_reg == KOS_NO_REG)
                args.bind_reg = (uint8_t)frame->base_proto_reg->reg;
            ++args.num_binds;
        }

        /* Closures from outer scopes */
        TRY(kos_red_black_walk(frame->closures, gen_closure_regs, &args));

        constant->bind_reg  = args.bind_reg;
        constant->num_binds = args.num_binds;
    }

    node = name_node->next;
    assert(node);
    assert(node->type == NT_PARAMETERS);
    node = node->next;
    assert(node);
    assert(node->type == NT_LANDMARK);
    open_node = node;
    node = node->next;
    assert(node);
    assert(node->type == NT_SCOPE);
    assert(node->next);
    assert(node->next->type == NT_LANDMARK);
    assert( ! node->next->next);

    fun_start_offs       = program->cur_offs;
    addr2line_start_offs = program->addr2line_gen_buf.size;

    TRY(add_addr2line(program, &open_node->token, KOS_TRUE_VALUE));

    /* Release unused registers */
    if (frame->args_reg && frame->args_reg->tmp) {
        free_reg(program, frame->args_reg);
        frame->args_reg = KOS_NULL;
    }
    TRY(kos_red_black_walk(scope->vars, free_arg_regs, program));

    /* Invoke super constructor if not invoked explicitly */
    if (needs_super_ctor && ! frame->uses_base_ctor)
        TRY(super_invocation(program, KOS_NULL, KOS_NULL));

    kos_activate_self_ref_func(program, fun_var);

    /* Generate code for function body */
    TRY(visit_node(program, node, &scope_reg));
    assert(!scope_reg);

    kos_deactivate_self_ref_func(program, fun_var);

    /* Set up constant template for LOAD.CONST */
    constant->closure_size = get_closure_size(program);
    constant->num_regs     = (uint8_t)frame->num_regs;
    constant->args_reg     = ((uint32_t)constant->min_args + (uint32_t)constant->num_used_def_args)
                             ? (uint8_t)scope->num_indep_vars : KOS_NO_REG;
    constant->flags        = scope->ellipsis ? KOS_COMP_FUN_ELLIPSIS : 0;

    if (fun_node->type == NT_CONSTRUCTOR_LITERAL)
        constant->flags |= KOS_COMP_FUN_CLASS;
    else if (frame->yield_token)
        constant->flags |= KOS_COMP_FUN_GENERATOR;

    if (scope->num_indep_vars || scope->num_indep_args)
        constant->flags |= KOS_COMP_FUN_CLOSURE;

    /* Check if instruction choice was correct */
    assert(constant->num_binds || (frame->num_self_refs == frame->num_binds));
    assert((fun_node->type == NT_CONSTRUCTOR_LITERAL) || ! constant->num_binds || frame->num_binds || frame->num_def_used);
    assert(scope->num_args - constant->min_args == frame->num_def_used);

    /* Move the function code to final code_buf */
    TRY(append_frame(program, constant, fun_start_offs, addr2line_start_offs));

    pop_scope(program);

    TRY(add_addr2line(program, &fun_node->token, KOS_FALSE_VALUE));

    /* Free register objects */
    free_all_regs(program, frame->used_regs);
    free_all_regs(program, frame->free_regs);
    frame->used_regs = KOS_NULL;
    frame->free_regs = KOS_NULL;

cleanup:
    return error;
}

static int function_literal(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_VAR            *fun_var,
                            KOS_REG            *base_ctor_reg,
                            KOS_REG            *base_proto_reg,
                            KOS_REG           **reg)
{
    int                 error    = KOS_SUCCESS;
    KOS_SCOPE          *scope;
    KOS_FRAME          *frame;
    KOS_COMP_FUNCTION  *constant;
    const KOS_AST_NODE *fun_node = node;

    /* Generate code for the function */
    TRY(gen_function(program, node, fun_var, base_ctor_reg != 0, &scope));

    assert(scope->has_frame);
    frame    = (KOS_FRAME *)scope;
    constant = frame->constant;
    assert(constant);
    assert( ! frame->uses_base_ctor || base_ctor_reg);
    assert( ! base_ctor_reg || constant->num_binds);

    /* Generate LOAD.CONST/LOAD.FUN instruction in the parent frame */
    assert(frame->num_regs > 0);
    assert(constant->this_reg == KOS_NO_REG || frame->num_regs > constant->this_reg);
    TRY(gen_reg(program, reg));

    TRY(gen_instr_load_const(program,
                             node,
                             constant->load_instr,
                             (*reg)->reg,
                             (int32_t)frame->constant->header.index));

    /* Generate BIND instructions in the parent frame */
    if (constant->num_binds) {
        KOS_BIND_ARGS bind_args;

        int bind_idx = 0;

        /* Bind base class constructor */
        if (base_ctor_reg)
            TRY(gen_instr3(program,
                           INSTR_BIND,
                           (*reg)->reg,
                           bind_idx++,
                           base_ctor_reg->reg));

        /* Bind base class prototype */
        if (base_proto_reg)
            TRY(gen_instr3(program,
                           INSTR_BIND,
                           (*reg)->reg,
                           bind_idx++,
                           base_proto_reg->reg));

        /* Binds for outer scopes */
        bind_args.program      = program;
        bind_args.func_reg     = *reg;
        bind_args.parent_frame = program->cur_frame;
        bind_args.delta        = constant->bind_reg;
        TRY(kos_red_black_walk(frame->closures, gen_binds, &bind_args));
    }

    /* Find the first default arg */
    if (constant->num_decl_def_args) {
        node = fun_node->children;
        assert(node);
        node = node->next;
        assert(node);
        node = node->children;
        assert(node);
        for ( ; node; node = node->next) {
            if (node->type == NT_ASSIGNMENT)
                break;
        }
        assert(node);
    }

    /* Generate array with default args and init code for unused default args */
    if (constant->num_decl_def_args) {

        int       i;
        const int num_used_def_args = scope->num_args - constant->min_args;
        KOS_REG  *defaults_reg      = KOS_NULL;

        assert(num_used_def_args <= (int)constant->num_used_def_args);
        assert(num_used_def_args >= 0);
        assert(num_used_def_args == frame->num_def_used);
        constant->num_used_def_args = (uint8_t)num_used_def_args;

        if (num_used_def_args > 0) {
            TRY(gen_reg(program, &defaults_reg));
            TRY(gen_load_array(program, node, defaults_reg->reg, num_used_def_args));
        }

        for (i = 0; node && node->type == NT_ASSIGNMENT; node = node->next, ++i) {

            const KOS_AST_NODE *def_node = node->children;
            KOS_VAR            *var;
            KOS_REG            *arg      = KOS_NULL;
            int                 used;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            assert( ! def_node->is_scope);
            assert(def_node->is_var);
            var = def_node->u.var;
            assert(var);
            used = var->num_reads;
            assert(var->num_reads || ! var->num_assignments);
            assert( ! used || num_used_def_args);

            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            /* Skip executing the init code for the default arg if:
             *  - the default arg is unused and
             *  - the init value is a constant expression or variable.
             */
            if ( ! used) {

                KOS_NODE_TYPE       type;
                const KOS_AST_NODE *const_node = kos_get_const(program, def_node);

                if ( ! const_node)
                    continue;

                type = (KOS_NODE_TYPE)const_node->type;

                if (type == NT_IDENTIFIER      ||
                    type == NT_NUMERIC_LITERAL ||
                    type == NT_STRING_LITERAL  ||
                    type == NT_THIS_LITERAL    ||
                    type == NT_LINE_LITERAL    ||
                    type == NT_BOOL_LITERAL    ||
                    type == NT_VOID_LITERAL)

                    continue;
            }

            TRY(visit_node(program, def_node, &arg));
            assert(arg);

            if (used && defaults_reg)
                TRY(gen_instr3(program, INSTR_SET_ELEM, defaults_reg->reg, i, arg->reg));

            free_reg(program, arg);
        }

        if (num_used_def_args > 0) {
            TRY(gen_instr2(program, INSTR_BIND_DEFAULTS, (*reg)->reg, defaults_reg->reg));

            free_reg(program, defaults_reg);
        }
    }
    else {
        assert( ! frame->num_def_used);
    }

cleanup:
    return error;
}

static int array_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_REG           **reg)
{
    KOS_REG *array_reg = *reg;

    int error = gen_array(program, node->children, &array_reg);

    if ( ! error) {
        if ( ! *reg)
            *reg = array_reg;
        else if (array_reg != *reg) {
            error = gen_instr2(program, INSTR_MOVE, (*reg)->reg, array_reg->reg);
            free_reg(program, array_reg);
        }
    }

    return error;
}

typedef struct KOS_OBJECT_PROP_DUPE_S {
    KOS_RED_BLACK_NODE rb_tree_node;
    int                str_idx;
} KOS_OBJECT_PROP_DUPE;

static int prop_compare_item(void               *what,
                             KOS_RED_BLACK_NODE *node)
{
    const int             str_idx   = (int)(intptr_t)what;
    KOS_OBJECT_PROP_DUPE *prop_node = (KOS_OBJECT_PROP_DUPE *)node;

    return str_idx - prop_node->str_idx;
}

static int prop_compare_node(KOS_RED_BLACK_NODE *a,
                             KOS_RED_BLACK_NODE *b)
{
    KOS_OBJECT_PROP_DUPE *a_node = (KOS_OBJECT_PROP_DUPE *)a;
    KOS_OBJECT_PROP_DUPE *b_node = (KOS_OBJECT_PROP_DUPE *)b;

    return a_node->str_idx - b_node->str_idx;
}

static int object_literal(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg,
                          KOS_VAR            *class_var,
                          KOS_REG            *prototype)
{
    int                 error;
    KOS_RED_BLACK_NODE *prop_str_idcs = KOS_NULL;

    if (prototype) {
        if ( ! *reg || (*reg == prototype && ! prototype->tmp)) {
            *reg = KOS_NULL;
            TRY(gen_reg(program, reg));
        }

        TRY(gen_instr2(program, INSTR_LOAD_OBJ_PROTO, (*reg)->reg, prototype->reg));
    }
    else {
        TRY(gen_reg(program, reg));
        TRY(gen_instr1(program, INSTR_LOAD_OBJ, (*reg)->reg));
    }

    assert(*reg);

    for (node = node->children; node; node = node->next) {

        int                 str_idx;
        const KOS_AST_NODE *prop_node = node->children;
        KOS_REG            *prop      = KOS_NULL;

        assert(node->type == NT_PROPERTY);
        assert(prop_node);
        assert(prop_node->type == NT_STRING_LITERAL);

        TRY(gen_str(program, &prop_node->token, &str_idx));

        if (kos_red_black_find(prop_str_idcs, (void *)(intptr_t)str_idx, prop_compare_item)) {
            program->error_token = &prop_node->token;
            program->error_str   = str_err_duplicate_property;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }
        else {
            KOS_OBJECT_PROP_DUPE *new_node = (KOS_OBJECT_PROP_DUPE *)
                KOS_mempool_alloc(&program->allocator, sizeof(KOS_OBJECT_PROP_DUPE));

            if ( ! new_node)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            new_node->str_idx = str_idx;

            kos_red_black_insert(&prop_str_idcs,
                                 (KOS_RED_BLACK_NODE *)new_node,
                                 prop_compare_node);
        }

        prop_node = prop_node->next;
        assert(prop_node);
        assert(!prop_node->next);

        assert(prop_node->type != NT_CONSTRUCTOR_LITERAL);
        if (prop_node->type == NT_FUNCTION_LITERAL)
            TRY(function_literal(program, prop_node, class_var, KOS_NULL, prototype, &prop));
        else
            TRY(visit_node(program, prop_node, &prop));
        assert(prop);

        TRY(gen_set_prop_instr(program, prop_node, (*reg)->reg, str_idx, prop->reg));

        free_reg(program, prop);
    }

cleanup:
    return error;
}

static int uses_base_proto(KOS_COMP_UNIT *program, const KOS_AST_NODE *node)
{
    KOS_SCOPE *const scope = node->u.scope;
    KOS_FRAME *const frame = (KOS_FRAME *)scope;

    assert(node->is_scope);
    assert(scope);
    assert(scope->has_frame);

    return frame->uses_base_proto;
}

static int find_uses_of_base_proto(KOS_COMP_UNIT *program, const KOS_AST_NODE *node)
{
    int need_base_proto = 0;

    assert(node->type == NT_OBJECT_LITERAL);

    node = node->children;

    for ( ; node; node = node->next) {

        const KOS_AST_NODE *prop_node = node->children;

        assert(node->type == NT_PROPERTY);

        assert(prop_node);
        prop_node = prop_node->next;
        assert(prop_node);
        assert( ! prop_node->next);

        if (prop_node->type == NT_FUNCTION_LITERAL ||
            prop_node->type == NT_CONSTRUCTOR_LITERAL)

            need_base_proto += uses_base_proto(program, prop_node);
    }

    return need_base_proto;
}

static int class_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_VAR            *class_var,
                         KOS_REG           **reg)
{
    int      error          = KOS_SUCCESS;
    int      void_proto     = 0;
    int      ctor_uses_bp   = 0;
    KOS_REG *base_ctor_reg  = KOS_NULL;
    KOS_REG *base_proto_reg = KOS_NULL;
    KOS_REG *proto_reg      = KOS_NULL;

    assert(node->children);
    node = node->children;
    assert(node->next);

    /* Handle 'extends' clause */
    if (node->type != NT_EMPTY && node->type != NT_VOID_LITERAL) {
        TRY(visit_node(program, node, &base_ctor_reg));
        assert(base_ctor_reg);
    }
    void_proto = node->type == NT_VOID_LITERAL;

    node = node->next;
    assert(node->type == NT_OBJECT_LITERAL);
    assert(node->next);

    if (base_ctor_reg) {
        int need_base_proto;

        ctor_uses_bp = uses_base_proto(program, node->next);

        need_base_proto = find_uses_of_base_proto(program, node) + ctor_uses_bp;

        TRY(gen_reg(program, &base_proto_reg));

        /* If this class has no own properties in prototype, we will attach the
         * prototype of the base class directly to this class' constructor */
        if ( ! need_base_proto)
            proto_reg = base_proto_reg;

        TRY(gen_instr2(program, INSTR_GET_PROTO, base_proto_reg->reg, base_ctor_reg->reg));
    }
    else if (void_proto)
        ctor_uses_bp = uses_base_proto(program, node->next);

    /* Build prototype */
    if (node->children || base_ctor_reg || void_proto) {
        if (void_proto) {
            assert( ! base_proto_reg);
            TRY(gen_reg(program, &base_proto_reg));

            TRY(gen_instr1(program, INSTR_LOAD_VOID, base_proto_reg->reg));
        }

        TRY(object_literal(program, node, &proto_reg, class_var, base_proto_reg));
        assert(proto_reg);

        if (void_proto && ! ctor_uses_bp) {
            free_reg(program, base_proto_reg);
            base_proto_reg = KOS_NULL;
        }
    }

    node = node->next;
    assert(node->type == NT_CONSTRUCTOR_LITERAL);
    assert( ! node->next);

    /* Build constructor */
    TRY(function_literal(program,
                         node,
                         class_var,
                         base_ctor_reg,
                         ctor_uses_bp ? base_proto_reg : KOS_NULL,
                         reg));
    assert(*reg);

    /* Set prototype on the constructor */
    if (proto_reg) {

        static const char str_prototype[] = "prototype";
        int               str_idx         = 0;
        KOS_TOKEN         token;

        memset(&token, 0, sizeof(token));
        token.begin  = str_prototype;
        token.length = sizeof(str_prototype) - 1;
        token.type   = TT_IDENTIFIER;

        TRY(gen_str(program, &token, &str_idx));

        TRY(gen_set_prop_instr(program, node, (*reg)->reg, str_idx, proto_reg->reg));
    }

    if (base_ctor_reg)
        free_reg(program, base_ctor_reg);
    if (base_proto_reg && base_proto_reg != base_ctor_reg)
        free_reg(program, base_proto_reg);
    if (proto_reg && proto_reg != base_proto_reg && proto_reg != base_ctor_reg)
        free_reg(program, proto_reg);

cleanup:
    return error;
}

/* For this function and all other similar functions which it invokes, reg is:
    - on input, the desired register in which we prefer the return value
    - on output, the actual register containing the value computed
*/
static int visit_node(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {
        case NT_EMPTY:
            error = KOS_SUCCESS;
            break;
        case NT_IMPORT:
            error = import(program, node);
            break;
        case NT_SCOPE:
            error = process_scope(program, node);
            break;
        case NT_IF:
            error = if_stmt(program, node);
            break;
        case NT_RETURN:
            error = return_stmt(program, node);
            break;
        case NT_YIELD:
            error = yield(program, node, reg);
            break;
        case NT_ASYNC:
            error = async_op(program, node, reg);
            break;
        case NT_THROW:
            error = throw_op(program, node);
            break;
        case NT_ASSERT:
            error = assert_stmt(program, node);
            break;
        case NT_REPEAT:
            error = repeat(program, node);
            break;
        case NT_WHILE:
            error = while_stmt(program, node);
            break;
        case NT_FOR_IN:
            if (is_for_range(program, node))
                error = for_range(program, node);
            else
                error = for_in(program, node);
            break;
        case NT_CONTINUE:
            /* fall through */
        case NT_BREAK:
            /* fall through */
        case NT_FALLTHROUGH:
            error = break_continue_fallthrough(program, node);
            break;
        case NT_SWITCH:
            error = switch_stmt(program, node);
            break;
        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = try_stmt(program, node);
            break;
        case NT_REFINEMENT:
            error = refinement(program, node, reg, KOS_NULL);
            break;
        case NT_SLICE:
            error = slice(program, node, reg);
            break;
        case NT_INVOCATION:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            error = invocation(program, node, reg, INSTR_CALL);
            break;
        case NT_OPERATOR:
            error = process_operator(program, node, reg);
            break;
        case NT_ASSIGNMENT:
            /* fall through */
        case NT_MULTI_ASSIGNMENT:
            error = assignment(program, node);
            break;
        case NT_IDENTIFIER:
            error = identifier(program, node, reg);
            break;
        case NT_NUMERIC_LITERAL:
            error = numeric_literal(program, node, reg);
            break;
        case NT_STRING_LITERAL:
            error = string_literal(program, node, reg);
            break;
        case NT_THIS_LITERAL:
            error = this_literal(program, node, reg);
            break;
        case NT_SUPER_CTOR_LITERAL:
            /* fall through */
        case NT_SUPER_PROTO_LITERAL:
            program->error_token = &node->token;
            program->error_str   = str_err_unexpected_super;
            error = KOS_ERROR_COMPILE_FAILED;
            break;
        case NT_BOOL_LITERAL:
            error = bool_literal(program, node, reg);
            break;
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            error = function_literal(program, node, KOS_NULL, KOS_NULL, KOS_NULL, reg);
            break;
        case NT_ARRAY_LITERAL:
            error = array_literal(program, node, reg);
            break;
        case NT_OBJECT_LITERAL:
            error = object_literal(program, node, reg, KOS_NULL, KOS_NULL);
            break;
        case NT_CLASS_LITERAL:
            error = class_literal(program, node, KOS_NULL, reg);
            break;
        case NT_PLACEHOLDER:
            program->error_token = &node->token;
            program->error_str   = str_err_unexpected_underscore;
            error                = KOS_ERROR_COMPILE_FAILED;
            break;
        case NT_VOID_LITERAL:
            /* fall through */
        default:
            assert(node->type == NT_VOID_LITERAL);
            error = void_literal(program, node, reg);
            break;
    }

    return error;
}

void kos_compiler_init(KOS_COMP_UNIT *program,
                       uint16_t       file_id)
{
    memset(program, 0, sizeof(*program));

    program->optimize = 1;
    program->file_id  = file_id;

    KOS_mempool_init(&program->allocator);

    KOS_vector_init(&program->code_buf);
    KOS_vector_init(&program->code_gen_buf);
    KOS_vector_init(&program->addr2line_buf);
    KOS_vector_init(&program->addr2line_gen_buf);
}

int kos_compiler_compile(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *ast,
                         unsigned      *num_opt_passes)
{
    PROF_ZONE(COMPILER)

    int      error;
    int      num_optimizations;
    unsigned num_passes = 0;
    KOS_REG *reg        = KOS_NULL;

    TRY(KOS_vector_reserve(&program->code_buf,          1024));
    TRY(KOS_vector_reserve(&program->code_gen_buf,      1024));
    TRY(KOS_vector_reserve(&program->addr2line_buf,     1024));
    TRY(KOS_vector_reserve(&program->addr2line_gen_buf, 256));

    TRY(kos_compiler_process_vars(program, ast));

    do {
        num_optimizations = program->num_optimizations;
        TRY(kos_optimize(program, ast));
        ++num_passes;
    }
    while (program->num_optimizations > num_optimizations);

    TRY(kos_allocate_args(program, ast));

    {
        PROF_ZONE_N(COMPILER, generate_code)

        TRY(visit_node(program, ast, &reg));
        assert(!reg);
    }

    if (num_opt_passes)
        *num_opt_passes = num_passes;

cleanup:
    return error;
}

void kos_compiler_destroy(KOS_COMP_UNIT *program)
{
    program->pre_globals = KOS_NULL;

    KOS_vector_destroy(&program->code_gen_buf);
    KOS_vector_destroy(&program->code_buf);
    KOS_vector_destroy(&program->addr2line_gen_buf);
    KOS_vector_destroy(&program->addr2line_buf);

    KOS_mempool_destroy(&program->allocator);
}

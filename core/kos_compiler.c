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
#include "kos_ast.h"
#include "kos_config.h"
#include "kos_disasm.h"
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

static const char str_err_catch_nesting_too_deep[]    = "too many nesting levels of 'try'/'defer'/'with' statements";
static const char str_err_duplicate_property[]        = "duplicate object property";
static const char str_err_expected_refinement[]       = "expected .identifier or '[' in argument to 'delete'";
static const char str_err_expected_refinement_ident[] = "expected identifier";
static const char str_err_invalid_index[]             = "index out of range";
static const char str_err_invalid_numeric_literal[]   = "invalid numeric literal";
static const char str_err_module_dereference[]        = "module is not an object";
static const char str_err_no_such_module_variable[]   = "no such global in module";
static const char str_err_operand_not_numeric[]       = "operand is not a numeric constant";
static const char str_err_operand_not_string[]        = "operand is not a string";
static const char str_err_return_in_generator[]       = "complex return statement in a generator function, return value always ignored";
static const char str_err_too_many_registers[]        = "register capacity exceeded";
static const char str_err_unexpected_super[]          = "'super' cannot be used in this context";

enum KOS_BOOL_E {
    KOS_FALSE_VALUE,
    KOS_TRUE_VALUE
};

static int _visit_node(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg);

static int _invocation(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg,
                       KOS_BYTECODE_INSTR  instr,
                       unsigned            tail_closure_size);

static int _gen_new_reg(KOS_COMP_UNIT *program,
                        KOS_REG      **out_reg)
{
    int        error = KOS_SUCCESS;
    KOS_FRAME *frame = program->cur_frame;
    KOS_REG   *reg;

    assert(frame->num_regs < 256);

    if (frame->num_regs == 255) {

        KOS_TOKEN *token = (KOS_TOKEN *)
            kos_mempool_alloc(&program->allocator, sizeof(KOS_TOKEN));

        if (token) {
            memset(token, 0, sizeof(*token));
            token->begin       = "";
            token->pos.file_id = program->file_id;
            token->pos.line    = 1;
            token->pos.column  = 1;
            token->type        = TT_EOF;

            /* TODO improve either detection or handling of this error */
            program->error_token = token;
            program->error_str   = str_err_too_many_registers;
        }

        return token ? KOS_ERROR_COMPILE_FAILED : KOS_ERROR_OUT_OF_MEMORY;
    }

    if (program->unused_regs) {
        reg                  = program->unused_regs;
        program->unused_regs = reg->next;
    }
    else {
        reg = (KOS_REG *)kos_mempool_alloc(&program->allocator, sizeof(KOS_REG));

        if ( ! reg)
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (reg)
        reg->reg = frame->num_regs++;

    *out_reg = reg;

    return error;
}

static void _mark_reg_as_used(KOS_FRAME *frame,
                              KOS_REG   *reg)
{
    reg->prev = 0;
    reg->next = frame->used_regs;

    if (frame->used_regs)
        frame->used_regs->prev = reg;

    frame->used_regs = reg;
}

static int _gen_reg(KOS_COMP_UNIT *program,
                    KOS_REG      **out_reg)
{
    int error = KOS_SUCCESS;

    if (!*out_reg) {
        KOS_FRAME *frame = program->cur_frame;
        KOS_REG   *reg   = frame->free_regs;

        if ( ! reg) {
            error = _gen_new_reg(program, &reg);
            assert(error || reg);
        }

        if ( ! error) {
            if (frame->free_regs == reg)
                frame->free_regs = reg->next;

            _mark_reg_as_used(frame, reg);

            reg->tmp = 1;

            *out_reg = reg;
        }
    }

    return error;
}

static int _gen_reg_range(KOS_COMP_UNIT *program,
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

            _mark_reg_as_used(frame, reg);

            *(out_reg++) = reg;
            *first_reg   = next;
            reg          = next;
        }
    }

    while (num_regs--) {
        reg = 0;

        error = _gen_new_reg(program, &reg);
        if (error)
            break;

        _mark_reg_as_used(frame, reg);

        reg->tmp = 1;

        *(out_reg++) = reg;
    }

    return error;
}

static int _gen_dest_reg(KOS_COMP_UNIT *program,
                         KOS_REG      **dest,
                         KOS_REG       *src_reg)
{
    int      error    = KOS_SUCCESS;
    KOS_REG *dest_reg = *dest;

    assert(src_reg);

    if ( ! src_reg->tmp && (src_reg == dest_reg || ! dest_reg)) {
        *dest = 0;
        error = _gen_reg(program, dest);
    }
    else if ( ! dest_reg)
        *dest = src_reg;

    return error;
}

static void _free_reg(KOS_COMP_UNIT *program,
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
        reg->prev = 0;
        *reg_ptr  = reg;
    }
}

static void _free_all_regs(KOS_COMP_UNIT *program,
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
static int _count_used_regs(KOS_COMP_UNIT *program)
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
static int _lookup_local_var_even_inactive(KOS_COMP_UNIT      *program,
                                           const KOS_AST_NODE *node,
                                           int                 only_active,
                                           KOS_REG           **reg)
{
    int error = KOS_SUCCESS;

    if (node->is_local_var) {

        KOS_VAR *var = node->var;

        if (var->type & VAR_LOCAL) {

            assert(node->var_scope);

            if (var == node->var_scope->ellipsis) {
                assert(var->is_active);
                assert(var->reg);
                *reg = var->reg;
            }
            else if (var->is_active || ! only_active) {

                if ( ! var->reg) {
                    error = _gen_reg(program, &var->reg);
                    if ( ! error)
                        var->reg->tmp = 0;
                }

                *reg = var->reg;
            }
        }
        else if (var->type & VAR_ARGUMENT_IN_REG) {
            assert(node->var_scope);
            assert(node->var_scope->is_function);
            assert(var->reg);
            *reg = var->reg;
        }
    }

    return error;
}

static int _lookup_local_var(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node,
                             KOS_REG           **reg)
{
    return _lookup_local_var_even_inactive(program, node, 1, reg);
}

static int _lookup_var(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_VAR           **out_var,
                       KOS_REG           **reg)
{
    KOS_VAR *var = node->var;

    if (var && ! var->is_active)
        var = 0;

    if (var) {
        const int  is_var        = var->type == VAR_INDEPENDENT_LOCAL;
        const int  is_arg_in_reg = var->type == VAR_INDEPENDENT_ARG_IN_REG;
        const int  is_local_arg  = node->is_local_var &&
                                   (var->type & (VAR_ARGUMENT | VAR_ARGUMENT_IN_REG));
        KOS_SCOPE *scope         = node->var_scope;
        const int  is_global     = scope->next == 0;

        *out_var = var;

        if (is_local_arg) {
            if (reg) {
                assert((var->type & VAR_ARGUMENT) && ! (var->type & VAR_ARGUMENT_IN_REG));
                assert(program->cur_frame->args_reg);
                *reg = program->cur_frame->args_reg;
            }
        }
        else if (!is_global) {

            KOS_SCOPE_REF *ref;

            assert(is_var ? ( ! scope->is_function || scope->ellipsis == var) : scope->is_function);

            /* Find function scope for this variable */
            while (scope->next && ! scope->is_function)
                scope = scope->next;

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

static void lookup_module_var(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node,
                              KOS_VAR           **out_var)
{
    KOS_REG *reg        = 0;
    KOS_VAR *module_var = 0;

    (void)_lookup_local_var(program, node, &reg);

    if (reg)
        _free_reg(program, reg);
    else if ( ! _lookup_var(program, node, &module_var, 0)) {
        if (module_var->type == VAR_MODULE)
            *out_var = module_var;
    }
}

static int _compare_strings(const char *a, unsigned len_a, KOS_UTF8_ESCAPE escape_a,
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

static void _get_token_str(const KOS_TOKEN *token,
                           const char     **out_begin,
                           unsigned        *out_length,
                           KOS_UTF8_ESCAPE *out_escape)
{
    const char *begin  = token->begin;
    unsigned    length = token->length;

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

static int _numbers_compare_item(void               *what,
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

static int _strings_compare_item(void               *what,
                                 KOS_RED_BLACK_NODE *node)
{
    const KOS_TOKEN       *token = (const KOS_TOKEN       *)what;
    const KOS_COMP_STRING *str   = (const KOS_COMP_STRING *)node;
    const char            *begin;
    unsigned               length;
    KOS_UTF8_ESCAPE        escape;

    if (str->header.type != KOS_COMP_CONST_STRING)
        return (int)KOS_COMP_CONST_STRING < (int)str->header.type ? -1 : 1;

    _get_token_str(token, &begin, &length, &escape);

    return _compare_strings(begin,    length,      escape,
                            str->str, str->length, str->escape);
}

static int _constants_compare_node(KOS_RED_BLACK_NODE *a,
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
            return _compare_strings(str_a->str, str_a->length, str_a->escape,
                                    str_b->str, str_b->length, str_b->escape);
        }

        case KOS_COMP_CONST_FUNCTION:
            return ((const KOS_COMP_FUNCTION *)const_a)->offset
                 < ((const KOS_COMP_FUNCTION *)const_b)->offset
                 ? -1 : 0;

        case KOS_COMP_CONST_PROTOTYPE:
            return const_a->index < const_b->index ? -1 : 0;
    }
}

static void _add_constant(KOS_COMP_UNIT  *program,
                          KOS_COMP_CONST *constant)
{
    constant->index = program->num_constants++;
    constant->next  = 0;

    if (program->last_constant)
        program->last_constant->next = constant;
    else
        program->first_constant = constant;

    program->last_constant = constant;

    kos_red_black_insert(&program->constants,
                          &constant->rb_tree_node,
                          _constants_compare_node);
}

static int _gen_str_esc(KOS_COMP_UNIT   *program,
                        const KOS_TOKEN *token,
                        KOS_UTF8_ESCAPE  escape,
                        int             *str_idx)
{
    int error = KOS_SUCCESS;

    KOS_COMP_STRING *str =
            (KOS_COMP_STRING *)kos_red_black_find(program->constants,
                                                  (KOS_TOKEN *)token,
                                                  _strings_compare_item);

    if (!str) {

        str = (KOS_COMP_STRING *)
            kos_mempool_alloc(&program->allocator, sizeof(KOS_COMP_STRING));

        if (str) {

            const char     *begin;
            unsigned        length;
            KOS_UTF8_ESCAPE tok_escape;

            _get_token_str(token, &begin, &length, &tok_escape);

            if (tok_escape == KOS_UTF8_NO_ESCAPE)
                escape = KOS_UTF8_NO_ESCAPE;

            str->header.type = KOS_COMP_CONST_STRING;
            str->str         = begin;
            str->length      = length;
            str->escape      = escape;

            _add_constant(program, (KOS_COMP_CONST *)str);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (!error)
        *str_idx = str->header.index;

    return error;
}

static int _gen_str(KOS_COMP_UNIT   *program,
                    const KOS_TOKEN *token,
                    int             *str_idx)
{
    return _gen_str_esc(program, token, KOS_UTF8_WITH_ESCAPE, str_idx);
}

static unsigned _calc_assert_str_len(const char *begin,
                                     const char *end)
{
    unsigned length         = 0;
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

static void _get_assert_str(const char *begin,
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

static int _gen_assert_str(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           int                *str_idx)
{
    int            error = KOS_SUCCESS;
    const char    *begin;
    const char    *end;
    char          *buf;
    unsigned       length;
    const unsigned max_length = 64;

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

    length = _calc_assert_str_len(begin, end) + sizeof(assertion_failed) - 1;

    if (length > max_length)
        length = max_length;

    buf = (char *)kos_mempool_alloc(&program->allocator, length);

    if (buf) {

        KOS_TOKEN token;

        memcpy(buf, assertion_failed, sizeof(assertion_failed) - 1);
        _get_assert_str(begin, end, buf + sizeof(assertion_failed) - 1, buf + length);

        memset(&token, 0, sizeof(token));
        token.begin  = buf;
        token.length = length;
        token.type   = TT_IDENTIFIER;

        error = _gen_str_esc(program, &token, KOS_UTF8_NO_ESCAPE, str_idx);
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int _add_addr2line(KOS_COMP_UNIT   *program,
                          const KOS_TOKEN *token,
                          enum KOS_BOOL_E  force)
{
    int                            error;
    KOS_VECTOR                    *addr2line = &program->addr2line_gen_buf;
    struct KOS_COMP_ADDR_TO_LINE_S new_loc;

    new_loc.offs = (uint32_t)program->cur_offs;
    new_loc.line = (uint32_t)token->pos.line;

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

    error = kos_vector_resize(addr2line, addr2line->size + sizeof(new_loc));

    if ( ! error) {

        struct KOS_COMP_ADDR_TO_LINE_S *const last =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (addr2line->buffer + (addr2line->size - sizeof(new_loc)));

        *last = new_loc;
    }

    return error;
}

static int _gen_instr(KOS_COMP_UNIT *program,
                      int            num_args,
                      ...)
{
    int cur_offs = program->cur_offs;
    int error    = kos_vector_resize(&program->code_gen_buf,
                                    (size_t)cur_offs + 1 + 4 * num_args); /* Over-estimate */
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
                for (ibyte=0; ibyte < size; ibyte++) {
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

static int _gen_instr1(KOS_COMP_UNIT *program,
                       int            opcode,
                       int32_t        operand1)
{
    return _gen_instr(program, 1, opcode, operand1);
}

static int _gen_instr2(KOS_COMP_UNIT *program,
                       int            opcode,
                       int32_t        operand1,
                       int32_t        operand2)
{
    return _gen_instr(program, 2, opcode, operand1, operand2);
}

static int _gen_instr3(KOS_COMP_UNIT *program,
                       int            opcode,
                       int32_t        operand1,
                       int32_t        operand2,
                       int32_t        operand3)
{
    return _gen_instr(program, 3, opcode, operand1, operand2, operand3);
}

static int _gen_instr4(KOS_COMP_UNIT *program,
                       int            opcode,
                       int32_t        operand1,
                       int32_t        operand2,
                       int32_t        operand3,
                       int32_t        operand4)
{
    return _gen_instr(program, 4, opcode, operand1, operand2, operand3, operand4);
}

static int _gen_instr5(KOS_COMP_UNIT *program,
                       int            opcode,
                       int32_t        operand1,
                       int32_t        operand2,
                       int32_t        operand3,
                       int32_t        operand4,
                       int32_t        operand5)
{
    return _gen_instr(program, 5, opcode, operand1, operand2, operand3, operand4, operand5);
}

static void _write_jump_offs(KOS_COMP_UNIT *program,
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
           opcode == INSTR_JUMP      ||
           opcode == INSTR_JUMP_COND ||
           opcode == INSTR_JUMP_NOT_COND);

    switch (opcode) {

        case INSTR_JUMP:
            jump_instr_size = 5;
            break;

        default:
            jump_instr_size = 6;
            break;
    }

    jump_offs = target_offs - (jump_instr_offs + jump_instr_size);

    buf += (opcode == INSTR_CATCH) ? 2 : 1;

    for (end = buf+4; buf < end; ++buf) {
        *buf      =   (uint8_t)jump_offs;
        jump_offs >>= 8;
    }
}

static void _update_jump_offs(KOS_COMP_UNIT *program,
                              int            jump_instr_offs,
                              int            target_offs)
{
    assert(jump_instr_offs <  program->cur_offs);
    assert(target_offs     <= program->cur_offs);

    _write_jump_offs(program, &program->code_gen_buf, jump_instr_offs, target_offs);
}

static void _remove_last_instr(KOS_COMP_UNIT *program,
                               int            offs)
{
    --program->cur_frame->num_instr;
    program->cur_offs = offs;
}

int kos_scope_compare_item(void               *what,
                           KOS_RED_BLACK_NODE *node)
{
    const KOS_AST_NODE *scope_node = (const KOS_AST_NODE *)what;
    const KOS_SCOPE    *scope      = (const KOS_SCOPE *)node;

    return (int)((intptr_t)scope_node - (intptr_t)scope->scope_node);
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

static int _free_scope_regs(KOS_RED_BLACK_NODE *node,
                            void               *cookie)
{
    KOS_VAR       *var     = (KOS_VAR *)node;
    KOS_COMP_UNIT *program = (KOS_COMP_UNIT *)cookie;

    if (var->reg && var->type != VAR_INDEPENDENT_LOCAL) {
        var->reg->tmp = 1;
        _free_reg(program, var->reg);
        var->reg = 0;
    }

    return KOS_SUCCESS;
}

static void _pop_scope(KOS_COMP_UNIT *program)
{
    assert(program->scope_stack);

    if (program->scope_stack->vars)
        kos_red_black_walk(program->scope_stack->vars, _free_scope_regs, (void *)program);

    program->scope_stack = program->scope_stack->next;
}

typedef struct KOS_IMPORT_INFO_S {
    KOS_COMP_UNIT *program;
    KOS_FILE_POS   pos;
} KOS_IMPORT_INFO;

static int _import_global(const char *global_name,
                          unsigned    global_length,
                          int         module_idx,
                          int         global_idx,
                          void       *cookie)
{
    int              error = KOS_SUCCESS;
    KOS_IMPORT_INFO *info  = (KOS_IMPORT_INFO *)cookie;
    KOS_REG         *reg   = 0;
    KOS_VAR         *var;
    KOS_TOKEN        token;

    memset(&token, 0, sizeof(token));

    token.begin  = global_name;
    token.length = global_length;
    token.pos    = info->pos;
    token.type   = TT_IDENTIFIER;

    var = kos_find_var(info->program->scope_stack->vars, &token);

    assert(var);
    assert(var->type == VAR_GLOBAL);

    TRY(_gen_reg(info->program, &reg));

    TRY(_gen_instr3(info->program, INSTR_GET_MOD_ELEM, reg->reg, module_idx, global_idx));

    TRY(_gen_instr2(info->program, INSTR_SET_GLOBAL, var->array_idx, reg->reg));

    _free_reg(info->program, reg);

cleanup:
    return error;
}

static int _import(KOS_COMP_UNIT      *program,
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

            info.pos = node->token.pos;

            error = kos_comp_walk_globals(program->ctx,
                                          module_idx,
                                          _import_global,
                                          &info);
        }
        else {

            for ( ; node; node = node->next) {

                int global_idx;

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                error = kos_comp_get_global_idx(program->ctx,
                                                module_idx,
                                                node->token.begin,
                                                node->token.length,
                                                &global_idx);
                if (error) {
                    program->error_token = &node->token;
                    program->error_str   = str_err_no_such_module_variable;
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
                }

                info.pos = node->token.pos;

                TRY(_import_global(node->token.begin,
                                   node->token.length,
                                   module_idx,
                                   global_idx,
                                   &info));
            }
        }
    }

cleanup:
    return error;
}

static int _append_frame(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *name_node,
                         int                 fun_start_offs,
                         size_t              addr2line_start_offs)
{
    int               error;
    const size_t      fun_end_offs = (size_t)program->cur_offs;
    const size_t      fun_size     = fun_end_offs - fun_start_offs;
    const size_t      fun_new_offs = program->code_buf.size;
    const size_t      a2l_size     = program->addr2line_gen_buf.size - addr2line_start_offs;
    size_t            a2l_new_offs = program->addr2line_buf.size;
    int               str_idx      = 0;
    KOS_TOKEN         global;
    static const char str_global[] = "<global>";
    const KOS_TOKEN  *name_token;

    TRY(kos_vector_resize(&program->code_buf, fun_new_offs + fun_size));

    if (a2l_new_offs) {

        struct KOS_COMP_ADDR_TO_LINE_S *last_ptr =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (program->addr2line_buf.buffer + a2l_new_offs);
        --last_ptr;

        if (last_ptr->offs == fun_new_offs)
            a2l_new_offs -= sizeof(struct KOS_COMP_ADDR_TO_LINE_S);
    }

    TRY(kos_vector_resize(&program->addr2line_buf, a2l_new_offs + a2l_size));

    TRY(kos_vector_resize(&program->addr2func_buf,
                          program->addr2func_buf.size + sizeof(struct KOS_COMP_ADDR_TO_FUNC_S)));

    if (name_node) {
        if (name_node->children)
            name_token = &name_node->children->token;
        else
            name_token = program->cur_frame->fun_token;
    }
    else {
        memset(&global, 0, sizeof(global));
        global.begin  = str_global;
        global.length = sizeof(str_global) - 1;
        global.type   = TT_IDENTIFIER;

        name_token = &global;
    }
    TRY(_gen_str(program, name_token, &str_idx));

    memcpy(program->code_buf.buffer + fun_new_offs,
           program->code_gen_buf.buffer + fun_start_offs,
           fun_size);

    TRY(kos_vector_resize(&program->code_gen_buf, (size_t)fun_start_offs));

    program->cur_offs = fun_start_offs;

    program->cur_frame->constant->offset = (uint32_t)fun_new_offs;

    memcpy(program->addr2line_buf.buffer + a2l_new_offs,
           program->addr2line_gen_buf.buffer + addr2line_start_offs,
           a2l_size);

    TRY(kos_vector_resize(&program->addr2line_gen_buf, addr2line_start_offs));

    /* Update addr2line offsets for this function */
    {
        struct KOS_COMP_ADDR_TO_LINE_S *ptr =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (program->addr2line_buf.buffer + a2l_new_offs);
        struct KOS_COMP_ADDR_TO_LINE_S *const end =
            (struct KOS_COMP_ADDR_TO_LINE_S *)
                (program->addr2line_buf.buffer + program->addr2line_buf.size);

        const uint32_t delta = (uint32_t)(fun_new_offs - fun_start_offs);

        for ( ; ptr < end; ptr++)
            ptr->offs += delta;
    }

    {
        KOS_VECTOR *buf = &program->addr2func_buf;

        struct KOS_COMP_ADDR_TO_FUNC_S *ptr =
            (struct KOS_COMP_ADDR_TO_FUNC_S *)
                (buf->buffer + buf->size - sizeof(struct KOS_COMP_ADDR_TO_FUNC_S));

        ptr->offs      = (uint32_t)fun_new_offs;
        if (program->cur_frame->fun_token)
            ptr->line  = program->cur_frame->fun_token->pos.line;
        else
            ptr->line  = 1;
        ptr->str_idx   = (uint32_t)str_idx;
        ptr->num_instr = program->cur_frame->num_instr;
        ptr->code_size = (uint32_t)fun_size;
    }

cleanup:
    return error;
}

static KOS_COMP_FUNCTION *_alloc_func_constant(KOS_COMP_UNIT *program,
                                               KOS_FRAME     *frame)
{
    KOS_COMP_FUNCTION *constant;

    constant = (KOS_COMP_FUNCTION *)kos_mempool_alloc(&program->allocator,
                                                       sizeof(KOS_COMP_FUNCTION));
    if (constant) {

        assert(frame->num_regs < 256);

        constant->header.type = KOS_COMP_CONST_FUNCTION;
        constant->offset      = 0;
        constant->num_regs    = (uint8_t)frame->num_regs;
        constant->args_reg    = 0;
        constant->num_args    = 0;
        constant->flags       = 0;
    }

    return constant;
}

static int _finish_global_scope(KOS_COMP_UNIT *program,
                                KOS_REG       *reg)
{
    int                error;
    KOS_COMP_FUNCTION *constant;

    if ( ! reg) {
        TRY(_gen_reg(program, &reg));
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    TRY(_gen_instr2(program, INSTR_RETURN, program->scope_stack->num_indep_vars, reg->reg));

    _free_reg(program, reg);
    reg = 0;

    constant = _alloc_func_constant(program, program->cur_frame);
    if ( ! constant)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    if (program->scope_stack->num_indep_vars)
        constant->flags |= KOS_COMP_FUN_CLOSURE;

    _add_constant(program, &constant->header);

    program->cur_frame->constant = constant;

    TRY(_append_frame(program, 0, 0, 0));

    assert(program->code_gen_buf.size == 0);

cleanup:
    return error;
}

static int _scope(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node)
{
    int       error  = KOS_SUCCESS;
    const int global = program->scope_stack == 0;

    const KOS_AST_NODE *child = node->children;

    if (child || global) {

        KOS_REG *reg      = 0;
#ifndef NDEBUG
        int      skip_tmp = 0;
#endif

        _push_scope(program, node);

        /* Init global scope */
        if (global) {

            KOS_VAR *var = 0;

            assert(program->scope_stack->has_frame);

            program->cur_frame = (KOS_FRAME *)program->scope_stack;

            /* Generate registers for local (non-global) independent variables */
            for (var = program->scope_stack->fun_vars_list; var; var = var->next)
                if (var->type == VAR_INDEPENDENT_LOCAL) {
                    TRY(_gen_reg(program, &var->reg));
                    var->reg->tmp  = 0;
                    var->array_idx = var->reg->reg;
                }
        }

        /* Process inner nodes */
        for ( ; child; child = child->next) {

#ifndef NDEBUG
            const int initial_used_regs = _count_used_regs(program) - skip_tmp;
#endif

            TRY(_add_addr2line(program, &child->token, KOS_FALSE_VALUE));

            if (reg) {
                _free_reg(program, reg);
                reg = 0;
            }

            TRY(_visit_node(program, child, &reg));

#ifndef NDEBUG
            skip_tmp = (reg && reg->tmp) ? 1 : 0;

            /* TODO: NT_EXPRESSION_LIST is a bit more difficult to check,
             * because it can contain multiple variable declarations and
             * assignments.  For now we skip checking it. */
            if (child->type != NT_EXPRESSION_LIST &&
                    ((child->type != NT_ASSIGNMENT && child->type != NT_MULTI_ASSIGNMENT)
                        || (child->children->type == NT_LEFT_HAND_SIDE))) {

                const int used_regs = _count_used_regs(program);
                assert(used_regs == initial_used_regs + skip_tmp);
            }
#endif
        }

        if (global)
            TRY(_finish_global_scope(program, reg));
        else if (reg)
            _free_reg(program, reg);

        _pop_scope(program);
    }

cleanup:
    return error;
}

static int _if(KOS_COMP_UNIT      *program,
               const KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;
    int offs  = -1;
    int always_truthy;

    KOS_REG *reg = 0;

    TRY(_add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    node = node->children;
    assert(node);

    always_truthy = kos_node_is_truthy(program, node);

    if ( ! always_truthy) {

        TRY(_visit_node(program, node, &reg));
        assert(reg);

        offs = program->cur_offs;
        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, reg->reg));

        _free_reg(program, reg);
        reg = 0;
    }

    node = node->next;
    assert(node);
    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    node = node->next;
    if (node && ! always_truthy) {

        const int jump_offs = program->cur_offs;
        TRY(_gen_instr1(program, INSTR_JUMP, 0));

        assert(offs >= 0);

        _update_jump_offs(program, offs, program->cur_offs);
        offs = jump_offs;

        TRY(_visit_node(program, node, &reg));
        assert(!reg);

        assert(!node->next);
    }

    if (offs >= 0)
        _update_jump_offs(program, offs, program->cur_offs);

cleanup:
    return error;
}

static KOS_SCOPE *_find_try_scope(KOS_SCOPE *scope)
{
    while (scope && ! scope->is_function && ! scope->catch_ref.catch_reg)
        scope = scope->next;

    if (scope && (scope->is_function || ! scope->catch_ref.catch_reg))
        scope = 0;

    return scope;
}

static int _get_closure_size(KOS_COMP_UNIT *program)
{
    int        closure_size;
    KOS_SCOPE *scope = kos_get_frame_scope(program);

    closure_size = scope->num_indep_vars + scope->num_indep_args;

    assert(closure_size <= 255);
    return closure_size;
}

static int _gen_return(KOS_COMP_UNIT *program,
                       int            reg)
{
    int        error = KOS_SUCCESS;
    KOS_SCOPE *scope = _find_try_scope(program->scope_stack);

    while (scope && ! scope->catch_ref.finally_active)
        scope = _find_try_scope(scope->next);

    if (scope) {

        const int return_reg = scope->catch_ref.catch_reg->reg;

        KOS_RETURN_OFFS *const return_offs = (KOS_RETURN_OFFS *)
            kos_mempool_alloc(&program->allocator, sizeof(KOS_RETURN_OFFS));

        if ( ! return_offs)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

        if (reg != return_reg)
            TRY(_gen_instr2(program, INSTR_MOVE, return_reg, reg));

        return_offs->next               = program->cur_frame->return_offs;
        return_offs->offs               = program->cur_offs;
        program->cur_frame->return_offs = return_offs;

        TRY(_gen_instr1(program, INSTR_JUMP, 0));
    }
    else
        TRY(_gen_instr2(program, INSTR_RETURN, _get_closure_size(program), reg));

cleanup:
    return error;
}

static int _is_generator(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *const scope = kos_get_frame_scope(program);

    assert(scope);

    return scope->is_function && ((KOS_FRAME *)scope)->yield_token;
}

static int _return(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node)
{
    int        error;
    KOS_REG   *reg       = 0;
    KOS_SCOPE *try_scope = _find_try_scope(program->scope_stack);
    int        tail_call = 0;

    if (try_scope)
        reg = try_scope->catch_ref.catch_reg;

    if (node->children) {

        if (node->children->type != NT_VOID_LITERAL && _is_generator(program)) {
            program->error_token = &node->token;
            program->error_str   = str_err_return_in_generator;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        if ( ! try_scope && node->children->type == NT_INVOCATION) {
            const int closure_size = _get_closure_size(program);
            tail_call = 1;
            TRY(_invocation(program, node->children, &reg, INSTR_TAIL_CALL, (unsigned)closure_size));
            assert( ! reg);
        }
        else {
            TRY(_visit_node(program, node->children, &reg));
            assert(reg);
        }
    }
    else {
        TRY(_gen_reg(program, &reg));
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    if (tail_call) {
        assert( ! try_scope);
        assert( ! reg);
    }
    else {

        error = _gen_return(program, reg->reg);

        if ( ! try_scope || reg != try_scope->catch_ref.catch_reg)
            _free_reg(program, reg);
    }

cleanup:
    return error;
}

static int _yield(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node,
                  KOS_REG           **reg)
{
    int      error;
    KOS_REG *src = *reg;

    assert(node->children);

    TRY(_visit_node(program, node->children, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    if (src != *reg)
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

    TRY(_gen_instr1(program, INSTR_YIELD, (*reg)->reg));

    if (src != *reg)
        _free_reg(program, src);

cleanup:
    return error;
}

static int _throw(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node)
{
    int      error;
    KOS_REG *reg = 0;

    assert(node->children);

    TRY(_visit_node(program, node->children, &reg));
    assert(reg);

    TRY(_gen_instr1(program, INSTR_THROW, reg->reg));

    _free_reg(program, reg);

cleanup:
    return error;
}

static int _assert_stmt(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node)
{
    int      error;
    int      jump_instr_offs;
    int      str_idx;
    KOS_REG *reg = 0;

    assert(node->children);

    TRY(_visit_node(program, node->children, &reg));
    assert(reg);

    jump_instr_offs = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));

    assert(node->children);
    assert(node->children->next);
    assert(node->children->next->type == NT_LANDMARK);
    assert( ! node->children->next->next);

    _free_reg(program, reg);
    reg = 0;

    TRY(_gen_assert_str(program, node, &str_idx));

    TRY(_gen_reg(program, &reg));

    TRY(_gen_instr2(program,
                    str_idx < 256 ? INSTR_LOAD_CONST8 : INSTR_LOAD_CONST,
                    reg->reg,
                    str_idx));

    TRY(_gen_instr1(program, INSTR_THROW, reg->reg));

    _update_jump_offs(program, jump_instr_offs, program->cur_offs);

    _free_reg(program, reg);

cleanup:
    return error;
}

static void _finish_break_continue(KOS_COMP_UNIT  *program,
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
            _update_jump_offs(program, break_offs->offs,
                    break_offs->type != NT_BREAK
                    ? continue_tgt_offs : break_tgt_offs);

        break_offs = next;
    }

    program->cur_frame->break_offs = old_break_offs;
}

static void _finish_fallthrough(KOS_COMP_UNIT *program)
{
    KOS_BREAK_OFFS **remaining_offs       = &program->cur_frame->break_offs;
    KOS_BREAK_OFFS  *break_offs           = *remaining_offs;
    const int        fallthrough_tgt_offs = program->cur_offs;

    *remaining_offs = 0;

    while (break_offs) {

        KOS_BREAK_OFFS *next = break_offs->next;

        if (break_offs->type == NT_FALLTHROUGH)
            _update_jump_offs(program, break_offs->offs, fallthrough_tgt_offs);
        else {
            break_offs->next = *remaining_offs;
            *remaining_offs  = break_offs;
        }

        break_offs = next;
    }
}

/* Saves last try scope before the loop, used for restoring catch offset */
static KOS_SCOPE *_push_try_scope(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *prev_try_scope = program->cur_frame->last_try_scope;
    KOS_SCOPE *scope          = _find_try_scope(program->scope_stack);

    if (scope)
        program->cur_frame->last_try_scope = scope;

    return prev_try_scope;
}

static int _repeat(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node)
{
    int             error;
    int             jump_instr_offs;
    int             test_instr_offs;
    const int       loop_start_offs = program->cur_offs;
    KOS_REG        *reg             = 0;
    KOS_BREAK_OFFS *old_break_offs  = program->cur_frame->break_offs;
    KOS_SCOPE      *prev_try_scope  = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    TRY(_add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    node = node->next;
    assert(node);

    TRY(_add_addr2line(program, &node->token, KOS_FALSE_VALUE));

    test_instr_offs = program->cur_offs;

    if ( ! kos_node_is_falsy(program, node)) {

        const int is_truthy = kos_node_is_truthy(program, node);

        if ( ! is_truthy) {
            TRY(_visit_node(program, node, &reg));
            assert(reg);
        }

        assert(!node->next);

        jump_instr_offs = program->cur_offs;

        if (reg)
            TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));
        else
            TRY(_gen_instr1(program, INSTR_JUMP, 0));

        _update_jump_offs(program, jump_instr_offs, loop_start_offs);
    }

    _finish_break_continue(program, test_instr_offs, old_break_offs);

    if (reg)
        _free_reg(program, reg);

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int _for(KOS_COMP_UNIT      *program,
                const KOS_AST_NODE *node)
{
    int                 error          = KOS_SUCCESS;
    const KOS_AST_NODE *cond_node      = node->children;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    KOS_SCOPE          *prev_try_scope = _push_try_scope(program);

    assert(cond_node);

    if ( ! kos_node_is_falsy(program, cond_node)) {

        int                 loop_start_offs;
        int                 cond_jump_instr_offs = -1;
        int                 final_jump_instr_offs;
        int                 continue_tgt_offs;
        int                 is_truthy;
        const KOS_AST_NODE *step_node;
        KOS_REG            *reg                  = 0;

        program->cur_frame->break_offs = 0;

        is_truthy = kos_node_is_truthy(program, cond_node);

        if (cond_node->type == NT_EMPTY) {
            assert( ! is_truthy);
            is_truthy = 1;
        }

        if ( ! is_truthy) {

            TRY(_add_addr2line(program, &cond_node->token, KOS_FALSE_VALUE));

            TRY(_visit_node(program, cond_node, &reg));
            assert(reg);

            cond_jump_instr_offs = program->cur_offs;
            TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, reg->reg));

            _free_reg(program, reg);
            reg = 0;
        }

        loop_start_offs = program->cur_offs;

        step_node = cond_node->next;
        assert(step_node);

        node = step_node->next;
        assert(node);
        assert( ! node->next);

        TRY(_visit_node(program, node, &reg));
        assert( ! reg);

        TRY(_add_addr2line(program, &step_node->token, KOS_FALSE_VALUE));

        continue_tgt_offs = program->cur_offs;

        TRY(_visit_node(program, step_node, &reg));
        assert( ! reg);

        if (program->cur_offs == continue_tgt_offs && is_truthy)
            continue_tgt_offs = loop_start_offs;

        if (is_truthy)
            reg = 0;

        else {

            TRY(_add_addr2line(program, &cond_node->token, KOS_FALSE_VALUE));

            TRY(_visit_node(program, cond_node, &reg));
        }

        final_jump_instr_offs = program->cur_offs;

        if (reg) {

            TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));

            _free_reg(program, reg);
            reg = 0;
        }
        else
            TRY(_gen_instr1(program, INSTR_JUMP, 0));

        _update_jump_offs(program, final_jump_instr_offs, loop_start_offs);
        if (cond_jump_instr_offs > -1)
            _update_jump_offs(program, cond_jump_instr_offs, program->cur_offs);

        _finish_break_continue(program, continue_tgt_offs, old_break_offs);
    }

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int _invoke_get_iterator(KOS_COMP_UNIT *program,
                                KOS_REG       **reg)
{
    int               error          = KOS_SUCCESS;
    int               str_idx;
    KOS_REG          *func_reg       = 0;
    KOS_REG          *obj_reg        = *reg;
    KOS_TOKEN         token;
    static const char str_iterator[] = "iterator";

    if ( ! (*reg)->tmp) {
        *reg = 0;
        TRY(_gen_reg(program, reg));
    }

    TRY(_gen_reg(program, &func_reg));

    memset(&token, 0, sizeof(token));
    token.begin  = str_iterator;
    token.length = sizeof(str_iterator) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    TRY(_gen_instr3(program, INSTR_GET_PROP, func_reg->reg, obj_reg->reg, str_idx));

    TRY(_gen_instr5(program, INSTR_CALL_N, (*reg)->reg, func_reg->reg, obj_reg->reg, 255, 0));

    _free_reg(program, func_reg);

cleanup:
    return error;
}

static int _for_in(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node)
{
    int                 error;
    int                 loop_start_offs;
    int                 cond_jump_instr_offs = -1;
    int                 final_jump_instr_offs;
    int                 continue_offs;
    const KOS_AST_NODE *var_node;
    const KOS_AST_NODE *expr_node;
    const KOS_AST_NODE *assg_node;
    KOS_REG            *reg            = 0;
    KOS_REG            *final_reg      = 0;
    KOS_REG            *iter_reg       = 0;
    KOS_REG            *item_reg       = 0;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    KOS_SCOPE          *prev_try_scope = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    _push_scope(program, node);

    assg_node = node->children;
    assert(assg_node);
    assert(assg_node->type == NT_IN);

    var_node = assg_node->children;
    assert(var_node);
    assert(var_node->type == NT_VAR || var_node->type == NT_CONST);

    expr_node = var_node->next;
    assert(expr_node);
    assert(!expr_node->next);

    var_node = var_node->children;
    assert(var_node);

    TRY(_visit_node(program, expr_node, &iter_reg));
    assert(iter_reg);

    kos_activate_new_vars(program, assg_node->children);

    TRY(_invoke_get_iterator(program, &iter_reg));

    TRY(_add_addr2line(program, &assg_node->token, KOS_FALSE_VALUE));

    if ( ! var_node->next) {

        TRY(_lookup_local_var(program, var_node, &item_reg));
        assert(item_reg);
    }
    else
        TRY(_gen_reg(program, &item_reg));

    TRY(_gen_reg(program, &final_reg));

    TRY(_gen_instr3(program, INSTR_CALL_GEN, item_reg->reg, iter_reg->reg, final_reg->reg));

    cond_jump_instr_offs = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, final_reg->reg));

    _free_reg(program, final_reg);

    loop_start_offs = program->cur_offs;

    if (var_node->next) {

        KOS_REG *value_iter_reg = item_reg;

        assert(value_iter_reg->tmp);

        TRY(_invoke_get_iterator(program, &value_iter_reg));

        assert(value_iter_reg == item_reg);

        for ( ; var_node; var_node = var_node->next) {

            KOS_REG *var_reg = 0;

            TRY(_lookup_local_var(program, var_node, &var_reg));
            assert(var_reg);

            TRY(_gen_instr4(program, INSTR_CALL_FUN, var_reg->reg, value_iter_reg->reg, 255, 0));
        }
    }

    node = assg_node->next;
    assert(node);
    assert(!node->next);

    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    TRY(_add_addr2line(program, &assg_node->token, KOS_FALSE_VALUE));

    continue_offs = program->cur_offs;

    TRY(_gen_instr3(program, INSTR_CALL_GEN, item_reg->reg, iter_reg->reg, final_reg->reg));

    final_jump_instr_offs = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, final_reg->reg));

    _update_jump_offs(program, final_jump_instr_offs, loop_start_offs);
    _update_jump_offs(program, cond_jump_instr_offs, program->cur_offs);
    _finish_break_continue(program, continue_offs, old_break_offs);

    final_reg = 0;
    _free_reg(program, item_reg);
    item_reg = 0;
    _free_reg(program, iter_reg);
    iter_reg = 0;

    _pop_scope(program);

    program->cur_frame->last_try_scope = prev_try_scope;

cleanup:
    return error;
}

static int _restore_catch(KOS_COMP_UNIT *program,
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

        error = _gen_instr2(program, INSTR_CATCH, outer_scope->catch_ref.catch_reg->reg, 0);
    }
    else
        error = _gen_instr(program, 0, INSTR_CANCEL);

    return error;
}

static int _restore_parent_scope_catch(KOS_COMP_UNIT *program)
{
    KOS_SCOPE *scope = program->scope_stack;

    assert(scope && ! scope->is_function);

    scope = _find_try_scope(scope->next);

    return _restore_catch(program, scope);
}

static int _push_break_offs(KOS_COMP_UNIT *program,
                            KOS_NODE_TYPE  type)
{
    int                   error      = KOS_SUCCESS;
    KOS_BREAK_OFFS *const break_offs = (KOS_BREAK_OFFS *)
        kos_mempool_alloc(&program->allocator, sizeof(KOS_BREAK_OFFS));

    if (break_offs) {
        break_offs->next               = program->cur_frame->break_offs;
        break_offs->type               = type;
        program->cur_frame->break_offs = break_offs;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int _break_continue_fallthrough(KOS_COMP_UNIT      *program,
                                       const KOS_AST_NODE *node)
{
    int error;

    TRY(_push_break_offs(program, node->type));

    if (program->cur_frame->last_try_scope) {

        _push_scope(program, node);

        TRY(_restore_catch(program, program->cur_frame->last_try_scope));

        _pop_scope(program);
    }

    program->cur_frame->break_offs->offs = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

cleanup:
    return error;
}

typedef struct KOS_SWITCH_CASE_S {
    int to_jump_offs;
    int final_jump_offs;
} KOS_SWITCH_CASE;

static int _count_siblings(const KOS_AST_NODE *node)
{
    int count = 0;

    while (node) {
        ++count;
        node = node->next;
    }

    return count;
}

static int _switch(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node)
{
    int                 error;
    KOS_REG            *value_reg       = 0;
    const KOS_AST_NODE *first_case_node;
    int                 num_cases;
    int                 i_case;
    int                 i_default_case  = -1;
    int                 final_jump_offs = -1;
    KOS_SWITCH_CASE    *cases           = 0;
    KOS_BREAK_OFFS     *old_break_offs  = program->cur_frame->break_offs;

    program->cur_frame->break_offs = 0;

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &value_reg));
    assert(value_reg);

    node = node->next;

    if ( ! node) {
        _free_reg(program, value_reg);
        return error;
    }

    num_cases = _count_siblings(node);

    assert(num_cases);

    cases = (KOS_SWITCH_CASE *)kos_mempool_alloc(
            &program->allocator, sizeof(KOS_SWITCH_CASE) * num_cases);

    if ( ! cases)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    if (node->type == NT_DEFAULT && num_cases == 1) {
        node = node->children;
        assert(node->type == NT_EMPTY);

        node = node->next;
        assert( ! node->next || (node->next->type == NT_FALLTHROUGH && ! node->next->next));

        _free_reg(program, value_reg);
        value_reg = 0;

        if (node->type != NT_FALLTHROUGH) {
            TRY(_visit_node(program, node, &value_reg));
            assert( ! value_reg);
        }

        return error;
    }

    first_case_node = node;

    for (i_case = 0; node; node = node->next, ++i_case) {

        if (node->type == NT_CASE) {

            KOS_REG            *case_reg   = 0;
            KOS_REG            *result_reg = 0;
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

            TRY(_visit_node(program, node->children, &case_reg));
            assert(case_reg);

            if (case_reg->tmp)
                result_reg = case_reg;
            else
                TRY(_gen_reg(program, &result_reg));

            TRY(_gen_instr3(program, INSTR_CMP_EQ, result_reg->reg, value_reg->reg, case_reg->reg));

            cases[i_case].to_jump_offs = program->cur_offs;

            TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, result_reg->reg));

            _free_reg(program, case_reg);
            if (case_reg != result_reg)
                _free_reg(program, result_reg);
        }
        else {

            assert(node->type == NT_DEFAULT);
            assert(node->children);
            assert(node->children->type == NT_EMPTY);

            i_default_case = i_case;
            cases[i_case].to_jump_offs = -1;
        }
    }

    _free_reg(program, value_reg);
    value_reg = 0;

    if (i_default_case >= 0)
        cases[i_default_case].to_jump_offs = program->cur_offs;
    else
        final_jump_offs = program->cur_offs;

    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    node = first_case_node;

    for (i_case = 0; node; node = node->next, ++i_case) {

        const KOS_AST_NODE *child_node = node->children;

        assert(child_node->next);

        child_node = child_node->next;

        assert(cases[i_case].to_jump_offs > 0);

        _update_jump_offs(program, cases[i_case].to_jump_offs, program->cur_offs);

        if (i_case)
            _finish_fallthrough(program);

        cases[i_case].final_jump_offs = -1;

        if (child_node->type != NT_FALLTHROUGH) {

            TRY(_visit_node(program, child_node, &value_reg));
            assert( ! value_reg);

            if ( ! child_node->next) {
                cases[i_case].final_jump_offs = program->cur_offs;

                TRY(_gen_instr1(program, INSTR_JUMP, 0));
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
        _update_jump_offs(program, final_jump_offs, program->cur_offs);

    for (i_case = 0; i_case < num_cases; ++i_case) {

        const int offs = cases[i_case].final_jump_offs;

        if (offs >= 0)
            _update_jump_offs(program, offs, program->cur_offs);
    }

    _finish_break_continue(program, -1, old_break_offs);

cleanup:
    return error;
}

static void _update_child_scope_catch(KOS_COMP_UNIT *program)
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
                _update_jump_offs(program, instr_offs, dest_offs);
        }
    }

    program->scope_stack->catch_ref.child_scopes = 0;
}

static int _try_stmt(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node)
{
    int                 error;
    int                 catch_offs;
    KOS_REG            *except_reg     = 0;
    KOS_VAR            *except_var     = 0;
    KOS_RETURN_OFFS    *return_offs    = program->cur_frame->return_offs;
    KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    const KOS_NODE_TYPE node_type      = node->type;
    const KOS_AST_NODE *try_node       = node->children;
    const KOS_AST_NODE *catch_node     = 0;
    const KOS_AST_NODE *defer_node     = 0;
    KOS_SCOPE          *scope;

    scope = _push_scope(program, node);

    program->cur_frame->break_offs = 0;

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

        except_var = variable->var;
        assert(except_var);
        assert(except_var == kos_find_var(program->scope_stack->vars, &variable->token));

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(_lookup_local_var(program, variable, &except_reg));
        assert(except_reg);

        except_var->is_active = VAR_INACTIVE;

        scope->catch_ref.catch_reg = except_reg;
    }
    else {

        defer_node = try_node->next;

        assert(node_type == NT_TRY_DEFER);
        assert(defer_node->type == NT_SCOPE);

        TRY(_gen_reg(program, &except_reg));

        scope->catch_ref.catch_reg = except_reg;

        scope->catch_ref.finally_active = 1;
        program->cur_frame->return_offs = 0;

        TRY(_gen_instr1(program, INSTR_LOAD_VOID, except_reg->reg));
    }

    /* Try section */

    catch_offs = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_CATCH, except_reg->reg, 0));

    assert(try_node->type == NT_SCOPE);
    TRY(_scope(program, try_node));

    /* We're done with the try scope, prevent _find_try_scope() from finding this
     * catch target again when inside catch or defer clause. */
    scope->catch_ref.catch_reg = 0;

    /* Catch section */

    if (node_type == NT_TRY_CATCH) {

        int jump_end_offs;

        TRY(_restore_parent_scope_catch(program));

        jump_end_offs = program->cur_offs;
        TRY(_gen_instr1(program, INSTR_JUMP, 0));

        _update_child_scope_catch(program);

        _update_jump_offs(program, catch_offs, program->cur_offs);

        TRY(_restore_parent_scope_catch(program));

        node = node->next;
        assert(node);
        assert(!node->next);
        assert(node->type == NT_SCOPE);

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(_scope(program, node));

        except_var->is_active = VAR_INACTIVE;

        _update_jump_offs(program, jump_end_offs, program->cur_offs);
    }

    /* Defer section */

    else {

        int             skip_throw_offs;
        KOS_BREAK_OFFS *try_break_offs = program->cur_frame->break_offs;

        program->cur_frame->break_offs = old_break_offs;
        old_break_offs                 = 0;

        {
            KOS_RETURN_OFFS *tmp            = program->cur_frame->return_offs;
            program->cur_frame->return_offs = return_offs;
            return_offs                     = tmp;
            scope->catch_ref.finally_active = 0;
        }

        _update_child_scope_catch(program);

        _update_jump_offs(program, catch_offs, program->cur_offs);

        TRY(_restore_parent_scope_catch(program));

        TRY(_scope(program, defer_node));

        skip_throw_offs = program->cur_offs;

        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, except_reg->reg));

        TRY(_gen_instr1(program, INSTR_THROW, except_reg->reg));

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
                        _update_jump_offs(program, break_offs->offs, program->cur_offs);

                TRY(_restore_parent_scope_catch(program));

                TRY(_scope(program, defer_node));

                TRY(_push_break_offs(program, type));
                program->cur_frame->break_offs->offs = program->cur_offs;

                TRY(_gen_instr1(program, INSTR_JUMP, 0));
            }
        }

        /* Defer section for return statement */

        if (return_offs) {

            for ( ; return_offs; return_offs = return_offs->next)
                _update_jump_offs(program, return_offs->offs, program->cur_offs);

            TRY(_restore_parent_scope_catch(program));

            TRY(_scope(program, defer_node));

            TRY(_gen_return(program, except_reg->reg));
        }

        _update_jump_offs(program, skip_throw_offs, program->cur_offs);
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

    _free_reg(program, except_reg);

    _pop_scope(program);

cleanup:
    return error;
}

static KOS_REG *super_prototype(KOS_COMP_UNIT      *program,
                                const KOS_AST_NODE *node)
{
    assert(program->cur_frame->base_proto_reg);

    return program->cur_frame->base_proto_reg;
}

static int _refinement_module(KOS_COMP_UNIT      *program,
                              KOS_VAR            *module_var,
                              const KOS_AST_NODE *node, /* the second child of the refinement node */
                              KOS_REG           **reg)
{
    int        error;
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    if (node->type == NT_STRING_LITERAL) {

        int             global_idx;
        const char     *begin;
        unsigned        length;
        KOS_UTF8_ESCAPE escape;

        /* TODO this does not work for escaped strings, kos_comp_get_global_idx assumes NO_ESCAPE */
        _get_token_str(&node->token, &begin, &length, &escape);

        error = kos_comp_get_global_idx(program->ctx, module_var->array_idx, begin, length, &global_idx);
        if (error) {
            program->error_token = &node->token;
            program->error_str   = str_err_no_such_module_variable;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(_gen_reg(program, reg));

        TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, (*reg)->reg, module_var->array_idx, global_idx));
    }
    else {

        KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_dest_reg(program, reg, prop));

        TRY(_gen_instr3(program, INSTR_GET_MOD, (*reg)->reg, module_var->array_idx, prop->reg));

        if (*reg != prop)
            _free_reg(program, prop);
    }

cleanup:
    kos_vector_destroy(&cstr);

    return error;
}

static int _maybe_int(const KOS_AST_NODE *node,
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

static int _refinement_object(KOS_COMP_UNIT      *program,
                              const KOS_AST_NODE *node,
                              KOS_REG           **reg, /* the first child of the refinement node */
                              KOS_REG           **out_obj)
{
    int      error;
    KOS_REG *obj = 0;
    int64_t  idx;

    if (node->type == NT_SUPER_PROTO_LITERAL)
        obj = super_prototype(program, node);
    else
        TRY(_visit_node(program, node, &obj));
    assert(obj);

    if (out_obj) {
        *out_obj = obj;
        TRY(_gen_reg(program, reg));
    }
    else
        TRY(_gen_dest_reg(program, reg, obj));

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {

        int str_idx;
        TRY(_gen_str(program, &node->token, &str_idx));

        TRY(_gen_instr3(program, INSTR_GET_PROP, (*reg)->reg, obj->reg, str_idx));
    }
    else if (_maybe_int(node, &idx)) {

        if (idx > INT_MAX || idx < INT_MIN) {
            program->error_token = &node->token;
            program->error_str   = str_err_invalid_index;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(_gen_instr3(program, INSTR_GET_ELEM, (*reg)->reg, obj->reg, (int)idx));
    }
    else {

        KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_instr3(program, INSTR_GET, (*reg)->reg, obj->reg, prop->reg));

        _free_reg(program, prop);
    }

    if (obj != *reg && ! out_obj)
        _free_reg(program, obj);

cleanup:
    return error;
}

static int _refinement(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg,
                       KOS_REG           **out_obj)
{
    int      error;
    KOS_VAR *module_var = 0;

    node = node->children;
    assert(node);

    if (node->type == NT_IDENTIFIER)
        lookup_module_var(program, node, &module_var);

    if (module_var) {

        node = node->next;
        assert(node);
        assert(!node->next);

        error = _refinement_module(program, module_var, node, reg);
    }
    else
        error = _refinement_object(program, node, reg, out_obj);

    return error;
}

static int _maybe_refinement(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node,
                             KOS_REG           **reg,
                             KOS_REG           **out_obj)
{
    int error;

    assert(out_obj);

    if (node->type == NT_REFINEMENT)
        error = _refinement(program, node, reg, out_obj);
    else {
        error = _visit_node(program, node, reg);
        *out_obj = 0;
    }

    assert(error || *reg);

    return error;
}

static int _slice(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node,
                  KOS_REG           **reg)
{
    int      error;
    KOS_REG *obj_reg   = 0;
    KOS_REG *begin_reg = 0;
    KOS_REG *end_reg   = 0;

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    node = node->next;
    assert(node);

    TRY(_visit_node(program, node, &begin_reg));
    assert(begin_reg);

    node = node->next;
    assert(node);
    assert( ! node->next);

    TRY(_visit_node(program, node, &end_reg));
    assert(end_reg);

    if ( ! *reg) {
        if (obj_reg->tmp)
            *reg = obj_reg;
        else
            TRY(_gen_reg(program, reg));
    }

    TRY(_gen_instr4(program, INSTR_GET_RANGE, (*reg)->reg, obj_reg->reg,
                                              begin_reg->reg, end_reg->reg));

    _free_reg(program, end_reg);
    _free_reg(program, begin_reg);
    if (obj_reg != *reg)
        _free_reg(program, obj_reg);

cleanup:
    return error;
}

static int _find_var_by_reg(KOS_RED_BLACK_NODE *node,
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

static int _is_var_used(KOS_COMP_UNIT      *program,
                        const KOS_AST_NODE *node,
                        KOS_REG            *reg)
{
    if ( ! reg || reg->tmp)
        return 0;

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            KOS_SCOPE *scope;

            for (scope = program->scope_stack; scope && scope->next; scope = scope->next) {

                const int error = kos_red_black_walk(scope->vars, _find_var_by_reg, reg);

                if (error == KOS_SUCCESS_RETURN)
                    return 1;

                if (scope->is_function)
                    break;
            }
        }

        if (_is_var_used(program, node->children, reg))
            return 1;
    }

    return 0;
}

static int _count_non_expanded_siblings(const KOS_AST_NODE *node)
{
    int count = 0;

    while (node && node->type != NT_EXPAND) {
        ++count;
        node = node->next;
    }

    return count;
}

#define MAX_CONTIG_REGS 4

static int _count_contig_arg_siblings(const KOS_AST_NODE *node)
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

static int _gen_array(KOS_COMP_UNIT      *program,
                      const KOS_AST_NODE *node,
                      KOS_REG           **reg)
{
    int       error;
    int       i;
    const int num_fixed = _count_non_expanded_siblings(node);

    if (_is_var_used(program, node, *reg))
        *reg = 0;

    TRY(_gen_reg(program, reg));
    if (num_fixed < 256)
        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, (*reg)->reg, num_fixed));
    else
        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY, (*reg)->reg, num_fixed));

    for (i = 0; node; node = node->next, ++i) {

        KOS_REG  *arg    = 0;
        const int expand = node->type == NT_EXPAND ? 1 : 0;

        if (expand) {
            assert(node->children);
            assert( ! node->children->next);
            assert(node->children->type != NT_EXPAND);
            assert(i >= num_fixed);
            TRY(_visit_node(program, node->children, &arg));
        }
        else
            TRY(_visit_node(program, node, &arg));

        assert(arg);

        if (i < num_fixed)
            TRY(_gen_instr3(program, INSTR_SET_ELEM, (*reg)->reg, i, arg->reg));
        else if (expand)
            TRY(_gen_instr2(program, INSTR_PUSH_EX, (*reg)->reg, arg->reg));
        else
            TRY(_gen_instr2(program, INSTR_PUSH, (*reg)->reg, arg->reg));

        _free_reg(program, arg);
    }

cleanup:
    return error;
}

static int super_invocation(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    static const char   str_apply[]   = "apply";
    int                 error         = KOS_SUCCESS;
    int                 str_idx       = 0;
    KOS_TOKEN           token;
    KOS_REG            *args_regs[2]  = { 0, 0 };
    KOS_REG            *apply_fun     = 0;
    KOS_REG            *base_ctor_reg;
    const KOS_AST_NODE *inv_node      = node;

    TRY(_gen_reg_range(program, &args_regs[0], 2));

    if (node) {
        node = node->children;

        assert(node);
        assert(node->type == NT_SUPER_CTOR_LITERAL);
        node = node->next;

        TRY(_gen_array(program, node, &args_regs[1]));
    }
    else
        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, args_regs[1]->reg, 0));

    memset(&token, 0, sizeof(token));
    token.begin  = str_apply;
    token.length = sizeof(str_apply) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    assert(program->cur_frame->base_ctor_reg);
    base_ctor_reg = program->cur_frame->base_ctor_reg;

    if (reg && _is_var_used(program, inv_node, *reg)) {
        TRY(_gen_reg(program, &apply_fun));
        assert(apply_fun);
        assert(*reg);
    }
    else {
        if ( ! reg || ! *reg) {
            TRY(_gen_reg(program, &apply_fun));
            if (reg)
                *reg = apply_fun;
        }
        else
            apply_fun = *reg;
    }

    TRY(_gen_instr3(program, INSTR_GET_PROP, apply_fun->reg, base_ctor_reg->reg, str_idx));

    assert(program->cur_frame->this_reg);
    TRY(_gen_instr2(program, INSTR_MOVE, args_regs[0]->reg, program->cur_frame->this_reg->reg));

    TRY(_gen_instr5(program, INSTR_CALL_N, apply_fun->reg, apply_fun->reg, base_ctor_reg->reg, args_regs[0]->reg, 2));

cleanup:
    if (args_regs[0])
        _free_reg(program, args_regs[0]);
    if (args_regs[1])
        _free_reg(program, args_regs[1]);
    if (apply_fun && ( ! reg || apply_fun != *reg))
        _free_reg(program, apply_fun);

    return error;
}

static int _invocation(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg,
                       KOS_BYTECODE_INSTR  instr,
                       unsigned            tail_closure_size)
{
    int      error;
    KOS_REG *args;
    KOS_REG *obj        = 0;
    KOS_REG *fun        = 0;
    int32_t  rdest      = (int32_t)tail_closure_size;
    int      interp_str = node->type == NT_INTERPOLATED_STRING;
    int      num_contig_args;

    assert(tail_closure_size <= 255U);
    assert(node->children);

    if (node->children->type == NT_SUPER_CTOR_LITERAL) {
        assert(instr == INSTR_CALL);
        assert(tail_closure_size == 0);
        return super_invocation(program, node, reg);
    }

    args = _is_var_used(program, node, *reg) ? 0 : *reg;

    node = node->children;

    if (interp_str) {

        static const char str_string[] = "stringify";
        int               string_idx   = 0;

        error = kos_comp_get_global_idx(program->ctx, 0, str_string, sizeof(str_string)-1,
                                        &string_idx);
        if (error) {
            program->error_token = &node->token;
            program->error_str   = str_err_no_such_module_variable;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(_gen_reg(program, &fun));

        TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, fun->reg, 0, string_idx));
    }
    else {

        TRY(_maybe_refinement(program, node, &fun, &obj));

        node = node->next;
    }

    num_contig_args = _count_contig_arg_siblings(node);

    if (num_contig_args <= MAX_CONTIG_REGS) {

        KOS_REG *argn[MAX_CONTIG_REGS] = { 0, 0, 0, 0 };
        int      i;

        if (num_contig_args > 1)
            TRY(_gen_reg_range(program, &argn[0], num_contig_args));

        for (i = 0; node; node = node->next, i++) {

            KOS_REG *arg = argn[i];

            assert(i == 0 || arg);
            assert(i == 0 || arg->reg == argn[i-1]->reg + 1);

            TRY(_visit_node(program, node, &arg));
            assert(arg);

            if ( ! argn[i]) {
                assert(num_contig_args == 1);
                argn[i] = arg;
            }
            else if (arg != argn[i]) {
                assert( ! arg->tmp);
                TRY(_gen_instr2(program, INSTR_MOVE, argn[i]->reg, arg->reg));
            }
        }

        /* TODO ignore moves if all args are existing, contiguous registers */

        if (instr == INSTR_CALL) {
            if ( ! *reg) {
                for (i = 0; i < num_contig_args; i++) {
                    if (argn[i]->tmp) {
                        *reg = argn[i];
                        break;
                    }
                }
            }
            if ( ! *reg)
                TRY(_gen_reg(program, reg));

            rdest = (*reg)->reg;
        }

        if (obj) {
            instr = (instr == INSTR_CALL) ? INSTR_CALL_N : INSTR_TAIL_CALL_N;

            TRY(_gen_instr5(program, instr, rdest, fun->reg, obj->reg,
                            num_contig_args ? argn[0]->reg : 255,
                            num_contig_args));
        }
        else {
            instr = (instr == INSTR_CALL) ? INSTR_CALL_FUN : INSTR_TAIL_CALL_FUN;

            TRY(_gen_instr4(program, instr, rdest, fun->reg,
                            num_contig_args ? argn[0]->reg : 255,
                            num_contig_args));
        }

        while (num_contig_args) {
            KOS_REG *arg = argn[--num_contig_args];
            if (arg != *reg)
                _free_reg(program, arg);
        }
    }
    else {

        TRY(_gen_array(program, node, &args));

        if ( ! *reg && instr == INSTR_CALL)
            *reg = args;

        if ( ! obj) {
            TRY(_gen_reg(program, &obj));
            TRY(_gen_instr1(program, INSTR_LOAD_VOID, obj->reg));
        }

        if (instr == INSTR_CALL)
            rdest = (*reg)->reg;

        TRY(_gen_instr4(program, instr, rdest, fun->reg, obj->reg, args->reg));

        if (args != *reg)
            _free_reg(program, args);
    }

    _free_reg(program, fun);
    if (obj)
        _free_reg(program, obj);

cleanup:
    return error;
}

static int _async(KOS_COMP_UNIT      *program,
                  const KOS_AST_NODE *node,
                  KOS_REG           **reg)
{
    int               error       = KOS_SUCCESS;
    KOS_REG          *argn[2]     = { 0, 0 };
    KOS_REG          *fun         = 0;
    KOS_REG          *async       = 0;
    KOS_REG          *obj;
    int               str_idx;
    KOS_TOKEN         token;
    static const char str_async[] = "async";

    node = node->children;
    assert(node);
    assert( ! node->next);
    assert(node->type == NT_INVOCATION);

    TRY(_gen_reg_range(program, &argn[0], 2));

    obj = argn[0];

    if (*reg)
        fun = *reg;

    node = node->children;
    assert(node);

    TRY(_maybe_refinement(program, node, &fun, &obj));

    node = node->next;

    TRY(_gen_array(program, node, &argn[1]));

    memset(&token, 0, sizeof(token));
    token.begin  = str_async;
    token.length = sizeof(str_async) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    if (*reg && fun != *reg)
        async = *reg;

    if (obj && obj != argn[0]) {
        TRY(_gen_instr2(program, INSTR_MOVE, argn[0]->reg, obj->reg));
        _free_reg(program, obj);
    }

    TRY(_gen_reg(program, &async));

    if ( ! *reg)
        *reg = async;

    TRY(_gen_instr3(program, INSTR_GET_PROP, async->reg, fun->reg, str_idx));

    if ( ! obj)
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, argn[0]->reg));

    TRY(_gen_instr5(program, INSTR_CALL_N, (*reg)->reg, async->reg, fun->reg, argn[0]->reg, 2));

    if (fun != *reg)
        _free_reg(program, fun);
    if (async != *reg)
        _free_reg(program, async);
    _free_reg(program, argn[0]);
    _free_reg(program, argn[1]);

cleanup:
    return error;
}

enum CHECK_TYPE_E {
    CHECK_NUMERIC           = 1,
    CHECK_STRING            = 2,
    CHECK_NUMERIC_OR_STRING = 3
};

static int _check_const_literal(KOS_COMP_UNIT      *program,
                                const KOS_AST_NODE *node,
                                enum CHECK_TYPE_E   expected_type)
{
    KOS_NODE_TYPE       cur_node_type;
    const KOS_AST_NODE *const_node = kos_get_const(program, node);

    if ( ! const_node)
        return KOS_SUCCESS;

    cur_node_type = const_node->type;

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

static int _pos_neg(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node,
                    KOS_REG           **reg)
{
    int                     error;
    const KOS_OPERATOR_TYPE op  = node->token.op;
    KOS_REG                *src = *reg;

    assert(op == OT_ADD || op == OT_SUB);

    node = node->children;
    assert(node);
    assert(!node->next);

    TRY(_check_const_literal(program, node, CHECK_NUMERIC));

    TRY(_visit_node(program, node, &src));
    assert(src);

    if (op == OT_SUB) {

        KOS_REG *val = 0;

        TRY(_gen_dest_reg(program, reg, src));

        TRY(_gen_reg(program, &val));
        TRY(_gen_instr2(program, INSTR_LOAD_INT8, val->reg, 0));

        TRY(_gen_instr3(program,
                        INSTR_SUB,
                        (*reg)->reg,
                        val->reg,
                        src->reg));

        _free_reg(program, val);

        if (src != *reg)
            _free_reg(program, src);
    }
    else
        /* TODO: enforce numeric */
        *reg = src;

cleanup:
    return error;
}

static int _log_not(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node,
                    KOS_REG           **reg)
{
    int      error;
    int      offs1;
    int      offs2;
    KOS_REG *src = *reg;

    node = node->children;
    assert(node);
    assert(!node->next);

    TRY(_visit_node(program, node, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    offs1 = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, src->reg));

    TRY(_gen_instr1(program, INSTR_LOAD_TRUE, (*reg)->reg));

    offs2 = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    _update_jump_offs(program, offs1, program->cur_offs);

    TRY(_gen_instr1(program, INSTR_LOAD_FALSE, (*reg)->reg));

    _update_jump_offs(program, offs2, program->cur_offs);

    if (src != *reg)
        _free_reg(program, src);

cleanup:
    return error;
}

static int _log_and_or(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg)
{
    int                     error;
    int                     left_offs;
    int                     right_offs = 0;
    const KOS_OPERATOR_TYPE op         = node->token.op;
    KOS_REG                *left       = 0;
    KOS_REG                *right      = 0;

    assert(op == OT_LOGAND || op == OT_LOGOR);

    node = node->children;
    assert(node);
    assert(node->next);

    TRY(_visit_node(program, node, &left));
    assert(left);

    node = node->next;
    assert(node);
    assert(!node->next);

    left_offs = program->cur_offs;

    TRY(_gen_instr2(program,
                    (op == OT_LOGAND) ? INSTR_JUMP_NOT_COND : INSTR_JUMP_COND,
                    0,
                    left->reg));

    if (left->tmp) {
        right = left;

        if ( ! *reg)
            *reg = left;
    }

    TRY(_visit_node(program, node, &right));
    assert(right);

    if ( ! *reg) {
        if (right->tmp)
            *reg = right;
        else
            TRY(_gen_reg(program, reg));
    }

    if (*reg != right && left != right) {
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, right->reg));
        _free_reg(program, right);
    }

    if (*reg != left && left != right) {
        right_offs = program->cur_offs;

        TRY(_gen_instr1(program, INSTR_JUMP, 0));
    }

    _update_jump_offs(program, left_offs, program->cur_offs);

    if (*reg != left) {
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, left->reg));
        _free_reg(program, left);
    }

    if (right_offs)
        _update_jump_offs(program, right_offs, program->cur_offs);

cleanup:
    return error;
}

static int _log_tri(KOS_COMP_UNIT      *program,
                    const KOS_AST_NODE *node,
                    KOS_REG           **reg)
{
    int      error;
    int      offs1;
    int      offs2;
    int      offs3;
    int      offs4;
    KOS_REG *cond_reg = 0;
    KOS_REG *src      = *reg;

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &cond_reg));
    assert(cond_reg);

    offs1 = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, cond_reg->reg));

    _free_reg(program, cond_reg);

    node = node->next;
    assert(node);

    offs2 = program->cur_offs;
    TRY(_visit_node(program, node, &src));
    assert(src);

    if (program->cur_offs != offs2 || src != *reg) {

        if (src != *reg) {
            if ( ! *reg)
                TRY(_gen_dest_reg(program, reg, src));

            TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

            if (src != *reg) {
                _free_reg(program, src);
                src = *reg;
            }
        }

        offs3 = program->cur_offs;
        TRY(_gen_instr1(program, INSTR_JUMP, 0));

        _update_jump_offs(program, offs1, program->cur_offs);
    }
    else {
        _remove_last_instr(program, offs1);
        offs3 = offs1;
        TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, cond_reg->reg));
    }

    node = node->next;
    assert(node);
    assert(!node->next);

    offs4 = program->cur_offs;
    TRY(_visit_node(program, node, &src));

    if (program->cur_offs != offs4 || src != *reg) {

        if (src != *reg) {
            TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));
            _free_reg(program, src);
        }

        _update_jump_offs(program, offs3, program->cur_offs);
    }
    else {
        _remove_last_instr(program, offs3);
        if (offs3 > offs1)
            _update_jump_offs(program, offs1, program->cur_offs);
    }

cleanup:
    return error;
}

static int _has_prop(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node,
                     KOS_REG           **reg)
{
    int      error;
    int      str_idx;
    KOS_REG *src = *reg;

    TRY(_visit_node(program, node->children, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    TRY(_gen_str(program, &node->children->next->token, &str_idx));

    TRY(_gen_instr3(program, INSTR_HAS_PROP, (*reg)->reg, src->reg, str_idx));

    if (src != *reg)
        _free_reg(program, src);

cleanup:
    return error;
}

static int _delete(KOS_COMP_UNIT      *program,
                   const KOS_AST_NODE *node,
                   KOS_REG           **reg)
{
    int      error;
    KOS_REG *obj = 0;

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

    TRY(_visit_node(program, node, &obj));
    assert(obj);

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {

        int str_idx;
        TRY(_gen_str(program, &node->token, &str_idx));

        TRY(_gen_instr2(program, INSTR_DEL_PROP, obj->reg, str_idx));
    }
    else if (node->type == NT_NUMERIC_LITERAL) {

        program->error_token = &node->token;
        program->error_str   = str_err_expected_refinement_ident;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }
    else {

        KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_instr2(program, INSTR_DEL, obj->reg, prop->reg));

        _free_reg(program, prop);
    }

    _free_reg(program, obj);

    TRY(_gen_reg(program, reg));
    TRY(_gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg));

cleanup:
    return error;
}

static int _operator(KOS_COMP_UNIT      *program,
                     const KOS_AST_NODE *node,
                     KOS_REG           **reg)
{
    int                     error    = KOS_SUCCESS;
    const KOS_OPERATOR_TYPE op       = node->token.op;
    const KOS_KEYWORD_TYPE  kw       = node->token.keyword;
    KOS_REG                *reg1     = 0;
    KOS_REG                *reg2     = 0;
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
            return _log_not(program, node, reg);

        case OT_LOGAND:
            /* fall through */
        case OT_LOGOR:
            return _log_and_or(program, node, reg);

        case OT_LOGTRI:
            return _log_tri(program, node, reg);

        case OT_NONE:
            switch (kw) {

                case KW_TYPEOF:
                    opcode   = INSTR_TYPE;
                    operands = 1;
                    break;

                case KW_DELETE:
                    return _delete(program, node, reg);

                case KW_IN:
                    {
                        const KOS_AST_NODE *second = node->children->next;
                        if (second && second->type == NT_STRING_LITERAL)
                            return _has_prop(program, node, reg);
                    }
                    opcode   = INSTR_HAS;
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
                return _pos_neg(program, node, reg);
            opcode   = INSTR_ADD;
            operands = 2;
            break;

        case OT_SUB:
            if (!node->children->next)
                return _pos_neg(program, node, reg);
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
                        const KOS_NODE_TYPE a_type = const_a->type;
                        const KOS_NODE_TYPE b_type = const_b->type;

                        if (a_type == NT_STRING_LITERAL ||
                            (a_type != NT_NUMERIC_LITERAL && b_type == NT_STRING_LITERAL)) {
                            TRY(_check_const_literal(program, node,       CHECK_STRING));
                            TRY(_check_const_literal(program, node->next, CHECK_STRING));
                        }
                        else {
                            TRY(_check_const_literal(program, node,       CHECK_NUMERIC));
                            TRY(_check_const_literal(program, node->next, CHECK_NUMERIC));
                        }
                    }
                    else
                        TRY(_check_const_literal(program, node, CHECK_NUMERIC_OR_STRING));
                }
                else
                    TRY(_check_const_literal(program, node->next, CHECK_NUMERIC_OR_STRING));

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
            TRY(_check_const_literal(program, node, CHECK_NUMERIC));

            if (node->next)
                TRY(_check_const_literal(program, node->next, CHECK_NUMERIC));
            break;

        default:
            break;
    }

    TRY(_visit_node(program, node, &reg1));
    assert(reg1);

    node = node->next;
    if (operands == 2) {

        assert(node);

        TRY(_visit_node(program, node, &reg2));
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
            TRY(_gen_reg(program, reg));
    }

    if (operands == 2) {
        if (swap) {
            KOS_REG *tmp = reg1;
            reg1         = reg2;
            reg2         = tmp;
        }
        error = _gen_instr3(program, opcode, (*reg)->reg, reg1->reg, reg2->reg);
    }
    else {
        assert( ! swap);
        error = _gen_instr2(program, opcode, (*reg)->reg, reg1->reg);
    }

    if (*reg != reg1)
        _free_reg(program, reg1);
    if (reg2 && *reg != reg2)
        _free_reg(program, reg2);

cleanup:
    return error;
}

static int _assign_instr(KOS_OPERATOR_TYPE op)
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

static int _assign_member(KOS_COMP_UNIT      *program,
                          KOS_OPERATOR_TYPE   assg_op,
                          const KOS_AST_NODE *node,
                          KOS_REG            *src)
{
    int      error;
    int      str_idx = 0;
    int64_t  idx     = 0;
    KOS_REG *obj     = 0;
    KOS_REG *tmp_reg = 0;

    assert(node->type == NT_REFINEMENT);

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &obj));
    assert(obj);

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL) {
        TRY(_gen_str(program, &node->token, &str_idx));

        if (assg_op != OT_SET) {

            TRY(_gen_reg(program, &tmp_reg));

            TRY(_gen_instr3(program, INSTR_GET_PROP, tmp_reg->reg, obj->reg, str_idx));

            TRY(_gen_instr3(program, _assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(_gen_instr3(program, INSTR_SET_PROP, obj->reg, str_idx, src->reg));
    }
    else if (_maybe_int(node, &idx)) {

        assert(node->type == NT_NUMERIC_LITERAL);

        if (idx > INT_MAX || idx < INT_MIN) {
            program->error_token = &node->token;
            program->error_str   = str_err_invalid_index;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        if (assg_op != OT_SET) {

            TRY(_gen_reg(program, &tmp_reg));

            TRY(_gen_instr3(program, INSTR_GET_ELEM, tmp_reg->reg, obj->reg, (int)idx));

            TRY(_gen_instr3(program, _assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(_gen_instr3(program, INSTR_SET_ELEM, obj->reg, (int)idx, src->reg));
    }
    else {

        KOS_REG *prop    = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        if (assg_op != OT_SET) {

            TRY(_gen_reg(program, &tmp_reg));

            TRY(_gen_instr3(program, INSTR_GET, tmp_reg->reg, obj->reg, prop->reg));

            TRY(_gen_instr3(program, _assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

            src = tmp_reg;
        }

        TRY(_gen_instr3(program, INSTR_SET, obj->reg, prop->reg, src->reg));

        _free_reg(program, prop);
    }

    if (tmp_reg)
        _free_reg(program, tmp_reg);

    _free_reg(program, obj);

cleanup:
    return error;
}

static int _assign_non_local(KOS_COMP_UNIT      *program,
                             KOS_OPERATOR_TYPE   assg_op,
                             const KOS_AST_NODE *node,
                             KOS_REG            *src)
{
    int      error;
    KOS_VAR *var     = 0;
    KOS_REG *tmp_reg = 0;
    KOS_REG *container_reg;

    assert(node->type == NT_IDENTIFIER);

    TRY(_lookup_var(program, node, &var, &container_reg));

    assert(var->type != VAR_LOCAL);
    assert(var->type != VAR_ARGUMENT_IN_REG);
    assert(var->type != VAR_MODULE);

    if (assg_op != OT_SET) {

        TRY(_gen_reg(program, &tmp_reg));

        if (var->type == VAR_GLOBAL)
            TRY(_gen_instr2(program, INSTR_GET_GLOBAL, tmp_reg->reg, var->array_idx));
        else
            TRY(_gen_instr3(program, INSTR_GET_ELEM, tmp_reg->reg, container_reg->reg, var->array_idx));

        TRY(_gen_instr3(program, _assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

        src = tmp_reg;
    }

    if (var->type == VAR_GLOBAL)
        TRY(_gen_instr2(program, INSTR_SET_GLOBAL, var->array_idx, src->reg));
    else
        TRY(_gen_instr3(program, INSTR_SET_ELEM, container_reg->reg, var->array_idx, src->reg));

    if (tmp_reg)
        _free_reg(program, tmp_reg);

cleanup:
    return error;
}

static int _assign_slice(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_REG            *src)
{
    int                 error;
    int                 str_idx;
    const KOS_AST_NODE *obj_node     = 0;
    KOS_REG            *argn[3]      = { 0, 0, 0 };
    KOS_REG            *obj_reg      = 0;
    KOS_REG            *func_reg     = 0;
    const int           src_reg      = src->reg;
    static const char   str_insert[] = "insert";
    KOS_TOKEN           token;

    _free_reg(program, src);

    TRY(_gen_reg_range(program, &argn[0], 3));

    if (src_reg != argn[2]->reg)
        TRY(_gen_instr2(program, INSTR_MOVE, argn[2]->reg, src_reg));

    node = node->children;
    assert(node);

    obj_node = node;
    node     = node->next;
    assert(node);

    obj_reg = argn[0];
    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    if (obj_reg != argn[0]) {
        TRY(_gen_instr2(program, INSTR_MOVE, argn[0]->reg, obj_reg->reg));
        _free_reg(program, obj_reg);
    }

    node = node->next;
    assert(node);
    assert( ! node->next);

    obj_reg = argn[1];
    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    if (obj_reg != argn[1]) {
        TRY(_gen_instr2(program, INSTR_MOVE, argn[1]->reg, obj_reg->reg));
        _free_reg(program, obj_reg);
    }

    obj_reg = 0;
    TRY(_visit_node(program, obj_node, &obj_reg));
    assert(obj_reg);

    memset(&token, 0, sizeof(token));
    token.begin  = str_insert;
    token.length = sizeof(str_insert) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    TRY(_gen_reg(program, &func_reg));

    TRY(_gen_instr3(program, INSTR_GET_PROP, func_reg->reg, obj_reg->reg, str_idx));

    TRY(_gen_instr5(program, INSTR_CALL_N, func_reg->reg, func_reg->reg, obj_reg->reg, argn[0]->reg, 3));

    _free_reg(program, argn[2]);
    _free_reg(program, argn[1]);
    _free_reg(program, argn[0]);
    _free_reg(program, func_reg);
    _free_reg(program, obj_reg);

cleanup:
    return error;
}

static int _assignment(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *assg_node)
{
    int                 error;
    int                 is_lhs;
    const KOS_AST_NODE *node;
    const KOS_AST_NODE *rhs_node;
    KOS_REG            *reg       = 0;
    KOS_REG            *rhs       = 0;
    const KOS_NODE_TYPE node_type = assg_node->type;

    assert(node_type == NT_ASSIGNMENT || node_type == NT_MULTI_ASSIGNMENT);

    node = assg_node->children;
    assert(node);

    rhs_node = node->next;
    assert(rhs_node);
    assert(!rhs_node->next);

    assert(node->type == NT_LEFT_HAND_SIDE ||
           node->type == NT_VAR ||
           node->type == NT_CONST);

    kos_activate_self_ref_func(program, node);

    is_lhs = (node->type == NT_LEFT_HAND_SIDE) ? 1 : 0;

    assert(is_lhs ||
            (node->children &&
                (node->children->type == NT_IDENTIFIER ||
                 node->children->type == NT_VOID_LITERAL)));

    node = node->children;
    assert(node);

    if (node_type == NT_ASSIGNMENT) {

        assert( ! node->next);
        assert(node->type != NT_VOID_LITERAL);

        if (assg_node->token.op != OT_SET)
            /* TODO check lhs variable type */
            TRY(_check_const_literal(program, rhs_node,
                    assg_node->token.op == OT_SETADD ? CHECK_NUMERIC_OR_STRING : CHECK_NUMERIC));

        if (node->type == NT_IDENTIFIER)
            TRY(_lookup_local_var_even_inactive(program, node, is_lhs, &reg));

        if (reg && assg_node->token.op == OT_SET)
            rhs = reg;
    }

    TRY(_visit_node(program, rhs_node, &rhs));
    assert(rhs);

    if (node_type == NT_MULTI_ASSIGNMENT)
        TRY(_invoke_get_iterator(program, &rhs));

    for ( ; node; node = node->next) {

        if ( ! reg && node->type == NT_IDENTIFIER)
            TRY(_lookup_local_var_even_inactive(program, node, is_lhs, &reg));

        if (reg) {

            if (assg_node->token.op == OT_SET) {

                if (node_type == NT_MULTI_ASSIGNMENT) {

                    assert(reg != rhs);

                    TRY(_gen_instr4(program, INSTR_CALL_FUN, reg->reg, rhs->reg, 255, 0));
                }
                else {

                    if (rhs != reg) {
                        TRY(_gen_instr2(program, INSTR_MOVE, reg->reg, rhs->reg));
                        _free_reg(program, rhs);
                    }
                }
            }
            else {

                assert(node_type == NT_ASSIGNMENT);

                TRY(_gen_instr3(program, _assign_instr(assg_node->token.op), reg->reg, reg->reg, rhs->reg));

                _free_reg(program, rhs);
            }

            if ( ! is_lhs)
                kos_activate_var(program, node);
        }
        else {

            if ( ! is_lhs)
                kos_activate_var(program, node);

            if (node_type == NT_MULTI_ASSIGNMENT) {

                TRY(_gen_reg(program, &reg));

                TRY(_gen_instr4(program, INSTR_CALL_FUN, reg->reg, rhs->reg, 255, 0));
            }
            else
                reg = rhs;

            if (node->type == NT_REFINEMENT)
                TRY(_assign_member(program, assg_node->token.op, node, reg));

            else if (node->type == NT_IDENTIFIER)
                TRY(_assign_non_local(program, assg_node->token.op, node, reg));

            else if (node->type != NT_VOID_LITERAL) {

                assert(node->type == NT_SLICE);
                assert(assg_node->token.op == OT_SET);
                TRY(_assign_slice(program, node, reg));
                reg = 0; /* _assign_slice frees the register */
            }

            if (reg)
                _free_reg(program, reg);
        }

        reg = 0;
    }

    if (node_type == NT_MULTI_ASSIGNMENT)
        _free_reg(program, rhs);

cleanup:
    return error;
}

static int _expression_list(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    int error = KOS_SUCCESS;

    node = node->children;

    for ( ; node; node = node->next) {

        KOS_REG *tmp_reg = 0;

        error = _add_addr2line(program, &node->token, KOS_FALSE_VALUE);
        if (error)
            break;

        error = _visit_node(program, node, &tmp_reg);
        if (error)
            break;
        if (tmp_reg)
            _free_reg(program, tmp_reg);
    }

    return error;
}

static int _identifier(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg)
{
    int      error   = KOS_SUCCESS;
    KOS_REG *src_reg = 0;

    TRY(_lookup_local_var(program, node, &src_reg));

    if (src_reg)
        *reg = src_reg;

    else {

        KOS_VAR *var           = 0;
        KOS_REG *container_reg = 0;

        TRY(_gen_reg(program, reg));

        TRY(_lookup_var(program, node, &var, &container_reg));

        assert(var->type != VAR_LOCAL);
        assert(var->type != VAR_ARGUMENT_IN_REG);

        switch (var->type) {

            case VAR_GLOBAL:
                TRY(_gen_instr2(program, INSTR_GET_GLOBAL, (*reg)->reg, var->array_idx));
                break;

            case VAR_MODULE:
                program->error_token = &node->token;
                program->error_str   = str_err_module_dereference;
                error                = KOS_ERROR_COMPILE_FAILED;
                break;

            default:
                assert(container_reg);
                TRY(_gen_instr3(program, INSTR_GET_ELEM, (*reg)->reg, container_reg->reg, var->array_idx));
                break;
        }
    }

cleanup:
    return error;
}

static int _numeric_literal(KOS_COMP_UNIT      *program,
                            const KOS_AST_NODE *node,
                            KOS_REG           **reg)
{
    KOS_NUMERIC numeric;
    int         error;

    TRY(_gen_reg(program, reg));

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
    else if (numeric.type == KOS_INTEGER_VALUE &&
             (uint64_t)((numeric.u.i >> 7) + 1) <= 1U)
        TRY(_gen_instr2(program, INSTR_LOAD_INT8, (*reg)->reg, (int32_t)numeric.u.i));
    else {

        KOS_COMP_CONST *constant =
            (KOS_COMP_CONST *)kos_red_black_find(program->constants,
                                                 &numeric,
                                                 _numbers_compare_item);

        if ( ! constant) {
            constant = (KOS_COMP_CONST *)
                kos_mempool_alloc(&program->allocator,
                                  KOS_max(sizeof(KOS_COMP_INTEGER),
                                          sizeof(KOS_COMP_FLOAT)));

            if ( ! constant)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            if (numeric.type == KOS_INTEGER_VALUE) {
                constant->type = KOS_COMP_CONST_INTEGER;
                ((KOS_COMP_INTEGER *)constant)->value = numeric.u.i;
            }
            else {
                constant->type = KOS_COMP_CONST_FLOAT;
                ((KOS_COMP_FLOAT *)constant)->value = numeric.u.d;
            }

            _add_constant(program, constant);
        }

        TRY(_gen_instr2(program,
                        constant->index < 256 ? INSTR_LOAD_CONST8 : INSTR_LOAD_CONST,
                        (*reg)->reg,
                        (int32_t)constant->index));
    }

cleanup:
    return error;
}

static int _string_literal(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           KOS_REG           **reg)
{
    int error;
    int str_idx;

    error = _gen_str(program, &node->token, &str_idx);

    if (!error) {
        error = _gen_reg(program, reg);

        if (!error)
            error = _gen_instr2(program,
                                str_idx < 256 ? INSTR_LOAD_CONST8 : INSTR_LOAD_CONST,
                                (*reg)->reg,
                                str_idx);
    }

    return error;
}

static int _this_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_REG           **reg)
{
    assert(program->cur_frame->this_reg);
    *reg = program->cur_frame->this_reg;
    return KOS_SUCCESS;
}

static int _bool_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_REG           **reg)
{
    int error = _gen_reg(program, reg);

    const int opcode = node->token.keyword == KW_TRUE ? INSTR_LOAD_TRUE : INSTR_LOAD_FALSE;

    if (!error)
        error = _gen_instr1(program, opcode, (*reg)->reg);

    return error;
}

static int _void_literal(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         KOS_REG           **reg)
{
    int error = _gen_reg(program, reg);

    if (!error)
        error = _gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg);

    return error;
}

typedef struct KOS_GEN_CLOSURE_ARGS_S {
    KOS_COMP_UNIT *program;
    int           *num_binds;
} KOS_GEN_CLOSURE_ARGS;

static int _gen_closure_regs(KOS_RED_BLACK_NODE *node,
                             void               *cookie)
{
    int error = KOS_SUCCESS;

    KOS_SCOPE_REF        *ref  = (KOS_SCOPE_REF *)node;
    KOS_GEN_CLOSURE_ARGS *args = (KOS_GEN_CLOSURE_ARGS *)cookie;

    if (ref->exported_locals) {
        ++*(args->num_binds);
        error = _gen_reg(args->program, &ref->vars_reg);
        if ( ! error) {
            ref->vars_reg->tmp = 0;
            ref->vars_reg_idx  = ref->vars_reg->reg;
        }
    }

    if ( ! error && ref->exported_args) {
        ++*(args->num_binds);
        error = _gen_reg(args->program, &ref->args_reg);
        if ( ! error) {
            ref->args_reg->tmp = 0;
            ref->args_reg_idx  = ref->args_reg->reg;
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

static int _gen_binds(KOS_RED_BLACK_NODE *node,
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
            TRY(_gen_instr2(program,
                            INSTR_BIND_SELF,
                            args->func_reg->reg,
                            ref->vars_reg_idx - delta));
        else {

            KOS_SCOPE_REF *other_ref =
                    kos_find_scope_ref(args->parent_frame, ref->closure);

            TRY(_gen_instr3(program,
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

            src_reg = other_ref->args_reg_idx;
        }

        TRY(_gen_instr3(program,
                        INSTR_BIND,
                        args->func_reg->reg,
                        ref->args_reg_idx - delta,
                        src_reg));
    }

cleanup:
    return error;
}

static int _free_arg_regs(KOS_RED_BLACK_NODE *node,
                          void               *cookie)
{
    KOS_VAR       *var     = (KOS_VAR *)node;
    KOS_COMP_UNIT *program = (KOS_COMP_UNIT *)cookie;

    if ((var->type & VAR_ARGUMENT_IN_REG) && var->reg->tmp) {
        _free_reg(program, var->reg);
        var->reg = 0;
    }

    return KOS_SUCCESS;
}

static int _gen_function(KOS_COMP_UNIT      *program,
                         const KOS_AST_NODE *node,
                         int                 needs_super_ctor,
                         KOS_SCOPE         **out_scope)
{
    int        error        = KOS_SUCCESS;
    KOS_SCOPE *scope;
    KOS_FRAME *frame;
    KOS_FRAME *last_frame   = program->cur_frame;
    KOS_VAR   *var;
    KOS_REG   *scope_reg    = 0;
    KOS_REG   *ellipsis_reg = 0;
    int        fun_start_offs;
    size_t     addr2line_start_offs;
#ifndef NDEBUG
    int        last_reg     = -1;
#endif

    const KOS_AST_NODE *fun_node  = node;
    const KOS_AST_NODE *open_node;
    const KOS_AST_NODE *name_node = fun_node->children;
    KOS_COMP_FUNCTION  *constant;

    scope = (KOS_SCOPE *)kos_red_black_find(program->scopes, (void *)node, kos_scope_compare_item);
    frame = (KOS_FRAME *)scope;

    *out_scope = scope;

    if (frame->constant)
        return KOS_SUCCESS;

    _push_scope(program, node);

    frame->fun_token    = &fun_node->token;
    frame->parent_frame = last_frame;
    program->cur_frame  = frame;

    /* Generate registers for local independent variables */
    for (var = scope->fun_vars_list; var; var = var->next)
        if (var->type == VAR_INDEPENDENT_LOCAL) {
            TRY(_gen_reg(program, &var->reg));
            var->reg->tmp  = 0;
            var->array_idx = var->reg->reg;
            assert(var->reg->reg == ++last_reg);
        }

    /* Generate registers for function arguments */
    if (scope->num_args) {

        KOS_AST_NODE *arg_node  = fun_node->children;
        int           rest_used = 0;
        int           i;

        assert(arg_node);
        assert(arg_node->type == NT_NAME || arg_node->type == NT_NAME_CONST);
        arg_node = arg_node->next;
        assert(arg_node->type == NT_PARAMETERS);
        arg_node = arg_node->children;

        for (i = 0; arg_node && arg_node->type != NT_ELLIPSIS; arg_node = arg_node->next, ++i) {

            KOS_AST_NODE *ident_node =
                arg_node->type == NT_IDENTIFIER ? arg_node : arg_node->children;

            var = ident_node->var;
            assert(var);
            assert(var == kos_find_var(scope->vars, &ident_node->token));

            if (arg_node->type == NT_IDENTIFIER)
                ++frame->num_non_def_args;
            else
                ++frame->num_def_args;

            if (var->type & VAR_ARGUMENT_IN_REG) {

                assert( ! var->reg);
                TRY(_gen_reg(program, &var->reg));

                assert(var->reg->reg == ++last_reg);

                if (var->num_reads || var->num_assignments)
                    var->reg->tmp = 0;

                var->array_idx = (var->type & VAR_INDEPENDENT) ? var->reg->reg : 0;
            }
            else if (var->num_reads || var->num_assignments) {
                assert(scope->have_rest);
                rest_used = 1;
            }
        }

        /* Generate register for the remaining args */
        if (scope->have_rest) {
            TRY(_gen_reg(program, &frame->args_reg));
            if (rest_used)
                frame->args_reg->tmp = 0;
            assert(frame->args_reg->reg == ++last_reg);
        }
    }

    /* Generate register for ellipsis */
    if (scope->ellipsis) {
        if (scope->ellipsis->type == VAR_INDEPENDENT_LOCAL) {
            assert(scope->ellipsis->reg);
            TRY(_gen_reg(program, &ellipsis_reg));
            assert(ellipsis_reg->reg == ++last_reg);
        }
        else {
            assert( ! scope->ellipsis->reg);
            TRY(_gen_reg(program, &scope->ellipsis->reg));
            scope->ellipsis->reg->tmp = 0;
            assert(scope->ellipsis->reg->reg == ++last_reg);
        }
    }

    /* Generate register for 'this' */
    TRY(_gen_reg(program, &frame->this_reg));
    assert(frame->this_reg->reg == ++last_reg);
    frame->bind_delta = frame->this_reg->reg + 1;
    if (scope->uses_this)
        frame->this_reg->tmp = 0;

    /* Generate registers for closures */
    {
        KOS_GEN_CLOSURE_ARGS args;

        /* Base class constructor */
        if (needs_super_ctor) {
            TRY(_gen_reg(program, &frame->base_ctor_reg));
            frame->base_ctor_reg->tmp = 0;
            ++frame->num_binds;
        }

        /* Base class prototype */
        if (frame->uses_base_proto) {
            TRY(_gen_reg(program, &frame->base_proto_reg));
            frame->base_proto_reg->tmp = 0;
            ++frame->num_binds;
        }

        /* Closures from outer scopes */
        args.program   = program;
        args.num_binds = &frame->num_binds;
        TRY(kos_red_black_walk(frame->closures, _gen_closure_regs, &args));
    }

    assert(name_node);
    assert(name_node->type == NT_NAME || name_node->type == NT_NAME_CONST);
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

    TRY(_add_addr2line(program, &open_node->token, KOS_TRUE_VALUE));

    /* Move ellipsis into place */
    if (ellipsis_reg) {
        TRY(_gen_instr2(program, INSTR_MOVE, scope->ellipsis->reg->reg, ellipsis_reg->reg));
        _free_reg(program, ellipsis_reg);
        ellipsis_reg = 0;
    }

    /* Release unused registers */
    if (frame->args_reg && frame->args_reg->tmp) {
        _free_reg(program, frame->args_reg);
        frame->args_reg = 0;
    }
    if (frame->this_reg->tmp) {
        _free_reg(program, frame->this_reg);
        frame->this_reg = 0;
    }
    if (scope->num_args)
        TRY(kos_red_black_walk(scope->vars, _free_arg_regs, program));

    /* Invoke super constructor if not invoked explicitly */
    if (needs_super_ctor && ! frame->uses_base_ctor)
        TRY(super_invocation(program, 0, 0));

    /* Generate code for function body */
    TRY(_visit_node(program, node, &scope_reg));
    assert(!scope_reg);

    /* Create constant template for LOAD.CONST */
    constant = _alloc_func_constant(program, frame);
    if ( ! constant)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    constant->args_reg = (uint8_t)scope->num_indep_vars;
    constant->num_args = (uint8_t)frame->num_non_def_args;
    constant->flags    = scope->ellipsis ? KOS_COMP_FUN_ELLIPSIS : 0;

    if (fun_node->type == NT_CONSTRUCTOR_LITERAL)
        constant->flags |= KOS_COMP_FUN_CLASS;
    else if (frame->yield_token)
        constant->flags |= KOS_COMP_FUN_GENERATOR;

    if (scope->num_indep_vars || scope->num_indep_args)
        constant->flags |= KOS_COMP_FUN_CLOSURE;

    _add_constant(program, &constant->header);

    frame->constant = constant;

    /* Create constant placeholder for class prototype */
    if (fun_node->type == NT_CONSTRUCTOR_LITERAL) {

        KOS_COMP_CONST *proto_const = (KOS_COMP_CONST *)
                kos_mempool_alloc(&program->allocator, sizeof(KOS_COMP_CONST));

        if ( ! proto_const)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

        proto_const->type = KOS_COMP_CONST_PROTOTYPE;

        _add_constant(program, proto_const);
    }

    /* Choose instruction for loading the function */
    frame->load_instr =
        (fun_node->type == NT_CONSTRUCTOR_LITERAL  ||
         scope->num_args > frame->num_non_def_args ||
         frame->num_binds)
            ? (constant->header.index < 256 ? INSTR_LOAD_FUN8   : INSTR_LOAD_FUN)
            : (constant->header.index < 256 ? INSTR_LOAD_CONST8 : INSTR_LOAD_CONST);

    /* Move the function code to final code_buf */
    TRY(_append_frame(program, name_node, fun_start_offs, addr2line_start_offs));

    program->cur_frame = last_frame;

    TRY(_add_addr2line(program, &fun_node->token, KOS_FALSE_VALUE));

    program->cur_frame = frame;
    _pop_scope(program);
    program->cur_frame = last_frame;

    /* Free register objects */
    _free_all_regs(program, frame->used_regs);
    _free_all_regs(program, frame->free_regs);
    frame->used_regs = 0;
    frame->free_regs = 0;

cleanup:
    return error;
}

static int _function_literal(KOS_COMP_UNIT      *program,
                             const KOS_AST_NODE *node,
                             KOS_REG            *base_ctor_reg,
                             KOS_REG            *base_proto_reg,
                             KOS_REG           **reg)
{
    int        error = KOS_SUCCESS;
    KOS_SCOPE *scope;
    KOS_FRAME *frame;
    KOS_VAR   *fun_var;

    const KOS_AST_NODE *fun_node  = node;
    const KOS_AST_NODE *name_node = fun_node->children;

    /* Generate code for the function */
    TRY(_gen_function(program, node, base_ctor_reg != 0, &scope));

    assert(scope->has_frame);
    frame = (KOS_FRAME *)scope;
    assert(frame->constant);
    assert( ! frame->uses_base_ctor || base_ctor_reg);
    assert( ! base_ctor_reg || frame->num_binds);

    /* Generate LOAD.CONST/LOAD.FUN instruction in the parent frame */
    assert(frame->num_regs > 0);
    assert(frame->num_regs >= frame->bind_delta);
    TRY(_gen_reg(program, reg));

    TRY(_gen_instr2(program,
                    frame->load_instr,
                    (*reg)->reg,
                    (int32_t)frame->constant->header.index));

    /* Generate BIND instructions in the parent frame */
    if (frame->num_binds) {
        KOS_BIND_ARGS bind_args;

        int bind_idx = 0;

        /* Bind base class constructor */
        if (base_ctor_reg)
            TRY(_gen_instr3(program,
                            INSTR_BIND,
                            (*reg)->reg,
                            bind_idx++,
                            base_ctor_reg->reg));

        /* Bind base class prototype */
        if (frame->uses_base_proto)
            TRY(_gen_instr3(program,
                            INSTR_BIND,
                            (*reg)->reg,
                            bind_idx++,
                            base_proto_reg->reg));

        /* Binds for outer scopes */
        bind_args.program      = program;
        bind_args.func_reg     = *reg;
        bind_args.parent_frame = program->cur_frame;
        bind_args.delta        = frame->bind_delta;
        TRY(kos_red_black_walk(frame->closures, _gen_binds, &bind_args));
    }

    /* Find the first default arg */
    if (frame->num_def_args) {
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

    /* Disable variable to which the function is assigned to prevent it from
     * being used by the argument defaults. */
    fun_var = 0;
    if (name_node->type == NT_NAME_CONST) {
        assert(name_node->children);
        assert(name_node->children->type == NT_IDENTIFIER);
        assert(name_node->children->token.type == TT_IDENTIFIER);

        fun_var = name_node->children->var;
        assert(fun_var);
        assert((fun_var->type & VAR_LOCAL) || fun_var->type == VAR_GLOBAL);
        assert(fun_var == kos_find_var(program->scope_stack->vars, &name_node->children->token));

        if ((fun_var->type & VAR_LOCAL) && fun_var->is_active)
            fun_var->is_active = VAR_INACTIVE;
        else
            fun_var = 0;
    }

    /* Generate array with default args */
    if (scope->num_args > frame->num_non_def_args) {

        int      i;
        KOS_REG *defaults_reg = 0;

        TRY(_gen_reg(program, &defaults_reg));

        if (frame->num_def_args < 256)
            TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, defaults_reg->reg, frame->num_def_args));
        else
            TRY(_gen_instr2(program, INSTR_LOAD_ARRAY, defaults_reg->reg, frame->num_def_args));

        for (i = 0; node && node->type == NT_ASSIGNMENT; node = node->next, ++i) {

            const KOS_AST_NODE *def_node = node->children;
            KOS_REG            *arg = 0;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            TRY(_visit_node(program, def_node, &arg));
            assert(arg);

            TRY(_gen_instr3(program, INSTR_SET_ELEM, defaults_reg->reg, i, arg->reg));

            _free_reg(program, arg);
        }

        TRY(_gen_instr2(program, INSTR_BIND_DEFAULTS, (*reg)->reg, defaults_reg->reg));

        _free_reg(program, defaults_reg);
    }
    /* Generate code for unused non-constant defaults */
    else if (frame->num_def_args) {

        for ( ; node && node->type == NT_ASSIGNMENT; node = node->next) {

            const KOS_AST_NODE *def_node = node->children;
            KOS_NODE_TYPE       type;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            def_node = kos_get_const(program, def_node);

            if ( ! def_node)
                continue;

            type = def_node->type;

            if (type != NT_IDENTIFIER      &&
                type != NT_NUMERIC_LITERAL &&
                type != NT_STRING_LITERAL  &&
                type != NT_THIS_LITERAL    &&
                type != NT_LINE_LITERAL    &&
                type != NT_BOOL_LITERAL    &&
                type != NT_VOID_LITERAL) {

                KOS_REG *out = 0;

                TRY(_visit_node(program, def_node, &out));
                assert(out);

                _free_reg(program, out);
            }
        }
    }

    if (fun_var)
        fun_var->is_active = VAR_ACTIVE;

cleanup:
    return error;
}

static int _array_literal(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg)
{
    KOS_REG *array_reg = *reg;

    int error = _gen_array(program, node->children, &array_reg);

    if ( ! error) {
        if ( ! *reg)
            *reg = array_reg;
        else if (array_reg != *reg) {
            error = _gen_instr2(program, INSTR_MOVE, (*reg)->reg, array_reg->reg);
            _free_reg(program, array_reg);
        }
    }

    return error;
}

typedef struct KOS_OBJECT_PROP_DUPE_S {
    KOS_RED_BLACK_NODE rb_tree_node;
    int                str_idx;
} KOS_OBJECT_PROP_DUPE;

static int _prop_compare_item(void               *what,
                              KOS_RED_BLACK_NODE *node)
{
    const int             str_idx   = (int)(intptr_t)what;
    KOS_OBJECT_PROP_DUPE *prop_node = (KOS_OBJECT_PROP_DUPE *)node;

    return str_idx - prop_node->str_idx;
}

static int _prop_compare_node(KOS_RED_BLACK_NODE *a,
                              KOS_RED_BLACK_NODE *b)
{
    KOS_OBJECT_PROP_DUPE *a_node = (KOS_OBJECT_PROP_DUPE *)a;
    KOS_OBJECT_PROP_DUPE *b_node = (KOS_OBJECT_PROP_DUPE *)b;

    return a_node->str_idx - b_node->str_idx;
}

static int _object_literal(KOS_COMP_UNIT      *program,
                           const KOS_AST_NODE *node,
                           KOS_REG           **reg,
                           KOS_REG            *prototype)
{
    int                 error;
    KOS_RED_BLACK_NODE *prop_str_idcs = 0;

    if (prototype) {
        if ( ! *reg || (*reg == prototype && ! prototype->tmp)) {
            *reg = 0;
            TRY(_gen_reg(program, reg));
        }

        TRY(_gen_instr2(program, INSTR_LOAD_OBJ_PROTO, (*reg)->reg, prototype->reg));
    }
    else {
        TRY(_gen_reg(program, reg));
        TRY(_gen_instr1(program, INSTR_LOAD_OBJ, (*reg)->reg));
    }

    assert(*reg);

    for (node = node->children; node; node = node->next) {

        int                 str_idx;
        const KOS_AST_NODE *prop_node = node->children;
        KOS_REG            *prop      = 0;

        assert(node->type == NT_PROPERTY);
        assert(prop_node);
        assert(prop_node->type == NT_STRING_LITERAL);

        TRY(_gen_str(program, &prop_node->token, &str_idx));

        if (kos_red_black_find(prop_str_idcs, (void *)(intptr_t)str_idx, _prop_compare_item)) {
            program->error_token = &prop_node->token;
            program->error_str   = str_err_duplicate_property;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }
        else {
            KOS_OBJECT_PROP_DUPE *new_node = (KOS_OBJECT_PROP_DUPE *)
                kos_mempool_alloc(&program->allocator, sizeof(KOS_OBJECT_PROP_DUPE));

            if ( ! new_node)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            new_node->str_idx = str_idx;

            kos_red_black_insert(&prop_str_idcs,
                                 (KOS_RED_BLACK_NODE *)new_node,
                                 _prop_compare_node);
        }

        prop_node = prop_node->next;
        assert(prop_node);
        assert(!prop_node->next);

        assert(prop_node->type != NT_CONSTRUCTOR_LITERAL);
        if (prop_node->type == NT_FUNCTION_LITERAL && prototype)
            TRY(_function_literal(program, prop_node, 0, prototype, &prop));
        else
            TRY(_visit_node(program, prop_node, &prop));
        assert(prop);

        TRY(_gen_instr3(program, INSTR_SET_PROP, (*reg)->reg, str_idx, prop->reg));

        _free_reg(program, prop);
    }

cleanup:
    return error;
}

static int uses_base_proto(KOS_COMP_UNIT *program, const KOS_AST_NODE *node)
{
    KOS_SCOPE *scope;
    KOS_FRAME *frame;

    scope = (KOS_SCOPE *)kos_red_black_find(program->scopes, (void *)node, kos_scope_compare_item);
    assert(scope);
    assert(scope->has_frame);
    frame = (KOS_FRAME *)scope;

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

static int _class_literal(KOS_COMP_UNIT      *program,
                          const KOS_AST_NODE *node,
                          KOS_REG           **reg)
{
    int      error          = KOS_SUCCESS;
    int      void_proto     = 0;
    int      ctor_uses_bp   = 0;
    KOS_REG *base_ctor_reg  = 0;
    KOS_REG *base_proto_reg = 0;
    KOS_REG *proto_reg      = 0;

    assert(node->children);
    node = node->children;
    assert(node->next);

    /* Handle 'extends' clause */
    if (node->type != NT_EMPTY && node->type != NT_VOID_LITERAL) {
        TRY(_visit_node(program, node, &base_ctor_reg));
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

        TRY(_gen_reg(program, &base_proto_reg));

        /* If this class has no own properties in prototype, we will attach the
         * prototype of the base class directly to this class' constructor */
        if ( ! need_base_proto)
            proto_reg = base_proto_reg;

        TRY(_gen_instr2(program, INSTR_GET_PROTO, base_proto_reg->reg, base_ctor_reg->reg));
    }

    /* Build prototype */
    if (node->children || base_ctor_reg || void_proto) {
        if (void_proto) {
            assert( ! base_proto_reg);
            TRY(_gen_reg(program, &base_proto_reg));

            TRY(_gen_instr1(program, INSTR_LOAD_VOID, base_proto_reg->reg));
        }

        TRY(_object_literal(program, node, &proto_reg, base_proto_reg));
        assert(proto_reg);

        if (void_proto) {
            _free_reg(program, base_proto_reg);
            base_proto_reg = 0;
        }
    }

    node = node->next;
    assert(node->type == NT_CONSTRUCTOR_LITERAL);
    assert( ! node->next);

    /* Build constructor */
    TRY(_function_literal(program, node, base_ctor_reg, ctor_uses_bp ? base_proto_reg : 0, reg));
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

        TRY(_gen_str(program, &token, &str_idx));

        TRY(_gen_instr3(program, INSTR_SET_PROP, (*reg)->reg, str_idx, proto_reg->reg));
    }

    if (base_ctor_reg)
        _free_reg(program, base_ctor_reg);
    if (base_proto_reg && base_proto_reg != base_ctor_reg)
        _free_reg(program, base_proto_reg);
    if (proto_reg && proto_reg != base_proto_reg && proto_reg != base_ctor_reg)
        _free_reg(program, proto_reg);

cleanup:
    return error;
}

/* For this function and all other similar functions which it invokes, reg is:
    - on input, the desired register in which we prefer the return value
    - on output, the actual register containing the value computed
*/
static int _visit_node(KOS_COMP_UNIT      *program,
                       const KOS_AST_NODE *node,
                       KOS_REG           **reg)
{
    int error = KOS_ERROR_INTERNAL;

    switch (node->type) {
        case NT_EMPTY:
            error = KOS_SUCCESS;
            break;
        case NT_IMPORT:
            error = _import(program, node);
            break;
        case NT_SCOPE:
            error = _scope(program, node);
            break;
        case NT_IF:
            error = _if(program, node);
            break;
        case NT_RETURN:
            error = _return(program, node);
            break;
        case NT_YIELD:
            error = _yield(program, node, reg);
            break;
        case NT_ASYNC:
            error = _async(program, node, reg);
            break;
        case NT_THROW:
            error = _throw(program, node);
            break;
        case NT_ASSERT:
            error = _assert_stmt(program, node);
            break;
        case NT_REPEAT:
            error = _repeat(program, node);
            break;
        case NT_FOR:
            error = _for(program, node);
            break;
        case NT_FOR_IN:
            error = _for_in(program, node);
            break;
        case NT_CONTINUE:
            /* fall through */
        case NT_BREAK:
            /* fall through */
        case NT_FALLTHROUGH:
            error = _break_continue_fallthrough(program, node);
            break;
        case NT_SWITCH:
            error = _switch(program, node);
            break;
        case NT_TRY_CATCH:
            /* fall through */
        case NT_TRY_DEFER:
            error = _try_stmt(program, node);
            break;
        case NT_REFINEMENT:
            error = _refinement(program, node, reg, 0);
            break;
        case NT_SLICE:
            error = _slice(program, node, reg);
            break;
        case NT_INVOCATION:
            /* fall through */
        case NT_INTERPOLATED_STRING:
            error = _invocation(program, node, reg, INSTR_CALL, 0);
            break;
        case NT_OPERATOR:
            error = _operator(program, node, reg);
            break;
        case NT_ASSIGNMENT:
            /* fall through */
        case NT_MULTI_ASSIGNMENT:
            error = _assignment(program, node);
            break;
        case NT_EXPRESSION_LIST:
            error = _expression_list(program, node, reg);
            break;
        case NT_IDENTIFIER:
            error = _identifier(program, node, reg);
            break;
        case NT_NUMERIC_LITERAL:
            error = _numeric_literal(program, node, reg);
            break;
        case NT_STRING_LITERAL:
            error = _string_literal(program, node, reg);
            break;
        case NT_THIS_LITERAL:
            error = _this_literal(program, node, reg);
            break;
        case NT_SUPER_CTOR_LITERAL:
            /* fall through */
        case NT_SUPER_PROTO_LITERAL:
            program->error_token = &node->token;
            program->error_str   = str_err_unexpected_super;
            error = KOS_ERROR_COMPILE_FAILED;
            break;
        case NT_BOOL_LITERAL:
            error = _bool_literal(program, node, reg);
            break;
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            error = _function_literal(program, node, 0, 0, reg);
            break;
        case NT_ARRAY_LITERAL:
            error = _array_literal(program, node, reg);
            break;
        case NT_OBJECT_LITERAL:
            error = _object_literal(program, node, reg, 0);
            break;
        case NT_CLASS_LITERAL:
            error = _class_literal(program, node, reg);
            break;
        case NT_VOID_LITERAL:
            /* fall through */
        default:
            assert(node->type == NT_VOID_LITERAL);
            error = _void_literal(program, node, reg);
            break;
    }

    return error;
}

void kos_compiler_init(KOS_COMP_UNIT *program,
                       int           file_id)
{
    memset(program, 0, sizeof(*program));

    program->optimize = 1;
    program->file_id  = file_id;

    kos_mempool_init(&program->allocator);

    kos_vector_init(&program->code_buf);
    kos_vector_init(&program->code_gen_buf);
    kos_vector_init(&program->addr2line_buf);
    kos_vector_init(&program->addr2line_gen_buf);
    kos_vector_init(&program->addr2func_buf);
}

int kos_compiler_compile(KOS_COMP_UNIT *program,
                         KOS_AST_NODE  *ast,
                         unsigned      *num_opt_passes)
{
    int      error;
    int      num_optimizations;
    unsigned num_passes = 0;
    KOS_REG *reg        = 0;

    TRY(kos_vector_reserve(&program->code_buf,          1024));
    TRY(kos_vector_reserve(&program->code_gen_buf,      1024));
    TRY(kos_vector_reserve(&program->addr2line_buf,     1024));
    TRY(kos_vector_reserve(&program->addr2line_gen_buf, 256));
    TRY(kos_vector_reserve(&program->addr2func_buf,     256));

    TRY(kos_compiler_process_vars(program, ast));

    do {
        num_optimizations = program->num_optimizations;
        TRY(kos_optimize(program, ast));
        ++num_passes;
    }
    while (program->num_optimizations > num_optimizations);

    TRY(kos_allocate_args(program, ast));

    TRY(_visit_node(program, ast, &reg));
    assert(!reg);

    if (num_opt_passes)
        *num_opt_passes = num_passes;

cleanup:
    return error;
}

void kos_compiler_destroy(KOS_COMP_UNIT *program)
{
    program->pre_globals = 0;

    kos_vector_destroy(&program->code_gen_buf);
    kos_vector_destroy(&program->code_buf);
    kos_vector_destroy(&program->addr2line_gen_buf);
    kos_vector_destroy(&program->addr2line_buf);
    kos_vector_destroy(&program->addr2func_buf);

    kos_mempool_destroy(&program->allocator);
}

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
static const char str_err_stream_dest_not_func[]      = "sink argument of the stream operator is not a function";

enum _KOS_BOOL {
    _KOS_FALSE,
    _KOS_TRUE
};

static int _visit_node(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg);

static int _invocation(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg,
                       enum _KOS_BYTECODE_INSTR    instr,
                       unsigned                    tail_closure_size);

static int _gen_new_reg(struct _KOS_COMP_UNIT *program,
                        struct _KOS_REG      **out_reg)
{
    int                error = KOS_SUCCESS;
    struct _KOS_FRAME *frame = program->cur_frame;
    struct _KOS_REG   *reg;

    if (program->unused_regs) {
        reg                  = program->unused_regs;
        program->unused_regs = reg->next;
    }
    else {
        reg = (struct _KOS_REG *)
            _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_REG));

        if ( ! reg)
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    /* TODO spill locals to an array, add optimizations to reduce register pressure */
    assert(frame->num_regs < 256);

    if (reg)
        reg->reg = frame->num_regs++;

    *out_reg = reg;

    return error;
}

static void _mark_reg_as_used(struct _KOS_FRAME *frame,
                              struct _KOS_REG   *reg)
{
    reg->prev = 0;
    reg->next = frame->used_regs;

    if (frame->used_regs)
        frame->used_regs->prev = reg;

    frame->used_regs = reg;
}

static int _gen_reg(struct _KOS_COMP_UNIT *program,
                    struct _KOS_REG      **out_reg)
{
    int error = KOS_SUCCESS;

    if (!*out_reg) {
        struct _KOS_FRAME *frame = program->cur_frame;
        struct _KOS_REG   *reg   = frame->free_regs;

        if (!reg)
            error = _gen_new_reg(program, &reg);

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

static int _gen_reg_range(struct _KOS_COMP_UNIT *program,
                          struct _KOS_REG      **out_reg,
                          int                    num_regs)
{
    int                error     = KOS_SUCCESS;
    struct _KOS_FRAME *frame     = program->cur_frame;
    struct _KOS_REG  **first_reg = &frame->free_regs;
    struct _KOS_REG   *reg       = frame->free_regs;
    int                count     = reg ? 1 : 0;

    assert(num_regs > 1);

    if (reg) for (;;) {

        struct _KOS_REG *next = reg->next;

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
            struct _KOS_REG *next = reg->next;

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

static int _gen_dest_reg(struct _KOS_COMP_UNIT *program,
                         struct _KOS_REG      **dest,
                         struct _KOS_REG       *src_reg)
{
    int              error    = KOS_SUCCESS;
    struct _KOS_REG *dest_reg = *dest;

    assert(src_reg);

    if ( ! src_reg->tmp && (src_reg == dest_reg || ! dest_reg)) {
        *dest = 0;
        error = _gen_reg(program, dest);
    }
    else if ( ! dest_reg)
        *dest = src_reg;

    return error;
}

static void _free_reg(struct _KOS_COMP_UNIT *program,
                      struct _KOS_REG       *reg)
{
    assert(reg);
    if (reg->tmp) {
        struct _KOS_FRAME *frame = program->cur_frame;
        struct _KOS_REG  **reg_ptr;

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

static void _free_all_regs(struct _KOS_COMP_UNIT *program,
                           struct _KOS_REG       *reg)
{
    if (reg) {
        struct _KOS_REG *first_reg = reg;

        while (reg->next)
            reg = reg->next;

        reg->next            = program->unused_regs;
        program->unused_regs = first_reg;
    }
}

static int _lookup_local_var_even_inactive(struct _KOS_COMP_UNIT   *program,
                                           const struct _KOS_TOKEN *token,
                                           int                      only_active,
                                           struct _KOS_REG        **reg)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VAR   *var   = 0;
    struct _KOS_SCOPE *scope;

    /* Lookup variable in local scopes until we find the current function.
     * Skip global scope, because it's handled by _lookup_var().
     * Function scope holds arguments, not local variables, so skip it.
     * Function scope is handled by _lookup_var() - arguments are accessed
     * via the arguments array. */
    for (scope = program->scope_stack; scope && scope->next && ! scope->is_function; scope = scope->next) {

        var = _KOS_find_var(scope->vars, token);

        if (var && (var->is_active || ! only_active)) {
            assert( ! (var->type & VAR_ARGUMENT));

            if ( ! var->reg) {
                error = _gen_reg(program, &var->reg);
                if ( ! error)
                    var->reg->tmp = 0;
            }

            *reg = var->reg;
            break;
        }

        var = 0;
    }

    /* Lookup arguments in registers */
    if ( ! var && scope && scope->is_function) {

        var = _KOS_find_var(scope->vars, token);

        if (var && (var->type & VAR_ARGUMENT_IN_REG)) {
            assert(var->reg);
            *reg = var->reg;
        }
        else
            var = 0;
    }

    /* Access arguments list */
    if ( ! var && scope && scope->is_function && scope->ellipsis) {

        var = _KOS_find_var(scope->vars, token);
        if (var != scope->ellipsis)
            var = 0;

        if (var) {
            assert(var->is_active);
            assert(var->reg);
            *reg = var->reg;
        }
    }

    return error;
}

static int _lookup_local_var(struct _KOS_COMP_UNIT   *program,
                             const struct _KOS_TOKEN *token,
                             struct _KOS_REG        **reg)
{
    return _lookup_local_var_even_inactive(program, token, 1, reg);
}

static int _lookup_var(struct _KOS_COMP_UNIT   *program,
                       const struct _KOS_TOKEN *token,
                       struct _KOS_VAR        **out_var,
                       struct _KOS_REG        **reg)
{
    struct _KOS_VAR   *var          = 0;
    struct _KOS_SCOPE *scope        = program->scope_stack;
    int                is_local_arg = 1;
    int                is_global    = 0;

    assert(scope);

    /* Skip local scopes */
    while (scope->next && ! scope->is_function)
        scope = scope->next;

    /* Find variable in args, closures and globals */
    for ( ; scope; scope = scope->next) {

        var = _KOS_find_var(scope->vars, token);

        if (var && var->is_active) {
            /* Global scope */
            if ( ! scope->next) {
                assert(!scope->is_function);
                is_local_arg = 0;
                is_global    = 1;
            }
            break;
        }

        var = 0;

        /* We are dealing with a local argument only on the first loop. */
        is_local_arg = 0;
    }

    if (var) {

        const int is_var        = var->type == VAR_INDEPENDENT_LOCAL;
        const int is_arg_in_reg = var->type == VAR_INDEPENDENT_ARG_IN_REG;

        *out_var = var;

        if (is_local_arg) {
            if (reg) {
                assert((var->type & VAR_ARGUMENT) && ! (var->type & VAR_ARGUMENT_IN_REG));
                assert(program->cur_frame->args_reg);
                *reg = program->cur_frame->args_reg;
            }
        }
        else if (!is_global) {

            struct _KOS_SCOPE_REF *ref;

            assert(is_var ? ( ! scope->is_function || scope->ellipsis == var) : scope->is_function);

            /* Find function scope for this variable */
            while (scope->next && ! scope->is_function)
                scope = scope->next;

            ref = _KOS_find_scope_ref(program->cur_frame, scope);

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
        program->error_token = token;

    return var ? KOS_SUCCESS : KOS_ERROR_INTERNAL;
}

static int _compare_strings(const char *a, unsigned len_a, enum _KOS_UTF8_ESCAPE escape_a,
                            const char *b, unsigned len_b, enum _KOS_UTF8_ESCAPE escape_b)
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

static void _get_token_str(const struct _KOS_TOKEN *token,
                           const char             **out_begin,
                           unsigned                *out_length,
                           enum _KOS_UTF8_ESCAPE   *out_escape)
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

static int _numbers_compare_item(void                       *what,
                                 struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_NUMERIC    *numeric  = (const struct _KOS_NUMERIC *)what;
    const struct _KOS_COMP_CONST *constant = (const struct _KOS_COMP_CONST *)node;

    const enum _KOS_COMP_CONST_TYPE type =
        numeric->type == KOS_INTEGER_VALUE ? KOS_COMP_CONST_INTEGER : KOS_COMP_CONST_FLOAT;

    if (type != constant->type)
        return (int)type < (int)constant->type ? -1 : 1;

    if (numeric->type == KOS_INTEGER_VALUE)
        return numeric->u.i < ((const struct _KOS_COMP_INTEGER *)constant)->value ? -1 :
               numeric->u.i > ((const struct _KOS_COMP_INTEGER *)constant)->value ? 1 : 0;
    else
        return numeric->u.d == ((const struct _KOS_COMP_FLOAT *)constant)->value ? 0 :
               numeric->u.d <  ((const struct _KOS_COMP_FLOAT *)constant)->value ? -1 : 1;
}

static int _strings_compare_item(void                       *what,
                                 struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_TOKEN       *token = (const struct _KOS_TOKEN       *)what;
    const struct _KOS_COMP_STRING *str   = (const struct _KOS_COMP_STRING *)node;
    const char                    *begin;
    unsigned                       length;
    enum _KOS_UTF8_ESCAPE          escape;

    if (str->header.type != KOS_COMP_CONST_STRING)
        return (int)KOS_COMP_CONST_STRING < (int)str->header.type ? -1 : 1;

    _get_token_str(token, &begin, &length, &escape);

    return _compare_strings(begin,    length,      escape,
                            str->str, str->length, str->escape);
}

static int _constants_compare_node(struct _KOS_RED_BLACK_NODE *a,
                                   struct _KOS_RED_BLACK_NODE *b)
{
    const struct _KOS_COMP_CONST *const_a = (const struct _KOS_COMP_CONST *)a;
    const struct _KOS_COMP_CONST *const_b = (const struct _KOS_COMP_CONST *)b;

    if (const_a->type != const_b->type)
        return (int)const_a->type < (int)const_b->type ? -1 : 0;

    else switch (const_a->type) {

        default:
            assert(const_a->type == KOS_COMP_CONST_INTEGER);
            return ((const struct _KOS_COMP_INTEGER *)const_a)->value
                 < ((const struct _KOS_COMP_INTEGER *)const_b)->value
                 ? -1 : 0;

        case KOS_COMP_CONST_FLOAT:
            return ((const struct _KOS_COMP_FLOAT *)const_a)->value
                 < ((const struct _KOS_COMP_FLOAT *)const_b)->value
                 ? -1 : 0;

        case KOS_COMP_CONST_STRING: {
            const struct _KOS_COMP_STRING *str_a = (const struct _KOS_COMP_STRING *)const_a;
            const struct _KOS_COMP_STRING *str_b = (const struct _KOS_COMP_STRING *)const_b;
            return _compare_strings(str_a->str, str_a->length, str_a->escape,
                                    str_b->str, str_b->length, str_b->escape);
        }

        case KOS_COMP_CONST_FUNCTION:
            return ((const struct _KOS_COMP_FUNCTION *)const_a)->offset
                 < ((const struct _KOS_COMP_FUNCTION *)const_b)->offset
                 ? -1 : 0;
    }
}

static void _add_constant(struct _KOS_COMP_UNIT  *program,
                          struct _KOS_COMP_CONST *constant)
{
    constant->index = program->num_constants++;
    constant->next  = 0;

    if (program->last_constant)
        program->last_constant->next = constant;
    else
        program->first_constant = constant;

    program->last_constant = constant;

    _KOS_red_black_insert(&program->constants,
                          &constant->rb_tree_node,
                          _constants_compare_node);
}

static int _gen_str_esc(struct _KOS_COMP_UNIT   *program,
                        const struct _KOS_TOKEN *token,
                        enum _KOS_UTF8_ESCAPE    escape,
                        int                     *str_idx)
{
    int error = KOS_SUCCESS;

    struct _KOS_COMP_STRING *str =
            (struct _KOS_COMP_STRING *)_KOS_red_black_find(program->constants,
                                                           (struct _KOS_TOKEN *)token,
                                                           _strings_compare_item);

    if (!str) {

        str = (struct _KOS_COMP_STRING *)
            _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_COMP_STRING));

        if (str) {

            const char           *begin;
            unsigned              length;
            enum _KOS_UTF8_ESCAPE tok_escape;

            _get_token_str(token, &begin, &length, &tok_escape);

            if (tok_escape == KOS_UTF8_NO_ESCAPE)
                escape = KOS_UTF8_NO_ESCAPE;

            str->header.type = KOS_COMP_CONST_STRING;
            str->str         = begin;
            str->length      = length;
            str->escape      = escape;

            _add_constant(program, (struct _KOS_COMP_CONST *)str);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (!error)
        *str_idx = str->header.index;

    return error;
}

static int _gen_str(struct _KOS_COMP_UNIT   *program,
                    const struct _KOS_TOKEN *token,
                    int                     *str_idx)
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
                            char       *buf)
{
    int last_printable = 0;

    for ( ; begin < end; ++begin) {

        const char c = *begin;

        const int printable = (uint8_t)c > 0x20U ? 1 : 0;

        if (printable)
            *(buf++) = c;
        else if (last_printable)
            *(buf++) = ' ';

        last_printable = printable;
    }
}

static int _gen_assert_str(struct _KOS_COMP_UNIT      *program,
                           const struct _KOS_AST_NODE *node,
                           int                        *str_idx)
{
    int         error = KOS_SUCCESS;
    const char *begin;
    const char *end;
    char       *buf;
    unsigned    length;

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

    buf = (char *)_KOS_mempool_alloc(&program->allocator, length);

    if (buf) {

        struct _KOS_TOKEN token;

        memcpy(buf, assertion_failed, sizeof(assertion_failed)-1);
        _get_assert_str(begin, end, buf+sizeof(assertion_failed)-1);

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

static int _add_addr2line(struct _KOS_COMP_UNIT   *program,
                          const struct _KOS_TOKEN *token,
                          enum _KOS_BOOL           force)
{
    int                           error;
    struct _KOS_VECTOR           *addr2line = &program->addr2line_gen_buf;
    struct _KOS_COMP_ADDR_TO_LINE new_loc;

    new_loc.offs = (uint32_t)program->cur_offs;
    new_loc.line = (uint32_t)token->pos.line;

    if (addr2line->size && ! force) {

        struct _KOS_COMP_ADDR_TO_LINE *const last =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (addr2line->buffer + (addr2line->size - sizeof(struct _KOS_COMP_ADDR_TO_LINE)));

        if (last->offs == new_loc.offs) {
            if (new_loc.line > last->line)
                last->line = new_loc.line;
            return KOS_SUCCESS;
        }
    }

    error = _KOS_vector_resize(addr2line, addr2line->size + sizeof(new_loc));

    if ( ! error) {

        struct _KOS_COMP_ADDR_TO_LINE *const last =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (addr2line->buffer + (addr2line->size - sizeof(new_loc)));

        *last = new_loc;
    }

    return error;
}

static int _gen_instr(struct _KOS_COMP_UNIT *program,
                      int                    num_args,
                      ...)
{
    int cur_offs = program->cur_offs;
    int error    = _KOS_vector_resize(&program->code_gen_buf,
                                      (size_t)cur_offs + 1 + 4 * num_args); /* Over-estimate */
    if (!error) {

        enum _KOS_BYTECODE_INSTR instr = INSTR_BREAKPOINT;

        va_list  args;
        int      i;
        uint8_t *buf = (uint8_t *)program->code_gen_buf.buffer;

        va_start(args, num_args);
        for (i = 0; i <= num_args; i++) {

            int32_t value = (int32_t)va_arg(args, int32_t);
            int     size  = 1;

            if (i == 0)
                instr = (enum _KOS_BYTECODE_INSTR)value;
            else
                size = _KOS_get_operand_size(instr, i-1);

            if (size == 1) {
                if ( ! _KOS_is_register(instr, i-1)) {
                    if (_KOS_is_signed_op(instr, i-1)) {
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

static int _gen_instr1(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1)
{
    return _gen_instr(program, 1, opcode, operand1);
}

static int _gen_instr2(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1,
                       int32_t                operand2)
{
    return _gen_instr(program, 2, opcode, operand1, operand2);
}

static int _gen_instr3(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1,
                       int32_t                operand2,
                       int32_t                operand3)
{
    return _gen_instr(program, 3, opcode, operand1, operand2, operand3);
}

static int _gen_instr4(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1,
                       int32_t                operand2,
                       int32_t                operand3,
                       int32_t                operand4)
{
    return _gen_instr(program, 4, opcode, operand1, operand2, operand3, operand4);
}

static int _gen_instr5(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1,
                       int32_t                operand2,
                       int32_t                operand3,
                       int32_t                operand4,
                       int32_t                operand5)
{
    return _gen_instr(program, 5, opcode, operand1, operand2, operand3, operand4, operand5);
}

static int _gen_instr6(struct _KOS_COMP_UNIT *program,
                       int                    opcode,
                       int32_t                operand1,
                       int32_t                operand2,
                       int32_t                operand3,
                       int32_t                operand4,
                       int32_t                operand5,
                       int32_t                operand6)
{
    return _gen_instr(program, 6, opcode, operand1, operand2, operand3, operand4, operand5, operand6);
}

static void _write_jump_offs(struct _KOS_COMP_UNIT *program,
                             struct _KOS_VECTOR    *vec,
                             int                    jump_instr_offs,
                             int                    target_offs)
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

    assert(opcode == INSTR_LOAD_FUN ||
           opcode == INSTR_LOAD_GEN ||
           opcode == INSTR_LOAD_CTOR||
           opcode == INSTR_CATCH     ||
           opcode == INSTR_JUMP      ||
           opcode == INSTR_JUMP_COND ||
           opcode == INSTR_JUMP_NOT_COND);

    switch (opcode) {

        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN:
            /* fall through */
        case INSTR_LOAD_CTOR:
            jump_instr_size = 10;
            break;

        case INSTR_JUMP:
            jump_instr_size = 5;
            break;

        default:
            jump_instr_size = 6;
            break;
    }

    jump_offs = target_offs - (jump_instr_offs + jump_instr_size);

    buf += (opcode == INSTR_LOAD_FUN
         || opcode == INSTR_LOAD_GEN
         || opcode == INSTR_LOAD_CTOR
         || opcode == INSTR_CATCH) ? 2 : 1;

    for (end = buf+4; buf < end; ++buf) {
        *buf      =   (uint8_t)jump_offs;
        jump_offs >>= 8;
    }
}

static void _update_jump_offs(struct _KOS_COMP_UNIT *program,
                              int                    jump_instr_offs,
                              int                    target_offs)
{
    assert(jump_instr_offs <  program->cur_offs);
    assert(target_offs     <= program->cur_offs);

    _write_jump_offs(program, &program->code_gen_buf, jump_instr_offs, target_offs);
}

static void _remove_last_instr(struct _KOS_COMP_UNIT *program,
                               int                    offs)
{
    --program->cur_frame->num_instr;
    program->cur_offs = offs;
}

int _KOS_scope_compare_item(void                       *what,
                            struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_AST_NODE *scope_node = (const struct _KOS_AST_NODE *)what;

    const struct _KOS_SCOPE *scope = (const struct _KOS_SCOPE *)node;

    return (int)((intptr_t)scope_node - (intptr_t)scope->scope_node);
}

static struct _KOS_SCOPE *_push_scope(struct _KOS_COMP_UNIT      *program,
                                      const struct _KOS_AST_NODE *node)
{
    struct _KOS_SCOPE *scope = (struct _KOS_SCOPE *)
            _KOS_red_black_find(program->scopes, (void *)node, _KOS_scope_compare_item);

    assert(scope);

    assert(scope->next == program->scope_stack);

    _KOS_deactivate_vars(scope);

    program->scope_stack = scope;

    return scope;
}

static int _free_scope_regs(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_VAR       *var     = (struct _KOS_VAR *)node;
    struct _KOS_COMP_UNIT *program = (struct _KOS_COMP_UNIT *)cookie;

    if (var->reg && var->type != VAR_INDEPENDENT_LOCAL) {
        var->reg->tmp = 1;
        _free_reg(program, var->reg);
        var->reg = 0;
    }

    return KOS_SUCCESS;
}

static void _pop_scope(struct _KOS_COMP_UNIT *program)
{
    assert(program->scope_stack);

    if (program->scope_stack->vars)
        _KOS_red_black_walk(program->scope_stack->vars, _free_scope_regs, (void *)program);

    program->scope_stack = program->scope_stack->next;
}

struct _IMPORT_INFO {
    struct _KOS_COMP_UNIT *program;
    struct _KOS_FILE_POS   pos;
};

static int _import_global(const char *global_name,
                          unsigned    global_length,
                          int         module_idx,
                          int         global_idx,
                          void       *cookie)
{
    int                  error = KOS_SUCCESS;
    struct _IMPORT_INFO *info  = (struct _IMPORT_INFO *)cookie;
    struct _KOS_REG     *reg   = 0;
    struct _KOS_VAR     *var;
    struct _KOS_TOKEN    token;

    memset(&token, 0, sizeof(token));

    token.begin  = global_name;
    token.length = global_length;
    token.pos    = info->pos;
    token.type   = TT_IDENTIFIER;

    var = _KOS_find_var(info->program->scope_stack->vars, &token);

    assert(var);
    assert(var->type == VAR_GLOBAL);

    TRY(_gen_reg(info->program, &reg));

    TRY(_gen_instr3(info->program, INSTR_GET_MOD_ELEM, reg->reg, module_idx, global_idx));

    TRY(_gen_instr2(info->program, INSTR_SET_GLOBAL, var->array_idx, reg->reg));

    _free_reg(info->program, reg);

_error:
    return error;
}

static int _import(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);

    if (node->next) {

        int                 module_idx;
        struct _IMPORT_INFO info;

        info.program = program;

        assert(program->import_module);
        assert(program->get_global_idx);
        assert(program->walk_globals);

        TRY(program->import_module(program->frame,
                                   node->token.begin,
                                   node->token.length,
                                   &module_idx));

        node = node->next;

        if (node->token.op == OT_MUL) {

            info.pos = node->token.pos;

            error = program->walk_globals(program->frame,
                                          module_idx,
                                          _import_global,
                                          &info);
        }
        else {

            for ( ; node; node = node->next) {

                int global_idx;

                assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

                TRY(program->get_global_idx(program->frame,
                                            module_idx,
                                            node->token.begin,
                                            node->token.length,
                                            &global_idx));

                info.pos = node->token.pos;

                TRY(_import_global(node->token.begin,
                                   node->token.length,
                                   module_idx,
                                   global_idx,
                                   &info));
            }
        }
    }

_error:
    return error;
}

static int _append_frame(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *name_node,
                         int                         fun_start_offs,
                         size_t                      addr2line_start_offs)
{
    int          error;
    const size_t fun_end_offs = (size_t)program->cur_offs;
    const size_t fun_size     = fun_end_offs - fun_start_offs;
    const size_t fun_new_offs = program->code_buf.size;
    const size_t a2l_size     = program->addr2line_gen_buf.size - addr2line_start_offs;
    size_t       a2l_new_offs = program->addr2line_buf.size;
    int          str_idx      = 0;

    const struct _KOS_TOKEN *name_token;

    TRY(_KOS_vector_resize(&program->code_buf, fun_new_offs + fun_size));

    if (a2l_new_offs)
    {
        struct _KOS_COMP_ADDR_TO_LINE *last_ptr =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (program->addr2line_buf.buffer + a2l_new_offs);
        --last_ptr;

        if (last_ptr->offs == fun_new_offs)
            a2l_new_offs -= sizeof(struct _KOS_COMP_ADDR_TO_LINE);
    }

    TRY(_KOS_vector_resize(&program->addr2line_buf, a2l_new_offs + a2l_size));

    TRY(_KOS_vector_resize(&program->addr2func_buf,
                           program->addr2func_buf.size + sizeof(struct _KOS_COMP_ADDR_TO_FUNC)));

    assert(name_node);
    if (name_node->children)
        name_token = &name_node->children->token;
    else
        name_token = program->cur_frame->fun_token;
    TRY(_gen_str(program, name_token, &str_idx));

    memcpy(program->code_buf.buffer + fun_new_offs,
           program->code_gen_buf.buffer + fun_start_offs,
           fun_size);

    TRY(_KOS_vector_resize(&program->code_gen_buf, (size_t)fun_start_offs));

    program->cur_offs = fun_start_offs;

    program->cur_frame->program_offs = (int)fun_new_offs;

    memcpy(program->addr2line_buf.buffer + a2l_new_offs,
           program->addr2line_gen_buf.buffer + addr2line_start_offs,
           a2l_size);

    TRY(_KOS_vector_resize(&program->addr2line_gen_buf, addr2line_start_offs));

    /* Update addr2line offsets for this function */
    {
        struct _KOS_COMP_ADDR_TO_LINE *ptr =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (program->addr2line_buf.buffer + a2l_new_offs);
        struct _KOS_COMP_ADDR_TO_LINE *const end =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (program->addr2line_buf.buffer + program->addr2line_buf.size);

        const uint32_t delta = (uint32_t)(fun_new_offs - fun_start_offs);

        for ( ; ptr < end; ptr++)
            ptr->offs += delta;
    }

    {
        struct _KOS_VECTOR *buf = &program->addr2func_buf;

        struct _KOS_COMP_ADDR_TO_FUNC *ptr =
            (struct _KOS_COMP_ADDR_TO_FUNC *)
                (buf->buffer + buf->size - sizeof(struct _KOS_COMP_ADDR_TO_FUNC));

        ptr->offs      = (uint32_t)fun_new_offs;
        ptr->line      = program->cur_frame->fun_token->pos.line;
        ptr->str_idx   = (uint32_t)str_idx;
        ptr->num_instr = program->cur_frame->num_instr;
        ptr->code_size = (uint32_t)fun_size;
    }

_error:
    return error;
}

static int _fix_frame_offsets(struct _KOS_RED_BLACK_NODE *node,
                              void                       *cookie)
{
    struct _KOS_SCOPE *scope   = (struct _KOS_SCOPE *)node;
    struct _KOS_FRAME *frame   = scope->frame;

    if (frame && frame->parent_frame)
        frame->program_offs += *(int *)cookie;

    return KOS_SUCCESS;
}

static int _insert_global_frame(struct _KOS_COMP_UNIT *program)
{
    /* At this point code_buf contains bytecodes of all functions
     * and code_gen_buf contains global scope bytecode. */
    int          error;
    const size_t global_scope_size = (size_t)program->cur_offs;
    const size_t functions_size    = program->code_buf.size;
    const size_t funcs_a2l_size    = program->addr2line_buf.size;

    TRY(_KOS_vector_resize(&program->code_buf,
                           functions_size + global_scope_size));

    TRY(_KOS_vector_resize(&program->addr2line_buf,
                           program->addr2line_buf.size + program->addr2line_gen_buf.size));

    memmove(program->code_buf.buffer + global_scope_size,
            program->code_buf.buffer,
            functions_size);

    memcpy(program->code_buf.buffer,
           program->code_gen_buf.buffer,
           global_scope_size);

    TRY(_KOS_vector_resize(&program->code_gen_buf, 0));

    program->cur_offs = 0;

    TRY(_KOS_red_black_walk(program->scopes, _fix_frame_offsets, (void *)&global_scope_size));

    /* Update addr2line offsets for functions */
    {
        struct _KOS_COMP_ADDR_TO_LINE *ptr =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                program->addr2line_buf.buffer;
        struct _KOS_COMP_ADDR_TO_LINE *end =
            (struct _KOS_COMP_ADDR_TO_LINE *)
                (program->addr2line_buf.buffer + program->addr2line_buf.size);

        for ( ; ptr < end; ptr++)
            ptr->offs += (uint32_t)global_scope_size;
    }

    {
        struct _KOS_COMP_ADDR_TO_FUNC *ptr =
            (struct _KOS_COMP_ADDR_TO_FUNC *)
                program->addr2func_buf.buffer;
        struct _KOS_COMP_ADDR_TO_FUNC *end =
            (struct _KOS_COMP_ADDR_TO_FUNC *)
                (program->addr2func_buf.buffer + program->addr2func_buf.size);

        for ( ; ptr < end; ptr++)
            ptr->offs += (uint32_t)global_scope_size;
    }

    memmove(program->addr2line_buf.buffer + program->addr2line_gen_buf.size,
            program->addr2line_buf.buffer,
            funcs_a2l_size);

    memcpy(program->addr2line_buf.buffer,
           program->addr2line_gen_buf.buffer,
           program->addr2line_gen_buf.size);

    TRY(_KOS_vector_resize(&program->addr2line_gen_buf, 0));

_error:
    return error;
}

static int _patch_fun_loads(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_SCOPE     *scope   = (struct _KOS_SCOPE *)node;
    struct _KOS_FRAME     *frame   = scope->frame;
    struct _KOS_COMP_UNIT *program = (struct _KOS_COMP_UNIT *)cookie;

    if (frame && frame->parent_frame)
        _write_jump_offs(program,
                         &program->code_buf,
                         frame->parent_frame->program_offs + frame->load_offs,
                         frame->program_offs);

    return KOS_SUCCESS;
}

static int _finish_global_scope(struct _KOS_COMP_UNIT *program,
                                struct _KOS_REG       *reg)
{
    int error;

    if ( ! reg) {
        TRY(_gen_reg(program, &reg));
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    TRY(_gen_instr2(program, INSTR_RETURN, program->scope_stack->num_indep_vars, reg->reg));

    _free_reg(program, reg);
    reg = 0;

    TRY(_insert_global_frame(program));

    assert(program->code_gen_buf.size == 0);

    TRY(_KOS_red_black_walk(program->scopes, _patch_fun_loads, program));

_error:
    return error;
}

static int _scope(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int       error  = KOS_SUCCESS;
    const int global = program->scope_stack == 0;

    const struct _KOS_AST_NODE *child = node->children;

    if (child || global) {

        struct _KOS_REG *reg = 0;

        _push_scope(program, node);

        /* Init global scope */
        if (global) {

            struct _KOS_VAR *var = 0;

            program->cur_frame            = program->scope_stack->frame;
            program->cur_frame->load_offs = -1;

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

            TRY(_add_addr2line(program, &child->token, _KOS_FALSE));

            if (reg) {
                _free_reg(program, reg);
                reg = 0;
            }

            TRY(_visit_node(program, child, &reg));
        }

        if (global)
            TRY(_finish_global_scope(program, reg));
        else if (reg)
            _free_reg(program, reg);

        _pop_scope(program);
    }

_error:
    return error;
}

static int _if(struct _KOS_COMP_UNIT      *program,
               const struct _KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;
    int offs  = -1;
    int always_truthy;

    struct _KOS_REG *reg = 0;

    TRY(_add_addr2line(program, &node->token, _KOS_FALSE));

    node = node->children;
    assert(node);

    always_truthy = _KOS_node_is_truthy(program, node);

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

_error:
    return error;
}

static struct _KOS_SCOPE *_find_try_scope(struct _KOS_SCOPE *scope)
{
    while (scope && ! scope->is_function && ! scope->catch_ref.catch_reg)
        scope = scope->next;

    if (scope && (scope->is_function || ! scope->catch_ref.catch_reg))
        scope = 0;

    return scope;
}

static int _get_closure_size(struct _KOS_COMP_UNIT *program)
{
    int                closure_size;
    struct _KOS_SCOPE *scope;

    scope = program->scope_stack;
    while (scope->next && ! scope->is_function)
        scope = scope->next;

    closure_size = scope->num_indep_vars + scope->num_indep_args;

    assert(closure_size <= 255);
    return closure_size;
}

static int _gen_return(struct _KOS_COMP_UNIT *program,
                       int                    reg)
{
    int                error = KOS_SUCCESS;
    struct _KOS_SCOPE *scope = _find_try_scope(program->scope_stack);

    while (scope && ! scope->catch_ref.finally_active)
        scope = _find_try_scope(scope->next);

    if (scope) {

        const int return_reg = scope->catch_ref.catch_reg->reg;

        struct _KOS_RETURN_OFFS *const return_offs = (struct _KOS_RETURN_OFFS *)
            _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_RETURN_OFFS));

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

_error:
    return error;
}

static int _is_generator(struct _KOS_COMP_UNIT *program)
{
    struct _KOS_SCOPE *scope = program->scope_stack;

    for ( ; scope && ! scope->is_function; scope = scope->next);

    return scope && scope->is_function && scope->frame->yield_token;
}

static int _return(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int                error;
    struct _KOS_REG   *reg       = 0;
    struct _KOS_SCOPE *try_scope = _find_try_scope(program->scope_stack);
    int                tail_call = 0;

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

_error:
    return error;
}

static int _yield(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node,
                  struct _KOS_REG           **reg)
{
    int              error;
    struct _KOS_REG *src = *reg;

    assert(node->children);

    TRY(_visit_node(program, node->children, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    if (src != *reg)
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

    TRY(_gen_instr1(program, INSTR_YIELD, (*reg)->reg));

    if (src != *reg)
        _free_reg(program, src);

_error:
    return error;
}

static int _stream(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node,
                   struct _KOS_REG           **reg)
{
    int                         error    = KOS_SUCCESS;
    struct _KOS_REG            *src_reg  = 0;
    struct _KOS_REG            *func_reg = 0;
    const struct _KOS_AST_NODE *arrow_node;
    const struct _KOS_AST_NODE *const_node;

    arrow_node = node;

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &src_reg));

    node = node->next;
    assert(node);
    assert( ! node->next);

    const_node = _KOS_get_const(program, node);

    if (const_node)
        switch (const_node->type) {

            case NT_NUMERIC_LITERAL:
                /* fall through */
            case NT_STRING_LITERAL:
                /* fall through */
            case NT_LINE_LITERAL:
                /* fall through */
            case NT_BOOL_LITERAL:
                /* fall through */
            case NT_VOID_LITERAL:
                /* fall through */
            case NT_ARRAY_LITERAL:
                /* fall through */
            case NT_OBJECT_LITERAL:
                program->error_token = &arrow_node->token;
                program->error_str   = str_err_stream_dest_not_func;
                RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);

            default:
                break;
        }

    TRY(_visit_node(program, node, &func_reg));

    TRY(_gen_dest_reg(program, reg, src_reg));

    TRY(_gen_instr4(program, INSTR_CALL_FUN, (*reg)->reg, func_reg->reg, src_reg->reg, 1));

    if (*reg != src_reg)
        _free_reg(program, src_reg);
    _free_reg(program, func_reg);

_error:
    return error;
}

static int _throw(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int              error;
    struct _KOS_REG *reg = 0;

    assert(node->children);

    TRY(_visit_node(program, node->children, &reg));
    assert(reg);

    TRY(_gen_instr1(program, INSTR_THROW, reg->reg));

    _free_reg(program, reg);

_error:
    return error;
}

static int _assert(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int              error;
    int              jump_instr_offs;
    int              str_idx;
    struct _KOS_REG *reg = 0;

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

_error:
    return error;
}

static void _finish_break_continue(struct _KOS_COMP_UNIT  *program,
                                   int                     continue_tgt_offs,
                                   struct _KOS_BREAK_OFFS *old_break_offs)
{
    struct _KOS_BREAK_OFFS   *break_offs      = program->cur_frame->break_offs;
    const int                 break_tgt_offs  = program->cur_offs;
    const enum _KOS_NODE_TYPE unsup_node_type = continue_tgt_offs >= 0 ? NT_FALLTHROUGH : NT_CONTINUE;

    while (break_offs) {
        struct _KOS_BREAK_OFFS *next = break_offs->next;

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

static void _finish_fallthrough(struct _KOS_COMP_UNIT *program)
{
    struct _KOS_BREAK_OFFS **remaining_offs       = &program->cur_frame->break_offs;
    struct _KOS_BREAK_OFFS  *break_offs           = *remaining_offs;
    const int                fallthrough_tgt_offs = program->cur_offs;

    *remaining_offs = 0;

    while (break_offs) {

        struct _KOS_BREAK_OFFS *next = break_offs->next;

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
static struct _KOS_SCOPE *_push_try_scope(struct _KOS_COMP_UNIT *program)
{
    struct _KOS_SCOPE *prev_try_scope = program->cur_frame->last_try_scope;
    struct _KOS_SCOPE *scope          = _find_try_scope(program->scope_stack);

    if (scope)
        program->cur_frame->last_try_scope = scope;

    return prev_try_scope;
}

static int _repeat(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int                     error;
    int                     jump_instr_offs;
    int                     test_instr_offs;
    const int               loop_start_offs = program->cur_offs;
    struct _KOS_REG        *reg             = 0;
    struct _KOS_BREAK_OFFS *old_break_offs  = program->cur_frame->break_offs;
    struct _KOS_SCOPE      *prev_try_scope  = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    node = node->children;
    assert(node);
    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    TRY(_add_addr2line(program, &node->token, _KOS_FALSE));

    node = node->next;
    assert(node);

    TRY(_add_addr2line(program, &node->token, _KOS_FALSE));

    test_instr_offs = program->cur_offs;

    if ( ! _KOS_node_is_falsy(program, node)) {

        TRY(_visit_node(program, node, &reg));
        assert(reg);

        assert(!node->next);

        jump_instr_offs = program->cur_offs;
        TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));
        _update_jump_offs(program, jump_instr_offs, loop_start_offs);
    }

    _finish_break_continue(program, test_instr_offs, old_break_offs);

    if (reg)
        _free_reg(program, reg);

    program->cur_frame->last_try_scope = prev_try_scope;

_error:
    return error;
}

static int _while(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int                         error          = KOS_SUCCESS;
    struct _KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    struct _KOS_SCOPE          *prev_try_scope = _push_try_scope(program);
    const struct _KOS_AST_NODE *cond_node      = node->children;

    assert(cond_node);

    if ( ! _KOS_node_is_falsy(program, cond_node)) {

        const int        is_truthy       = _KOS_node_is_truthy(program, cond_node);
        struct _KOS_REG *reg             = 0;
        int              jump_instr_offs = 0;
        int              loop_start_offs;
        int              continue_offs;
        int              offs;

        program->cur_frame->break_offs = 0;

        if ( ! is_truthy) {

            TRY(_visit_node(program, cond_node, &reg));
            assert(reg);

            jump_instr_offs = program->cur_offs;
            TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, reg->reg));

            _free_reg(program, reg);
            reg = 0;
        }

        loop_start_offs = program->cur_offs;
        continue_offs   = loop_start_offs;

        node = cond_node->next;
        assert(node);
        TRY(_visit_node(program, node, &reg));
        assert(!reg);

        assert(!node->next);

        /* TODO skip jump if last node was terminating - return, throw, break, continue */

        if (is_truthy) {

            offs = program->cur_offs;

            TRY(_gen_instr1(program, INSTR_JUMP, 0));
        }
        else {

            TRY(_add_addr2line(program, &cond_node->token, _KOS_FALSE));

            continue_offs = program->cur_offs;

            TRY(_visit_node(program, cond_node, &reg));
            assert(reg);

            offs = program->cur_offs;

            TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));

            _free_reg(program, reg);
            reg = 0;
        }

        _update_jump_offs(program, offs, loop_start_offs);

        if ( ! is_truthy)
            _update_jump_offs(program, jump_instr_offs, program->cur_offs);

        _finish_break_continue(program, continue_offs, old_break_offs);
    }

    program->cur_frame->last_try_scope = prev_try_scope;

_error:
    return error;
}

static int _for(struct _KOS_COMP_UNIT      *program,
                const struct _KOS_AST_NODE *node)
{
    int                         error          = KOS_SUCCESS;
    const struct _KOS_AST_NODE *cond_node      = node->children;
    struct _KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    struct _KOS_SCOPE          *prev_try_scope = _push_try_scope(program);

    assert(cond_node);

    if ( ! _KOS_node_is_falsy(program, cond_node)) {

        int                         loop_start_offs;
        int                         cond_jump_instr_offs = -1;
        int                         final_jump_instr_offs;
        int                         step_instr_offs;
        const struct _KOS_AST_NODE *step_node;
        struct _KOS_REG            *reg                  = 0;

        program->cur_frame->break_offs = 0;

        TRY(_add_addr2line(program, &cond_node->token, _KOS_FALSE));

        /* TODO check truthy/falsy */

        TRY(_visit_node(program, cond_node, &reg));

        if (reg) {

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
        assert(!node->next);

        TRY(_visit_node(program, node, &reg));
        assert(!reg);

        TRY(_add_addr2line(program, &step_node->token, _KOS_FALSE));

        step_instr_offs = program->cur_offs;

        TRY(_visit_node(program, step_node, &reg));
        assert( ! reg);

        TRY(_add_addr2line(program, &cond_node->token, _KOS_FALSE));

        TRY(_visit_node(program, cond_node, &reg));

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

        _finish_break_continue(program, step_instr_offs, old_break_offs);
    }

    program->cur_frame->last_try_scope = prev_try_scope;

_error:
    return error;
}

static int _invoke_get_iterator(struct _KOS_COMP_UNIT *program,
                                struct _KOS_REG      **reg)
{
    int               error          = KOS_SUCCESS;
    int               str_idx;
    struct _KOS_REG  *func_reg       = 0;
    struct _KOS_REG  *obj_reg        = *reg;
    struct _KOS_TOKEN token;
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

_error:
    return error;
}

static int _for_in(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int                         error;
    int                         loop_start_offs;
    int                         cond_jump_instr_offs = -1;
    int                         final_jump_instr_offs;
    int                         continue_offs;
    const struct _KOS_AST_NODE *var_node;
    const struct _KOS_AST_NODE *expr_node;
    const struct _KOS_AST_NODE *assg_node;
    struct _KOS_REG            *reg            = 0;
    struct _KOS_REG            *final_reg      = 0;
    struct _KOS_REG            *iter_reg       = 0;
    struct _KOS_REG            *item_reg       = 0;
    struct _KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    struct _KOS_SCOPE          *prev_try_scope = _push_try_scope(program);

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

    _KOS_activate_new_vars(program, assg_node->children);

    TRY(_invoke_get_iterator(program, &iter_reg));

    TRY(_add_addr2line(program, &assg_node->token, _KOS_FALSE));

    if ( ! var_node->next) {

        TRY(_lookup_local_var(program, &var_node->token, &item_reg));
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

        struct _KOS_REG *value_iter_reg = item_reg;

        TRY(_invoke_get_iterator(program, &value_iter_reg));

        for ( ; var_node; var_node = var_node->next) {

            struct _KOS_REG *var_reg = 0;

            TRY(_lookup_local_var(program, &var_node->token, &var_reg));
            assert(var_reg);

            TRY(_gen_instr4(program, INSTR_CALL_FUN, var_reg->reg, value_iter_reg->reg, 255, 0));
        }

        if (value_iter_reg != item_reg)
            _free_reg(program, value_iter_reg);
    }

    node = assg_node->next;
    assert(node);
    assert(!node->next);

    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    TRY(_add_addr2line(program, &assg_node->token, _KOS_FALSE));

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

_error:
    return error;
}

static int _restore_catch(struct _KOS_COMP_UNIT *program,
                          struct _KOS_SCOPE     *outer_scope,
                          int                    offs_idx)
{
    struct _KOS_SCOPE *cur_scope = program->scope_stack;
    int                error;

    assert(cur_scope);
    assert( ! cur_scope->is_function);

    if (outer_scope && outer_scope->catch_ref.catch_reg) {

        cur_scope->catch_ref.catch_offs[offs_idx] = program->cur_offs;

        if (offs_idx == 0) {
            assert(!cur_scope->catch_ref.next);
            cur_scope->catch_ref.next           = outer_scope->catch_ref.child_scopes;
            outer_scope->catch_ref.child_scopes = cur_scope;
        }

        error = _gen_instr2(program, INSTR_CATCH, outer_scope->catch_ref.catch_reg->reg, 0);
    }
    else
        error = _gen_instr(program, 0, INSTR_CANCEL);

    return error;
}

static int _restore_parent_scope_catch(struct _KOS_COMP_UNIT *program,
                                       int                    offs_idx)
{
    struct _KOS_SCOPE *scope = program->scope_stack;

    assert(scope && ! scope->is_function);

    scope = _find_try_scope(scope->next);

    return _restore_catch(program, scope, offs_idx);
}

static int _push_break_offs(struct _KOS_COMP_UNIT *program,
                            enum _KOS_NODE_TYPE    type)
{
    int                           error      = KOS_SUCCESS;
    struct _KOS_BREAK_OFFS *const break_offs = (struct _KOS_BREAK_OFFS *)
        _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_BREAK_OFFS));

    if (break_offs) {
        break_offs->next               = program->cur_frame->break_offs;
        break_offs->type               = type;
        program->cur_frame->break_offs = break_offs;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static int _break_continue_fallthrough(struct _KOS_COMP_UNIT      *program,
                                       const struct _KOS_AST_NODE *node)
{
    int error;

    TRY(_push_break_offs(program, node->type));

    if (program->cur_frame->last_try_scope) {

        _push_scope(program, node);

        TRY(_restore_catch(program, program->cur_frame->last_try_scope, 0));

        _pop_scope(program);
    }

    program->cur_frame->break_offs->offs = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

_error:
    return error;
}

struct _KOS_SWITCH_CASE {
    int to_jump_offs;
    int final_jump_offs;
};

static int _count_siblings(const struct _KOS_AST_NODE *node)
{
    int count = 0;

    while (node) {
        ++count;
        node = node->next;
    }

    return count;
}

static int _switch(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int                         error;
    struct _KOS_REG            *value_reg       = 0;
    const struct _KOS_AST_NODE *first_case_node;
    int                         num_cases;
    int                         i_case;
    int                         i_default_case  = -1;
    int                         final_jump_offs = -1;
    struct _KOS_SWITCH_CASE    *cases           = 0;
    struct _KOS_BREAK_OFFS     *old_break_offs  = program->cur_frame->break_offs;

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

    cases = (struct _KOS_SWITCH_CASE *)_KOS_mempool_alloc(
            &program->allocator, sizeof(struct _KOS_SWITCH_CASE) * num_cases);

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

            struct _KOS_REG            *case_reg   = 0;
            struct _KOS_REG            *result_reg = 0;
            const struct _KOS_AST_NODE *case_node;

            assert(node->children);
            assert(node->children->type != NT_EMPTY);

            case_node = _KOS_get_const(program, node->children);

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

        const struct _KOS_AST_NODE *child_node = node->children;

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

_error:
    return error;
}

static void _update_child_scope_catch(struct _KOS_COMP_UNIT *program)
{
    struct _KOS_SCOPE *scope     = program->scope_stack;
    const int          dest_offs = program->cur_offs;

    scope = scope->catch_ref.child_scopes;

    for ( ; scope; scope = scope->catch_ref.next) {
        size_t i;
        for (i = 0; i < sizeof(scope->catch_ref.catch_offs) / sizeof(int); i++) {
            const int instr_offs = scope->catch_ref.catch_offs[i];
            if (instr_offs)
                _update_jump_offs(program, instr_offs, dest_offs);
        }
    }

    program->scope_stack->catch_ref.child_scopes = 0;
}

static int _try_stmt(struct _KOS_COMP_UNIT      *program,
                     const struct _KOS_AST_NODE *node)
{
    int                         error;
    int                         jump_end_offs;
    int                         catch_offs;
    struct _KOS_REG            *except_reg     = 0;
    struct _KOS_VAR            *except_var     = 0;
    struct _KOS_RETURN_OFFS    *return_offs    = program->cur_frame->return_offs;
    struct _KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    const struct _KOS_AST_NODE *try_node       = node->children;
    const struct _KOS_AST_NODE *catch_node;
    const struct _KOS_AST_NODE *defer_node;
    struct _KOS_SCOPE          *scope;

    scope = _push_scope(program, node);

    program->cur_frame->break_offs = 0;

    assert(try_node);
    catch_node = try_node->next;
    assert(catch_node);
    defer_node = catch_node->next;
    assert(defer_node);
    assert(!defer_node->next);

    assert(catch_node->type == NT_EMPTY || defer_node->type == NT_EMPTY);

    if (catch_node->type == NT_CATCH) {

        struct _KOS_AST_NODE *variable;

        assert(defer_node->type == NT_EMPTY);

        node = catch_node->children;
        assert(node);
        assert(node->type == NT_VAR || node->type == NT_CONST);

        variable = node->children;
        assert(variable);
        assert(variable->type == NT_IDENTIFIER);
        assert(!variable->children);
        assert(!variable->next);

        except_var = _KOS_find_var(program->scope_stack->vars, &variable->token);
        assert(except_var);

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(_lookup_local_var(program, &variable->token, &except_reg));
        assert(except_reg);

        except_var->is_active = VAR_INACTIVE;

        scope->catch_ref.catch_reg = except_reg;
    }
    else {

        assert(catch_node->type == NT_EMPTY);
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

    TRY(_restore_parent_scope_catch(program, 0));

    jump_end_offs = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    /* Catch section */

    _update_child_scope_catch(program);

    _update_jump_offs(program, catch_offs, program->cur_offs);

    TRY(_restore_parent_scope_catch(program, 1));

    if (catch_node->type == NT_CATCH) {

        node = node->next;
        assert(node);
        assert(!node->next);
        assert(node->type == NT_SCOPE);

        assert(except_var->is_active == VAR_INACTIVE);
        except_var->is_active = VAR_ACTIVE;

        TRY(_scope(program, node));

        except_var->is_active = VAR_INACTIVE;
    }

    /* Defer section (defer is implemented as try-finally) */

    _update_jump_offs(program, jump_end_offs, program->cur_offs);

    if (defer_node->type == NT_SCOPE) {

        int                     skip_throw_offs;
        struct _KOS_BREAK_OFFS *try_break_offs = program->cur_frame->break_offs;

        program->cur_frame->break_offs = old_break_offs;
        old_break_offs                 = 0;

        {
            struct _KOS_RETURN_OFFS *tmp    = program->cur_frame->return_offs;
            program->cur_frame->return_offs = return_offs;
            return_offs                     = tmp;
            scope->catch_ref.finally_active = 0;
        }

        TRY(_scope(program, defer_node));

        skip_throw_offs = program->cur_offs;

        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, except_reg->reg));

        TRY(_gen_instr1(program, INSTR_THROW, except_reg->reg));

        /* Defer section for break, continue and fallthrough */

        if (try_break_offs) {

            struct _KOS_BREAK_OFFS *break_offs   = try_break_offs;
            int                     jump_offs[3] = { 0, 0, 0 };
            enum _KOS_NODE_TYPE     node_type[3] = { NT_BREAK, NT_CONTINUE, NT_FALLTHROUGH };
            int                     i;

            while (break_offs) {

                assert(break_offs->type == NT_CONTINUE ||
                       break_offs->type == NT_BREAK    ||
                       break_offs->type == NT_FALLTHROUGH);

                for (i = 0; i < (int)(sizeof(jump_offs) / sizeof(jump_offs[0])); i++)
                    if (break_offs->type == node_type[i]) {
                        jump_offs[i] = 1;
                        break;
                    }

                break_offs = break_offs->next;
            }

            for (i = 0; i < (int)(sizeof(jump_offs) / sizeof(jump_offs[0])); i++) {

                const enum _KOS_NODE_TYPE type = node_type[i];

                if ( ! jump_offs[i])
                    continue;

                for (break_offs = try_break_offs; break_offs; break_offs = break_offs->next)
                    if (break_offs->type == type)
                        _update_jump_offs(program, break_offs->offs, program->cur_offs);

                TRY(_restore_parent_scope_catch(program, i + 2));

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

            TRY(_restore_parent_scope_catch(program, 5));

            TRY(_scope(program, defer_node));

            TRY(_gen_return(program, except_reg->reg));
        }

        _update_jump_offs(program, skip_throw_offs, program->cur_offs);
    }

    if (old_break_offs) {
        if (program->cur_frame->break_offs) {

            struct _KOS_BREAK_OFFS **break_offs = &program->cur_frame->break_offs;

            while (*break_offs)
                break_offs = &(*break_offs)->next;

            *break_offs = old_break_offs;
        }
        else
            program->cur_frame->break_offs = old_break_offs;
    }

    _free_reg(program, except_reg);

    _pop_scope(program);

_error:
    return error;
}

static int _refinement_module(struct _KOS_COMP_UNIT      *program,
                              struct _KOS_VAR            *module_var,
                              const struct _KOS_AST_NODE *node, /* the second child of the refinement node */
                              struct _KOS_REG           **reg)
{
    int                error;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    if (node->type == NT_STRING_LITERAL) {

        int                   global_idx;
        const char           *begin;
        unsigned              length;
        enum _KOS_UTF8_ESCAPE escape;

        /* TODO this does not work for escaped strings, get_global_idx assumes NO_ESCAPE */
        _get_token_str(&node->token, &begin, &length, &escape);

        assert(program->get_global_idx);
        error = program->get_global_idx(program->frame, module_var->array_idx, begin, length, &global_idx);
        if (error) {
            program->error_token = &node->token;
            program->error_str   = str_err_no_such_module_variable;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        TRY(_gen_reg(program, reg));

        TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, (*reg)->reg, module_var->array_idx, global_idx));
    }
    else {

        struct _KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_dest_reg(program, reg, prop));

        TRY(_gen_instr3(program, INSTR_GET_MOD, (*reg)->reg, module_var->array_idx, prop->reg));

        if (*reg != prop)
            _free_reg(program, prop);
    }

_error:
    _KOS_vector_destroy(&cstr);

    return error;
}

static int _maybe_int(const struct _KOS_AST_NODE *node,
                      int64_t                    *value)
{
    struct _KOS_NUMERIC numeric;

    if (node->type != NT_NUMERIC_LITERAL)
        return 0;

    if (node->token.type == TT_NUMERIC_BINARY) {

        const struct _KOS_NUMERIC *ptr = (const struct _KOS_NUMERIC *)node->token.begin;

        assert(node->token.length == sizeof(struct _KOS_NUMERIC));

        numeric = *ptr;
    }
    else {

        if (KOS_SUCCESS != _KOS_parse_numeric(node->token.begin,
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

static int _refinement_object(struct _KOS_COMP_UNIT      *program,
                              const struct _KOS_AST_NODE *node,
                              struct _KOS_REG           **reg, /* the first child of the refinement node */
                              struct _KOS_REG           **out_obj)
{
    int              error;
    struct _KOS_REG *obj = 0;
    int64_t          idx;

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

        struct _KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_instr3(program, INSTR_GET, (*reg)->reg, obj->reg, prop->reg));

        _free_reg(program, prop);
    }

_error:
    return error;
}

static int _refinement(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg,
                       struct _KOS_REG           **out_obj)
{
    int              error;
    struct _KOS_VAR *module_var = 0;

    node = node->children;
    assert(node);

    if (node->type == NT_IDENTIFIER) {
        if (!_lookup_var(program, &node->token, &module_var, 0)) {
            if (module_var->type != VAR_MODULE)
                module_var = 0;
        }
    }

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

static int _slice(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node,
                  struct _KOS_REG           **reg)
{
    int              error;
    struct _KOS_REG *obj_reg   = 0;
    struct _KOS_REG *begin_reg = 0;
    struct _KOS_REG *end_reg   = 0;

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

    if (obj_reg->tmp)
        *reg = obj_reg;
    else
        TRY(_gen_reg(program, reg));

    TRY(_gen_instr4(program, INSTR_GET_RANGE, (*reg)->reg, obj_reg->reg,
                                              begin_reg->reg, end_reg->reg));

    _free_reg(program, end_reg);
    _free_reg(program, begin_reg);

_error:
    return error;
}

static int _find_var_by_reg(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_VAR *var = (struct _KOS_VAR *)node;
    struct _KOS_REG *reg = (struct _KOS_REG *)cookie;

    /* Handle local variables, arguments in registers and ellipsis. */
    /* Ignore rest arguments, which are not stored in registers.    */
    if (var->reg == reg && ! (var->type & VAR_ARGUMENT)) {

        /* Technically this is not an error, but it will stop tree iteration */
        return KOS_SUCCESS_RETURN;
    }

    return KOS_SUCCESS;
}

static int _is_var_used(struct _KOS_COMP_UNIT      *program,
                        const struct _KOS_AST_NODE *node,
                        struct _KOS_REG            *reg)
{
    if ( ! reg || reg->tmp)
        return 0;

    for ( ; node; node = node->next) {

        if (node->type == NT_IDENTIFIER) {

            struct _KOS_SCOPE *scope;

            for (scope = program->scope_stack; scope && scope->next; scope = scope->next) {

                const int error = _KOS_red_black_walk(scope->vars, _find_var_by_reg, reg);

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

static int _count_non_expanded_siblings(const struct _KOS_AST_NODE *node)
{
    int count = 0;

    while (node && node->type != NT_EXPAND) {
        ++count;
        node = node->next;
    }

    return count;
}

#define MAX_CONTIG_REGS 4

static int _count_contig_arg_siblings(const struct _KOS_AST_NODE *node)
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

static int _gen_array(struct _KOS_COMP_UNIT      *program,
                      const struct _KOS_AST_NODE *node,
                      struct _KOS_REG           **reg)
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

        struct _KOS_REG *arg    = 0;
        const int        expand = node->type == NT_EXPAND ? 1 : 0;

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

_error:
    return error;
}

static int _invocation(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg,
                       enum _KOS_BYTECODE_INSTR    instr,
                       unsigned                    tail_closure_size)
{
    int              error;
    struct _KOS_REG *obj   = 0;
    struct _KOS_REG *fun   = 0;
    struct _KOS_REG *args  = _is_var_used(program, node, *reg) ? 0 : *reg;
    int32_t          rdest = (int32_t)tail_closure_size;
    int              num_contig_args;

    assert(tail_closure_size <= 255U);

    node = node->children;
    assert(node);

    if (node->type == NT_REFINEMENT)
        TRY(_refinement(program, node, &fun, &obj));

    else {

        TRY(_visit_node(program, node, &fun));
        assert(fun);
    }

    node = node->next;

    num_contig_args = _count_contig_arg_siblings(node);

    if (num_contig_args <= MAX_CONTIG_REGS) {

        struct _KOS_REG *argn[MAX_CONTIG_REGS] = { 0, 0, 0, 0 };
        int              i;

        if (num_contig_args > 1)
            TRY(_gen_reg_range(program, &argn[0], num_contig_args));

        for (i = 0; node; node = node->next, i++) {

            struct _KOS_REG *arg = argn[i];

            assert(i == 0 || arg->reg == argn[i-1]->reg + 1);

            TRY(_visit_node(program, node, &arg));

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
            struct _KOS_REG *arg = argn[--num_contig_args];
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

_error:
    return error;
}

enum _CHECK_TYPE {
    CHECK_NUMERIC           = 1,
    CHECK_STRING            = 2,
    CHECK_NUMERIC_OR_STRING = 3
};

static int _check_const_literal(struct _KOS_COMP_UNIT      *program,
                                const struct _KOS_AST_NODE *node,
                                enum _CHECK_TYPE            expected_type)
{
    enum _KOS_NODE_TYPE         cur_node_type;
    const struct _KOS_AST_NODE *const_node = _KOS_get_const(program, node);

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

static int _pos_neg(struct _KOS_COMP_UNIT      *program,
                    const struct _KOS_AST_NODE *node,
                    struct _KOS_REG           **reg)
{
    int                           error;
    const enum _KOS_OPERATOR_TYPE op  = node->token.op;
    struct _KOS_REG              *src = *reg;

    assert(op == OT_ADD || op == OT_SUB);

    node = node->children;
    assert(node);
    assert(!node->next);

    TRY(_check_const_literal(program, node, CHECK_NUMERIC));

    TRY(_visit_node(program, node, &src));
    assert(src);

    if (op == OT_SUB) {

        struct _KOS_REG *val = 0;

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

_error:
    return error;
}

static int _log_not(struct _KOS_COMP_UNIT      *program,
                    const struct _KOS_AST_NODE *node,
                    struct _KOS_REG           **reg)
{
    int              error;
    int              offs1;
    int              offs2;
    struct _KOS_REG *src = *reg;

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

_error:
    return error;
}

static int _log_and_or(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg)
{
    int                           error;
    int                           offs;
    const enum _KOS_OPERATOR_TYPE op    = node->token.op;
    struct _KOS_REG              *left  = *reg;
    struct _KOS_REG              *right = 0;

    assert(op == OT_LOGAND || op == OT_LOGOR);

    node = node->children;
    assert(node);
    assert(node->next);

    if ( ! left || ! left->tmp) {
        left = 0;
        TRY(_gen_reg(program, &left));
    }

    TRY(_visit_node(program, node, &left));
    assert(left);

    node = node->next;
    assert(node);
    assert(!node->next);

    offs = program->cur_offs;

    if (op == OT_LOGAND)
        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, left->reg));
    else
        TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, left->reg));

    right = left;

    TRY(_visit_node(program, node, &right));
    assert(right);

    if (left != right) {
        TRY(_gen_instr2(program, INSTR_MOVE, left->reg, right->reg));
        _free_reg(program, right);
    }

    _update_jump_offs(program, offs, program->cur_offs);

    if ( ! *reg)
        *reg = left;
    else if (*reg != left) {
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, left->reg));
        _free_reg(program, left);
    }

_error:
    return error;
}

static int _log_tri(struct _KOS_COMP_UNIT      *program,
                    const struct _KOS_AST_NODE *node,
                    struct _KOS_REG           **reg)
{
    int              error;
    int              offs1;
    int              offs2;
    int              offs3;
    int              offs4;
    struct _KOS_REG *cond_reg = 0;
    struct _KOS_REG *src      = *reg;

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

_error:
    return error;
}

static int _has_prop(struct _KOS_COMP_UNIT      *program,
                     const struct _KOS_AST_NODE *node,
                     struct _KOS_REG           **reg)
{
    int              error;
    int              str_idx;
    struct _KOS_REG *src = *reg;

    TRY(_visit_node(program, node->children, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    TRY(_gen_str(program, &node->children->next->token, &str_idx));

    TRY(_gen_instr3(program, INSTR_HAS_PROP, (*reg)->reg, src->reg, str_idx));

    if (src != *reg)
        _free_reg(program, src);

_error:
    return error;
}

static int _delete(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node,
                   struct _KOS_REG           **reg)
{
    int              error;
    struct _KOS_REG *obj = 0;

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

        struct _KOS_REG *prop = 0;

        TRY(_visit_node(program, node, &prop));
        assert(prop);

        TRY(_gen_instr2(program, INSTR_DEL, obj->reg, prop->reg));

        _free_reg(program, prop);
    }

    _free_reg(program, obj);

    TRY(_gen_reg(program, reg));
    TRY(_gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg));

_error:
    return error;
}

static int _operator(struct _KOS_COMP_UNIT      *program,
                     const struct _KOS_AST_NODE *node,
                     struct _KOS_REG           **reg)
{
    int                           error    = KOS_SUCCESS;
    const enum _KOS_OPERATOR_TYPE op       = node->token.op;
    const enum _KOS_KEYWORD_TYPE  kw       = node->token.keyword;
    struct _KOS_REG              *reg1     = 0;
    struct _KOS_REG              *reg2     = 0;
    int                           opcode   = 0;
    int                           operands = 0;
    int                           swap     = 0;

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
                        const struct _KOS_AST_NODE *second = node->children->next;
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
                const struct _KOS_AST_NODE *const_a = _KOS_get_const(program, node);
                const struct _KOS_AST_NODE *const_b;
                assert(node->next);
                const_b = _KOS_get_const(program, node->next);

                if (const_a) {
                    if (const_b) {
                        const enum _KOS_NODE_TYPE a_type = const_a->type;
                        const enum _KOS_NODE_TYPE b_type = const_b->type;

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
            struct _KOS_REG *tmp = reg1;
            reg1                 = reg2;
            reg2                 = tmp;
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

_error:
    return error;
}

static int _assign_instr(enum _KOS_OPERATOR_TYPE op)
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

static int _assign_member(struct _KOS_COMP_UNIT      *program,
                          enum _KOS_OPERATOR_TYPE     assg_op,
                          const struct _KOS_AST_NODE *node,
                          struct _KOS_REG            *src)
{
    int              error;
    int              str_idx = 0;
    int64_t          idx     = 0;
    struct _KOS_REG *obj     = 0;
    struct _KOS_REG *tmp_reg = 0;

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

        struct _KOS_REG *prop    = 0;

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

_error:
    return error;
}

static int _assign_non_local(struct _KOS_COMP_UNIT      *program,
                             enum _KOS_OPERATOR_TYPE     assg_op,
                             const struct _KOS_AST_NODE *node,
                             struct _KOS_REG            *src)
{
    int              error;
    struct _KOS_VAR *var     = 0;
    struct _KOS_REG *tmp_reg = 0;
    struct _KOS_REG *container_reg;

    assert(node->type == NT_IDENTIFIER);

    TRY(_lookup_var(program, &node->token, &var, &container_reg));

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

_error:
    return error;
}

static int _assign_slice(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *node,
                         struct _KOS_REG            *src)
{
    int                         error;
    int                         str_idx;
    const struct _KOS_AST_NODE *obj_node     = 0;
    struct _KOS_REG            *argn[3]      = { 0, 0, 0 };
    struct _KOS_REG            *obj_reg      = 0;
    struct _KOS_REG            *func_reg     = 0;
    const int                   src_reg      = src->reg;
    static const char           str_insert[] = "insert";
    struct _KOS_TOKEN           token;

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
        assert( ! obj_reg->tmp);
        TRY(_gen_instr2(program, INSTR_MOVE, argn[0]->reg, obj_reg->reg));
    }

    node = node->next;
    assert(node);
    assert( ! node->next);

    obj_reg = argn[1];
    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    if (obj_reg != argn[1]) {
        assert( ! obj_reg->tmp);
        TRY(_gen_instr2(program, INSTR_MOVE, argn[1]->reg, obj_reg->reg));
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

_error:
    return error;
}

static int _assignment(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *assg_node)
{
    int                         error;
    int                         is_lhs;
    const struct _KOS_AST_NODE *node;
    const struct _KOS_AST_NODE *rhs_node;
    struct _KOS_REG            *reg       = 0;
    struct _KOS_REG            *rhs       = 0;
    const enum _KOS_NODE_TYPE   node_type = assg_node->type;

    assert(node_type == NT_ASSIGNMENT || node_type == NT_MULTI_ASSIGNMENT);

    node = assg_node->children;
    assert(node);

    rhs_node = node->next;
    assert(rhs_node);
    assert(!rhs_node->next);

    assert(node->type == NT_LEFT_HAND_SIDE ||
           node->type == NT_VAR ||
           node->type == NT_CONST);

    is_lhs = (node->type == NT_LEFT_HAND_SIDE) ? 1 : 0;

    assert(is_lhs || (node->children && node->children->type == NT_IDENTIFIER));

    node = node->children;
    assert(node);

    if (node_type == NT_ASSIGNMENT) {

        assert( ! node->next);

        if (assg_node->token.op != OT_SET)
            /* TODO check lhs variable type */
            TRY(_check_const_literal(program, rhs_node,
                    assg_node->token.op == OT_SETADD ? CHECK_NUMERIC_OR_STRING : CHECK_NUMERIC));

        if (node->type == NT_IDENTIFIER)
            TRY(_lookup_local_var_even_inactive(program, &node->token, is_lhs, &reg));

        if (reg && assg_node->token.op == OT_SET)
            rhs = reg;
    }

    TRY(_visit_node(program, rhs_node, &rhs));
    assert(rhs);

    if (node_type == NT_MULTI_ASSIGNMENT)
        TRY(_invoke_get_iterator(program, &rhs));

    for ( ; node; node = node->next) {

        if ( ! reg && node->type == NT_IDENTIFIER)
            TRY(_lookup_local_var_even_inactive(program, &node->token, is_lhs, &reg));

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
                _KOS_activate_var(program, node);
        }
        else {

            if ( ! is_lhs)
                _KOS_activate_var(program, node);

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

            else {

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

_error:
    return error;
}

static int _interpolated_string(struct _KOS_COMP_UNIT      *program,
                                const struct _KOS_AST_NODE *node,
                                struct _KOS_REG           **reg)
{
    int               error        = KOS_SUCCESS;
    int               string_idx   = 0;
    struct _KOS_REG  *func_reg     = 0;
    struct _KOS_REG  *args         = *reg;
    static const char str_string[] = "stringify";

    assert(program->get_global_idx);
    error = program->get_global_idx(program->frame, 0, str_string, sizeof(str_string)-1, &string_idx);
    if (error) {
        program->error_token = &node->token;
        program->error_str   = str_err_no_such_module_variable;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }

    /* TODO use INSTR_CALL_FUN if possible, reuse portion of _invocation() */

    TRY(_gen_array(program, node->children, &args));

    if ( ! *reg)
        *reg = args;

    TRY(_gen_reg(program, &func_reg));

    TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, func_reg->reg, 0, string_idx));

    TRY(_gen_instr4(program, INSTR_CALL, (*reg)->reg, func_reg->reg, args->reg, args->reg));

    _free_reg(program, func_reg);
    if (args != *reg)
        _free_reg(program, args);

_error:
    return error;
}

static int _expression_list(struct _KOS_COMP_UNIT      *program,
                            const struct _KOS_AST_NODE *node,
                            struct _KOS_REG           **reg)
{
    int error = KOS_SUCCESS;

    node = node->children;

    for ( ; node; node = node->next) {

        struct _KOS_REG *tmp_reg = 0;

        error = _add_addr2line(program, &node->token, _KOS_FALSE);
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

static int _identifier(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg)
{
    int              error   = KOS_SUCCESS;
    struct _KOS_REG *src_reg = 0;

    TRY(_lookup_local_var(program, &node->token, &src_reg));

    if (src_reg)
        *reg = src_reg;

    else {

        struct _KOS_VAR *var = 0;
        struct _KOS_REG *container_reg;

        TRY(_gen_reg(program, reg));

        TRY(_lookup_var(program, &node->token, &var, &container_reg));

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
                TRY(_gen_instr3(program, INSTR_GET_ELEM, (*reg)->reg, container_reg->reg, var->array_idx));
                break;
        }
    }

_error:
    return error;
}

static int _numeric_literal(struct _KOS_COMP_UNIT      *program,
                            const struct _KOS_AST_NODE *node,
                            struct _KOS_REG           **reg)
{
    struct _KOS_NUMERIC numeric;
    int                 error;

    TRY(_gen_reg(program, reg));

    if (node->token.type == TT_NUMERIC_BINARY) {

        const struct _KOS_NUMERIC *value = (const struct _KOS_NUMERIC *)node->token.begin;

        assert(node->token.length == sizeof(struct _KOS_NUMERIC));

        numeric = *value;
    }
    else
        error = _KOS_parse_numeric(node->token.begin,
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

        struct _KOS_COMP_CONST *constant =
            (struct _KOS_COMP_CONST *)_KOS_red_black_find(program->constants,
                                                          &numeric,
                                                          _numbers_compare_item);

        if ( ! constant) {
            constant = (struct _KOS_COMP_CONST *)
                _KOS_mempool_alloc(&program->allocator,
                                   KOS_max(sizeof(struct _KOS_COMP_INTEGER),
                                           sizeof(struct _KOS_COMP_FLOAT)));

            if (numeric.type == KOS_INTEGER_VALUE) {
                constant->type = KOS_COMP_CONST_INTEGER;
                ((struct _KOS_COMP_INTEGER *)constant)->value = numeric.u.i;
            }
            else {
                constant->type = KOS_COMP_CONST_FLOAT;
                ((struct _KOS_COMP_FLOAT *)constant)->value = numeric.u.d;
            }

            _add_constant(program, constant);
        }

        TRY(_gen_instr2(program,
                        constant->index < 256 ? INSTR_LOAD_CONST8 : INSTR_LOAD_CONST,
                        (*reg)->reg,
                        (int32_t)constant->index));
    }

_error:
    return error;
}

static int _string_literal(struct _KOS_COMP_UNIT      *program,
                           const struct _KOS_AST_NODE *node,
                           struct _KOS_REG           **reg)
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

static int _this_literal(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *node,
                         struct _KOS_REG           **reg)
{
    assert(program->cur_frame->this_reg);
    *reg = program->cur_frame->this_reg;
    return KOS_SUCCESS;
}

static int _bool_literal(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *node,
                         struct _KOS_REG           **reg)
{
    int error = _gen_reg(program, reg);

    const int opcode = node->token.keyword == KW_TRUE ? INSTR_LOAD_TRUE : INSTR_LOAD_FALSE;

    if (!error)
        error = _gen_instr1(program, opcode, (*reg)->reg);

    return error;
}

static int _void_literal(struct _KOS_COMP_UNIT      *program,
                         const struct _KOS_AST_NODE *node,
                         struct _KOS_REG           **reg)
{
    int error = _gen_reg(program, reg);

    if (!error)
        error = _gen_instr1(program, INSTR_LOAD_VOID, (*reg)->reg);

    return error;
}

static int _gen_closure_regs(struct _KOS_RED_BLACK_NODE *node,
                             void                       *cookie)
{
    int error = KOS_SUCCESS;

    struct _KOS_SCOPE_REF *ref     = (struct _KOS_SCOPE_REF *)node;
    struct _KOS_COMP_UNIT *program = (struct _KOS_COMP_UNIT *)cookie;

    if (ref->exported_locals) {
        error = _gen_reg(program, &ref->vars_reg);
        if ( ! error)
            ref->vars_reg->tmp = 0;
    }

    if ( ! error && ref->exported_args) {
        error = _gen_reg(program, &ref->args_reg);
        if ( ! error)
            ref->args_reg->tmp = 0;
    }

    return error;
}

struct _BIND_ARGS {
    struct _KOS_COMP_UNIT *program;
    struct _KOS_REG       *func_reg;
    struct _KOS_FRAME     *parent_frame;
    int                    delta;
};

static int _gen_binds(struct _KOS_RED_BLACK_NODE *node,
                      void                       *cookie)
{
    int error = KOS_SUCCESS;

    struct _KOS_SCOPE_REF *ref     = (struct _KOS_SCOPE_REF *)node;
    struct _BIND_ARGS     *args    = (struct _BIND_ARGS *)    cookie;
    struct _KOS_COMP_UNIT *program = args->program;
    const int              delta   = args->delta;

    if (ref->exported_locals) {

        assert(ref->vars_reg);
        assert(ref->vars_reg->reg >= delta);

        if (args->parent_frame == ref->closure->frame)
            TRY(_gen_instr2(program,
                            INSTR_BIND_SELF,
                            args->func_reg->reg,
                            ref->vars_reg->reg - delta));
        else {

            struct _KOS_SCOPE_REF *other_ref =
                    _KOS_find_scope_ref(args->parent_frame, ref->closure);

            TRY(_gen_instr3(program,
                            INSTR_BIND,
                            args->func_reg->reg,
                            ref->vars_reg->reg - delta,
                            other_ref->vars_reg->reg));
        }
    }

    if (ref->exported_args) {

        struct _KOS_REG *reg;

        assert(ref->args_reg);
        assert(ref->args_reg->reg >= delta);

        if (args->parent_frame == ref->closure->frame) {
            assert(args->parent_frame->args_reg);
            reg = args->parent_frame->args_reg;
        }
        else {

            struct _KOS_SCOPE_REF *other_ref =
                    _KOS_find_scope_ref(args->parent_frame, ref->closure);

            reg = other_ref->args_reg;
        }

        TRY(_gen_instr3(program,
                        INSTR_BIND,
                        args->func_reg->reg,
                        ref->args_reg->reg - delta,
                        reg->reg));
    }

_error:
    return error;
}

static int _free_arg_regs(struct _KOS_RED_BLACK_NODE *node,
                          void                       *cookie)
{
    struct _KOS_VAR       *var     = (struct _KOS_VAR *)node;
    struct _KOS_COMP_UNIT *program = (struct _KOS_COMP_UNIT *)cookie;

    if ((var->type & VAR_ARGUMENT_IN_REG) && var->reg->tmp) {
        _free_reg(program, var->reg);
        var->reg = 0;
    }

    return KOS_SUCCESS;
}

static int _function_literal(struct _KOS_COMP_UNIT      *program,
                             const struct _KOS_AST_NODE *node,
                             struct _KOS_REG           **reg)
{
    int                error           = KOS_SUCCESS;
    struct _KOS_SCOPE *scope           = _push_scope(program, node);
    struct _KOS_FRAME *frame           = scope->frame;
    struct _KOS_FRAME *last_frame      = program->cur_frame;
    struct _KOS_VAR   *var;
    struct _KOS_REG   *scope_reg       = 0;
    struct _KOS_REG   *ellipsis_reg    = 0;
    int                fun_start_offs;
    size_t             addr2line_start_offs;
    struct _BIND_ARGS  bind_args;
    int                num_def         = 0;
    int                num_non_def     = 0;
#ifndef NDEBUG
    int                last_reg        = -1;
#endif

    const struct _KOS_AST_NODE *fun_node = node;
    const struct _KOS_AST_NODE *open_node;
    const struct _KOS_AST_NODE *name_node;

    assert(frame);

    frame->fun_token    = &fun_node->token;
    frame->parent_frame = last_frame;
    frame->program_offs = program->cur_offs; /* Temp, for load_offs, overwritten in _append_frame() */
    frame->load_offs    = program->cur_offs - last_frame->program_offs;
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

        struct _KOS_AST_NODE *arg_node  = fun_node->children;
        int                   rest_used = 0;
        int                   i;

        assert(arg_node);
        assert(arg_node->type == NT_NAME || arg_node->type == NT_NAME_CONST);
        arg_node = arg_node->next;
        assert(arg_node->type == NT_PARAMETERS);
        arg_node = arg_node->children;

        for (i = 0; arg_node && arg_node->type != NT_ELLIPSIS; arg_node = arg_node->next, ++i) {

            struct _KOS_AST_NODE *ident_node =
                arg_node->type == NT_IDENTIFIER ? arg_node : arg_node->children;

            var = _KOS_find_var(scope->vars, &ident_node->token);
            assert(var);

            if (arg_node->type == NT_IDENTIFIER)
                ++num_non_def;
            else
                ++num_def;

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
    bind_args.delta = frame->this_reg->reg + 1;
    if (scope->uses_this)
        frame->this_reg->tmp = 0;

    /* Generate registers for closures */
    TRY(_KOS_red_black_walk(frame->closures, _gen_closure_regs, program));

    name_node = fun_node->children;
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

    TRY(_add_addr2line(program, &open_node->token, _KOS_TRUE));

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
        TRY(_KOS_red_black_walk(scope->vars, _free_arg_regs, program));

    /* Generate code for function body */
    TRY(_visit_node(program, node, &scope_reg));
    assert(!scope_reg);

    /* Move the function code to final code_buf */
    TRY(_append_frame(program, name_node, fun_start_offs, addr2line_start_offs));

    program->cur_frame = last_frame;

    TRY(_add_addr2line(program, &fun_node->token, _KOS_FALSE));

    /* Generate LOAD.FUN/LOAD.GEN/LOAD.CTOR instruction in the parent frame */
    assert(frame->num_regs > 0);
    assert(frame->num_regs >= bind_args.delta);
    TRY(_gen_reg(program, reg));
    TRY(_gen_instr6(program,
                    fun_node->type == NT_CONSTRUCTOR_LITERAL
                        ? INSTR_LOAD_CTOR
                        : frame->yield_token ? INSTR_LOAD_GEN : INSTR_LOAD_FUN,
                    (*reg)->reg,
                    0,
                    frame->num_regs,
                    scope->num_indep_vars,
                    num_non_def,
                    scope->ellipsis ? KOS_FUN_ELLIPSIS : 0));

    /* Generate BIND instructions in the parent frame */
    bind_args.program      = program;
    bind_args.func_reg     = *reg;
    bind_args.parent_frame = last_frame;
    TRY(_KOS_red_black_walk(frame->closures, _gen_binds, &bind_args));

    program->cur_frame = frame;
    _pop_scope(program);
    program->cur_frame = last_frame;

    /* Free register objects */
    _free_all_regs(program, frame->used_regs);
    _free_all_regs(program, frame->free_regs);

    /* Find the first default arg */
    if (num_def) {
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
    var = 0;
    if (name_node->type == NT_NAME_CONST) {
        assert(name_node->children);
        assert(name_node->children->type == NT_IDENTIFIER);
        assert(name_node->children->token.type == TT_IDENTIFIER);

        var = _KOS_find_var(program->scope_stack->vars, &name_node->children->token);
        assert(var);
        assert((var->type & VAR_LOCAL) || var->type == VAR_GLOBAL);

        if (var->type & VAR_LOCAL) {
            assert(var->is_active == VAR_ALWAYS_ACTIVE);
            var->is_active = VAR_INACTIVE;
        }
        else
            var = 0;
    }

    /* Generate array with default args */
    if (scope->num_args > num_non_def) {

        int              i;
        struct _KOS_REG *defaults_reg = 0;

        TRY(_gen_reg(program, &defaults_reg));

        if (num_def < 256)
            TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, defaults_reg->reg, num_def));
        else
            TRY(_gen_instr2(program, INSTR_LOAD_ARRAY, defaults_reg->reg, num_def));

        for (i = 0; node && node->type == NT_ASSIGNMENT; node = node->next, ++i) {

            const struct _KOS_AST_NODE *def_node = node->children;
            struct _KOS_REG            *arg = 0;

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
    else if (num_def) {

        for ( ; node && node->type == NT_ASSIGNMENT; node = node->next) {

            const struct _KOS_AST_NODE *def_node = node->children;
            enum _KOS_NODE_TYPE         type;

            assert(def_node);
            assert(def_node->type == NT_IDENTIFIER);
            def_node = def_node->next;
            assert(def_node);
            assert( ! def_node->next);

            def_node = _KOS_get_const(program, def_node);

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

                struct _KOS_REG *out = 0;

                TRY(_visit_node(program, def_node, &out));
                assert(out);

                _free_reg(program, out);
            }
        }
    }

    if (var)
        var->is_active = VAR_ALWAYS_ACTIVE;

_error:
    return error;
}

static int _array_literal(struct _KOS_COMP_UNIT      *program,
                          const struct _KOS_AST_NODE *node,
                          struct _KOS_REG           **reg)
{
    struct _KOS_REG *array_reg = *reg;

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

struct _KOS_OBJECT_PROP_DUPE {
    struct _KOS_RED_BLACK_NODE rb_tree_node;
    int                        str_idx;
};

static int _prop_compare_item(void                       *what,
                              struct _KOS_RED_BLACK_NODE *node)
{
    const int                     str_idx   = (int)(intptr_t)what;
    struct _KOS_OBJECT_PROP_DUPE *prop_node = (struct _KOS_OBJECT_PROP_DUPE *)node;

    return str_idx - prop_node->str_idx;
}

static int _prop_compare_node(struct _KOS_RED_BLACK_NODE *a,
                              struct _KOS_RED_BLACK_NODE *b)
{
    struct _KOS_OBJECT_PROP_DUPE *a_node = (struct _KOS_OBJECT_PROP_DUPE *)a;
    struct _KOS_OBJECT_PROP_DUPE *b_node = (struct _KOS_OBJECT_PROP_DUPE *)b;

    return a_node->str_idx - b_node->str_idx;
}

static int _object_literal(struct _KOS_COMP_UNIT      *program,
                           const struct _KOS_AST_NODE *node,
                           struct _KOS_REG           **reg)
{
    int                         error;
    struct _KOS_RED_BLACK_NODE *prop_str_idcs = 0;

    TRY(_gen_reg(program, reg));
    TRY(_gen_instr1(program, INSTR_LOAD_OBJ, (*reg)->reg));

    for (node = node->children; node; node = node->next) {

        int                         str_idx;
        const struct _KOS_AST_NODE *prop_node = node->children;
        struct _KOS_REG            *prop      = 0;

        assert(node->type == NT_PROPERTY);
        assert(prop_node);
        assert(prop_node->type == NT_STRING_LITERAL);

        TRY(_gen_str(program, &prop_node->token, &str_idx));

        if (_KOS_red_black_find(prop_str_idcs, (void *)(intptr_t)str_idx, _prop_compare_item)) {
            program->error_token = &prop_node->token;
            program->error_str   = str_err_duplicate_property;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }
        else {
            struct _KOS_OBJECT_PROP_DUPE *new_node = (struct _KOS_OBJECT_PROP_DUPE *)
                _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_OBJECT_PROP_DUPE));

            if ( ! new_node)
                RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

            new_node->str_idx = str_idx;

            _KOS_red_black_insert(&prop_str_idcs,
                                  (struct _KOS_RED_BLACK_NODE *)new_node,
                                  _prop_compare_node);
        }

        prop_node = prop_node->next;
        assert(prop_node);
        assert(!prop_node->next);

        TRY(_visit_node(program, prop_node, &prop));
        assert(prop);

        TRY(_gen_instr3(program, INSTR_SET_PROP, (*reg)->reg, str_idx, prop->reg));

        _free_reg(program, prop);
    }

_error:
    return error;
}

static int _class_literal(struct _KOS_COMP_UNIT      *program,
                          const struct _KOS_AST_NODE *node,
                          struct _KOS_REG           **reg)
{
    int              error     = KOS_SUCCESS;
    struct _KOS_REG *proto_reg = 0;

    assert(node->children);
    node = node->children;
    assert(node->type == NT_OBJECT_LITERAL);
    assert(node->next);

    if (node->children) {
        TRY(_object_literal(program, node, &proto_reg));
        assert(proto_reg);
    }

    node = node->next;
    assert(node->type == NT_CONSTRUCTOR_LITERAL);
    assert( ! node->next);

    TRY(_function_literal(program, node, reg));
    assert(*reg);

    if (proto_reg) {

        static const char str_prototype[] = "prototype";
        int               str_idx         = 0;
        struct _KOS_TOKEN token;

        memset(&token, 0, sizeof(token));
        token.begin  = str_prototype;
        token.length = sizeof(str_prototype) - 1;
        token.type   = TT_IDENTIFIER;

        TRY(_gen_str(program, &token, &str_idx));

        TRY(_gen_instr3(program, INSTR_SET_PROP, (*reg)->reg, str_idx, proto_reg->reg));

        _free_reg(program, proto_reg);
    }

_error:
    return error;
}

/* For this function and all other similar functions which it invokes, reg is:
    - on input, the desired register in which we prefer the return value
    - on output, the actual register containing the value computed
*/
static int _visit_node(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg)
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
        case NT_STREAM:
            error = _stream(program, node, reg);
            break;
        case NT_THROW:
            error = _throw(program, node);
            break;
        case NT_ASSERT:
            error = _assert(program, node);
            break;
        case NT_REPEAT:
            error = _repeat(program, node);
            break;
        case NT_WHILE:
            error = _while(program, node);
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
        case NT_TRY:
            error = _try_stmt(program, node);
            break;
        case NT_REFINEMENT:
            error = _refinement(program, node, reg, 0);
            break;
        case NT_SLICE:
            error = _slice(program, node, reg);
            break;
        case NT_INVOCATION:
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
        case NT_INTERPOLATED_STRING:
            error = _interpolated_string(program, node, reg);
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
        case NT_BOOL_LITERAL:
            error = _bool_literal(program, node, reg);
            break;
        case NT_FUNCTION_LITERAL:
            /* fall through */
        case NT_CONSTRUCTOR_LITERAL:
            error = _function_literal(program, node, reg);
            break;
        case NT_ARRAY_LITERAL:
            error = _array_literal(program, node, reg);
            break;
        case NT_OBJECT_LITERAL:
            error = _object_literal(program, node, reg);
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

void _KOS_compiler_init(struct _KOS_COMP_UNIT *program,
                        int                    file_id)
{
    memset(program, 0, sizeof(*program));

    program->optimize = 1;
    program->file_id  = file_id;

    _KOS_mempool_init(&program->allocator);

    _KOS_vector_init(&program->code_buf);
    _KOS_vector_init(&program->code_gen_buf);
    _KOS_vector_init(&program->addr2line_buf);
    _KOS_vector_init(&program->addr2line_gen_buf);
    _KOS_vector_init(&program->addr2func_buf);
}

int _KOS_compiler_compile(struct _KOS_COMP_UNIT *program,
                          struct _KOS_AST_NODE  *ast)
{
    int              error;
    int              num_optimizations;
    struct _KOS_REG *reg = 0;

    TRY(_KOS_vector_reserve(&program->code_buf,          1024));
    TRY(_KOS_vector_reserve(&program->code_gen_buf,      1024));
    TRY(_KOS_vector_reserve(&program->addr2line_buf,     1024));
    TRY(_KOS_vector_reserve(&program->addr2line_gen_buf, 256));
    TRY(_KOS_vector_reserve(&program->addr2func_buf,     256));

    TRY(_KOS_compiler_process_vars(program, ast));

    do {
        num_optimizations = program->num_optimizations;
        TRY(_KOS_optimize(program, ast));
    }
    while (program->num_optimizations > num_optimizations);

    TRY(_KOS_allocate_args(program, ast));

    TRY(_visit_node(program, ast, &reg));
    assert(!reg);

_error:
    return error;
}

void _KOS_compiler_destroy(struct _KOS_COMP_UNIT *program)
{
    program->pre_globals = 0;

    _KOS_vector_destroy(&program->code_gen_buf);
    _KOS_vector_destroy(&program->code_buf);
    _KOS_vector_destroy(&program->addr2line_gen_buf);
    _KOS_vector_destroy(&program->addr2line_buf);
    _KOS_vector_destroy(&program->addr2func_buf);

    _KOS_mempool_destroy(&program->allocator);
}

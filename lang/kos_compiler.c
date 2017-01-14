/*
 * Copyright (c) 2014-2017 Chris Dragan
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
#include "kos_misc.h"
#include "../inc/kos_error.h"
#include "../inc/kos_bytecode.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char str_err_duplicate_property[]        = "duplicate object property";
static const char str_err_expected_refinement[]       = "expected .identifier or '[' in argument to 'delete'";
static const char str_err_expected_refinement_ident[] = "expected identifier";
static const char str_err_invalid_case[]              = "case expression does not resolve to an immutable constant";
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

static int _gen_reg(struct _KOS_COMP_UNIT *program,
                    struct _KOS_REG      **out_reg)
{
    int error = KOS_SUCCESS;

    if (!*out_reg) {
        struct _KOS_FRAME *frame = program->cur_frame;
        struct _KOS_REG   *reg   = frame->free_regs;

        if (!reg) {
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
        }

        if (reg) {
            if (frame->used_regs)
                frame->used_regs->prev = reg;
            if (frame->free_regs == reg)
                frame->free_regs = reg->next;
            reg->next        = frame->used_regs;
            reg->prev        = 0;
            frame->used_regs = reg;

            reg->tmp = 1;

            *out_reg = reg;
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
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
        reg->next = *reg_ptr;
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
            assert((var->type & VAR_ARGUMENT) != VAR_ARGUMENT);

            if ( ! var->reg) {
                error = _gen_reg(program, &var->reg);
                var->reg->tmp = 0;
            }

            *reg = var->reg;
            break;
        }

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

        const int is_var = var->type == VAR_INDEPENDENT_LOCAL;

        *out_var = var;

        if (is_local_arg) {
            if (reg) {
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
            assert((ref->exported_types & var->type) == var->type);

            if (reg) {
                if (is_var)
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

static int _compare_strings(const char *a, unsigned len_a,
                            const char *b, unsigned len_b)
{
    const unsigned min_len = (len_a <= len_b) ? len_a : len_b;

    /* TODO do proper unicode compare */
    int result = memcmp(a, b, min_len);

    if (result == 0)
        result = (int)len_a - (int)len_b;

    return result;
}

static void _get_token_str(const struct _KOS_TOKEN *token,
                           const char             **out_begin,
                           unsigned                *out_length)
{
    const char *begin  = token->begin;
    unsigned    length = token->length;

    if (token->type >= TT_STRING) { /* TT_STRING* */
        assert(token->type == TT_STRING         ||
               token->type == TT_STRING_OPEN_SQ ||
               token->type == TT_STRING_OPEN_DQ);
        ++begin;
        length -= 2;
        if (token->type > TT_STRING) /* TT_STRING_OPEN_* */
            --length;
        else {
            assert(token->type == TT_STRING);
        }
    }
    else {
        assert(token->type == TT_IDENTIFIER || token->type == TT_KEYWORD);
    }

    *out_begin  = begin;
    *out_length = length;
}

static int _strings_compare_item(void                       *what,
                                 struct _KOS_RED_BLACK_NODE *node)
{
    const struct _KOS_TOKEN       *token = (const struct _KOS_TOKEN       *)what;
    const struct _KOS_COMP_STRING *str   = (const struct _KOS_COMP_STRING *)node;
    const char                    *begin;
    unsigned                       length;

    _get_token_str(token, &begin, &length);

    return _compare_strings(begin,    length,
                            str->str, str->length);
}

static int _strings_compare_node(struct _KOS_RED_BLACK_NODE *a,
                                 struct _KOS_RED_BLACK_NODE *b)
{
    const struct _KOS_COMP_STRING *str_a = (const struct _KOS_COMP_STRING *)a;
    const struct _KOS_COMP_STRING *str_b = (const struct _KOS_COMP_STRING *)b;

    return _compare_strings(str_a->str, str_a->length,
                            str_b->str, str_b->length);
}

static int _gen_str_esc(struct _KOS_COMP_UNIT   *program,
                        const struct _KOS_TOKEN *token,
                        enum _KOS_UTF8_ESCAPE    escape,
                        int                     *str_idx)
{
    int error = KOS_SUCCESS;

    struct _KOS_COMP_STRING *str =
            (struct _KOS_COMP_STRING *)_KOS_red_black_find(program->strings,
                                                           (struct _KOS_TOKEN *)token,
                                                           _strings_compare_item);

    if (!str) {

        str = (struct _KOS_COMP_STRING *)
            _KOS_mempool_alloc(&program->allocator, sizeof(struct _KOS_COMP_STRING));

        if (str) {

            const char *begin;
            unsigned    length;

            _get_token_str(token, &begin, &length);

            str->index  = program->num_strings++;
            str->next   = 0;
            str->str    = begin;
            str->length = length;
            str->escape = escape;

            if (program->last_string)
                program->last_string->next = str;
            else
                program->string_list = str;

            program->last_string = str;

            _KOS_red_black_insert(&program->strings,
                                  (struct _KOS_RED_BLACK_NODE *)str,
                                  _strings_compare_node);
        }
        else
            error = KOS_ERROR_OUT_OF_MEMORY;
    }

    if (!error)
        *str_idx = str->index;

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

static int _get_num_operands(enum _KOS_BYTECODE_INSTR instr)
{
    switch (instr) {

        case INSTR_BREAKPOINT:          /* fall through */
        case INSTR_CATCH_CANCEL:        /* fall through */
        default:
            assert(instr == INSTR_BREAKPOINT || instr == INSTR_CATCH_CANCEL);
            return 0;

        case INSTR_LOAD_TRUE:           /* fall through */
        case INSTR_LOAD_FALSE:          /* fall through */
        case INSTR_LOAD_VOID:           /* fall through */
        case INSTR_LOAD_OBJ:            /* fall through */
        case INSTR_JUMP:                /* fall through */
        case INSTR_YIELD:               /* fall through */
        case INSTR_THROW:
            return 1;

        case INSTR_LOAD_INT8:           /* fall through */
        case INSTR_LOAD_INT32:          /* fall through */
        case INSTR_LOAD_STR:            /* fall through */
        case INSTR_LOAD_ARRAY8:         /* fall through */
        case INSTR_LOAD_ARRAY:          /* fall through */
        case INSTR_MOVE:                /* fall through */
        case INSTR_GET_GLOBAL:          /* fall through */
        case INSTR_SET_GLOBAL:          /* fall through */
        case INSTR_DEL:                 /* fall through */
        case INSTR_DEL_PROP:            /* fall through */
        case INSTR_NOT:                 /* fall through */
        case INSTR_TYPE:                /* fall through */
        case INSTR_JUMP_COND:           /* fall through */
        case INSTR_JUMP_NOT_COND:       /* fall through */
        case INSTR_BIND_SELF:           /* fall through */
        case INSTR_RETURN:              /* fall through */
        case INSTR_CATCH:
            return 2;

        case INSTR_LOAD_INT64:          /* fall through */
        case INSTR_LOAD_FLOAT:          /* fall through */
        case INSTR_GET:                 /* fall through */
        case INSTR_GET_ELEM:            /* fall through */
        case INSTR_GET_PROP:            /* fall through */
        case INSTR_GET_MOD:             /* fall through */
        case INSTR_GET_MOD_ELEM:        /* fall through */
        case INSTR_SET:                 /* fall through */
        case INSTR_SET_ELEM:            /* fall through */
        case INSTR_SET_PROP:            /* fall through */
        case INSTR_ADD:                 /* fall through */
        case INSTR_SUB:                 /* fall through */
        case INSTR_MUL:                 /* fall through */
        case INSTR_DIV:                 /* fall through */
        case INSTR_MOD:                 /* fall through */
        case INSTR_SHL:                 /* fall through */
        case INSTR_SHR:                 /* fall through */
        case INSTR_SSR:                 /* fall through */
        case INSTR_AND:                 /* fall through */
        case INSTR_OR:                  /* fall through */
        case INSTR_XOR:                 /* fall through */
        case INSTR_CMP_EQ:              /* fall through */
        case INSTR_CMP_NE:              /* fall through */
        case INSTR_CMP_LE:              /* fall through */
        case INSTR_CMP_LT:              /* fall through */
        case INSTR_HAS:                 /* fall through */
        case INSTR_HAS_PROP:            /* fall through */
        case INSTR_INSTANCEOF:          /* fall through */
        case INSTR_BIND:                /* fall through */
        case INSTR_CALL_GEN:            /* fall through */
        case INSTR_NEW:
            return 3;

        case INSTR_CALL:                /* fall through */
        case INSTR_TAIL_CALL:           /* fall through */
        case INSTR_GET_RANGE:           /* fall through */
            return 4;

        case INSTR_LOAD_FUN:            /* fall through */
        case INSTR_LOAD_GEN:
            return 5;
    }
}

static int _get_operand_size(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_INT32:
            /* fall through */
        case INSTR_LOAD_INT64:
            /* fall through */
        case INSTR_LOAD_FLOAT:
            /* fall through */
        case INSTR_LOAD_STR:
            /* fall through */
        case INSTR_LOAD_ARRAY:
            /* fall through */
        case INSTR_GET_GLOBAL:
            /* fall through */
        case INSTR_GET_MOD_ELEM:
            /* fall through */
        case INSTR_DEL_PROP:
            /* fall through */
        case INSTR_CATCH:
            if (op > 0)
                return 4;
            break;

        case INSTR_SET_GLOBAL:
            /* fall through */
        case INSTR_JUMP:
            /* fall through */
        case INSTR_JUMP_COND:
            /* fall through */
        case INSTR_JUMP_NOT_COND:
            if (op == 0)
                return 4;
            break;

        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN:
            /* fall through */
        case INSTR_GET_MOD:
            /* fall through */
        case INSTR_SET_ELEM:
            /* fall through */
        case INSTR_SET_PROP:
            if (op == 1)
                return 4;
            break;

        case INSTR_GET_ELEM:
            /* fall through */
        case INSTR_GET_PROP:
            /* fall through */
        case INSTR_HAS_PROP:
            if (op == 2)
                return 4;
            break;

        default:
            break;
    }
    return 1;
}

/* Returns number of bytes after the offset in the instruction or -1 if not offset */
static int _get_offset_operand_tail(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_JUMP:
            return 0;

        case INSTR_JUMP_COND:
            /* fall through */
        case INSTR_JUMP_NOT_COND:
            if (op == 0)
                return 1;
            break;

        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN:
            if (op == 1)
                return 3;
            break;

        case INSTR_CATCH:
            if (op == 1)
                return 0;
            break;

        default:
            break;
    }
    return -1;
}

static int _is_register(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_INT8:
            /* fall through */
        case INSTR_LOAD_INT32:
            /* fall through */
        case INSTR_LOAD_INT64:
            /* fall through */
        case INSTR_LOAD_FLOAT:
            /* fall through */
        case INSTR_LOAD_STR:
            /* fall through */
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN:
            /* fall through */
        case INSTR_LOAD_ARRAY8:
            /* fall through */
        case INSTR_LOAD_ARRAY:
            /* fall through */
        case INSTR_GET_GLOBAL:
            /* fall through */
        case INSTR_GET_MOD_ELEM:
            /* fall through */
        case INSTR_DEL_PROP:
            /* fall through */
        case INSTR_BIND_SELF:
            /* fall through */
        case INSTR_CATCH:
            return op > 0 ? 0 : 1;

        case INSTR_GET_ELEM:
            /* fall through */
        case INSTR_GET_PROP:
            /* fall through */
        case INSTR_HAS_PROP:
            return op > 1 ? 0 : 1;

        case INSTR_GET_MOD:
            /* fall through */
        case INSTR_SET_ELEM:
            /* fall through */
        case INSTR_SET_PROP:
            /* fall through */
        case INSTR_BIND:
            return op == 1 ? 0 : 1;

        case INSTR_SET_GLOBAL:
            /* fall through */
        case INSTR_JUMP_COND:
            /* fall through */
        case INSTR_JUMP_NOT_COND:
            /* fall through */
        case INSTR_RETURN:
            /* fall through */
        case INSTR_TAIL_CALL:
            return op;

        case INSTR_JUMP:
            return 0;

        default:
            break;
    }
    return 1;
}

static int _is_signed_op(enum _KOS_BYTECODE_INSTR instr, int op)
{
    assert( ! _is_register(instr, op));
    assert(_get_operand_size(instr, op) == 1);
    switch (instr) {

        case INSTR_LOAD_INT8:
            return 1;

        default:
            break;
    }
    return 0;
}

void _KOS_disassemble(const uint8_t                       *bytecode,
                      uint32_t                             size,
                      const struct _KOS_COMP_ADDR_TO_LINE *line_addrs,
                      uint32_t                             num_line_addrs)
{
    const struct _KOS_COMP_ADDR_TO_LINE *line_addrs_end = line_addrs + num_line_addrs;

    static const char *str_instr[] = {
        "BREAKPOINT",
        "LOAD.INT8",
        "LOAD.INT32",
        "LOAD.INT64",
        "LOAD.FLOAT",
        "LOAD.STR",
        "LOAD.TRUE",
        "LOAD.FALSE",
        "LOAD.VOID",
        "LOAD.FUN",
        "LOAD.GEN",
        "LOAD.ARRAY8",
        "LOAD.ARRAY",
        "LOAD.OBJ",
        "MOVE",
        "GET",
        "GET.ELEM",
        "GET.RANGE",
        "GET.PROP",
        "GET.GLOBAL",
        "GET.MOD",
        "GET.MOD.ELEM",
        "SET",
        "SET.ELEM",
        "SET.PROP",
        "SET.GLOBAL",
        "DEL",
        "DEL.PROP",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "MOD",
        "SHL",
        "SHR",
        "SSR",
        "NOT",
        "AND",
        "OR",
        "XOR",
        "TYPE",
        "CMP.EQ",
        "CMP.NE",
        "CMP.LE",
        "CMP.LT",
        "HAS",
        "HAS.PROP",
        "INSTANCEOF",
        "JUMP",
        "JUMP.COND",
        "JUMP.NOT.COND",
        "BIND",
        "BIND.SELF",
        "CALL",
        "CALL.GEN",
        "NEW",
        "RETURN",
        "TAIL.CALL",
        "YIELD",
        "THROW",
        "CATCH",
        "CATCH.CANCEL"
    };
    uint32_t offs = 0;

    while (size) {
        int           i;
        int           iop;
        int           num_operands;
        int           instr_size = 1;
        char          bin[64];
        char          dis[128];
        size_t        dis_size;
        const int     mnem_align = 44;
        const char*   str_opcode;
        const uint8_t opcode = *bytecode;
        assert(opcode <= sizeof(str_instr)/sizeof(str_instr[0]));

        assert(line_addrs == line_addrs_end ||
               offs <= line_addrs->offs);

        if (line_addrs < line_addrs_end &&
            offs == line_addrs->offs) {

            printf("@%u:\n", line_addrs->line);
            ++line_addrs;
        }

        str_opcode   = str_instr[opcode];
        num_operands = _get_num_operands((enum _KOS_BYTECODE_INSTR)opcode);

        dis[sizeof(dis)-1] = 0;
        dis_size           = strlen(str_opcode);
        memcpy(dis, str_opcode, dis_size);
        while (dis_size < 16)
            dis[dis_size++] = ' ';
        dis[dis_size] = 0;

        for (iop = 0; iop < num_operands; iop++) {
            const int opsize = _get_operand_size((enum _KOS_BYTECODE_INSTR)opcode, iop);
            int       value  = 0;
            int       tail   = 0;

            assert(opsize == 1 || opsize == 4);

            for (i = 0; i < opsize; i++)
                value |= (int32_t)((uint32_t)bytecode[instr_size+i] << (8*i));

            tail = _get_offset_operand_tail((enum _KOS_BYTECODE_INSTR)opcode, iop);
            if (tail >= 0)
                snprintf(&dis[dis_size], sizeof(dis)-dis_size, "%08X", value + offs + instr_size + opsize + tail);
            else if (_is_register((enum _KOS_BYTECODE_INSTR)opcode, iop))
                snprintf(&dis[dis_size], sizeof(dis)-dis_size, "r%d", value);
            else {
                if (opsize == 1 && _is_signed_op((enum _KOS_BYTECODE_INSTR)opcode, iop))
                    value = (int32_t)(int8_t)value;
                snprintf(&dis[dis_size], sizeof(dis)-dis_size, "%d", value);
            }
            dis_size = strlen(dis);

            if (iop+1 < num_operands) {
                dis[dis_size++] = ',';
                dis[dis_size++] = ' ';
                dis[dis_size]   = 0;
            }

            assert(dis_size < sizeof(dis));

            instr_size += opsize;
        }

        snprintf(bin, sizeof(bin), "%08X: ", (unsigned)offs);
        bin[sizeof(bin)-1] = 0;
        for (i = 0; i < instr_size; i++) {
            size_t pos = strlen(bin);
            snprintf(&bin[pos], sizeof(bin)-pos, "%02X ", (int)bytecode[i]);
        }
        i = (int)strlen(bin);
        while (i < mnem_align)
            bin[i++] = ' ';
        bin[i] = 0;

        printf("%s%s\n", bin, dis);

        bytecode += instr_size;
        offs     += instr_size;
        assert(size >= (size_t)instr_size);
        if (size >= (size_t)instr_size)
            size -= instr_size;
    }
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
                size = _get_operand_size(instr, i-1);

            if (size == 1) {
                if ( ! _is_register(instr, i-1)) {
                    if (_is_signed_op(instr, i-1)) {
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

    assert(opcode == INSTR_LOAD_FUN  ||
           opcode == INSTR_LOAD_GEN  ||
           opcode == INSTR_CATCH     ||
           opcode == INSTR_JUMP      ||
           opcode == INSTR_JUMP_COND ||
           opcode == INSTR_JUMP_NOT_COND);

    switch (opcode) {
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN: jump_instr_size = 9; break;
        case INSTR_JUMP:     jump_instr_size = 5; break;
        default:             jump_instr_size = 6; break;
    }

    jump_offs = target_offs - (jump_instr_offs + jump_instr_size);

    buf += (opcode == INSTR_LOAD_FUN || opcode == INSTR_LOAD_GEN || opcode == INSTR_CATCH) ? 2 : 1;

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

    if (var->reg) {
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

static int _import(struct _KOS_COMP_UNIT      *program,
                   const struct _KOS_AST_NODE *node)
{
    int error = KOS_SUCCESS;

    node = node->children;
    assert(node);

    if (node->next) {

        int module_idx;

        assert(program->import_module);
        assert(program->get_global_idx);

        TRY(program->import_module(program->frame,
                                   node->token.begin,
                                   node->token.length,
                                   KOS_COMP_MANDATORY,
                                   &module_idx));

        node = node->next;

        if (node->token.op == OT_MUL) {
            /* TODO import all globals */
            assert(0);
            error = KOS_ERROR_INTERNAL;
        }
        else {

            int              global_idx;
            struct _KOS_VAR *var;
            struct _KOS_REG *reg = 0;

            assert(node->token.type == TT_IDENTIFIER || node->token.type == TT_KEYWORD);

            TRY(program->get_global_idx(program->frame,
                                        module_idx,
                                        node->token.begin,
                                        node->token.length,
                                        &global_idx));

            var = _KOS_find_var(program->scope_stack->vars, &node->token);

            assert(var);
            assert(var->type == VAR_GLOBAL);

            TRY(_gen_reg(program, &reg));

            TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, reg->reg, module_idx, global_idx));

            TRY(_gen_instr2(program, INSTR_SET_GLOBAL, var->array_idx, reg->reg));

            _free_reg(program, reg);
        }
    }

_error:
    return error;
}

static int _append_frame(struct _KOS_COMP_UNIT *program,
                         int                    fun_start_offs,
                         size_t                 addr2line_start_offs)
{
    int          error;
    const size_t fun_end_offs = (size_t)program->cur_offs;
    const size_t fun_size     = fun_end_offs - fun_start_offs;
    const size_t fun_new_offs = program->code_buf.size;
    const size_t a2l_size     = program->addr2line_gen_buf.size - addr2line_start_offs;
    size_t       a2l_new_offs = program->addr2line_buf.size;
    int          str_idx      = 0;

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

    TRY(_gen_str(program, program->cur_frame->fun_token, &str_idx));

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

static int _finish_global_scope(struct _KOS_COMP_UNIT *program)
{
    int              error;
    struct _KOS_REG *reg = 0;

    TRY(_gen_reg(program, &reg));
    TRY(_gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
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

            struct _KOS_REG *reg = 0;

            TRY(_add_addr2line(program, &child->token, _KOS_FALSE));

            TRY(_visit_node(program, child, &reg));

            if (reg)
                _free_reg(program, reg);
        }

        if (global)
            TRY(_finish_global_scope(program));

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
    else {
        int closure_size;

        scope = program->scope_stack;
        while (scope->next && ! scope->is_function)
            scope = scope->next;

        closure_size = scope->num_indep_vars;
        if (scope->num_indep_args)
            ++closure_size;

        TRY(_gen_instr2(program, INSTR_RETURN, closure_size, reg));
    }

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

    if (try_scope)
        reg = try_scope->catch_ref.catch_reg;

    if (node->children) {

        if (node->children->type != NT_VOID_LITERAL && _is_generator(program)) {
            program->error_token = &node->token;
            program->error_str   = str_err_return_in_generator;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }

        /* TODO - tail recursion (INSTR_TAIL_CALL) if there are no pending catches */

        TRY(_visit_node(program, node->children, &reg));
        assert(reg);
    }
    else {
        TRY(_gen_reg(program, &reg));
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, reg->reg));
    }

    error = _gen_return(program, reg->reg);

    if ( ! try_scope || reg != try_scope->catch_ref.catch_reg)
        _free_reg(program, reg);

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
    struct _KOS_REG            *args_reg = 0;
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

    TRY(_gen_reg(program, &args_reg));

    TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, args_reg->reg, 1));

    TRY(_gen_instr3(program, INSTR_SET_ELEM, args_reg->reg, 0, src_reg->reg));

    if ( ! src_reg->tmp) {
        src_reg = 0;
        TRY(_gen_reg(program, &src_reg));
    }

    TRY(_gen_instr1(program, INSTR_LOAD_VOID, src_reg->reg));

    TRY(_gen_dest_reg(program, reg, src_reg));

    TRY(_gen_instr4(program, INSTR_CALL, (*reg)->reg, func_reg->reg, src_reg->reg, args_reg->reg));

    if (*reg != src_reg)
        _free_reg(program, src_reg);
    _free_reg(program, args_reg);
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

    TRY(_gen_instr2(program, INSTR_LOAD_STR, reg->reg, str_idx));

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
    struct _KOS_BREAK_OFFS *break_offs     = program->cur_frame->break_offs;
    const int               break_tgt_offs = program->cur_offs;

    while (break_offs) {
        struct _KOS_BREAK_OFFS *next = break_offs->next;

        assert(break_offs->type == NT_CONTINUE ||
               break_offs->type == NT_BREAK);

        _update_jump_offs(program, break_offs->offs,
                break_offs->type == NT_CONTINUE
                ? continue_tgt_offs : break_tgt_offs);

        break_offs = next;
    }

    program->cur_frame->break_offs = old_break_offs;
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

static int _do(struct _KOS_COMP_UNIT      *program,
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

    if (_KOS_node_is_falsy(program, node))
        _finish_break_continue(program, program->cur_offs, old_break_offs);

    else {

        test_instr_offs = program->cur_offs;

        TRY(_visit_node(program, node, &reg));
        assert(reg);

        assert(!node->next);

        jump_instr_offs = program->cur_offs;
        TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));
        _update_jump_offs(program, jump_instr_offs, loop_start_offs);

        _finish_break_continue(program, test_instr_offs, old_break_offs);

        _free_reg(program, reg);
    }

    program->cur_frame->last_try_scope = prev_try_scope;

_error:
    return error;
}

static int _while(struct _KOS_COMP_UNIT      *program,
                  const struct _KOS_AST_NODE *node)
{
    int                     error           = KOS_SUCCESS;
    const int               loop_start_offs = program->cur_offs;
    int                     jump_instr_offs = 0;
    int                     offs;
    struct _KOS_REG        *reg             = 0;
    struct _KOS_BREAK_OFFS *old_break_offs  = program->cur_frame->break_offs;
    struct _KOS_SCOPE      *prev_try_scope  = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    node = node->children;
    assert(node);

    if ( ! _KOS_node_is_falsy(program, node)) {

        const int is_truthy = _KOS_node_is_truthy(program, node);

        if ( ! is_truthy) {

            TRY(_visit_node(program, node, &reg));
            assert(reg);

            jump_instr_offs = program->cur_offs;
            TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, reg->reg));

            _free_reg(program, reg);
            reg = 0;
        }

        node = node->next;
        assert(node);
        TRY(_visit_node(program, node, &reg));
        assert(!reg);

        assert(!node->next);

        /* TODO skip jump if last node was terminating - return, throw, break, continue */

        offs = program->cur_offs;
        TRY(_gen_instr1(program, INSTR_JUMP, 0));
        _update_jump_offs(program, offs, loop_start_offs);

        if ( ! is_truthy)
            _update_jump_offs(program, jump_instr_offs, program->cur_offs);

        _finish_break_continue(program, loop_start_offs, old_break_offs);
    }
    else
        program->cur_frame->break_offs = old_break_offs;

    program->cur_frame->last_try_scope = prev_try_scope;

_error:
    return error;
}

static int _for(struct _KOS_COMP_UNIT      *program,
                const struct _KOS_AST_NODE *node)
{
    int                         error;
    int                         loop_start_offs;
    int                         cond_jump_instr_offs = -1;
    int                         final_jump_instr_offs;
    int                         step_instr_offs;
    const struct _KOS_AST_NODE *step_node;
    struct _KOS_REG            *reg                  = 0;
    struct _KOS_BREAK_OFFS     *old_break_offs       = program->cur_frame->break_offs;
    struct _KOS_SCOPE          *prev_try_scope       = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    loop_start_offs = program->cur_offs;

    node = node->children;
    assert(node);

    TRY(_add_addr2line(program, &node->token, _KOS_FALSE));

    /* TODO check truthy/falsy */

    TRY(_visit_node(program, node, &reg));

    if (reg) {

        cond_jump_instr_offs = program->cur_offs;
        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, reg->reg));

        _free_reg(program, reg);
        reg = 0;
    }

    step_node = node->next;
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

    final_jump_instr_offs = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    _update_jump_offs(program, final_jump_instr_offs, loop_start_offs);
    if (cond_jump_instr_offs > -1)
        _update_jump_offs(program, cond_jump_instr_offs, program->cur_offs);

    _finish_break_continue(program, step_instr_offs, old_break_offs);

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
    struct _KOS_REG  *args_reg       = 0;
    struct _KOS_REG  *obj_reg        = *reg;
    struct _KOS_TOKEN token;
    static const char str_iterator[] = "iterator";

    if ( ! (*reg)->tmp) {
        _free_reg(program, *reg);
        *reg = 0;

        TRY(_gen_reg(program, reg));
    }

    TRY(_gen_reg(program, &func_reg));
    TRY(_gen_reg(program, &args_reg));

    memset(&token, 0, sizeof(token));
    token.begin  = str_iterator;
    token.length = sizeof(str_iterator) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    TRY(_gen_instr3(program, INSTR_GET_PROP, func_reg->reg, obj_reg->reg, str_idx));

    TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, args_reg->reg, 0));

    TRY(_gen_instr4(program, INSTR_CALL, (*reg)->reg, func_reg->reg, obj_reg->reg, args_reg->reg));

    _free_reg(program, args_reg);
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
    const struct _KOS_AST_NODE *var_node;
    const struct _KOS_AST_NODE *expr_node;
    struct _KOS_REG            *reg            = 0;
    struct _KOS_REG            *iter_reg       = 0;
    struct _KOS_REG            *item_reg       = 0;
    struct _KOS_BREAK_OFFS     *old_break_offs = program->cur_frame->break_offs;
    struct _KOS_SCOPE          *prev_try_scope = _push_try_scope(program);

    program->cur_frame->break_offs = 0;

    _push_scope(program, node);

    node = node->children;
    assert(node);
    assert(node->type == NT_IN);

    var_node = node->children;
    assert(var_node);
    assert(var_node->type == NT_VAR);

    expr_node = var_node->next;
    assert(expr_node);
    assert(!expr_node->next);

    var_node = var_node->children;
    assert(var_node);

    TRY(_visit_node(program, expr_node, &iter_reg));
    assert(iter_reg);

    _KOS_activate_new_vars(program, node->children);

    TRY(_invoke_get_iterator(program, &iter_reg));

    loop_start_offs = program->cur_offs;

    TRY(_add_addr2line(program, &node->token, _KOS_FALSE));

    if ( ! var_node->next) {

        TRY(_lookup_local_var(program, &var_node->token, &item_reg));
        assert(item_reg);
    }
    else
        TRY(_gen_reg(program, &item_reg));

    TRY(_gen_reg(program, &reg));

    TRY(_gen_instr3(program, INSTR_CALL_GEN, item_reg->reg, iter_reg->reg, reg->reg));

    cond_jump_instr_offs = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, reg->reg));

    if (var_node->next) {

        struct _KOS_REG *value_iter_reg = item_reg;

        TRY(_invoke_get_iterator(program, &value_iter_reg));

        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, reg->reg, 0));

        for ( ; var_node; var_node = var_node->next) {

            item_reg = 0;
            TRY(_lookup_local_var(program, &var_node->token, &item_reg));
            assert(item_reg);

            TRY(_gen_instr4(program, INSTR_CALL, item_reg->reg, value_iter_reg->reg, reg->reg, reg->reg));
        }

        _free_reg(program, value_iter_reg);
    }

    _free_reg(program, reg);
    reg = 0;

    node = node->next;
    assert(node);
    assert(!node->next);

    TRY(_visit_node(program, node, &reg));
    assert(!reg);

    final_jump_instr_offs = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    _update_jump_offs(program, final_jump_instr_offs, loop_start_offs);
    _update_jump_offs(program, cond_jump_instr_offs, program->cur_offs);
    _finish_break_continue(program, loop_start_offs, old_break_offs);

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
        error = _gen_instr(program, 0, INSTR_CATCH_CANCEL);

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

static int _break_continue(struct _KOS_COMP_UNIT      *program,
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

    if (num_cases) {
        cases = (struct _KOS_SWITCH_CASE *)_KOS_mempool_alloc(
                &program->allocator, sizeof(struct _KOS_SWITCH_CASE) * num_cases);

        if ( ! cases)
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    }

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

                case NT_NUMERIC_LITERAL:
                    /* fall-through */
                case NT_STRING_LITERAL:
                    /* fall-through */
                case NT_BOOL_LITERAL:
                    /* fall-through */
                case NT_VOID_LITERAL:
                    break;

                /* TODO identifier -> const */

                /* TODO allow functions (immutable) */

                default:
                    program->error_token = &node->children->token;
                    program->error_str   = str_err_invalid_case;
                    RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
            }

            /* TODO ensure unique */

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

        cases[i_case].final_jump_offs = -1;

        if (child_node->type != NT_FALLTHROUGH) {

            TRY(_visit_node(program, child_node, &value_reg));
            assert( ! value_reg);

            if ( ! child_node->next) {
                cases[i_case].final_jump_offs = program->cur_offs;

                TRY(_gen_instr1(program, INSTR_JUMP, 0));
            }
            else {
                assert(child_node->next->type == NT_FALLTHROUGH);
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
        for (i = 0; i < sizeof(scope->catch_ref.catch_offs)/sizeof(int); i++) {
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
    struct _KOS_REG            *except_reg   = 0;
    struct _KOS_VAR            *except_var   = 0;
    struct _KOS_RETURN_OFFS    *return_offs  = program->cur_frame->return_offs;
    const struct _KOS_AST_NODE *try_node     = node->children;
    const struct _KOS_AST_NODE *catch_node;
    const struct _KOS_AST_NODE *finally_node;
    struct _KOS_SCOPE          *scope;

    scope = _push_scope(program, node);

    assert(try_node);
    catch_node = try_node->next;
    assert(catch_node);
    finally_node = catch_node->next;
    assert(finally_node);
    assert(!finally_node->next);

    if (catch_node->type == NT_CATCH) {

        struct _KOS_AST_NODE *variable;

        assert(finally_node->type == NT_EMPTY);

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
        assert(finally_node->type == NT_SCOPE);

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

    /* Finally section */

    _update_jump_offs(program, jump_end_offs, program->cur_offs);

    if (finally_node->type == NT_SCOPE) {

        int skip_throw_offs;

        {
            struct _KOS_RETURN_OFFS *tmp    = program->cur_frame->return_offs;
            program->cur_frame->return_offs = return_offs;
            return_offs                     = tmp;
            scope->catch_ref.finally_active = 0;
        }

        TRY(_scope(program, finally_node));

        skip_throw_offs = program->cur_offs;

        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, except_reg->reg));

        TRY(_gen_instr1(program, INSTR_THROW, except_reg->reg));

        /* Finally section for break and continue */

        if (program->cur_frame->break_offs) {

            struct _KOS_BREAK_OFFS *break_offs         = program->cur_frame->break_offs;
            int                     break_jump_offs    = 0;
            int                     continue_jump_offs = 0;

            while (break_offs) {
                assert(break_offs->type == NT_CONTINUE ||
                       break_offs->type == NT_BREAK);
                if (break_offs->type == NT_CONTINUE) {
                    continue_jump_offs = 1;
                    _update_jump_offs(program, break_offs->offs, program->cur_offs);
                }
                else
                    break_jump_offs = 1;
                break_offs = break_offs->next;
            }

            if (continue_jump_offs) {

                TRY(_restore_parent_scope_catch(program, 3));

                TRY(_scope(program, finally_node));

                continue_jump_offs = program->cur_offs;

                TRY(_gen_instr1(program, INSTR_JUMP, 0));
            }

            break_offs = program->cur_frame->break_offs;

            while (break_offs) {
                struct _KOS_BREAK_OFFS *cur = break_offs;
                break_offs                  = break_offs->next;

                if (cur->type == NT_BREAK)
                    _update_jump_offs(program, cur->offs, program->cur_offs);
            }

            program->cur_frame->break_offs = 0;

            if (break_jump_offs) {

                TRY(_restore_parent_scope_catch(program, 4));

                TRY(_scope(program, finally_node));

                break_jump_offs = program->cur_offs;

                TRY(_gen_instr1(program, INSTR_JUMP, 0));

                TRY(_push_break_offs(program, NT_BREAK));

                program->cur_frame->break_offs->offs = break_jump_offs;
            }

            if (continue_jump_offs) {

                TRY(_push_break_offs(program, NT_CONTINUE));

                program->cur_frame->break_offs->offs = continue_jump_offs;
            }
        }

        /* Finally section for return statement */

        if (return_offs) {

            for ( ; return_offs; return_offs = return_offs->next)
                _update_jump_offs(program, return_offs->offs, program->cur_offs);

            TRY(_restore_parent_scope_catch(program, 4));

            TRY(_scope(program, finally_node));

            TRY(_gen_return(program, except_reg->reg));
        }

        _update_jump_offs(program, skip_throw_offs, program->cur_offs);
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

        int         global_idx;
        const char *begin;
        unsigned    length;

        _get_token_str(&node->token, &begin, &length);

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

struct KOS_FIND_VAR_BY_REG {
    struct _KOS_REG *reg; /* input */
    struct _KOS_VAR *var; /* output */
};

static int _find_var_by_reg(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_VAR            *var = (struct _KOS_VAR *)node;
    struct KOS_FIND_VAR_BY_REG *find = (struct KOS_FIND_VAR_BY_REG *)cookie;

    if (var->reg == find->reg) {
        find->var = var;

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

            for (scope = program->scope_stack; scope && scope->next && ! scope->is_function; scope = scope->next) {

                int                        error;
                struct KOS_FIND_VAR_BY_REG find = { 0, 0 };

                find.reg = reg;
                error    = _KOS_red_black_walk(scope->vars, _find_var_by_reg, &find);

                if (error == KOS_SUCCESS_RETURN)
                    return 1;
            }

            /* Arguments list */
            if (scope && scope->is_function && scope->ellipsis && scope->ellipsis->reg == reg)
                return 1;
        }

        if (_is_var_used(program, node->children, reg))
            return 1;
    }

    return 0;
}

static int _gen_array(struct _KOS_COMP_UNIT      *program,
                      const struct _KOS_AST_NODE *node,
                      struct _KOS_REG           **reg)
{
    int       error;
    int       i;
    const int num_elems = _count_siblings(node);

    if (_is_var_used(program, node, *reg))
        *reg = 0;

    TRY(_gen_reg(program, reg));
    if (num_elems < 256)
        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, (*reg)->reg, num_elems));
    else
        TRY(_gen_instr2(program, INSTR_LOAD_ARRAY, (*reg)->reg, num_elems));

    for (i = 0; node; node = node->next, ++i) {

        struct _KOS_REG *arg = 0;
        TRY(_visit_node(program, node, &arg));
        assert(arg);

        TRY(_gen_instr3(program, INSTR_SET_ELEM, (*reg)->reg, i, arg->reg));

        _free_reg(program, arg);
    }

_error:
    return error;
}

static int _invocation(struct _KOS_COMP_UNIT      *program,
                       const struct _KOS_AST_NODE *node,
                       struct _KOS_REG           **reg)
{
    int              error;
    struct _KOS_REG *obj  = 0;
    struct _KOS_REG *fun  = 0;
    struct _KOS_REG *args = _is_var_used(program, node, *reg) ? 0 : *reg;

    node = node->children;
    assert(node);

    if (node->type == NT_REFINEMENT)
        TRY(_refinement(program, node, &fun, &obj));

    else {

        TRY(_visit_node(program, node, &fun));
        assert(fun);
    }

    node = node->next;

    TRY(_gen_array(program, node, &args));

    if ( ! *reg)
        *reg = args;

    if (!obj) {
        TRY(_gen_reg(program, &obj));
        TRY(_gen_instr1(program, INSTR_LOAD_VOID, obj->reg));
    }

    TRY(_gen_instr4(program, INSTR_CALL, (*reg)->reg, fun->reg, obj->reg, args->reg));

    _free_reg(program, fun);
    _free_reg(program, obj);
    if (args != *reg)
        _free_reg(program, args);

_error:
    return error;
}

static int _new(struct _KOS_COMP_UNIT      *program,
                const struct _KOS_AST_NODE *node,
                struct _KOS_REG           **reg)
{
    int              error;
    struct _KOS_REG *fun  = 0;
    struct _KOS_REG *args = _is_var_used(program, node, *reg) ? 0 : *reg;

    assert(node->children);

    node = node->children;
    assert(node->type == NT_INVOCATION);
    assert(!node->next);

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &fun));
    assert(fun);

    node = node->next;

    TRY(_gen_array(program, node, &args));

    if ( ! *reg)
        *reg = args;

    TRY(_gen_instr3(program, INSTR_NEW, (*reg)->reg, fun->reg, args->reg));

    _free_reg(program, fun);
    if (args != *reg)
        _free_reg(program, args);

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
    const enum _KOS_OPERATOR_TYPE op  = node->token.op;
    struct _KOS_REG              *src = *reg;

    assert(op == OT_LOGAND || op == OT_LOGOR);

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    if (src != *reg)
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

    node = node->next;
    assert(node);
    assert(!node->next);

    offs = program->cur_offs;

    if (op == OT_LOGAND)
        TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, src->reg));
    else
        TRY(_gen_instr2(program, INSTR_JUMP_COND, 0, src->reg));

    if (src != *reg)
        _free_reg(program, src);
    src = (*reg)->tmp ? *reg : 0;

    TRY(_visit_node(program, node, &src));
    assert(src);

    if (src != *reg) {
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));
        _free_reg(program, src);
    }

    _update_jump_offs(program, offs, program->cur_offs);

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
    struct _KOS_REG *src = *reg;

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &src));
    assert(src);

    offs1 = program->cur_offs;
    TRY(_gen_instr2(program, INSTR_JUMP_NOT_COND, 0, src->reg));

    _free_reg(program, src);
    src = 0;

    node = node->next;
    assert(node);

    TRY(_visit_node(program, node, &src));
    assert(src);

    TRY(_gen_dest_reg(program, reg, src));

    if (src != *reg)
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));

    offs2 = program->cur_offs;
    TRY(_gen_instr1(program, INSTR_JUMP, 0));

    _update_jump_offs(program, offs1, program->cur_offs);

    node = node->next;
    assert(node);
    assert(!node->next);

    TRY(_visit_node(program, node, &src));

    if (src != *reg) {
        TRY(_gen_instr2(program, INSTR_MOVE, (*reg)->reg, src->reg));
        _free_reg(program, src);
    }

    _update_jump_offs(program, offs2, program->cur_offs);

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

                case KW_NEW:
                    return _new(program, node, reg);

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

        case OT_MUL: opcode = INSTR_MUL;    operands = 2; break;
        case OT_DIV: opcode = INSTR_DIV;    operands = 2; break;
        case OT_MOD: opcode = INSTR_MOD;    operands = 2; break;
        case OT_NOT: opcode = INSTR_NOT;    operands = 1; break;
        case OT_AND: opcode = INSTR_AND;    operands = 2; break;
        case OT_OR:  opcode = INSTR_OR;     operands = 2; break;
        case OT_XOR: opcode = INSTR_XOR;    operands = 2; break;
        case OT_SHL: opcode = INSTR_SHL;    operands = 2; break;
        case OT_SHR: opcode = INSTR_SHR;    operands = 2; break;
        case OT_SSR: opcode = INSTR_SSR;    operands = 2; break;
        case OT_EQ:  opcode = INSTR_CMP_EQ; operands = 2; break;
        case OT_NE:  opcode = INSTR_CMP_NE; operands = 2; break;
        case OT_GE:  opcode = INSTR_CMP_LE; operands = 2; swap = 1; break;
        case OT_GT:  opcode = INSTR_CMP_LT; operands = 2; swap = 1; break;
        case OT_LE:  opcode = INSTR_CMP_LE; operands = 2; break;
        case OT_LT:  opcode = INSTR_CMP_LT; operands = 2; break;
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
        case OT_SSR:
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
        case OT_SETSSR:
            /* fall through */
        default:        assert(op == OT_SETSSR);
                        return INSTR_SSR;
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
    struct _KOS_REG *prop    = 0;
    struct _KOS_REG *tmp_reg = 0;

    assert(node->type == NT_REFINEMENT);

    node = node->children;
    assert(node);

    TRY(_visit_node(program, node, &obj));
    assert(obj);

    node = node->next;
    assert(node);
    assert(!node->next);

    if (node->type == NT_STRING_LITERAL)
        TRY(_gen_str(program, &node->token, &str_idx));

    else if (_maybe_int(node, &idx)) {

        assert(node->type == NT_NUMERIC_LITERAL);

        if (idx > INT_MAX || idx < INT_MIN) {
            program->error_token = &node->token;
            program->error_str   = str_err_invalid_index;
            RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
        }
    }
    else {

        TRY(_visit_node(program, node, &prop));
        assert(prop);
    }

    if (assg_op != OT_SET) {

        TRY(_gen_reg(program, &tmp_reg));

        if (node->type == NT_STRING_LITERAL)
            TRY(_gen_instr3(program, INSTR_GET_PROP, tmp_reg->reg, obj->reg, str_idx));

        else if (node->type == NT_NUMERIC_LITERAL)
            TRY(_gen_instr3(program, INSTR_GET_ELEM, tmp_reg->reg, obj->reg, (int)idx));

        else
            TRY(_gen_instr3(program, INSTR_GET, tmp_reg->reg, obj->reg, prop->reg));

        TRY(_gen_instr3(program, _assign_instr(assg_op), tmp_reg->reg, tmp_reg->reg, src->reg));

        src = tmp_reg;
    }

    if (node->type == NT_STRING_LITERAL)
        TRY(_gen_instr3(program, INSTR_SET_PROP, obj->reg, str_idx, src->reg));

    else if (node->type == NT_NUMERIC_LITERAL)

        TRY(_gen_instr3(program, INSTR_SET_ELEM, obj->reg, (int)idx, src->reg));
    else
        TRY(_gen_instr3(program, INSTR_SET, obj->reg, prop->reg, src->reg));

    if (prop)
        _free_reg(program, prop);

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
    struct _KOS_REG            *args_reg     = 0;
    struct _KOS_REG            *obj_reg      = 0;
    struct _KOS_REG            *func_reg     = 0;
    static const char           str_insert[] = "insert";
    struct _KOS_TOKEN           token;

    TRY(_gen_reg(program, &args_reg));
    TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, args_reg->reg, 3));
    TRY(_gen_instr3(program, INSTR_SET_ELEM, args_reg->reg, 2, src->reg));
    _free_reg(program, src);

    node = node->children;
    assert(node);

    obj_node = node;
    node     = node->next;
    assert(node);

    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    TRY(_gen_instr3(program, INSTR_SET_ELEM, args_reg->reg, 0, obj_reg->reg));

    _free_reg(program, obj_reg);
    obj_reg = 0;

    node = node->next;
    assert(node);
    assert( ! node->next);

    TRY(_visit_node(program, node, &obj_reg));
    assert(obj_reg);

    TRY(_gen_instr3(program, INSTR_SET_ELEM, args_reg->reg, 1, obj_reg->reg));

    _free_reg(program, obj_reg);
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

    TRY(_gen_instr4(program, INSTR_CALL, func_reg->reg, func_reg->reg, obj_reg->reg, args_reg->reg));

    _free_reg(program, args_reg);
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
    struct _KOS_REG            *args_reg  = 0;
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

        if (node_type == NT_MULTI_ASSIGNMENT && ! args_reg) {

            TRY(_gen_reg(program, &args_reg));

            TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, args_reg->reg, 0));
        }

        if (reg) {

            if (assg_node->token.op == OT_SET) {

                if (node_type == NT_MULTI_ASSIGNMENT) {

                    assert(reg != rhs);

                    TRY(_gen_instr4(program, INSTR_CALL, reg->reg, rhs->reg, args_reg->reg, args_reg->reg));
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

                TRY(_gen_instr4(program, INSTR_CALL, reg->reg, rhs->reg, args_reg->reg, args_reg->reg));
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

    if (args_reg)
        _free_reg(program, args_reg);

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
    static const char str_string[] = "string";

    assert(program->get_global_idx);
    error = program->get_global_idx(program->frame, 0, str_string, sizeof(str_string)-1, &string_idx);
    if (error) {
        program->error_token = &node->token;
        program->error_str   = str_err_no_such_module_variable;
        RAISE_ERROR(KOS_ERROR_COMPILE_FAILED);
    }

    TRY(_gen_array(program, node->children, &args));

    if ( ! *reg)
        *reg = args;

    TRY(_gen_reg(program, &func_reg));

    TRY(_gen_instr3(program, INSTR_GET_MOD_ELEM, func_reg->reg, 0, string_idx));

    TRY(_gen_instr3(program, INSTR_NEW, (*reg)->reg, func_reg->reg, args->reg));

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
    else if (numeric.type == KOS_INTEGER_VALUE) {

        if ((uint64_t)((numeric.u.i >> 7) + 1) <= 1U)
            TRY(_gen_instr2(program, INSTR_LOAD_INT8, (*reg)->reg, (int32_t)numeric.u.i));
        else if ((uint64_t)((numeric.u.i >> 31) + 1) <= 1U)
            TRY(_gen_instr2(program, INSTR_LOAD_INT32, (*reg)->reg, (int32_t)numeric.u.i));
        else
            TRY(_gen_instr3(program, INSTR_LOAD_INT64, (*reg)->reg,
                            (int32_t)numeric.u.i, (int32_t)(numeric.u.i >> 32)));
    }
    else
        TRY(_gen_instr3(program, INSTR_LOAD_FLOAT, (*reg)->reg,
                        (int32_t)((uint64_t)numeric.u.i & 0xFFFFFFFFU),
                        (int32_t)(((uint64_t)numeric.u.i >> 32) & 0xFFFFFFFFU)));

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
            error = _gen_instr2(program, INSTR_LOAD_STR, (*reg)->reg, str_idx);
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

int _gen_arg_list(struct _KOS_COMP_UNIT *program,
                  struct _KOS_VAR       *ellipsis_var,
                  struct _KOS_REG       *args_reg,
                  int                    num_args)
{
    int               error    = KOS_SUCCESS;
    int               str_idx;
    struct _KOS_REG  *tmp_reg  = 0;
    struct _KOS_REG  *ellipsis_reg;
    struct _KOS_TOKEN token;
    static const char str_slice[] = "slice";

    TRY(_gen_reg(program, &ellipsis_var->reg));
    ellipsis_reg = ellipsis_var->reg;
    ellipsis_reg->tmp = 0;

    TRY(_gen_reg(program, &tmp_reg));

    memset(&token, 0, sizeof(token));
    token.begin  = str_slice;
    token.length = sizeof(str_slice) - 1;
    token.type   = TT_IDENTIFIER;

    TRY(_gen_str(program, &token, &str_idx));

    TRY(_gen_instr2(program, INSTR_LOAD_ARRAY8, tmp_reg->reg, 2));

    if (num_args <= 0x7F)
        TRY(_gen_instr2(program, INSTR_LOAD_INT8, ellipsis_reg->reg, num_args));
    else
        TRY(_gen_instr2(program, INSTR_LOAD_INT32, ellipsis_reg->reg, num_args));

    TRY(_gen_instr3(program, INSTR_SET_ELEM, tmp_reg->reg, 0, ellipsis_reg->reg));

    TRY(_gen_instr3(program, INSTR_GET_PROP, ellipsis_reg->reg, args_reg->reg, str_idx));

    TRY(_gen_instr4(program, INSTR_CALL, ellipsis_reg->reg, ellipsis_reg->reg, args_reg->reg, tmp_reg->reg));

    _free_reg(program, tmp_reg);

_error:
    return error;
}

static int _gen_closure_vars(struct _KOS_RED_BLACK_NODE *node,
                             void                       *cookie)
{
    int error = KOS_SUCCESS;

    struct _KOS_SCOPE_REF *ref     = (struct _KOS_SCOPE_REF *)node;
    struct _KOS_COMP_UNIT *program = (struct _KOS_COMP_UNIT *)cookie;

    if ((ref->exported_types & VAR_INDEPENDENT_ARGUMENT) == VAR_INDEPENDENT_ARGUMENT) {
        error = _gen_reg(program, &ref->args_reg);
        if (!error) {
            assert(ref->args_reg->reg >= 2 + program->scope_stack->num_indep_vars);
            ref->args_reg->tmp = 0;
        }
    }

    if (!error && (ref->exported_types & VAR_INDEPENDENT_LOCAL) == VAR_INDEPENDENT_LOCAL) {
        error = _gen_reg(program, &ref->vars_reg);
        if (!error) {
            assert(ref->vars_reg->reg >= 2 + program->scope_stack->num_indep_vars);
            ref->vars_reg->tmp = 0;
        }
    }

    return error;
}

struct _BIND_ARGS {
    struct _KOS_COMP_UNIT *program;
    struct _KOS_REG       *func_reg;
    struct _KOS_FRAME     *parent_frame;
};

static int _gen_binds(struct _KOS_RED_BLACK_NODE *node,
                      void                       *cookie)
{
    int error = KOS_SUCCESS;

    struct _KOS_SCOPE_REF *ref     = (struct _KOS_SCOPE_REF *)node;
    struct _BIND_ARGS     *args    = (struct _BIND_ARGS *)    cookie;
    struct _KOS_COMP_UNIT *program = args->program;

    /* Register of the first referenced independend variable in the closure */
    int delta = program->scope_stack->num_indep_vars;
    if (program->scope_stack->next)
        delta += 2; /* args and this, but not in global scope */

    if ((ref->exported_types & VAR_INDEPENDENT_ARGUMENT) == VAR_INDEPENDENT_ARGUMENT) {

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

        TRY(_gen_instr3(args->program,
                        INSTR_BIND,
                        args->func_reg->reg,
                        ref->args_reg->reg - delta,
                        reg->reg));
    }

    if ((ref->exported_types & VAR_INDEPENDENT_LOCAL) == VAR_INDEPENDENT_LOCAL) {

        assert(ref->vars_reg);
        assert(ref->vars_reg->reg >= delta);

        if (args->parent_frame == ref->closure->frame)
            TRY(_gen_instr2(args->program,
                            INSTR_BIND_SELF,
                            args->func_reg->reg,
                            ref->vars_reg->reg - delta));
        else {

            struct _KOS_SCOPE_REF *other_ref =
                    _KOS_find_scope_ref(args->parent_frame, ref->closure);

            TRY(_gen_instr3(args->program,
                            INSTR_BIND,
                            args->func_reg->reg,
                            ref->vars_reg->reg - delta,
                            other_ref->vars_reg->reg));
        }
    }

_error:
    return error;
}

static int _is_any_var_used(struct _KOS_RED_BLACK_NODE *node,
                            void                       *cookie)
{
    struct _KOS_VAR *var = (struct _KOS_VAR *)node;

    if (var->num_reads || var->num_assignments)
        return KOS_SUCCESS_RETURN;

    return KOS_SUCCESS;
}

static int _function_literal(struct _KOS_COMP_UNIT      *program,
                             const struct _KOS_AST_NODE *node,
                             struct _KOS_REG           **reg)
{
    int                error      = KOS_SUCCESS;
    struct _KOS_SCOPE *scope      = _push_scope(program, node);
    struct _KOS_FRAME *frame      = scope->frame;
    struct _KOS_FRAME *last_frame = program->cur_frame;
    struct _KOS_VAR   *var;
    struct _KOS_REG   *scope_reg  = 0;
    int                fun_start_offs;
    size_t             addr2line_start_offs;
    struct _BIND_ARGS  bind_args;

    const struct _KOS_AST_NODE *fun_node;
    const struct _KOS_AST_NODE *open_node;

    assert(frame);

    frame->fun_token    = &node->token;
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
        }

    /* Generate register for current arguments */
    TRY(_gen_reg(program, &frame->args_reg));
    assert(frame->args_reg->reg == scope->num_indep_vars);
    if (_KOS_red_black_walk(scope->vars, _is_any_var_used, 0) == KOS_SUCCESS_RETURN)
        frame->args_reg->tmp = 0;

    /* Generate register for 'this' */
    TRY(_gen_reg(program, &frame->this_reg));
    if (scope->uses_this)
        frame->this_reg->tmp = 0;

    /* Generate registers for closures */
    TRY(_KOS_red_black_walk(frame->closures, _gen_closure_vars, program));

    fun_start_offs       = program->cur_offs;
    addr2line_start_offs = program->addr2line_gen_buf.size;

    fun_node = node;
    node = node->children;
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

    TRY(_add_addr2line(program, &open_node->token, _KOS_TRUE));

    if (scope->ellipsis && (scope->ellipsis->type == VAR_INDEPENDENT_LOCAL ||
                (scope->ellipsis->type == VAR_LOCAL && scope->ellipsis->local_reads))) {
        if (scope->num_args)
            TRY(_gen_arg_list(program, scope->ellipsis, frame->args_reg, scope->num_args));
        else if (scope->ellipsis->type == VAR_INDEPENDENT_LOCAL) {
            assert(scope->ellipsis->reg);
            TRY(_gen_instr2(program, INSTR_MOVE, scope->ellipsis->reg->reg, frame->args_reg->reg));
        }
        else {
            assert( ! scope->ellipsis->reg);
            scope->ellipsis->reg = frame->args_reg;
            frame->args_reg->tmp = 0;
        }
    }

    /* Release unused registers */
    _free_reg(program, frame->args_reg);
    _free_reg(program, frame->this_reg);

    /* Generate code for function body */
    TRY(_visit_node(program, node, &scope_reg));
    assert(!scope_reg);

    /* Move the function code to final code_buf */
    TRY(_append_frame(program, fun_start_offs, addr2line_start_offs));

    program->cur_frame = last_frame;

    TRY(_add_addr2line(program, &fun_node->token, _KOS_FALSE));

    /* Generate LOAD.FUN/LOAD.GEN instruction in the parent frame */
    TRY(_gen_reg(program, reg));
    TRY(_gen_instr5(program,
                    frame->yield_token ? INSTR_LOAD_GEN : INSTR_LOAD_FUN,
                    (*reg)->reg,
                    0,
                    scope->num_args,
                    frame->num_regs < 2 ? 2 : frame->num_regs,
                    scope->num_indep_vars));

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
        case NT_DO:
            error = _do(program, node);
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
            error = _break_continue(program, node);
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
            error = _invocation(program, node, reg);
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
        case NT_VOID_LITERAL:
            error = _void_literal(program, node, reg);
            break;
        case NT_FUNCTION_LITERAL:
            error = _function_literal(program, node, reg);
            break;
        case NT_ARRAY_LITERAL:
            error = _array_literal(program, node, reg);
            break;
        case NT_OBJECT_LITERAL:
            /* fall through */
        default:
            assert(node->type == NT_OBJECT_LITERAL);
            error = _object_literal(program, node, reg);
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

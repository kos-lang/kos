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

#include "kos_disasm.h"
#include "kos_compiler.h"
#include "kos_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int _get_num_operands(enum _KOS_BYTECODE_INSTR instr)
{
    switch (instr) {

        case INSTR_BREAKPOINT:          /* fall through */
        case INSTR_CANCEL:              /* fall through */
        default:
            assert(instr == INSTR_BREAKPOINT || instr == INSTR_CANCEL);
            return 0;

        case INSTR_LOAD_TRUE:           /* fall through */
        case INSTR_LOAD_FALSE:          /* fall through */
        case INSTR_LOAD_VOID:           /* fall through */
        case INSTR_LOAD_OBJ:            /* fall through */
        case INSTR_JUMP:                /* fall through */
        case INSTR_YIELD:               /* fall through */
        case INSTR_THROW:
            return 1;

        case INSTR_LOAD_CONST8:         /* fall through */
        case INSTR_LOAD_CONST:          /* fall through */
        case INSTR_LOAD_INT8:           /* fall through */
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
        case INSTR_BIND_DEFAULTS:       /* fall through */
        case INSTR_RETURN:              /* fall through */
        case INSTR_CATCH:               /* fall through */
        case INSTR_PUSH:                /* fall through */
        case INSTR_PUSH_EX:
            return 2;

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
        case INSTR_SHRU:                /* fall through */
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
        case INSTR_CALL_GEN:
            return 3;

        case INSTR_CALL:                /* fall through */
        case INSTR_CALL_FUN:            /* fall through */
        case INSTR_TAIL_CALL:           /* fall through */
        case INSTR_TAIL_CALL_FUN:       /* fall through */
        case INSTR_GET_RANGE:
            return 4;

        case INSTR_CALL_N:              /* fall through */
        case INSTR_TAIL_CALL_N:
            return 5;

        case INSTR_LOAD_FUN:            /* fall through */
        case INSTR_LOAD_GEN:            /* fall through */
        case INSTR_LOAD_CTOR:
            return 6;
    }
}

int _KOS_get_operand_size(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST:
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
        case INSTR_LOAD_CTOR:
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
            /* fall through */
        case INSTR_LOAD_CTOR:
            if (op == 1)
                return 4;
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

int _KOS_is_register(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST8:
            /* fall through */
        case INSTR_LOAD_CONST:
            /* fall through */
        case INSTR_LOAD_INT8:
            /* fall through */
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_GEN:
            /* fall through */
        case INSTR_LOAD_CTOR:
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

        case INSTR_CALL_N:
            return op < 4;

        case INSTR_CALL_FUN:
            return op < 3;

        case INSTR_TAIL_CALL_N:
            return op && op < 4;

        case INSTR_TAIL_CALL_FUN:
            return op && op < 3;

        case INSTR_JUMP:
            return 0;

        default:
            break;
    }
    return 1;
}

int _KOS_is_signed_op(enum _KOS_BYTECODE_INSTR instr, int op)
{
    assert( ! _KOS_is_register(instr, op));
    assert(_KOS_get_operand_size(instr, op) == 1);
    switch (instr) {

        case INSTR_LOAD_INT8:
            return 1;

        default:
            break;
    }
    return 0;
}

static int _is_constant(enum _KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST8:
            /* fall through */
        case INSTR_LOAD_CONST:
            /* fall through */
        case INSTR_GET_PROP:
            /* fall through */
        case INSTR_SET_PROP:
            /* fall through */
        case INSTR_DEL_PROP:
            /* fall through */
        case INSTR_HAS_PROP:
            return ! _KOS_is_register(instr, op);

        default:
            break;
    }
    return 0;
}

void _KOS_disassemble(const char                          *filename,
                      uint32_t                             offs,
                      const uint8_t                       *bytecode,
                      uint32_t                             size,
                      const struct _KOS_COMP_ADDR_TO_LINE *line_addrs,
                      uint32_t                             num_line_addrs,
                      const char                   *const *func_names,
                      const struct _KOS_COMP_ADDR_TO_FUNC *func_addrs,
                      uint32_t                             num_func_addrs,
                      _KOS_PRINT_CONST                     print_const,
                      void                                *print_const_cookie)
{
    const struct _KOS_COMP_ADDR_TO_LINE *line_addrs_end  = line_addrs + num_line_addrs;
    const struct _KOS_COMP_ADDR_TO_FUNC *func_addrs_end  = func_addrs + num_func_addrs;
    struct _KOS_VECTOR                   const_buf;
    const size_t                         max_const_chars = 32;

    static const char *const str_instr[] = {
        "BREAKPOINT",
        "LOAD.CONST8",
        "LOAD.CONST",
        "LOAD.INT8",
        "LOAD.TRUE",
        "LOAD.FALSE",
        "LOAD.VOID",
        "LOAD.FUN",
        "LOAD.GEN",
        "LOAD.CTOR",
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
        "PUSH",
        "PUSH.EX",
        "DEL",
        "DEL.PROP",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "MOD",
        "SHL",
        "SHR",
        "SHRU",
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
        "BIND.DEFAULTS",
        "CALL",
        "CALL.N",
        "CALL.FUN",
        "CALL.GEN",
        "RETURN",
        "TAIL.CALL",
        "TAIL.CALL.N",
        "TAIL.CALL.FUN",
        "YIELD",
        "THROW",
        "CATCH",
        "CANCEL"
    };

    _KOS_vector_init(&const_buf);

    if (_KOS_vector_reserve(&const_buf, 128)) {
        printf("Error: Failed to allocate buffer\n");
        _KOS_vector_destroy(&const_buf);
        return;
    }

    bytecode += offs;
    size     -= offs;

    while (line_addrs < line_addrs_end && line_addrs->offs < offs)
        ++line_addrs;

    while (func_addrs < func_addrs_end && func_addrs->offs < offs)
        ++func_addrs;

    while (size) {
        int           i;
        int           iop;
        int           num_operands;
        int           instr_size   = 1;
        uint32_t      constant     = ~0U;
        int           has_constant = 0;
        char          bin[64];
        char          dis[128];
        size_t        dis_size;
        const char   *const_str    = "";
        const int     mnem_align   = 44;
        const char   *str_opcode;
        const uint8_t opcode = *bytecode;
        assert((unsigned)(opcode - INSTR_BREAKPOINT) <= sizeof(str_instr)/sizeof(str_instr[0]));

        assert(line_addrs == line_addrs_end ||
               offs <= line_addrs->offs);
        assert(func_addrs == func_addrs_end ||
               offs <= func_addrs->offs);

        if (func_addrs < func_addrs_end &&
            offs == func_addrs->offs) {

            printf("\n");
            if (func_names) {
                printf("-- %s() --\n", *func_names);
                ++func_names;
            }
            else
                printf("-- fun() --\n");
            ++func_addrs;
        }
        if (line_addrs < line_addrs_end &&
            offs == line_addrs->offs) {

            printf("@%s:%u:\n", filename, line_addrs->line);
            ++line_addrs;
        }

        str_opcode   = str_instr[opcode - INSTR_BREAKPOINT];
        num_operands = _get_num_operands((enum _KOS_BYTECODE_INSTR)opcode);

        dis[sizeof(dis)-1] = 0;
        dis_size           = strlen(str_opcode);
        memcpy(dis, str_opcode, dis_size);
        while (dis_size < 16)
            dis[dis_size++] = ' ';
        dis[dis_size] = 0;

        for (iop = 0; iop < num_operands; iop++) {
            const int opsize = _KOS_get_operand_size((enum _KOS_BYTECODE_INSTR)opcode, iop);
            int32_t   value  = 0;
            int       tail   = 0;

            assert(opsize == 1 || opsize == 4);

            for (i = 0; i < opsize; i++)
                value |= (int32_t)((uint32_t)bytecode[instr_size+i] << (8*i));

            if (_is_constant((enum _KOS_BYTECODE_INSTR)opcode, iop)) {
                constant     = (uint32_t)value;
                has_constant = 1;
            }

            tail = _get_offset_operand_tail((enum _KOS_BYTECODE_INSTR)opcode, iop);
            if (tail >= 0)
                snprintf(&dis[dis_size], sizeof(dis)-dis_size, "%08X", value + offs + instr_size + opsize + tail);
            else if (_KOS_is_register((enum _KOS_BYTECODE_INSTR)opcode, iop))
                snprintf(&dis[dis_size], sizeof(dis)-dis_size, "r%d", value);
            else {
                if (opsize == 1 && _KOS_is_signed_op((enum _KOS_BYTECODE_INSTR)opcode, iop))
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

        if (has_constant && print_const) {
            static const char header[] = " # ";
            memcpy(const_buf.buffer, header, sizeof(header));
            const_buf.size = sizeof(header);
            if ( ! print_const(print_const_cookie, &const_buf, constant)) {
                const_str = const_buf.buffer;
                if (const_buf.size > max_const_chars + 1) {
                    static const char dotdotdot[] = "...";
                    memcpy(const_buf.buffer + max_const_chars + 1 - sizeof(dotdotdot),
                           dotdotdot,
                           sizeof(dotdotdot));
                }
            }
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

        printf("%s%s%s\n", bin, dis, const_str);

        bytecode += instr_size;
        offs     += instr_size;
        assert(size >= (size_t)instr_size);
        if (size >= (size_t)instr_size)
            size -= instr_size;
    }

    _KOS_vector_destroy(&const_buf);
}

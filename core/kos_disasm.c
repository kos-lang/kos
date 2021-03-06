/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "kos_disasm.h"
#include "../inc/kos_memory.h"
#include "kos_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int get_num_operands(KOS_BYTECODE_INSTR instr)
{
    switch (instr) {

        default:
            assert(instr == INSTR_BREAKPOINT || instr == INSTR_CANCEL);
            return 0;

        case INSTR_LOAD_TRUE:           /* fall through */
        case INSTR_LOAD_FALSE:          /* fall through */
        case INSTR_LOAD_VOID:           /* fall through */
        case INSTR_LOAD_OBJ:            /* fall through */
        case INSTR_JUMP:                /* fall through */
        case INSTR_RETURN:              /* fall through */
        case INSTR_THROW:
            return 1;

        case INSTR_LOAD_CONST8:         /* fall through */
        case INSTR_LOAD_CONST:          /* fall through */
        case INSTR_LOAD_INT8:           /* fall through */
        case INSTR_LOAD_FUN8:           /* fall through */
        case INSTR_LOAD_FUN:            /* fall through */
        case INSTR_LOAD_ARRAY8:         /* fall through */
        case INSTR_LOAD_ARRAY:          /* fall through */
        case INSTR_LOAD_OBJ_PROTO:      /* fall through */
        case INSTR_LOAD_ITER:           /* fall through */
        case INSTR_MOVE:                /* fall through */
        case INSTR_GET_PROTO:           /* fall through */
        case INSTR_GET_GLOBAL:          /* fall through */
        case INSTR_SET_GLOBAL:          /* fall through */
        case INSTR_DEL:                 /* fall through */
        case INSTR_DEL_PROP8:           /* fall through */
        case INSTR_NOT:                 /* fall through */
        case INSTR_TYPE:                /* fall through */
        case INSTR_JUMP_COND:           /* fall through */
        case INSTR_JUMP_NOT_COND:       /* fall through */
        case INSTR_BIND_SELF:           /* fall through */
        case INSTR_BIND_DEFAULTS:       /* fall through */
        case INSTR_CATCH:               /* fall through */
        case INSTR_PUSH:                /* fall through */
        case INSTR_PUSH_EX:             /* fall through */
        case INSTR_YIELD:               /* fall through */
        case INSTR_NEXT:
            return 2;

        case INSTR_GET:                 /* fall through */
        case INSTR_GET_ELEM:            /* fall through */
        case INSTR_GET_PROP8:           /* fall through */
        case INSTR_GET_MOD:             /* fall through */
        case INSTR_GET_MOD_ELEM:        /* fall through */
        case INSTR_SET:                 /* fall through */
        case INSTR_SET_ELEM:            /* fall through */
        case INSTR_SET_PROP8:           /* fall through */
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
        case INSTR_HAS_DP:              /* fall through */
        case INSTR_HAS_DP_PROP8:        /* fall through */
        case INSTR_HAS_SH:              /* fall through */
        case INSTR_HAS_SH_PROP8:        /* fall through */
        case INSTR_INSTANCEOF:          /* fall through */
        case INSTR_BIND:                /* fall through */
        case INSTR_NEXT_JUMP:           /* fall through */
        case INSTR_TAIL_CALL:           /* fall through */
        case INSTR_TAIL_CALL_FUN:
            return 3;

        case INSTR_CALL:                /* fall through */
        case INSTR_CALL_FUN:            /* fall through */
        case INSTR_TAIL_CALL_N:         /* fall through */
        case INSTR_GET_RANGE:
            return 4;

        case INSTR_CALL_N:
            return 5;
    }
}

int kos_get_operand_size(KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST:
            /* fall through */
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_ARRAY:
            /* fall through */
        case INSTR_GET_GLOBAL:
            /* fall through */
        case INSTR_GET_MOD_ELEM:
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

        case INSTR_GET_MOD:
            /* fall through */
        case INSTR_SET_ELEM:
            if (op == 1)
                return 4;
            break;

        case INSTR_GET_ELEM:
            /* fall through */
        case INSTR_NEXT_JUMP:
            if (op == 2)
                return 4;
            break;

        default:
            break;
    }
    return 1;
}

/* Returns number of bytes after the offset in the instruction or -1 if not offset */
static int get_offset_operand_tail(KOS_BYTECODE_INSTR instr, int op)
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

        case INSTR_CATCH:
            if (op == 1)
                return 0;
            break;

        case INSTR_NEXT_JUMP:
            if (op == 2)
                return 0;
            break;

        default:
            break;
    }
    return -1;
}

int kos_is_register(KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST8:
            /* fall through */
        case INSTR_LOAD_CONST:
            /* fall through */
        case INSTR_LOAD_INT8:
            /* fall through */
        case INSTR_LOAD_FUN8:
            /* fall through */
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_LOAD_ARRAY8:
            /* fall through */
        case INSTR_LOAD_ARRAY:
            /* fall through */
        case INSTR_GET_GLOBAL:
            /* fall through */
        case INSTR_GET_MOD_ELEM:
            /* fall through */
        case INSTR_DEL_PROP8:
            /* fall through */
        case INSTR_BIND_SELF:
            /* fall through */
        case INSTR_CATCH:
            return op > 0 ? 0 : 1;

        case INSTR_GET_ELEM:
            /* fall through */
        case INSTR_GET_PROP8:
            /* fall through */
        case INSTR_HAS_DP_PROP8:
            /* fall through */
        case INSTR_HAS_SH_PROP8:
            return op > 1 ? 0 : 1;

        case INSTR_GET_MOD:
            /* fall through */
        case INSTR_SET_ELEM:
            /* fall through */
        case INSTR_SET_PROP8:
            /* fall through */
        case INSTR_BIND:
            return op == 1 ? 0 : 1;

        case INSTR_SET_GLOBAL:
            /* fall through */
        case INSTR_JUMP_COND:
            /* fall through */
        case INSTR_JUMP_NOT_COND:
            return op;

        case INSTR_CALL_N:
            return op < 4;

        case INSTR_CALL_FUN:
            /* fall through */
        case INSTR_TAIL_CALL_N:
            return op < 3;

        case INSTR_TAIL_CALL_FUN:
            /* fall through */
        case INSTR_NEXT_JUMP:
            return op < 2;

        case INSTR_JUMP:
            return 0;

        default:
            break;
    }
    return 1;
}

int kos_is_signed_op(KOS_BYTECODE_INSTR instr, int op)
{
    assert( ! kos_is_register(instr, op));
    assert(kos_get_operand_size(instr, op) == 1);
    switch (instr) {

        case INSTR_LOAD_INT8:
            return 1;

        default:
            break;
    }
    return 0;
}

static int is_constant(KOS_BYTECODE_INSTR instr, int op)
{
    switch (instr) {

        case INSTR_LOAD_CONST8:
            /* fall through */
        case INSTR_LOAD_CONST:
            /* fall through */
        case INSTR_LOAD_FUN8:
            /* fall through */
        case INSTR_LOAD_FUN:
            /* fall through */
        case INSTR_GET_PROP8:
            /* fall through */
        case INSTR_SET_PROP8:
            /* fall through */
        case INSTR_DEL_PROP8:
            /* fall through */
        case INSTR_HAS_DP_PROP8:
            /* fall through */
        case INSTR_HAS_SH_PROP8:
            return ! kos_is_register(instr, op);

        default:
            break;
    }
    return 0;
}

static const char *const str_instr[] = {
    "BREAKPOINT",
    "LOAD.INT8",
    "LOAD.CONST8",
    "LOAD.CONST",
    "LOAD.FUN8",
    "LOAD.FUN",
    "LOAD.TRUE",
    "LOAD.FALSE",
    "LOAD.VOID",
    "LOAD.ARRAY8",
    "LOAD.ARRAY",
    "LOAD.OBJ",
    "LOAD.OBJ.PROTO",
    "LOAD.ITER",
    "MOVE",
    "GET",
    "GET.ELEM",
    "GET.RANGE",
    "GET.PROP8",
    "GET.PROTO",
    "GET.GLOBAL",
    "GET.MOD",
    "GET.MOD.ELEM",
    "SET",
    "SET.ELEM",
    "SET.PROP8",
    "SET.GLOBAL",
    "PUSH",
    "PUSH.EX",
    "DEL",
    "DEL.PROP8",
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
    "HAS.DP",
    "HAS.DP.PROP8",
    "HAS.SH",
    "HAS.SH.PROP8",
    "INSTANCEOF",
    "JUMP",
    "JUMP.COND",
    "JUMP.NOT.COND",
    "NEXT.JUMP",
    "NEXT",
    "BIND",
    "BIND.SELF",
    "BIND.DEFAULTS",
    "CALL",
    "CALL.N",
    "CALL.FUN",
    "RETURN",
    "TAIL.CALL",
    "TAIL.CALL.N",
    "TAIL.CALL.FUN",
    "YIELD",
    "THROW",
    "CATCH",
    "CANCEL"
};

void kos_disassemble(const char                           *filename,
                     uint32_t                              offs,
                     const uint8_t                        *bytecode,
                     uint32_t                              size,
                     const struct KOS_COMP_ADDR_TO_LINE_S *line_addrs,
                     uint32_t                              num_line_addrs,
                     const char                    *const *func_names,
                     const struct KOS_COMP_ADDR_TO_FUNC_S *func_addrs,
                     uint32_t                              num_func_addrs,
                     KOS_PRINT_CONST                       print_const,
                     KOS_PRINT_FUNC                        print_func,
                     void                                 *print_cookie)
{
    const struct KOS_COMP_ADDR_TO_LINE_S *line_addrs_end  = line_addrs + num_line_addrs;
    const struct KOS_COMP_ADDR_TO_FUNC_S *func_addrs_end  = func_addrs + num_func_addrs;
    KOS_VECTOR                            const_buf;
    const size_t                          max_const_chars = 32;

    KOS_vector_init(&const_buf);

    if (KOS_vector_reserve(&const_buf, 128)) {
        printf("Error: Failed to allocate buffer\n");
        KOS_vector_destroy(&const_buf);
        return;
    }

    bytecode += offs;
    size     -= offs;

    while (line_addrs < line_addrs_end && line_addrs->offs < offs)
        ++line_addrs;

    while (func_addrs < func_addrs_end && func_addrs->offs < offs) {
        ++func_addrs;
        ++func_names;
    }

    while (size) {
        int            i;
        int            iop;
        int            num_operands;
        int            instr_size   = 1;
        uint32_t       constant     = ~0U;
        int            has_constant = 0;
        char           bin[64];
        unsigned       bin_len;
        char           dis[128];
        size_t         dis_size;
        const char    *const_str    = "";
        const unsigned mnem_align   = 44;
        const char    *str_opcode;
        const uint8_t  opcode = *bytecode;

        assert((unsigned)(opcode - INSTR_BREAKPOINT) <= sizeof(str_instr)/sizeof(str_instr[0]));

        assert(line_addrs == line_addrs_end ||
               offs <= line_addrs->offs);
        assert(func_addrs == func_addrs_end ||
               offs <= func_addrs->offs);

        if (func_addrs < func_addrs_end &&
            offs == func_addrs->offs) {

            const char *const name = func_names ? *(func_names++) : "fun";

            printf("\n");
            printf("-- %s() --\n", name);

            if (print_func)
                print_func(print_cookie, func_addrs->fun_idx);

            ++func_addrs;
        }
        if (line_addrs < line_addrs_end &&
            offs == line_addrs->offs) {

            printf("@%s:%u:\n", filename, line_addrs->line);
            ++line_addrs;
        }

        str_opcode   = str_instr[opcode - INSTR_BREAKPOINT];
        num_operands = get_num_operands((KOS_BYTECODE_INSTR)opcode);

        dis[0]   = 0;
        dis_size = 0;

        for (iop = 0; iop < num_operands; iop++) {
            const int opsize = kos_get_operand_size((KOS_BYTECODE_INSTR)opcode, iop);
            int32_t   value  = 0;
            int       tail   = 0;
            int       pr_size;
            char      dis_num[12];

            assert(opsize == 1 || opsize == 4);

            for (i = 0; i < opsize; i++)
                value |= (int32_t)((uint32_t)bytecode[instr_size+i] << (8*i));

            if (is_constant((KOS_BYTECODE_INSTR)opcode, iop)) {
                constant     = (uint32_t)value;
                has_constant = 1;
            }

            tail = get_offset_operand_tail((KOS_BYTECODE_INSTR)opcode, iop);
            if (tail >= 0)
                pr_size = snprintf(dis_num, sizeof(dis_num), "%08X",
                                   value + offs + instr_size + opsize + tail);
            else if (kos_is_register((KOS_BYTECODE_INSTR)opcode, iop))
                pr_size = snprintf(dis_num, sizeof(dis_num), "r%d", value);
            else {
                if (opsize == 1 && kos_is_signed_op((KOS_BYTECODE_INSTR)opcode, iop))
                    value = (int32_t)(int8_t)value;
                pr_size = snprintf(dis_num, sizeof(dis_num), "%d", value);
            }

            memcpy(&dis[dis_size], dis_num, pr_size);
            dis_size += (size_t)pr_size;

            if (iop + 1 < num_operands) {
                dis[dis_size++] = ',';
                dis[dis_size++] = ' ';
                dis[dis_size]   = 0;
            }
            else
                dis[dis_size]   = 0;

            assert(dis_size < sizeof(dis));

            instr_size += opsize;
        }

        if (has_constant && print_const) {
            static const char header[] = " # ";
            memcpy(const_buf.buffer, header, sizeof(header));
            const_buf.size = sizeof(header);
            if ( ! print_const(print_cookie, &const_buf, constant)) {
                const_str = const_buf.buffer;
                if (const_buf.size > max_const_chars + 1) {
                    static const char dotdotdot[] = "...";
                    memcpy(const_buf.buffer + max_const_chars + 1 - sizeof(dotdotdot),
                           dotdotdot,
                           sizeof(dotdotdot));
                }
            }
        }

        bin_len = (unsigned)snprintf(bin, sizeof(bin), "%08X: ", (unsigned)offs);
        for (i = 0; i < instr_size; i++) {
            snprintf(&bin[bin_len], sizeof(bin) - bin_len, "%02X ", (int)bytecode[i]);
            bin_len += 3;
        }
        while (bin_len < mnem_align)
            bin[bin_len++] = ' ';
        bin[bin_len] = 0;

        printf("%s%-15s%s%s\n", bin, str_opcode, dis, const_str);

        bytecode += instr_size;
        offs     += instr_size;
        assert(size >= (size_t)instr_size);
        if (size >= (size_t)instr_size)
            size -= instr_size;
    }

    fflush(stdout);

    KOS_vector_destroy(&const_buf);
}

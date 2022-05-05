/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_DISASM_H_INCLUDED
#define KOS_DISASM_H_INCLUDED

#include "../inc/kos_bytecode.h"
#include <stdint.h>

struct KOS_LINE_ADDR_S;
struct KOS_VECTOR_S;

int kos_get_operand_size(KOS_BYTECODE_INSTR instr, int op);

int kos_is_register(KOS_BYTECODE_INSTR instr, int op);

int kos_is_signed_op(KOS_BYTECODE_INSTR instr, int op);

typedef int (*KOS_PRINT_CONST)(void                *cookie,
                               struct KOS_VECTOR_S *cstr_buf,
                               uint32_t             const_index);

int kos_disassemble(const char                   *filename,
                    const uint8_t                *bytecode,
                    uint32_t                      size,
                    const struct KOS_LINE_ADDR_S *line_addr,
                    const struct KOS_LINE_ADDR_S *line_addr_end,
                    KOS_PRINT_CONST               print_const,
                    void                         *print_cookie);

#endif

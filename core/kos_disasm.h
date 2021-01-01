/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_DISASM_H_INCLUDED
#define KOS_DISASM_H_INCLUDED

#include "../inc/kos_bytecode.h"
#include <stdint.h>

struct KOS_COMP_ADDR_TO_LINE_S;
struct KOS_COMP_ADDR_TO_FUNC_S;
struct KOS_VECTOR_S;

int kos_get_operand_size(KOS_BYTECODE_INSTR instr, int op);

int kos_is_register(KOS_BYTECODE_INSTR instr, int op);

int kos_is_signed_op(KOS_BYTECODE_INSTR instr, int op);

typedef int (*KOS_PRINT_CONST)(void                *cookie,
                               struct KOS_VECTOR_S *cstr_buf,
                               uint32_t             const_index);

typedef int (*KOS_PRINT_FUNC)(void    *cookie,
                              uint32_t func_index);

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
                     void                                 *print_cookie);

#endif

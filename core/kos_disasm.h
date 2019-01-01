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
                     void                                 *print_const_cookie);

#endif

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

#ifndef __KOS_DISASM_H
#define __KOS_DISASM_H

#include "../inc/kos_bytecode.h"
#include <stdint.h>

struct _KOS_COMP_ADDR_TO_LINE;
struct _KOS_COMP_ADDR_TO_FUNC;

int _KOS_get_operand_size(enum _KOS_BYTECODE_INSTR instr, int op);

int _KOS_is_register(enum _KOS_BYTECODE_INSTR instr, int op);

int _KOS_is_signed_op(enum _KOS_BYTECODE_INSTR instr, int op);

void _KOS_disassemble(const char                          *filename,
                      uint32_t                             offs,
                      const uint8_t                       *bytecode,
                      uint32_t                             size,
                      const struct _KOS_COMP_ADDR_TO_LINE *line_addrs,
                      uint32_t                             num_line_addrs,
                      const char                   *const *func_names,
                      const struct _KOS_COMP_ADDR_TO_FUNC *func_addrs,
                      uint32_t                             num_func_addrs);

#endif

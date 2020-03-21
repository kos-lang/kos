/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_BYTECODE_H_INCLUDED
#define KOS_BYTECODE_H_INCLUDED

typedef enum KOS_BYTECODE_INSTR_U {

#define DEFINE_INSTRUCTION(name, value) INSTR_##name = value,
#include "kos_opcodes.h"
#undef DEFINE_INSTRUCTION

    INSTR_LAST_OPCODE
} KOS_BYTECODE_INSTR;

#endif

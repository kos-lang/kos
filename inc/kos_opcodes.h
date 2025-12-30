/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef DEFINE_INSTRUCTION
#error "Missing DEFINE_INSTRUCTION macro"
#endif

/* We have relatively few instructions.  Most of binary code consists of
 * bytes lower than 128.  Use values 128 and up to increase the chances
 * of jumping into an area which doesn't contain instructions when there
 * is an error. */

/* BREAKPOINT */
DEFINE_INSTRUCTION(BREAKPOINT, 0x80)

/* LOAD.INT8 <r.dest>, <int8> */
DEFINE_INSTRUCTION(LOAD_INT8, 0x81)
/* LOAD.CONST <r.dest>, <uimm> */
DEFINE_INSTRUCTION(LOAD_CONST, 0x82)
/* LOAD.FUN <r.dest>, <uimm> */
DEFINE_INSTRUCTION(LOAD_FUN, 0x83)
/* LOAD.TRUE <r.dest> */
DEFINE_INSTRUCTION(LOAD_TRUE, 0x84)
/* LOAD.FALSE <r.dest> */
DEFINE_INSTRUCTION(LOAD_FALSE, 0x85)
/* LOAD.VOID <r.dest> */
DEFINE_INSTRUCTION(LOAD_VOID, 0x86)
/* NEW.ARRAY8 <r.dest>, <uint8.size> */
DEFINE_INSTRUCTION(NEW_ARRAY8, 0x87)
/* NEW.OBJ <r.dest>, <r.src.proto> */
DEFINE_INSTRUCTION(NEW_OBJ, 0x88)
/* NEW.ITER <r.dest>, <r.src> */
DEFINE_INSTRUCTION(NEW_ITER, 0x89)

/* MOVE <r.dest>, <r.src>
 * Move data from one local variable to another. */
DEFINE_INSTRUCTION(MOVE, 0x8A)

/* GET <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(GET, 0x8B)
/* GET.OPT <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(GET_OPT, 0x8C)
/* GET.ELEM8 <r.dest>, <r.src>, <int8> */
DEFINE_INSTRUCTION(GET_ELEM8, 0x8D)
/* GET.ELEM8.OPT <r.dest>, <r.src>, <int8> */
DEFINE_INSTRUCTION(GET_ELEM8_OPT, 0x8E)
/* GET.RANGE <r.dest>, <r.src>, <r.begin>, <r.end> */
DEFINE_INSTRUCTION(GET_RANGE, 0x8F)
/* GET.PROP8 <r.dest>, <r.src>, <uint8.str.idx> */
DEFINE_INSTRUCTION(GET_PROP8, 0x90)
/* GET.PROP8.OPT <r.dest>, <r.src>, <uint8.str.idx> */
DEFINE_INSTRUCTION(GET_PROP8_OPT, 0x91)
/* GET.PROTO <r.dest>, <r.src> */
DEFINE_INSTRUCTION(GET_PROTO, 0x92)
/* GET.GLOBAL <r.dest>, <uimm.glob.idx> */
DEFINE_INSTRUCTION(GET_GLOBAL, 0x93)
/* GET.MOD.GLOBAL <r.dest>, <uimm.mod.idx>, <r.glob> */
DEFINE_INSTRUCTION(GET_MOD_GLOBAL, 0x94)
/* GET.MOD.GLOBAL.OPT <r.dest>, <uimm.mod.idx>, <r.glob> */
DEFINE_INSTRUCTION(GET_MOD_GLOBAL_OPT, 0x95)
/* GET.MOD.ELEM <r.dest>, <uimm.mod.idx>, <uimm.glob.idx> */
DEFINE_INSTRUCTION(GET_MOD_ELEM, 0x96)
/* GET.MOD <r.dest>, <uimm.mod.idx> */
DEFINE_INSTRUCTION(GET_MOD, 0x97)

/* SET <r.dest>, <r.prop>, <r.src> */
DEFINE_INSTRUCTION(SET, 0x98)
/* SET.ELEM8 <r.dest>, <int8>, <r.src> */
DEFINE_INSTRUCTION(SET_ELEM8, 0x99)
/* SET.PROP8 <r.dest>, <uint8.str.idx>, <r.src> */
DEFINE_INSTRUCTION(SET_PROP8, 0x9A)
/* SET.GLOBAL <uimm.glob.idx>, <r.src> */
DEFINE_INSTRUCTION(SET_GLOBAL, 0x9B)

/* PUSH <r.dest>, <r.src> */
/* Append r.src to array in r.dest */
DEFINE_INSTRUCTION(PUSH, 0x9C)
/* PUSH.EX <r.dest>, <r.src> */
/* Expand elements from r.src and append them to array in r.dest */
DEFINE_INSTRUCTION(PUSH_EX, 0x9D)

/* DEL <r.dest>, <r.prop> */
DEFINE_INSTRUCTION(DEL, 0x9E)

/* ADD <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(ADD, 0x9F)
/* SUB <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(SUB, 0xA0)
/* MUL <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(MUL, 0xA1)
/* DIV <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(DIV, 0xA2)
/* MOD <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(MOD, 0xA3)
/* SHL <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(SHL, 0xA4)
/* SHR <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(SHR, 0xA5)
/* SHRU <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(SHRU, 0xA6)
/* NOT <r.dest>, <r.src> */
DEFINE_INSTRUCTION(NOT, 0xA7)
/* AND <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(AND, 0xA8)
/* OR <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(OR, 0xA9)
/* XOR <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(XOR, 0xAA)

/* TYPE <r.dest>, <r.src> */
DEFINE_INSTRUCTION(TYPE, 0xAB)

/* CMP.EQ <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(CMP_EQ, 0xAC)
/* CMP.NE <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(CMP_NE, 0xAD)
/* CMP.LE <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(CMP_LE, 0xAE)
/* CMP.LT <r.dest>, <r.src1>, <r.src2> */
DEFINE_INSTRUCTION(CMP_LT, 0xAF)

/* HAS.DP <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(HAS_DP, 0xB0)
/* HAS.DP.PROP8 <r.dest>, <r.src>, <uint8.str.idx> */
DEFINE_INSTRUCTION(HAS_DP_PROP8, 0xB1)
/* HAS.SH <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(HAS_SH, 0xB2)
/* HAS.SH.PROP8 <r.dest>, <r.src>, <uint8.str.idx> */
DEFINE_INSTRUCTION(HAS_SH_PROP8, 0xB3)
/* INSTANCEOF <r.dest>, <r.src>, <r.func> */
DEFINE_INSTRUCTION(INSTANCEOF, 0xB4)

/* JUMP <simm.delta>
 * Relative, unconditional jump. */
DEFINE_INSTRUCTION(JUMP, 0xB5)
/* JUMP.COND <simm.delta>, <r.src>
 * Relative jump, taken only if r.src is truthy. */
DEFINE_INSTRUCTION(JUMP_COND, 0xB6)
/* JUMP.NOT.COND <simm.delta>, <r.src>
 * Relative jump, taken only if r.src is falsy. */
DEFINE_INSTRUCTION(JUMP_NOT_COND, 0xB7)

/* NEXT.JUMP <r.dest>, <r.func>, <simm.delta> */
/* Call generator created with NEW.ITER.
 * If generator yields a value, jump to the specified offset. */
DEFINE_INSTRUCTION(NEXT_JUMP, 0xB8)
/* NEXT <r.dest>, <r.func> */
/* Call generator created with NEW.ITER.
 * If generator ends, throw an exception. */
DEFINE_INSTRUCTION(NEXT, 0xB9)

/* BIND <r.dest>, <uint8.slot.idx>, <r.src>
 * Bind an array to a function (closure). */
DEFINE_INSTRUCTION(BIND, 0xBA)
/* BIND.SELF <r.dest>, <uint8.slot.idx> */
DEFINE_INSTRUCTION(BIND_SELF, 0xBB)
/* BIND.DEFAULTS <r.dest>, <r.src>
 * Bind an array to a function as a list of arg default values. */
DEFINE_INSTRUCTION(BIND_DEFAULTS, 0xBC)

/* CALL <r.dest>, <r.func>, <r.this>, <r.args> */
DEFINE_INSTRUCTION(CALL, 0xBD)
/* CALL.N <r.dest>, <r.func>, <r.this>, <r.arg1>, <uint8.numargs> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs is 0. */
DEFINE_INSTRUCTION(CALL_N, 0xBE)
/* CALL.FUN <r.dest>, <r.func>, <r.arg1>, <uint8.numargs> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs is 0. */
DEFINE_INSTRUCTION(CALL_FUN, 0xBF)
/* RETURN <r.src> */
DEFINE_INSTRUCTION(RETURN, 0xC0)
/* TAIL.CALL <r.func>, <r.this>, <r.args> */
DEFINE_INSTRUCTION(TAIL_CALL, 0xC1)
/* TAIL.CALL.N <r.func>, <r.this>, <r.arg1>, <uint8.numargs> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs is 0. */
DEFINE_INSTRUCTION(TAIL_CALL_N, 0xC2)
/* TAIL.CALL.FUN <r.func>, <r.arg1>, <uint8.numargs> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs is 0. */
DEFINE_INSTRUCTION(TAIL_CALL_FUN, 0xC3)
/* YIELD <r.dest>, <r.src> */
DEFINE_INSTRUCTION(YIELD, 0xC4)
/* THROW <r.src> */
DEFINE_INSTRUCTION(THROW, 0xC5)
/* CATCH <r.dest>, <simm.delta> */
DEFINE_INSTRUCTION(CATCH, 0xC6)
/* CANCEL */
DEFINE_INSTRUCTION(CANCEL, 0xC7)

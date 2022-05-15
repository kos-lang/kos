/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef DEFINE_INSTRUCTION
#error "Missing DEFINE_INSTRUCTION macro"
#endif

/* BREAKPOINT */
DEFINE_INSTRUCTION(BREAKPOINT, 0x80)
/* We have relatively few instructions.  Most of binary code consists of
 * bytes lower than 128.  Use values 128 and up to increase the chances
 * of jumping into an area which doesn't contain instructions when there
 * is an error. */

/* LOAD.INT8 <r.dest>, <int8> */
DEFINE_INSTRUCTION(LOAD_INT8, 0x81)
/* LOAD.CONST8 <r.dest>, <uint8> */
DEFINE_INSTRUCTION(LOAD_CONST8, 0x82)
/* LOAD.CONST <r.dest>, <uint16> */
DEFINE_INSTRUCTION(LOAD_CONST, 0x83)
/* LOAD.FUN8 <r.dest>, <uint8> */
DEFINE_INSTRUCTION(LOAD_FUN8, 0x84)
/* LOAD.FUN <r.dest>, <uint16> */
DEFINE_INSTRUCTION(LOAD_FUN, 0x85)
/* LOAD.TRUE <r.dest> */
DEFINE_INSTRUCTION(LOAD_TRUE, 0x86)
/* LOAD.FALSE <r.dest> */
DEFINE_INSTRUCTION(LOAD_FALSE, 0x87)
/* LOAD.VOID <r.dest> */
DEFINE_INSTRUCTION(LOAD_VOID, 0x88)
/* LOAD.ARRAY <r.dest>, <size.uint8> */
DEFINE_INSTRUCTION(LOAD_ARRAY, 0x89)
/* LOAD.OBJ <r.dest> */
DEFINE_INSTRUCTION(LOAD_OBJ, 0x8A)
/* LOAD.OBJ.PROTO <r.dest>, <r.src> */
DEFINE_INSTRUCTION(LOAD_OBJ_PROTO, 0x8B)
/* LOAD.ITER <r.dest>, <r.src> */
DEFINE_INSTRUCTION(LOAD_ITER, 0x8C)

/* TODO SAVE.ARRAY <r.dest>, <r.src>, <size.uint8>
 * Save `size` registers starting with r.src into a new array. */
/* TODO RESTORE.ARRAY <r.dest>, <r.src>, <size.uint8>
 * Populate `size` registers starting with r.dest with array from r.src. */

/* MOVE <r.dest>, <r.src>
 * Move data from one local variable to another. */
DEFINE_INSTRUCTION(MOVE, 0x8D)

/* GET <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(GET, 0x8E)
/* GET.ELEM <r.dest>, <r.src>, <int32> */
DEFINE_INSTRUCTION(GET_ELEM, 0x8F)
/* TODO GET.ELEM8 <r.dest>, <r.src>, <int8> */
/* GET.RANGE <r.dest>, <r.src>, <r.begin>, <r.end> */
DEFINE_INSTRUCTION(GET_RANGE, 0x90)
/* GET.PROP8 <r.dest>, <r.src>, <str.idx.uint8> */
DEFINE_INSTRUCTION(GET_PROP8, 0x91)
/* GET.PROTO <r.dest>, <r.src> */
DEFINE_INSTRUCTION(GET_PROTO, 0x92)
/* GET.GLOBAL <r.dest>, <int32> */
DEFINE_INSTRUCTION(GET_GLOBAL, 0x93)
/* GET.MOD.GLOBAL <r.dest>, <uint16>, <r.glob> */
DEFINE_INSTRUCTION(GET_MOD_GLOBAL, 0x94)
/* GET.MOD.ELEM <r.dest>, <uint16>, <int32> */
DEFINE_INSTRUCTION(GET_MOD_ELEM, 0x95)
/* GET.MOD <r.dest>, <uint16> */
DEFINE_INSTRUCTION(GET_MOD, 0x96)

/* SET <r.dest>, <r.prop>, <r.src> */
DEFINE_INSTRUCTION(SET, 0x97)
/* SET.ELEM <r.dest>, <int32>, <r.src> */
DEFINE_INSTRUCTION(SET_ELEM, 0x98)
/* TODO SET.ELEM8 <r.dest>, <int8>, <r.src> */
/* SET.PROP8 <r.dest>, <str.idx.uint8>, <r.src> */
DEFINE_INSTRUCTION(SET_PROP8, 0x99)
/* SET.GLOBAL <int32>, <r.src> */
DEFINE_INSTRUCTION(SET_GLOBAL, 0x9A)

/* PUSH <r.dest>, <r.src> */
/* Append r.src to array in r.dest */
DEFINE_INSTRUCTION(PUSH, 0x9B)
/* PUSH.EX <r.dest>, <r.src> */
/* Expand elements from r.src and append them to array in r.dest */
DEFINE_INSTRUCTION(PUSH_EX, 0x9C)

/* DEL <r.dest>, <r.prop> */
DEFINE_INSTRUCTION(DEL, 0x9D)
/* DEL.PROP8 <r.dest>, <str.idx.uint8> */
DEFINE_INSTRUCTION(DEL_PROP8, 0x9E)

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
/* HAS.DP.PROP8 <r.dest>, <r.src>, <str.idx.uint8> */
DEFINE_INSTRUCTION(HAS_DP_PROP8, 0xB1)
/* HAS.SH <r.dest>, <r.src>, <r.prop> */
DEFINE_INSTRUCTION(HAS_SH, 0xB2)
/* HAS.SH.PROP8 <r.dest>, <r.src>, <str.idx.uint8> */
DEFINE_INSTRUCTION(HAS_SH_PROP8, 0xB3)
/* INSTANCEOF <r.dest>, <r.src>, <r.func> */
DEFINE_INSTRUCTION(INSTANCEOF, 0xB4)

/* JUMP <delta.int32>
 * Relative, unconditional jump. */
DEFINE_INSTRUCTION(JUMP, 0xB5)
/* JUMP.COND <delta.int32>, <r.src>
 * Relative jump, taken only if r.src is truthy. */
DEFINE_INSTRUCTION(JUMP_COND, 0xB6)
/* JUMP.NOT.COND <delta.int32>, <r.src>
 * Relative jump, taken only if r.src is falsy. */
DEFINE_INSTRUCTION(JUMP_NOT_COND, 0xB7)

/* NEXT.JUMP <r.dest>, <r.func>, <delta.int32> */
/* Call generator created with LOAD.ITER.
 * If generator yields a value, jump to the specified offset. */
DEFINE_INSTRUCTION(NEXT_JUMP, 0xB8)
/* NEXT <r.dest>, <r.func> */
/* Call generator created with LOAD.ITER.
 * If generator ends, throw an exception. */
DEFINE_INSTRUCTION(NEXT, 0xB9)

/* BIND <r.dest>, <slot.idx.uint8>, <r.src>
 * Bind an array to a function (closure). */
DEFINE_INSTRUCTION(BIND, 0xBA)
/* BIND.SELF <r.dest>, <slot.idx.uint8> */
DEFINE_INSTRUCTION(BIND_SELF, 0xBB)
/* BIND.DEFAULTS <r.dest>, <r.src>
 * Bind an array to a function (closure) as a list of arg default values. */
DEFINE_INSTRUCTION(BIND_DEFAULTS, 0xBC)

/* CALL <r.dest>, <r.func>, <r.this>, <r.args> */
DEFINE_INSTRUCTION(CALL, 0xBD)
/* CALL.N <r.dest>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs.uint8 is 0. */
DEFINE_INSTRUCTION(CALL_N, 0xBE)
/* CALL.FUN <r.dest>, <r.func>, <r.arg1>, <numargs.uint8> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs.uint8 is 0. */
DEFINE_INSTRUCTION(CALL_FUN, 0xBF)
/* RETURN <r.src> */
DEFINE_INSTRUCTION(RETURN, 0xC0)
/* TAIL.CALL <r.func>, <r.this>, <r.args> */
DEFINE_INSTRUCTION(TAIL_CALL, 0xC1)
/* TAIL.CALL.N <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs.uint8 is 0. */
DEFINE_INSTRUCTION(TAIL_CALL_N, 0xC2)
/* TAIL.CALL.FUN <r.func>, <r.arg1>, <numargs.uint8> */
/* Arguments are in consecutive registers, r.arg1 ignored if numargs.uint8 is 0. */
DEFINE_INSTRUCTION(TAIL_CALL_FUN, 0xC3)
/* YIELD <r.dest>, <r.src> */
DEFINE_INSTRUCTION(YIELD, 0xC4)
/* THROW <r.src> */
DEFINE_INSTRUCTION(THROW, 0xC5)
/* CATCH <r.dest>, <delta.int32> */
DEFINE_INSTRUCTION(CATCH, 0xC6)
/* CANCEL */
DEFINE_INSTRUCTION(CANCEL, 0xC7)

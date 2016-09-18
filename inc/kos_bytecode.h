/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#ifndef __KOS_BYTECODE_H
#define __KOS_BYTECODE_H

typedef enum _KOS_BYTECODE_INSTR {
    /* BREAKPOINT */
    INSTR_BREAKPOINT,

    /* LOAD.INT8 <r.dest>, <int8> */
    INSTR_LOAD_INT8,
    /* LOAD.INT32 <r.dest>, <int32> */
    INSTR_LOAD_INT32,
    /* LOAD.INT64 <r.dest>, <low.uint32>, <high.int32> */
    INSTR_LOAD_INT64,
    /* LOAD.FLOAT <r.dest>, <low.uint32>, <high.uint32> */
    INSTR_LOAD_FLOAT,
    /* LOAD.STR <r.dest>, <str.idx.int32> */
    INSTR_LOAD_STR,
    /* LOAD.TRUE <r.dest> */
    INSTR_LOAD_TRUE,
    /* LOAD.FALSE <r.dest> */
    INSTR_LOAD_FALSE,
    /* LOAD.VOID <r.dest> */
    INSTR_LOAD_VOID,
    /* LOAD.FUN <r.dest>, <delta32>, <min.args>, <num.regs>, <args.reg>
     * Create a function object with the specified code. */
    INSTR_LOAD_FUN,
    /* LOAD.GEN <r.dest>, <delta32>, <min.args>, <num.regs>, <args.reg>
     * Create a generator object with the specified code. */
    INSTR_LOAD_GEN,
    /* LOAD.ARRAY8 <r.dest>, <int.size8> */
    INSTR_LOAD_ARRAY8,
    /* LOAD.ARRAY <r.dest>, <int.size32> */
    INSTR_LOAD_ARRAY,
    /* LOAD.OBJ <r.dest> */
    INSTR_LOAD_OBJ,

    /* MOVE <r.dest>, <r.src>
     * Move data from one local variable to another. */
    INSTR_MOVE,

    /* GET <r.dest>, <r.src>, <r.prop> */
    INSTR_GET,
    /* GET.ELEM <r.dest>, <r.src>, <int32> */
    INSTR_GET_ELEM,
    /* TODO GET.ELEM8 <r.dest>, <r.src>, <int8> */
    /* GET.RANGE <r.dest>, <r.src>, <r.begin>, <r.end> */
    INSTR_GET_RANGE,
    /* GET.PROP <r.dest>, <r.src>, <str.idx.int32> */
    INSTR_GET_PROP,
    /* GET.GLOBAL <r.dest>, <int32> */
    INSTR_GET_GLOBAL,
    /* GET.MOD <r.dest>, <int32>, <r.glob> */
    INSTR_GET_MOD,
    /* GET.MOD.ELEM <r.dest>, <int32>, <int32> */
    INSTR_GET_MOD_ELEM,

    /* SET <r.dest>, <r.prop>, <r.src> */
    INSTR_SET,
    /* SET.ELEM <r.dest>, <int32>, <r.src> */
    INSTR_SET_ELEM,
    /* TODO SET.ELEM8 <r.dest>, <int8>, <r.src> */
    /* SET.PROP <r.dest>, <str.idx.int32>, <r.src> */
    INSTR_SET_PROP,
    /* SET.GLOBAL <int32>, <r.src> */
    INSTR_SET_GLOBAL,

    /* DEL <r.dest>, <r.prop> */
    INSTR_DEL,
    /* DEL.PROP <r.dest>, <str.idx.int32> */
    INSTR_DEL_PROP,

    /* ADD <r.dest>, <r.src1>, <r.src2> */
    INSTR_ADD,
    /* SUB <r.dest>, <r.src1>, <r.src2> */
    INSTR_SUB,
    /* MUL <r.dest>, <r.src1>, <r.src2> */
    INSTR_MUL,
    /* DIV <r.dest>, <r.src1>, <r.src2> */
    INSTR_DIV,
    /* MOD <r.dest>, <r.src1>, <r.src2> */
    INSTR_MOD,
    /* SHL <r.dest>, <r.src1>, <r.src2> */
    INSTR_SHL,
    /* SHR <r.dest>, <r.src1>, <r.src2> */
    INSTR_SHR,
    /* SSR <r.dest>, <r.src1>, <r.src2> */
    INSTR_SSR,
    /* NOT <r.dest>, <r.src> */
    INSTR_NOT,
    /* AND <r.dest>, <r.src1>, <r.src2> */
    INSTR_AND,
    /* OR <r.dest>, <r.src1>, <r.src2> */
    INSTR_OR,
    /* XOR <r.dest>, <r.src1>, <r.src2> */
    INSTR_XOR,

    /* TYPE <r.dest>, <r.src> */
    INSTR_TYPE,

    /* CMP.EQ <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_EQ,
    /* CMP.NE <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_NE,
    /* CMP.GE <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_GE,
    /* CMP.GT <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_GT,
    /* CMP.LE <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_LE,
    /* CMP.LT <r.dest>, <r.src1>, <r.src2> */
    INSTR_CMP_LT,

    /* HAS <r.dest>, <r.src>, <r.prop> */
    INSTR_HAS,
    /* HAS.PROP <r.dest>, <r.src>, <str.idx.int32> */
    INSTR_HAS_PROP,
    /* INSTANCEOF <r.dest>, <r.src>, <r.func> */
    INSTR_INSTANCEOF,

    /* JUMP <delta32>
     * Relative, unconditional jump. */
    INSTR_JUMP,
    /* JUMP.COND <delta32>, <r.src>
     * Relative jump, taken only if r.src is truthy. */
    INSTR_JUMP_COND,
    /* JUMP.NOT.COND <delta32>, <r.src>
     * Relative jump, taken only if r.src is falsy. */
    INSTR_JUMP_NOT_COND,

    /* BIND <r.dest>, <slot.idx.uint8>, <r.src>
     * Bind an array to a function (closure). */
    INSTR_BIND,
    /* BIND.SELF <r.dest>, <slot.idx.uint8> */
    INSTR_BIND_SELF,

    /* CALL <r.dest>, <r.func>, <r.this>, <r.args> */
    INSTR_CALL,
    /* TODO CALL.ARGS <r.dest>, <r.func>, <r.this>, <r.arg1>, <num.args.uint8> */
    /* TODO CALL.FUN <r.dest>, <r.func>, <r.arg1>, <num.args.uint8> */
    /* CALL.GEN <r.dest>, <r.func>, <r.final>, <r.args> */
    /* Call generator which is "READY" or "ACTIVE".  r.final is set to false
     * if a subsequent value is yielded or true if the generator returned. */
    INSTR_CALL_GEN,
    /* NEW <r.dest>, <r.func>, <r.args> */
    INSTR_NEW,
    /* RETURN <closure.size.int>, <r.src> */
    INSTR_RETURN,
    /* TAIL.CALL <closure.size.int>, <r.func>, <r.this>, <r.args> */
    INSTR_TAIL_CALL,
    /* YIELD <r.src> */
    INSTR_YIELD,
    /* THROW <r.src> */
    INSTR_THROW,
    /* CATCH <r.dest>, <delta32> */
    INSTR_CATCH,
    /* CATCH.CANCEL */
    INSTR_CATCH_CANCEL
} KOS_BYTECODE_INSTR;

#endif

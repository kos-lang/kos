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

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_bytecode.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include "kos_vm.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static const char str_err_args_not_array[]      = "function arguments are not an array";
static const char str_err_cannot_yield[]        = "function is not a generator";
static const char str_err_div_by_zero[]         = "division by zero";
static const char str_err_generator_end[]       = "generator";
static const char str_err_generator_running[]   = "generator is running";
static const char str_err_invalid_byte_value[]  = "buffer element value out of range";
static const char str_err_invalid_index[]       = "index out of range";
static const char str_err_invalid_instruction[] = "invalid instruction";
static const char str_err_not_callable[]        = "object is not callable";
static const char str_err_not_function[]        = "object is not a function";
static const char str_err_not_generator[]       = "function is not a generator";
static const char str_err_too_few_args[]        = "not enough arguments passed to a function";
static const char str_err_unsup_operand_types[] = "unsupported operand types";

#define NEW_THIS ((KOS_OBJ_ID)(intptr_t)0xC001)

static int _exec_function(KOS_FRAME stack_frame);

static KOS_OBJ_ID _make_string(KOS_FRAME           frame,
                               struct _KOS_MODULE *module,
                               int                 idx)
{
    return KOS_array_read(frame, module->strings, idx);
}

static KOS_OBJ_ID _add_integer(KOS_FRAME  frame,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                ret = KOS_new_int(frame, a + GET_SMALL_INT(bobj));
                break;

            case OBJ_NUM_INTEGER:
                ret = KOS_new_int(frame, a + *OBJPTR(INTEGER, bobj));
                break;

            case OBJ_NUM_FLOAT:
                ret = KOS_new_float(frame, a + *OBJPTR(FLOAT, bobj));
                break;
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _add_float(KOS_FRAME  frame,
                             double     a,
                             KOS_OBJ_ID bobj)
{
    if (IS_NUMERIC_OBJ(bobj)) {

        double b;

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                b = (double)GET_SMALL_INT(bobj);
                break;

            case OBJ_NUM_INTEGER:
                b = (double)*OBJPTR(INTEGER, bobj);
                break;

            case OBJ_NUM_FLOAT:
                b = *OBJPTR(FLOAT, bobj);
                break;
        }

        return KOS_new_float(frame, a + b);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }
}

static KOS_OBJ_ID _sub_integer(KOS_FRAME  frame,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                ret = KOS_new_int(frame, a - GET_SMALL_INT(bobj));
                break;

            case OBJ_NUM_INTEGER:
                ret = KOS_new_int(frame, a - *OBJPTR(INTEGER, bobj));
                break;

            case OBJ_NUM_FLOAT:
                ret = KOS_new_float(frame, a - *OBJPTR(FLOAT, bobj));
                break;
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _sub_float(KOS_FRAME  frame,
                             double     a,
                             KOS_OBJ_ID bobj)
{
    if (IS_NUMERIC_OBJ(bobj)) {

        double b;

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                b = (double)GET_SMALL_INT(bobj);
                break;

            case OBJ_NUM_INTEGER:
                b = (double)*OBJPTR(INTEGER, bobj);
                break;

            case OBJ_NUM_FLOAT:
                b = *OBJPTR(FLOAT, bobj);
                break;
        }

        return KOS_new_float(frame, a - b);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }
}

static KOS_OBJ_ID _mul_integer(KOS_FRAME  frame,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                ret = KOS_new_int(frame, a * GET_SMALL_INT(bobj));
                break;

            case OBJ_NUM_INTEGER:
                ret = KOS_new_int(frame, a * *OBJPTR(INTEGER, bobj));
                break;

            case OBJ_NUM_FLOAT:
                ret = KOS_new_float(frame, a * *OBJPTR(FLOAT, bobj));
                break;
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _mul_float(KOS_FRAME  frame,
                             double     a,
                             KOS_OBJ_ID bobj)
{
    if (IS_NUMERIC_OBJ(bobj)) {

        double b;

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                b = (double)GET_SMALL_INT(bobj);
                break;

            case OBJ_NUM_INTEGER:
                b = (double)*OBJPTR(INTEGER, bobj);
                break;

            case OBJ_NUM_FLOAT:
                b = *OBJPTR(FLOAT, bobj);
                break;
        }

        return KOS_new_float(frame, a * b);
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        return KOS_BADPTR;
    }
}

static KOS_OBJ_ID _div_integer(KOS_FRAME  frame,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        const enum KOS_NUMERIC_TAG type = GET_NUMERIC_TYPE(bobj);

        if (type == OBJ_NUM_FLOAT) {

            const double b = *OBJPTR(FLOAT, bobj);

            if (b != 0)
                ret = KOS_new_float(frame, (double)a / b);
            else {
                KOS_raise_exception_cstring(frame, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
        }
        else {

            int64_t b;

            if (type == OBJ_NUM_INTEGER)
                b = *OBJPTR(INTEGER, bobj);
            else
                b = GET_SMALL_INT(bobj);

            if (b)
                ret = KOS_new_int(frame, a / b);
            else {
                KOS_raise_exception_cstring(frame, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _div_float(KOS_FRAME  frame,
                             double     a,
                             KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        double b;

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                b = (double)GET_SMALL_INT(bobj);
                break;

            case OBJ_NUM_INTEGER:
                b = (double)*OBJPTR(INTEGER, bobj);
                break;

            case OBJ_NUM_FLOAT:
                b = *OBJPTR(FLOAT, bobj);
                break;
        }

        if (b != 0)
            ret = KOS_new_float(frame, a / b);
        else {
            KOS_raise_exception_cstring(frame, str_err_div_by_zero);
            ret = KOS_BADPTR;
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _mod_integer(KOS_FRAME  frame,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        const enum KOS_NUMERIC_TAG type = GET_NUMERIC_TYPE(bobj);

        if (type == OBJ_NUM_FLOAT) {

            const double b = *OBJPTR(FLOAT, bobj);

            if (b != 0)
                ret = KOS_new_float(frame, fmod((double)a, b));
            else {
                KOS_raise_exception_cstring(frame, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
        }
        else {

            int64_t b;

            if (type == OBJ_NUM_INTEGER)
                b = *OBJPTR(INTEGER, bobj);
            else
                b = GET_SMALL_INT(bobj);

            if (b)
                ret = KOS_new_int(frame, a % b);
            else {
                KOS_raise_exception_cstring(frame, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _mod_float(KOS_FRAME  frame,
                             double     a,
                             KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_NUMERIC_OBJ(bobj)) {

        double b;

        switch (GET_NUMERIC_TYPE(bobj)) {

            default:
                b = (double)GET_SMALL_INT(bobj);
                break;

            case OBJ_NUM_INTEGER:
                b = (double)*OBJPTR(INTEGER, bobj);
                break;

            case OBJ_NUM_FLOAT:
                b = *OBJPTR(FLOAT, bobj);
                break;
        }

        if (b != 0)
            ret = KOS_new_float(frame, fmod(a, b));
        else {
            KOS_raise_exception_cstring(frame, str_err_div_by_zero);
            ret = KOS_BADPTR;
        }
    }
    else {
        KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
        ret = KOS_BADPTR;
    }

    return ret;
}

static int _compare_integer(KOS_BYTECODE_INSTR instr,
                            int64_t            a,
                            int64_t            b)
{
    int ret;

    switch (instr) {

        case INSTR_CMP_EQ:
            /* fall through*/
        default:
            assert(instr == INSTR_CMP_EQ);
            ret = a == b;
            break;

        case INSTR_CMP_NE:
            ret = a != b;
            break;

        case INSTR_CMP_LT:
            ret = a < b;
            break;

        case INSTR_CMP_LE:
            ret = a <= b;
            break;
    }

    return ret;
}

static int _compare_float(KOS_BYTECODE_INSTR instr,
                          KOS_OBJ_ID         aobj,
                          KOS_OBJ_ID         bobj)
{
    int                  ret;
    enum KOS_NUMERIC_TAG a_type = GET_NUMERIC_TYPE(aobj);
    enum KOS_NUMERIC_TAG b_type = GET_NUMERIC_TYPE(bobj);

    if (a_type != OBJ_NUM_FLOAT && b_type != OBJ_NUM_FLOAT) {

        const int64_t a = IS_SMALL_INT(aobj) ? GET_SMALL_INT(aobj)
                                             : *OBJPTR(INTEGER, aobj);
        const int64_t b = IS_SMALL_INT(bobj) ? GET_SMALL_INT(bobj)
                                             : *OBJPTR(INTEGER, bobj);

        ret = _compare_integer(instr, a, b);
    }
    else {

        const double a = (a_type == OBJ_NUM_FLOAT)
                            ? *OBJPTR(FLOAT, aobj)
                            : (a_type == OBJ_NUM_INTEGER)
                                ? *OBJPTR(INTEGER, aobj)
                                : GET_SMALL_INT(aobj);

        const double b = (b_type == OBJ_NUM_FLOAT)
                            ? *OBJPTR(FLOAT, bobj)
                            : (b_type == OBJ_NUM_INTEGER)
                                ? *OBJPTR(INTEGER, bobj)
                                : GET_SMALL_INT(bobj);

        switch (instr) {

            case INSTR_CMP_EQ:
                /* fall through */
            default:
                assert(instr == INSTR_CMP_EQ);
                ret = a == b;
                break;

            case INSTR_CMP_NE:
                ret = a != b;
                break;

            case INSTR_CMP_LT:
                ret = a < b;
                break;

            case INSTR_CMP_LE:
                ret = a <= b;
                break;
        }
    }

    return ret;
}

static int _compare_string(KOS_BYTECODE_INSTR instr,
                           KOS_OBJ_ID         aobj,
                           KOS_OBJ_ID         bobj)
{
    int ret;
    int str_cmp;

    assert(GET_OBJ_TYPE(aobj) == OBJ_STRING);
    assert(GET_OBJ_TYPE(bobj) == OBJ_STRING);

    if (instr == INSTR_CMP_EQ) {
        if (KOS_string_get_hash(aobj) != KOS_string_get_hash(bobj))
            return 0;
    }
    else if (instr == INSTR_CMP_NE) {
        if (KOS_string_get_hash(aobj) != KOS_string_get_hash(bobj))
            return 1;
    }

    str_cmp = KOS_string_compare(aobj, bobj);

    switch (instr) {

        case INSTR_CMP_EQ:
            /* fall through */
        default:
            assert(instr == INSTR_CMP_EQ);
            ret = ! str_cmp;
            break;

        case INSTR_CMP_NE:
            ret = !! str_cmp;
            break;

        case INSTR_CMP_LE:
            ret = str_cmp <= 0;
            break;

        case INSTR_CMP_LT:
            ret = str_cmp < 0;
            break;
    }

    return ret;
}

static int _init_registers(KOS_FRAME     stack_frame,
                           KOS_FUNCTION *func,
                           KOS_OBJ_ID    regs,
                           KOS_OBJ_ID    args_obj,
                           KOS_OBJ_ID    this_obj,
                           KOS_OBJ_ID    closures)
{
    int error = KOS_SUCCESS;

    KOS_ATOMIC(KOS_OBJ_ID) *const new_regs = _KOS_get_array_buffer(OBJPTR(ARRAY, regs));

    uint32_t reg = func->args_reg;

    assert(func->num_regs >= reg + 2); /* args, this */
    assert(func->num_regs == KOS_get_array_size(regs));

    new_regs[reg++] = args_obj;
    new_regs[reg++] = this_obj;

    if (GET_OBJ_TYPE(closures) == OBJ_ARRAY) {
        uint32_t src_len;
        uint32_t i;

        src_len = KOS_get_array_size(closures);

        assert(reg + src_len <= 256U);
        assert(reg + src_len <= KOS_get_array_size(regs));

        for (i=0; i < src_len; i++) {
            KOS_OBJ_ID obj = KOS_array_read(stack_frame, closures, (int)i);
            TRY_OBJID(obj);
            new_regs[reg++] = obj;
        }
    }

_error:
    return error;
}

static KOS_FRAME _prepare_call(KOS_FRAME          frame,
                               KOS_BYTECODE_INSTR instr,
                               KOS_OBJ_ID         func_obj,
                               KOS_OBJ_ID        *this_obj,
                               KOS_OBJ_ID         args_obj)
{
    int                      error           = KOS_SUCCESS;
    KOS_FUNCTION            *func;
    enum _KOS_FUNCTION_STATE state;
    KOS_FRAME                new_stack_frame = 0;

    assert( ! IS_BAD_PTR(func_obj));
    assert( ! IS_BAD_PTR(args_obj));

    if (GET_OBJ_TYPE(func_obj) != OBJ_FUNCTION)
        RAISE_EXCEPTION(str_err_not_callable);

    if (GET_OBJ_TYPE(args_obj) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_args_not_array);

    func  = OBJPTR(FUNCTION, func_obj);
    state = (enum _KOS_FUNCTION_STATE)func->state;

    if (KOS_get_array_size(args_obj) < func->min_args)
        RAISE_EXCEPTION(str_err_too_few_args);

    if (instr == INSTR_CALL_GEN && state < KOS_GEN_READY)
        RAISE_EXCEPTION(str_err_not_generator);

    switch (state) {

        /* Constructor function */
        case KOS_CTOR:
            if (*this_obj == NEW_THIS) {
                const KOS_OBJ_ID proto_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(func->prototype);
                assert( ! IS_BAD_PTR(proto_obj));

                if (func->handler)
                    *this_obj = proto_obj;
                else {
                    *this_obj = KOS_new_object_with_prototype(frame, proto_obj);
                    TRY_OBJID(*this_obj);
                }
            }
            /* fall through */

        /* Regular function */
        case KOS_FUN: {
            new_stack_frame = _KOS_stack_frame_push_func(frame, func);
            if ( ! new_stack_frame)
                RAISE_ERROR(KOS_ERROR_EXCEPTION);

            if ( ! func->handler)
                TRY(_init_registers(new_stack_frame,
                                    func,
                                    new_stack_frame->registers,
                                    args_obj,
                                    *this_obj,
                                    func->closures));

            break;
        }

        /* Instantiate a generator function */
        case KOS_GEN_INIT: {
            KOS_FUNCTION    *dest;
            KOS_OBJ_ID       ret;
            const KOS_OBJ_ID proto_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(func->prototype);

            assert( ! IS_BAD_PTR(proto_obj));

            ret = KOS_new_function(frame, proto_obj);
            TRY_OBJID(ret);

            dest = OBJPTR(FUNCTION, ret);

            dest->min_args   = 0;
            dest->num_regs   = func->num_regs;
            dest->instr_offs = func->instr_offs;
            dest->closures   = func->closures;
            dest->module     = func->module;
            dest->handler    = func->handler;
            dest->state      = KOS_GEN_READY;

            new_stack_frame = _KOS_stack_frame_push_func(frame, func);
            if ( ! new_stack_frame)
                RAISE_ERROR(KOS_ERROR_EXCEPTION);

            if (func->handler)
                new_stack_frame->registers = args_obj;
            else
                TRY(_init_registers(frame,
                                    dest,
                                    new_stack_frame->registers,
                                    args_obj,
                                    *this_obj,
                                    func->closures));

            dest->generator_stack_frame = new_stack_frame;
            new_stack_frame->parent     = 0;
            new_stack_frame->yield_reg  = KOS_CAN_YIELD;

            *this_obj = ret;
            break;
        }

        /* Generator function */
        case KOS_GEN_READY:
            /* fall through */
        case KOS_GEN_ACTIVE: {
            KOS_ATOMIC(KOS_OBJ_ID) *gen_regs = 0;
            const uint32_t          num_args = KOS_get_array_size(args_obj);

            assert(func->generator_stack_frame);

            new_stack_frame = func->generator_stack_frame;

            if ( ! func->handler)
                gen_regs = _KOS_get_array_buffer(OBJPTR(ARRAY, new_stack_frame->registers));
            else
                *this_obj = new_stack_frame->registers;

            if (state == KOS_GEN_ACTIVE) {

                const uint32_t r = new_stack_frame->yield_reg;

                assert( ! func->handler);

                assert(r < KOS_get_array_size(new_stack_frame->registers));

                assert(gen_regs);

                if (gen_regs) {
                    if (num_args)
                        gen_regs[r] = KOS_array_read(frame, args_obj, 0);
                    else
                        gen_regs[r] = KOS_VOID;
                }
            }

            /* TODO perform CAS for thread safety */
            func->state = KOS_GEN_RUNNING;

            new_stack_frame->parent    = frame;
            new_stack_frame->yield_reg = KOS_CAN_YIELD;
            break;
        }

        case KOS_GEN_RUNNING:
            RAISE_EXCEPTION(str_err_generator_running);

        default:
            assert(state == KOS_GEN_DONE);
            RAISE_EXCEPTION(str_err_generator_end);
    }

_error:
    return error ? 0 : new_stack_frame;
}

static KOS_OBJ_ID _finish_call(KOS_FRAME                 frame,
                               KOS_BYTECODE_INSTR        instr,
                               KOS_FUNCTION             *func,
                               KOS_OBJ_ID                this_obj,
                               KOS_FRAME                 new_stack_frame,
                               enum _KOS_FUNCTION_STATE *state)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert(new_stack_frame->parent == frame);

    if ( ! KOS_is_exception_pending(new_stack_frame)) {

        if (*state == KOS_CTOR && ! func->handler)
            ret = this_obj;
        else
            ret = new_stack_frame->retval;

        if (*state >= KOS_GEN_INIT) {
            if (new_stack_frame->yield_reg == KOS_CAN_YIELD) {
                *state  = KOS_GEN_DONE;
                func->state = KOS_GEN_DONE;
                if (instr != INSTR_CALL_GEN) {
                    if (IS_BAD_PTR(new_stack_frame->retval))
                        KOS_raise_exception_cstring(frame, str_err_generator_end);
                    else
                        KOS_raise_exception(frame, new_stack_frame->retval);
                }
            }
            else {
                const enum _KOS_FUNCTION_STATE end_state = func->handler ? KOS_GEN_READY : KOS_GEN_ACTIVE;

                *state      = end_state;
                func->state = (uint8_t)end_state;
            }
        }
    }
    else {
        if (*state >= KOS_GEN_INIT) {
            *state  = KOS_GEN_DONE;
            func->state = KOS_GEN_DONE;
        }
        frame->exception = new_stack_frame->exception;
    }

    new_stack_frame->parent = 0;

    return ret;
}

static KOS_OBJ_ID _read_buffer(KOS_FRAME frame, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    size = KOS_get_buffer_size(objptr);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception_cstring(frame, str_err_invalid_index);
        ret = KOS_VOID;
    }
    else {
        uint8_t *const buf = KOS_buffer_data(objptr);
        ret = TO_SMALL_INT((int)buf[idx]);
    }

    return ret;
}

static int _write_buffer(KOS_FRAME frame, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error;
    uint32_t size;
    int64_t  byte_value;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    TRY(KOS_get_integer(frame, value, &byte_value));

    if (byte_value < 0 || byte_value > 255)
        RAISE_EXCEPTION(str_err_invalid_byte_value);

    size = KOS_get_buffer_size(objptr);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)
        RAISE_EXCEPTION(str_err_invalid_index);
    else {
        uint8_t *const buf = KOS_buffer_data(objptr);
        buf[idx] = (uint8_t)byte_value;
    }

_error:
    return error;
}

static uint32_t _load_32(const uint8_t *bytecode)
{
    return (uint32_t)bytecode[0] +
           ((uint32_t)bytecode[1] << 8) +
           ((uint32_t)bytecode[2] << 16) +
           ((uint32_t)bytecode[3] << 24);
}

static int _exec_function(KOS_FRAME frame)
{
    KOS_ARRAY              *regs_array = OBJPTR(ARRAY, frame->registers);
    KOS_ATOMIC(KOS_OBJ_ID) *regs       = _KOS_get_array_buffer(regs_array);
    KOS_MODULE             *module     = frame->module;
    int                     error      = KOS_SUCCESS;
    const uint8_t          *bytecode;

    assert(module);
    assert(module->context);
    bytecode = module->bytecode + frame->instr_offs;

    for (;;) { /* Exit condition at the end of the loop */

        const KOS_BYTECODE_INSTR instr = (KOS_BYTECODE_INSTR)*bytecode;
        int32_t                  delta = 1;
        KOS_OBJ_ID               out   = KOS_BADPTR;
        unsigned                 rdest = 0;

        switch (instr) {

            case INSTR_BREAKPOINT:
                /* TODO */
                break;

            case INSTR_LOAD_INT8: { /* <r.dest>, <int8> */
                const int8_t value = (int8_t)bytecode[2];

                rdest = bytecode[1];
                out   = TO_SMALL_INT(value);
                delta = 3;
                break;
            }

            case INSTR_LOAD_INT32: { /* <r.dest>, <int32> */
                const int32_t value = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_new_int(frame, value);
                delta = 6;
                break;
            }

            case INSTR_LOAD_INT64: { /* <r.dest>, <low.uint32>, <high.int32> */
                const uint32_t low  = _load_32(bytecode+2);
                const uint32_t high = _load_32(bytecode+6);

                rdest = bytecode[1];
                out   = KOS_new_int(frame, (int64_t)(((uint64_t)high << 32) | low));
                delta = 10;
                break;
            }

            case INSTR_LOAD_FLOAT: { /* <r.dest>, <low.uint32>, <high.uint32> */
                const uint32_t low  = _load_32(bytecode+2);
                const uint32_t high = _load_32(bytecode+6);

                union {
                    uint64_t ui64;
                    double   d;
                } int2double;

                int2double.ui64 = ((uint64_t)high << 32) | low;

                rdest = bytecode[1];
                out   = KOS_new_float(frame, int2double.d);
                delta = 10;
                break;
            }

            case INSTR_LOAD_STR: { /* <r.dest>, <str.idx.int32> */
                const int32_t idx = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = _make_string(frame, module, idx);
                delta = 6;
                break;
            }

            case INSTR_LOAD_TRUE: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_TRUE;
                delta = 2;
                break;
            }

            case INSTR_LOAD_FALSE: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_FALSE;
                delta = 2;
                break;
            }

            case INSTR_LOAD_VOID: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_VOID;
                delta = 2;
                break;
            }

            case INSTR_LOAD_FUN: /* <r.dest>, <delta.int32>, <min.args>, <num.regs>, <args.reg> */
                /* fall through */
            case INSTR_LOAD_GEN: /* <r.dest>, <delta.int32>, <min.args>, <num.regs>, <args.reg> */
                /* fall through */
            case INSTR_LOAD_CTOR: { /* <r.dest>, <delta.int32>, <min.args>, <num.regs>, <args.reg> */
                const int32_t  fun_offs  = (int32_t)_load_32(bytecode+2);
                const uint8_t  min_args  = bytecode[6];
                const uint8_t  num_regs  = bytecode[7];
                const uint8_t  args_reg  = bytecode[8];

                const uint32_t offset    = (uint32_t)((bytecode + 9 + fun_offs) - module->bytecode);
                KOS_OBJ_ID     fun_obj   = KOS_BADPTR;
                KOS_OBJ_ID     proto_obj = KOS_VOID;

                assert(offset < module->bytecode_size);

                if (instr == INSTR_LOAD_CTOR)
                    proto_obj = KOS_gen_prototype(frame, bytecode + 9 + fun_offs);

                if ( ! IS_BAD_PTR(proto_obj))
                    fun_obj = KOS_new_function(frame, proto_obj);

                if ( ! IS_BAD_PTR(fun_obj)) {

                    KOS_FUNCTION *const fun = OBJPTR(FUNCTION, fun_obj);

                    fun->min_args   = min_args;
                    fun->num_regs   = num_regs;
                    fun->args_reg   = args_reg;
                    fun->instr_offs = offset;
                    fun->module     = module;

                    if (instr == INSTR_LOAD_GEN)
                        fun->state = KOS_GEN_INIT;
                    else if (instr == INSTR_LOAD_CTOR)
                        fun->state = KOS_CTOR;
                }

                rdest = bytecode[1];
                out   = fun_obj;
                delta = 9;
                break;
            }

            case INSTR_LOAD_ARRAY8: { /* <r.dest>, <size.int8> */
                const uint8_t size = bytecode[2];

                rdest = bytecode[1];
                out   = KOS_new_array(frame, size);
                delta = 3;
                break;
            }

            case INSTR_LOAD_ARRAY: { /* <r.dest>, <size.int32> */
                const uint32_t size = _load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_new_array(frame, size);
                delta = 6;
                break;
            }

            case INSTR_LOAD_OBJ: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_new_object(frame);
                delta = 2;
                break;
            }

            case INSTR_MOVE: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];
                out   = regs[rsrc];
                delta = 3;
                break;
            }

            case INSTR_GET_GLOBAL: { /* <r.dest>, <int32> */
                const int32_t  idx = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_array_read(frame, module->globals, idx);
                delta = 6;
                break;
            }

            case INSTR_SET_GLOBAL: { /* <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->size);

                error = KOS_array_write(frame, module->globals, idx, regs[rsrc]);
                delta = 6;
                break;
            }

            case INSTR_GET_MOD: { /* <r.dest>, <int32>, <r.glob> */
                const int      mod_idx    = (int32_t)_load_32(bytecode+2);
                const unsigned rglob      = bytecode[6];
                KOS_OBJ_ID     module_obj = KOS_array_read(frame, module->context->modules, mod_idx);

                assert(rglob < regs_array->size);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {

                    KOS_OBJ_ID glob_idx;

                    assert(GET_OBJ_SUBTYPE(module_obj) == OBJ_MODULE);

                    glob_idx = KOS_get_property(frame, OBJPTR(MODULE, module_obj)->global_names, regs[rglob]);

                    if (!IS_BAD_PTR(glob_idx)) {

                        assert(IS_SMALL_INT(glob_idx));

                        out = KOS_array_read(frame, OBJPTR(MODULE, module_obj)->globals,
                                             (int)GET_SMALL_INT(glob_idx));
                    }
                }

                delta = 7;
                break;
            }

            case INSTR_GET_MOD_ELEM: { /* <r.dest>, <int32>, <int32> */
                const int  mod_idx    = (int32_t)_load_32(bytecode+2);
                const int  glob_idx   = (int32_t)_load_32(bytecode+6);
                KOS_OBJ_ID module_obj = KOS_array_read(frame, module->context->modules, mod_idx);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {
                    assert(GET_OBJ_SUBTYPE(module_obj) == OBJ_MODULE);

                    out = KOS_array_read(frame, OBJPTR(MODULE, module_obj)->globals, glob_idx);
                }

                delta = 10;
                break;
            }

            case INSTR_GET: { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     prop;

                assert(rsrc  < regs_array->size);
                assert(rprop < regs_array->size);

                rdest = bytecode[1];
                src   = regs[rsrc];
                prop  = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(frame, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception_cstring(frame, str_err_invalid_index);
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        const enum KOS_OBJECT_TAG type = GET_OBJ_TYPE(src);
                        if (type == OBJ_STRING)
                            out = KOS_string_get_char(frame, src, (int)idx);
                        else if (type == OBJ_BUFFER)
                            out = _read_buffer(frame, src, (int)idx);
                        else
                            out = KOS_array_read(frame, src, (int)idx);
                    }
                }
                else {
                    KOS_OBJ_ID value = KOS_get_property(frame, src, prop);

                    if (GET_OBJ_SUBTYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args;
                        frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        value = OBJPTR(DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(frame, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(frame, value, src, args);
                            if (IS_BAD_PTR(value))
                                error = KOS_ERROR_EXCEPTION;
                        }
                    }

                    if ( ! error && ! IS_BAD_PTR(value))
                        out = value;
                }

                delta = 4;
                break;
            }

            case INSTR_GET_ELEM: { /* <r.dest>, <r.src>, <int32> */
                const unsigned      rsrc = bytecode[2];
                const int32_t       idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID          src;
                enum KOS_OBJECT_TAG type;

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];
                src   = regs[rsrc];

                type = GET_OBJ_TYPE(src);

                if (type == OBJ_STRING)
                    out = KOS_string_get_char(frame, src, idx);
                else if (type == OBJ_BUFFER)
                    out = _read_buffer(frame, src, idx);
                else
                    out = KOS_array_read(frame, src, idx);

                delta = 7;
                break;
            }

            case INSTR_GET_RANGE: { /* <r.dest>, <r.src>, <r.begin>, <r.end> */
                const unsigned rsrc   = bytecode[2];
                const unsigned rbegin = bytecode[3];
                const unsigned rend   = bytecode[4];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     begin;
                KOS_OBJ_ID     end;
                int64_t        begin_idx;
                int64_t        end_idx = 0;

                assert(rsrc   < regs_array->size);
                assert(rbegin < regs_array->size);
                assert(rend   < regs_array->size);

                rdest = bytecode[1];
                src   = regs[rsrc];
                begin = regs[rbegin];
                end   = regs[rend];

                if (begin != KOS_VOID)
                    error = KOS_get_integer(frame, begin, &begin_idx);
                else
                    begin_idx = 0;

                if ( ! error) {
                    if (end != KOS_VOID)
                        error = KOS_get_integer(frame, end, &end_idx);
                    else
                        end_idx = MAX_INT64;
                }

                if ( ! error) {
                    if (GET_OBJ_TYPE(src) == OBJ_STRING)
                        out = KOS_string_slice(frame, src, begin_idx, end_idx);
                    else if (GET_OBJ_TYPE(src) == OBJ_BUFFER)
                        out = KOS_buffer_slice(frame, src, begin_idx, end_idx);
                    else
                        out = KOS_array_slice(frame, src, begin_idx, end_idx);
                }

                delta = 5;
                break;
            }

            case INSTR_GET_PROP: { /* <r.dest>, <r.src>, <str.idx.int32> */
                const unsigned rsrc = bytecode[2];
                const int32_t  idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID     prop;

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];
                prop  = _make_string(frame, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_ID obj   = regs[rsrc];
                    KOS_OBJ_ID value = KOS_get_property(frame, obj, prop);

                    if (GET_OBJ_SUBTYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args;
                        frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        value = OBJPTR(DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(frame, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(frame, value, obj, args);
                            if (IS_BAD_PTR(value))
                                error = KOS_ERROR_EXCEPTION;
                        }
                    }

                    if ( ! error && ! IS_BAD_PTR(value))
                        out = value;
                }

                delta = 7;
                break;
            }

            case INSTR_SET: { /* <r.dest>, <r.prop>, <r.src> */
                const unsigned rprop = bytecode[2];
                const unsigned rsrc  = bytecode[3];
                KOS_OBJ_ID     prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->size);
                assert(rprop < regs_array->size);
                assert(rsrc  < regs_array->size);

                prop = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(frame, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception_cstring(frame, str_err_invalid_index);
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        const KOS_OBJ_ID obj = regs[rdest];
                        if (GET_OBJ_TYPE(obj) == OBJ_BUFFER)
                            error = _write_buffer(frame, obj, (int)idx, regs[rsrc]);
                        else
                            error = KOS_array_write(frame, obj, (int)idx, regs[rsrc]);
                    }
                }
                else {
                    KOS_OBJ_ID obj   = regs[rdest];
                    KOS_OBJ_ID value = regs[rsrc];

                    error = KOS_set_property(frame, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID setter;
                        KOS_OBJ_ID args;

                        assert(KOS_is_exception_pending(frame));
                        setter = KOS_get_exception(frame);
                        KOS_clear_exception(frame);

                        assert(GET_OBJ_SUBTYPE(setter) == OBJ_DYNAMIC_PROP);
                        frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        setter = OBJPTR(DYNAMIC_PROP, setter)->setter;

                        args  = KOS_new_array(frame, 1);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_array_write(frame, args, 0, value);
                            assert( ! error);
                            value = KOS_call_function(frame, setter, obj, args);
                            if (IS_BAD_PTR(value))
                                error = KOS_ERROR_EXCEPTION;
                        }
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SET_ELEM: { /* <r.dest>, <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+2);
                const unsigned rsrc = bytecode[6];
                KOS_OBJ_ID     dest;

                rdest = bytecode[1];

                assert(rdest < regs_array->size);
                assert(rsrc  < regs_array->size);

                dest = regs[rdest];

                if (GET_OBJ_TYPE(dest) == OBJ_BUFFER)
                    error = _write_buffer(frame, dest, idx, regs[rsrc]);
                else
                    error = KOS_array_write(frame, dest, idx, regs[rsrc]);

                delta = 7;
                break;
            }

            case INSTR_SET_PROP: { /* <r.dest>, <str.idx.int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+2);
                const unsigned rsrc = bytecode[6];
                KOS_OBJ_ID     prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->size);
                assert(rsrc  < regs_array->size);

                prop = _make_string(frame, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_ID obj   = regs[rdest];
                    KOS_OBJ_ID value = regs[rsrc];

                    error = KOS_set_property(frame, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID setter;
                        KOS_OBJ_ID args;

                        assert(KOS_is_exception_pending(frame));
                        setter = KOS_get_exception(frame);
                        KOS_clear_exception(frame);

                        assert(GET_OBJ_SUBTYPE(setter) == OBJ_DYNAMIC_PROP);
                        frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        setter = OBJPTR(DYNAMIC_PROP, setter)->setter;

                        args  = KOS_new_array(frame, 1);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_array_write(frame, args, 0, value);
                            assert( ! error);
                            value = KOS_call_function(frame, setter, obj, args);
                            if (IS_BAD_PTR(value))
                                error = KOS_ERROR_EXCEPTION;
                        }
                    }
                }

                delta = 7;
                break;
            }

            case INSTR_DEL: { /* <r.dest>, <r.prop> */
                const unsigned rprop = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < regs_array->size);
                assert(rprop < regs_array->size);

                KOS_delete_property(frame, regs[rdest], regs[rprop]);

                delta = 3;
                break;
            }

            case INSTR_DEL_PROP: { /* <r.dest>, <str.idx.int32> */
                const int32_t idx = (int32_t)_load_32(bytecode+2);
                KOS_OBJ_ID    prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->size);

                prop = _make_string(frame, module, idx);

                if (!IS_BAD_PTR(prop))
                    KOS_delete_property(frame, regs[rdest], prop);

                delta = 6;
                break;
            }

            case INSTR_ADD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _add_integer(frame, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER:
                            /* fall through */
                        case OBJ_INTEGER2:
                            out = _add_integer(frame, *OBJPTR(INTEGER, src1), src2);
                            break;

                        case OBJ_FLOAT:
                            /* fall through */
                        case OBJ_FLOAT2:
                            out = _add_float(frame, *OBJPTR(FLOAT, src1), src2);
                            break;

                        case OBJ_STRING: {
                            if (GET_OBJ_TYPE(src2) == OBJ_STRING)
                                out = KOS_string_add(frame, src1, src2);
                            else
                                KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
                            break;
                        }

                        default:
                            KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SUB: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_NUMERIC_OBJ(src1)) {

                    switch (GET_NUMERIC_TYPE(src1)) {

                        default:
                            out = _sub_integer(frame, GET_SMALL_INT(src1), src2);
                            break;

                        case OBJ_NUM_INTEGER:
                            out = _sub_integer(frame, *OBJPTR(INTEGER, src1), src2);
                            break;

                        case OBJ_NUM_FLOAT:
                            out = _sub_float(frame, *OBJPTR(FLOAT, src1), src2);
                            break;
                    }
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);

                delta = 4;
                break;
            }

            case INSTR_MUL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_NUMERIC_OBJ(src1)) {

                    switch (GET_NUMERIC_TYPE(src1)) {

                        default:
                            out = _mul_integer(frame, GET_SMALL_INT(src1), src2);
                            break;

                        case OBJ_NUM_INTEGER:
                            out = _mul_integer(frame, *OBJPTR(INTEGER, src1), src2);
                            break;

                        case OBJ_NUM_FLOAT:
                            out = _mul_float(frame, *OBJPTR(FLOAT, src1), src2);
                            break;
                    }
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);

                delta = 4;
                break;
            }

            case INSTR_DIV: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_NUMERIC_OBJ(src1)) {

                    switch (GET_NUMERIC_TYPE(src1)) {

                        default:
                            out = _div_integer(frame, GET_SMALL_INT(src1), src2);
                            break;

                        case OBJ_NUM_INTEGER:
                            out = _div_integer(frame, *OBJPTR(INTEGER, src1), src2);
                            break;

                        case OBJ_NUM_FLOAT:
                            out = _div_float(frame, *OBJPTR(FLOAT, src1), src2);
                            break;
                    }
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);


                delta = 4;
                break;
            }

            case INSTR_MOD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_NUMERIC_OBJ(src1)) {

                    switch (GET_NUMERIC_TYPE(src1)) {

                        default:
                            out = _mod_integer(frame, GET_SMALL_INT(src1), src2);
                            break;

                        case OBJ_NUM_INTEGER:
                            out = _mod_integer(frame, *OBJPTR(INTEGER, src1), src2);
                            break;

                        case OBJ_NUM_FLOAT:
                            out = _mod_float(frame, *OBJPTR(FLOAT, src1), src2);
                            break;
                    }
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_unsup_operand_types);

                delta = 4;
                break;
            }

            case INSTR_SHL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b < 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(frame, a >> -b);
                        else
                            out = KOS_new_int(frame, a << b);
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SHR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b > 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(frame, a << -b);
                        else
                            out = KOS_new_int(frame, a >> b);
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SHRU: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(0);
                        else if (b < 0)
                            out = KOS_new_int(frame, a << -b);
                        else
                            out = KOS_new_int(frame, (int64_t)((uint64_t)a >> b));
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_NOT: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];
                int64_t        a;

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc], &a);

                if (!error)
                    out = KOS_new_int(frame, ~a);

                delta = 3;
                break;
            }

            case INSTR_AND: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(frame, a & b);
                }

                delta = 4;
                break;
            }

            case INSTR_OR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(frame, a | b);
                }

                delta = 4;
                break;
            }

            case INSTR_XOR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];

                error = KOS_get_integer(frame, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(frame, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(frame, a ^ b);
                }

                delta = 4;
                break;
            }

            case INSTR_TYPE: { /* <r.dest>, <r.src> */
                static const char t_integer[]  = "integer";
                static const char t_float[]    = "float";
                static const char t_string[]   = "string";
                static const char t_boolean[]  = "boolean";
                static const char t_void[]     = "void";
                static const char t_object[]   = "object";
                static const char t_array[]    = "array";
                static const char t_buffer[]   = "buffer";
                static const char t_function[] = "function";

                const unsigned rsrc  = bytecode[2];
                KOS_OBJ_ID     src;

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];
                src   = regs[rsrc];

                assert(!IS_BAD_PTR(src));

                if (IS_SMALL_INT(src))
                    out = KOS_context_get_cstring(frame, t_integer);

                else switch (GET_OBJ_TYPE(src)) {
                    case OBJ_INTEGER:
                        /* fall through */
                    case OBJ_INTEGER2:
                        out = KOS_context_get_cstring(frame, t_integer);
                        break;

                    case OBJ_FLOAT:
                        /* fall through */
                    case OBJ_FLOAT2:
                        out = KOS_context_get_cstring(frame, t_float);
                        break;

                    case OBJ_STRING:
                        out = KOS_context_get_cstring(frame, t_string);
                        break;

                    case OBJ_IMMEDIATE:
                        if (src == KOS_VOID)
                            out = KOS_context_get_cstring(frame, t_void);
                        else {
                            assert(src == KOS_TRUE || src == KOS_FALSE);
                            out = KOS_context_get_cstring(frame, t_boolean);
                        }
                        break;

                    case OBJ_ARRAY:
                        out = KOS_context_get_cstring(frame, t_array);
                        break;

                    case OBJ_BUFFER:
                        out = KOS_context_get_cstring(frame, t_buffer);
                        break;

                    case OBJ_FUNCTION:
                        out = KOS_context_get_cstring(frame, t_function);
                        break;

                    default:
                        out = KOS_context_get_cstring(frame, t_object);
                        break;
                }

                delta = 3;
                break;
            }

            case INSTR_CMP_EQ: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_NE: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_LE: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_LT: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int            ret;
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                enum KOS_OBJECT_TAG src1_type;
                enum KOS_OBJECT_TAG src2_type;

                assert(rsrc1 < regs_array->size);
                assert(rsrc2 < regs_array->size);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                src1_type = IS_NUMERIC_OBJ(src1) ? OBJ_INTEGER : GET_OBJ_TYPE(src1);
                src2_type = IS_NUMERIC_OBJ(src2) ? OBJ_INTEGER : GET_OBJ_TYPE(src2);

                if (src1_type == src2_type)
                    switch (src1_type) {

                        case OBJ_INTEGER:
                            ret = _compare_float(instr, src1, src2);
                            break;

                        case OBJ_STRING:
                            ret = _compare_string(instr, src1, src2);
                            break;

                        default:
                            ret = _compare_integer(instr, (int64_t)(intptr_t)src1, (int64_t)(intptr_t)src2);
                            break;
                    }
                else
                    ret = _compare_integer(instr, src1_type, src2_type);

                out   = KOS_BOOL(ret);
                delta = 4;
                break;
            }

            case INSTR_HAS: { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                KOS_OBJ_ID obj;

                assert(rsrc  < regs_array->size);
                assert(rprop < regs_array->size);

                rdest = bytecode[1];

                obj = KOS_get_property(frame, regs[rsrc], regs[rprop]);
                KOS_clear_exception(frame);

                out   = KOS_BOOL(!IS_BAD_PTR(obj));
                delta = 4;
                break;
            }

            case INSTR_HAS_PROP: { /* <r.dest>, <r.src>, <str.idx.int32> */
                const unsigned rsrc  = bytecode[2];
                const int32_t  idx   = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID     prop;

                assert(rsrc  < regs_array->size);

                rdest = bytecode[1];

                prop = _make_string(frame, module, idx);

                if (!IS_BAD_PTR(prop)) {

                    KOS_OBJ_ID obj = KOS_get_property(frame, regs[rsrc], prop);
                    KOS_clear_exception(frame);

                    out = KOS_BOOL(!IS_BAD_PTR(obj));
                }

                delta = 7;
                break;
            }

            case INSTR_INSTANCEOF: { /* <r.dest>, <r.src>, <r.func> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rfunc = bytecode[3];

                KOS_OBJ_ID constr_obj;
                KOS_OBJ_ID proto_obj      = KOS_BADPTR;
                KOS_OBJ_ID ret            = KOS_FALSE;
                int        constr_is_func = 1;

                assert(rsrc  < regs_array->size);
                assert(rfunc < regs_array->size);

                rdest      = bytecode[1];
                constr_obj = regs[rfunc];

                if (GET_OBJ_TYPE(constr_obj) == OBJ_FUNCTION) {
                    KOS_FUNCTION *const constr = OBJPTR(FUNCTION, constr_obj);
                    proto_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(constr->prototype);
                    assert( ! IS_BAD_PTR(proto_obj));
                }
                else
                    constr_is_func = 0;

                assert(GET_OBJ_TYPE(proto_obj) != OBJ_INTERNAL);

                if ( ! IS_BAD_PTR(proto_obj)) {
                    KOS_OBJ_ID obj = regs[rsrc];
                    do {
                        obj = KOS_get_prototype(frame, obj);
                        if (obj == proto_obj) {
                            ret = KOS_TRUE;
                            break;
                        }
                    }
                    while (!IS_BAD_PTR(obj));
                }
                else
                    if (constr_is_func)
                        KOS_clear_exception(frame);

                out   = ret;
                delta = 4;
                break;
            }

            case INSTR_JUMP: { /* <delta.int32> */
                delta = 5 + (int32_t)_load_32(bytecode+1);
                break;
            }

            case INSTR_JUMP_COND: { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->size);

                delta = 6;

                if (_KOS_is_truthy(regs[rsrc]))
                    delta += offs;
                break;
            }

            case INSTR_JUMP_NOT_COND: { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->size);

                delta = 6;

                if ( ! _KOS_is_truthy(regs[rsrc]))
                    delta += offs;
                break;
            }

            case INSTR_BIND_SELF: /* <r.dest>, <slot.idx.uint8> */
                /* fall through */
            case INSTR_BIND: { /* <r.dest>, <slot.idx.uint8>, <r.src> */
                const unsigned idx = bytecode[2];

                KOS_OBJ_ID dest;
                rdest = bytecode[1];
                assert(rdest < regs_array->size);
                dest = regs[rdest];

                if (GET_OBJ_TYPE(dest) != OBJ_FUNCTION)
                    KOS_raise_exception_cstring(frame, str_err_not_callable);
                else {
                    KOS_OBJ_ID closures = OBJPTR(FUNCTION, dest)->closures;
                    KOS_OBJ_ID regs_obj;

                    if (instr == INSTR_BIND) {
                        const unsigned rsrc = bytecode[3];
                        assert(rsrc < regs_array->size);
                        regs_obj = regs[rsrc];
                    }
                    else
                        regs_obj = frame->registers;

                    assert(closures == KOS_VOID ||
                           GET_OBJ_TYPE(closures) == OBJ_ARRAY);

                    if (closures == KOS_VOID) {
                        closures = KOS_new_array(frame, idx+1);
                        if (IS_BAD_PTR(closures))
                            error = KOS_ERROR_EXCEPTION;
                        else
                            OBJPTR(FUNCTION, dest)->closures = closures;
                    }
                    else if (idx >= KOS_get_array_size(closures))
                        error = KOS_array_resize(frame, closures, idx+1);

                    if (!error)
                        error = KOS_array_write(frame, closures, (int)idx, regs_obj);
                }

                delta = (instr == INSTR_BIND_SELF) ? 3 : 4;
                break;
            }

            case INSTR_TAIL_CALL: /* <closure.size.uint8>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_CALL: /* <r.dest>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_CALL_GEN: { /* <r.dest>, <r.func>, <r.final> */
                const unsigned rfunc = bytecode[2];
                unsigned       rthis = ~0U;
                unsigned       rargs = ~0U;

                KOS_OBJ_ID func_obj;
                KOS_OBJ_ID this_obj;
                KOS_OBJ_ID args_obj;

                KOS_FRAME new_stack_frame = 0;

                rdest = bytecode[1];

                switch (instr) {

                    case INSTR_CALL_GEN:
                        rthis = bytecode[3];
                        assert(rthis < regs_array->size);

                        this_obj = regs[rthis];
                        assert( ! IS_BAD_PTR(this_obj));
                        break;

                    default:
                        rthis = bytecode[3];
                        rargs = bytecode[4];
                        assert(rthis < regs_array->size);

                        this_obj = regs[rthis];
                        assert( ! IS_BAD_PTR(this_obj));
                        break;
                }

                assert(instr != INSTR_TAIL_CALL || rdest <= regs_array->size);
                assert(rfunc < regs_array->size);

                func_obj = regs[rfunc];

                if (instr == INSTR_CALL_GEN)
                    args_obj = KOS_new_array(frame, 0);
                else {
                    assert(rargs < regs_array->size);
                    args_obj = regs[rargs];
                }

                frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

                if (IS_BAD_PTR(args_obj))
                    error = KOS_ERROR_EXCEPTION;
                else {
                    if (GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION
                            && OBJPTR(FUNCTION, func_obj)->state == KOS_CTOR)
                        this_obj = NEW_THIS;
                    new_stack_frame = _prepare_call(frame, instr, func_obj, &this_obj, args_obj);
                    if ( ! new_stack_frame)
                        error = KOS_ERROR_EXCEPTION;
                }

                if ( ! error) {

                    KOS_FUNCTION *func = OBJPTR(FUNCTION, func_obj);

                    if (func->state == KOS_GEN_INIT)
                        out = this_obj;

                    else {
                        enum _KOS_FUNCTION_STATE state =
                                (enum _KOS_FUNCTION_STATE)func->state;

                        assert(new_stack_frame);

                        /* TODO optimize INSTR_TAIL_CALL */

                        if (func->handler)  {
                            const KOS_OBJ_ID ret_val = func->handler(new_stack_frame,
                                                                     this_obj,
                                                                     args_obj);

                            /* Avoid detecting as end of iterator in the code below */
                            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(ret_val))
                                new_stack_frame->yield_reg = 0;

                            new_stack_frame->retval = ret_val;

                            if (KOS_is_exception_pending(new_stack_frame)) {
                                assert(IS_BAD_PTR(ret_val));
                                error = KOS_ERROR_EXCEPTION;
                                _KOS_wrap_exception(new_stack_frame);
                            }
                            else {
                                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(ret_val));
                            }
                        }
                        else {
                            error = _exec_function(new_stack_frame);
                            assert( ! error || KOS_is_exception_pending(new_stack_frame));
                        }

                        out = _finish_call(frame, instr, func, this_obj, new_stack_frame, &state);

                        if (instr == INSTR_CALL_GEN && ! error) {
                            const KOS_OBJ_ID result = KOS_BOOL(state == KOS_GEN_DONE);
                            if (rthis == rdest)
                                out = result;
                            else {
                                assert(rthis < regs_array->size);
                                regs[rthis] = result;
                            }
                        }
                    }

                    if (instr == INSTR_TAIL_CALL && ! error) {
                        frame->retval    = out;
                        out              = KOS_BADPTR;
                        regs_array->size = rdest; /* closure size */
                        error            = KOS_SUCCESS_RETURN;
                    }
                }

                if (instr == INSTR_CALL)
                    delta = 5;
                else if (instr == INSTR_CALL_GEN)
                    delta = 4;
                break;
            }

            case INSTR_RETURN: { /* <closure.size.uint8>, <r.src> */
                const unsigned closure_size = bytecode[1];
                const unsigned rsrc         = bytecode[2];

                assert(closure_size <= regs_array->size);
                assert(rsrc         <  regs_array->size);

                frame->retval = regs[rsrc];

                regs_array->size = closure_size;

                error = KOS_SUCCESS_RETURN;
                break;
            }

            case INSTR_YIELD: { /* <r.src> */
                const unsigned rsrc = bytecode[1];

                assert(rsrc < regs_array->size);

                if (frame->yield_reg == KOS_CANNOT_YIELD)
                    KOS_raise_exception_cstring(frame, str_err_cannot_yield);
                else {
                    assert(frame->yield_reg == KOS_CAN_YIELD);

                    frame->retval    = regs[rsrc];
                    frame->yield_reg = (uint16_t)rsrc;

                    /* Move bytecode pointer here, because at the end of the loop
                       we test !error, but error is set to KOS_SUCCESS_RETURN */
                    bytecode += 2;

                    error =  KOS_SUCCESS_RETURN;
                }

                delta = 2;
                break;
            }

            case INSTR_THROW: { /* <r.src> */
                const unsigned rsrc = bytecode[1];

                assert(rsrc < regs_array->size);

                KOS_raise_exception(frame, regs[rsrc]);

                delta = 2;
                break;
            }

            case INSTR_CATCH: { /* <r.dest>, <delta.int32> */
                const int32_t  rel_offs = (int32_t)_load_32(bytecode+2);
                const uint32_t offset   = (uint32_t)((bytecode + 6 + rel_offs) - module->bytecode);

                rdest = bytecode[1];

                assert(rdest  < regs_array->size);
                assert(offset < module->bytecode_size);

                frame->catch_reg  = (uint8_t)rdest;
                frame->catch_offs = offset;

                delta = 6;
                break;
            }

            case INSTR_CANCEL:
                /* fall through */
            default: {
                assert(instr == INSTR_CANCEL);
                if (instr == INSTR_CANCEL) {
                    frame->catch_offs = KOS_NO_CATCH;
                    delta = 1;
                }
                else
                    KOS_raise_exception_cstring(frame, str_err_invalid_instruction);
                break;
            }
        }

        if ( ! KOS_is_exception_pending(frame)) {
            if ( ! IS_BAD_PTR(out)) {
                assert(rdest < regs_array->size);
                regs[rdest] = out;
            }
        }
        else {
            error = KOS_ERROR_EXCEPTION;

            frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

            _KOS_wrap_exception(frame);

            if (frame->catch_offs != KOS_NO_CATCH) {
                const unsigned rexc = frame->catch_reg;

                assert(rexc < regs_array->size);

                regs[rexc] = KOS_get_exception(frame);
                delta      = 0;
                bytecode   = module->bytecode + frame->catch_offs;
                error      = KOS_SUCCESS;

                frame->catch_offs = KOS_NO_CATCH;
                KOS_clear_exception(frame);
            }
        }

        if (error)
            break;

        bytecode += delta;

        assert((uint32_t)(bytecode - module->bytecode) < module->bytecode_size);
    }

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

    assert(!error || KOS_is_exception_pending(frame));

    frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

    return error;
}

KOS_OBJ_ID _KOS_call_function(KOS_FRAME             frame,
                              KOS_OBJ_ID            func_obj,
                              KOS_OBJ_ID            this_obj,
                              KOS_OBJ_ID            args_obj,
                              enum _KOS_CALL_FLAVOR call_flavor)
{
    int           error = KOS_SUCCESS;
    KOS_OBJ_ID    ret   = KOS_BADPTR;
    KOS_FUNCTION *func;
    KOS_FRAME     new_stack_frame;

    KOS_context_validate(frame);

    assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION);

    if (GET_OBJ_TYPE(func_obj) != OBJ_FUNCTION) {
        KOS_raise_exception_cstring(frame, str_err_not_function);
        return KOS_BADPTR;
    }

    func = OBJPTR(FUNCTION, func_obj);

    if (func->state == KOS_CTOR && call_flavor == KOS_CALL_FUNCTION)
        this_obj = NEW_THIS;

    new_stack_frame = _prepare_call(frame, INSTR_CALL, func_obj, &this_obj, args_obj);

    if ( ! new_stack_frame)
        return KOS_BADPTR;

    if (func->state == KOS_GEN_INIT)
        ret = this_obj;

    else {
        enum _KOS_FUNCTION_STATE state =
                (enum _KOS_FUNCTION_STATE)func->state;

        if (func->handler)  {
            const KOS_OBJ_ID retval = func->handler(new_stack_frame,
                                                    this_obj,
                                                    args_obj);

            /* Avoid detecting as end of iterator */
            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(retval))
                new_stack_frame->yield_reg = 0;

            new_stack_frame->retval = retval;

            if (KOS_is_exception_pending(new_stack_frame)) {
                assert(IS_BAD_PTR(retval));
                error = KOS_ERROR_EXCEPTION;
                _KOS_wrap_exception(new_stack_frame);
            }
            else {
                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(retval));
            }
        }
        else {
            error = _exec_function(new_stack_frame);
            assert( ! error || KOS_is_exception_pending(new_stack_frame));
        }

        ret = _finish_call(frame, INSTR_CALL_GEN, func, this_obj, new_stack_frame, &state);

        if (state == KOS_GEN_DONE)
            ret = KOS_BADPTR;
    }

    return error ? KOS_BADPTR : ret;
}

int _KOS_vm_run_module(struct _KOS_MODULE *module, KOS_OBJ_ID *ret)
{
    struct _KOS_STACK_FRAME frame;
    int                     error;

    _KOS_init_stack_frame(&frame, module, KOS_AREA_RECLAIMABLE, module->instr_offs, module->num_regs);

    KOS_context_validate(&frame);

    error = _exec_function(&frame);

    assert( ! KOS_is_exception_pending(&frame) || error == KOS_ERROR_EXCEPTION);

    if (error)
        *ret = frame.exception;
    else
        *ret = frame.retval;

    assert( ! IS_BAD_PTR(*ret));

    return error;
}

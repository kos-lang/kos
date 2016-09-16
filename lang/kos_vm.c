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

static KOS_ASCII_STRING(str_err_args_not_array,      "function arguments are not an array");
static KOS_ASCII_STRING(str_err_cannot_yield,        "function is not a generator");
static KOS_ASCII_STRING(str_err_div_by_zero,         "division by zero");
static KOS_ASCII_STRING(str_err_generator_end,       "generator");
static KOS_ASCII_STRING(str_err_generator_running,   "generator is running");
static KOS_ASCII_STRING(str_err_invalid_byte_value,  "buffer element value out of range");
static KOS_ASCII_STRING(str_err_invalid_index,       "index out of range");
static KOS_ASCII_STRING(str_err_invalid_instruction, "invalid instruction");
static KOS_ASCII_STRING(str_err_invalid_string,      "invalid string index");
static KOS_ASCII_STRING(str_err_new_with_generator,  "new invoked a generator");
static KOS_ASCII_STRING(str_err_not_callable,        "object is not callable");
static KOS_ASCII_STRING(str_err_not_generator,       "function is not a generator");
static KOS_ASCII_STRING(str_err_too_few_args,        "not enough arguments passed to a function");
static KOS_ASCII_STRING(str_err_unsup_operand_types, "unsupported operand types");
static KOS_ASCII_STRING(str_proto,                   "prototype");

static int _exec_function(KOS_STACK_FRAME *stack_frame);

static KOS_OBJ_PTR _make_string(KOS_CONTEXT        *ctx,
                                struct _KOS_MODULE *module,
                                int                 idx)
{
    KOS_OBJ_PTR obj = TO_OBJPTR(0);

    if (idx >= 0) /* TODO check upper boundary */
        obj = TO_OBJPTR(&module->strings[idx]);
    else
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_string));

    return obj;
}

static KOS_OBJ_PTR _add_integer(KOS_CONTEXT *ctx,
                                int64_t      a,
                                KOS_OBJ_PTR  bobj)
{
    enum KOS_OBJECT_TYPE type;
    KOS_OBJ_PTR          ret;

    if (IS_SMALL_INT(bobj) || (type = GET_OBJ_TYPE(bobj)) == OBJ_INTEGER) {
        const int64_t b = IS_SMALL_INT(bobj)
                         ? GET_SMALL_INT(bobj)
                         : OBJPTR(KOS_INTEGER, bobj)->number;
        ret = KOS_new_int(ctx, a + b);
    }
    else if (type == OBJ_FLOAT)
        ret = KOS_new_float(ctx, a + OBJPTR(KOS_FLOAT, bobj)->number);
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _add_float(KOS_CONTEXT *ctx,
                              double       a,
                              KOS_OBJ_PTR  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)OBJPTR(KOS_INTEGER, bobj)->number;
            break;

        case OBJ_FLOAT:
            b = OBJPTR(KOS_FLOAT, bobj)->number;
            break;

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
    }

    return KOS_new_float(ctx, a + b);
}

static KOS_OBJ_PTR _sub_integer(KOS_CONTEXT *ctx,
                                int64_t      a,
                                KOS_OBJ_PTR  bobj)
{
    enum KOS_OBJECT_TYPE type;
    KOS_OBJ_PTR          ret;

    if (IS_SMALL_INT(bobj) || (type = GET_OBJ_TYPE(bobj)) == OBJ_INTEGER) {
        const int64_t b = IS_SMALL_INT(bobj)
                         ? GET_SMALL_INT(bobj)
                         : OBJPTR(KOS_INTEGER, bobj)->number;
        ret = KOS_new_int(ctx, a - b);
    }
    else if (type == OBJ_FLOAT)
        ret = KOS_new_float(ctx, a - OBJPTR(KOS_FLOAT, bobj)->number);
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _sub_float(KOS_CONTEXT *ctx,
                              double       a,
                              KOS_OBJ_PTR  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)OBJPTR(KOS_INTEGER, bobj)->number;
            break;

        case OBJ_FLOAT:
            b = OBJPTR(KOS_FLOAT, bobj)->number;
            break;

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
    }

    return KOS_new_float(ctx, a - b);
}

static KOS_OBJ_PTR _mul_integer(KOS_CONTEXT *ctx,
                                int64_t      a,
                                KOS_OBJ_PTR  bobj)
{
    enum KOS_OBJECT_TYPE type;
    KOS_OBJ_PTR          ret;

    if (IS_SMALL_INT(bobj) || (type = GET_OBJ_TYPE(bobj)) == OBJ_INTEGER) {
        const int64_t b = IS_SMALL_INT(bobj)
                         ? GET_SMALL_INT(bobj)
                         : OBJPTR(KOS_INTEGER, bobj)->number;
        ret = KOS_new_int(ctx, a * b);
    }
    else if (type == OBJ_FLOAT)
        ret = KOS_new_float(ctx, a * OBJPTR(KOS_FLOAT, bobj)->number);
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _mul_float(KOS_CONTEXT *ctx,
                              double       a,
                              KOS_OBJ_PTR  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)OBJPTR(KOS_INTEGER, bobj)->number;
            break;

        case OBJ_FLOAT:
            b = OBJPTR(KOS_FLOAT, bobj)->number;
            break;

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
    }

    return KOS_new_float(ctx, a * b);
}

static KOS_OBJ_PTR _div_integer(KOS_CONTEXT *ctx,
                                int64_t      a,
                                KOS_OBJ_PTR  bobj)
{
    enum KOS_OBJECT_TYPE type;
    KOS_OBJ_PTR          ret;

    if (IS_SMALL_INT(bobj) || (type = GET_OBJ_TYPE(bobj)) == OBJ_INTEGER) {
        const int64_t b = IS_SMALL_INT(bobj)
                         ? GET_SMALL_INT(bobj)
                         : OBJPTR(KOS_INTEGER, bobj)->number;
        if (b)
            ret = KOS_new_int(ctx, a / b);
        else {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
            ret = TO_OBJPTR(0);
        }
    }
    else if (type == OBJ_FLOAT) {
        const double b = OBJPTR(KOS_FLOAT, bobj)->number;
        if (b != 0)
            ret = KOS_new_float(ctx, a / b);
        else {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
            ret = TO_OBJPTR(0);
        }
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _div_float(KOS_CONTEXT *ctx,
                              double       a,
                              KOS_OBJ_PTR  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)OBJPTR(KOS_INTEGER, bobj)->number;
            break;

        case OBJ_FLOAT:
            b = OBJPTR(KOS_FLOAT, bobj)->number;
            break;

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
    }

    if (b == 0) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
        return TO_OBJPTR(0);
    }

    return KOS_new_float(ctx, a / b);
}

static KOS_OBJ_PTR _mod_integer(KOS_CONTEXT *ctx,
                                int64_t      a,
                                KOS_OBJ_PTR  bobj)
{
    enum KOS_OBJECT_TYPE type;
    KOS_OBJ_PTR          ret;

    if (IS_SMALL_INT(bobj) || (type = GET_OBJ_TYPE(bobj)) == OBJ_INTEGER) {
        const int64_t b = IS_SMALL_INT(bobj)
                         ? GET_SMALL_INT(bobj)
                         : OBJPTR(KOS_INTEGER, bobj)->number;
        if (b)
            ret = KOS_new_int(ctx, a % b);
        else {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
            ret = TO_OBJPTR(0);
        }
    }
    else if (type == OBJ_FLOAT) {
        const double b = OBJPTR(KOS_FLOAT, bobj)->number;
        if (b != 0)
            ret = KOS_new_float(ctx, fmod((double)a, b));
        else {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
            ret = TO_OBJPTR(0);
        }
    }
    else {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
        ret = TO_OBJPTR(0);
    }

    return ret;
}

static KOS_OBJ_PTR _mod_float(KOS_CONTEXT *ctx,
                              double       a,
                              KOS_OBJ_PTR  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)OBJPTR(KOS_INTEGER, bobj)->number;
            break;

        case OBJ_FLOAT:
            b = OBJPTR(KOS_FLOAT, bobj)->number;
            break;

        default:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
            return TO_OBJPTR(0);
    }

    if (b == 0) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_div_by_zero));
        return TO_OBJPTR(0);
    }

    return KOS_new_float(ctx, fmod(a, b));
}

static int _compare_integer(KOS_BYTECODE_INSTR instr,
                            int64_t            a,
                            int64_t            b)
{
    int ret;

    switch (instr) {

        case INSTR_CMP_EQ:
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

        case INSTR_CMP_GT:
            ret = a > b;
            break;

        case INSTR_CMP_GE:
            ret = a >= b;
            break;

        default:
            assert(0);
            ret = 0;
            break;
    }

    return ret;
}

static int _compare_float(KOS_BYTECODE_INSTR instr,
                          KOS_OBJ_PTR        aobj,
                          KOS_OBJ_PTR        bobj)
{
    int ret;

    if ((IS_SMALL_INT(aobj) || GET_OBJ_TYPE(aobj) == OBJ_INTEGER) &&
        (IS_SMALL_INT(bobj) || GET_OBJ_TYPE(bobj) == OBJ_INTEGER)) {

        const int64_t a = IS_SMALL_INT(aobj) ? GET_SMALL_INT(aobj)
                                             : OBJPTR(KOS_INTEGER, aobj)->number;
        const int64_t b = IS_SMALL_INT(bobj) ? GET_SMALL_INT(bobj)
                                             : OBJPTR(KOS_INTEGER, bobj)->number;

        ret = _compare_integer(instr, a, b);
    }
    else {

        const double a = IS_SMALL_INT(aobj)
                             ? GET_SMALL_INT(aobj)
                             : GET_OBJ_TYPE(aobj) == OBJ_INTEGER
                                   ? OBJPTR(KOS_INTEGER, aobj)->number
                                   : OBJPTR(KOS_FLOAT,   aobj)->number;

        const double b = IS_SMALL_INT(bobj)
                             ? GET_SMALL_INT(bobj)
                             : GET_OBJ_TYPE(bobj) == OBJ_INTEGER
                                   ? OBJPTR(KOS_INTEGER, bobj)->number
                                   : OBJPTR(KOS_FLOAT,   bobj)->number;

        switch (instr) {

            case INSTR_CMP_EQ:
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

            case INSTR_CMP_GT:
                ret = a > b;
                break;

            case INSTR_CMP_GE:
                ret = a >= b;
                break;

            default:
                assert(0);
                ret = 0;
                break;
        }
    }

    return ret;
}

static int _compare_string(KOS_BYTECODE_INSTR instr,
                           KOS_OBJ_PTR        aobj,
                           KOS_OBJ_PTR        bobj)
{
    int ret;

    if (IS_STRING_OBJ(bobj)) {

        const int str_cmp = KOS_string_compare(aobj, bobj);

        switch (instr) {

            case INSTR_CMP_EQ:
                ret = ! str_cmp;
                break;

            case INSTR_CMP_GE:
                ret = str_cmp >= 0;
                break;

            case INSTR_CMP_GT:
                ret = str_cmp > 0;
                break;

            case INSTR_CMP_LE:
                ret = str_cmp <= 0;
                break;

            case INSTR_CMP_LT:
                ret = str_cmp < 0;
                break;

            case INSTR_CMP_NE:
                ret = !! str_cmp;
                break;

            default:
                assert(0);
                ret = 0;
                break;
        }
    }
    else {
        ret = 0;
    }

    return ret;
}

static int _init_registers(KOS_CONTEXT  *ctx,
                           KOS_FUNCTION *func,
                           KOS_OBJ_PTR   regs,
                           KOS_OBJ_PTR   args_obj,
                           KOS_OBJ_PTR   this_obj,
                           KOS_OBJ_PTR   closures)
{
    int error = KOS_SUCCESS;

    KOS_ATOMIC(KOS_OBJ_PTR) *const new_regs = _KOS_get_array_buffer(OBJPTR(KOS_ARRAY, regs));

    uint32_t reg = func->args_reg;

    assert(func->num_regs >= reg + 2); /* args, this */
    assert(func->num_regs == KOS_get_array_size(regs));

    new_regs[reg++] = args_obj;
    new_regs[reg++] = this_obj;

    assert(!IS_BAD_PTR(closures));
    assert(!IS_SMALL_INT(closures));
    if (GET_OBJ_TYPE(closures) == OBJ_ARRAY) {
        uint32_t src_len;
        uint32_t i;

        src_len = KOS_get_array_size(closures);

        assert(reg + src_len <= 256U);
        assert(reg + src_len <= KOS_get_array_size(regs));

        for (i=0; i < src_len; i++) {
            KOS_OBJ_PTR obj = KOS_array_read(ctx, closures, (int)i);
            if (IS_BAD_PTR(obj)) {
                error = KOS_ERROR_EXCEPTION;
                break;
            }
            else
                new_regs[reg++] = obj;
        }
    }

    return error;
}

static int _prepare_call(KOS_CONTEXT       *ctx,
                         KOS_BYTECODE_INSTR instr,
                         KOS_OBJ_PTR        func_obj,
                         KOS_OBJ_PTR       *this_obj,
                         KOS_OBJ_PTR        args_obj)
{
    int                       error = KOS_SUCCESS;
    KOS_FUNCTION             *func;
    enum _KOS_GENERATOR_STATE gen_state;
    KOS_STACK_FRAME          *new_stack_frame;

    assert( ! IS_BAD_PTR(func_obj));
    assert( ! IS_BAD_PTR(args_obj));

    if (IS_SMALL_INT(func_obj) || GET_OBJ_TYPE(func_obj) != OBJ_FUNCTION) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_callable));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (IS_SMALL_INT(args_obj) || GET_OBJ_TYPE(args_obj) != OBJ_ARRAY) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_args_not_array));
        TRY(KOS_ERROR_EXCEPTION);
    }

    func      = OBJPTR(KOS_FUNCTION, func_obj);
    gen_state = func->generator_state;

    if (KOS_get_array_size(args_obj) < func->min_args) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_too_few_args));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (instr == INSTR_NEW && gen_state != KOS_NOT_GEN) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_new_with_generator));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (instr == INSTR_CALL_GEN && gen_state < KOS_GEN_READY) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_generator));
        TRY(KOS_ERROR_EXCEPTION);
    }

    switch (gen_state) {

        /* Regular function */
        case KOS_NOT_GEN: {
            if (instr == INSTR_NEW) {
                KOS_OBJ_PTR proto = KOS_get_property(ctx,
                                                     func_obj,
                                                     TO_OBJPTR(&str_proto));
                if (IS_BAD_PTR(proto))
                    TRY(KOS_ERROR_EXCEPTION);

                if (func->handler)
                    *this_obj = proto;
                else {
                    *this_obj = KOS_new_object_with_prototype(ctx, proto);
                    if (IS_BAD_PTR(*this_obj))
                        TRY(KOS_ERROR_EXCEPTION);
                }

            }

            new_stack_frame = _KOS_stack_frame_push_func(ctx, func);
            if ( ! new_stack_frame) {
                assert(KOS_is_exception_pending(ctx));
                TRY(KOS_ERROR_EXCEPTION);
            }

            if ( ! func->handler)
                TRY(_init_registers(ctx, func, new_stack_frame->registers, args_obj, *this_obj, func->closures));

            break;
        }

        /* Instantiate a generator function */
        case KOS_GEN_INIT: {
            KOS_FUNCTION *dest;
            KOS_OBJ_PTR   ret;
            KOS_OBJ_PTR   stack_frame = ctx->stack_frame;
            KOS_OBJ_PTR   proto_obj   = KOS_get_property(ctx, func_obj, TO_OBJPTR(&str_proto));

            if (IS_BAD_PTR(proto_obj))
                TRY(KOS_ERROR_EXCEPTION);

            ret = KOS_new_function(ctx, proto_obj);
            if (IS_BAD_PTR(ret))
                TRY(KOS_ERROR_EXCEPTION);

            dest = OBJPTR(KOS_FUNCTION, ret);

            dest->min_args        = 0;
            dest->num_regs        = func->num_regs;
            dest->instr_offs      = func->instr_offs;
            dest->closures        = func->closures;
            dest->module          = func->module;
            dest->handler         = func->handler;
            dest->generator_state = KOS_GEN_READY;

            new_stack_frame = _KOS_stack_frame_push_func(ctx, func);
            if ( ! new_stack_frame)
                TRY(KOS_ERROR_EXCEPTION);

            if (func->handler)
                new_stack_frame->registers = args_obj;
            else
                TRY(_init_registers(ctx,
                                    dest,
                                    new_stack_frame->registers,
                                    args_obj,
                                    *this_obj,
                                    func->closures));

            ctx->stack_frame            = stack_frame;
            dest->generator_stack_frame = TO_OBJPTR(new_stack_frame);
            new_stack_frame->parent     = KOS_VOID;
            new_stack_frame->yield_reg  = KOS_CAN_YIELD;

            *this_obj = ret;
            break;
        }

        /* Generator function */
        case KOS_GEN_READY:
            /* fall through */
        case KOS_GEN_ACTIVE: {
            KOS_ATOMIC(KOS_OBJ_PTR) *gen_regs = 0;
            const uint32_t           num_args = KOS_get_array_size(args_obj);

            assert(!IS_BAD_PTR(func->generator_stack_frame));
            assert(!IS_SMALL_INT(func->generator_stack_frame));
            assert(GET_OBJ_TYPE(func->generator_stack_frame) == OBJ_STACK_FRAME);

            new_stack_frame = OBJPTR(KOS_STACK_FRAME, func->generator_stack_frame);

            if ( ! func->handler)
                gen_regs = _KOS_get_array_buffer(OBJPTR(KOS_ARRAY, new_stack_frame->registers));
            else
                *this_obj = new_stack_frame->registers;

            /* TODO remove these checks? */
            if (gen_state == KOS_GEN_READY && num_args) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_too_few_args));
                TRY(KOS_ERROR_EXCEPTION);
            }
            else if (num_args > 1) {
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_too_few_args));
                TRY(KOS_ERROR_EXCEPTION);
            }
            else if (gen_state == KOS_GEN_ACTIVE) {

                const uint32_t r = new_stack_frame->yield_reg;

                assert( ! func->handler);

                assert(r < KOS_get_array_size(new_stack_frame->registers));

                assert(gen_regs);

                if (gen_regs) {
                    if (num_args)
                        gen_regs[r] = KOS_array_read(ctx, args_obj, 0);
                    else
                        gen_regs[r] = KOS_VOID;
                }
            }

            /* TODO perform CAS for thread safety */
            func->generator_state = KOS_GEN_RUNNING;

            new_stack_frame->parent    = ctx->stack_frame;
            new_stack_frame->yield_reg = KOS_CAN_YIELD;
            ctx->stack_frame           = TO_OBJPTR(new_stack_frame);
            break;
        }

        case KOS_GEN_RUNNING:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_generator_running));
            TRY(KOS_ERROR_EXCEPTION);
            break;

        case KOS_GEN_DONE:
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_generator_end));
            TRY(KOS_ERROR_EXCEPTION);
            break;

        default:
            assert(0);
            error = KOS_ERROR_INTERNAL;
            goto _error;
    }

_error:
    return error;
}

static KOS_OBJ_PTR _finish_call(KOS_CONTEXT               *ctx,
                                KOS_BYTECODE_INSTR         instr,
                                KOS_FUNCTION              *func,
                                KOS_OBJ_PTR                this_obj,
                                KOS_STACK_FRAME           *stack_frame,
                                enum _KOS_GENERATOR_STATE *gen_state)
{
    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    if ( ! KOS_is_exception_pending(ctx)) {

        if (instr == INSTR_NEW && ! func->handler)
            ret = this_obj;
        else
            ret = stack_frame->retval;

        if (*gen_state != KOS_NOT_GEN) {
            if (stack_frame->yield_reg == KOS_CAN_YIELD) {
                *gen_state            = KOS_GEN_DONE;
                func->generator_state = KOS_GEN_DONE;
                if (instr != INSTR_CALL_GEN) {
                    if (IS_BAD_PTR(stack_frame->retval))
                        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_generator_end));
                    else
                        KOS_raise_exception(ctx, stack_frame->retval);
                }
            }
            else {
                const enum _KOS_GENERATOR_STATE end_state = func->handler ? KOS_GEN_READY : KOS_GEN_ACTIVE;

                *gen_state            = end_state;
                func->generator_state = end_state;
            }
        }
    }
    else if (*gen_state != KOS_NOT_GEN) {
        *gen_state            = KOS_GEN_DONE;
        func->generator_state = KOS_GEN_DONE;
    }

    return ret;
}

static KOS_OBJ_PTR _read_buffer(KOS_CONTEXT *ctx, KOS_OBJ_PTR objptr, int idx)
{
    KOS_BUFFER              *buffer;
    struct _KOS_BUFFER_DATA *data;
    uint32_t                 size;
    KOS_OBJ_PTR              ret;

    assert( ! IS_BAD_PTR(objptr));
    assert( ! IS_SMALL_INT(objptr));
    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    buffer = OBJPTR(KOS_BUFFER, objptr);
    size   = KOS_atomic_read_u32(buffer->size);
    data   = (struct _KOS_BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_index));
        ret = KOS_VOID;
    }
    else
        ret = TO_SMALL_INT((int)data->buf[idx]);

    return ret;
}

static int _write_buffer(KOS_CONTEXT *ctx, KOS_OBJ_PTR objptr, int idx, KOS_OBJ_PTR value)
{
    int                      error;
    KOS_BUFFER              *buffer;
    struct _KOS_BUFFER_DATA *data;
    uint32_t                 size;
    int64_t                  byte_value;

    assert( ! IS_BAD_PTR(objptr));
    assert( ! IS_SMALL_INT(objptr));
    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    TRY(KOS_get_integer(ctx, value, &byte_value));

    if (byte_value < 0 || byte_value > 255) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_byte_value));
        TRY(KOS_ERROR_EXCEPTION);
    }

    buffer = OBJPTR(KOS_BUFFER, objptr);
    size   = KOS_atomic_read_u32(buffer->size);
    data   = (struct _KOS_BUFFER_DATA *)KOS_atomic_read_ptr(buffer->data);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_index));
        TRY(KOS_ERROR_EXCEPTION);
    }

    data->buf[idx] = (uint8_t)byte_value;

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

static int _exec_function(KOS_STACK_FRAME *stack_frame)
{
    KOS_ARRAY               *regs_array = OBJPTR(KOS_ARRAY, stack_frame->registers);
    KOS_ATOMIC(KOS_OBJ_PTR) *regs       = _KOS_get_array_buffer(regs_array);
    KOS_MODULE              *module     = OBJPTR(KOS_MODULE, stack_frame->module);
    KOS_CONTEXT             *ctx        = module->context;
    const uint8_t           *bytecode   = module->bytecode + stack_frame->instr_offs;
    int                      error      = KOS_SUCCESS;

    for (;;) { /* Exit condition at the end of the loop */

        const KOS_BYTECODE_INSTR instr = (KOS_BYTECODE_INSTR)*bytecode;
        int32_t                  delta = 1;
        KOS_OBJ_PTR              out   = TO_OBJPTR(0);
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
                out   = KOS_new_int(ctx, value);
                delta = 6;
                break;
            }

            case INSTR_LOAD_INT64: { /* <r.dest>, <low32>, <high32> */
                const uint32_t low   = _load_32(bytecode+2);
                const uint32_t high  = _load_32(bytecode+6);

                rdest = bytecode[1];
                out   = KOS_new_int(ctx, (int64_t)(((uint64_t)high << 32) | low));
                delta = 10;
                break;
            }

            case INSTR_LOAD_FLOAT: { /* <r.dest>, <low32>, <high32> */
                const uint32_t low   = _load_32(bytecode+2);
                const uint32_t high  = _load_32(bytecode+6);

                union {
                    uint64_t ui64;
                    double   d;
                } int2double;

                int2double.ui64 = ((uint64_t)high << 32) | low;

                rdest = bytecode[1];
                out   = KOS_new_float(ctx, int2double.d);
                delta = 10;
                break;
            }

            case INSTR_LOAD_STR: { /* <r.dest>, <const.str.idx32> */
                const int32_t  idx   = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = _make_string(ctx, module, idx);
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

            case INSTR_LOAD_FUN: /* <r.dest>, <delta32>, <min.args>, <num.regs>, <args.reg> */
                /* fall through */
            case INSTR_LOAD_GEN: { /* <r.dest>, <delta32>, <min.args>, <num.regs>, <args.reg> */
                const int32_t  fun_offs = (int32_t)_load_32(bytecode+2);
                const uint8_t  min_args = bytecode[6];
                const uint8_t  num_regs = bytecode[7];
                const uint8_t  args_reg = bytecode[8];

                const uint32_t offset   = (uint32_t)((bytecode + 9 + fun_offs) - module->bytecode);
                KOS_OBJ_PTR    fun_obj  = TO_OBJPTR(0);
                KOS_OBJ_PTR    proto_obj;

                assert(offset < module->bytecode_size);

                proto_obj = KOS_gen_prototype(ctx, bytecode + 9 + fun_offs);

                if ( ! IS_BAD_PTR(proto_obj))
                    fun_obj = KOS_new_function(ctx, proto_obj);

                if ( ! IS_BAD_PTR(fun_obj)) {

                    KOS_FUNCTION *const fun = OBJPTR(KOS_FUNCTION, fun_obj);

                    fun->min_args   = min_args;
                    fun->num_regs   = num_regs;
                    fun->args_reg   = args_reg;
                    fun->instr_offs = offset;
                    fun->module     = TO_OBJPTR(module);

                    if (instr == INSTR_LOAD_GEN)
                        fun->generator_state = KOS_GEN_INIT;
                }

                rdest = bytecode[1];
                out   = fun_obj;
                delta = 9;
                break;
            }

            case INSTR_LOAD_ARRAY8: { /* <r.dest>, <int.size8> */
                const uint8_t size = bytecode[2];

                rdest = bytecode[1];
                out   = KOS_new_array(ctx, size);
                delta = 3;
                break;
            }

            case INSTR_LOAD_ARRAY: { /* <r.dest>, <int.size32> */
                const uint32_t size = _load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_new_array(ctx, size);
                delta = 6;
                break;
            }

            case INSTR_LOAD_OBJ: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_new_object(ctx);
                delta = 2;
                break;
            }

            case INSTR_MOVE: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];
                out   = regs[rsrc];
                delta = 3;
                break;
            }

            case INSTR_GET_GLOBAL: { /* <r.dest>, <int32> */
                const int32_t  idx = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_array_read(ctx, module->globals, idx);
                delta = 6;
                break;
            }

            case INSTR_SET_GLOBAL: { /* <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->length);

                error = KOS_array_write(ctx, module->globals, idx, regs[rsrc]);
                delta = 6;
                break;
            }

            case INSTR_GET_MOD: { /* <r.dest>, <int32>, <r.glob> */
                const int      mod_idx    = (int32_t)_load_32(bytecode+2);
                const unsigned rglob      = bytecode[6];
                KOS_OBJ_PTR    module_obj = KOS_array_read(ctx, TO_OBJPTR(&ctx->modules), mod_idx);

                assert(rglob < regs_array->length);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {

                    KOS_OBJ_PTR glob_idx;

                    assert(!IS_SMALL_INT(module_obj));
                    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                    glob_idx = KOS_get_property(ctx, OBJPTR(KOS_MODULE, module_obj)->global_names, regs[rglob]);

                    if (!IS_BAD_PTR(glob_idx)) {

                        assert(IS_SMALL_INT(glob_idx));

                        out = KOS_array_read(ctx, OBJPTR(KOS_MODULE, module_obj)->globals,
                                             (int)GET_SMALL_INT(glob_idx));
                    }
                }

                delta = 7;
                break;
            }

            case INSTR_GET_MOD_ELEM: { /* <r.dest>, <int32>, <int32> */
                const int      mod_idx    = (int32_t)_load_32(bytecode+2);
                const int      glob_idx   = (int32_t)_load_32(bytecode+6);
                KOS_OBJ_PTR    module_obj = KOS_array_read(ctx, TO_OBJPTR(&ctx->modules), mod_idx);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {
                    assert(!IS_SMALL_INT(module_obj));
                    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                    out = KOS_array_read(ctx, OBJPTR(KOS_MODULE, module_obj)->globals, glob_idx);
                }

                delta = 10;
                break;
            }

            case INSTR_GET: { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];
                KOS_OBJ_PTR    src;
                KOS_OBJ_PTR    prop;

                assert(rsrc  < regs_array->length);
                assert(rprop < regs_array->length);

                rdest = bytecode[1];
                src   = regs[rsrc];
                prop  = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(ctx, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_index));
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        enum KOS_OBJECT_TYPE type = (IS_BAD_PTR(src) || IS_SMALL_INT(src)) ? OBJ_INTEGER : GET_OBJ_TYPE(src);
                        if (type <= '4')
                            out = KOS_string_get_char(ctx, src, (int)idx);
                        else if (type == OBJ_BUFFER)
                            out = _read_buffer(ctx, src, (int)idx);
                        else
                            out = KOS_array_read(ctx, src, (int)idx);
                    }
                }
                else {
                    KOS_OBJ_PTR value = KOS_get_property(ctx, src, prop);

                    if ( ! IS_BAD_PTR(value) && ! IS_SMALL_INT(value) && GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_PTR args;
                        stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        value = OBJPTR(KOS_DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(ctx, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(ctx, value, src, args);
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
                const unsigned       rsrc = bytecode[2];
                const int32_t        idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_PTR          src;
                enum KOS_OBJECT_TYPE type;

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];
                src   = regs[rsrc];

                type = (IS_BAD_PTR(src) || IS_SMALL_INT(src)) ? OBJ_INTEGER : GET_OBJ_TYPE(src);

                if (type <= '4')
                    out = KOS_string_get_char(ctx, src, idx);
                else if (type == OBJ_BUFFER)
                    out = _read_buffer(ctx, src, idx);
                else
                    out = KOS_array_read(ctx, src, idx);

                delta = 7;
                break;
            }

            case INSTR_GET_RANGE: { /* <r.dest>, <r.src>, <r.begin>, <r.end> */
                const unsigned rsrc   = bytecode[2];
                const unsigned rbegin = bytecode[3];
                const unsigned rend   = bytecode[4];
                KOS_OBJ_PTR    src;
                KOS_OBJ_PTR    begin;
                KOS_OBJ_PTR    end;
                int64_t        begin_idx;
                int64_t        end_idx = 0;

                assert(rsrc   < regs_array->length);
                assert(rbegin < regs_array->length);
                assert(rend   < regs_array->length);

                rdest = bytecode[1];
                src   = regs[rsrc];
                begin = regs[rbegin];
                end   = regs[rend];

                if (IS_SMALL_INT(begin) || GET_OBJ_TYPE(begin) != OBJ_VOID)
                    error = KOS_get_integer(ctx, begin, &begin_idx);
                else
                    begin_idx = 0;

                if ( ! error) {
                    if (IS_SMALL_INT(end) || GET_OBJ_TYPE(end) != OBJ_VOID)
                        error = KOS_get_integer(ctx, end, &end_idx);
                    else
                        end_idx = MAX_INT64;
                }

                if ( ! error) {
                    if (IS_STRING_OBJ(src))
                        out = KOS_string_slice(ctx, src, begin_idx, end_idx);
                    else if ( ! IS_SMALL_INT(src) && GET_OBJ_TYPE(src) == OBJ_BUFFER)
                        out = KOS_buffer_slice(ctx, src, begin_idx, end_idx);
                    else
                        out = KOS_array_slice(ctx, src, begin_idx, end_idx);
                }

                delta = 5;
                break;
            }

            case INSTR_GET_PROP: { /* <r.dest>, <r.src>, <const.str.idx32> */
                const unsigned rsrc = bytecode[2];
                const int32_t  idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_PTR    prop;

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];
                prop  = _make_string(ctx, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_PTR obj   = regs[rsrc];
                    KOS_OBJ_PTR value = KOS_get_property(ctx, obj, prop);

                    if ( ! IS_BAD_PTR(value) &&  ! IS_SMALL_INT(value) && GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_PTR args;
                        stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        value = OBJPTR(KOS_DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(ctx, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(ctx, value, obj, args);
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
                KOS_OBJ_PTR    prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->length);
                assert(rprop < regs_array->length);
                assert(rsrc  < regs_array->length);

                prop = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(ctx, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_index));
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        const KOS_OBJ_PTR obj = regs[rdest];
                        if ( ! IS_BAD_PTR(obj) && ! IS_SMALL_INT(obj) && GET_OBJ_TYPE(obj) == OBJ_BUFFER)
                            error = _write_buffer(ctx, obj, (int)idx, regs[rsrc]);
                        else
                            error = KOS_array_write(ctx, obj, (int)idx, regs[rsrc]);
                    }
                }
                else {
                    KOS_OBJ_PTR obj   = regs[rdest];
                    KOS_OBJ_PTR value = regs[rsrc];

                    error = KOS_set_property(ctx, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_PTR setter;
                        KOS_OBJ_PTR args;

                        assert(KOS_is_exception_pending(ctx));
                        setter = KOS_get_exception(ctx);
                        KOS_clear_exception(ctx);

                        assert( ! IS_BAD_PTR(setter) && ! IS_SMALL_INT(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                        stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        setter = OBJPTR(KOS_DYNAMIC_PROP, setter)->setter;

                        args  = KOS_new_array(ctx, 1);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_array_write(ctx, args, 0, value);
                            assert( ! error);
                            value = KOS_call_function(ctx, setter, obj, args);
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
                KOS_OBJ_PTR    dest;

                rdest = bytecode[1];

                assert(rdest < regs_array->length);
                assert(rsrc  < regs_array->length);

                dest = regs[rdest];

                if ( ! IS_BAD_PTR(dest) && ! IS_SMALL_INT(dest) && GET_OBJ_TYPE(dest) == OBJ_BUFFER)
                    error = _write_buffer(ctx, dest, idx, regs[rsrc]);
                else
                    error = KOS_array_write(ctx, dest, idx, regs[rsrc]);

                delta = 7;
                break;
            }

            case INSTR_SET_PROP: { /* <r.dest>, <const.str.idx32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+2);
                const unsigned rsrc = bytecode[6];
                KOS_OBJ_PTR    prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->length);
                assert(rsrc  < regs_array->length);

                prop = _make_string(ctx, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_PTR obj   = regs[rdest];
                    KOS_OBJ_PTR value = regs[rsrc];

                    error = KOS_set_property(ctx, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_PTR setter;
                        KOS_OBJ_PTR args;

                        assert(KOS_is_exception_pending(ctx));
                        setter = KOS_get_exception(ctx);
                        KOS_clear_exception(ctx);

                        assert( ! IS_BAD_PTR(setter) && ! IS_SMALL_INT(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                        stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                        setter = OBJPTR(KOS_DYNAMIC_PROP, setter)->setter;

                        args  = KOS_new_array(ctx, 1);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_array_write(ctx, args, 0, value);
                            assert( ! error);
                            value = KOS_call_function(ctx, setter, obj, args);
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

                assert(rdest < regs_array->length);
                assert(rprop < regs_array->length);

                KOS_delete_property(ctx, regs[rdest], regs[rprop]);

                delta = 3;
                break;
            }

            case INSTR_DEL_PROP: { /* <r.dest>, <const.str.idx32> */
                const int32_t  idx = (int32_t)_load_32(bytecode+2);
                KOS_OBJ_PTR    prop;

                rdest = bytecode[1];

                assert(rdest < regs_array->length);

                prop = _make_string(ctx, module, idx);

                if (!IS_BAD_PTR(prop))
                    KOS_delete_property(ctx, regs[rdest], prop);

                delta = 6;
                break;
            }

            case INSTR_ADD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_PTR src1;
                KOS_OBJ_PTR src2;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _add_integer(ctx, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER: {
                            KOS_INTEGER *a = OBJPTR(KOS_INTEGER, src1);
                            out            = _add_integer(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_FLOAT: {
                            KOS_FLOAT *a = OBJPTR(KOS_FLOAT, src1);
                            out          = _add_float(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_STRING_8:
                            /* fall through */
                        case OBJ_STRING_16:
                            /* fall through */
                        case OBJ_STRING_32: {
                            if (!IS_BAD_PTR(src2) && IS_STRING_OBJ(src2))
                                out = KOS_string_add(ctx, src1, src2);
                            else
                                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                        }

                        default:
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SUB: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_PTR src1;
                KOS_OBJ_PTR src2;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _sub_integer(ctx, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER: {
                            KOS_INTEGER *a = OBJPTR(KOS_INTEGER, src1);
                            out            = _sub_integer(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_FLOAT: {
                            KOS_FLOAT *a = OBJPTR(KOS_FLOAT, src1);
                            out          = _sub_float(ctx, a->number, src2);
                            break;
                        }

                        default:
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_MUL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_PTR src1;
                KOS_OBJ_PTR src2;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _mul_integer(ctx, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER: {
                            KOS_INTEGER *a = OBJPTR(KOS_INTEGER, src1);
                            out            = _mul_integer(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_FLOAT: {
                            KOS_FLOAT *a = OBJPTR(KOS_FLOAT, src1);
                            out          = _mul_float(ctx, a->number, src2);
                            break;
                        }

                        default:
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_DIV: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_PTR src1;
                KOS_OBJ_PTR src2;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _div_integer(ctx, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER: {
                            KOS_INTEGER *a = OBJPTR(KOS_INTEGER, src1);
                            out            = _div_integer(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_FLOAT: {
                            KOS_FLOAT *a = OBJPTR(KOS_FLOAT, src1);
                            out          = _div_float(ctx, a->number, src2);
                            break;
                        }

                        default:
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_MOD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_PTR src1;
                KOS_OBJ_PTR src2;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _mod_integer(ctx, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER: {
                            KOS_INTEGER *a = OBJPTR(KOS_INTEGER, src1);
                            out            = _mod_integer(ctx, a->number, src2);
                            break;
                        }

                        case OBJ_FLOAT: {
                            KOS_FLOAT *a = OBJPTR(KOS_FLOAT, src1);
                            out          = _mod_float(ctx, a->number, src2);
                            break;
                        }

                        default:
                            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_unsup_operand_types));
                            break;
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SHL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b < 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(ctx, a >> -b);
                        else
                            out = KOS_new_int(ctx, a << b);
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

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b > 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(ctx, a << -b);
                        else
                            out = KOS_new_int(ctx, a >> b);
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SSR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(0);
                        else if (b < 0)
                            out = KOS_new_int(ctx, a << -b);
                        else
                            out = KOS_new_int(ctx, (int64_t)((uint64_t)a >> b));
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_NOT: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];
                int64_t        a;

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc], &a);

                if (!error)
                    out = KOS_new_int(ctx, ~a);

                delta = 3;
                break;
            }

            case INSTR_AND: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(ctx, a & b);
                }

                delta = 4;
                break;
            }

            case INSTR_OR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(ctx, a | b);
                }

                delta = 4;
                break;
            }

            case INSTR_XOR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];

                error = KOS_get_integer(ctx, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(ctx, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(ctx, a ^ b);
                }

                delta = 4;
                break;
            }

            case INSTR_TYPE: { /* <r.dest>, <r.src> */
                static KOS_ASCII_STRING(t_integer,  "integer");
                static KOS_ASCII_STRING(t_float,    "float");
                static KOS_ASCII_STRING(t_string,   "string");
                static KOS_ASCII_STRING(t_boolean,  "boolean");
                static KOS_ASCII_STRING(t_void,     "void");
                static KOS_ASCII_STRING(t_object,   "object");
                static KOS_ASCII_STRING(t_array,    "array");
                static KOS_ASCII_STRING(t_buffer,   "buffer");
                static KOS_ASCII_STRING(t_function, "function");

                const unsigned rsrc  = bytecode[2];
                KOS_OBJ_PTR    src;

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];
                src   = regs[rsrc];

                assert(!IS_BAD_PTR(src));

                if (IS_SMALL_INT(src))
                    out = TO_OBJPTR(&t_integer);

                else switch (GET_OBJ_TYPE(src)) {
                    case OBJ_INTEGER:
                        out = TO_OBJPTR(&t_integer);
                        break;

                    case OBJ_FLOAT:
                        out = TO_OBJPTR(&t_float);
                        break;

                    case OBJ_STRING_8:
                        /* fall through */
                    case OBJ_STRING_16:
                        /* fall through */
                    case OBJ_STRING_32:
                        out = TO_OBJPTR(&t_string);
                        break;

                    case OBJ_BOOLEAN:
                        out = TO_OBJPTR(&t_boolean);
                        break;

                    case OBJ_VOID:
                        out = TO_OBJPTR(&t_void);
                        break;

                    case OBJ_ARRAY:
                        out = TO_OBJPTR(&t_array);
                        break;

                    case OBJ_BUFFER:
                        out = TO_OBJPTR(&t_buffer);
                        break;

                    case OBJ_FUNCTION:
                        out = TO_OBJPTR(&t_function);
                        break;

                    default:
                        out = TO_OBJPTR(&t_object);
                        break;
                }

                delta = 3;
                break;
            }

            case INSTR_CMP_EQ: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_NE: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_GE: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_GT: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_LE: /* <r.dest>, <r.src1>, <r.src2> */
                /* fall through */
            case INSTR_CMP_LT: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int            ret;
                KOS_OBJ_PTR    src1;
                KOS_OBJ_PTR    src2;

                enum KOS_OBJECT_TYPE src1_type;
                enum KOS_OBJECT_TYPE src2_type;

                assert(rsrc1 < regs_array->length);
                assert(rsrc2 < regs_array->length);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                src1_type = IS_SMALL_INT(src1) ? OBJ_INTEGER : GET_OBJ_TYPE(src1);
                src2_type = IS_SMALL_INT(src2) ? OBJ_INTEGER : GET_OBJ_TYPE(src2);

                if (src1_type == src2_type)
                    switch (src1_type) {

                        case OBJ_INTEGER:
                        case OBJ_FLOAT:
                            ret = _compare_float(instr, src1, src2);
                            break;

                        case OBJ_STRING_8:
                        case OBJ_STRING_16:
                        case OBJ_STRING_32:
                            ret = _compare_string(instr, src1, src2);
                            break;

                        case OBJ_VOID:
                            ret = _compare_integer(instr, 0, 0);
                            break;

                        case OBJ_BOOLEAN:
                            ret = _compare_integer(instr, !! KOS_get_bool(src1), !! KOS_get_bool(src2));
                            break;

                        default:
                            ret = _compare_integer(instr, (int64_t)(intptr_t)src1, (int64_t)(intptr_t)src2);
                            break;
                    }
                else
                    switch (src1_type) {

                        case OBJ_INTEGER:
                        case OBJ_FLOAT:
                            if (src2_type == OBJ_INTEGER || src2_type == OBJ_FLOAT)
                                ret = _compare_float(instr, src1, src2);
                            else
                                ret = _compare_integer(instr, src1_type, src2_type);
                            break;

                        case OBJ_STRING_8:
                        case OBJ_STRING_16:
                        case OBJ_STRING_32:
                            if (src2_type <= OBJ_STRING_32)
                                ret = _compare_string(instr, src1, src2);
                            else
                                ret = _compare_integer(instr, src1_type, src2_type);
                            break;

                        default:
                            ret = _compare_integer(instr, src1_type, src2_type);
                            break;
                    }

                out   = KOS_BOOL(ret);
                delta = 4;
                break;
            }

            case INSTR_HAS: { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                KOS_OBJ_PTR obj;

                assert(rsrc  < regs_array->length);
                assert(rprop < regs_array->length);

                rdest = bytecode[1];

                obj = KOS_get_property(ctx, regs[rsrc], regs[rprop]);
                KOS_clear_exception(ctx);

                out   = KOS_BOOL(!IS_BAD_PTR(obj));
                delta = 4;
                break;
            }

            case INSTR_HAS_PROP: { /* <r.dest>, <r.src>, <const.str.idx32> */
                const unsigned rsrc  = bytecode[2];
                const int32_t  idx   = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_PTR    prop;

                assert(rsrc  < regs_array->length);

                rdest = bytecode[1];

                prop = _make_string(ctx, module, idx);

                if (!IS_BAD_PTR(prop)) {

                    KOS_OBJ_PTR obj = KOS_get_property(ctx, regs[rsrc], prop);
                    KOS_clear_exception(ctx);

                    out = KOS_BOOL(!IS_BAD_PTR(obj));
                }

                delta = 7;
                break;
            }

            case INSTR_INSTANCEOF: { /* <r.dest>, <r.src>, <r.func> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rfunc = bytecode[3];

                KOS_OBJ_PTR constr;
                KOS_OBJ_PTR proto;
                KOS_OBJ_PTR ret = KOS_FALSE;

                assert(rsrc  < regs_array->length);
                assert(rfunc < regs_array->length);

                rdest  = bytecode[1];
                constr = regs[rfunc];
                proto  = KOS_get_property(ctx, constr, TO_OBJPTR(&str_proto));

                if ( ! IS_BAD_PTR(proto) && ! IS_SMALL_INT(proto) && GET_OBJ_TYPE(proto) == OBJ_DYNAMIC_PROP) {
                    KOS_OBJ_PTR args;
                    stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);
                    proto = OBJPTR(KOS_DYNAMIC_PROP, proto)->getter;
                    args  = KOS_new_array(ctx, 0);
                    if (IS_BAD_PTR(args))
                        error = KOS_ERROR_EXCEPTION;
                    else {
                        proto = KOS_call_function(ctx, proto, constr, args);
                        if (IS_BAD_PTR(proto))
                            proto = TO_OBJPTR(0);
                    }
                }

                if (IS_BAD_PTR(proto))
                    KOS_clear_exception(ctx);
                else {
                    KOS_OBJ_PTR obj = regs[rsrc];
                    do {
                        obj = KOS_get_prototype(ctx, obj);
                        if (obj == proto) {
                            ret = KOS_TRUE;
                            break;
                        }
                    }
                    while (!IS_BAD_PTR(obj));
                }

                out   = ret;
                delta = 4;
                break;
            }

            case INSTR_JUMP: { /* <delta32> */
                delta = 5 + (int32_t)_load_32(bytecode+1);
                break;
            }

            case INSTR_JUMP_COND: { /* <delta32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->length);

                delta = 6;

                if (_KOS_is_truthy(regs[rsrc]))
                    delta += offs;
                break;
            }

            case INSTR_JUMP_NOT_COND: { /* <delta32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < regs_array->length);

                delta = 6;

                if ( ! _KOS_is_truthy(regs[rsrc]))
                    delta += offs;
                break;
            }

            case INSTR_BIND_SELF: /* <r.dest>, <slot.idx> */
                /* fall through */
            case INSTR_BIND: { /* <r.dest>, <slot.idx>, <r.src> */
                const unsigned idx = bytecode[2];

                KOS_OBJ_PTR dest;
                rdest = bytecode[1];
                assert(rdest < regs_array->length);
                dest = regs[rdest];

                if (IS_SMALL_INT(dest) || GET_OBJ_TYPE(dest) != OBJ_FUNCTION)
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_callable));
                else {
                    KOS_OBJ_PTR closures = OBJPTR(KOS_FUNCTION, dest)->closures;
                    KOS_OBJ_PTR regs_obj;

                    if (instr == INSTR_BIND) {
                        const unsigned rsrc = bytecode[3];
                        assert(rsrc < regs_array->length);
                        regs_obj = regs[rsrc];
                    }
                    else
                        regs_obj = stack_frame->registers;

                    assert(!IS_BAD_PTR(closures));
                    assert(!IS_SMALL_INT(closures));

                    if (GET_OBJ_TYPE(closures) == OBJ_VOID) {
                        closures = KOS_new_array(ctx, idx+1);
                        if (IS_BAD_PTR(closures))
                            error = KOS_ERROR_EXCEPTION;
                        else
                            OBJPTR(KOS_FUNCTION, dest)->closures = closures;
                    }
                    else if (idx >= KOS_get_array_size(closures))
                        error = KOS_array_resize(ctx, closures, idx+1);

                    if (!error)
                        error = KOS_array_write(ctx, closures, (int)idx, regs_obj);
                }

                delta = (instr == INSTR_BIND_SELF) ? 3 : 4;
                break;
            }

            case INSTR_TAIL_CALL: /* <closure.size.int>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_CALL: /* <r.dest>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_CALL_GEN: /* <r.dest>, <r.func>, <r.final>, <r.args> */
                /* fall through */
            case INSTR_NEW: { /* <r.dest>, <r.func>, <r.args> */
                const unsigned rfunc = bytecode[2];
                unsigned       rthis = ~0U;
                unsigned       rargs;

                KOS_OBJ_PTR func_obj;
                KOS_OBJ_PTR this_obj;
                KOS_OBJ_PTR args_obj;

                rdest = bytecode[1];

                if (instr == INSTR_NEW) {
                    rargs    = bytecode[3];
                    this_obj = TO_OBJPTR(0);
                }
                else {
                    rthis = bytecode[3];
                    rargs = bytecode[4];
                    assert(rthis < regs_array->length);

                    this_obj = regs[rthis];
                    assert( ! IS_BAD_PTR(this_obj));
                }

                assert(instr != INSTR_TAIL_CALL || rdest <= regs_array->length);
                assert(rfunc < regs_array->length);
                assert(rargs < regs_array->length);

                func_obj = regs[rfunc];
                args_obj = regs[rargs];

                stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

                error = _prepare_call(ctx, instr, func_obj, &this_obj, args_obj);

                if ( ! error) {

                    KOS_FUNCTION *func = OBJPTR(KOS_FUNCTION, func_obj);

                    if (func->generator_state == KOS_GEN_INIT)
                        out = this_obj;

                    else {
                        KOS_STACK_FRAME          *new_stack_frame = OBJPTR(KOS_STACK_FRAME, ctx->stack_frame);
                        enum _KOS_GENERATOR_STATE gen_state       = func->generator_state;

                        /* TODO INSTR_TAIL_CALL */

                        if (func->handler)  {
                            const KOS_OBJ_PTR ret_val = func->handler(ctx, this_obj, args_obj);

                            /* Avoid detecting as end of iterator in the code below */
                            if (gen_state != KOS_NOT_GEN && ! IS_BAD_PTR(ret_val))
                                new_stack_frame->yield_reg = 0;

                            new_stack_frame->retval = ret_val;

                            if (KOS_is_exception_pending(ctx)) {
                                assert(IS_BAD_PTR(ret_val));
                                error = KOS_ERROR_EXCEPTION;
                                _KOS_wrap_exception(ctx, new_stack_frame);
                            }
                            else {
                                assert(gen_state > KOS_GEN_INIT || ! IS_BAD_PTR(ret_val));
                            }
                        }
                        else {
                            error = _exec_function(new_stack_frame);
                            assert( ! error || KOS_is_exception_pending(ctx));
                        }

                        ctx->stack_frame        = TO_OBJPTR(stack_frame);
                        new_stack_frame->parent = KOS_VOID;

                        out = _finish_call(ctx, instr, func, this_obj, new_stack_frame, &gen_state);

                        if (instr == INSTR_CALL_GEN) {
                            const KOS_OBJ_PTR result = KOS_BOOL(gen_state == KOS_GEN_DONE);
                            if (rthis == rdest)
                                out = result;
                            else {
                                assert(rthis < regs_array->length);
                                regs[rthis] = result;
                            }
                        }
                    }
                }

                switch (instr) {
                    case INSTR_NEW:       delta = 4; break;
                    case INSTR_TAIL_CALL: delta = 0; break;
                    default:              delta = 5; break;
                }
                break;
            }

            case INSTR_RETURN: { /* <closure.size.int>, <r.src> */
                const unsigned closure_size = bytecode[1];
                const unsigned rsrc         = bytecode[2];

                assert(closure_size <= regs_array->length);
                assert(rsrc         <  regs_array->length);

                stack_frame->retval = regs[rsrc];

                regs_array->length = closure_size;

                error = KOS_SUCCESS_RETURN;
                break;
            }

            case INSTR_YIELD: { /* <r.src> */
                const unsigned rsrc = bytecode[1];

                assert(rsrc < regs_array->length);

                if (stack_frame->yield_reg == KOS_CANNOT_YIELD)
                    KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_yield));
                else {
                    assert(stack_frame->yield_reg == KOS_CAN_YIELD);

                    stack_frame->retval    = regs[rsrc];
                    stack_frame->yield_reg = rsrc;

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

                assert(rsrc < regs_array->length);

                KOS_raise_exception(ctx, regs[rsrc]);

                delta = 2;
                break;
            }

            case INSTR_CATCH: { /* <r.dest>, <delta32> */
                const int32_t  rel_offs = (int32_t)_load_32(bytecode+2);
                const uint32_t offset   = (uint32_t)((bytecode + 6 + rel_offs) - module->bytecode);

                rdest = bytecode[1];

                assert(rdest  < regs_array->length);
                assert(offset < module->bytecode_size);

                stack_frame->catch_reg  = (uint8_t)rdest;
                stack_frame->catch_offs = offset;

                delta = 6;
                break;
            }

            case INSTR_CATCH_CANCEL: {
                stack_frame->catch_offs = KOS_NO_CATCH;

                delta = 1;
                break;
            }

            default: {
                assert(0);
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_invalid_instruction));
                break;
            }
        }

        if ( ! KOS_is_exception_pending(ctx)) {
            if ( ! IS_BAD_PTR(out)) {
                assert(rdest < regs_array->length);
                regs[rdest] = out;
            }
        }
        else {
            assert(ctx->stack_frame == TO_OBJPTR(stack_frame));

            error = KOS_ERROR_EXCEPTION;

            stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

            _KOS_wrap_exception(ctx, stack_frame);

            if (stack_frame->catch_offs != KOS_NO_CATCH) {
                const unsigned rexc = stack_frame->catch_reg;

                assert(rexc < regs_array->length);

                regs[rexc] = KOS_get_exception(ctx);
                delta      = 0;
                bytecode   = module->bytecode + stack_frame->catch_offs;
                error      = KOS_SUCCESS;

                stack_frame->catch_offs = KOS_NO_CATCH;
                KOS_clear_exception(ctx);
            }
        }

        if (error)
            break;

        bytecode += delta;

        assert((uint32_t)(bytecode - module->bytecode) < module->bytecode_size);
    }

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

    assert(!error || KOS_is_exception_pending(ctx));

    stack_frame->instr_offs = (uint32_t)(bytecode - module->bytecode);

    return error;
}

KOS_OBJ_PTR KOS_call_function(KOS_CONTEXT *ctx,
                              KOS_OBJ_PTR  func_obj,
                              KOS_OBJ_PTR  this_obj,
                              KOS_OBJ_PTR  args_obj)
{
    int           error            = KOS_SUCCESS;
    KOS_OBJ_PTR   orig_stack_frame = ctx->stack_frame;
    KOS_OBJ_PTR   ret              = TO_OBJPTR(0);
    KOS_FUNCTION *func;

    TRY(_prepare_call(ctx, INSTR_CALL, func_obj, &this_obj, args_obj));

    func = OBJPTR(KOS_FUNCTION, func_obj);

    if (func->generator_state == KOS_GEN_INIT)
        ret = this_obj;

    else {
        KOS_STACK_FRAME          *new_stack_frame = OBJPTR(KOS_STACK_FRAME, ctx->stack_frame);
        enum _KOS_GENERATOR_STATE gen_state       = func->generator_state;

        if (func->handler)  {
            const KOS_OBJ_PTR retval = func->handler(ctx, this_obj, args_obj);

            /* Avoid detecting as end of iterator */
            if (gen_state != KOS_NOT_GEN && ! IS_BAD_PTR(retval))
                new_stack_frame->yield_reg = 0;

            new_stack_frame->retval = retval;

            if (KOS_is_exception_pending(ctx)) {
                assert(IS_BAD_PTR(retval));
                error = KOS_ERROR_EXCEPTION;
                _KOS_wrap_exception(ctx, new_stack_frame);
            }
            else {
                assert(gen_state > KOS_GEN_INIT || ! IS_BAD_PTR(retval));
            }
        }
        else {
            error = _exec_function(new_stack_frame);
            assert( ! error || KOS_is_exception_pending(ctx));
        }

        ctx->stack_frame        = orig_stack_frame;
        new_stack_frame->parent = KOS_VOID;

        ret = _finish_call(ctx, INSTR_CALL_GEN, func, this_obj, new_stack_frame, &gen_state);

        if (gen_state == KOS_GEN_DONE)
            ret = TO_OBJPTR(0);
    }

_error:
    return error ? TO_OBJPTR(0) : ret;
}

int _KOS_vm_run_module(struct _KOS_MODULE *module)
{
    KOS_STACK_FRAME *const stack_frame =
            _KOS_stack_frame_push(module->context,
                                  TO_OBJPTR(module),
                                  module->instr_offs,
                                  module->num_regs);

    int error = KOS_SUCCESS;

    if (stack_frame) {
        KOS_STACK_FRAME *root_stack_frame = &module->context->root_stack_frame;

        error = _exec_function(stack_frame);

        assert(!KOS_is_exception_pending(module->context) || error == KOS_ERROR_EXCEPTION);

        assert(module->context->stack_frame == TO_OBJPTR(stack_frame));

        root_stack_frame->retval     = stack_frame->retval;
        module->context->stack_frame = TO_OBJPTR(root_stack_frame);
    }
    else {
        assert(KOS_is_exception_pending(module->context));
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

/*
 * Copyright (c) 2014-2020 Chris Dragan
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
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_const_strings.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "kos_vm.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_args_not_array,      "function arguments are not an array");
KOS_DECLARE_STATIC_CONST_STRING(str_err_cannot_yield,        "function is not a generator");
KOS_DECLARE_STATIC_CONST_STRING(str_err_corrupted_defaults,  "argument defaults are corrupted");
KOS_DECLARE_STATIC_CONST_STRING(str_err_div_by_zero,         "division by zero");
KOS_DECLARE_STATIC_CONST_STRING(str_err_generator_running,   "generator is running");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_byte_value,  "buffer element value out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_index,       "index out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_instruction, "invalid instruction");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_callable,        "object is not callable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_class,           "base object is not a class");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_generator,       "function is not a generator");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_indexable,       "object is not indexable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_slice_not_function,  "slice is not a function");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_few_args,        "not enough arguments passed to a function");
KOS_DECLARE_STATIC_CONST_STRING(str_err_unsup_operand_types, "unsupported operand types");

DECLARE_STATIC_CONST_OBJECT(new_this, OBJ_OPAQUE, 0xC0);

#define NEW_THIS KOS_CONST_ID(new_this)

static KOS_OBJ_ID _add_integer(KOS_CONTEXT ctx,
                               int64_t     a,
                               KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(ctx, a + GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a + OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, a + OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _add_float(KOS_CONTEXT ctx,
                             double      a,
                             KOS_OBJ_ID  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)(OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            b = OBJPTR(FLOAT, bobj)->value;
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            return KOS_BADPTR;
    }

    return KOS_new_float(ctx, a + b);
}

static KOS_OBJ_ID _sub_integer(KOS_CONTEXT ctx,
                               int64_t     a,
                               KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(ctx, a - GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a - OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, a - OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _sub_float(KOS_CONTEXT ctx,
                             double      a,
                             KOS_OBJ_ID  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)(OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            b = OBJPTR(FLOAT, bobj)->value;
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            return KOS_BADPTR;
    }

    return KOS_new_float(ctx, a - b);
}

static KOS_OBJ_ID _mul_integer(KOS_CONTEXT ctx,
                               int64_t     a,
                               KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(ctx, a * GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a * OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, a * OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _mul_float(KOS_CONTEXT ctx,
                             double      a,
                             KOS_OBJ_ID  bobj)
{
    double b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)(OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            b = OBJPTR(FLOAT, bobj)->value;
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            return KOS_BADPTR;
    }

    return KOS_new_float(ctx, a * b);
}

static KOS_OBJ_ID _div_integer(KOS_CONTEXT ctx,
                               int64_t     a,
                               KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj)) {

        const int64_t b = GET_SMALL_INT(bobj);

        if (b)
            ret = KOS_new_int(ctx, a / b);
        else {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
            ret = KOS_BADPTR;
        }
    }
    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_FLOAT: {

            const double b = OBJPTR(FLOAT, bobj)->value;

            if (b != 0)
                ret = KOS_new_float(ctx, (double)a / b);
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

        case OBJ_INTEGER: {

            const int64_t b = OBJPTR(INTEGER, bobj)->value;

            if (b)
                ret = KOS_new_int(ctx, a / b);
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _div_float(KOS_CONTEXT ctx,
                             double      a,
                             KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;
    double     b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)(OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            b = OBJPTR(FLOAT, bobj)->value;
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            return KOS_BADPTR;
    }

    if (b != 0)
        ret = KOS_new_float(ctx, a / b);
    else {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _mod_integer(KOS_CONTEXT ctx,
                               int64_t     a,
                               KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj)) {

        const int64_t b = GET_SMALL_INT(bobj);

        if (b)
            ret = KOS_new_int(ctx, a % b);
        else {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
            ret = KOS_BADPTR;
        }
    }
    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_FLOAT: {

            const double b = OBJPTR(FLOAT, bobj)->value;

            if (b != 0)
                ret = KOS_new_float(ctx, fmod((double)a, b));
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

        case OBJ_INTEGER: {

            const int64_t b = OBJPTR(INTEGER, bobj)->value;

            if (b)
                ret = KOS_new_int(ctx, a % b);
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _mod_float(KOS_CONTEXT ctx,
                             double      a,
                             KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;
    double     b;

    if (IS_SMALL_INT(bobj))
        b = (double)GET_SMALL_INT(bobj);

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            b = (double)(OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            b = OBJPTR(FLOAT, bobj)->value;
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            return KOS_BADPTR;
    }

    if (b != 0)
        ret = KOS_new_float(ctx, fmod(a, b));
    else {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
        ret = KOS_BADPTR;
    }

    return ret;
}

static int _is_generator_end_exception(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst      = ctx->inst;
    const KOS_OBJ_ID    exception = KOS_get_exception(ctx);
    int                 ret       = 0;

    if (KOS_get_prototype(ctx, exception) == inst->prototypes.exception_proto) {

        KOS_OBJ_ID value;

        KOS_clear_exception(ctx);

        kos_track_refs(ctx, 1, &exception);
        value = KOS_get_property(ctx, exception, KOS_STR_VALUE);
        kos_untrack_refs(ctx, 1);

        if (IS_BAD_PTR(value)) {
            KOS_clear_exception(ctx);
            KOS_raise_exception(ctx, exception);
        }
        else if (KOS_get_prototype(ctx, value) != inst->prototypes.generator_end_proto)
            KOS_raise_exception(ctx, exception);
        else
            ret = 1;
    }

    return ret;
}

#ifndef NDEBUG
static uint32_t get_num_regs(KOS_OBJ_ID stack, uint32_t regs_idx)
{
    uint32_t size;

    assert( ! IS_BAD_PTR(stack));
    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);
    assert(regs_idx + 1U < size);

    return size - 1U - regs_idx;
}
#endif

static KOS_OBJ_ID make_args(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  stack,
                            uint32_t    regs_idx,
                            unsigned    num_args)
{
    KOS_OBJ_ID array_obj;

    kos_track_refs(ctx, 1, &stack);
    array_obj = KOS_new_array(ctx, num_args);
    kos_untrack_refs(ctx, 1);

    if ( ! IS_BAD_PTR(array_obj) && num_args)
        kos_atomic_move_ptr((KOS_ATOMIC(void *) *)kos_get_array_buffer(OBJPTR(ARRAY, array_obj)),
                            (KOS_ATOMIC(void *) *)&OBJPTR(STACK, stack)->buf[regs_idx],
                            num_args);

    return array_obj;
}

static KOS_OBJ_ID slice_args(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  args_obj,
                             KOS_OBJ_ID  stack,
                             uint32_t    rarg1_idx,
                             unsigned    num_args,
                             unsigned    slice_begin,
                             unsigned    slice_end)
{
    unsigned   size = 0;
    KOS_OBJ_ID new_args;

    if ( ! IS_BAD_PTR(args_obj))
        num_args = KOS_get_array_size(args_obj);

    if (slice_begin < num_args && slice_begin < slice_end) {

        if (slice_end > num_args)
            slice_end = num_args;

        size = slice_end - slice_begin;
    }

    kos_track_refs(ctx, 2, &args_obj, &stack);

    new_args = KOS_new_array(ctx, size);

    kos_untrack_refs(ctx, 2);

    if ( ! IS_BAD_PTR(new_args) && size)
        kos_atomic_move_ptr((KOS_ATOMIC(void *) *)kos_get_array_buffer(OBJPTR(ARRAY, new_args)),
                            (KOS_ATOMIC(void *) *)(slice_begin + (IS_BAD_PTR(args_obj)
                                ? &OBJPTR(STACK, stack)->buf[rarg1_idx]
                                : kos_get_array_buffer(OBJPTR(ARRAY, args_obj)))),
                            size);

    return new_args;
}

static int init_registers(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  func_obj,
                          KOS_OBJ_ID  args_obj,
                          KOS_OBJ_ID  stack_obj,
                          uint32_t    rarg1_idx,
                          unsigned    num_args,
                          KOS_OBJ_ID  this_obj)
{
    int            error            = KOS_SUCCESS;
    uint32_t       reg              = OBJPTR(FUNCTION, func_obj)->opts.args_reg;
    const uint32_t num_non_def_args = OBJPTR(FUNCTION, func_obj)->opts.min_args;
    const uint32_t num_def_args     = GET_OBJ_TYPE(OBJPTR(FUNCTION, func_obj)->defaults) == OBJ_ARRAY
                                       ? KOS_get_array_size(OBJPTR(FUNCTION, func_obj)->defaults) : 0;
    const uint32_t num_named_args   = num_non_def_args + num_def_args;
    const uint32_t num_arg_regs     = KOS_min(num_named_args, KOS_MAX_ARGS_IN_REGS);
    const uint32_t num_input_args   = IS_BAD_PTR(args_obj) ? num_args : KOS_get_array_size(args_obj);

    KOS_LOCAL      func;
    KOS_LOCAL      args;
    KOS_LOCAL      stack;
    KOS_LOCAL      this_;
    KOS_LOCAL      ellipsis;
    KOS_LOCAL      rest;

    KOS_init_locals(ctx, 6, &func, &args, &stack, &this_, &ellipsis, &rest);

    this_.o = this_obj;
    stack.o = stack_obj;
    args.o  = args_obj;
    func.o  = func_obj;

    assert(GET_OBJ_TYPE(this_.o) <= OBJ_LAST_TYPE);
    assert( ! IS_BAD_PTR(args.o) || GET_OBJ_TYPE(stack.o) == OBJ_STACK);
    assert( ! OBJPTR(FUNCTION, func.o)->handler);
    assert(OBJPTR(FUNCTION, func_obj)->opts.num_def_args == num_def_args);

    if (OBJPTR(FUNCTION, func.o)->opts.ellipsis_reg != KOS_NO_REG) {
        /* args, ellipsis, this */
        assert(OBJPTR(FUNCTION, func.o)->opts.num_regs >= reg + num_arg_regs + 2);
    }
    else {
        /* args, this */
        assert(OBJPTR(FUNCTION, func.o)->opts.num_regs >= reg + num_arg_regs + 1);
    }
    assert(num_input_args >= num_non_def_args);

    if (OBJPTR(FUNCTION, func.o)->opts.ellipsis_reg != KOS_NO_REG)  {
        if (num_input_args > num_arg_regs)
            ellipsis.o = slice_args(ctx, args.o, stack.o, rarg1_idx, num_args, num_named_args, ~0U);
        else
            ellipsis.o = KOS_new_array(ctx, 0);
        TRY_OBJID(ellipsis.o);
    }

    reg += ctx->regs_idx;

    if (num_named_args <= KOS_MAX_ARGS_IN_REGS) {

        const uint32_t num_to_move = KOS_min(num_input_args, num_named_args);

        assert(reg - ctx->regs_idx + num_to_move <= get_num_regs(ctx->stack, ctx->regs_idx));

        if (num_to_move) {
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&OBJPTR(STACK, ctx->stack)->buf[reg],
                                (KOS_ATOMIC(void *) *)(IS_BAD_PTR(args.o)
                                    ? &OBJPTR(STACK, stack.o)->buf[rarg1_idx]
                                    : kos_get_array_buffer(OBJPTR(ARRAY, args.o))),
                                num_to_move);
            reg += num_to_move;
        }

        if (num_to_move < num_named_args) {

            KOS_ATOMIC(KOS_OBJ_ID) *src_buf = kos_get_array_buffer(OBJPTR(ARRAY, OBJPTR(FUNCTION, func.o)->defaults));
            KOS_ATOMIC(KOS_OBJ_ID) *src_end = src_buf + num_def_args;

            assert(num_def_args);

            src_buf += num_to_move - num_non_def_args;

            do
                KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, ctx->stack)->buf[reg++],
                                     KOS_atomic_read_relaxed_ptr(*(src_buf++)));
            while (src_buf < src_end);
        }

        assert(OBJPTR(FUNCTION, func_obj)->opts.rest_reg == KOS_NO_REG);
    }
    else {
        const uint32_t num_to_move = KOS_min(num_input_args, KOS_MAX_ARGS_IN_REGS - 1U);

        rest.o = slice_args(ctx, args.o, stack.o, rarg1_idx, num_args, KOS_MAX_ARGS_IN_REGS - 1, num_named_args);
        TRY_OBJID(rest.o);

        if (num_to_move) {
            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&OBJPTR(STACK, ctx->stack)->buf[reg],
                                (KOS_ATOMIC(void *) *)(IS_BAD_PTR(args.o)
                                    ? &OBJPTR(STACK, stack.o)->buf[rarg1_idx]
                                    : kos_get_array_buffer(OBJPTR(ARRAY, args.o))),
                                num_to_move);
            reg += num_to_move;
        }

        if (num_input_args < num_named_args) {

            uint32_t src_offs = 0;

            if (num_to_move > num_non_def_args)
                src_offs += num_to_move - num_non_def_args;

            if (num_to_move < KOS_MAX_ARGS_IN_REGS - 1U) {

                const uint32_t to_copy  = KOS_MAX_ARGS_IN_REGS - 1U - num_to_move;
                KOS_OBJ_ID     defaults = OBJPTR(FUNCTION, func.o)->defaults;

                defaults = kos_get_array_storage(defaults);

                kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&OBJPTR(STACK, ctx->stack)->buf[reg],
                                    (KOS_ATOMIC(void *) *)&OBJPTR(ARRAY_STORAGE, defaults)->buf[src_offs],
                                    to_copy);

                src_offs += to_copy;
                reg      += to_copy;
            }

            TRY(KOS_array_insert(ctx,
                                 rest.o,
                                 MAX_INT64,
                                 MAX_INT64,
                                 OBJPTR(FUNCTION, func.o)->defaults,
                                 src_offs,
                                 MAX_INT64));
        }

        assert(reg - ctx->regs_idx == OBJPTR(FUNCTION, func_obj)->opts.rest_reg);
        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, ctx->stack)->buf[reg++], rest.o);
    }

    if ( ! IS_BAD_PTR(ellipsis.o)) {
        assert(reg - ctx->regs_idx == OBJPTR(FUNCTION, func_obj)->opts.ellipsis_reg);
        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, ctx->stack)->buf[reg++], ellipsis.o);
    }

    assert(OBJPTR(FUNCTION, func_obj)->opts.this_reg == KOS_NO_REG ||
           OBJPTR(FUNCTION, func_obj)->opts.this_reg == reg - ctx->regs_idx);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, ctx->stack)->buf[reg++], this_.o);

    assert( ! IS_BAD_PTR(OBJPTR(FUNCTION, func.o)->closures));

    if (GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->closures) == OBJ_ARRAY) {
        KOS_ATOMIC(KOS_OBJ_ID) *src_buf;
        KOS_ATOMIC(KOS_OBJ_ID) *end;
        uint32_t                src_len;

        src_len = KOS_get_array_size(OBJPTR(FUNCTION, func.o)->closures);

        assert(src_len > 0);
        assert(reg - ctx->regs_idx + src_len <= 256U);
        assert(reg - ctx->regs_idx + src_len <= get_num_regs(ctx->stack, ctx->regs_idx));

        src_buf = kos_get_array_buffer(OBJPTR(ARRAY, OBJPTR(FUNCTION, func.o)->closures));
        end     = src_buf + src_len;

        assert(reg - ctx->regs_idx == OBJPTR(FUNCTION, func_obj)->opts.bind_reg);
        assert(OBJPTR(FUNCTION, func_obj)->opts.num_binds == src_len);

        while (src_buf < end)
            KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, ctx->stack)->buf[reg++],
                                 KOS_atomic_read_relaxed_obj(*(src_buf++)));
    }
    else {
        assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->closures) == OBJ_VOID);
        assert(OBJPTR(FUNCTION, func_obj)->opts.bind_reg  == KOS_NO_REG);
        assert(OBJPTR(FUNCTION, func_obj)->opts.num_binds == 0);
    }

cleanup:
    KOS_destroy_top_locals(ctx, &func, &rest);
    return error;
}

static void set_handler_reg(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = ctx->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(OBJPTR(STACK, stack)->flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1]) == TO_SMALL_INT(1));
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[size - 2], obj_id);
}

static KOS_OBJ_ID get_handler_reg(KOS_CONTEXT ctx)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = ctx->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(OBJPTR(STACK, stack)->flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1]) == TO_SMALL_INT(1));

    return KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 2]);
}

static void write_to_yield_reg(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id)
{
    const KOS_OBJ_ID stack     = ctx->stack;
    const uint32_t   yield_reg = (uint32_t)OBJPTR(STACK, stack)->yield_reg;

    assert(yield_reg < get_num_regs(stack, ctx->regs_idx));

    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[ctx->regs_idx + yield_reg], obj_id);
}

static int _prepare_call(KOS_CONTEXT        ctx,
                         KOS_BYTECODE_INSTR instr,
                         KOS_OBJ_ID         func_obj,
                         KOS_OBJ_ID        *this_obj,
                         KOS_OBJ_ID         args_obj,
                         uint32_t           rarg1,
                         unsigned           num_args)
{
    int                error    = KOS_SUCCESS;
    KOS_FUNCTION_STATE state;
    const uint32_t     regs_idx = ctx->regs_idx;
    KOS_LOCAL          func;
    KOS_LOCAL          args;
    KOS_LOCAL          stack;

    KOS_init_local_with(ctx, &stack, ctx->stack);
    KOS_init_local_with(ctx, &args, args_obj);
    KOS_init_local_with(ctx, &func, func_obj);

    assert( ! IS_BAD_PTR(func.o));
    if (IS_BAD_PTR(args.o)) {
        if ( ! num_args) {
            assert( ! rarg1);
        }
    }
    else {
        assert( ! num_args && ! rarg1);
    }

    assert(GET_OBJ_TYPE(func.o) == OBJ_FUNCTION ||
           GET_OBJ_TYPE(func.o) == OBJ_CLASS);

    state = (GET_OBJ_TYPE(func.o) == OBJ_FUNCTION)
        ? (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state)
        : KOS_CTOR;

    assert(GET_OBJ_TYPE(func.o) == OBJ_CLASS || state != KOS_CTOR);

    if (IS_BAD_PTR(args.o)) {
        if (num_args < OBJPTR(FUNCTION, func.o)->opts.min_args)
            RAISE_EXCEPTION_STR(str_err_too_few_args);
    }
    else {
        if (GET_OBJ_TYPE(args.o) != OBJ_ARRAY)
            RAISE_EXCEPTION_STR(str_err_args_not_array);

        if (KOS_get_array_size(args.o) < OBJPTR(FUNCTION, func.o)->opts.min_args)
            RAISE_EXCEPTION_STR(str_err_too_few_args);
    }

    if (instr == INSTR_CALL_GEN && state < KOS_GEN_READY)
        RAISE_EXCEPTION_STR(str_err_not_generator);

    switch (state) {

        /* Constructor function */
        case KOS_CTOR:
            if (*this_obj == NEW_THIS) {
                KOS_CLASS *const class_ptr = OBJPTR(CLASS, func.o);
                const KOS_OBJ_ID proto_obj = KOS_atomic_read_relaxed_obj(class_ptr->prototype);
                assert( ! IS_BAD_PTR(proto_obj));

                if (OBJPTR(FUNCTION, func.o)->handler)
                    *this_obj = proto_obj;
                else {
                    *this_obj = KOS_new_object_with_prototype(ctx, proto_obj);
                    TRY_OBJID(*this_obj);
                }
            }
            /* fall through */

        /* Regular function */
        case KOS_FUN: {
            TRY(kos_stack_push(ctx, func.o));

            if ( ! OBJPTR(FUNCTION, func.o)->handler)
                TRY(init_registers(ctx,
                                   func.o,
                                   args.o,
                                   stack.o,
                                   regs_idx + rarg1,
                                   num_args,
                                   *this_obj));

            break;
        }

        /* Instantiate a generator function */
        case KOS_GEN_INIT: {
            func.o = kos_copy_function(ctx, func.o);
            TRY_OBJID(func.o);

            TRY(kos_stack_push(ctx, func.o));

            KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func.o)->state, KOS_GEN_READY);

            if ( ! OBJPTR(FUNCTION, func.o)->handler)
                TRY(init_registers(ctx,
                                   func.o,
                                   args.o,
                                   stack.o,
                                   regs_idx + rarg1,
                                   num_args,
                                   *this_obj));
            else {
                if (IS_BAD_PTR(args.o)) {
                    args.o = make_args(ctx, stack.o, regs_idx + rarg1, num_args);
                    TRY_OBJID(args.o);
                }
                set_handler_reg(ctx, args.o);
            }

            OBJPTR(FUNCTION, func.o)->opts.min_args = 0;

            OBJPTR(STACK, ctx->stack)->flags |= KOS_CAN_YIELD;

            kos_stack_pop(ctx);

            assert(ctx->regs_idx == regs_idx);

            *this_obj = func.o;
            break;
        }

        /* Generator function */
        case KOS_GEN_READY:
            /* fall through */
        case KOS_GEN_ACTIVE: {
            assert( ! IS_BAD_PTR(OBJPTR(FUNCTION, func.o)->generator_stack_frame));

            TRY(kos_stack_push(ctx, func.o));

            if ( ! OBJPTR(FUNCTION, func.o)->handler) {
                if (state == KOS_GEN_ACTIVE) {

                    KOS_OBJ_ID value;

                    if (IS_BAD_PTR(args.o))
                        value = num_args ? KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack.o)->buf[regs_idx + rarg1]) : KOS_VOID;

                    else {

                        num_args = KOS_get_array_size(args.o);

                        value = num_args ? KOS_array_read(ctx, args.o, 0) : KOS_VOID;
                    }

                    write_to_yield_reg(ctx, value);
                }
            }
            else
                *this_obj = get_handler_reg(ctx);

            if ( ! KOS_atomic_cas_strong_u32(OBJPTR(FUNCTION, func.o)->state, state, KOS_GEN_RUNNING)) {
                /* TODO error */
            }

            OBJPTR(STACK, ctx->stack)->flags |= KOS_CAN_YIELD;
            break;
        }

        case KOS_GEN_RUNNING:
            RAISE_EXCEPTION_STR(str_err_generator_running);

        default:
            assert(state == KOS_GEN_DONE);
            KOS_raise_generator_end(ctx);
            error = KOS_ERROR_EXCEPTION;
    }

cleanup:
    if (error && (stack.o != ctx->stack || regs_idx != ctx->regs_idx))
        kos_stack_pop(ctx);
    KOS_destroy_top_locals(ctx, &func, &stack);
    return error;
}

static KOS_OBJ_ID _finish_call(KOS_CONTEXT         ctx,
                               KOS_BYTECODE_INSTR  instr,
                               KOS_OBJ_ID          func_obj,
                               KOS_OBJ_ID          this_obj,
                               KOS_FUNCTION_STATE *state)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if ( ! KOS_is_exception_pending(ctx)) {

        if (READ_OBJ_TYPE(func_obj) == OBJ_CLASS
                && ! OBJPTR(FUNCTION, func_obj)->handler)
            ret = this_obj;
        else
            ret = ctx->retval;

        assert(IS_BAD_PTR(ret) || GET_OBJ_TYPE(ret) <= OBJ_LAST_TYPE);

        if (*state >= KOS_GEN_INIT) {
            if (OBJPTR(STACK, ctx->stack)->flags & KOS_CAN_YIELD) {
                *state = KOS_GEN_DONE;
                KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func_obj)->state, KOS_GEN_DONE);
                if (instr != INSTR_CALL_GEN)
                    KOS_raise_generator_end(ctx);
            }
            else {
                const KOS_FUNCTION_STATE end_state =
                    OBJPTR(FUNCTION, func_obj)->handler ? KOS_GEN_READY : KOS_GEN_ACTIVE;

                *state = end_state;
                KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func_obj)->state, (uint32_t)end_state);
            }
        }
    }
    else {
        if (*state >= KOS_GEN_INIT) {
            *state = KOS_GEN_DONE;
            KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func_obj)->state, KOS_GEN_DONE);
        }
    }

    ctx->retval = KOS_BADPTR;

    kos_stack_pop(ctx);

    return ret;
}

static KOS_OBJ_ID _read_buffer(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    size = KOS_get_buffer_size(objptr);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
        ret = KOS_BADPTR;
    }
    else {
        uint8_t *const buf = KOS_buffer_data_volatile(objptr);
        ret = TO_SMALL_INT((int)buf[idx]);
    }

    return ret;
}

static int _write_buffer(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error;
    uint32_t size;
    int64_t  byte_value;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    TRY(KOS_get_integer(ctx, value, &byte_value));

    if (byte_value < 0 || byte_value > 255)
        RAISE_EXCEPTION_STR(str_err_invalid_byte_value);

    size = KOS_get_buffer_size(objptr);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)
        RAISE_EXCEPTION_STR(str_err_invalid_index);
    else {
        uint8_t *const buf = KOS_buffer_data_volatile(objptr);
        buf[idx] = (uint8_t)byte_value;
    }

cleanup:
    return error;
}

static KOS_OBJ_ID _read_stack(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(OBJPTR(STACK, objptr)->flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, objptr)->size);
    assert(size >= 1 + KOS_STACK_EXTRA);

    if (idx < 0)
        idx += (int)size - 1;
    else
        idx += KOS_STACK_EXTRA;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
        ret = KOS_VOID;
    }
    else
        ret = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, objptr)->buf[idx]);

    return ret;
}

static int _write_stack(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error = KOS_SUCCESS;
    uint32_t size;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(OBJPTR(STACK, objptr)->flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, objptr)->size);
    assert(size >= 1 + KOS_STACK_EXTRA);

    if (idx < 0)
        idx += (int)size - 1;
    else
        idx += KOS_STACK_EXTRA;


    if ((uint32_t)idx >= size) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));
        error = KOS_ERROR_EXCEPTION;
    }
    else
        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, objptr)->buf[idx], value);

    return error;
}

static void set_closure_stack_size(KOS_CONTEXT ctx, unsigned closure_size)
{
    const KOS_OBJ_ID stack = ctx->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);

#ifndef NDEBUG
    {
        KOS_OBJ_ID func_obj;

        assert(ctx->regs_idx >= 3);

        func_obj = OBJPTR(STACK, ctx->stack)->buf[ctx->regs_idx - 3];

        assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION ||
               GET_OBJ_TYPE(func_obj) == OBJ_CLASS);

#if 0 /* TODO Fix handling of BIND.SELF in kos_vm_test */
        assert(OBJPTR(FUNCTION, func_obj)->opts.closure_size == closure_size);
#endif
    }
#endif

    if (OBJPTR(STACK, stack)->flags & KOS_REENTRANT_STACK) {

        const uint32_t size     = closure_size + 1U + KOS_STACK_EXTRA;
        const uint32_t old_size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);

        assert(size <= old_size);

        KOS_atomic_write_relaxed_u32(OBJPTR(STACK, stack)->size, size);
        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[size - 1], TO_SMALL_INT((int)closure_size));

        ctx->stack_depth -= old_size - size;
    }
}

KOS_OBJ_ID KOS_get_module(KOS_CONTEXT ctx)
{
    KOS_OBJ_ID func_obj;
    KOS_OBJ_ID module_obj;

    assert(ctx->regs_idx >= 3);

    func_obj = OBJPTR(STACK, ctx->stack)->buf[ctx->regs_idx - 3];

    assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION ||
           GET_OBJ_TYPE(func_obj) == OBJ_CLASS);

    module_obj = OBJPTR(FUNCTION, func_obj)->module;

    assert( ! IS_BAD_PTR(module_obj));
    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    return module_obj;
}

static int64_t load_instr_offs(KOS_OBJ_ID stack,
                               uint32_t   regs_idx)
{
    KOS_OBJ_ID instr_offs;

    assert(regs_idx >= 1);

    instr_offs = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[regs_idx - 1]);

    assert(IS_SMALL_INT(instr_offs));

    return GET_SMALL_INT(instr_offs);
}

static void store_instr_offs(KOS_OBJ_ID stack,
                             uint32_t   regs_idx,
                             uint32_t   instr_offs)
{
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[regs_idx - 1],
                         TO_SMALL_INT((int64_t)instr_offs));
}

static uint32_t get_catch(KOS_OBJ_ID stack,
                          uint32_t   regs_idx,
                          uint8_t   *catch_reg)
{
    const KOS_OBJ_ID catch_data_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[regs_idx - 2]);
    uint64_t         catch_data;
    uint32_t         catch_offs;

    assert(IS_SMALL_INT(catch_data_obj));

    catch_data = (uint64_t)GET_SMALL_INT(catch_data_obj);

    catch_offs = (uint32_t)(catch_data >> 8);
    *catch_reg = (uint8_t)(catch_data & 0xFFU);

    return catch_offs;
}

static void set_catch(KOS_OBJ_ID stack,
                      uint32_t   regs_idx,
                      uint32_t   catch_offs,
                      uint8_t    catch_reg)
{
    const uint64_t catch_data = (((uint64_t)catch_offs << 8) | catch_reg);
    assert(catch_offs < KOS_NO_CATCH);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[regs_idx - 2],
                         TO_SMALL_INT((intptr_t)(int64_t)catch_data));
}

static void clear_catch(KOS_OBJ_ID stack,
                        uint32_t   regs_idx)
{
    const intptr_t catch_data = KOS_NO_CATCH << 8;
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[regs_idx - 2], TO_SMALL_INT(catch_data));
}

static uint32_t _load_32(const uint8_t *bytecode)
{
    return (uint32_t)bytecode[0] +
           ((uint32_t)bytecode[1] << 8) +
           ((uint32_t)bytecode[2] << 16) +
           ((uint32_t)bytecode[3] << 24);
}

#define REGISTER(reg) KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[regs_idx + (reg)])

#define WRITE_REGISTER(reg, value) do {                                       \
    const KOS_OBJ_ID out_val = (value);                                       \
    assert(GET_OBJ_TYPE(out_val) <= OBJ_LAST_TYPE);                           \
    assert(reg < num_regs);                                                   \
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[regs_idx + (reg)], \
                                 out_val);                                    \
} while (0)

#if KOS_DISPATCH_TABLE
#   define BEGIN_INSTRUCTION(instr)     OP_##instr
#   define BEGIN_BREAKPOINT_INSTRUCTION OP_BREAKPOINT
#   define NEXT_INSTRUCTION             KOS_PERF_CNT(instructions);                              \
                                        instr     = (KOS_BYTECODE_INSTR)*bytecode;               \
                                        jump_offs = (uint8_t)(instr - INSTR_BREAKPOINT);         \
                                        if (jump_offs >= (INSTR_LAST_OPCODE - INSTR_BREAKPOINT)) \
                                            goto OP_BREAKPOINT;                                  \
                                        goto* dispatch_table[jump_offs];
#else
#   define BEGIN_INSTRUCTION(instr)     case INSTR_##instr
#   define BEGIN_BREAKPOINT_INSTRUCTION default
#   define NEXT_INSTRUCTION             break
#endif

static int exec_function(KOS_CONTEXT ctx)
{
    const uint8_t     *bytecode;
    KOS_BYTECODE_INSTR instr;
    KOS_OBJ_ID         out      = KOS_BADPTR;
    KOS_OBJ_ID         module;
    KOS_OBJ_ID         stack    = ctx->stack;
    int                error    = KOS_SUCCESS;
    uint32_t           regs_idx = ctx->regs_idx;
    unsigned           rdest;
#ifndef NDEBUG
    const uint32_t     num_regs = get_num_regs(stack, regs_idx);
#endif

#if KOS_DISPATCH_TABLE
    uint8_t            jump_offs;
    static void       *dispatch_table[] = {
#   define DEFINE_INSTRUCTION(name, value) &&OP_##name,
#   include "../inc/kos_opcodes.h"
#   undef DEFINE_INSTRUCTION
    };
#endif

    module = KOS_get_module(ctx);

    assert( ! IS_BAD_PTR(module));
    assert(OBJPTR(MODULE, module)->inst);
    bytecode = OBJPTR(MODULE, module)->bytecode + load_instr_offs(stack, regs_idx);

    assert( ! kos_is_heap_object(module));
    assert( ! kos_is_heap_object(stack));

#if KOS_DISPATCH_TABLE
    NEXT_INSTRUCTION;
#else
    for (;;) {
        assert( ! KOS_is_exception_pending(ctx));
        assert((uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode) < OBJPTR(MODULE, module)->bytecode_size);

        KOS_PERF_CNT(instructions);

        instr = (KOS_BYTECODE_INSTR)*bytecode;

        switch (instr) {
#endif

            BEGIN_INSTRUCTION(LOAD_CONST8): { /* <r.dest>, <uint8> */
                const uint8_t value = bytecode[2];

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, value);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_CONST): { /* <r.dest>, <uint32> */
                const uint32_t value = _load_32(bytecode+2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, value);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_FUN8): { /* <r.dest>, <uint8> */
                const uint8_t value = bytecode[2];

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, value);
                TRY_OBJID(out);

                out = kos_copy_function(ctx, out);
                TRY_OBJID(out);

                if (GET_OBJ_TYPE(out) == OBJ_CLASS) {

                    const KOS_OBJ_ID proto_obj = KOS_array_read(ctx,
                                                                OBJPTR(MODULE, module)->constants,
                                                                (uint32_t)value + 1U);
                    TRY_OBJID(proto_obj);

                    KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, out)->prototype, proto_obj);
                }

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_FUN): { /* <r.dest>, <uint32> */
                const uint32_t value = _load_32(bytecode+2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, value);
                TRY_OBJID(out);

                out = kos_copy_function(ctx, out);
                TRY_OBJID(out);

                if (GET_OBJ_TYPE(out) == OBJ_CLASS) {
                    const KOS_OBJ_ID proto_obj = KOS_array_read(ctx,
                                                                OBJPTR(MODULE, module)->constants,
                                                                value + 1);
                    TRY_OBJID(proto_obj);

                    KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, out)->prototype, proto_obj);
                }

                WRITE_REGISTER(rdest, out);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_INT8): { /* <r.dest>, <int8> */
                const int8_t value = (int8_t)bytecode[2];

                rdest = bytecode[1];

                WRITE_REGISTER(rdest, TO_SMALL_INT(value));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_TRUE): { /* <r.dest> */
                rdest = bytecode[1];

                WRITE_REGISTER(rdest, KOS_TRUE);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_FALSE): { /* <r.dest> */
                rdest = bytecode[1];

                WRITE_REGISTER(rdest, KOS_FALSE);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_VOID): { /* <r.dest> */
                rdest = bytecode[1];

                WRITE_REGISTER(rdest, KOS_VOID);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_ARRAY8): { /* <r.dest>, <size.uint8> */
                const uint8_t size = bytecode[2];

                rdest = bytecode[1];

                out = KOS_new_array(ctx, size);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_ARRAY): { /* <r.dest>, <size.int32> */
                const uint32_t size = _load_32(bytecode+2);

                rdest = bytecode[1];

                out = KOS_new_array(ctx, size);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_OBJ): { /* <r.dest> */
                rdest = bytecode[1];

                out = KOS_new_object(ctx);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_OBJ_PROTO): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                assert(rsrc < num_regs);

                out = KOS_new_object_with_prototype(ctx, REGISTER(rsrc));
                TRY_OBJID(out);

                rdest = bytecode[1];

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MOVE): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                assert(rsrc < num_regs);

                rdest = bytecode[1];

                out = REGISTER(rsrc);

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_PROTO): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];
                KOS_OBJ_ID     constr_obj;

                assert(rsrc < num_regs);

                constr_obj = REGISTER(rsrc);

                if (GET_OBJ_TYPE(constr_obj) == OBJ_CLASS) {

                    out = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, constr_obj)->prototype);

                    assert( ! IS_BAD_PTR(out));
                }
                else if (constr_obj == KOS_VOID)
                    out = KOS_VOID;

                else
                    RAISE_EXCEPTION_STR(str_err_not_class);

                rdest = bytecode[1];

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_GLOBAL): { /* <r.dest>, <int32> */
                const int32_t  idx = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->globals, idx);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_GLOBAL): { /* <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < num_regs);

                TRY(KOS_array_write(ctx,
                                    OBJPTR(MODULE, module)->globals,
                                    idx,
                                    REGISTER(rsrc)));

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_MOD): { /* <r.dest>, <int32>, <r.glob> */
                const int      mod_idx    = (int32_t)_load_32(bytecode+2);
                const unsigned rglob      = bytecode[6];
                KOS_OBJ_ID     glob_idx;
                KOS_OBJ_ID     module_obj = KOS_array_read(ctx,
                                                           OBJPTR(MODULE, module)->inst->modules.modules,
                                                           mod_idx);
                TRY_OBJID(module_obj);

                assert(rglob < num_regs);

                rdest = bytecode[1];

                assert( ! IS_SMALL_INT(module_obj));
                assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                glob_idx = KOS_get_property(ctx,
                                            OBJPTR(MODULE, module_obj)->global_names,
                                            REGISTER(rglob));
                TRY_OBJID(glob_idx);

                assert(IS_SMALL_INT(glob_idx));

                out = KOS_array_read(ctx, OBJPTR(MODULE, module_obj)->globals,
                                     (int)GET_SMALL_INT(glob_idx));
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 7;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_MOD_ELEM): { /* <r.dest>, <int32>, <int32> */
                const int  mod_idx    = (int32_t)_load_32(bytecode+2);
                const int  glob_idx   = (int32_t)_load_32(bytecode+6);
                KOS_OBJ_ID module_obj = KOS_array_read(ctx,
                                                       OBJPTR(MODULE, module)->inst->modules.modules,
                                                       mod_idx);
                TRY_OBJID(module_obj);

                rdest = bytecode[1];

                assert( ! IS_SMALL_INT(module_obj));
                assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                out = KOS_array_read(ctx, OBJPTR(MODULE, module_obj)->globals, glob_idx);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 10;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET): { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];
                src   = REGISTER(rsrc);
                prop  = REGISTER(rprop);

                if (IS_NUMERIC_OBJ(prop)) {
                    KOS_TYPE type;
                    int64_t  idx;

                    TRY(KOS_get_integer(ctx, prop, &idx));

                    if (idx > INT_MAX || idx < INT_MIN)
                        RAISE_EXCEPTION_STR(str_err_invalid_index);

                    type = GET_OBJ_TYPE(src);
                    if (type == OBJ_STRING)
                        out = KOS_string_get_char(ctx, src, (int)idx);
                    else if (type == OBJ_BUFFER)
                        out = _read_buffer(ctx, src, (int)idx);
                    else
                        out = KOS_array_read(ctx, src, (int)idx);
                }
                else {
                    out = KOS_get_property(ctx, src, prop);
                    TRY_OBJID(out);

                    if (GET_OBJ_TYPE(out) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args;
                        store_instr_offs(stack, regs_idx,
                                         (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                        out = OBJPTR(DYNAMIC_PROP, out)->getter;

                        kos_track_refs(ctx, 2, &out, &src);
                        args = KOS_new_array(ctx, 0);
                        kos_untrack_refs(ctx, 2);

                        TRY_OBJID(args);

                        out = KOS_call_function(ctx, out, src, args);

                        assert(ctx->regs_idx == regs_idx);
                    }
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_ELEM): { /* <r.dest>, <r.src>, <int32> */
                const unsigned rsrc = bytecode[2];
                const int32_t  idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID     src;
                KOS_TYPE       type;

                assert(rsrc < num_regs);

                rdest = bytecode[1];
                src   = REGISTER(rsrc);

                type = GET_OBJ_TYPE(src);

                if (type == OBJ_ARRAY)
                    out = KOS_array_read(ctx, src, idx);
                else if (type == OBJ_STRING)
                    out = KOS_string_get_char(ctx, src, idx);
                else if (type == OBJ_BUFFER)
                    out = _read_buffer(ctx, src, idx);
                else if (type == OBJ_STACK)
                    out = _read_stack(ctx, src, idx);
                else
                    RAISE_EXCEPTION_STR(str_err_not_indexable);

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 7;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_RANGE): { /* <r.dest>, <r.src>, <r.begin>, <r.end> */
                const unsigned rsrc   = bytecode[2];
                const unsigned rbegin = bytecode[3];
                const unsigned rend   = bytecode[4];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     begin;
                KOS_OBJ_ID     end;
                int64_t        begin_idx;
                int64_t        end_idx = 0;

                KOS_DECLARE_STATIC_CONST_STRING(str_slice, "slice");

                assert(rsrc   < num_regs);
                assert(rbegin < num_regs);
                assert(rend   < num_regs);

                rdest = bytecode[1];
                src   = REGISTER(rsrc);
                begin = REGISTER(rbegin);
                end   = REGISTER(rend);

                if (IS_SMALL_INT(begin) || GET_OBJ_TYPE(begin) != OBJ_VOID)
                    TRY(KOS_get_integer(ctx, begin, &begin_idx));
                else
                    begin_idx = 0;

                if (IS_SMALL_INT(end) || GET_OBJ_TYPE(end) != OBJ_VOID)
                    TRY(KOS_get_integer(ctx, end, &end_idx));
                else
                    end_idx = MAX_INT64;

                if (GET_OBJ_TYPE(src) == OBJ_STRING)
                    out = KOS_string_slice(ctx, src, begin_idx, end_idx);
                else if (GET_OBJ_TYPE(src) == OBJ_BUFFER)
                    out = KOS_buffer_slice(ctx, src, begin_idx, end_idx);
                else if (GET_OBJ_TYPE(src) == OBJ_ARRAY)
                    out = KOS_array_slice(ctx, src, begin_idx, end_idx);
                else {
                    KOS_OBJ_ID              args;
                    KOS_ATOMIC(KOS_OBJ_ID) *buf;

                    out = KOS_get_property(ctx, src, KOS_CONST_ID(str_slice));
                    TRY_OBJID(out);

                    if (GET_OBJ_TYPE(out) != OBJ_FUNCTION)
                        RAISE_EXCEPTION_STR(str_err_slice_not_function);

                    kos_track_refs(ctx, 2, &out, &src);
                    args = KOS_new_array(ctx, 2);
                    kos_untrack_refs(ctx, 2);

                    TRY_OBJID(args);

                    buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                    buf[0] = REGISTER(rbegin);
                    buf[1] = REGISTER(rend);

                    out = KOS_call_function(ctx, out, src, args);

                    assert(ctx->regs_idx == regs_idx);
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 5;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_PROP8): { /* <r.dest>, <r.src>, <str.idx.uint8> */
                const unsigned rsrc = bytecode[2];
                const uint8_t  idx  = bytecode[3];
                KOS_OBJ_ID     prop;
                KOS_OBJ_ID     obj;

                assert(rsrc < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                obj = REGISTER(rsrc);
                out = KOS_get_property(ctx, obj, prop);
                TRY_OBJID(out);

                if (GET_OBJ_TYPE(out) == OBJ_DYNAMIC_PROP) {
                    KOS_OBJ_ID args;
                    store_instr_offs(stack, regs_idx,
                                     (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                    out = OBJPTR(DYNAMIC_PROP, out)->getter;

                    kos_track_refs(ctx, 2, &out, &obj);
                    args  = KOS_new_array(ctx, 0);
                    kos_untrack_refs(ctx, 2);

                    TRY_OBJID(args);

                    out = KOS_call_function(ctx, out, obj, args);
                    TRY_OBJID(out);

                    assert(ctx->regs_idx == regs_idx);
                }

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET): { /* <r.dest>, <r.prop>, <r.src> */
                const unsigned rprop = bytecode[2];
                const unsigned rsrc  = bytecode[3];
                KOS_OBJ_ID     prop;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rprop < num_regs);
                assert(rsrc  < num_regs);

                prop = REGISTER(rprop);

                if (IS_NUMERIC_OBJ(prop)) {
                    KOS_OBJ_ID obj;
                    int64_t    idx;

                    TRY(KOS_get_integer(ctx, prop, &idx));

                    if (idx > INT_MAX || idx < INT_MIN)
                        RAISE_EXCEPTION_STR(str_err_invalid_index);

                    obj = REGISTER(rdest);
                    if (GET_OBJ_TYPE(obj) == OBJ_BUFFER)
                        TRY(_write_buffer(ctx, obj, (int)idx, REGISTER(rsrc)));
                    else
                        TRY(KOS_array_write(ctx, obj, (int)idx, REGISTER(rsrc)));
                }
                else {
                    KOS_OBJ_ID obj   = REGISTER(rdest);
                    KOS_OBJ_ID value = REGISTER(rsrc);

                    kos_track_refs(ctx, 3, &obj, &prop, &value);
                    error = KOS_set_property(ctx, obj, prop, value);
                    kos_untrack_refs(ctx, 3);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID              args;
                        KOS_OBJ_ID              setter;
                        KOS_ATOMIC(KOS_OBJ_ID) *buf;

                        assert(KOS_is_exception_pending(ctx));
                        setter = KOS_get_exception(ctx);
                        KOS_clear_exception(ctx);

                        assert( ! IS_BAD_PTR(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                        store_instr_offs(stack, regs_idx,
                                         (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                        setter = OBJPTR(DYNAMIC_PROP, setter)->setter;

                        kos_track_refs(ctx, 2, &setter, &obj);
                        args = KOS_new_array(ctx, 1);
                        kos_untrack_refs(ctx, 2);

                        TRY_OBJID(args);

                        buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                        buf[0] = REGISTER(rsrc);

                        value = KOS_call_function(ctx, setter, obj, args);
                        TRY_OBJID(value);

                        assert(ctx->regs_idx == regs_idx);
                    }
                    else if (error)
                        goto cleanup;
                }

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_ELEM): { /* <r.dest>, <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+2);
                const unsigned rsrc = bytecode[6];
                KOS_OBJ_ID     dest;
                KOS_TYPE       type;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                dest = REGISTER(rdest);

                type = GET_OBJ_TYPE(dest);

                if (type == OBJ_ARRAY)
                    TRY(KOS_array_write(ctx, dest, idx, REGISTER(rsrc)));
                else if (type == OBJ_BUFFER)
                    TRY(_write_buffer(ctx, dest, idx, REGISTER(rsrc)));
                else if (type == OBJ_STACK)
                    TRY(_write_stack(ctx, dest, idx, REGISTER(rsrc)));
                else
                    RAISE_EXCEPTION_STR(str_err_not_indexable);

                bytecode += 7;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_PROP8): { /* <r.dest>, <str.idx.uint8>, <r.src> */
                const uint8_t  idx  = bytecode[2];
                const unsigned rsrc = bytecode[3];
                KOS_OBJ_ID     prop;
                KOS_OBJ_ID     obj;
                KOS_OBJ_ID     value;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                obj   = REGISTER(rdest);
                value = REGISTER(rsrc);

                error = KOS_set_property(ctx, obj, prop, value);

                if (error == KOS_ERROR_SETTER) {
                    KOS_OBJ_ID              setter;
                    KOS_OBJ_ID              args;
                    KOS_ATOMIC(KOS_OBJ_ID) *buf;

                    assert(KOS_is_exception_pending(ctx));
                    setter = KOS_get_exception(ctx);
                    KOS_clear_exception(ctx);

                    assert( ! IS_BAD_PTR(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                    store_instr_offs(stack, regs_idx,
                                     (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                    setter = OBJPTR(DYNAMIC_PROP, setter)->setter;

                    kos_track_refs(ctx, 1, &setter);
                    args = KOS_new_array(ctx, 1);
                    kos_untrack_refs(ctx, 1);

                    TRY_OBJID(args);

                    buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                    buf[0] = REGISTER(rsrc);

                    value = KOS_call_function(ctx, setter, REGISTER(rdest), args);
                    TRY_OBJID(value);

                    assert(ctx->regs_idx == regs_idx);
                }
                else if (error)
                    goto cleanup;

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(PUSH): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                TRY(KOS_array_push(ctx, REGISTER(rdest), REGISTER(rsrc), 0));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(PUSH_EX): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                TRY(KOS_array_push_expand(ctx, REGISTER(rdest), REGISTER(rsrc)));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(DEL): { /* <r.dest>, <r.prop> */
                const unsigned rprop = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rprop < num_regs);

                TRY(KOS_delete_property(ctx, REGISTER(rdest), REGISTER(rprop)));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(DEL_PROP8): { /* <r.dest>, <str.idx.uint8> */
                const uint8_t idx = bytecode[2];
                KOS_OBJ_ID    prop;

                rdest = bytecode[1];

                assert(rdest < num_regs);

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                TRY(KOS_delete_property(ctx, REGISTER(rdest), prop));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(ADD): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_LOCAL src[2];

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest    = bytecode[1];
                src[0].o = REGISTER(rsrc1);
                src[1].o = REGISTER(rsrc2);

                if (IS_SMALL_INT(src[0].o)) {
                    const int64_t a = GET_SMALL_INT(src[0].o);
                    out             = _add_integer(ctx, a, src[1].o);
                }
                else {

                    switch (GET_OBJ_TYPE(src[0].o)) {

                        case OBJ_INTEGER:
                            out = _add_integer(ctx, OBJPTR(INTEGER, src[0].o)->value, src[1].o);
                            break;

                        case OBJ_FLOAT:
                            out = _add_float(ctx, OBJPTR(FLOAT, src[0].o)->value, src[1].o);
                            break;

                        case OBJ_STRING: {
                            if (GET_OBJ_TYPE(src[1].o) == OBJ_STRING) {
                                KOS_init_local_with(ctx, &src[0], src[0].o);
                                KOS_init_local_with(ctx, &src[1], src[1].o);

                                out = KOS_string_add_n(ctx, src, 2);

                                KOS_destroy_top_locals(ctx, &src[1], &src[0]);
                            }
                            else
                                RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                            break;
                        }

                        default:
                            RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                            break;
                    }
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SUB): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                if (IS_SMALL_INT(src1))
                    out = _sub_integer(ctx, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _sub_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _sub_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MUL): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                if (IS_SMALL_INT(src1))
                    out = _mul_integer(ctx, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _mul_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _mul_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(DIV): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                if (IS_SMALL_INT(src1))
                    out = _div_integer(ctx, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _div_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _div_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MOD): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                if (IS_SMALL_INT(src1))
                    out = _mod_integer(ctx, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _mod_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _mod_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHL): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                if (b > 63 || b < -62)
                    out = TO_SMALL_INT((a < 0 && b < 0) ? -1 : 0);
                else if (b < 0)
                    out = KOS_new_int(ctx, a >> -b);
                else
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << b));

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHR): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                if (b > 62 || b < -63)
                    out = TO_SMALL_INT((a < 0 && b > 0) ? -1 : 0);
                else if (b < 0)
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << -b));
                else
                    out = KOS_new_int(ctx, a >> b);

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHRU): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                if (b > 63 || b < -63)
                    out = TO_SMALL_INT(0);
                else if (b < 0)
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << -b));
                else
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a >> b));

                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(NOT): { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];
                int64_t        a;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc), &a));

                out = KOS_new_int(ctx, ~a);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(AND): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                out = KOS_new_int(ctx, a & b);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(OR): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                out = KOS_new_int(ctx, a | b);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(XOR): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, REGISTER(rsrc1), &a));
                TRY(KOS_get_integer(ctx, REGISTER(rsrc2), &b));

                out = KOS_new_int(ctx, a ^ b);
                TRY_OBJID(out);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(TYPE): { /* <r.dest>, <r.src> */
                const unsigned rsrc  = bytecode[2];
                KOS_OBJ_ID     src;
                unsigned       type_idx;

                KOS_DECLARE_STATIC_CONST_STRING(str_integer,  "integer");
                KOS_DECLARE_STATIC_CONST_STRING(str_float,    "float");
                KOS_DECLARE_STATIC_CONST_STRING(str_boolean,  "boolean");
                KOS_DECLARE_STATIC_CONST_STRING(str_string,   "string");
                KOS_DECLARE_STATIC_CONST_STRING(str_object,   "object");
                KOS_DECLARE_STATIC_CONST_STRING(str_array,    "array");
                KOS_DECLARE_STATIC_CONST_STRING(str_buffer,   "buffer");
                KOS_DECLARE_STATIC_CONST_STRING(str_class,    "class");

                static const KOS_OBJ_ID obj_type_map[11] = {
                    KOS_CONST_ID(str_integer),
                    KOS_CONST_ID(str_integer),
                    KOS_CONST_ID(str_float),
                    KOS_STR_VOID,
                    KOS_CONST_ID(str_boolean),
                    KOS_CONST_ID(str_string),
                    KOS_CONST_ID(str_object),
                    KOS_CONST_ID(str_array),
                    KOS_CONST_ID(str_buffer),
                    KOS_STR_FUNCTION,
                    KOS_CONST_ID(str_class)
                };

                assert(rsrc  < num_regs);

                rdest = bytecode[1];
                src   = REGISTER(rsrc);

                assert(!IS_BAD_PTR(src));

                type_idx = GET_OBJ_TYPE(src) >> 1;

                if (type_idx >= sizeof(obj_type_map) / sizeof(obj_type_map[0]))
                    out = KOS_CONST_ID(str_object);
                else
                    out = obj_type_map[type_idx];

                WRITE_REGISTER(rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_EQ): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) == KOS_EQUAL);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_NE): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                KOS_COMPARE_RESULT cmp;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                cmp = KOS_compare(src1, src2);
                out = KOS_BOOL(cmp);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_LE): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) <= KOS_LESS_THAN);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_LT): { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = REGISTER(rsrc1);
                src2  = REGISTER(rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) == KOS_LESS_THAN);

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_DP): { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];

                out = KOS_get_property(ctx, REGISTER(rsrc), REGISTER(rprop));
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_DP_PROP8): { /* <r.dest>, <r.src>, <str.idx.uint8> */
                const unsigned rsrc  = bytecode[2];
                const int32_t  idx   = bytecode[3];
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                out = KOS_get_property(ctx, REGISTER(rsrc), prop);
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_SH): { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];

                out = KOS_get_property_with_depth(ctx, REGISTER(rsrc), REGISTER(rprop), KOS_SHALLOW);
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_SH_PROP8): { /* <r.dest>, <r.src>, <str.idx.uint8> */
                const unsigned rsrc  = bytecode[2];
                const uint8_t  idx   = bytecode[3];
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                out = KOS_get_property_with_depth(ctx, REGISTER(rsrc), prop, KOS_SHALLOW);
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(INSTANCEOF): { /* <r.dest>, <r.src>, <r.func> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rfunc = bytecode[3];
                KOS_OBJ_ID     constr_obj;

                out = KOS_FALSE;

                assert(rsrc  < num_regs);
                assert(rfunc < num_regs);

                rdest      = bytecode[1];
                constr_obj = REGISTER(rfunc);

                if (GET_OBJ_TYPE(constr_obj) == OBJ_CLASS) {
                    KOS_CLASS *const constr    = OBJPTR(CLASS, constr_obj);
                    KOS_OBJ_ID       proto_obj = KOS_atomic_read_relaxed_obj(constr->prototype);

                    assert( ! IS_BAD_PTR(proto_obj));

                    assert(IS_SMALL_INT(proto_obj) || GET_OBJ_TYPE(proto_obj) <= OBJ_LAST_TYPE);

                    if (KOS_has_prototype(ctx, REGISTER(rsrc), proto_obj))
                        out = KOS_TRUE;
                }

                WRITE_REGISTER(rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP): { /* <delta.int32> */
                int delta;

                KOS_help_gc(ctx);

                delta = 5 + (int32_t)_load_32(bytecode+1);
#ifdef CONFIG_FUZZ
                if (delta < 5)
                    delta = 5;
#endif
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP_COND): { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];
                int            delta;

                assert(rsrc < num_regs);

                KOS_help_gc(ctx);

                delta = 6;

                if (kos_is_truthy(REGISTER(rsrc)))
                    delta += offs;
#ifdef CONFIG_FUZZ
                if (delta < 6)
                    delta = 6;
#endif
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP_NOT_COND): { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];
                int            delta;

                assert(rsrc < num_regs);

                KOS_help_gc(ctx);

                delta = 6;

                if ( ! kos_is_truthy(REGISTER(rsrc)))
                    delta += offs;
#ifdef CONFIG_FUZZ
                if (delta < 6)
                    delta = 6;
#endif
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(BIND_SELF): /* <r.dest>, <slot.idx.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(BIND): { /* <r.dest>, <slot.idx.uint8>, <r.src> */
                const unsigned idx = bytecode[2];

                KOS_OBJ_ID dest;
                KOS_OBJ_ID closures;
                KOS_OBJ_ID regs_obj;

                rdest = bytecode[1];
                assert(rdest < num_regs);
                dest = REGISTER(rdest);

                {
                    const KOS_TYPE type = GET_OBJ_TYPE(dest);

                    if (type != OBJ_FUNCTION && type != OBJ_CLASS)
                        RAISE_EXCEPTION_STR(str_err_not_callable);
                }

                closures = OBJPTR(FUNCTION, dest)->closures;

                if (instr == INSTR_BIND) {
                    const unsigned rsrc = bytecode[3];
                    assert(rsrc < num_regs);
                    regs_obj = REGISTER(rsrc);
                }
                else {
                    regs_obj = ctx->stack;
                    assert(OBJPTR(STACK, regs_obj)->flags & KOS_REENTRANT_STACK);
                }

                assert( ! IS_SMALL_INT(closures));
                assert(GET_OBJ_TYPE(closures) == OBJ_VOID ||
                       GET_OBJ_TYPE(closures) == OBJ_ARRAY);

                kos_track_refs(ctx, 3, &dest, &closures, &regs_obj);

                error = KOS_SUCCESS;

                if (GET_OBJ_TYPE(closures) == OBJ_VOID) {
                    closures = KOS_new_array(ctx, idx+1);
                    if (IS_BAD_PTR(closures))
                        error = KOS_ERROR_EXCEPTION;
                    else
                        OBJPTR(FUNCTION, dest)->closures = closures;
                }
                else if (idx >= KOS_get_array_size(closures))
                    error = KOS_array_resize(ctx, closures, idx+1);

                kos_untrack_refs(ctx, 3);

                if (error)
                    goto cleanup;

                TRY(KOS_array_write(ctx, closures, (int)idx, regs_obj));

                bytecode += (instr == INSTR_BIND_SELF) ? 3 : 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(BIND_DEFAULTS): { /* <r.dest>, <r.src> */
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     dest;
                KOS_TYPE       type;

                const unsigned rsrc = bytecode[2];
                rdest               = bytecode[1];

                assert(rsrc  < num_regs);
                assert(rdest < num_regs);

                src  = REGISTER(rsrc);
                dest = REGISTER(rdest);

                if (GET_OBJ_TYPE(src) != OBJ_ARRAY)
                    RAISE_EXCEPTION_STR(str_err_corrupted_defaults);

                type = GET_OBJ_TYPE(dest);

                if (type == OBJ_FUNCTION || type == OBJ_CLASS)
                    OBJPTR(FUNCTION, dest)->defaults = src;
                else
                    RAISE_EXCEPTION_STR(str_err_not_callable);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(TAIL_CALL): /* <closure.size.uint8>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            BEGIN_INSTRUCTION(TAIL_CALL_N): /* <closure.size.uint8>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(TAIL_CALL_FUN): /* <closure.size.uint8>, <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL): /* <r.dest>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL_N): /* <r.dest>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL_FUN): /* <r.dest>, <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL_GEN): { /* <r.dest>, <r.func>, <r.final> */
                const unsigned     rfunc     = bytecode[2];
                unsigned           rthis     = ~0U;
                unsigned           rfinal    = ~0U;
                unsigned           rargs     = ~0U;
                unsigned           rarg1     = ~0U;
                unsigned           num_args  = 0;
                int                tail_call = 0;
                int                delta;
                KOS_FUNCTION_STATE state;

                KOS_LOCAL      func;
                KOS_LOCAL      this_;
                KOS_LOCAL      args;

                KOS_init_locals(ctx, 3, &func, &this_, &args);

                rdest = bytecode[1];

                switch (instr) {

                    case INSTR_TAIL_CALL:
                        rthis     = bytecode[3];
                        rargs     = bytecode[4];
                        tail_call = 1;
                        delta     = 5;
                        assert(rdest <= num_regs);
                        break;

                    case INSTR_TAIL_CALL_N:
                        rthis     = bytecode[3];
                        rarg1     = bytecode[4];
                        num_args  = bytecode[5];
                        tail_call = 1;
                        delta     = 6;
                        assert(rdest <= num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    case INSTR_TAIL_CALL_FUN:
                        rarg1     = bytecode[3];
                        num_args  = bytecode[4];
                        tail_call = 1;
                        delta     = 5;
                        assert(rdest <= num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    case INSTR_CALL:
                        rthis = bytecode[3];
                        rargs = bytecode[4];
                        delta = 5;
                        assert(rdest < num_regs);
                        break;

                    case INSTR_CALL_N:
                        rthis    = bytecode[3];
                        rarg1    = bytecode[4];
                        num_args = bytecode[5];
                        delta    = 6;
                        assert(rdest < num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    case INSTR_CALL_FUN:
                        rarg1    = bytecode[3];
                        num_args = bytecode[4];
                        delta    = 5;
                        assert(rdest < num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    default:
                        assert(instr == INSTR_CALL_GEN);
                        rfinal = bytecode[3];
                        delta = 4;
                        assert(rdest < num_regs);
                        break;
                }

                if (rthis != ~0U) {
                    rthis = bytecode[3];
                    assert(rthis < num_regs);

                    this_.o = REGISTER(rthis);
                    assert( ! IS_BAD_PTR(this_.o));
                }
                else
                    this_.o = KOS_VOID;

                assert(rfunc < num_regs);

                func.o = REGISTER(rfunc);
                assert( ! IS_BAD_PTR(func.o));

                if (rargs != ~0U) {
                    assert(rargs < num_regs);
                    args.o = REGISTER(rargs);
                }

                store_instr_offs(stack, regs_idx,
                                 (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));

                switch (GET_OBJ_TYPE(func.o)) {

                    case OBJ_CLASS:
                        this_.o = NEW_THIS;
                        /* fall through */
                    case OBJ_FUNCTION:
                        break;

                    default:
                        KOS_destroy_top_locals(ctx, &func, &args);
                        RAISE_EXCEPTION_STR(str_err_not_callable);
                }

                error = _prepare_call(ctx, instr, func.o, &this_.o,
                                      args.o, num_args ? rarg1 : 0, num_args);
                if (error) {
                    KOS_destroy_top_locals(ctx, &func, &args);
                    goto cleanup;
                }

                state = (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state);

                if (state == KOS_GEN_INIT)
                    out = this_.o;

                else {
                    /* TODO optimize INSTR_TAIL_CALL */

                    if (OBJPTR(FUNCTION, func.o)->handler)  {
                        KOS_OBJ_ID ret_val = KOS_BADPTR;

                        if (IS_BAD_PTR(args.o))
                            args.o = make_args(ctx, stack, regs_idx + rarg1, num_args);

                        if (IS_BAD_PTR(args.o)) {
                            assert(KOS_is_exception_pending(ctx));
                            error = KOS_ERROR_EXCEPTION;
                        }
                        else {
                            ret_val = OBJPTR(FUNCTION, func.o)->handler(ctx, this_.o, args.o);

                            assert(IS_BAD_PTR(ret_val) || GET_OBJ_TYPE(ret_val) <= OBJ_LAST_TYPE);

                            assert(ctx->tmp_ref_count == 0);

                            assert(ctx->local_list == &func);

                            if (state >= KOS_GEN_INIT) {
                                /* Avoid detecting as end of iterator in _finish_call() */
                                if ( ! IS_BAD_PTR(ret_val))
                                    OBJPTR(STACK, ctx->stack)->flags &= ~KOS_CAN_YIELD;

                                if (KOS_is_exception_pending(ctx))
                                    error = KOS_ERROR_EXCEPTION;
                            }
                            else if (IS_BAD_PTR(ret_val)) {
                                assert(KOS_is_exception_pending(ctx));
                                error = KOS_ERROR_EXCEPTION;
                            }
                            else {
                                assert( ! KOS_is_exception_pending(ctx));
                            }
                        }

                        ctx->retval = ret_val;

                        if (error) {
                            assert(IS_BAD_PTR(ret_val));
                            kos_wrap_exception(ctx);
                        }
                    }
                    else
                        error = exec_function(ctx);

                    assert( ! error || KOS_is_exception_pending(ctx));
                    assert(error || ! KOS_is_exception_pending(ctx));
                    assert(ctx->local_list == &func);

                    out = _finish_call(ctx, instr, func.o, this_.o, &state);

                    assert(ctx->regs_idx == regs_idx);

                    if (KOS_is_exception_pending(ctx))
                        error = KOS_ERROR_EXCEPTION;

                    if (instr == INSTR_CALL_GEN) {
                        if (error && state == KOS_GEN_DONE
                                && _is_generator_end_exception(ctx)) {
                            KOS_clear_exception(ctx);
                            error = KOS_SUCCESS;
                        }
                        if ( ! error) {
                            const KOS_OBJ_ID result = KOS_BOOL(state == KOS_GEN_DONE);
                            if (rfinal == rdest)
                                out = result;
                            else {
                                assert(rfinal < num_regs);
                                WRITE_REGISTER(rfinal, result);
                            }
                        }
                    }
                }

                assert(ctx->stack == stack);
                KOS_destroy_top_locals(ctx, &func, &args);

                if (error) {
                    assert(KOS_is_exception_pending(ctx));
                    goto cleanup;
                }

                assert( ! KOS_is_exception_pending(ctx));

                if (tail_call) {
                    ctx->retval = out;

                    set_closure_stack_size(ctx, rdest);

                    store_instr_offs(stack, regs_idx,
                                     (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                    return KOS_SUCCESS;
                }

                if (instr != INSTR_CALL_GEN || ! IS_BAD_PTR(out))
                    WRITE_REGISTER(rdest, out);

                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(RETURN): { /* <closure.size.uint8>, <r.src> */
                const unsigned closure_size = bytecode[1];
                const unsigned rsrc         = bytecode[2];

                assert(closure_size <= num_regs);
                assert(rsrc         <  num_regs);

                ctx->retval = REGISTER(rsrc);

                assert(GET_OBJ_TYPE(ctx->retval) <= OBJ_LAST_TYPE);

                set_closure_stack_size(ctx, closure_size);

                store_instr_offs(stack, regs_idx,
                                 (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                return KOS_SUCCESS;
            }

            BEGIN_INSTRUCTION(YIELD): { /* <r.src> */
                const uint8_t rsrc = bytecode[1];

                assert(rsrc < num_regs);

                if ( ! (OBJPTR(STACK, ctx->stack)->flags & KOS_CAN_YIELD))
                    RAISE_EXCEPTION_STR(str_err_cannot_yield);

                ctx->retval = REGISTER(rsrc);

                assert(OBJPTR(STACK, ctx->stack)->flags & KOS_REENTRANT_STACK);
                OBJPTR(STACK, ctx->stack)->yield_reg =  rsrc;
                OBJPTR(STACK, ctx->stack)->flags     &= ~KOS_CAN_YIELD;

                bytecode += 2;

                store_instr_offs(stack, regs_idx,
                                 (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));
                return KOS_SUCCESS;
            }

            BEGIN_INSTRUCTION(CATCH): { /* <r.dest>, <delta.int32> */
                const int32_t  rel_offs = (int32_t)_load_32(bytecode+2);
                const uint32_t offset   = (uint32_t)((bytecode + 6 + rel_offs) - OBJPTR(MODULE, module)->bytecode);

                rdest = bytecode[1];

                assert(rdest  < num_regs);
                assert(offset < OBJPTR(MODULE, module)->bytecode_size);

                set_catch(stack, regs_idx, offset, (uint8_t)rdest);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CANCEL): {
                clear_catch(stack, regs_idx);
                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_BREAKPOINT_INSTRUCTION:
                assert(instr == INSTR_BREAKPOINT);
                if (instr != INSTR_BREAKPOINT)
                    RAISE_EXCEPTION_STR(str_err_invalid_instruction);

                /* TODO simply call a debugger function from instance */

                bytecode += 1;
                NEXT_INSTRUCTION;

            BEGIN_INSTRUCTION(THROW): { /* <r.src> */
                const unsigned rsrc = bytecode[1];

                assert(rsrc < num_regs);

                KOS_raise_exception(ctx, REGISTER(rsrc));
            }

cleanup:
            assert(KOS_is_exception_pending(ctx));
            {
                uint32_t catch_offs;
                uint8_t  catch_reg;

                store_instr_offs(stack, regs_idx,
                                 (uint32_t)(bytecode - OBJPTR(MODULE, module)->bytecode));

                kos_wrap_exception(ctx);

                catch_offs = get_catch(stack, regs_idx, &catch_reg);

                if (catch_offs == KOS_NO_CATCH)
                    return KOS_ERROR_EXCEPTION;

                assert(catch_reg < num_regs);

                WRITE_REGISTER(catch_reg, KOS_get_exception(ctx));

                bytecode = OBJPTR(MODULE, module)->bytecode + catch_offs;

                clear_catch(stack, regs_idx);
                KOS_clear_exception(ctx);
                NEXT_INSTRUCTION;
            }

#if ! KOS_DISPATCH_TABLE
        } /* end of switch statement */
    } /* end of for loop */
#endif
}

KOS_OBJ_ID kos_call_function(KOS_CONTEXT            ctx,
                             KOS_OBJ_ID             func_obj,
                             KOS_OBJ_ID             this_obj,
                             KOS_OBJ_ID             args_obj,
                             enum KOS_CALL_FLAVOR_E call_flavor)
{
    int                error  = KOS_SUCCESS;
    KOS_TYPE           type;
    KOS_FUNCTION_STATE state;
    KOS_LOCAL          func;
    KOS_LOCAL          this_;
    KOS_LOCAL          args;
    KOS_LOCAL          ret;

    KOS_instance_validate(ctx);

    type = GET_OBJ_TYPE(func_obj);

    if (type != OBJ_FUNCTION && type != OBJ_CLASS) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_callable));
        return KOS_BADPTR;
    }

    KOS_init_local(     ctx, &ret);
    KOS_init_local_with(ctx, &args,  args_obj);
    KOS_init_local_with(ctx, &this_, this_obj);
    KOS_init_local_with(ctx, &func,  func_obj);

    if (type == OBJ_CLASS && (call_flavor != KOS_APPLY_FUNCTION || this_.o == KOS_VOID))
        this_.o = NEW_THIS;

    error = _prepare_call(ctx, INSTR_CALL, func.o, &this_.o, args.o, 0, 0);

    if (error) {
        KOS_destroy_top_locals(ctx, &func, &ret);
        return KOS_BADPTR;
    }

    state = (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state);

    if (state == KOS_GEN_INIT)
        ret.o = this_.o;

    else {
        if (OBJPTR(FUNCTION, func.o)->handler)  {
            KOS_OBJ_ID retval = KOS_BADPTR;

            retval = OBJPTR(FUNCTION, func.o)->handler(ctx, this_.o, args.o);

            assert(ctx->local_list == &func);

            /* Avoid detecting as end of iterator in _finish_call() */
            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(retval))
                OBJPTR(STACK, ctx->stack)->flags &= ~KOS_CAN_YIELD;

            if (KOS_is_exception_pending(ctx)) {
                assert(IS_BAD_PTR(retval));
                error = KOS_ERROR_EXCEPTION;
                kos_wrap_exception(ctx);
            }
            else {
                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(retval));
                ctx->retval = retval;
            }
        }
        else {
            error = exec_function(ctx);
            assert( ! error || KOS_is_exception_pending(ctx));
            assert(ctx->local_list == &func);
        }

        ret.o = _finish_call(ctx,
                           call_flavor == KOS_CALL_GENERATOR ? INSTR_CALL_GEN : INSTR_CALL,
                           func.o, this_.o, &state);

        if (state == KOS_GEN_DONE) {
            if (call_flavor == KOS_CALL_GENERATOR &&
                KOS_is_exception_pending(ctx) &&
                _is_generator_end_exception(ctx)) {

                KOS_clear_exception(ctx);
            }

            ret.o = KOS_BADPTR;
        }
    }

    ret.o = KOS_destroy_top_locals(ctx, &func, &ret);

    return error ? KOS_BADPTR : ret.o;
}

KOS_OBJ_ID kos_vm_run_module(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int        error;
    KOS_OBJ_ID func_obj;
    int        pushed = 0;

    assert( ! IS_BAD_PTR(module_obj));
    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);
    assert(OBJPTR(MODULE, module_obj)->inst);

    func_obj = KOS_array_read(ctx,
                              OBJPTR(MODULE, module_obj)->constants,
                              OBJPTR(MODULE, module_obj)->main_idx);

    if ( ! IS_BAD_PTR(func_obj)) {
        assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION);
        assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func_obj)->module) == OBJ_MODULE);

        error = kos_stack_push(ctx, func_obj);

        if ( ! error)
            pushed = 1;
    }
    else
        error = KOS_ERROR_EXCEPTION;

    if ( ! error) {

        KOS_instance_validate(ctx);

        error = exec_function(ctx);

        if ( ! error)
            func_obj = ctx->retval;

        assert( ! KOS_is_exception_pending(ctx) || error == KOS_ERROR_EXCEPTION);
    }

    if (pushed)
        kos_stack_pop(ctx);

    assert(error == KOS_SUCCESS || error == KOS_ERROR_EXCEPTION);

    return error ? KOS_BADPTR : func_obj;
}

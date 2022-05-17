/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_bytecode.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_args_not_array,           "function arguments are not an array");
KOS_DECLARE_STATIC_CONST_STRING(str_err_cannot_yield,             "function is not a generator");
KOS_DECLARE_STATIC_CONST_STRING(str_err_corrupted_defaults,       "argument defaults are corrupted");
KOS_DECLARE_STATIC_CONST_STRING(str_err_div_by_zero,              "division by zero");
KOS_DECLARE_STATIC_CONST_STRING(str_err_div_overflow,             "division overflow");
KOS_DECLARE_STATIC_CONST_STRING(str_err_generator_running,        "generator is running");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_byte_value,       "buffer element value out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_index,            "index out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_instruction,      "invalid instruction");
KOS_DECLARE_STATIC_CONST_STRING(str_err_named_args_not_supported, "function does not support named arguments");
KOS_DECLARE_STATIC_CONST_STRING(str_err_no_setter,                "property is read-only");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_callable,             "object is not callable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_class,                "base object is not a class");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_generator,            "function is not a generator");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_indexable,            "object is not indexable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_slice_not_function,       "slice is not a function");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_few_args,             "not enough arguments passed to a function");
KOS_DECLARE_STATIC_CONST_STRING(str_err_unsup_operand_types,      "unsupported operand types");

DECLARE_STATIC_CONST_OBJECT(args_placeholder, OBJ_OPAQUE, 0xF0);

static KOS_OBJ_ID add_integer(KOS_CONTEXT ctx,
                              int64_t     a,
                              KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            ret = KOS_new_int(ctx, a + GET_SMALL_INT(bobj));
            break;

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a + OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, (double)a + OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID add_float(KOS_CONTEXT ctx,
                            double      a,
                            KOS_OBJ_ID  bobj)
{
    double b;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            b = (double)GET_SMALL_INT(bobj);
            break;

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

static KOS_OBJ_ID sub_integer(KOS_CONTEXT ctx,
                              int64_t     a,
                              KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            ret = KOS_new_int(ctx, a - GET_SMALL_INT(bobj));
            break;

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a - OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, (double)a - OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID sub_float(KOS_CONTEXT ctx,
                            double      a,
                            KOS_OBJ_ID  bobj)
{
    double b;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            b = (double)GET_SMALL_INT(bobj);
            break;

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

static KOS_OBJ_ID mul_integer(KOS_CONTEXT ctx,
                              int64_t     a,
                              KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            ret = KOS_new_int(ctx, a * GET_SMALL_INT(bobj));
            break;

        case OBJ_INTEGER:
            ret = KOS_new_int(ctx, a * OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(ctx, (double)a * OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID mul_float(KOS_CONTEXT ctx,
                            double      a,
                            KOS_OBJ_ID  bobj)
{
    double b;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            b = (double)GET_SMALL_INT(bobj);
            break;

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

static KOS_OBJ_ID div_integer(KOS_CONTEXT ctx,
                              int64_t     a,
                              KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID     ret;
    const KOS_TYPE btype = GET_OBJ_TYPE(bobj);

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            /* fall-through */
        case OBJ_INTEGER: {

            const int64_t b = (btype == OBJ_SMALL_INTEGER) ? GET_SMALL_INT(bobj) : OBJPTR(INTEGER, bobj)->value;

            if ((b == -1) && (a == (int64_t)((uint64_t)1 << 63))) {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_overflow));
                ret = KOS_BADPTR;
            }
            else if (b)
                ret = KOS_new_int(ctx, a / b);
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

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

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID div_float(KOS_CONTEXT ctx,
                            double      a,
                            KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;
    double     b;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            b = (double)GET_SMALL_INT(bobj);
            break;

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

static KOS_OBJ_ID mod_integer(KOS_CONTEXT ctx,
                              int64_t     a,
                              KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID     ret;
    const KOS_TYPE btype = GET_OBJ_TYPE(bobj);

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            /* fall-through */
        case OBJ_INTEGER: {

            const int64_t b = (btype == OBJ_SMALL_INTEGER) ? GET_SMALL_INT(bobj) : OBJPTR(INTEGER, bobj)->value;

            if ((b == -1) && (a == (int64_t)((uint64_t)1 << 63))) {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_overflow));
                ret = KOS_BADPTR;
            }
            else if (b)
                ret = KOS_new_int(ctx, a % b);
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_div_by_zero));
                ret = KOS_BADPTR;
            }
            break;
        }

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

        default:
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_unsup_operand_types));
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID mod_float(KOS_CONTEXT ctx,
                            double      a,
                            KOS_OBJ_ID  bobj)
{
    KOS_OBJ_ID ret;
    double     b;

    switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_SMALL_INTEGER:
            b = (double)GET_SMALL_INT(bobj);
            break;

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

static int is_generator_end_exception(KOS_CONTEXT ctx)
{
    int                 ret       = 0;
    KOS_INSTANCE *const inst      = ctx->inst;
    KOS_LOCAL           exception;

    KOS_init_local_with(ctx, &exception, KOS_get_exception(ctx));

    if (KOS_get_prototype(ctx, exception.o) == inst->prototypes.exception_proto) {

        KOS_OBJ_ID value;

        KOS_clear_exception(ctx);

        value = KOS_get_property(ctx, exception.o, KOS_STR_VALUE);

        if (IS_BAD_PTR(value)) {
            KOS_clear_exception(ctx);
            KOS_raise_exception(ctx, exception.o);
        }
        else if (KOS_get_prototype(ctx, value) != inst->prototypes.generator_end_proto)
            KOS_raise_exception(ctx, exception.o);
        else
            ret = 1;
    }

    KOS_destroy_top_local(ctx, &exception);

    return ret;
}

#ifndef NDEBUG
static uint32_t get_num_regs(KOS_CONTEXT ctx)
{
    uint32_t size;
    uint32_t num_regs;

    assert( ! IS_BAD_PTR(ctx->stack));
    assert(GET_OBJ_TYPE(ctx->stack) == OBJ_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, ctx->stack)->size);
    assert(size > KOS_STACK_EXTRA);
    assert(ctx->regs_idx + 1U < size);

    num_regs = size - 1U - ctx->regs_idx;
    assert(num_regs < KOS_NO_REG);

    return num_regs;
}
#endif

static KOS_STACK_FRAME *get_current_stack_frame(KOS_CONTEXT ctx)
{
    return (KOS_STACK_FRAME *)&OBJPTR(STACK, ctx->stack)->buf[ctx->regs_idx - 3];
}

static KOS_OBJ_ID get_current_func(KOS_STACK_FRAME *stack_frame)
{
    const KOS_OBJ_ID func_obj = KOS_atomic_read_relaxed_obj(stack_frame->func_obj);

    assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION ||
           GET_OBJ_TYPE(func_obj) == OBJ_CLASS);

    return func_obj;
}

static KOS_FUNCTION_STATE get_func_state(KOS_OBJ_ID func_obj)
{
    return (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func_obj)->state);
}

static KOS_OBJ_ID make_args(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  stack,
                            uint32_t    regs_idx,
                            unsigned    num_args)
{
    KOS_OBJ_ID array_obj;

    assert( ! kos_is_heap_object(stack));

    array_obj = KOS_new_array(ctx, num_args);

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
    KOS_LOCAL  args;

    assert(IS_BAD_PTR(stack) || ! kos_is_heap_object(stack));

    KOS_init_local_with(ctx, &args, args_obj);

    if ( ! IS_BAD_PTR(args.o))
        num_args = KOS_get_array_size(args.o);

    if (slice_begin < num_args && slice_begin < slice_end) {

        if (slice_end > num_args)
            slice_end = num_args;

        size = slice_end - slice_begin;
    }

    new_args = KOS_new_array(ctx, size);

    if ( ! IS_BAD_PTR(new_args) && size)
        kos_atomic_move_ptr((KOS_ATOMIC(void *) *)kos_get_array_buffer(OBJPTR(ARRAY, new_args)),
                            (KOS_ATOMIC(void *) *)(slice_begin + (IS_BAD_PTR(args.o)
                                ? &OBJPTR(STACK, stack)->buf[rarg1_idx]
                                : kos_get_array_buffer(OBJPTR(ARRAY, args.o)))),
                            size);

    KOS_destroy_top_local(ctx, &args);

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
    PROF_ZONE(VM)

    int                    error            = KOS_SUCCESS;
    uint32_t               reg;
    const uint32_t         args_reg         = OBJPTR(FUNCTION, func_obj)->opts.args_reg;
    const uint32_t         num_non_def_args = OBJPTR(FUNCTION, func_obj)->opts.min_args;
    const uint32_t         num_def_args     = OBJPTR(FUNCTION, func_obj)->opts.num_def_args;
    const uint32_t         num_named_args   = num_non_def_args + num_def_args;
    const uint32_t         rest_reg         = OBJPTR(FUNCTION, func_obj)->opts.rest_reg;
    const uint32_t         args_reg_end     = KOS_min(rest_reg, args_reg + num_named_args);
    const uint32_t         num_input_args   = IS_BAD_PTR(args_obj) ? num_args : KOS_get_array_size(args_obj);
    KOS_STACK_FRAME *const stack_frame      = get_current_stack_frame(ctx);

    KOS_LOCAL func;
    KOS_LOCAL rest;
    KOS_LOCAL args;

    assert(IS_BAD_PTR(stack_obj) || ! kos_is_heap_object(stack_obj));

    KOS_init_locals(ctx, &func, &rest, &args, kos_end_locals);

    args.o = args_obj;
    func.o = func_obj;

    assert(GET_OBJ_TYPE(this_obj) <= OBJ_LAST_TYPE);
    assert( ! IS_BAD_PTR(args.o) || GET_OBJ_TYPE(stack_obj) == OBJ_STACK);
    assert( ! OBJPTR(FUNCTION, func.o)->handler);
    assert(args_reg_end <= KOS_NO_REG);
    assert(num_input_args >= num_non_def_args);

    /* Initialize this */
    reg = OBJPTR(FUNCTION, func.o)->opts.this_reg;
    if (reg != KOS_NO_REG) {
        assert(reg < get_num_regs(ctx));
        KOS_atomic_write_relaxed_ptr(stack_frame->regs[reg], this_obj);
    }

    /* Initialize ellipsis */
    reg = OBJPTR(FUNCTION, func.o)->opts.ellipsis_reg;
    if (reg != KOS_NO_REG)  {
        KOS_OBJ_ID ellipsis_obj;

        assert(reg < get_num_regs(ctx));

        if (num_input_args > num_named_args)
            ellipsis_obj = slice_args(ctx, args.o, stack_obj, rarg1_idx, num_args, num_named_args, ~0U);
        else
            ellipsis_obj = KOS_new_array(ctx, 0);
        TRY_OBJID(ellipsis_obj);

        KOS_atomic_write_relaxed_ptr(stack_frame->regs[reg], ellipsis_obj);
    }

    /* Initialize args */
    if (args_reg != KOS_NO_REG) {

        const uint32_t num_to_move  = KOS_min(num_input_args, (args_reg_end - args_reg));

        /* Move input args to registers */
        reg = args_reg;
        if (num_to_move) {
            kos_atomic_move_ptr(
                    (KOS_ATOMIC(void *) *)&stack_frame->regs[reg],
                    (KOS_ATOMIC(void *) *)(IS_BAD_PTR(args.o)
                        ? &OBJPTR(STACK, stack_obj)->buf[rarg1_idx]
                        : kos_get_array_buffer(OBJPTR(ARRAY, args.o))),
                    num_to_move);

            reg += num_to_move;
        }

        /* Move default values if not all named args were specified */
        if (num_def_args) {
            assert( ! IS_BAD_PTR(OBJPTR(FUNCTION, func.o)->defaults));
            assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->defaults) == OBJ_ARRAY);
            assert(KOS_get_array_size(OBJPTR(FUNCTION, func.o)->defaults) == num_def_args);
        }
        else {
            assert( ! IS_BAD_PTR(OBJPTR(FUNCTION, func.o)->defaults));
            assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->defaults) == OBJ_VOID);
        }

        assert(reg <= args_reg_end);

        if (reg < args_reg_end) {

            const uint32_t def_src_offs = num_input_args - num_non_def_args;
            const uint32_t to_copy      = args_reg_end - reg;
            KOS_OBJ_ID     defaults;

            assert(num_def_args >= to_copy);

            defaults = kos_get_array_storage(OBJPTR(FUNCTION, func.o)->defaults);

            kos_atomic_move_ptr((KOS_ATOMIC(void *) *)&stack_frame->regs[reg],
                                (KOS_ATOMIC(void *) *)&OBJPTR(ARRAY_STORAGE, defaults)->buf[def_src_offs],
                                to_copy);

            reg += to_copy;
        }

        assert(reg == args_reg_end);

        /* Move remaining args to the rest array */
        if (rest_reg != KOS_NO_REG) {

            const uint32_t num_reg_args = args_reg_end - args_reg;

            assert(num_reg_args <= num_named_args);

            rest.o = slice_args(ctx, args.o, stack_obj, rarg1_idx, num_args, num_reg_args, num_named_args);
            TRY_OBJID(rest.o);

            assert(GET_OBJ_TYPE(rest.o) == OBJ_ARRAY);
            assert(KOS_get_array_size(rest.o) <= num_named_args - num_reg_args);

            if (num_input_args < num_named_args && num_reg_args < num_named_args) {

                const uint32_t def_src_offs = num_input_args < num_reg_args
                                              ? num_reg_args - num_non_def_args
                                              : num_input_args - num_reg_args;

                TRY(KOS_array_insert(ctx,
                                     rest.o,
                                     MAX_INT64,
                                     MAX_INT64,
                                     OBJPTR(FUNCTION, func.o)->defaults,
                                     def_src_offs,
                                     MAX_INT64));
            }

            KOS_atomic_write_relaxed_ptr(stack_frame->regs[rest_reg], rest.o);
        }
    }
    else {
        assert(OBJPTR(FUNCTION, func.o)->opts.min_args     == 0);
        assert(OBJPTR(FUNCTION, func.o)->opts.num_def_args == 0);
        assert(OBJPTR(FUNCTION, func.o)->opts.rest_reg     == KOS_NO_REG);
    }

    /* Initialize bound closures */
    reg = OBJPTR(FUNCTION, func.o)->opts.bind_reg;
    if (reg != KOS_NO_REG) {
        KOS_ATOMIC(KOS_OBJ_ID) *src_buf;
        KOS_ATOMIC(KOS_OBJ_ID) *end;
        uint32_t                src_len;

        assert(reg < get_num_regs(ctx));
        assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->closures) == OBJ_ARRAY);

        src_len = KOS_get_array_size(OBJPTR(FUNCTION, func.o)->closures);
        assert(src_len > 0);
        assert(src_len == OBJPTR(FUNCTION, func.o)->opts.num_binds);

        src_buf = kos_get_array_buffer(OBJPTR(ARRAY, OBJPTR(FUNCTION, func.o)->closures));
        end     = src_buf + src_len;

        while (src_buf < end)
            KOS_atomic_write_relaxed_ptr(stack_frame->regs[reg++],
                                         KOS_atomic_read_relaxed_obj(*(src_buf++)));
    }
    else {
        assert(OBJPTR(FUNCTION, func.o)->opts.num_binds == 0);
        assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func.o)->closures) == OBJ_VOID);
    }

cleanup:
    KOS_destroy_top_locals(ctx, &func, &args);
    return error;
}

static void set_stack_flag(KOS_CONTEXT ctx, uint32_t new_flag)
{
    KOS_OBJ_ID stack = ctx->stack;

    for (;;) {
        uint32_t flags = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags);

        if (KOS_atomic_cas_weak_u32(OBJPTR(STACK, stack)->flags, flags, flags | new_flag))
            break;
    }
}

static void clear_stack_flag(KOS_CONTEXT ctx, uint32_t clear_flag)
{
    KOS_OBJ_ID stack = ctx->stack;

    for (;;) {
        uint32_t flags = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags);

        if (KOS_atomic_cas_weak_u32(OBJPTR(STACK, stack)->flags, flags, flags & ~clear_flag))
            break;
    }
}

static void set_handler_reg(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = ctx->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags) & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert((GET_SMALL_INT(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1])) & 0xFF) == 1);
    KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[size - 2], obj_id);
}

static KOS_OBJ_ID get_handler_reg(KOS_CONTEXT ctx)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = ctx->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags) & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert((GET_SMALL_INT(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1])) & 0xFF) == 1);

    return KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 2]);
}

static void write_to_yield_reg(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  obj_id)
{
    const KOS_OBJ_ID       stack       = ctx->stack;
    KOS_STACK_FRAME *const stack_frame = get_current_stack_frame(ctx);
    const uint32_t         yield_reg   = (uint32_t)OBJPTR(STACK, stack)->yield_reg;

    assert(yield_reg < get_num_regs(ctx));

    KOS_atomic_write_relaxed_ptr(stack_frame->regs[yield_reg], obj_id);
}

static KOS_OBJ_ID create_this(KOS_CONTEXT ctx, KOS_OBJ_ID func_obj)
{
    const KOS_OBJ_ID proto_obj = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, func_obj)->prototype);

    assert( ! IS_BAD_PTR(proto_obj));

    if (OBJPTR(FUNCTION, func_obj)->handler)
        return proto_obj;

    return KOS_new_object_with_prototype(ctx, proto_obj);
}

static KOS_OBJ_ID set_default_args_for_handler(KOS_CONTEXT ctx,
                                               KOS_OBJ_ID  func_obj,
                                               KOS_OBJ_ID  args_obj)
{
    assert((GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION) || (GET_OBJ_TYPE(func_obj) == OBJ_CLASS));
    assert(GET_OBJ_TYPE(args_obj) == OBJ_ARRAY);
    assert(OBJPTR(FUNCTION, func_obj)->handler);

    if (OBJPTR(FUNCTION, func_obj)->opts.num_def_args) {

        const uint32_t min_args     = OBJPTR(FUNCTION, func_obj)->opts.min_args;
        const uint32_t max_args     = min_args + OBJPTR(FUNCTION, func_obj)->opts.num_def_args;
        const uint32_t cur_num_args = KOS_get_array_size(args_obj);

        if (cur_num_args < max_args) {
            KOS_LOCAL args;

            KOS_init_local_with(ctx, &args, args_obj);

            if (KOS_array_insert(ctx,
                                 args.o,
                                 cur_num_args,
                                 MAX_INT64,
                                 OBJPTR(FUNCTION, func_obj)->defaults,
                                 cur_num_args - min_args,
                                 MAX_INT64) != KOS_SUCCESS)
                args.o = KOS_BADPTR;

            args_obj = KOS_destroy_top_local(ctx, &args);
        }
    }

    return args_obj;
}

static KOS_OBJ_ID get_named_args(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  func_obj,
                                 KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL      args;
    KOS_LOCAL      input;
    KOS_LOCAL      func;
    KOS_VECTOR     cstr;
    int            error     = KOS_SUCCESS;
    uint32_t       num_found = 0; /* Number of input args       */
    uint32_t       end_found = 0; /* Max arg index found plus 1 */
    const uint32_t min_args  = OBJPTR(FUNCTION, func_obj)->opts.min_args;
    const uint32_t max_args  = OBJPTR(FUNCTION, func_obj)->opts.num_def_args + min_args;
    uint32_t       i;

    KOS_init_local(     ctx, &args);
    KOS_init_local_with(ctx, &input, args_obj);
    KOS_init_local_with(ctx, &func,  func_obj);

    KOS_vector_init(&cstr);

    if (OBJPTR(FUNCTION, func.o)->arg_map == KOS_VOID)
        RAISE_EXCEPTION_STR(str_err_named_args_not_supported);

    args.o = KOS_new_array(ctx, max_args);
    TRY_OBJID(args.o);

    for (i = 0; i < max_args; i++)
        TRY(KOS_array_write(ctx, args.o, i, KOS_CONST_ID(args_placeholder)));

    input.o = KOS_new_iterator(ctx, input.o, KOS_SHALLOW);
    TRY_OBJID(input.o);

    while ( ! KOS_iterator_next(ctx, input.o)) {

        uint32_t         idx;
        const KOS_OBJ_ID idx_id = KOS_get_property(ctx,
                                                   OBJPTR(FUNCTION, func.o)->arg_map,
                                                   KOS_get_walk_key(input.o));

        ++num_found;

        if (IS_BAD_PTR(idx_id)) {
            KOS_clear_exception(ctx);
            TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(input.o), &cstr));
            KOS_raise_printf(ctx, "invalid function parameter: '%s'", cstr.buffer);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        assert(IS_SMALL_INT(idx_id));
        assert(GET_SMALL_INT(idx_id) >= 0);
        assert(GET_SMALL_INT(idx_id) < ~0U);

        idx = (uint32_t)GET_SMALL_INT(idx_id);

        /* Unused args may be optimized-out */
        if (idx < max_args) {
            end_found = KOS_max(end_found, idx + 1U);

            TRY(KOS_array_write(ctx, args.o, (int)idx, KOS_get_walk_value(input.o)));
        }
    }

    if (KOS_is_exception_pending(ctx))
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    for (i = 0; i < min_args; i++) {

        KOS_OBJ_ID value = KOS_array_read(ctx, args.o, i);
        TRY_OBJID(value);

        if (value == KOS_CONST_ID(args_placeholder)) {
            value = KOS_get_named_arg(ctx, func.o, i);
            TRY_OBJID(value);

            TRY(KOS_string_to_cstr_vec(ctx, value, &cstr));
            KOS_raise_printf(ctx, "missing function parameter: '%s'", cstr.buffer);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    for ( ; i < end_found; i++) {

        KOS_OBJ_ID value = KOS_array_read(ctx, args.o, i);
        TRY_OBJID(value);

        if (value == KOS_CONST_ID(args_placeholder)) {
            value = KOS_array_read(ctx, OBJPTR(FUNCTION, func.o)->defaults, i - min_args);
            TRY_OBJID(value);

            TRY(KOS_array_write(ctx, args.o, i, value));
        }
    }

    if (i < max_args)
        TRY(KOS_array_insert(ctx,
                             args.o,
                             i,
                             max_args,
                             OBJPTR(FUNCTION, func.o)->defaults,
                             i - min_args,
                             MAX_INT64));

cleanup:
    KOS_vector_destroy(&cstr);

    args.o = KOS_destroy_top_locals(ctx, &func, &args);

    return error ? KOS_BADPTR : args.o;
}

static int prepare_call(KOS_CONTEXT        ctx,
                        KOS_BYTECODE_INSTR instr,
                        KOS_OBJ_ID         func_obj,
                        KOS_OBJ_ID        *this_obj,
                        KOS_OBJ_ID        *args_obj,
                        uint32_t           rarg1,
                        unsigned           num_args,
                        uint8_t            ret_reg)
{
    PROF_ZONE(VM)

    int                error     = KOS_SUCCESS;
    int                state_set = 0;
    KOS_FUNCTION_STATE state;
    const uint32_t     regs_idx  = ctx->regs_idx;
    KOS_OBJ_ID         stack_obj = ctx->stack;
    KOS_LOCAL          func;
    KOS_LOCAL          args;

    KOS_init_local_with(ctx, &args, *args_obj);
    KOS_init_local_with(ctx, &func, func_obj);

    assert(IS_BAD_PTR(stack_obj) || ! kos_is_heap_object(stack_obj));

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

    assert((GET_OBJ_TYPE(func.o) == OBJ_FUNCTION) || (get_func_state(func.o) == KOS_CTOR));

    state = get_func_state(func.o);

    if (IS_BAD_PTR(args.o)) {
        if (num_args < OBJPTR(FUNCTION, func.o)->opts.min_args)
            RAISE_EXCEPTION_STR(str_err_too_few_args);
    }
    else {
        const KOS_TYPE type = GET_OBJ_TYPE(args.o);

        if (type != OBJ_ARRAY) {
            if (type == OBJ_OBJECT) {
                args.o = get_named_args(ctx, func.o, args.o);
                TRY_OBJID(args.o);
            }
            else
                RAISE_EXCEPTION_STR(str_err_args_not_array);
        }

        if (KOS_get_array_size(args.o) < OBJPTR(FUNCTION, func.o)->opts.min_args)
            RAISE_EXCEPTION_STR(str_err_too_few_args);
    }

    if ((instr <= INSTR_NEXT) && (state < KOS_GEN_READY))
        RAISE_EXCEPTION_STR(str_err_not_generator);

    switch (state) {

        /* Constructor function */
        case KOS_CTOR:
            /* fall through */

        /* Regular function */
        case KOS_FUN: {
            assert(instr > INSTR_NEXT);
            TRY(kos_stack_push(ctx, func.o, ret_reg, (uint8_t)instr));

            if ( ! OBJPTR(FUNCTION, func.o)->handler)
                TRY(init_registers(ctx,
                                   func.o,
                                   args.o,
                                   stack_obj,
                                   regs_idx + rarg1,
                                   num_args,
                                   *this_obj));
            else {
                if (IS_BAD_PTR(args.o)) {
                    args.o = make_args(ctx, stack_obj, regs_idx + rarg1, num_args);
                    TRY_OBJID(args.o);
                }

                args.o = set_default_args_for_handler(ctx, func.o, args.o);
                TRY_OBJID(args.o);
            }

            break;
        }

        /* Instantiate a generator function */
        case KOS_GEN_INIT: {
            func.o = kos_copy_function(ctx, func.o);
            TRY_OBJID(func.o);

            assert(instr > INSTR_NEXT);
            TRY(kos_stack_push(ctx, func.o, ret_reg, (uint8_t)instr));

            KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func.o)->state, KOS_GEN_READY);

            if ( ! OBJPTR(FUNCTION, func.o)->handler)
                TRY(init_registers(ctx,
                                   func.o,
                                   args.o,
                                   stack_obj,
                                   regs_idx + rarg1,
                                   num_args,
                                   *this_obj));
            else {
                if (IS_BAD_PTR(args.o)) {
                    args.o = make_args(ctx, stack_obj, regs_idx + rarg1, num_args);
                    TRY_OBJID(args.o);
                }

                args.o = set_default_args_for_handler(ctx, func.o, args.o);
                TRY_OBJID(args.o);

                set_handler_reg(ctx, args.o);
            }

            OBJPTR(FUNCTION, func.o)->opts.min_args = 0;

            set_stack_flag(ctx, KOS_CAN_YIELD);

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

            if ( ! KOS_atomic_cas_strong_u32(OBJPTR(FUNCTION, func.o)->state, state, KOS_GEN_RUNNING))
                RAISE_EXCEPTION_STR(str_err_generator_running);

            state_set = 1;

            TRY(kos_stack_push(ctx, func.o, ret_reg, (uint8_t)instr));

            if ( ! OBJPTR(FUNCTION, func.o)->handler) {
                if (state == KOS_GEN_ACTIVE) {

                    KOS_OBJ_ID value;

                    if (IS_BAD_PTR(args.o))
                        value = num_args ? KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack_obj)->buf[regs_idx + rarg1]) : KOS_VOID;

                    else {

                        num_args = KOS_get_array_size(args.o);

                        value = num_args ? KOS_array_read(ctx, args.o, 0) : KOS_VOID;
                    }

                    write_to_yield_reg(ctx, value);
                }
            }
            else {
                *this_obj = get_handler_reg(ctx);

                if (IS_BAD_PTR(args.o)) {
                    args.o = make_args(ctx, stack_obj, regs_idx + rarg1, num_args);
                    TRY_OBJID(args.o);
                }

                args.o = set_default_args_for_handler(ctx, func.o, args.o);
                TRY_OBJID(args.o);
            }

            set_stack_flag(ctx, KOS_CAN_YIELD);
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
    if (error && (stack_obj != ctx->stack || regs_idx != ctx->regs_idx)) {
        kos_stack_pop(ctx);

        if (state_set)
            KOS_atomic_write_release_u32(OBJPTR(FUNCTION, func.o)->state, state);
    }

    *args_obj = KOS_destroy_top_locals(ctx, &func, &args);

    return error;
}

static void finish_call(KOS_CONTEXT        ctx,
                        KOS_STACK_FRAME   *stack_frame,
                        KOS_BYTECODE_INSTR instr)
{
    KOS_OBJ_ID         func_obj = get_current_func(stack_frame);
    KOS_FUNCTION_STATE state    = get_func_state(func_obj);

    if ( ! KOS_is_exception_pending(ctx)) {

        if (state >= KOS_GEN_INIT) {
            if (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, ctx->stack)->flags) & KOS_CAN_YIELD) {
                state = KOS_GEN_DONE;
                if (instr != INSTR_NEXT_JUMP) {
                    KOS_LOCAL saved_func;

                    KOS_init_local_with(ctx, &saved_func, func_obj);
                    KOS_raise_generator_end(ctx);
                    func_obj = KOS_destroy_top_local(ctx, &saved_func);
                }
            }
            else {
                const KOS_FUNCTION_STATE end_state =
                    OBJPTR(FUNCTION, func_obj)->handler ? KOS_GEN_READY : KOS_GEN_ACTIVE;

                state = end_state;
            }
        }
    }
    else {
        if (state >= KOS_GEN_INIT) {
            state = KOS_GEN_DONE;
        }
    }

    kos_stack_pop(ctx);

    KOS_atomic_write_relaxed_u32(OBJPTR(FUNCTION, func_obj)->state, state);
}

static KOS_OBJ_ID read_buffer(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx)
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
        const uint8_t *const buf = KOS_buffer_data_const(objptr);
        ret = TO_SMALL_INT((int)buf[idx]);
    }

    return ret;
}

static int write_buffer(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
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
        uint8_t *const buf = KOS_buffer_data_volatile(ctx, objptr);
        if ( ! buf)
            goto cleanup;
        buf[idx] = (uint8_t)byte_value;
    }

cleanup:
    return error;
}

static KOS_OBJ_ID read_stack(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, objptr)->flags) & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, objptr)->size);
    assert(size >= 1 + KOS_STACK_EXTRA);

    if (idx < 0)
        idx += (int)size - 1;
    else
        idx += KOS_STACK_EXTRA;

    if ((uint32_t)idx < size) {
        ret = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, objptr)->buf[idx]);
        assert( ! IS_BAD_PTR(ret));
    }

    if (IS_BAD_PTR(ret))
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_index));

    return ret;
}

static int write_stack(KOS_CONTEXT ctx, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error = KOS_SUCCESS;
    uint32_t size;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, objptr)->flags) & KOS_REENTRANT_STACK);

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

static void set_closure_stack_size(KOS_CONTEXT      ctx,
                                   KOS_OBJ_ID       stack,
                                   KOS_STACK_FRAME *stack_frame)
{
    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(stack == ctx->stack);
    assert(get_current_stack_frame(ctx) == stack_frame);

    if (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags) & KOS_REENTRANT_STACK) {

        const KOS_OBJ_ID func_obj = get_current_func(stack_frame);

        assert(ctx->regs_idx >= 3);

        if (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->flags) & KOS_GENERATOR_DONE) {

            const uint32_t closure_size = OBJPTR(FUNCTION, func_obj)->opts.closure_size;
            const uint32_t size         = closure_size + 1U + KOS_STACK_EXTRA;
            const uint32_t old_size     = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
            KOS_OBJ_ID     size_obj;

            assert(size <= old_size);

            KOS_atomic_write_relaxed_u32(OBJPTR(STACK, stack)->size, size);

            size_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[old_size - 1]);
            assert(IS_SMALL_INT(size_obj));
            size_obj = TO_SMALL_INT((int64_t)(((uint32_t)GET_SMALL_INT(size_obj) & ~0xFFU) | closure_size));
            KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[size - 1], size_obj);

            ctx->stack_depth -= old_size - size;
        }
    }
}

static KOS_OBJ_ID get_module(KOS_STACK_FRAME *stack_frame)
{
    const KOS_OBJ_ID func_obj = get_current_func(stack_frame);
    KOS_OBJ_ID       module_obj;

    module_obj = OBJPTR(FUNCTION, func_obj)->module;

    assert( ! IS_BAD_PTR(module_obj));
    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    return module_obj;
}

KOS_OBJ_ID KOS_get_module(KOS_CONTEXT ctx)
{
    assert(ctx->regs_idx >= 3);
    return get_module(get_current_stack_frame(ctx));
}

const KOS_BYTECODE *get_bytecode_objptr(KOS_STACK_FRAME *stack_frame)
{
    const KOS_OBJ_ID func         = get_current_func(stack_frame);
    const KOS_OBJ_ID bytecode_obj = OBJPTR(FUNCTION, func)->bytecode;
    return (const KOS_BYTECODE *)OBJPTR(OPAQUE, bytecode_obj);
}

#ifndef NDEBUG
static uint32_t get_bytecode_size(KOS_STACK_FRAME *stack_frame)
{
    const KOS_BYTECODE *const bytecode_ptr = get_bytecode_objptr(stack_frame);
    return bytecode_ptr->bytecode_size;
}
#endif

static const uint8_t *get_bytecode_at_offs(KOS_STACK_FRAME *stack_frame, uint32_t offs)
{
    const KOS_BYTECODE *const bytecode_ptr = get_bytecode_objptr(stack_frame);
    return &bytecode_ptr->bytecode[offs];
}

static const uint8_t *get_bytecode(KOS_STACK_FRAME *stack_frame)
{
    const KOS_OBJ_ID instr_offs_id = KOS_atomic_read_relaxed_obj(stack_frame->instr_offs);
    int64_t          instr_offs;

    assert(IS_SMALL_INT(instr_offs_id));

    instr_offs = GET_SMALL_INT(instr_offs_id);

    assert(instr_offs >= 0 && instr_offs < get_bytecode_size(stack_frame));

    return get_bytecode_at_offs(stack_frame, (uint32_t)instr_offs);
}

static uint32_t get_instr_offs(KOS_STACK_FRAME *stack_frame, const uint8_t *bytecode)
{
    return (uint32_t)(bytecode - get_bytecode_at_offs(stack_frame, 0));
}

static void store_instr_offs(KOS_STACK_FRAME *stack_frame,
                             const uint8_t   *bytecode)
{
    const uint32_t instr_offs = get_instr_offs(stack_frame, bytecode);
    KOS_atomic_write_relaxed_ptr(stack_frame->instr_offs, TO_SMALL_INT((int64_t)instr_offs));
}

static uint32_t get_catch(KOS_STACK_FRAME *stack_frame,
                          uint8_t         *catch_reg)
{
    const KOS_OBJ_ID catch_data_obj = KOS_atomic_read_relaxed_obj(stack_frame->catch_info);
    uint64_t         catch_data;
    uint32_t         catch_offs;

    assert(IS_SMALL_INT(catch_data_obj));

    catch_data = (uint64_t)GET_SMALL_INT(catch_data_obj);

    catch_offs = (uint32_t)(catch_data >> 8);
    *catch_reg = (uint8_t)(catch_data & 0xFFU);

    return catch_offs;
}

static void set_catch(KOS_STACK_FRAME *stack_frame,
                      uint32_t         catch_offs,
                      uint8_t          catch_reg)
{
    const uint64_t catch_data = (((uint64_t)catch_offs << 8) | catch_reg);
    assert(catch_offs < KOS_NO_CATCH);
    KOS_atomic_write_relaxed_ptr(stack_frame->catch_info,
                                 TO_SMALL_INT((intptr_t)(int64_t)catch_data));
}

static void clear_catch(KOS_STACK_FRAME *stack_frame)
{
    const intptr_t catch_data = KOS_NO_CATCH << 8;
    KOS_atomic_write_relaxed_ptr(stack_frame->catch_info, TO_SMALL_INT(catch_data));
}

static uint16_t load_16(const uint8_t *bytecode)
{
    return (uint16_t)bytecode[0] +
           ((uint16_t)bytecode[1] << 8);
}

static uint32_t load_32(const uint8_t *bytecode)
{
    return (uint32_t)bytecode[0] +
           ((uint32_t)bytecode[1] << 8) +
           ((uint32_t)bytecode[2] << 16) +
           ((uint32_t)bytecode[3] << 24);
}

static KOS_OBJ_ID read_reg(KOS_STACK_FRAME *stack_frame, uint32_t reg)
{
    return KOS_atomic_read_relaxed_obj(stack_frame->regs[reg]);
}

static void write_reg(KOS_STACK_FRAME *stack_frame,
                      uint32_t         reg,
                      KOS_OBJ_ID       value)
{
    assert(GET_OBJ_TYPE(value) <= OBJ_LAST_TYPE);
    KOS_atomic_write_relaxed_ptr(stack_frame->regs[reg], value);
}

#if KOS_DISPATCH_TABLE
#   define BEGIN_INSTRUCTION(instr)     OP_##instr
#   define BEGIN_BREAKPOINT_INSTRUCTION OP_BREAKPOINT
#   define NEXT_INSTRUCTION                                   \
        do {                                                  \
            uint32_t jump_offs;                               \
                                                              \
            KOS_INSTR_FUZZ_LIMIT();                           \
            KOS_PERF_CNT(instructions);                       \
            instr     = (KOS_BYTECODE_INSTR)*bytecode;        \
            jump_offs = (uint32_t)(instr - INSTR_BREAKPOINT); \
            jump_offs = (jump_offs < (INSTR_LAST_OPCODE - INSTR_BREAKPOINT)) ? jump_offs : 0; \
            goto *dispatch_table[jump_offs];                  \
        } while (0)

#else
#   define BEGIN_INSTRUCTION(instr)     case INSTR_##instr
#   define BEGIN_BREAKPOINT_INSTRUCTION default
#   define NEXT_INSTRUCTION             KOS_INSTR_FUZZ_LIMIT(); break
#endif

#if defined(__cplusplus) && defined(TRACY_ENABLE)
/* This works correctly only for KOS_STRING_ELEM_8 and ASCII function names */
#   define PROF_ZONE_NAME_FUN(func_obj_expr)                             \
    {                                                                    \
        const KOS_OBJ_ID func_obj = (func_obj_expr);                     \
        const KOS_OBJ_ID name = OBJPTR(FUNCTION, func_obj)->name;        \
        assert(GET_OBJ_TYPE(name) == OBJ_STRING);                        \
        PROF_ZONE_NAME(                                                  \
                (OBJPTR(STRING, name)->header.flags & KOS_STRING_LOCAL)  \
                    ? (const char *)&OBJPTR(STRING, name)->local.data[0] \
                    : (const char *)OBJPTR(STRING, name)->ptr.data_ptr,  \
                OBJPTR(STRING, name)->header.length);                    \
    }
#else
#   define PROF_ZONE_NAME_FUN(func_obj_expr)
#endif

static KOS_OBJ_ID execute(KOS_CONTEXT ctx)
{
    PROF_ZONE(VM)

    const uint8_t     *bytecode;
    KOS_BYTECODE_INSTR instr;
    KOS_OBJ_ID         out      = KOS_BADPTR;
    KOS_OBJ_ID         module;
    KOS_OBJ_ID         stack    = ctx->stack;
    KOS_STACK_FRAME   *stack_frame;
    int                error    = KOS_SUCCESS;
    int                depth    = 0; /* Number of calls inside execute() without affecting native stack */
    unsigned           rdest;
#ifndef NDEBUG
    uint32_t           regs_idx = ctx->regs_idx;
    uint32_t           num_regs = get_num_regs(ctx);
#endif

#if KOS_DISPATCH_TABLE
    static void *const dispatch_table[] = {
#   define DEFINE_INSTRUCTION(name, value) &&OP_##name,
#   include "../inc/kos_opcodes.h"
#   undef DEFINE_INSTRUCTION
    };
#endif

    stack_frame = get_current_stack_frame(ctx);

    PROF_ZONE_NAME_FUN(get_current_func(stack_frame));

    module = get_module(stack_frame);
    assert( ! IS_BAD_PTR(module));
    assert(OBJPTR(MODULE, module)->inst);

    bytecode = get_bytecode(stack_frame);

    assert( ! kos_is_heap_object(module));
    assert( ! kos_is_heap_object(stack));

#if KOS_DISPATCH_TABLE
    NEXT_INSTRUCTION;
#else
    for (;;) {
        assert( ! KOS_is_exception_pending(ctx));
        assert(get_instr_offs(stack_frame, bytecode) < get_bytecode_size(stack_frame));

        KOS_PERF_CNT(instructions);

        instr = (KOS_BYTECODE_INSTR)*bytecode;

        switch (instr) {
#endif

            BEGIN_INSTRUCTION(LOAD_CONST): { /* <r.dest>, <uimm> */
                PROF_ZONE_N(INSTR, "LOAD.CONST")
                const KOS_IMM imm = kos_load_uimm(bytecode + 2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, imm.value.sv);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 2 + imm.delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_FUN): { /* <r.dest>, <uimm> */
                PROF_ZONE_N(INSTR, "LOAD.FUN")
                const KOS_IMM imm = kos_load_uimm(bytecode + 2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, imm.value.sv);
                TRY_OBJID(out);

                out = kos_copy_function(ctx, out);
                TRY_OBJID(out);

                if (GET_OBJ_TYPE(out) == OBJ_CLASS) {
                    const KOS_OBJ_ID proto_obj = KOS_array_read(ctx,
                                                                OBJPTR(MODULE, module)->constants,
                                                                imm.value.sv + 1);
                    TRY_OBJID(proto_obj);

                    KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, out)->prototype, proto_obj);
                }

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 2 + imm.delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_INT8): { /* <r.dest>, <int8> */
                PROF_ZONE_N(INSTR, "LOAD.INT8")
                const int8_t value = (int8_t)bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, TO_SMALL_INT(value));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_TRUE): { /* <r.dest> */
                PROF_ZONE_N(INSTR, "LOAD.TRUE")
                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, KOS_TRUE);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_FALSE): { /* <r.dest> */
                PROF_ZONE_N(INSTR, "LOAD.FALSE")
                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, KOS_FALSE);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LOAD_VOID): { /* <r.dest> */
                PROF_ZONE_N(INSTR, "LOAD.VOID")
                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, KOS_VOID);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(NEW_ARRAY8): { /* <r.dest>, <size.uint8> */
                PROF_ZONE_N(INSTR, "NEW.ARRAY8")
                const uint8_t size = bytecode[2];

                rdest = bytecode[1];

                out = KOS_new_array(ctx, size);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(NEW_OBJ): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "NEW.OBJ")
                const unsigned rsrc = bytecode[2];

                if (rsrc == KOS_NO_REG)
                    out = KOS_new_object(ctx);
                else {
                    assert(rsrc < num_regs);

                    out = KOS_new_object_with_prototype(ctx, read_reg(stack_frame, rsrc));
                }
                TRY_OBJID(out);

                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(NEW_ITER): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "NEW.ITER")
                const unsigned rsrc = bytecode[2];

                assert(rsrc < num_regs);

                out = KOS_new_iterator(ctx, read_reg(stack_frame, rsrc), KOS_CONTENTS);
                TRY_OBJID(out);

                rdest = bytecode[1];

                assert(rdest < num_regs);
                KOS_atomic_write_relaxed_ptr(stack_frame->regs[rdest], out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MOVE): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "MOVE")
                const unsigned rsrc = bytecode[2];

                assert(rsrc < num_regs);

                rdest = bytecode[1];

                out = read_reg(stack_frame, rsrc);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_PROTO): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "GET.PROTO")
                const unsigned rsrc = bytecode[2];
                KOS_OBJ_ID     constr_obj;

                assert(rsrc < num_regs);

                constr_obj = read_reg(stack_frame, rsrc);

                if (GET_OBJ_TYPE(constr_obj) == OBJ_CLASS) {

                    out = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, constr_obj)->prototype);

                    assert( ! IS_BAD_PTR(out));
                }
                else if (constr_obj == KOS_VOID)
                    out = KOS_VOID;

                else
                    RAISE_EXCEPTION_STR(str_err_not_class);

                rdest = bytecode[1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_GLOBAL): { /* <r.dest>, <int32.glob.idx> */
                PROF_ZONE_N(INSTR, "GET.GLOBAL")
                const int32_t  idx = (int32_t)load_32(bytecode+2);

                rdest = bytecode[1];

                out = KOS_array_read(ctx, OBJPTR(MODULE, module)->globals, idx);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_GLOBAL): { /* <int32.glob.idx>, <r.src> */
                PROF_ZONE_N(INSTR, "SET.GLOBAL")
                const int32_t  idx  = (int32_t)load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < num_regs);

                TRY(KOS_array_write(ctx,
                                    OBJPTR(MODULE, module)->globals,
                                    idx,
                                    read_reg(stack_frame, rsrc)));

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_MOD_GLOBAL): { /* <r.dest>, <uint16.mod.idx>, <r.glob> */
                PROF_ZONE_N(INSTR, "GET.MOD.GLOBAL")
                const int      mod_idx    = (int32_t)load_16(bytecode+2);
                const unsigned rglob      = bytecode[4];
                KOS_OBJ_ID     glob_idx;
                KOS_OBJ_ID     module_obj = KOS_array_read(ctx,
                                                           OBJPTR(MODULE, module)->inst->modules.modules,
                                                           mod_idx);
                TRY_OBJID(module_obj);

                assert(rglob < num_regs);

                rdest = bytecode[1];

                assert( ! IS_SMALL_INT(module_obj));
                assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                glob_idx = KOS_get_property_shallow(ctx,
                                                    OBJPTR(MODULE, module_obj)->global_names,
                                                    read_reg(stack_frame, rglob));
                TRY_OBJID(glob_idx);

                assert(IS_SMALL_INT(glob_idx));

                out = KOS_array_read(ctx, OBJPTR(MODULE, module_obj)->globals,
                                     (int)GET_SMALL_INT(glob_idx));
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 5;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_MOD_ELEM): { /* <r.dest>, <uint16.mod.idx>, <int32.glob.idx> */
                PROF_ZONE_N(INSTR, "GET.MOD.ELEM")
                const int  mod_idx    = (int32_t)load_16(bytecode+2);
                const int  glob_idx   = (int32_t)load_32(bytecode+4);
                KOS_OBJ_ID module_obj = KOS_array_read(ctx,
                                                       OBJPTR(MODULE, module)->inst->modules.modules,
                                                       mod_idx);
                TRY_OBJID(module_obj);

                rdest = bytecode[1];

                assert( ! IS_SMALL_INT(module_obj));
                assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                out = KOS_array_read(ctx, OBJPTR(MODULE, module_obj)->globals, glob_idx);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 8;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_MOD): { /* <r.dest>, <uint16.mod.idx> */
                PROF_ZONE_N(INSTR, "GET.MOD")
                const int  mod_idx    = (int32_t)load_16(bytecode+2);
                KOS_OBJ_ID module_obj = KOS_array_read(ctx,
                                                       OBJPTR(MODULE, module)->inst->modules.modules,
                                                       mod_idx);
                TRY_OBJID(module_obj);

                rdest = bytecode[1];

                assert( ! IS_SMALL_INT(module_obj));
                assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                out = module_obj;

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET): { /* <r.dest>, <r.src>, <r.prop> */
                PROF_ZONE_N(INSTR, "GET")
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];
                src   = read_reg(stack_frame, rsrc);
                prop  = read_reg(stack_frame, rprop);

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
                        out = read_buffer(ctx, src, (int)idx);
                    else
                        out = KOS_array_read(ctx, src, (int)idx);
                }
                else {
                    out = KOS_get_property(ctx, src, prop);
                    TRY_OBJID(out);

                    if (GET_OBJ_TYPE(out) == OBJ_DYNAMIC_PROP) {
                        store_instr_offs(stack_frame, bytecode);

                        out = OBJPTR(DYNAMIC_PROP, out)->getter;
                        out = KOS_call_function(ctx, out, src, KOS_EMPTY_ARRAY);

                        assert(ctx->regs_idx == regs_idx);
                    }
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_ELEM8): { /* <r.dest>, <r.src>, <int8> */
                PROF_ZONE_N(INSTR, "GET.ELEM8")
                const unsigned rsrc = bytecode[2];
                const int32_t  idx  = (int8_t)bytecode[3];
                KOS_OBJ_ID     src;
                KOS_TYPE       type;

                assert(rsrc < num_regs);

                rdest = bytecode[1];
                src   = read_reg(stack_frame, rsrc);

                type = GET_OBJ_TYPE(src);

                if (type == OBJ_ARRAY)
                    out = KOS_array_read(ctx, src, idx);
                else if (type == OBJ_STRING)
                    out = KOS_string_get_char(ctx, src, idx);
                else if (type == OBJ_BUFFER)
                    out = read_buffer(ctx, src, idx);
                else if (type == OBJ_STACK)
                    out = read_stack(ctx, src, idx);
                else
                    RAISE_EXCEPTION_STR(str_err_not_indexable);

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_RANGE): { /* <r.dest>, <r.src>, <r.begin>, <r.end> */
                PROF_ZONE_N(INSTR, "GET.RANGE")
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
                src   = read_reg(stack_frame, rsrc);
                begin = read_reg(stack_frame, rbegin);
                end   = read_reg(stack_frame, rend);

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
                    KOS_LOCAL               saved_out;
                    KOS_LOCAL               saved_src;

                    out = KOS_get_property(ctx, src, KOS_CONST_ID(str_slice));
                    TRY_OBJID(out);

                    if (GET_OBJ_TYPE(out) != OBJ_FUNCTION)
                        RAISE_EXCEPTION_STR(str_err_slice_not_function);

                    KOS_init_local_with(ctx, &saved_out, out);
                    KOS_init_local_with(ctx, &saved_src, src);

                    args = KOS_new_array(ctx, 2);

                    src = KOS_destroy_top_local(ctx, &saved_src);
                    out = KOS_destroy_top_local(ctx, &saved_out);

                    TRY_OBJID(args);

                    buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                    buf[0] = read_reg(stack_frame, rbegin);
                    buf[1] = read_reg(stack_frame, rend);

                    out = KOS_call_function(ctx, out, src, args);

                    assert(ctx->regs_idx == regs_idx);
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 5;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GET_PROP8): { /* <r.dest>, <r.src>, <uint8.str.idx> */
                PROF_ZONE_N(INSTR, "GET.PROP8")
                const unsigned rsrc = bytecode[2];
                const uint8_t  idx  = bytecode[3];
                KOS_OBJ_ID     prop;
                KOS_OBJ_ID     obj;

                assert(rsrc < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                obj = read_reg(stack_frame, rsrc);
                out = KOS_get_property(ctx, obj, prop);
                TRY_OBJID(out);

                if (GET_OBJ_TYPE(out) == OBJ_DYNAMIC_PROP) {
                    store_instr_offs(stack_frame, bytecode);

                    out = OBJPTR(DYNAMIC_PROP, out)->getter;
                    out = KOS_call_function(ctx, out, obj, KOS_EMPTY_ARRAY);
                    TRY_OBJID(out);

                    assert(ctx->regs_idx == regs_idx);
                }

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET): { /* <r.dest>, <r.prop>, <r.src> */
                PROF_ZONE_N(INSTR, "SET")
                const unsigned rprop = bytecode[2];
                const unsigned rsrc  = bytecode[3];
                KOS_OBJ_ID     prop;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rprop < num_regs);
                assert(rsrc  < num_regs);

                prop = read_reg(stack_frame, rprop);

                if (IS_NUMERIC_OBJ(prop)) {
                    KOS_OBJ_ID obj;
                    int64_t    idx;

                    TRY(KOS_get_integer(ctx, prop, &idx));

                    if (idx > INT_MAX || idx < INT_MIN)
                        RAISE_EXCEPTION_STR(str_err_invalid_index);

                    obj = read_reg(stack_frame, rdest);
                    if (GET_OBJ_TYPE(obj) == OBJ_BUFFER)
                        TRY(write_buffer(ctx, obj, (int)idx, read_reg(stack_frame, rsrc)));
                    else
                        TRY(KOS_array_write(ctx, obj, (int)idx, read_reg(stack_frame, rsrc)));
                }
                else {
                    error = KOS_set_property(ctx, read_reg(stack_frame, rdest), prop, read_reg(stack_frame, rsrc));

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID              args;
                        KOS_OBJ_ID              retval;
                        KOS_LOCAL               setter;
                        KOS_ATOMIC(KOS_OBJ_ID) *buf;

                        assert(KOS_is_exception_pending(ctx));
                        setter.o = KOS_get_exception(ctx);
                        KOS_clear_exception(ctx);

                        assert( ! IS_BAD_PTR(setter.o) && GET_OBJ_TYPE(setter.o) == OBJ_DYNAMIC_PROP);
                        store_instr_offs(stack_frame, bytecode);

                        setter.o = OBJPTR(DYNAMIC_PROP, setter.o)->setter;
                        if (IS_BAD_PTR(setter.o))
                            /* TODO print property name */
                            RAISE_EXCEPTION_STR(str_err_no_setter);

                        KOS_init_local_with(ctx, &setter, setter.o);

                        args = KOS_new_array(ctx, 1);

                        setter.o = KOS_destroy_top_local(ctx, &setter);

                        TRY_OBJID(args);

                        buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                        buf[0] = read_reg(stack_frame, rsrc);

                        retval = KOS_call_function(ctx, setter.o, read_reg(stack_frame, rdest), args);
                        TRY_OBJID(retval);

                        assert(ctx->regs_idx == regs_idx);
                    }
                    else if (error)
                        goto cleanup;
                }

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_ELEM8): { /* <r.dest>, <int8>, <r.src> */
                PROF_ZONE_N(INSTR, "SET.ELEM8")
                const int32_t  idx  = (int8_t)bytecode[2];
                const unsigned rsrc = bytecode[3];
                KOS_OBJ_ID     dest;
                KOS_TYPE       type;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                dest = read_reg(stack_frame, rdest);

                type = GET_OBJ_TYPE(dest);

                if (type == OBJ_ARRAY)
                    TRY(KOS_array_write(ctx, dest, idx, read_reg(stack_frame, rsrc)));
                else if (type == OBJ_BUFFER)
                    TRY(write_buffer(ctx, dest, idx, read_reg(stack_frame, rsrc)));
                else if (type == OBJ_STACK)
                    TRY(write_stack(ctx, dest, idx, read_reg(stack_frame, rsrc)));
                else
                    RAISE_EXCEPTION_STR(str_err_not_indexable);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SET_PROP8): { /* <r.dest>, <uint8.str.idx>, <r.src> */
                PROF_ZONE_N(INSTR, "SET.PROP8")
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

                obj   = read_reg(stack_frame, rdest);
                value = read_reg(stack_frame, rsrc);

                error = KOS_set_property(ctx, obj, prop, value);

                if (error == KOS_ERROR_SETTER) {
                    KOS_LOCAL               setter;
                    KOS_OBJ_ID              args;
                    KOS_ATOMIC(KOS_OBJ_ID) *buf;

                    assert(KOS_is_exception_pending(ctx));
                    setter.o = KOS_get_exception(ctx);
                    KOS_clear_exception(ctx);

                    assert( ! IS_BAD_PTR(setter.o) && GET_OBJ_TYPE(setter.o) == OBJ_DYNAMIC_PROP);
                    store_instr_offs(stack_frame, bytecode);

                    setter.o = OBJPTR(DYNAMIC_PROP, setter.o)->setter;
                    if (IS_BAD_PTR(setter.o))
                        /* TODO print property name */
                        RAISE_EXCEPTION_STR(str_err_no_setter);

                    KOS_init_local_with(ctx, &setter, setter.o);

                    args = KOS_new_array(ctx, 1);

                    setter.o = KOS_destroy_top_local(ctx, &setter);

                    TRY_OBJID(args);

                    buf = kos_get_array_buffer(OBJPTR(ARRAY, args));

                    buf[0] = read_reg(stack_frame, rsrc);

                    value = KOS_call_function(ctx, setter.o, read_reg(stack_frame, rdest), args);
                    TRY_OBJID(value);

                    assert(ctx->regs_idx == regs_idx);
                }
                else if (error)
                    goto cleanup;

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(PUSH): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "PUSH")
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                TRY(KOS_array_push(ctx, read_reg(stack_frame, rdest), read_reg(stack_frame, rsrc), KOS_NULL));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(PUSH_EX): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "PUSH.EX")
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                TRY(KOS_array_push_expand(ctx, read_reg(stack_frame, rdest), read_reg(stack_frame, rsrc)));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(DEL): { /* <r.dest>, <r.prop> */
                PROF_ZONE_N(INSTR, "DEL")
                const unsigned rprop = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rprop < num_regs);

                TRY(KOS_delete_property(ctx, read_reg(stack_frame, rdest), read_reg(stack_frame, rprop)));

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(ADD): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "ADD")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_LOCAL src[2];

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest    = bytecode[1];
                src[0].o = read_reg(stack_frame, rsrc1);
                src[1].o = read_reg(stack_frame, rsrc2);

                switch (GET_OBJ_TYPE(src[0].o)) {

                    case OBJ_SMALL_INTEGER: {
                        const int64_t a = GET_SMALL_INT(src[0].o);
                        out             = add_integer(ctx, a, src[1].o);
                        break;
                    }

                    case OBJ_INTEGER:
                        out = add_integer(ctx, OBJPTR(INTEGER, src[0].o)->value, src[1].o);
                        break;

                    case OBJ_FLOAT:
                        out = add_float(ctx, OBJPTR(FLOAT, src[0].o)->value, src[1].o);
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

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SUB): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "SUB")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_SMALL_INTEGER:
                        out = sub_integer(ctx, GET_SMALL_INT(src1), src2);
                        break;

                    case OBJ_INTEGER:
                        out = sub_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = sub_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MUL): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "MUL")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_SMALL_INTEGER:
                        out = mul_integer(ctx, GET_SMALL_INT(src1), src2);
                        break;

                    case OBJ_INTEGER:
                        out = mul_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = mul_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(DIV): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "DIV")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_SMALL_INTEGER:
                        out = div_integer(ctx, GET_SMALL_INT(src1), src2);
                        break;

                    case OBJ_INTEGER:
                        out = div_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = div_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MOD): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "MOD")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_SMALL_INTEGER:
                        out = mod_integer(ctx, GET_SMALL_INT(src1), src2);
                        break;

                    case OBJ_INTEGER:
                        out = mod_integer(ctx, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = mod_float(ctx, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        RAISE_EXCEPTION_STR(str_err_unsup_operand_types);
                        break;
                }

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHL): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "SHL")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                if (b > 63 || b < -62)
                    out = TO_SMALL_INT((a < 0 && b < 0) ? -1 : 0);
                else if (b < 0)
                    out = KOS_new_int(ctx, a >> -b);
                else
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << b));

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHR): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "SHR")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                if (b > 62 || b < -63)
                    out = TO_SMALL_INT((a < 0 && b > 0) ? -1 : 0);
                else if (b < 0)
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << -b));
                else
                    out = KOS_new_int(ctx, a >> b);

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(SHRU): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "SHRU")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                if (b > 63 || b < -63)
                    out = TO_SMALL_INT(0);
                else if (b < 0)
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a << -b));
                else
                    out = KOS_new_int(ctx, (int64_t)((uint64_t)a >> b));

                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(NOT): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "NOT")
                const unsigned rsrc = bytecode[2];
                int64_t        a;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc), &a));

                out = KOS_new_int(ctx, ~a);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(AND): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "AND")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                out = KOS_new_int(ctx, a & b);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(OR): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "OR")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                out = KOS_new_int(ctx, a | b);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(XOR): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "XOR")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc1), &a));
                TRY(KOS_get_integer(ctx, read_reg(stack_frame, rsrc2), &b));

                out = KOS_new_int(ctx, a ^ b);
                TRY_OBJID(out);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(TYPE): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "TYPE")
                const unsigned rsrc = bytecode[2];
                KOS_OBJ_ID     src;
                unsigned       type_idx;

                KOS_DECLARE_STATIC_CONST_STRING(str_integer, "integer");
                KOS_DECLARE_STATIC_CONST_STRING(str_float,   "float");
                KOS_DECLARE_STATIC_CONST_STRING(str_boolean, "boolean");
                KOS_DECLARE_STATIC_CONST_STRING(str_string,  "string");
                KOS_DECLARE_STATIC_CONST_STRING(str_object,  "object");
                KOS_DECLARE_STATIC_CONST_STRING(str_array,   "array");
                KOS_DECLARE_STATIC_CONST_STRING(str_buffer,  "buffer");
                KOS_DECLARE_STATIC_CONST_STRING(str_class,   "class");
                KOS_DECLARE_STATIC_CONST_STRING(str_module,  "module");

                static const KOS_OBJ_ID obj_type_map[12] = {
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
                    KOS_CONST_ID(str_class),
                    KOS_CONST_ID(str_module)
                };

                assert(rsrc < num_regs);

                rdest = bytecode[1];
                src   = read_reg(stack_frame, rsrc);

                assert(!IS_BAD_PTR(src));

                type_idx = GET_OBJ_TYPE(src);
                type_idx = (type_idx < 2 * (sizeof(obj_type_map) / sizeof(obj_type_map[0])))
                           ? type_idx : (unsigned)OBJ_OBJECT;

                out = obj_type_map[type_idx >> 1];

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_EQ): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "CMP.EQ")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) == KOS_EQUAL);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_NE): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "CMP.NE")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                KOS_COMPARE_RESULT cmp;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                cmp = KOS_compare(src1, src2);
                out = KOS_BOOL(cmp);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_LE): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "CMP.LE")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) <= KOS_LESS_THAN);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CMP_LT): { /* <r.dest>, <r.src1>, <r.src2> */
                PROF_ZONE_N(INSTR, "CMP.LT")
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                KOS_OBJ_ID     src1;
                KOS_OBJ_ID     src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = read_reg(stack_frame, rsrc1);
                src2  = read_reg(stack_frame, rsrc2);

                out = KOS_BOOL(KOS_compare(src1, src2) == KOS_LESS_THAN);

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_DP): { /* <r.dest>, <r.src>, <r.prop> */
                PROF_ZONE_N(INSTR, "HAS.DP")
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];

                out = KOS_get_property(ctx, read_reg(stack_frame, rsrc), read_reg(stack_frame, rprop));
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_DP_PROP8): { /* <r.dest>, <r.src>, <uint8.str.idx> */
                PROF_ZONE_N(INSTR, "HAS.DP.PROP8")
                const unsigned rsrc  = bytecode[2];
                const int32_t  idx   = bytecode[3];
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                out = KOS_get_property(ctx, read_reg(stack_frame, rsrc), prop);
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_SH): { /* <r.dest>, <r.src>, <r.prop> */
                PROF_ZONE_N(INSTR, "HAS.SH")
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];

                out = KOS_get_property_shallow(ctx, read_reg(stack_frame, rsrc), read_reg(stack_frame, rprop));
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(HAS_SH_PROP8): { /* <r.dest>, <r.src>, <uint8.str.idx> */
                PROF_ZONE_N(INSTR, "HAS.SH.PROP8")
                const unsigned rsrc  = bytecode[2];
                const uint8_t  idx   = bytecode[3];
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                prop = KOS_array_read(ctx, OBJPTR(MODULE, module)->constants, idx);
                TRY_OBJID(prop);

                out = KOS_get_property_shallow(ctx, read_reg(stack_frame, rsrc), prop);
                KOS_clear_exception(ctx);

                out = KOS_BOOL( ! IS_BAD_PTR(out));

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(INSTANCEOF): { /* <r.dest>, <r.src>, <r.func> */
                PROF_ZONE_N(INSTR, "INSTANCEOF")
                const unsigned rsrc  = bytecode[2];
                const unsigned rfunc = bytecode[3];
                KOS_OBJ_ID     constr_obj;

                out = KOS_FALSE;

                assert(rsrc  < num_regs);
                assert(rfunc < num_regs);

                rdest      = bytecode[1];
                constr_obj = read_reg(stack_frame, rfunc);

                if (GET_OBJ_TYPE(constr_obj) == OBJ_CLASS) {
                    KOS_CLASS *const constr    = OBJPTR(CLASS, constr_obj);
                    KOS_OBJ_ID       proto_obj = KOS_atomic_read_relaxed_obj(constr->prototype);

                    assert( ! IS_BAD_PTR(proto_obj));

                    assert(IS_SMALL_INT(proto_obj) || GET_OBJ_TYPE(proto_obj) <= OBJ_LAST_TYPE);

                    if (KOS_has_prototype(ctx, read_reg(stack_frame, rsrc), proto_obj))
                        out = KOS_TRUE;
                }

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP): { /* <delta.int32> */
                PROF_ZONE_N(INSTR, "JUMP")
                int delta;

                KOS_help_gc(ctx);

                delta = 5 + (int32_t)load_32(bytecode+1);
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP_COND): { /* <delta.int32>, <r.src> */
                PROF_ZONE_N(INSTR, "JUMP.COND")
                const int32_t  offs = (int32_t)load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];
                int            delta;

                assert(rsrc < num_regs);

                KOS_help_gc(ctx);

                delta = 6;

                if (kos_is_truthy(read_reg(stack_frame, rsrc)))
                    delta += offs;
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP_NOT_COND): { /* <delta.int32>, <r.src> */
                PROF_ZONE_N(INSTR, "JUMP.NOT.COND")
                const int32_t  offs = (int32_t)load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];
                int            delta;

                assert(rsrc < num_regs);

                KOS_help_gc(ctx);

                delta = 6;

                if ( ! kos_is_truthy(read_reg(stack_frame, rsrc)))
                    delta += offs;
                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(BIND_SELF): /* <r.dest>, <uint8.slot.idx> */
                /* fall through */
            BEGIN_INSTRUCTION(BIND): { /* <r.dest>, <uint8.slot.idx>, <r.src> */
                PROF_ZONE_N(INSTR, "BIND")
                const unsigned idx = bytecode[2];

                KOS_LOCAL dest;
                KOS_LOCAL closures;
                KOS_LOCAL regs;

                rdest = bytecode[1];
                assert(rdest < num_regs);

                KOS_init_local_with(ctx, &dest, read_reg(stack_frame, rdest));

                {
                    const KOS_TYPE type = GET_OBJ_TYPE(dest.o);

                    if (type != OBJ_FUNCTION && type != OBJ_CLASS) {
                        KOS_destroy_top_local(ctx, &dest);
                        RAISE_EXCEPTION_STR(str_err_not_callable);
                    }
                }

                KOS_init_local_with(ctx, &closures, OBJPTR(FUNCTION, dest.o)->closures);
                KOS_init_local(ctx, &regs);

                if (instr == INSTR_BIND) {
                    const unsigned rsrc = bytecode[3];
                    assert(rsrc < num_regs);
                    regs.o = read_reg(stack_frame, rsrc);
                }
                else {
                    regs.o = ctx->stack;
                    assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, regs.o)->flags) & KOS_REENTRANT_STACK);
                }

                assert( ! IS_SMALL_INT(closures.o));
                assert(GET_OBJ_TYPE(closures.o) == OBJ_VOID ||
                       GET_OBJ_TYPE(closures.o) == OBJ_ARRAY);

                error = KOS_SUCCESS;

                if (GET_OBJ_TYPE(closures.o) == OBJ_VOID) {
                    closures.o = KOS_new_array(ctx, idx+1);
                    if (IS_BAD_PTR(closures.o))
                        error = KOS_ERROR_EXCEPTION;
                    else
                        OBJPTR(FUNCTION, dest.o)->closures = closures.o;
                }
                else if (idx >= KOS_get_array_size(closures.o))
                    error = KOS_array_resize(ctx, closures.o, idx+1);

                regs.o     = KOS_destroy_top_local(ctx, &regs);
                closures.o = KOS_destroy_top_local(ctx, &closures);
                KOS_destroy_top_local(ctx, &dest);

                if (error)
                    goto cleanup;

                TRY(KOS_array_write(ctx, closures.o, (int)idx, regs.o));

                bytecode += (instr == INSTR_BIND_SELF) ? 3 : 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(BIND_DEFAULTS): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "BIND.DEFAULTS")
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     dest;
                KOS_TYPE       type;

                const unsigned rsrc = bytecode[2];
                rdest               = bytecode[1];

                assert(rsrc  < num_regs);
                assert(rdest < num_regs);

                src  = read_reg(stack_frame, rsrc);
                dest = read_reg(stack_frame, rdest);

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

            BEGIN_INSTRUCTION(NEXT): /* <r.dest>, <r.func> */
                /* fall through */
            BEGIN_INSTRUCTION(NEXT_JUMP): { /* <r.dest>, <r.func>, <delta.int32> */
                PROF_ZONE_N(INSTR, "NEXT.JUMP")
                KOS_LOCAL      iter;
                int            finished = 0;

                const unsigned riter = bytecode[2];
                rdest                = bytecode[1];

                assert(riter < num_regs);
                assert(rdest < num_regs);

                iter.o = read_reg(stack_frame, riter);

                if (GET_OBJ_TYPE(iter.o) != OBJ_ITERATOR)
                    RAISE_EXCEPTION_STR(str_err_not_callable);

                store_instr_offs(stack_frame, bytecode);

                KOS_init_local_with(ctx, &iter, iter.o);

                if (OBJPTR(ITERATOR, iter.o)->type == OBJ_FUNCTION) {

                    KOS_LOCAL          func;
                    KOS_FUNCTION_STATE state;

                    func.o = OBJPTR(ITERATOR, iter.o)->obj;

                    assert(GET_OBJ_TYPE(func.o) == OBJ_FUNCTION);

                    state = get_func_state(func.o);

                    if (state == KOS_GEN_DONE) {
                        KOS_raise_generator_end(ctx);
                        KOS_destroy_top_local(ctx, &iter);
                        goto cleanup;
                    }

#ifndef CONFIG_DEEP_STACK
                    KOS_init_local_with(ctx, &func, func.o);

                    if ((state == KOS_GEN_READY || state == KOS_GEN_ACTIVE) && ! OBJPTR(FUNCTION, func.o)->handler) {

                        KOS_OBJ_ID this_obj = KOS_VOID;
                        KOS_OBJ_ID args_obj = KOS_EMPTY_ARRAY;

                        error = prepare_call(ctx, instr, func.o, &this_obj,
                                             &args_obj, 0, 0, (uint8_t)rdest);
                        if (error) {
                            KOS_destroy_top_locals(ctx, &func, &iter);
                            goto cleanup;
                        }

                        ++depth;

                        out         = KOS_BADPTR;
                        stack       = ctx->stack;
                        stack_frame = get_current_stack_frame(ctx);
#ifndef NDEBUG
                        regs_idx    = ctx->regs_idx;
                        num_regs    = get_num_regs(ctx);
#endif
                        module      = get_module(stack_frame);
                        assert( ! IS_BAD_PTR(module));
                        assert(OBJPTR(MODULE, module)->inst);
                        assert( ! kos_is_heap_object(module));
                        assert( ! kos_is_heap_object(stack));

                        bytecode    = get_bytecode(stack_frame);

                        KOS_destroy_top_locals(ctx, &func, &iter);

                        NEXT_INSTRUCTION;
                    }

                    KOS_destroy_top_local(ctx, &func);
#endif
                }

                error = KOS_iterator_next(ctx, iter.o);

                iter.o = KOS_destroy_top_local(ctx, &iter);

                if ( ! error) {
                    KOS_OBJ_ID key_obj = KOS_get_walk_key(iter.o);

                    if (GET_OBJ_TYPE(key_obj) == OBJ_STRING) {
                        KOS_OBJ_ID pair;
                        KOS_OBJ_ID value;

                        KOS_init_local_with(ctx, &iter, iter.o);

                        pair = KOS_new_array(ctx, 2);

                        iter.o = KOS_destroy_top_local(ctx, &iter);

                        TRY_OBJID(pair);

                        value = KOS_get_walk_value(iter.o);

                        if (GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                            KOS_LOCAL saved_pair;

                            KOS_init_local_with(ctx, &iter,       iter.o);
                            KOS_init_local_with(ctx, &saved_pair, pair);

                            store_instr_offs(stack_frame, bytecode);

                            value = OBJPTR(DYNAMIC_PROP, value)->getter;

                            value = KOS_call_function(ctx,
                                                      value,
                                                      KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, iter.o)->obj),
                                                      KOS_EMPTY_ARRAY);

                            assert(ctx->regs_idx == regs_idx);

                            iter.o = KOS_destroy_top_locals(ctx, &saved_pair, &iter);

                            TRY_OBJID(value);
                        }

                        TRY(KOS_array_write(ctx, pair, 0, KOS_get_walk_key(iter.o)));
                        TRY(KOS_array_write(ctx, pair, 1, value));

                        write_reg(stack_frame, rdest, pair);
                    }
                    else
                        write_reg(stack_frame, rdest, KOS_get_walk_value(iter.o));
                }
                else if (error == KOS_ERROR_NOT_FOUND) {
                    assert( ! KOS_is_exception_pending(ctx));

                    write_reg(stack_frame, rdest, KOS_VOID);

                    finished = 1;

                    error = KOS_SUCCESS;
                }
                else {
                    assert(error == KOS_ERROR_EXCEPTION);
                    assert(KOS_is_exception_pending(ctx));
                    goto cleanup;
                }

                if (instr == INSTR_NEXT) {
                    if (finished) {
                        KOS_raise_generator_end(ctx);
                        goto cleanup;
                    }

                    bytecode += 3;
                }
                else {
                    KOS_help_gc(ctx);

                    bytecode += finished ? 0 : (int32_t)load_32(bytecode + 3);
                    bytecode += 7;
                }

                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(TAIL_CALL): /* <r.func>, <r.this>, <r.args> */
                /* fall through */
            BEGIN_INSTRUCTION(TAIL_CALL_N): /* <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(TAIL_CALL_FUN): /* <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL_N): /* <r.dest>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL_FUN): /* <r.dest>, <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            BEGIN_INSTRUCTION(CALL): { /* <r.dest>, <r.func>, <r.this>, <r.args> */
                PROF_ZONE_N(INSTR, "CALL")
                unsigned           rfunc     = ~0U;
                unsigned           rthis     = ~0U;
                unsigned           rargs     = ~0U;
                unsigned           rarg1     = ~0U;
                unsigned           num_args  = 0;
                int                tail_call = 0;
                int                delta;
                KOS_FUNCTION_STATE state;

                KOS_LOCAL      func;
                KOS_LOCAL      ret;
                KOS_LOCAL      this_;
                KOS_LOCAL      args;

                KOS_init_locals(ctx, &func, &ret, &this_, &args, kos_end_locals);

                switch (instr) {

                    case INSTR_TAIL_CALL:
                        rdest     = ~0U;
                        rfunc     = bytecode[1];
                        rthis     = bytecode[2];
                        rargs     = bytecode[3];
                        tail_call = 1;
                        delta     = 4;
                        break;

                    case INSTR_TAIL_CALL_N:
                        rdest     = ~0U;
                        rfunc     = bytecode[1];
                        rthis     = bytecode[2];
                        rarg1     = bytecode[3];
                        num_args  = bytecode[4];
                        tail_call = 1;
                        delta     = 5;
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    case INSTR_TAIL_CALL_FUN:
                        rdest     = ~0U;
                        rfunc     = bytecode[1];
                        rarg1     = bytecode[2];
                        num_args  = bytecode[3];
                        tail_call = 1;
                        delta     = 4;
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    case INSTR_CALL:
                        rdest = bytecode[1];
                        rfunc = bytecode[2];
                        rthis = bytecode[3];
                        rargs = bytecode[4];
                        delta = 5;
                        assert(rdest < num_regs);
                        break;

                    case INSTR_CALL_N:
                        rdest    = bytecode[1];
                        rfunc    = bytecode[2];
                        rthis    = bytecode[3];
                        rarg1    = bytecode[4];
                        num_args = bytecode[5];
                        delta    = 6;
                        assert(rdest < num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;

                    default:
                        assert(instr == INSTR_CALL_FUN);
                        rdest    = bytecode[1];
                        rfunc    = bytecode[2];
                        rarg1    = bytecode[3];
                        num_args = bytecode[4];
                        delta    = 5;
                        assert(rdest < num_regs);
                        assert( ! num_args || rarg1 + num_args <= num_regs);
                        break;
                }

                if (rthis != ~0U) {
                    assert(rthis < num_regs);

                    this_.o = read_reg(stack_frame, rthis);
                    assert( ! IS_BAD_PTR(this_.o));
                }
                else
                    this_.o = KOS_VOID;

                assert(rfunc < num_regs);

                func.o = read_reg(stack_frame, rfunc);
                assert( ! IS_BAD_PTR(func.o));

                if (rargs != ~0U) {
                    assert(rargs < num_regs);
                    args.o = read_reg(stack_frame, rargs);
                }

                store_instr_offs(stack_frame, bytecode);

                switch (GET_OBJ_TYPE(func.o)) {

                    case OBJ_CLASS:
                        this_.o = create_this(ctx, func.o);
                        if (IS_BAD_PTR(this_.o)) {
                            KOS_destroy_top_locals(ctx, &func, &args);
                            goto cleanup;
                        }
                        /* fall through */
                    case OBJ_FUNCTION:
                        break;

                    default:
                        KOS_destroy_top_locals(ctx, &func, &args);
                        RAISE_EXCEPTION_STR(str_err_not_callable);
                }

                error = prepare_call(ctx, instr, func.o, &this_.o,
                                     &args.o, num_args ? rarg1 : 0, num_args,
                                     (uint8_t)rdest);
                if (error) {
                    KOS_destroy_top_locals(ctx, &func, &args);
                    goto cleanup;
                }

                state = get_func_state(func.o);

                if (state == KOS_GEN_INIT)
                    out = this_.o;

                else {
                    if ( ! OBJPTR(FUNCTION, func.o)->handler)  {
#ifdef CONFIG_DEEP_STACK
                        ret.o = execute(ctx);

                        assert(IS_BAD_PTR(ret.o) || GET_OBJ_TYPE(ret.o) <= OBJ_LAST_TYPE);

                        if (IS_BAD_PTR(ret.o))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            assert((state != KOS_CTOR) || (ret.o == this_.o));
                        }
#else
                        /* TODO to handle TAIL_CALL, pop previous frame before prepare_call (?) */

                        ++depth;

                        out         = KOS_BADPTR;
                        error       = KOS_SUCCESS;
                        stack       = ctx->stack;
                        stack_frame = get_current_stack_frame(ctx);
#ifndef NDEBUG
                        regs_idx    = ctx->regs_idx;
                        num_regs    = get_num_regs(ctx);
#endif
                        module      = get_module(stack_frame);
                        assert( ! IS_BAD_PTR(module));
                        assert(OBJPTR(MODULE, module)->inst);
                        assert( ! kos_is_heap_object(module));
                        assert( ! kos_is_heap_object(stack));

                        bytecode    = get_bytecode(stack_frame);

                        KOS_destroy_top_locals(ctx, &func, &args);

                        NEXT_INSTRUCTION;
#endif
                    }
                    else {
                        if (IS_BAD_PTR(args.o)) {
                            assert(KOS_is_exception_pending(ctx));
                            error = KOS_ERROR_EXCEPTION;
                        }
                        else {
                            PROF_ZONE(VM)
                            PROF_ZONE_NAME_FUN(func.o)

                            ret.o = OBJPTR(FUNCTION, func.o)->handler(ctx, this_.o, args.o);

                            assert(IS_BAD_PTR(ret.o) || GET_OBJ_TYPE(ret.o) <= OBJ_LAST_TYPE);

                            assert(ctx->local_list == &func);

                            if (state >= KOS_GEN_INIT) {
                                /* Avoid detecting as end of iterator in finish_call() */
                                if ( ! IS_BAD_PTR(ret.o))
                                    clear_stack_flag(ctx, KOS_CAN_YIELD);

                                if (KOS_is_exception_pending(ctx))
                                    error = KOS_ERROR_EXCEPTION;
                            }
                            else if (IS_BAD_PTR(ret.o)) {
                                assert(KOS_is_exception_pending(ctx));
                                error = KOS_ERROR_EXCEPTION;
                            }
                            else {
                                assert( ! KOS_is_exception_pending(ctx));
                            }
                        }

                        if (error) {
                            assert(IS_BAD_PTR(ret.o));
                            kos_wrap_exception(ctx);
                        }
                    }

                    assert( ! error || KOS_is_exception_pending(ctx));
                    assert(error || ! KOS_is_exception_pending(ctx));
                    assert(ctx->local_list == &func);

                    finish_call(ctx, get_current_stack_frame(ctx), instr);

                    out = ret.o;

                    assert(ctx->regs_idx == regs_idx);

                    if (KOS_is_exception_pending(ctx))
                        error = KOS_ERROR_EXCEPTION;
                }

                assert(ctx->stack == stack);
                KOS_destroy_top_locals(ctx, &func, &args);

                if (error) {
                    assert(KOS_is_exception_pending(ctx));
                    goto cleanup;
                }

                assert( ! KOS_is_exception_pending(ctx));

                if (tail_call) {
                    set_stack_flag(ctx, KOS_GENERATOR_DONE);

                    assert(error == KOS_SUCCESS);
                    assert( ! IS_BAD_PTR(out));
                    goto handle_return;
                }

                assert(rdest < num_regs);
                write_reg(stack_frame, rdest, out);

                bytecode += delta;
                NEXT_INSTRUCTION;
            }

            /* TODO remove closure size from RETURN and TAIL.CALL */

            BEGIN_INSTRUCTION(RETURN): { /* <r.src> */
                PROF_ZONE_N(INSTR, "RETURN")
                const unsigned rsrc = bytecode[1];

                assert(regs_idx >= 3);
                assert(rsrc < num_regs);

                out = read_reg(stack_frame, rsrc);

                assert(GET_OBJ_TYPE(out) <= OBJ_LAST_TYPE);

                set_stack_flag(ctx, KOS_GENERATOR_DONE);

                error = KOS_SUCCESS;
                assert( ! IS_BAD_PTR(out));
                goto handle_return;
            }

            BEGIN_INSTRUCTION(YIELD): { /* <r.dest>, <r.src> */
                PROF_ZONE_N(INSTR, "YIELD")
                const uint8_t rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rsrc  < num_regs);
                assert(rdest < num_regs);

                if ( ! (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, ctx->stack)->flags) & KOS_CAN_YIELD))
                    RAISE_EXCEPTION_STR(str_err_cannot_yield);

                out = read_reg(stack_frame, rsrc);

                assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, ctx->stack)->flags) & KOS_REENTRANT_STACK);
                OBJPTR(STACK, ctx->stack)->yield_reg = (uint8_t)rdest;

                clear_stack_flag(ctx, KOS_CAN_YIELD);

                bytecode += 3;

                error = KOS_SUCCESS;
                assert( ! IS_BAD_PTR(out));
                goto handle_return;
            }

            BEGIN_INSTRUCTION(CATCH): { /* <r.dest>, <delta.int32> */
                PROF_ZONE_N(INSTR, "CATCH")
                const int32_t  rel_offs = (int32_t)load_32(bytecode+2);
                const uint32_t offset   = get_instr_offs(stack_frame, bytecode) + 6 + rel_offs;

                rdest = bytecode[1];

                assert(rdest  < num_regs);
                assert(offset < get_bytecode_size(stack_frame));

                set_catch(stack_frame, offset, (uint8_t)rdest);

                bytecode += 6;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(CANCEL): {
                PROF_ZONE_N(INSTR, "CANCEL")
                clear_catch(stack_frame);
                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_BREAKPOINT_INSTRUCTION: {
                PROF_ZONE_N(INSTR, "BREAKPOINT")
                assert(instr == INSTR_BREAKPOINT);
                if (instr != INSTR_BREAKPOINT)
                    RAISE_EXCEPTION_STR(str_err_invalid_instruction);

                /* TODO simply call a debugger function from instance */

                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(THROW): { /* <r.src> */
                PROF_ZONE_N(INSTR, "THROW")
                const unsigned rsrc = bytecode[1];

                assert(rsrc < num_regs);

                KOS_raise_exception(ctx, read_reg(stack_frame, rsrc));
            }

cleanup:
            {
                PROF_ZONE_N(INSTR, "exception")
                uint32_t catch_offs;
                uint8_t  catch_reg;

                assert(KOS_is_exception_pending(ctx));

                store_instr_offs(stack_frame, bytecode);

                kos_wrap_exception(ctx);

                catch_offs = get_catch(stack_frame, &catch_reg);

                if (catch_offs == KOS_NO_CATCH) {

                    set_stack_flag(ctx, KOS_GENERATOR_DONE);

                    error = KOS_ERROR_EXCEPTION;
                    out   = KOS_BADPTR;
                    goto handle_return;
                }

                assert(catch_reg < num_regs);
                write_reg(stack_frame, catch_reg, KOS_get_exception(ctx));

                bytecode = get_bytecode_at_offs(stack_frame, catch_offs);

                clear_catch(stack_frame);
                KOS_clear_exception(ctx);
                NEXT_INSTRUCTION;
            }

handle_return:
            store_instr_offs(stack_frame, bytecode);

            if (depth) {
                PROF_ZONE_N(INSTR, "return")

                KOS_OBJ_ID         num_regs_obj;
                const KOS_OBJ_ID   func_obj = get_current_func(stack_frame);
                size_t             size;
                KOS_BYTECODE_INSTR call_instr;

                assert( ! error || KOS_is_exception_pending(ctx));
                assert(error || ! KOS_is_exception_pending(ctx));

                assert(depth > 0);
                --depth;

                size         = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
                num_regs_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1]);

                assert(IS_SMALL_INT(num_regs_obj));

                rdest      = (uint8_t)(GET_SMALL_INT(num_regs_obj) >> 8);
                call_instr = (KOS_BYTECODE_INSTR)(uint8_t)(GET_SMALL_INT(num_regs_obj) >> 16);

                assert((call_instr == INSTR_NEXT) ||
                       (call_instr == INSTR_NEXT_JUMP) ||
                       ((call_instr >= INSTR_CALL) && (call_instr <= INSTR_TAIL_CALL_FUN)));

                set_closure_stack_size(ctx, stack, stack_frame);

                finish_call(ctx, stack_frame, call_instr);

                if (KOS_is_exception_pending(ctx))
                    error = KOS_ERROR_EXCEPTION;

                stack       = ctx->stack;
                stack_frame = get_current_stack_frame(ctx);
#ifndef NDEBUG
                regs_idx    = ctx->regs_idx;
                num_regs    = get_num_regs(ctx);
#endif
                module      = get_module(stack_frame);
                assert( ! IS_BAD_PTR(module));
                assert(OBJPTR(MODULE, module)->inst);
                assert( ! kos_is_heap_object(module));
                assert( ! kos_is_heap_object(stack));
                bytecode    = get_bytecode(stack_frame);

                if (error &&
                    (call_instr == INSTR_NEXT_JUMP) &&
                    (get_func_state(func_obj) == KOS_GEN_DONE) &&
                    is_generator_end_exception(ctx)) {

                    KOS_clear_exception(ctx);
                    error = KOS_SUCCESS;
                }

                if (error) {
                    assert(KOS_is_exception_pending(ctx));
                    out = KOS_BADPTR;
                    goto cleanup;
                }

                assert((KOS_BYTECODE_INSTR)*bytecode == call_instr);

                switch (call_instr) {

                    case INSTR_NEXT_JUMP:
                        if (get_func_state(func_obj) != KOS_GEN_DONE)
                            bytecode += (int32_t)load_32(bytecode + 3);
                        else
                            out = KOS_VOID;

                        bytecode += 7;
                        break;

                    case INSTR_NEXT:
                        if (get_func_state(func_obj) == KOS_GEN_DONE) {
                            if (rdest != KOS_NO_REG) {
                                assert(rdest < num_regs);
                                write_reg(stack_frame, rdest, KOS_VOID);
                            }

                            assert( ! KOS_is_exception_pending(ctx));
                            KOS_raise_generator_end(ctx);
                            out = KOS_BADPTR;
                            goto cleanup;
                        }

                        bytecode += 3;
                        break;

                    case INSTR_CALL_N:
                        bytecode += 6;
                        break;

                    case INSTR_CALL:
                        /* fall through */
                    case INSTR_CALL_FUN:
                        bytecode += 5;
                        break;

                    default:
                        assert(((KOS_BYTECODE_INSTR)*bytecode == INSTR_TAIL_CALL) ||
                               ((KOS_BYTECODE_INSTR)*bytecode == INSTR_TAIL_CALL_FUN) ||
                               ((KOS_BYTECODE_INSTR)*bytecode == INSTR_TAIL_CALL_N));
                        goto handle_return;
                }

                if (rdest != KOS_NO_REG) {
                    assert(rdest < num_regs);
                    assert( ! IS_BAD_PTR(out));
                    write_reg(stack_frame, rdest, out);
                }

                NEXT_INSTRUCTION;
            }
            else
                set_closure_stack_size(ctx, stack, stack_frame);

            if (error) {
                assert(KOS_is_exception_pending(ctx));
                assert(IS_BAD_PTR(out));
            }
            else {
                assert( ! KOS_is_exception_pending(ctx));
                assert( ! IS_BAD_PTR(out));
            }
            return out;

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

    kos_validate_context(ctx);

    type = GET_OBJ_TYPE(func_obj);

    if (type != OBJ_FUNCTION && type != OBJ_CLASS) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_callable));
        return KOS_BADPTR;
    }

    KOS_init_local(     ctx, &ret);
    KOS_init_local_with(ctx, &args,  args_obj);
    KOS_init_local_with(ctx, &this_, this_obj);
    KOS_init_local_with(ctx, &func,  func_obj);

    state = get_func_state(func.o);

    if (type == OBJ_CLASS && (call_flavor != KOS_APPLY_FUNCTION || this_.o == KOS_VOID)) {
        assert(state == KOS_CTOR);
        this_.o = create_this(ctx, func.o);

        if (IS_BAD_PTR(this_.o)) {
            KOS_destroy_top_locals(ctx, &func, &ret);
            return KOS_BADPTR;
        }
    }

    error = prepare_call(ctx, INSTR_CALL, func.o, &this_.o, &args.o, 0, 0, KOS_NO_REG);

    if (error) {
        KOS_destroy_top_locals(ctx, &func, &ret);
        return KOS_BADPTR;
    }

    if (state == KOS_GEN_INIT)
        ret.o = this_.o;

    else {
        if (OBJPTR(FUNCTION, func.o)->handler)  {
            PROF_ZONE(VM)
            PROF_ZONE_NAME_FUN(func.o)

            ret.o = OBJPTR(FUNCTION, func.o)->handler(ctx, this_.o, args.o);

            assert(ctx->local_list == &func);

            /* Avoid detecting as end of iterator in finish_call() */
            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(ret.o))
                clear_stack_flag(ctx, KOS_CAN_YIELD);

            if (KOS_is_exception_pending(ctx)) {
                assert(IS_BAD_PTR(ret.o));
                error = KOS_ERROR_EXCEPTION;
                kos_wrap_exception(ctx);
            }
            else {
                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(ret.o));
            }
        }
        else {
            ret.o = execute(ctx);
            assert( ! IS_BAD_PTR(ret.o) || KOS_is_exception_pending(ctx));
            assert(ctx->local_list == &func);

            assert(IS_BAD_PTR(ret.o) || GET_OBJ_TYPE(ret.o) <= OBJ_LAST_TYPE);
            assert((state != KOS_CTOR) || (ret.o == this_.o));
        }

        finish_call(ctx, get_current_stack_frame(ctx),
                    call_flavor == KOS_CALL_GENERATOR ? INSTR_NEXT_JUMP : INSTR_CALL);

        if ((state > KOS_GEN_INIT) && (get_func_state(func.o) == KOS_GEN_DONE)) {
            if (call_flavor == KOS_CALL_GENERATOR &&
                KOS_is_exception_pending(ctx) &&
                is_generator_end_exception(ctx)) {

                KOS_clear_exception(ctx);
            }

            ret.o = KOS_BADPTR;
        }
    }

    ret.o = KOS_destroy_top_locals(ctx, &func, &ret);

    return error ? KOS_BADPTR : ret.o;
}

KOS_OBJ_ID KOS_run_module(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int        error;
    KOS_OBJ_ID func_obj;
    KOS_OBJ_ID ret_obj = KOS_BADPTR;
    int        pushed  = 0;

    assert( ! IS_BAD_PTR(module_obj));
    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);
    assert(OBJPTR(MODULE, module_obj)->inst);

    func_obj = KOS_array_read(ctx,
                              OBJPTR(MODULE, module_obj)->constants,
                              OBJPTR(MODULE, module_obj)->main_idx);

    if ( ! IS_BAD_PTR(func_obj)) {
        assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION);
        assert(GET_OBJ_TYPE(OBJPTR(FUNCTION, func_obj)->module) == OBJ_MODULE);

        error = kos_stack_push(ctx, func_obj, KOS_NO_REG, INSTR_CALL);

        if ( ! error)
            pushed = 1;
    }
    else
        error = KOS_ERROR_EXCEPTION;

    if ( ! error) {

        kos_validate_context(ctx);

        ret_obj = execute(ctx);

        if (IS_BAD_PTR(ret_obj)) {
            error = KOS_ERROR_EXCEPTION;
            assert(KOS_is_exception_pending(ctx));
        }

        assert( ! KOS_is_exception_pending(ctx) || error == KOS_ERROR_EXCEPTION);
    }

    if (pushed)
        kos_stack_pop(ctx);

    assert(error == KOS_SUCCESS || error == KOS_ERROR_EXCEPTION);
    assert( ! error || IS_BAD_PTR(ret_obj));
    assert(error || ! IS_BAD_PTR(ret_obj));

    return ret_obj;
}

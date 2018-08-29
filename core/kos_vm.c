/*
 * Copyright (c) 2014-2018 Chris Dragan
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
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include "kos_vm.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static const char str_err_args_not_array[]       = "function arguments are not an array";
static const char str_err_cannot_yield[]         = "function is not a generator";
static const char str_err_corrupted_defaults[]   = "argument defaults are corrupted";
static const char str_err_div_by_zero[]          = "division by zero";
static const char str_err_generator_running[]    = "generator is running";
static const char str_err_invalid_byte_value[]   = "buffer element value out of range";
static const char str_err_invalid_index[]        = "index out of range";
static const char str_err_invalid_instruction[]  = "invalid instruction";
static const char str_err_not_callable[]         = "object is not callable";
static const char str_err_not_generator[]        = "function is not a generator";
static const char str_err_not_indexable[]        = "object is not indexable";
static const char str_err_slice_not_function[]   = "slice is not a function";
static const char str_err_too_few_args[]         = "not enough arguments passed to a function";
static const char str_err_unsup_operand_types[]  = "unsupported operand types";
static const char str_slice[]                    = "slice";
static const char str_value[]                    = "value";

DECLARE_STATIC_CONST_OBJECT(_new_this) = KOS_CONST_OBJECT_INIT(OBJ_OPAQUE, 0xC0);

#define NEW_THIS KOS_CONST_ID(_new_this)

static int _exec_function(KOS_YARN yarn);

static KOS_OBJ_ID _make_string(KOS_YARN            yarn,
                               struct _KOS_MODULE *module,
                               int                 idx)
{
    return KOS_array_read(yarn, module->constants_storage, idx);
}

static KOS_OBJ_ID _add_integer(KOS_YARN   yarn,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(yarn, a + GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(yarn, a + OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(yarn, a + OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _add_float(KOS_YARN   yarn,
                             double     a,
                             KOS_OBJ_ID bobj)
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
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            return KOS_BADPTR;
    }

    return KOS_new_float(yarn, a + b);
}

static KOS_OBJ_ID _sub_integer(KOS_YARN   yarn,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(yarn, a - GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(yarn, a - OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(yarn, a - OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _sub_float(KOS_YARN   yarn,
                             double     a,
                             KOS_OBJ_ID bobj)
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
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            return KOS_BADPTR;
    }

    return KOS_new_float(yarn, a - b);
}

static KOS_OBJ_ID _mul_integer(KOS_YARN   yarn,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj))
        ret = KOS_new_int(yarn, a * GET_SMALL_INT(bobj));

    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_INTEGER:
            ret = KOS_new_int(yarn, a * OBJPTR(INTEGER, bobj)->value);
            break;

        case OBJ_FLOAT:
            ret = KOS_new_float(yarn, a * OBJPTR(FLOAT, bobj)->value);
            break;

        default:
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _mul_float(KOS_YARN   yarn,
                             double     a,
                             KOS_OBJ_ID bobj)
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
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            return KOS_BADPTR;
    }

    return KOS_new_float(yarn, a * b);
}

static KOS_OBJ_ID _div_integer(KOS_YARN   yarn,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj)) {

        const int64_t b = GET_SMALL_INT(bobj);

        if (b)
            ret = KOS_new_int(yarn, a / b);
        else {
            KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
            ret = KOS_BADPTR;
        }
    }
    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_FLOAT: {

            const double b = OBJPTR(FLOAT, bobj)->value;

            if (b != 0)
                ret = KOS_new_float(yarn, (double)a / b);
            else {
                KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
            break;
        }

        case OBJ_INTEGER: {

            const int64_t b = OBJPTR(INTEGER, bobj)->value;

            if (b)
                ret = KOS_new_int(yarn, a / b);
            else {
                KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
            break;
        }

        default:
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _div_float(KOS_YARN   yarn,
                             double     a,
                             KOS_OBJ_ID bobj)
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
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            return KOS_BADPTR;
    }

    if (b != 0)
        ret = KOS_new_float(yarn, a / b);
    else {
        KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _mod_integer(KOS_YARN   yarn,
                               int64_t    a,
                               KOS_OBJ_ID bobj)
{
    KOS_OBJ_ID ret;

    if (IS_SMALL_INT(bobj)) {

        const int64_t b = GET_SMALL_INT(bobj);

        if (b)
            ret = KOS_new_int(yarn, a % b);
        else {
            KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
            ret = KOS_BADPTR;
        }
    }
    else switch (GET_OBJ_TYPE(bobj)) {

        case OBJ_FLOAT: {

            const double b = OBJPTR(FLOAT, bobj)->value;

            if (b != 0)
                ret = KOS_new_float(yarn, fmod((double)a, b));
            else {
                KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
            break;
        }

        case OBJ_INTEGER: {

            const int64_t b = OBJPTR(INTEGER, bobj)->value;

            if (b)
                ret = KOS_new_int(yarn, a % b);
            else {
                KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
                ret = KOS_BADPTR;
            }
            break;
        }

        default:
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            ret = KOS_BADPTR;
            break;
    }

    return ret;
}

static KOS_OBJ_ID _mod_float(KOS_YARN   yarn,
                             double     a,
                             KOS_OBJ_ID bobj)
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
            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
            return KOS_BADPTR;
    }

    if (b != 0)
        ret = KOS_new_float(yarn, fmod(a, b));
    else {
        KOS_raise_exception_cstring(yarn, str_err_div_by_zero);
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
    int            ret;
    const KOS_TYPE a_type = IS_SMALL_INT(aobj) ? OBJ_INTEGER : GET_OBJ_TYPE(aobj);
    const KOS_TYPE b_type = IS_SMALL_INT(bobj) ? OBJ_INTEGER : GET_OBJ_TYPE(bobj);

    if (a_type != OBJ_FLOAT && b_type != OBJ_FLOAT) {

        const int64_t a = IS_SMALL_INT(aobj) ? GET_SMALL_INT(aobj)
                                             : OBJPTR(INTEGER, aobj)->value;
        const int64_t b = IS_SMALL_INT(bobj) ? GET_SMALL_INT(bobj)
                                             : OBJPTR(INTEGER, bobj)->value;

        ret = _compare_integer(instr, a, b);
    }
    else {

        const double a = (a_type == OBJ_FLOAT)
                            ? OBJPTR(FLOAT, aobj)->value
                            : IS_SMALL_INT(aobj)
                                ? GET_SMALL_INT(aobj)
                                : OBJPTR(INTEGER, aobj)->value;

        const double b = (b_type == OBJ_FLOAT)
                            ? OBJPTR(FLOAT, bobj)->value
                            : IS_SMALL_INT(bobj)
                                ? GET_SMALL_INT(bobj)
                                : OBJPTR(INTEGER, bobj)->value;

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

static KOS_OBJ_ID _copy_function(KOS_YARN   yarn,
                                 KOS_OBJ_ID obj_id)
{
    KOS_FUNCTION *src;
    KOS_FUNCTION *dest;
    KOS_OBJ_ID    ret;

    if (GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION) {
        src = OBJPTR(FUNCTION, obj_id);
        ret = KOS_new_function(yarn);
        if (IS_BAD_PTR(ret))
            return ret;
        dest = OBJPTR(FUNCTION, ret);
    }
    else {
        assert(GET_OBJ_TYPE(obj_id) == OBJ_CLASS);
        src = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
        ret = KOS_new_class(yarn, KOS_VOID);
        if (IS_BAD_PTR(ret))
            return ret;
        dest = (KOS_FUNCTION *)OBJPTR(CLASS, ret);
    }

    dest->header.flags    = src->header.flags;
    dest->header.num_args = src->header.num_args;
    dest->header.num_regs = src->header.num_regs;
    dest->args_reg        = src->args_reg;
    dest->state           = src->state;
    dest->instr_offs      = src->instr_offs;
    dest->module          = src->module;
    dest->closures        = src->closures;
    dest->defaults        = src->defaults;
    dest->handler         = src->handler;

    return ret;
}

static int _is_generator_end_exception(KOS_YARN yarn)
{
    KOS_CONTEXT *const ctx       = yarn->ctx;
    const KOS_OBJ_ID   exception = KOS_get_exception(yarn);
    int                ret       = 0;

    if (KOS_get_prototype(yarn, exception) == ctx->prototypes.exception_proto) {

        KOS_OBJ_ID value;

        KOS_clear_exception(yarn);

        value = KOS_get_property(yarn, exception, KOS_context_get_cstring(yarn, str_value));

        if (IS_BAD_PTR(value)) {
            KOS_clear_exception(yarn);
            KOS_raise_exception(yarn, exception);
        }
        else if (KOS_get_prototype(yarn, value) != ctx->prototypes.generator_end_proto)
            KOS_raise_exception(yarn, exception);
        else
            ret = 1;
    }

    return ret;
}

static KOS_ATOMIC(KOS_OBJ_ID) *_get_regs(KOS_YARN  yarn,
                                         uint32_t *num_regs)
{
    const KOS_OBJ_ID stack    = yarn->stack;
    const uint32_t   regs_idx = yarn->regs_idx;
    uint32_t         size;

    assert( ! IS_BAD_PTR(stack));
    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);
    assert(regs_idx + 1U < size);

    *num_regs = size - 1U - regs_idx;

    return &OBJPTR(STACK, stack)->buf[regs_idx];
}

static KOS_OBJ_ID _make_args(KOS_YARN                yarn,
                             KOS_ATOMIC(KOS_OBJ_ID) *src,
                             unsigned                num_args)
{
    const KOS_OBJ_ID array_obj = KOS_new_array(yarn, num_args);

    if ( ! IS_BAD_PTR(array_obj) && num_args) {
        KOS_ATOMIC(KOS_OBJ_ID) *dest = _KOS_get_array_buffer(OBJPTR(ARRAY, array_obj));
        KOS_ATOMIC(KOS_OBJ_ID) *end  = src + num_args;

        while (src < end) {

            const KOS_OBJ_ID value = (KOS_OBJ_ID)KOS_atomic_read_ptr(*src);
            KOS_atomic_write_ptr(*dest, value);

            ++src;
            ++dest;
        }
    }

    return array_obj;
}

static KOS_OBJ_ID _slice_args(KOS_YARN                yarn,
                              KOS_OBJ_ID              args_obj,
                              KOS_ATOMIC(KOS_OBJ_ID) *stack_args,
                              unsigned                num_args,
                              unsigned                slice_begin,
                              unsigned                slice_end)
{
    unsigned size = 0;

    if ( ! IS_BAD_PTR(args_obj)) {
        num_args = KOS_get_array_size(args_obj);
        stack_args = _KOS_get_array_buffer(OBJPTR(ARRAY, args_obj));
    }

    if (slice_begin < num_args && slice_begin < slice_end) {

        if (slice_end > num_args)
            slice_end = num_args;

        size = slice_end - slice_begin;
    }

    return _make_args(yarn, stack_args + slice_begin, size);
}

static int _init_registers(KOS_YARN                yarn,
                           KOS_FUNCTION           *func,
                           KOS_OBJ_ID              args_obj,
                           KOS_ATOMIC(KOS_OBJ_ID)* stack_args,
                           unsigned                num_args,
                           KOS_OBJ_ID              this_obj)
{
    int                     error            = KOS_SUCCESS;
    KOS_OBJ_ID              ellipsis_obj     = KOS_BADPTR;
    KOS_ATOMIC(KOS_OBJ_ID) *regs_buf         = 0;
    uint32_t                num_regs         = 0;
    uint32_t                reg              = func->args_reg;
    const uint32_t          num_non_def_args = func->header.num_args;
    const uint32_t          num_def_args     = GET_OBJ_TYPE(func->defaults) == OBJ_ARRAY
                                                ? KOS_get_array_size(func->defaults) : 0;
    const uint32_t          num_named_args   = num_non_def_args + num_def_args;
    const uint32_t          num_arg_regs     = KOS_min(num_named_args, _KOS_MAX_ARGS_IN_REGS);
    const uint32_t          num_input_args   = IS_BAD_PTR(args_obj) ? num_args : KOS_get_array_size(args_obj);

    assert( ! func->handler);

    if (func->header.flags & KOS_FUN_ELLIPSIS) {
        /* args, ellipsis, this */
        assert(func->header.num_regs >= reg + num_arg_regs + 2);
    }
    else {
        /* args, this */
        assert(func->header.num_regs >= reg + num_arg_regs + 1);
    }
    assert(num_input_args >= num_non_def_args);

    if (func->header.flags & KOS_FUN_ELLIPSIS)  {
        if (num_input_args > num_arg_regs)
            ellipsis_obj = _slice_args(yarn, args_obj, stack_args, num_args, num_named_args, ~0U);
        else
            ellipsis_obj = KOS_new_array(yarn, 0);
        TRY_OBJID(ellipsis_obj);
    }

    regs_buf = _get_regs(yarn, &num_regs);

    if (num_named_args <= _KOS_MAX_ARGS_IN_REGS) {

        const uint32_t num_to_move = KOS_min(num_input_args, num_named_args);

        assert(reg + num_to_move <= num_regs);

        if (num_to_move) {
            KOS_ATOMIC(KOS_OBJ_ID) *src_buf = IS_BAD_PTR(args_obj) ? stack_args :
                                              _KOS_get_array_buffer(OBJPTR(ARRAY, args_obj));
            KOS_ATOMIC(KOS_OBJ_ID) *end     = src_buf + num_to_move;

            while (src_buf < end)
                KOS_atomic_write_ptr(regs_buf[reg++],
                                     (KOS_OBJ_ID)KOS_atomic_read_ptr(*(src_buf++)));
        }

        if (num_to_move < num_named_args) {

            KOS_ATOMIC(KOS_OBJ_ID) *src_buf = _KOS_get_array_buffer(OBJPTR(ARRAY, func->defaults));
            KOS_ATOMIC(KOS_OBJ_ID) *src_end = src_buf + num_def_args;

            assert(num_def_args);

            src_buf += num_to_move - num_non_def_args;

            do
                KOS_atomic_write_ptr(regs_buf[reg++],
                                     KOS_atomic_read_ptr(*(src_buf++)));
            while (src_buf < src_end);
        }
    }
    else {
        const uint32_t num_to_move = KOS_min(num_input_args, _KOS_MAX_ARGS_IN_REGS - 1U);

        KOS_OBJ_ID rest_obj = _slice_args(yarn, args_obj, stack_args, num_args, _KOS_MAX_ARGS_IN_REGS - 1, num_named_args);
        TRY_OBJID(rest_obj);

        if (num_to_move) {
            KOS_ATOMIC(KOS_OBJ_ID) *src_buf = IS_BAD_PTR(args_obj) ? stack_args :
                                              _KOS_get_array_buffer(OBJPTR(ARRAY, args_obj));
            KOS_ATOMIC(KOS_OBJ_ID) *end     = src_buf + num_to_move;

            while (src_buf < end)
                KOS_atomic_write_ptr(regs_buf[reg++],
                                     (KOS_OBJ_ID)KOS_atomic_read_ptr(*(src_buf++)));
        }

        if (num_input_args < num_named_args) {

            KOS_ATOMIC(KOS_OBJ_ID) *const src_buf0 = _KOS_get_array_buffer(OBJPTR(ARRAY, func->defaults));
            KOS_ATOMIC(KOS_OBJ_ID)       *src_buf  = src_buf0;

            if (num_to_move > num_non_def_args)
                src_buf += num_to_move - num_non_def_args;

            if (num_to_move < _KOS_MAX_ARGS_IN_REGS - 1U) {

                KOS_ATOMIC(KOS_OBJ_ID)* reg_end = src_buf
                                                + _KOS_MAX_ARGS_IN_REGS - 1U - num_to_move;

                while (src_buf < reg_end)
                    KOS_atomic_write_ptr(regs_buf[reg++],
                                         (KOS_OBJ_ID)KOS_atomic_read_ptr(*(src_buf++)));
            }

            TRY(KOS_array_insert(yarn,
                                 rest_obj,
                                 MAX_INT64,
                                 MAX_INT64,
                                 func->defaults,
                                 src_buf - _KOS_get_array_buffer(OBJPTR(ARRAY, func->defaults)),
                                 MAX_INT64));
        }

        KOS_atomic_write_ptr(regs_buf[reg++], rest_obj);
    }

    if ( ! IS_BAD_PTR(ellipsis_obj))
        KOS_atomic_write_ptr(regs_buf[reg++], ellipsis_obj);

    KOS_atomic_write_ptr(regs_buf[reg++], this_obj);

    assert( ! IS_BAD_PTR(func->closures));

    if (GET_OBJ_TYPE(func->closures) == OBJ_ARRAY) {
        KOS_ATOMIC(KOS_OBJ_ID) *src_buf;
        KOS_ATOMIC(KOS_OBJ_ID) *end;
        uint32_t                src_len;

        src_len = KOS_get_array_size(func->closures);

        assert(src_len > 0);
        assert(reg + src_len <= 256U);
        assert(reg + src_len <= num_regs);

        src_buf = _KOS_get_array_buffer(OBJPTR(ARRAY, func->closures));
        end     = src_buf + src_len;

        while (src_buf < end)
            KOS_atomic_write_ptr(regs_buf[reg++],
                                 (KOS_OBJ_ID)KOS_atomic_read_ptr(*(src_buf++)));
    }
    else {
        assert(GET_OBJ_TYPE(func->closures) == OBJ_VOID);
    }

_error:
    return error;
}

static void _set_handler_reg(KOS_YARN   yarn,
                             KOS_OBJ_ID obj_id)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = yarn->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(OBJPTR(STACK, stack)->header.flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[size - 1]) == TO_SMALL_INT(1));
    KOS_atomic_write_ptr(OBJPTR(STACK, stack)->buf[size - 2], obj_id);
}

static KOS_OBJ_ID _get_handler_reg(KOS_YARN yarn)
{
    uint32_t         size;
    const KOS_OBJ_ID stack = yarn->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    assert(OBJPTR(STACK, stack)->header.flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);
    assert(size > KOS_STACK_EXTRA);

    assert((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[size - 1]) == TO_SMALL_INT(1));

    return (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[size - 2]);
}

static void _write_to_yield_reg(KOS_YARN   yarn,
                                KOS_OBJ_ID obj_id)
{
    const KOS_OBJ_ID        stack = yarn->stack;
    uint32_t                num_regs;
    KOS_ATOMIC(KOS_OBJ_ID) *regs;

    regs = _get_regs(yarn, &num_regs);

    assert((uint32_t)OBJPTR(STACK, stack)->header.yield_reg < num_regs);

    KOS_atomic_write_ptr(regs[OBJPTR(STACK, stack)->header.yield_reg], obj_id);
}

static int _prepare_call(KOS_YARN                yarn,
                         KOS_BYTECODE_INSTR      instr,
                         KOS_OBJ_ID              func_obj,
                         KOS_OBJ_ID             *this_obj,
                         KOS_OBJ_ID              args_obj,
                         KOS_ATOMIC(KOS_OBJ_ID) *stack_args,
                         unsigned                num_args)
{
    int                      error = KOS_SUCCESS;
    KOS_FUNCTION            *func;
    enum _KOS_FUNCTION_STATE state;

    assert( ! IS_BAD_PTR(func_obj));
    if (IS_BAD_PTR(args_obj)) {
        if (num_args) {
            assert(stack_args);
        }
        else {
            assert( ! stack_args);
        }
    }
    else {
        assert( ! num_args && ! stack_args);
    }

    if (GET_OBJ_TYPE(func_obj) != OBJ_CLASS) {
        assert(GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION);
        func  = OBJPTR(FUNCTION, func_obj);
        state = (enum _KOS_FUNCTION_STATE)func->state;
        assert(state != KOS_CTOR);
    }
    else {
        func  = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
        state = KOS_CTOR;
    }

    if (IS_BAD_PTR(args_obj)) {
        if (num_args < func->header.num_args)
            RAISE_EXCEPTION(str_err_too_few_args);
    }
    else {
        if (GET_OBJ_TYPE(args_obj) != OBJ_ARRAY)
            RAISE_EXCEPTION(str_err_args_not_array);

        if (KOS_get_array_size(args_obj) < func->header.num_args)
            RAISE_EXCEPTION(str_err_too_few_args);
    }

    if (instr == INSTR_CALL_GEN && state < KOS_GEN_READY)
        RAISE_EXCEPTION(str_err_not_generator);

    switch (state) {

        /* Constructor function */
        case KOS_CTOR:
            if (*this_obj == NEW_THIS) {
                KOS_CLASS *const class_ptr = OBJPTR(CLASS, func_obj);
                const KOS_OBJ_ID proto_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(class_ptr->prototype);
                assert( ! IS_BAD_PTR(proto_obj));

                if (func->handler)
                    *this_obj = proto_obj;
                else {
                    *this_obj = KOS_new_object_with_prototype(yarn, proto_obj);
                    TRY_OBJID(*this_obj);
                }
            }
            /* fall through */

        /* Regular function */
        case KOS_FUN: {
            TRY(_KOS_stack_push(yarn, func_obj));

            if ( ! func->handler)
                TRY(_init_registers(yarn,
                                    func,
                                    args_obj,
                                    stack_args,
                                    num_args,
                                    *this_obj));

            break;
        }

        /* Instantiate a generator function */
        case KOS_GEN_INIT: {
            KOS_FUNCTION *dest;
            KOS_OBJ_ID    ret;

            ret = _copy_function(yarn, func_obj);
            TRY_OBJID(ret);

            dest = OBJPTR(FUNCTION, ret);

            TRY(_KOS_stack_push(yarn, ret));

            dest->state = KOS_GEN_READY;

            if ( ! func->handler)
                TRY(_init_registers(yarn,
                                    dest,
                                    args_obj,
                                    stack_args,
                                    num_args,
                                    *this_obj));
            else {
                if (IS_BAD_PTR(args_obj)) {
                    args_obj = _make_args(yarn, stack_args, num_args);
                    TRY_OBJID(args_obj);
                }
                _set_handler_reg(yarn, args_obj);
            }

            dest->header.num_args = 0;

            OBJPTR(STACK, yarn->stack)->header.flags |= KOS_CAN_YIELD;

            _KOS_stack_pop(yarn);

            *this_obj = ret;
            break;
        }

        /* Generator function */
        case KOS_GEN_READY:
            /* fall through */
        case KOS_GEN_ACTIVE: {
            assert( ! IS_BAD_PTR(func->generator_stack_frame));

            TRY(_KOS_stack_push(yarn, func_obj));

            if ( ! func->handler) {
                if (state == KOS_GEN_ACTIVE) {

                    KOS_OBJ_ID value;

                    if (IS_BAD_PTR(args_obj))
                        value = num_args ? (KOS_OBJ_ID)KOS_atomic_read_ptr(stack_args[0]) : KOS_VOID;

                    else {

                        num_args = KOS_get_array_size(args_obj);

                        value = num_args ? KOS_array_read(yarn, args_obj, 0) : KOS_VOID;
                    }

                    _write_to_yield_reg(yarn, value);
                }
            }
            else
                *this_obj = _get_handler_reg(yarn);

            /* TODO perform CAS for thread safety */
            func->state = KOS_GEN_RUNNING;

            OBJPTR(STACK, yarn->stack)->header.flags |= KOS_CAN_YIELD;
            break;
        }

        case KOS_GEN_RUNNING:
            RAISE_EXCEPTION(str_err_generator_running);

        default:
            assert(state == KOS_GEN_DONE);
            KOS_raise_generator_end(yarn);
            error = KOS_ERROR_EXCEPTION;
    }

_error:
    return error;
}

static KOS_OBJ_ID _finish_call(KOS_YARN                  yarn,
                               KOS_BYTECODE_INSTR        instr,
                               KOS_FUNCTION             *func,
                               KOS_OBJ_ID                this_obj,
                               enum _KOS_FUNCTION_STATE *state)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if ( ! KOS_is_exception_pending(yarn)) {

        if (func->header.type == OBJ_CLASS && ! func->handler)
            ret = this_obj;
        else
            ret = yarn->retval;

        if (*state >= KOS_GEN_INIT) {
            if (OBJPTR(STACK, yarn->stack)->header.flags & KOS_CAN_YIELD) {
                *state = KOS_GEN_DONE;
                func->state = KOS_GEN_DONE;
                if (instr != INSTR_CALL_GEN)
                    KOS_raise_generator_end(yarn);
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
            *state = KOS_GEN_DONE;
            func->state = KOS_GEN_DONE;
        }
    }

    yarn->retval = KOS_BADPTR;

    _KOS_stack_pop(yarn);

    return ret;
}

static KOS_OBJ_ID _read_buffer(KOS_YARN yarn, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    size = KOS_get_buffer_size(objptr);

    if (idx < 0)
        idx += (int)size;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception_cstring(yarn, str_err_invalid_index);
        ret = KOS_VOID;
    }
    else {
        uint8_t *const buf = KOS_buffer_data(objptr);
        ret = TO_SMALL_INT((int)buf[idx]);
    }

    return ret;
}

static int _write_buffer(KOS_YARN yarn, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error;
    uint32_t size;
    int64_t  byte_value;

    assert(GET_OBJ_TYPE(objptr) == OBJ_BUFFER);

    TRY(KOS_get_integer(yarn, value, &byte_value));

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

static KOS_OBJ_ID _read_stack(KOS_YARN yarn, KOS_OBJ_ID objptr, int idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(OBJPTR(STACK, objptr)->header.flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, objptr)->size);
    assert(size >= 1 + KOS_STACK_EXTRA);

    if (idx < 0)
        idx += (int)size - 1;
    else
        idx += KOS_STACK_EXTRA;

    if ((uint32_t)idx >= size)  {
        KOS_raise_exception_cstring(yarn, str_err_invalid_index);
        ret = KOS_VOID;
    }
    else
        ret = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, objptr)->buf[idx]);

    return ret;
}

static int _write_stack(KOS_YARN yarn, KOS_OBJ_ID objptr, int idx, KOS_OBJ_ID value)
{
    int      error = KOS_SUCCESS;
    uint32_t size;

    assert(GET_OBJ_TYPE(objptr) == OBJ_STACK);
    assert(OBJPTR(STACK, objptr)->header.flags & KOS_REENTRANT_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, objptr)->size);
    assert(size >= 1 + KOS_STACK_EXTRA);

    if (idx < 0)
        idx += (int)size - 1;
    else
        idx += KOS_STACK_EXTRA;


    if ((uint32_t)idx >= size) {
        KOS_raise_exception_cstring(yarn, str_err_invalid_index);
        error = KOS_ERROR_EXCEPTION;
    }
    else
        KOS_atomic_write_ptr(OBJPTR(STACK, objptr)->buf[idx], value);

    return error;
}

static void _set_closure_stack_size(KOS_YARN yarn, unsigned closure_size)
{
    const KOS_OBJ_ID stack = yarn->stack;

    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);

    if (OBJPTR(STACK, stack)->header.flags & KOS_REENTRANT_STACK) {

        const uint32_t size     = closure_size + 1U + KOS_STACK_EXTRA;
        const uint32_t old_size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);

        assert(size <= old_size);

        KOS_atomic_write_u32(OBJPTR(STACK, stack)->size, size);
        KOS_atomic_write_ptr(OBJPTR(STACK, stack)->buf[size - 1], TO_SMALL_INT((int)closure_size));

        yarn->stack_depth -= old_size - size;
    }
}

static KOS_MODULE *_get_module(KOS_ATOMIC(KOS_OBJ_ID) *regs)
{
    const KOS_OBJ_ID func_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(regs[-3]);
    KOS_FUNCTION    *func;

    if (GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION)
        func = OBJPTR(FUNCTION, func_obj);
    else {
        assert(GET_OBJ_TYPE(func_obj) == OBJ_CLASS);
        func = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
    }

    assert( ! IS_BAD_PTR(func->module));
    assert(GET_OBJ_TYPE(func->module) == OBJ_MODULE);

    return OBJPTR(MODULE, func->module);
}

KOS_OBJ_ID KOS_get_module(KOS_YARN yarn)
{
    uint32_t                num_regs;
    KOS_ATOMIC(KOS_OBJ_ID) *regs = _get_regs(yarn, &num_regs);

    assert(regs);

    return OBJID(MODULE, _get_module(regs));
}

static int64_t _load_instr_offs(KOS_ATOMIC(KOS_OBJ_ID) *regs)
{
    assert(IS_SMALL_INT((KOS_OBJ_ID)KOS_atomic_read_ptr(regs[-1])));
    return GET_SMALL_INT((KOS_OBJ_ID)KOS_atomic_read_ptr(regs[-1]));
}

static void _store_instr_offs(KOS_ATOMIC(KOS_OBJ_ID) *regs,
                              uint32_t                instr_offs)
{
    KOS_atomic_write_ptr(regs[-1], TO_SMALL_INT((int64_t)instr_offs));
}

static uint32_t _get_catch(KOS_ATOMIC(KOS_OBJ_ID) *regs, uint8_t *catch_reg)
{
    const KOS_OBJ_ID catch_data_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(regs[-2]);
    uint64_t         catch_data;
    uint32_t         catch_offs;

    assert(IS_SMALL_INT(catch_data_obj));

    catch_data = (uint64_t)GET_SMALL_INT(catch_data_obj);

    catch_offs = (uint32_t)(catch_data >> 8);
    *catch_reg = (uint8_t)(catch_data & 0xFFU);

    return catch_offs;
}

static void _set_catch(KOS_ATOMIC(KOS_OBJ_ID) *regs, uint32_t catch_offs, uint8_t catch_reg)
{
    const uint64_t catch_data = (((uint64_t)catch_offs << 8) | catch_reg);
    assert(catch_offs < KOS_NO_CATCH);
    KOS_atomic_write_ptr(regs[-2], TO_SMALL_INT((int64_t)catch_data));
}

static void _clear_catch(KOS_ATOMIC(KOS_OBJ_ID) *regs)
{
    const uint64_t catch_data = KOS_NO_CATCH << 8;
    KOS_atomic_write_ptr(regs[-2], TO_SMALL_INT((int64_t)catch_data));
}

static uint32_t _load_32(const uint8_t *bytecode)
{
    return (uint32_t)bytecode[0] +
           ((uint32_t)bytecode[1] << 8) +
           ((uint32_t)bytecode[2] << 16) +
           ((uint32_t)bytecode[3] << 24);
}

static int _exec_function(KOS_YARN yarn)
{
    int                     error = KOS_SUCCESS;
    KOS_ATOMIC(KOS_OBJ_ID) *regs;
    uint32_t                num_regs;
    KOS_MODULE             *module;
    KOS_BYTECODE_INSTR      instr;
    const uint8_t          *bytecode;

    regs = _get_regs(yarn, &num_regs);

    module = _get_module(regs);

    assert(module);
    assert(module->context);
    bytecode = module->bytecode + _load_instr_offs(regs);

    for (;;) { /* Exit condition at the end of the loop */

        int32_t    delta = 1;
        KOS_OBJ_ID out   = KOS_BADPTR;
        unsigned   rdest = 0;

        instr = (KOS_BYTECODE_INSTR)*bytecode;

        switch (instr) {

            case INSTR_LOAD_CONST8: { /* <r.dest>, <uint8> */
                const uint8_t value = bytecode[2];

                rdest = bytecode[1];

                assert(value < KOS_get_array_size(module->constants_storage));

                out = module->constants[value];

                delta = 3;
                break;
            }

            case INSTR_LOAD_CONST: { /* <r.dest>, <uint32> */
                const uint32_t value = _load_32(bytecode+2);

                rdest = bytecode[1];

                assert(value < KOS_get_array_size(module->constants_storage));

                out = module->constants[value];

                delta = 6;
                break;
            }

            case INSTR_LOAD_FUN8: { /* <r.dest>, <uint8> */
                const uint8_t value = bytecode[2];

                rdest = bytecode[1];

                assert(value < KOS_get_array_size(module->constants_storage));

                out = _copy_function(yarn, module->constants[value]);

                if ( ! IS_BAD_PTR(out) && GET_OBJ_TYPE(out) == OBJ_CLASS) {
                    assert(value + 1U < KOS_get_array_size(module->constants_storage));
                    KOS_atomic_write_ptr(OBJPTR(CLASS, out)->prototype,
                                         KOS_atomic_read_ptr(module->constants[value + 1]));
                }

                delta = 3;
                break;
            }

            case INSTR_LOAD_FUN: { /* <r.dest>, <uint32> */
                const uint32_t value = _load_32(bytecode+2);

                rdest = bytecode[1];

                assert(value < KOS_get_array_size(module->constants_storage));

                out = _copy_function(yarn, module->constants[value]);

                if ( ! IS_BAD_PTR(out) && GET_OBJ_TYPE(out) == OBJ_CLASS) {
                    assert(value + 1U < KOS_get_array_size(module->constants_storage));
                    KOS_atomic_write_ptr(OBJPTR(CLASS, out)->prototype,
                                         KOS_atomic_read_ptr(module->constants[value + 1]));
                }

                delta = 6;
                break;
            }

            case INSTR_LOAD_INT8: { /* <r.dest>, <int8> */
                const int8_t value = (int8_t)bytecode[2];

                rdest = bytecode[1];
                out   = TO_SMALL_INT(value);
                delta = 3;
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

            case INSTR_LOAD_ARRAY8: { /* <r.dest>, <size.uint8> */
                const uint8_t size = bytecode[2];

                rdest = bytecode[1];
                out   = KOS_new_array(yarn, size);
                delta = 3;
                break;
            }

            case INSTR_LOAD_ARRAY: { /* <r.dest>, <size.int32> */
                const uint32_t size = _load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_new_array(yarn, size);
                delta = 6;
                break;
            }

            case INSTR_LOAD_OBJ: { /* <r.dest> */
                rdest = bytecode[1];
                out   = KOS_new_object(yarn);
                delta = 2;
                break;
            }

            case INSTR_MOVE: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                assert(rsrc  < num_regs);

                rdest = bytecode[1];
                out   = regs[rsrc];
                delta = 3;
                break;
            }

            case INSTR_GET_GLOBAL: { /* <r.dest>, <int32> */
                const int32_t  idx = (int32_t)_load_32(bytecode+2);

                rdest = bytecode[1];
                out   = KOS_array_read(yarn, module->globals, idx);
                delta = 6;
                break;
            }

            case INSTR_SET_GLOBAL: { /* <int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < num_regs);

                error = KOS_array_write(yarn, module->globals, idx, regs[rsrc]);
                delta = 6;
                break;
            }

            case INSTR_GET_MOD: { /* <r.dest>, <int32>, <r.glob> */
                const int      mod_idx    = (int32_t)_load_32(bytecode+2);
                const unsigned rglob      = bytecode[6];
                KOS_OBJ_ID     module_obj = KOS_array_read(yarn, module->context->modules.modules, mod_idx);

                assert(rglob < num_regs);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {

                    KOS_OBJ_ID glob_idx;

                    assert( ! IS_SMALL_INT(module_obj));
                    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                    glob_idx = KOS_get_property(yarn, OBJPTR(MODULE, module_obj)->global_names, regs[rglob]);

                    if (!IS_BAD_PTR(glob_idx)) {

                        assert(IS_SMALL_INT(glob_idx));

                        out = KOS_array_read(yarn, OBJPTR(MODULE, module_obj)->globals,
                                             (int)GET_SMALL_INT(glob_idx));
                    }
                }

                delta = 7;
                break;
            }

            case INSTR_GET_MOD_ELEM: { /* <r.dest>, <int32>, <int32> */
                const int  mod_idx    = (int32_t)_load_32(bytecode+2);
                const int  glob_idx   = (int32_t)_load_32(bytecode+6);
                KOS_OBJ_ID module_obj = KOS_array_read(yarn, module->context->modules.modules, mod_idx);

                rdest = bytecode[1];

                if (!IS_BAD_PTR(module_obj)) {
                    assert( ! IS_SMALL_INT(module_obj));
                    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

                    out = KOS_array_read(yarn, OBJPTR(MODULE, module_obj)->globals, glob_idx);
                }

                delta = 10;
                break;
            }

            case INSTR_GET: { /* <r.dest>, <r.src>, <r.prop> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rprop = bytecode[3];
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];
                src   = regs[rsrc];
                prop  = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(yarn, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception_cstring(yarn, str_err_invalid_index);
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        const enum KOS_OBJECT_TYPE type = GET_OBJ_TYPE(src);
                        if (type == OBJ_STRING)
                            out = KOS_string_get_char(yarn, src, (int)idx);
                        else if (type == OBJ_BUFFER)
                            out = _read_buffer(yarn, src, (int)idx);
                        else
                            out = KOS_array_read(yarn, src, (int)idx);
                    }
                }
                else {
                    KOS_OBJ_ID value = KOS_get_property(yarn, src, prop);

                    if ( ! IS_BAD_PTR(value) && GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args;
                        _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));
                        value = OBJPTR(DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(yarn, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(yarn, value, src, args);
                            if (IS_BAD_PTR(value)) {
                                assert(KOS_is_exception_pending(yarn));
                                error = KOS_ERROR_EXCEPTION;
                            }
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
                KOS_OBJ_ID           src;
                enum KOS_OBJECT_TYPE type;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];
                src   = regs[rsrc];

                type = GET_OBJ_TYPE(src);

                if (type == OBJ_ARRAY)
                    out = KOS_array_read(yarn, src, idx);
                else if (type == OBJ_STRING)
                    out = KOS_string_get_char(yarn, src, idx);
                else if (type == OBJ_BUFFER)
                    out = _read_buffer(yarn, src, idx);
                else if (type == OBJ_STACK)
                    out = _read_stack(yarn, src, idx);
                else
                    KOS_raise_exception_cstring(yarn, str_err_not_indexable);

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

                assert(rsrc   < num_regs);
                assert(rbegin < num_regs);
                assert(rend   < num_regs);

                rdest = bytecode[1];
                src   = regs[rsrc];
                begin = regs[rbegin];
                end   = regs[rend];

                if (IS_SMALL_INT(begin) || GET_OBJ_TYPE(begin) != OBJ_VOID)
                    error = KOS_get_integer(yarn, begin, &begin_idx);
                else
                    begin_idx = 0;

                if ( ! error) {
                    if (IS_SMALL_INT(end) || GET_OBJ_TYPE(end) != OBJ_VOID)
                        error = KOS_get_integer(yarn, end, &end_idx);
                    else
                        end_idx = MAX_INT64;
                }

                if ( ! error) {
                    if (GET_OBJ_TYPE(src) == OBJ_STRING)
                        out = KOS_string_slice(yarn, src, begin_idx, end_idx);
                    else if (GET_OBJ_TYPE(src) == OBJ_BUFFER)
                        out = KOS_buffer_slice(yarn, src, begin_idx, end_idx);
                    else if (GET_OBJ_TYPE(src) == OBJ_ARRAY)
                        out = KOS_array_slice(yarn, src, begin_idx, end_idx);
                    else {
                        out = KOS_get_property(yarn,
                                               src,
                                               KOS_context_get_cstring(yarn, str_slice));
                        if (IS_BAD_PTR(out))
                            error = KOS_ERROR_EXCEPTION;
                        else if (GET_OBJ_TYPE(out) != OBJ_FUNCTION)
                            KOS_raise_exception_cstring(yarn, str_err_slice_not_function);
                        else {
                            KOS_ATOMIC(KOS_OBJ_ID) in_args[2];
                            KOS_OBJ_ID             args;

                            in_args[0] = begin;
                            in_args[1] = end;
                            args       = _make_args(yarn, &in_args[0], 2);

                            if (IS_BAD_PTR(args))
                                error = KOS_ERROR_EXCEPTION;
                            else
                                out = KOS_call_function(yarn, out, src, args);
                        }
                    }
                }

                delta = 5;
                break;
            }

            case INSTR_GET_PROP: { /* <r.dest>, <r.src>, <str.idx.int32> */
                const unsigned rsrc = bytecode[2];
                const int32_t  idx  = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];
                prop  = _make_string(yarn, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_ID obj   = regs[rsrc];
                    KOS_OBJ_ID value = KOS_get_property(yarn, obj, prop);

                    if ( ! IS_BAD_PTR(value) && GET_OBJ_TYPE(value) == OBJ_DYNAMIC_PROP) {
                        KOS_OBJ_ID args;
                        _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));
                        value = OBJPTR(DYNAMIC_PROP, value)->getter;
                        args  = KOS_new_array(yarn, 0);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            value = KOS_call_function(yarn, value, obj, args);
                            if (IS_BAD_PTR(value)) {
                                assert(KOS_is_exception_pending(yarn));
                                error = KOS_ERROR_EXCEPTION;
                            }
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

                assert(rdest < num_regs);
                assert(rprop < num_regs);
                assert(rsrc  < num_regs);

                prop = regs[rprop];

                if (IS_NUMERIC_OBJ(prop)) {
                    int64_t idx;
                    error = KOS_get_integer(yarn, prop, &idx);
                    if (!error) {
                        if (idx > INT_MAX || idx < INT_MIN) {
                            KOS_raise_exception_cstring(yarn, str_err_invalid_index);
                            error = KOS_ERROR_EXCEPTION;
                        }
                    }
                    if (!error) {
                        const KOS_OBJ_ID obj = regs[rdest];
                        if (GET_OBJ_TYPE(obj) == OBJ_BUFFER)
                            error = _write_buffer(yarn, obj, (int)idx, regs[rsrc]);
                        else
                            error = KOS_array_write(yarn, obj, (int)idx, regs[rsrc]);
                    }
                }
                else {
                    KOS_OBJ_ID obj   = regs[rdest];
                    KOS_OBJ_ID value = regs[rsrc];

                    error = KOS_set_property(yarn, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID setter;
                        KOS_OBJ_ID args;

                        assert(KOS_is_exception_pending(yarn));
                        setter = KOS_get_exception(yarn);
                        KOS_clear_exception(yarn);

                        assert( ! IS_BAD_PTR(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                        _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));
                        setter = OBJPTR(DYNAMIC_PROP, setter)->setter;

                        args = _make_args(yarn, (KOS_ATOMIC(KOS_OBJ_ID) *)&value, 1);
                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_SUCCESS;
                            value = KOS_call_function(yarn, setter, obj, args);
                            if (IS_BAD_PTR(value)) {
                                assert(KOS_is_exception_pending(yarn));
                                error = KOS_ERROR_EXCEPTION;
                            }
                        }
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_SET_ELEM: { /* <r.dest>, <int32>, <r.src> */
                const int32_t        idx  = (int32_t)_load_32(bytecode+2);
                const unsigned       rsrc = bytecode[6];
                KOS_OBJ_ID           dest;
                enum KOS_OBJECT_TYPE type;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                dest = regs[rdest];

                type = GET_OBJ_TYPE(dest);

                if (type == OBJ_ARRAY)
                    error = KOS_array_write(yarn, dest, idx, regs[rsrc]);
                else if (type == OBJ_BUFFER)
                    error = _write_buffer(yarn, dest, idx, regs[rsrc]);
                else if (type == OBJ_STACK)
                    error = _write_stack(yarn, dest, idx, regs[rsrc]);
                else
                    KOS_raise_exception_cstring(yarn, str_err_not_indexable);

                delta = 7;
                break;
            }

            case INSTR_SET_PROP: { /* <r.dest>, <str.idx.int32>, <r.src> */
                const int32_t  idx  = (int32_t)_load_32(bytecode+2);
                const unsigned rsrc = bytecode[6];
                KOS_OBJ_ID     prop;

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                prop = _make_string(yarn, module, idx);

                if (!IS_BAD_PTR(prop)) {
                    KOS_OBJ_ID obj   = regs[rdest];
                    KOS_OBJ_ID value = regs[rsrc];

                    error = KOS_set_property(yarn, obj, prop, value);

                    if (error == KOS_ERROR_SETTER) {
                        KOS_OBJ_ID setter;
                        KOS_OBJ_ID args;

                        assert(KOS_is_exception_pending(yarn));
                        setter = KOS_get_exception(yarn);
                        KOS_clear_exception(yarn);

                        assert( ! IS_BAD_PTR(setter) && GET_OBJ_TYPE(setter) == OBJ_DYNAMIC_PROP);
                        _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));
                        setter = OBJPTR(DYNAMIC_PROP, setter)->setter;
                        args   = _make_args(yarn, (KOS_ATOMIC(KOS_OBJ_ID) *)&value, 1);

                        if (IS_BAD_PTR(args))
                            error = KOS_ERROR_EXCEPTION;
                        else {
                            error = KOS_SUCCESS;
                            value = KOS_call_function(yarn, setter, obj, args);
                            if (IS_BAD_PTR(value)) {
                                assert(KOS_is_exception_pending(yarn));
                                error = KOS_ERROR_EXCEPTION;
                            }
                        }
                    }
                }

                delta = 7;
                break;
            }

            case INSTR_PUSH: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                error = KOS_array_push(yarn, regs[rdest], regs[rsrc], 0);

                delta = 3;
                break;
            }

            case INSTR_PUSH_EX: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rsrc  < num_regs);

                error = KOS_array_push_expand(yarn, regs[rdest], regs[rsrc]);

                delta = 3;
                break;
            }

            case INSTR_DEL: { /* <r.dest>, <r.prop> */
                const unsigned rprop = bytecode[2];

                rdest = bytecode[1];

                assert(rdest < num_regs);
                assert(rprop < num_regs);

                KOS_delete_property(yarn, regs[rdest], regs[rprop]);

                delta = 3;
                break;
            }

            case INSTR_DEL_PROP: { /* <r.dest>, <str.idx.int32> */
                const int32_t idx = (int32_t)_load_32(bytecode+2);
                KOS_OBJ_ID    prop;

                rdest = bytecode[1];

                assert(rdest < num_regs);

                prop = _make_string(yarn, module, idx);

                if (!IS_BAD_PTR(prop))
                    KOS_delete_property(yarn, regs[rdest], prop);

                delta = 6;
                break;
            }

            case INSTR_ADD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1)) {
                    const int64_t a = GET_SMALL_INT(src1);
                    out             = _add_integer(yarn, a, src2);
                }
                else {

                    switch (GET_OBJ_TYPE(src1)) {

                        case OBJ_INTEGER:
                            out = _add_integer(yarn, OBJPTR(INTEGER, src1)->value, src2);
                            break;

                        case OBJ_FLOAT:
                            out = _add_float(yarn, OBJPTR(FLOAT, src1)->value, src2);
                            break;

                        case OBJ_STRING: {
                            if (GET_OBJ_TYPE(src2) == OBJ_STRING)
                                out = KOS_string_add(yarn, src1, src2);
                            else
                                KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
                            break;
                        }

                        default:
                            KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
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

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1))
                    out = _sub_integer(yarn, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _sub_integer(yarn, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _sub_float(yarn, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
                        break;
                }

                delta = 4;
                break;
            }

            case INSTR_MUL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1))
                    out = _mul_integer(yarn, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _mul_integer(yarn, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _mul_float(yarn, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
                        break;
                }

                delta = 4;
                break;
            }

            case INSTR_DIV: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1))
                    out = _div_integer(yarn, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _div_integer(yarn, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _div_float(yarn, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
                        break;
                }

                delta = 4;
                break;
            }

            case INSTR_MOD: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];

                KOS_OBJ_ID src1;
                KOS_OBJ_ID src2;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                if (IS_SMALL_INT(src1))
                    out = _mod_integer(yarn, GET_SMALL_INT(src1), src2);

                else switch (GET_OBJ_TYPE(src1)) {

                    case OBJ_INTEGER:
                        out = _mod_integer(yarn, OBJPTR(INTEGER, src1)->value, src2);
                        break;

                    case OBJ_FLOAT:
                        out = _mod_float(yarn, OBJPTR(FLOAT, src1)->value, src2);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_unsup_operand_types);
                        break;
                }

                delta = 4;
                break;
            }

            case INSTR_SHL: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b < 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(yarn, a >> -b);
                        else
                            out = KOS_new_int(yarn, (int64_t)((uint64_t)a << b));
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

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(a < 0 && b > 0 ? -1 : 0);
                        else if (b < 0)
                            out = KOS_new_int(yarn, (int64_t)((uint64_t)a << -b));
                        else
                            out = KOS_new_int(yarn, a >> b);
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

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error) {
                        if (b > 63 || b < -63)
                            out = TO_SMALL_INT(0);
                        else if (b < 0)
                            out = KOS_new_int(yarn, (int64_t)((uint64_t)a << -b));
                        else
                            out = KOS_new_int(yarn, (int64_t)((uint64_t)a >> b));
                    }
                }

                delta = 4;
                break;
            }

            case INSTR_NOT: { /* <r.dest>, <r.src> */
                const unsigned rsrc = bytecode[2];
                int64_t        a;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc], &a);

                if (!error)
                    out = KOS_new_int(yarn, ~a);

                delta = 3;
                break;
            }

            case INSTR_AND: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(yarn, a & b);
                }

                delta = 4;
                break;
            }

            case INSTR_OR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(yarn, a | b);
                }

                delta = 4;
                break;
            }

            case INSTR_XOR: { /* <r.dest>, <r.src1>, <r.src2> */
                const unsigned rsrc1 = bytecode[2];
                const unsigned rsrc2 = bytecode[3];
                int64_t        a;
                int64_t        b;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];

                error = KOS_get_integer(yarn, regs[rsrc1], &a);
                if (!error) {
                    error = KOS_get_integer(yarn, regs[rsrc2], &b);
                    if (!error)
                        out = KOS_new_int(yarn, a ^ b);
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
                static const char t_class[]    = "class";

                const unsigned rsrc  = bytecode[2];
                KOS_OBJ_ID     src;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];
                src   = regs[rsrc];

                assert(!IS_BAD_PTR(src));

                if (IS_SMALL_INT(src))
                    out = KOS_context_get_cstring(yarn, t_integer);

                else switch (GET_OBJ_TYPE(src)) {
                    case OBJ_INTEGER:
                        out = KOS_context_get_cstring(yarn, t_integer);
                        break;

                    case OBJ_FLOAT:
                        out = KOS_context_get_cstring(yarn, t_float);
                        break;

                    case OBJ_STRING:
                        out = KOS_context_get_cstring(yarn, t_string);
                        break;

                    case OBJ_VOID:
                        out = KOS_context_get_cstring(yarn, t_void);
                        break;

                    case OBJ_BOOLEAN:
                        out = KOS_context_get_cstring(yarn, t_boolean);
                        break;

                    case OBJ_ARRAY:
                        out = KOS_context_get_cstring(yarn, t_array);
                        break;

                    case OBJ_BUFFER:
                        out = KOS_context_get_cstring(yarn, t_buffer);
                        break;

                    case OBJ_FUNCTION:
                        out = KOS_context_get_cstring(yarn, t_function);
                        break;

                    case OBJ_CLASS:
                        out = KOS_context_get_cstring(yarn, t_class);
                        break;

                    default:
                        out = KOS_context_get_cstring(yarn, t_object);
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

                enum KOS_OBJECT_TYPE src1_type;
                enum KOS_OBJECT_TYPE src2_type;

                assert(rsrc1 < num_regs);
                assert(rsrc2 < num_regs);

                rdest = bytecode[1];
                src1  = regs[rsrc1];
                src2  = regs[rsrc2];

                src1_type = IS_NUMERIC_OBJ(src1) ? OBJ_INTEGER : READ_OBJ_TYPE(src1);
                src2_type = IS_NUMERIC_OBJ(src2) ? OBJ_INTEGER : READ_OBJ_TYPE(src2);

                if (src1_type == src2_type)
                    switch (src1_type) {

                        case OBJ_INTEGER:
                            ret = _compare_float(instr, src1, src2);
                            break;

                        case OBJ_STRING:
                            ret = _compare_string(instr, src1, src2);
                            break;

                        case OBJ_BOOLEAN:
                            ret = _compare_integer(instr,
                                                   OBJPTR(BOOLEAN, src1)->boolean.value,
                                                   OBJPTR(BOOLEAN, src2)->boolean.value);
                            break;

                        case OBJ_VOID:
                            ret = _compare_integer(instr, 0, 0);
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

                assert(rsrc  < num_regs);
                assert(rprop < num_regs);

                rdest = bytecode[1];

                obj = KOS_get_property(yarn, regs[rsrc], regs[rprop]);
                KOS_clear_exception(yarn);

                out   = KOS_BOOL( ! IS_BAD_PTR(obj));
                delta = 4;
                break;
            }

            case INSTR_HAS_PROP: { /* <r.dest>, <r.src>, <str.idx.int32> */
                const unsigned rsrc  = bytecode[2];
                const int32_t  idx   = (int32_t)_load_32(bytecode+3);
                KOS_OBJ_ID     prop;

                assert(rsrc  < num_regs);

                rdest = bytecode[1];

                prop = _make_string(yarn, module, idx);

                if (!IS_BAD_PTR(prop)) {

                    KOS_OBJ_ID obj = KOS_get_property(yarn, regs[rsrc], prop);
                    KOS_clear_exception(yarn);

                    out = KOS_BOOL( ! IS_BAD_PTR(obj));
                }

                delta = 7;
                break;
            }

            case INSTR_INSTANCEOF: { /* <r.dest>, <r.src>, <r.func> */
                const unsigned rsrc  = bytecode[2];
                const unsigned rfunc = bytecode[3];

                KOS_OBJ_ID     ret   = KOS_FALSE;
                KOS_OBJ_ID     constr_obj;

                assert(rsrc  < num_regs);
                assert(rfunc < num_regs);

                rdest      = bytecode[1];
                constr_obj = regs[rfunc];

                if (GET_OBJ_TYPE(constr_obj) == OBJ_CLASS) {
                    KOS_CLASS *const constr    = OBJPTR(CLASS, constr_obj);
                    KOS_OBJ_ID       proto_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(constr->prototype);

                    assert( ! IS_BAD_PTR(proto_obj));

                    assert(IS_SMALL_INT(proto_obj) || GET_OBJ_TYPE(proto_obj) <= OBJ_LAST_TYPE);

                    if (KOS_has_prototype(yarn, regs[rsrc], proto_obj))
                        ret = KOS_TRUE;
                }

                out   = ret;
                delta = 4;
                break;
            }

            case INSTR_JUMP: { /* <delta.int32> */
                delta = 5 + (int32_t)_load_32(bytecode+1);
#ifdef CONFIG_FUZZ
                if (delta < 5)
                    delta = 5;
#endif
                break;
            }

            case INSTR_JUMP_COND: { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < num_regs);

                delta = 6;

                if (_KOS_is_truthy(regs[rsrc]))
                    delta += offs;
#ifdef CONFIG_FUZZ
                if (delta < 6)
                    delta = 6;
#endif
                break;
            }

            case INSTR_JUMP_NOT_COND: { /* <delta.int32>, <r.src> */
                const int32_t  offs = (int32_t)_load_32(bytecode+1);
                const unsigned rsrc = bytecode[5];

                assert(rsrc < num_regs);

                delta = 6;

                if ( ! _KOS_is_truthy(regs[rsrc]))
                    delta += offs;
#ifdef CONFIG_FUZZ
                if (delta < 6)
                    delta = 6;
#endif
                break;
            }

            case INSTR_BIND_SELF: /* <r.dest>, <slot.idx.uint8> */
                /* fall through */
            case INSTR_BIND: { /* <r.dest>, <slot.idx.uint8>, <r.src> */
                const unsigned idx = bytecode[2];

                KOS_OBJ_ID    dest;
                KOS_FUNCTION *func = 0;

                rdest = bytecode[1];
                assert(rdest < num_regs);
                dest = regs[rdest];

                switch (GET_OBJ_TYPE(dest)) {

                    case OBJ_FUNCTION:
                        func = OBJPTR(FUNCTION, dest);
                        break;

                    case OBJ_CLASS:
                        func = (KOS_FUNCTION *)OBJPTR(CLASS, dest);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_not_callable);
                        break;
                }

                if (func) {
                    KOS_OBJ_ID closures = func->closures;
                    KOS_OBJ_ID regs_obj;

                    if (instr == INSTR_BIND) {
                        const unsigned rsrc = bytecode[3];
                        assert(rsrc < num_regs);
                        regs_obj = regs[rsrc];
                    }
                    else {
                        regs_obj = yarn->stack;
                        assert(OBJPTR(STACK, regs_obj)->header.flags & KOS_REENTRANT_STACK);
                    }

                    assert( ! IS_SMALL_INT(closures));
                    assert(GET_OBJ_TYPE(closures) == OBJ_VOID ||
                           GET_OBJ_TYPE(closures) == OBJ_ARRAY);

                    if (GET_OBJ_TYPE(closures) == OBJ_VOID) {
                        closures = KOS_new_array(yarn, idx+1);
                        if (IS_BAD_PTR(closures))
                            error = KOS_ERROR_EXCEPTION;
                        else
                            func->closures = closures;
                    }
                    else if (idx >= KOS_get_array_size(closures))
                        error = KOS_array_resize(yarn, closures, idx+1);

                    if (!error)
                        error = KOS_array_write(yarn, closures, (int)idx, regs_obj);
                }

                delta = (instr == INSTR_BIND_SELF) ? 3 : 4;
                break;
            }

            case INSTR_BIND_DEFAULTS: { /* <r.dest>, <r.src> */
                KOS_OBJ_ID     src;
                KOS_OBJ_ID     dest;
                const unsigned rsrc = bytecode[2];
                rdest               = bytecode[1];

                assert(rsrc  < num_regs);
                assert(rdest < num_regs);

                src  = regs[rsrc];
                dest = regs[rdest];

                if (GET_OBJ_TYPE(src) != OBJ_ARRAY)
                    KOS_raise_exception_cstring(yarn, str_err_corrupted_defaults);
                else {

                    KOS_FUNCTION *func = 0;

                    switch (GET_OBJ_TYPE(dest)) {

                        case OBJ_FUNCTION:
                            func = OBJPTR(FUNCTION, dest);
                            break;

                        case OBJ_CLASS:
                            func = (KOS_FUNCTION *)OBJPTR(CLASS, dest);
                            break;

                        default:
                            KOS_raise_exception_cstring(yarn, str_err_not_callable);
                            break;
                    }

                    if (func)
                        func->defaults = src;
                }

                delta = 3;
                break;
            }

            case INSTR_TAIL_CALL: /* <closure.size.uint8>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_TAIL_CALL_N: /* <closure.size.uint8>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            case INSTR_TAIL_CALL_FUN: /* <closure.size.uint8>, <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            case INSTR_CALL: /* <r.dest>, <r.func>, <r.this>, <r.args> */
                /* fall through */
            case INSTR_CALL_N: /* <r.dest>, <r.func>, <r.this>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            case INSTR_CALL_FUN: /* <r.dest>, <r.func>, <r.arg1>, <numargs.uint8> */
                /* fall through */
            case INSTR_CALL_GEN: { /* <r.dest>, <r.func>, <r.final> */
                const unsigned rfunc     = bytecode[2];
                unsigned       rthis     = ~0U;
                unsigned       rfinal    = ~0U;
                unsigned       rargs     = ~0U;
                unsigned       rarg1     = ~0U;
                unsigned       num_args  = 0;
                int            tail_call = 0;

                KOS_OBJ_ID     func_obj;
                KOS_OBJ_ID     this_obj;
                KOS_OBJ_ID     args_obj  = KOS_BADPTR;

                KOS_FUNCTION  *func = 0;

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

                    this_obj = regs[rthis];
                    assert( ! IS_BAD_PTR(this_obj));
                }
                else
                    this_obj = KOS_VOID;

                assert(rfunc < num_regs);

                func_obj = regs[rfunc];

                if (rargs != ~0U) {
                    assert(rargs < num_regs);
                    args_obj = regs[rargs];
                }

                _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));

                switch (GET_OBJ_TYPE(func_obj)) {

                    case OBJ_FUNCTION:
                        func = OBJPTR(FUNCTION, func_obj);
                        break;

                    case OBJ_CLASS:
                        this_obj = NEW_THIS;
                        func     = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
                        break;

                    default:
                        KOS_raise_exception_cstring(yarn, str_err_not_callable);
                        error = KOS_ERROR_EXCEPTION;
                        break;
                }

                if (func)
                    error = _prepare_call(yarn, instr, func_obj, &this_obj,
                                          args_obj, num_args ? &regs[rarg1] : 0, num_args);

                if ( ! error) {

                    if (func->state == KOS_GEN_INIT)
                        out = this_obj;

                    else {
                        enum _KOS_FUNCTION_STATE state =
                                (enum _KOS_FUNCTION_STATE)func->state;

                        /* TODO optimize INSTR_TAIL_CALL */

                        if (func->handler)  {
                            KOS_OBJ_ID ret_val = KOS_BADPTR;

                            if (IS_BAD_PTR(args_obj))
                                args_obj = _make_args(yarn, &regs[rarg1], num_args);

                            if ( ! IS_BAD_PTR(args_obj))
                                ret_val = func->handler(yarn,
                                                        this_obj,
                                                        args_obj);

                            /* Avoid detecting as end of iterator in _finish_call() */
                            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(ret_val))
                                OBJPTR(STACK, yarn->stack)->header.flags &= ~KOS_CAN_YIELD;

                            yarn->retval = ret_val;

                            if (KOS_is_exception_pending(yarn)) {
                                assert(IS_BAD_PTR(ret_val));
                                error = KOS_ERROR_EXCEPTION;
                                _KOS_wrap_exception(yarn);
                            }
                            else {
                                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(ret_val));
                            }
                        }
                        else {
                            error = _exec_function(yarn);
                            assert( ! error || KOS_is_exception_pending(yarn));
                        }

                        out = _finish_call(yarn, instr, func, this_obj, &state);

                        if (instr == INSTR_CALL_GEN) {
                            if (error == KOS_ERROR_EXCEPTION && state == KOS_GEN_DONE
                                    && _is_generator_end_exception(yarn)) {
                                KOS_clear_exception(yarn);
                                error = KOS_SUCCESS;
                            }
                            if ( ! error) {
                                const KOS_OBJ_ID result = KOS_BOOL(state == KOS_GEN_DONE);
                                if (rfinal == rdest)
                                    out = result;
                                else {
                                    assert(rfinal < num_regs);
                                    regs[rfinal] = result;
                                }
                            }
                        }
                    }

                    if (tail_call && ! error) {
                        yarn->retval = out;
                        out           = KOS_BADPTR;
                        num_regs      = rdest; /* closure size */
                        error         = KOS_SUCCESS_RETURN;
                        _set_closure_stack_size(yarn, num_regs);
                    }
                }
                break;
            }

            case INSTR_RETURN: { /* <closure.size.uint8>, <r.src> */
                const unsigned closure_size = bytecode[1];
                const unsigned rsrc         = bytecode[2];

                assert(closure_size <= num_regs);
                assert(rsrc         <  num_regs);

                yarn->retval = regs[rsrc];

                num_regs = closure_size;
                _set_closure_stack_size(yarn, closure_size);

                error = KOS_SUCCESS_RETURN;
                break;
            }

            case INSTR_YIELD: { /* <r.src> */
                const uint8_t rsrc = bytecode[1];

                assert(rsrc < num_regs);

                if (OBJPTR(STACK, yarn->stack)->header.flags & KOS_CAN_YIELD) {
                    yarn->retval = regs[rsrc];

                    assert(OBJPTR(STACK, yarn->stack)->header.flags & KOS_REENTRANT_STACK);
                    OBJPTR(STACK, yarn->stack)->header.yield_reg =  rsrc;
                    OBJPTR(STACK, yarn->stack)->header.flags     &= ~KOS_CAN_YIELD;

                    /* Move bytecode pointer here, because at the end of the loop
                       we test !error, but error is set to KOS_SUCCESS_RETURN */
                    bytecode += 2;

                    error =  KOS_SUCCESS_RETURN;
                }
                else
                    KOS_raise_exception_cstring(yarn, str_err_cannot_yield);

                delta = 2;
                break;
            }

            case INSTR_THROW: { /* <r.src> */
                const unsigned rsrc = bytecode[1];

                assert(rsrc < num_regs);

                KOS_raise_exception(yarn, regs[rsrc]);

                delta = 2;
                break;
            }

            case INSTR_CATCH: { /* <r.dest>, <delta.int32> */
                const int32_t  rel_offs = (int32_t)_load_32(bytecode+2);
                const uint32_t offset   = (uint32_t)((bytecode + 6 + rel_offs) - module->bytecode);

                rdest = bytecode[1];

                assert(rdest  < num_regs);
                assert(offset < module->bytecode_size);

                _set_catch(regs, offset, (uint8_t)rdest);

                delta = 6;
                break;
            }

            case INSTR_CANCEL: {
                _clear_catch(regs);
                delta = 1;
                break;
            }

            default:
                assert(instr == INSTR_BREAKPOINT);
                if (instr == INSTR_BREAKPOINT) {
                    /* TODO simply call a debugger function from context */
                }
                else
                    KOS_raise_exception_cstring(yarn, str_err_invalid_instruction);
                delta = 1;
                break;
        }

        if ( ! KOS_is_exception_pending(yarn)) {
            if ( ! IS_BAD_PTR(out)) {
                assert(rdest < num_regs);
                regs[rdest] = out;
            }
        }
        else {
            uint32_t catch_offs;
            uint8_t  catch_reg;

            error = KOS_ERROR_EXCEPTION;

            _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));

            _KOS_wrap_exception(yarn);

            catch_offs = _get_catch(regs, &catch_reg);

            if (catch_offs != KOS_NO_CATCH) {
                assert(catch_reg < num_regs);

                regs[catch_reg] = KOS_get_exception(yarn);
                delta           = 0;
                bytecode        = module->bytecode + catch_offs;
                error           = KOS_SUCCESS;

                _clear_catch(regs);
                KOS_clear_exception(yarn);
            }
        }

        if (error)
            break;

        bytecode += delta;

        assert((uint32_t)(bytecode - module->bytecode) < module->bytecode_size);
    }

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

    assert(!error || KOS_is_exception_pending(yarn));

    _store_instr_offs(regs, (uint32_t)(bytecode - module->bytecode));

    return error;
}

KOS_OBJ_ID _KOS_call_function(KOS_YARN              yarn,
                              KOS_OBJ_ID            func_obj,
                              KOS_OBJ_ID            this_obj,
                              KOS_OBJ_ID            args_obj,
                              enum _KOS_CALL_FLAVOR call_flavor)
{
    int           error = KOS_SUCCESS;
    KOS_OBJ_ID    ret   = KOS_BADPTR;
    KOS_FUNCTION *func;

    KOS_context_validate(yarn);

    switch (GET_OBJ_TYPE(func_obj)) {

        case OBJ_FUNCTION:
            func = OBJPTR(FUNCTION, func_obj);
            break;

        case OBJ_CLASS:
            func = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
            break;

        default:
            KOS_raise_exception_cstring(yarn, str_err_not_callable);
            return KOS_BADPTR;
    }

    if (func->header.type == OBJ_CLASS && call_flavor != KOS_APPLY_FUNCTION)
        this_obj = NEW_THIS;

    error = _prepare_call(yarn, INSTR_CALL, func_obj, &this_obj, args_obj, 0, 0);

    if (error)
        return KOS_BADPTR;

    if (func->state == KOS_GEN_INIT)
        ret = this_obj;

    else {
        enum _KOS_FUNCTION_STATE state =
                (enum _KOS_FUNCTION_STATE)func->state;

        if (func->handler)  {
            const KOS_OBJ_ID retval = func->handler(yarn,
                                                    this_obj,
                                                    args_obj);

            /* Avoid detecting as end of iterator in _finish_call() */
            if (state >= KOS_GEN_INIT && ! IS_BAD_PTR(retval))
                OBJPTR(STACK, yarn->stack)->header.flags &= ~KOS_CAN_YIELD;

            if (KOS_is_exception_pending(yarn)) {
                assert(IS_BAD_PTR(retval));
                error = KOS_ERROR_EXCEPTION;
                _KOS_wrap_exception(yarn);
            }
            else {
                assert(state > KOS_GEN_INIT || ! IS_BAD_PTR(retval));
                yarn->retval = retval;
            }
        }
        else {
            error = _exec_function(yarn);
            assert( ! error || KOS_is_exception_pending(yarn));
        }

        ret = _finish_call(yarn,
                           call_flavor == KOS_CALL_GENERATOR ? INSTR_CALL_GEN : INSTR_CALL,
                           func, this_obj, &state);

        if (state == KOS_GEN_DONE)
            ret = KOS_BADPTR;
    }

    return error ? KOS_BADPTR : ret;
}

int _KOS_vm_run_module(struct _KOS_MODULE *module, KOS_OBJ_ID *ret)
{
    int      error;
    KOS_YARN yarn;

    assert(module);
    assert(module->context);

    yarn = (KOS_YARN)_KOS_tls_get(module->context->threads.thread_key);

    error = _KOS_stack_push(yarn, module->constants[module->main_idx]);

    if (error)
        *ret = yarn->exception;

    else {

        KOS_context_validate(yarn);

        error = _exec_function(yarn);

        assert( ! KOS_is_exception_pending(yarn) || error == KOS_ERROR_EXCEPTION);

        if (error)
            *ret = yarn->exception;
        else
            *ret = yarn->retval;

        assert( ! IS_BAD_PTR(*ret));
    }

    yarn->exception = KOS_BADPTR;
    yarn->retval    = KOS_BADPTR;

    _KOS_stack_pop(yarn);

    return error;
}

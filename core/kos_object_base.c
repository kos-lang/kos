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

#include "../inc/kos_object_base.h"
#include "../inc/kos_array.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_memory.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

static const char str_err_invalid_custom_size[] = "invalid custom object size";

KOS_OBJ_ID KOS_new_int(KOS_FRAME frame, int64_t value)
{
    KOS_OBJ_ID obj_id = TO_SMALL_INT((intptr_t)value);

    if (GET_SMALL_INT(obj_id) != value) {

        KOS_INTEGER *integer = (KOS_INTEGER *)_KOS_alloc_object(frame, INTEGER);

        if (integer)
            *integer = value;

        obj_id = OBJID(INTEGER, integer);
    }

    return obj_id;
}

KOS_OBJ_ID KOS_new_float(KOS_FRAME frame, double value)
{
    KOS_FLOAT *number = (KOS_FLOAT *)_KOS_alloc_object(frame, FLOAT);

    if (number)
        *number = value;

    return OBJID(FLOAT, number);
}

KOS_OBJ_ID KOS_new_function(KOS_FRAME frame, KOS_OBJ_ID proto_obj)
{
    KOS_FUNCTION *func = (KOS_FUNCTION *)_KOS_alloc_object(frame, FUNCTION);

    if (func) {
        func->min_args              = 0;
        func->num_regs              = 0;
        func->args_reg              = 0;
        func->prototype             = proto_obj;
        func->closures              = KOS_VOID;
        func->module                = frame->module;
        func->handler               = 0;
        func->generator_stack_frame = 0;
        func->instr_offs            = ~0U;
        func->state                 = KOS_FUN;
    }

    return OBJID(FUNCTION, func);
}

KOS_OBJ_ID KOS_new_builtin_function(KOS_FRAME            frame,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args)
{
    KOS_OBJ_ID func_obj  = KOS_BADPTR;
    KOS_OBJ_ID proto_obj = KOS_gen_prototype(frame, (void *)(intptr_t)handler);

    if ( ! IS_BAD_PTR(proto_obj)) {

        func_obj = KOS_new_function(frame, proto_obj);

        if ( ! IS_BAD_PTR(func_obj)) {
            assert(min_args >= 0 && min_args < 256);

            OBJPTR(FUNCTION, func_obj)->min_args = (uint8_t)min_args;
            OBJPTR(FUNCTION, func_obj)->handler  = handler;
        }
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_FRAME  frame,
                                KOS_OBJ_ID getter,
                                KOS_OBJ_ID setter)
{
    KOS_DYNAMIC_PROP *dyn_prop = (KOS_DYNAMIC_PROP *)_KOS_alloc_object(frame, DYNAMIC_PROP);

    if (dyn_prop) {
        dyn_prop->type   = OBJ_DYNAMIC_PROP;
        dyn_prop->getter = getter;
        dyn_prop->setter = setter;
    }

    return OBJID(DYNAMIC_PROP, dyn_prop);
}

KOS_OBJ_ID KOS_new_custom(KOS_FRAME frame, unsigned custom_size)
{
    KOS_CUSTOM *custom = 0;

    if (custom_size < sizeof(KOS_CUSTOM) || custom_size > 64)
        KOS_raise_exception_cstring(frame, str_err_invalid_custom_size);
    else
        custom = (KOS_CUSTOM *)_KOS_alloc_object_internal(frame, KOS_AREA_64, (int)custom_size);

    if (custom) {
        custom->type     = OBJ_CUSTOM;
        custom->owned    = KOS_VOID;
        custom->finalize = 0;
    }

    return OBJID(CUSTOM, custom);
}

void _KOS_init_stack_frame(KOS_FRAME           frame,
                           KOS_MODULE         *module,
                           enum _KOS_AREA_TYPE alloc_mode,
                           uint32_t            instr_offs,
                           uint32_t            num_regs)
{
    assert(num_regs < 256 || num_regs == ~0U); /* ~0U indicates built-in generator */
    assert(module);
    assert(module->context);

    frame->alloc_mode = (uint8_t)alloc_mode;
    frame->catch_reg  = 0;
    frame->registers  = KOS_BADPTR;
    frame->module     = module;
    frame->allocator  = &module->context->allocator;
    frame->exception  = KOS_BADPTR;
    frame->retval     = KOS_VOID;
    frame->parent     = 0;
    frame->instr_offs = instr_offs;
    frame->yield_reg  = KOS_CANNOT_YIELD;
    frame->catch_offs = KOS_NO_CATCH;

    if (num_regs < 256)
        frame->registers = KOS_new_array(frame, num_regs);
}

typedef struct _KOS_STACK_FRAME KOS_STACK_FRAME;

#define OBJ_STACK_FRAME 0xF

KOS_FRAME _KOS_stack_frame_push(KOS_FRAME   frame,
                                KOS_MODULE *module,
                                uint32_t    instr_offs,
                                uint32_t    num_regs)
{
    KOS_FRAME new_frame = (KOS_FRAME)_KOS_alloc_object(frame, STACK_FRAME);

    if (new_frame) {

        assert(num_regs < 256 || num_regs == ~0U); /* ~0U indicates built-in generator */
        assert(module);
        assert(KOS_context_from_frame(frame) == module->context);

        _KOS_init_stack_frame(new_frame, module, KOS_AREA_RECLAIMABLE, instr_offs, num_regs);

        new_frame->parent = frame;

        if (num_regs < 256 && IS_BAD_PTR(new_frame->registers))
            new_frame = 0; /* object is garbage-collected */
    }

    return new_frame;
}

KOS_FRAME _KOS_stack_frame_push_func(KOS_FRAME     frame,
                                     KOS_FUNCTION *func)
{
    const int no_regs = func->state == KOS_GEN_INIT && func->handler;
    return _KOS_stack_frame_push(frame,
                                 func->module,
                                 func->instr_offs,
                                 no_regs ? ~0U : func->num_regs);
}

int _KOS_is_truthy(KOS_OBJ_ID obj_id)
{
    int ret;

    if (IS_NUMERIC_OBJ(obj_id)) {

        switch (GET_NUMERIC_TYPE(obj_id)) {

            case OBJ_NUM_INTEGER:
                ret = !! *OBJPTR(INTEGER, obj_id);
                break;

            case OBJ_NUM_FLOAT:
                ret = *OBJPTR(FLOAT, obj_id) != 0.0;
                break;

            default:
                ret = !! obj_id;
                break;
        }
    }
    else switch ((enum KOS_OBJECT_IMMEDIATE)(intptr_t)obj_id) {

        case OBJ_FALSE:
            ret = 0;
            break;

        case OBJ_VOID:
            ret = 0;
            break;

        default:
            ret = 1;
            break;
    }

    return ret;
}

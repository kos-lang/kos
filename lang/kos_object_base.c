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

#include "../inc/kos_object_base.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_memory.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

struct _KOS_BOOLEAN kos_static_object_true  = { OBJ_BOOLEAN, 1 };
struct _KOS_BOOLEAN kos_static_object_false = { OBJ_BOOLEAN, 0 };
struct _KOS_VOID    kos_static_object_void  = { OBJ_VOID       };

KOS_OBJ_PTR KOS_new_int(KOS_CONTEXT *ctx, int64_t value)
{
    KOS_OBJ_PTR objptr = TO_SMALL_INT((intptr_t)value);

    if (GET_SMALL_INT(objptr) != value) {

        KOS_ANY_OBJECT *obj = _KOS_alloc_object(ctx, KOS_INTEGER);

        if (obj) {
            obj->type           = OBJ_INTEGER;
            obj->integer.number = value;
        }

        objptr = TO_OBJPTR(obj);
    }

    return objptr;
}

KOS_OBJ_PTR KOS_new_float(KOS_CONTEXT *ctx, double value)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(ctx, KOS_FLOAT);

    if (obj) {
        obj->type           = OBJ_FLOAT;
        obj->floatpt.number = value;
    }

    return TO_OBJPTR(obj);
}

KOS_OBJ_PTR KOS_new_function(KOS_CONTEXT *ctx, KOS_OBJ_PTR proto_obj)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(ctx, KOS_FUNCTION);

    if (obj) {
        obj->type                           = OBJ_FUNCTION;
        obj->function.min_args              = 0;
        obj->function.num_regs              = 0;
        obj->function.args_reg              = 0;
        obj->function.closures              = KOS_VOID;
        obj->function.module                = TO_OBJPTR(0);
        obj->function.handler               = 0;
        obj->function.generator_stack_frame = KOS_VOID;
        obj->function.instr_offs            = ~0U;
        obj->function.generator_state       = KOS_NOT_GEN;

        _KOS_init_properties(&obj->properties);

        {
            static KOS_ASCII_STRING(str_proto, "prototype");
            const int error = KOS_set_property(ctx,
                                               TO_OBJPTR(obj),
                                               TO_OBJPTR(&str_proto),
                                               proto_obj);
            if (error)
                obj = 0; /* object is garbage-collected */
        }
    }

    return TO_OBJPTR(obj);
}

KOS_OBJ_PTR KOS_new_builtin_function(KOS_CONTEXT         *ctx,
                                     KOS_FUNCTION_HANDLER handler,
                                     int                  min_args)
{
    KOS_OBJ_PTR func_obj  = TO_OBJPTR(0);
    KOS_OBJ_PTR proto_obj = KOS_gen_prototype(ctx, (void *)(intptr_t)handler);

    if ( ! IS_BAD_PTR(proto_obj)) {

        func_obj = KOS_new_function(ctx, proto_obj);

        if ( ! IS_BAD_PTR(func_obj)) {
            assert(min_args >= 0 && min_args < 256);

            OBJPTR(KOS_FUNCTION, func_obj)->min_args = (uint8_t)min_args;
            OBJPTR(KOS_FUNCTION, func_obj)->handler  = handler;
        }
    }

    return func_obj;
}

KOS_OBJ_PTR KOS_new_dynamic_prop(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  getter,
                                 KOS_OBJ_PTR  setter)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(ctx, KOS_DYNAMIC_PROP);

    if (obj) {
        obj->type                = OBJ_DYNAMIC_PROP;
        obj->dynamic_prop.getter = getter;
        obj->dynamic_prop.setter = setter;
    }

    return TO_OBJPTR(obj);
}

void _KOS_stack_frame_init(KOS_STACK_FRAME *frame)
{
    frame->type       = OBJ_STACK_FRAME;
    frame->catch_reg  = 0;
    frame->instr_offs = 0;
    frame->registers  = TO_OBJPTR(0);
    frame->module     = TO_OBJPTR(0);
    frame->retval     = TO_OBJPTR(0);
    frame->parent     = TO_OBJPTR(0);
    frame->yield_reg  = KOS_CANNOT_YIELD;
    frame->catch_offs = KOS_NO_CATCH;
}

KOS_STACK_FRAME *_KOS_stack_frame_push(KOS_CONTEXT *ctx,
                                       KOS_OBJ_PTR  module,
                                       uint32_t     instr_offs,
                                       uint32_t     num_regs)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(ctx, KOS_STACK_FRAME);

    if (obj) {

        assert(num_regs < 256 || num_regs == ~0U); /* ~0U indicates built-in generator */

        obj->type                   = OBJ_STACK_FRAME;
        obj->stack_frame.catch_reg  = 0;
        obj->stack_frame.registers  = num_regs > 255 ? TO_OBJPTR(0) : KOS_new_array(ctx, num_regs);
        obj->stack_frame.module     = module;
        obj->stack_frame.retval     = KOS_VOID;
        obj->stack_frame.parent     = ctx->stack_frame;
        obj->stack_frame.instr_offs = instr_offs;
        obj->stack_frame.yield_reg  = KOS_CANNOT_YIELD;
        obj->stack_frame.catch_offs = KOS_NO_CATCH;

        if (num_regs < 256 && IS_BAD_PTR(obj->stack_frame.registers))
            obj = 0; /* object is garbage-collected */
        else
            ctx->stack_frame = TO_OBJPTR(obj);
    }

    return &obj->stack_frame;
}

KOS_STACK_FRAME *_KOS_stack_frame_push_func(KOS_CONTEXT  *ctx,
                                            KOS_FUNCTION *func)
{
    const int no_regs = func->generator_state == KOS_GEN_INIT && func->handler;
    return _KOS_stack_frame_push(ctx,
                                 func->module,
                                 func->instr_offs,
                                 no_regs ? ~0U : func->num_regs);
}

int _KOS_is_truthy(KOS_OBJ_PTR obj)
{
    int ret;

    if (IS_SMALL_INT(obj))
        ret = !! obj;

    else switch (GET_OBJ_TYPE(obj)) {

        case OBJ_INTEGER:
            ret = !! OBJPTR(KOS_INTEGER, obj)->number;
            break;

        case OBJ_FLOAT:
            ret = OBJPTR(KOS_FLOAT, obj)->number != 0.0;
            break;

        case OBJ_VOID:
            ret = 0;
            break;

        case OBJ_BOOLEAN:
            ret = !! KOS_get_bool(obj);
            break;

        default:
            ret = 1;
            break;
    }

    return ret;
}

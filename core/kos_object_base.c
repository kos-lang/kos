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

#include "../inc/kos_object_base.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_memory.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

KOS_OBJ_ID KOS_new_int(KOS_FRAME frame, int64_t value)
{
    KOS_OBJ_ID   obj_id = TO_SMALL_INT((intptr_t)value);
    KOS_INTEGER *integer;

    if (GET_SMALL_INT(obj_id) == value)
        return obj_id;

    integer = (KOS_INTEGER *)_KOS_alloc_object(frame,
                                               KOS_ALLOC_DEFAULT,
                                               OBJ_INTEGER,
                                               sizeof(KOS_INTEGER));

    if (integer) {
        assert(integer->header.type == OBJ_INTEGER);
        integer->value = value;
    }

    return OBJID(INTEGER, integer);
}

KOS_OBJ_ID KOS_new_float(KOS_FRAME frame, double value)
{
    KOS_FLOAT *number = (KOS_FLOAT *)_KOS_alloc_object(frame,
                                                       KOS_ALLOC_DEFAULT,
                                                       OBJ_FLOAT,
                                                       sizeof(KOS_FLOAT));

    if (number) {
        assert(number->header.type == OBJ_FLOAT);
        number->value = value;
    }

    return OBJID(FLOAT, number);
}

KOS_OBJ_ID KOS_new_void(KOS_FRAME frame)
{
    KOS_CONTEXT *const ctx = KOS_context_from_frame(frame);

    return ctx->void_obj;
}

KOS_OBJ_ID KOS_new_boolean(KOS_FRAME frame, int value)
{
    KOS_CONTEXT *const ctx = KOS_context_from_frame(frame);

    return value ? ctx->true_obj : ctx->false_obj;
}

KOS_OBJ_ID KOS_new_function(KOS_FRAME frame, KOS_OBJ_ID proto_obj)
{
    KOS_FUNCTION *func = (KOS_FUNCTION *)_KOS_alloc_object(frame,
                                                           KOS_ALLOC_DEFAULT,
                                                           OBJ_FUNCTION,
                                                           sizeof(KOS_FUNCTION));

    if (func) {
        assert(func->header.type == OBJ_FUNCTION);

        func->header.flags          = 0;
        func->header.num_args       = 0;
        func->header.num_regs       = 0;
        func->args_reg              = 0;
        func->prototype             = proto_obj;
        func->module                = frame->module;
        func->closures              = KOS_new_void(frame);
        func->defaults              = KOS_new_void(frame);
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

            OBJPTR(FUNCTION, func_obj)->header.num_args = (uint8_t)min_args;
            OBJPTR(FUNCTION, func_obj)->handler         = handler;
        }
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_FRAME  frame,
                                KOS_OBJ_ID getter,
                                KOS_OBJ_ID setter)
{
    KOS_DYNAMIC_PROP *dyn_prop = (KOS_DYNAMIC_PROP *)_KOS_alloc_object(frame,
                                                                       KOS_ALLOC_DEFAULT,
                                                                       OBJ_DYNAMIC_PROP,
                                                                       sizeof(KOS_DYNAMIC_PROP));

    if (dyn_prop) {
        dyn_prop->getter = getter;
        dyn_prop->setter = setter;
    }

    return OBJID(DYNAMIC_PROP, dyn_prop);
}

int _KOS_init_stack_frame(KOS_FRAME   frame,
                          KOS_MODULE *module,
                          uint32_t    instr_offs)
{
    int error = KOS_SUCCESS;

    assert(module);
    assert(module->context);

    frame->header.type      = OBJ_STACK_FRAME;
    frame->header.catch_reg = 0;
    frame->header.yield_reg = 255;
    frame->header.flags     = 0;
    frame->allocator        = &module->context->allocator;
    frame->catch_offs       = KOS_NO_CATCH;
    frame->instr_offs       = instr_offs;
    frame->num_saved_frames = 0;
    frame->parent           = KOS_BADPTR;
    frame->module           = OBJID(MODULE, module);
    frame->registers        = KOS_BADPTR;
    frame->exception        = KOS_BADPTR;
    frame->retval           = KOS_new_void(frame);

    return error;
}

static KOS_FRAME _pop_saved_frame(KOS_FRAME frame)
{
    KOS_FRAME saved_frame = 0;

    if (frame->num_saved_frames) {
        KOS_OBJ_ID frame_obj = frame->saved_frames[--frame->num_saved_frames];
        saved_frame          = OBJPTR(STACK_FRAME, frame_obj);
    }

    return saved_frame;
}

KOS_FRAME _KOS_stack_frame_push(KOS_FRAME   frame,
                                KOS_MODULE *module,
                                uint32_t    instr_offs,
                                KOS_OBJ_ID  regs)
{
    KOS_FRAME new_frame = _pop_saved_frame(frame);

    if ( ! new_frame) {
        if ( ! IS_BAD_PTR(frame->parent))
            new_frame = _pop_saved_frame(OBJPTR(STACK_FRAME, frame->parent));

        if ( ! new_frame)
            new_frame = (KOS_FRAME)_KOS_alloc_object(frame,
                                                     KOS_ALLOC_LOCAL,
                                                     OBJ_STACK_FRAME,
                                                     sizeof(struct _KOS_STACK_FRAME));
    }

    if (new_frame) {

        assert(IS_BAD_PTR(regs) ||
               (GET_OBJ_TYPE(regs) == OBJ_ARRAY && KOS_get_array_size(regs) < 256));
        assert(module);
        assert(KOS_context_from_frame(frame) == module->context);

        if (_KOS_init_stack_frame(new_frame, module, instr_offs)) {
            assert( ! IS_BAD_PTR(new_frame->exception));
            frame->exception = new_frame->exception;
            new_frame        = 0; /* object is garbage-collected */
        }
        else {
            new_frame->parent    = OBJID(STACK_FRAME, frame);
            new_frame->registers = regs;
        }
    }

    return new_frame;
}

int _KOS_is_truthy(KOS_OBJ_ID obj_id)
{
    int ret;

    if (IS_SMALL_INT(obj_id))
        ret = obj_id != TO_SMALL_INT(0);

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            ret = !! OBJPTR(INTEGER, obj_id)->value;
            break;

        case OBJ_FLOAT:
            ret = OBJPTR(FLOAT, obj_id)->value != 0.0;
            break;

        case OBJ_VOID:
            ret = 0;
            break;

        case OBJ_BOOLEAN:
            ret = KOS_get_bool(obj_id);
            break;

        default:
            ret = 1;
    }

    return ret;
}

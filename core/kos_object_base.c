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
#include "kos_heap.h"
#include "kos_memory.h"
#include "kos_object_internal.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

static const char str_err_cannot_override_prototype[] = "cannot override prototype";
static const char str_err_not_class[]                 = "object is not a class";
static const char str_prototype[]                     = "prototype";

KOS_OBJ_ID KOS_new_int(KOS_YARN yarn, int64_t value)
{
    KOS_OBJ_ID   obj_id = TO_SMALL_INT((intptr_t)value);
    KOS_INTEGER *integer;

    if (GET_SMALL_INT(obj_id) == value)
        return obj_id;

    integer = (KOS_INTEGER *)_KOS_alloc_object(yarn,
                                               KOS_ALLOC_DEFAULT,
                                               OBJ_INTEGER,
                                               sizeof(KOS_INTEGER));

    if (integer) {
        assert(integer->header.type == OBJ_INTEGER);
        integer->value = value;
    }

    return OBJID(INTEGER, integer);
}

KOS_OBJ_ID KOS_new_float(KOS_YARN yarn, double value)
{
    KOS_FLOAT *number = (KOS_FLOAT *)_KOS_alloc_object(yarn,
                                                       KOS_ALLOC_DEFAULT,
                                                       OBJ_FLOAT,
                                                       sizeof(KOS_FLOAT));

    if (number) {
        assert(number->header.type == OBJ_FLOAT);
        number->value = value;
    }

    return OBJID(FLOAT, number);
}

KOS_OBJ_ID KOS_new_function(KOS_YARN yarn)
{
    KOS_FUNCTION *func = (KOS_FUNCTION *)_KOS_alloc_object(yarn,
                                                           KOS_ALLOC_DEFAULT,
                                                           OBJ_FUNCTION,
                                                           sizeof(KOS_FUNCTION));

    if (func) {
        assert(func->header.type == OBJ_FUNCTION);

        func->header.flags          = 0;
        func->header.num_args       = 0;
        func->header.num_regs       = 0;
        func->args_reg              = 0;
        func->module                = KOS_BADPTR;
        func->closures              = KOS_VOID;
        func->defaults              = KOS_VOID;
        func->handler               = 0;
        func->generator_stack_frame = KOS_BADPTR;
        func->instr_offs            = ~0U;
        func->state                 = KOS_FUN;
    }

    return OBJID(FUNCTION, func);
}

static KOS_OBJ_ID _get_prototype(KOS_YARN   yarn,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_CLASS) {

        KOS_CLASS *const func = OBJPTR(CLASS, this_obj);

        ret = (KOS_OBJ_ID)KOS_atomic_read_ptr(func->prototype);

        assert( ! IS_BAD_PTR(ret));
    }
    else {
        KOS_raise_exception_cstring(yarn, str_err_not_class);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID _set_prototype(KOS_YARN   yarn,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_CLASS) {

        KOS_OBJ_ID                 arg     = KOS_array_read(yarn, args_obj, 0);
        const KOS_FUNCTION_HANDLER handler = OBJPTR(CLASS, this_obj)->handler;

        if (handler) {
            KOS_raise_exception_cstring(yarn, str_err_cannot_override_prototype);
            arg = KOS_BADPTR;
        }

        if ( ! IS_BAD_PTR(arg)) {
            KOS_CLASS *func = OBJPTR(CLASS, this_obj);
            KOS_atomic_write_ptr(func->prototype, arg);
            ret = this_obj;
        }
    }
    else
        KOS_raise_exception_cstring(yarn, str_err_not_class);

    return ret;
}

KOS_OBJ_ID KOS_new_class(KOS_YARN yarn, KOS_OBJ_ID proto_obj)
{
    KOS_CLASS *func = (KOS_CLASS *)_KOS_alloc_object(yarn,
                                                     KOS_ALLOC_DEFAULT,
                                                     OBJ_CLASS,
                                                     sizeof(KOS_CLASS));

    if (func) {
        assert(func->header.type == OBJ_CLASS);

        func->header.flags          = 0;
        func->header.num_args       = 0;
        func->header.num_regs       = 0;
        func->args_reg              = 0;
        func->_dummy                = KOS_CTOR;
        func->module                = KOS_BADPTR;
        func->closures              = KOS_VOID;
        func->defaults              = KOS_VOID;
        func->handler               = 0;
        func->instr_offs            = ~0U;
        KOS_atomic_write_ptr(func->prototype, proto_obj);
        KOS_atomic_write_ptr(func->props,     KOS_BADPTR);

        if (KOS_set_builtin_dynamic_property(yarn,
                                             OBJID(CLASS, func),
                                             KOS_instance_get_cstring(yarn, str_prototype),
                                             yarn->inst->modules.init_module,
                                             _get_prototype,
                                             _set_prototype))
            func = 0; /* object is garbage collected */
    }

    return OBJID(CLASS, func);
}

KOS_OBJ_ID KOS_new_builtin_function(KOS_YARN             yarn,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args)
{
    KOS_OBJ_ID func_obj = KOS_new_function(yarn);

    if ( ! IS_BAD_PTR(func_obj)) {
        assert(min_args >= 0 && min_args < 256);

        OBJPTR(FUNCTION, func_obj)->header.num_args = (uint8_t)min_args;
        OBJPTR(FUNCTION, func_obj)->handler         = handler;
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_builtin_class(KOS_YARN             yarn,
                                 KOS_FUNCTION_HANDLER handler,
                                 int                  min_args)
{
    KOS_OBJ_ID func_obj  = KOS_BADPTR;
    KOS_OBJ_ID proto_obj = KOS_new_object(yarn);

    if ( ! IS_BAD_PTR(proto_obj)) {

        func_obj = KOS_new_class(yarn, proto_obj);

        if ( ! IS_BAD_PTR(func_obj)) {
            assert(min_args >= 0 && min_args < 256);

            OBJPTR(CLASS, func_obj)->header.num_args = (uint8_t)min_args;
            OBJPTR(CLASS, func_obj)->handler         = handler;
        }
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_YARN   yarn,
                                KOS_OBJ_ID getter,
                                KOS_OBJ_ID setter)
{
    KOS_DYNAMIC_PROP *dyn_prop = (KOS_DYNAMIC_PROP *)_KOS_alloc_object(yarn,
                                                                       KOS_ALLOC_DEFAULT,
                                                                       OBJ_DYNAMIC_PROP,
                                                                       sizeof(KOS_DYNAMIC_PROP));

    if (dyn_prop) {
        dyn_prop->getter = getter;
        dyn_prop->setter = setter;
    }

    return OBJID(DYNAMIC_PROP, dyn_prop);
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

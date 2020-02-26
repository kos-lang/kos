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

KOS_OBJ_ID KOS_new_int(KOS_CONTEXT ctx, int64_t value)
{
    KOS_OBJ_ID   obj_id = TO_SMALL_INT((intptr_t)value);
    KOS_INTEGER *integer;

    if (GET_SMALL_INT(obj_id) == value)
        return obj_id;

    integer = (KOS_INTEGER *)kos_alloc_object(ctx,
                                              KOS_ALLOC_MOVABLE,
                                              OBJ_INTEGER,
                                              sizeof(KOS_INTEGER));

    if (integer) {
        assert(kos_get_object_type(integer->header) == OBJ_INTEGER);
        integer->value = value;
    }

    return OBJID(INTEGER, integer);
}

KOS_OBJ_ID KOS_new_float(KOS_CONTEXT ctx, double value)
{
    KOS_FLOAT *number = (KOS_FLOAT *)kos_alloc_object(ctx,
                                                      KOS_ALLOC_MOVABLE,
                                                      OBJ_FLOAT,
                                                      sizeof(KOS_FLOAT));

    if (number) {
        assert(kos_get_object_type(number->header) == OBJ_FLOAT);
        number->value = value;
    }

    return OBJID(FLOAT, number);
}

KOS_OBJ_ID KOS_new_function(KOS_CONTEXT ctx)
{
    KOS_FUNCTION *func = (KOS_FUNCTION *)kos_alloc_object(ctx,
                                                          KOS_ALLOC_MOVABLE,
                                                          OBJ_FUNCTION,
                                                          sizeof(KOS_FUNCTION));

    if (func) {
        assert(kos_get_object_type(func->header) == OBJ_FUNCTION);

        memset(&func->opts, 0, sizeof(func->opts));

        func->opts.args_reg     = KOS_NO_REG;
        func->opts.rest_reg     = KOS_NO_REG;
        func->opts.ellipsis_reg = KOS_NO_REG;
        func->opts.this_reg     = KOS_NO_REG;
        func->opts.bind_reg     = KOS_NO_REG;

        func->module                = KOS_BADPTR;
        func->closures              = KOS_VOID;
        func->defaults              = KOS_VOID;
        func->handler               = 0;
        func->generator_stack_frame = KOS_BADPTR;
        func->instr_offs            = ~0U;

        KOS_atomic_write_relaxed_u32(func->state, KOS_FUN);
    }

    return OBJID(FUNCTION, func);
}

KOS_OBJ_ID kos_copy_function(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id)
{
    KOS_FUNCTION *src;
    KOS_FUNCTION *dest;
    KOS_OBJ_ID    ret;
    KOS_LOCAL     obj;

    KOS_init_local_with(ctx, &obj, obj_id);

    if (GET_OBJ_TYPE(obj.o) == OBJ_FUNCTION)
        ret = KOS_new_function(ctx);
    else {
        assert(GET_OBJ_TYPE(obj.o) == OBJ_CLASS);
        ret = KOS_new_class(ctx, KOS_VOID);
    }

    if ( ! IS_BAD_PTR(ret)) {

        src  = OBJPTR(FUNCTION, obj.o);
        dest = OBJPTR(FUNCTION, ret);

        KOS_atomic_write_relaxed_u32(dest->state, KOS_atomic_read_relaxed_u32(src->state));

        dest->opts       = src->opts;
        dest->instr_offs = src->instr_offs;
        dest->module     = src->module;
        dest->closures   = src->closures;
        dest->defaults   = src->defaults;
        dest->handler    = src->handler;
    }

    KOS_destroy_top_local(ctx, &obj);

    return ret;
}

static KOS_OBJ_ID get_prototype(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret;

    if (GET_OBJ_TYPE(this_obj) == OBJ_CLASS) {

        KOS_CLASS *const func = OBJPTR(CLASS, this_obj);

        ret = KOS_atomic_read_relaxed_obj(func->prototype);

        assert( ! IS_BAD_PTR(ret));
    }
    else {
        KOS_raise_exception_cstring(ctx, str_err_not_class);
        ret = KOS_BADPTR;
    }

    return ret;
}

static KOS_OBJ_ID set_prototype(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) == OBJ_CLASS) {

        const KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        if ( ! IS_BAD_PTR(arg)) {

            const KOS_FUNCTION_HANDLER handler = OBJPTR(CLASS, this_obj)->handler;

            if ( ! handler) {
                KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, this_obj)->prototype, arg);
                ret = this_obj;
            }
            else
                KOS_raise_exception_cstring(ctx, str_err_cannot_override_prototype);
        }
    }
    else
        KOS_raise_exception_cstring(ctx, str_err_not_class);

    return ret;
}

KOS_OBJ_ID KOS_new_class(KOS_CONTEXT ctx, KOS_OBJ_ID proto_obj)
{
    KOS_LOCAL proto;
    KOS_LOCAL func;

    KOS_init_locals(ctx, 2, &proto, &func);

    proto.o = proto_obj;

    func.o = OBJID(CLASS, (KOS_CLASS *)
                   kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_CLASS, sizeof(KOS_CLASS)));

    if ( ! IS_BAD_PTR(func.o)) {

        int error;

        KOS_DECLARE_STATIC_CONST_STRING(str_prototype, "prototype");

        assert(READ_OBJ_TYPE(func.o) == OBJ_CLASS);

        memset(&OBJPTR(CLASS, func.o)->opts, 0, sizeof(OBJPTR(CLASS, func.o)->opts));

        OBJPTR(CLASS, func.o)->opts.args_reg     = KOS_NO_REG;
        OBJPTR(CLASS, func.o)->opts.rest_reg     = KOS_NO_REG;
        OBJPTR(CLASS, func.o)->opts.ellipsis_reg = KOS_NO_REG;
        OBJPTR(CLASS, func.o)->opts.this_reg     = KOS_NO_REG;
        OBJPTR(CLASS, func.o)->opts.bind_reg     = KOS_NO_REG;

        OBJPTR(CLASS, func.o)->dummy      = KOS_CTOR;
        OBJPTR(CLASS, func.o)->module     = KOS_BADPTR;
        OBJPTR(CLASS, func.o)->closures   = KOS_VOID;
        OBJPTR(CLASS, func.o)->defaults   = KOS_VOID;
        OBJPTR(CLASS, func.o)->handler    = 0;
        OBJPTR(CLASS, func.o)->instr_offs = ~0U;
        KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, func.o)->prototype, proto.o);
        KOS_atomic_write_relaxed_ptr(OBJPTR(CLASS, func.o)->props,     KOS_BADPTR);

        error = KOS_set_builtin_dynamic_property(ctx,
                                                 func.o,
                                                 KOS_CONST_ID(str_prototype),
                                                 ctx->inst->modules.init_module,
                                                 get_prototype,
                                                 set_prototype);

        if (error)
            func.o = KOS_BADPTR; /* object is garbage collected */
    }

    return KOS_destroy_top_locals(ctx, &proto, &func);
}

KOS_OBJ_ID KOS_new_builtin_function(KOS_CONTEXT          ctx,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args)
{
    KOS_OBJ_ID func_obj = KOS_new_function(ctx);

    if ( ! IS_BAD_PTR(func_obj)) {
        assert(min_args >= 0 && min_args < 256);

        OBJPTR(FUNCTION, func_obj)->opts.min_args = (uint8_t)min_args;
        OBJPTR(FUNCTION, func_obj)->handler       = handler;
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_builtin_class(KOS_CONTEXT          ctx,
                                 KOS_FUNCTION_HANDLER handler,
                                 int                  min_args)
{
    KOS_OBJ_ID func_obj  = KOS_BADPTR;
    KOS_OBJ_ID proto_obj = KOS_new_object(ctx);

    if ( ! IS_BAD_PTR(proto_obj)) {

        func_obj = KOS_new_class(ctx, proto_obj);

        if ( ! IS_BAD_PTR(func_obj)) {
            assert(min_args >= 0 && min_args < 256);

            OBJPTR(CLASS, func_obj)->opts.min_args = (uint8_t)min_args;
            OBJPTR(CLASS, func_obj)->handler       = handler;
        }
    }

    return func_obj;
}

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_CONTEXT ctx)
{
    KOS_DYNAMIC_PROP *dyn_prop = (KOS_DYNAMIC_PROP *)kos_alloc_object(ctx,
                                                                      KOS_ALLOC_MOVABLE,
                                                                      OBJ_DYNAMIC_PROP,
                                                                      sizeof(KOS_DYNAMIC_PROP));

    if (dyn_prop) {
        dyn_prop->getter = KOS_BADPTR;
        dyn_prop->setter = KOS_BADPTR;
    }

    return OBJID(DYNAMIC_PROP, dyn_prop);
}

KOS_OBJ_ID KOS_new_builtin_dynamic_prop(KOS_CONTEXT          ctx,
                                        KOS_OBJ_ID           module_obj,
                                        KOS_FUNCTION_HANDLER getter,
                                        KOS_FUNCTION_HANDLER setter)
{
    KOS_LOCAL dyn_prop;

    assert(getter);
    assert( ! kos_is_heap_object(module_obj));

    KOS_init_local_with(ctx, &dyn_prop, KOS_new_dynamic_prop(ctx));

    if ( ! IS_BAD_PTR(dyn_prop.o)) {

        KOS_OBJ_ID func_obj = KOS_new_function(ctx);

        if ( ! IS_BAD_PTR(func_obj)) {
            OBJPTR(FUNCTION, func_obj)->module         = module_obj;
            OBJPTR(FUNCTION, func_obj)->opts.min_args  = 0;
            OBJPTR(FUNCTION, func_obj)->handler        = getter;
            OBJPTR(DYNAMIC_PROP, dyn_prop.o)->getter = func_obj;
        }
        else
            dyn_prop.o = KOS_BADPTR;
    }

    if ( ! IS_BAD_PTR(dyn_prop.o)) {

        if (setter) {

            KOS_OBJ_ID func_obj = KOS_new_function(ctx);

            if ( ! IS_BAD_PTR(func_obj)) {
                OBJPTR(FUNCTION, func_obj)->module         = module_obj;
                OBJPTR(FUNCTION, func_obj)->opts.min_args  = 0;
                OBJPTR(FUNCTION, func_obj)->handler        = setter;
                OBJPTR(DYNAMIC_PROP, dyn_prop.o)->setter = func_obj;
            }
            else
                dyn_prop.o = KOS_BADPTR;
        }
    }

    return KOS_destroy_top_local(ctx, &dyn_prop);
}

int kos_is_truthy(KOS_OBJ_ID obj_id)
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

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../inc/kos_entity.h"
#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_heap.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include <limits.h>
#include <string.h>
#include <assert.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_cannot_make_read_only,     "cannot make object read only");
KOS_DECLARE_STATIC_CONST_STRING(str_err_cannot_override_prototype, "cannot override prototype");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_callable,              "object is not a function or class");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_class,                 "object is not a class");

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

        func->bytecode              = KOS_BADPTR;
        func->module                = KOS_BADPTR;
        func->name                  = KOS_STR_EMPTY;
        func->closures              = KOS_VOID;
        func->defaults              = KOS_VOID;
        func->arg_map               = KOS_VOID;
        func->handler               = KOS_NULL;
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
        dest->bytecode   = src->bytecode;
        dest->module     = src->module;
        dest->name       = src->name;
        dest->closures   = src->closures;
        dest->defaults   = src->defaults;
        dest->arg_map    = src->arg_map;
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
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_class));
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
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_cannot_override_prototype));
        }
    }
    else
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_class));

    return ret;
}

KOS_OBJ_ID KOS_new_class(KOS_CONTEXT ctx, KOS_OBJ_ID proto_obj)
{
    KOS_LOCAL proto;
    KOS_LOCAL func;

    KOS_init_locals(ctx, &proto, &func, kos_end_locals);

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
        OBJPTR(CLASS, func.o)->bytecode   = KOS_BADPTR;
        OBJPTR(CLASS, func.o)->module     = KOS_BADPTR;
        OBJPTR(CLASS, func.o)->name       = KOS_STR_EMPTY;
        OBJPTR(CLASS, func.o)->closures   = KOS_VOID;
        OBJPTR(CLASS, func.o)->defaults   = KOS_VOID;
        OBJPTR(CLASS, func.o)->arg_map    = KOS_VOID;
        OBJPTR(CLASS, func.o)->handler    = KOS_NULL;
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

static unsigned count_args(const KOS_CONVERT *args)
{
    unsigned num = 0;

    assert(args && ! IS_BAD_PTR(args->name));

    do {
        ++num;
        ++args;
    } while ( ! IS_BAD_PTR(args->name));

    return num;
}

static int init_builtin_function(KOS_CONTEXT          ctx,
                                 KOS_OBJ_ID           func_obj,
                                 KOS_OBJ_ID           name_obj,
                                 KOS_FUNCTION_HANDLER handler,
                                 const KOS_CONVERT   *args)
{
    KOS_LOCAL func;
    KOS_LOCAL arg_map;
    KOS_LOCAL defaults;
    unsigned  min_args     = 0;
    unsigned  num_def_args = 0;
    int       error        = KOS_SUCCESS;

    OBJPTR(FUNCTION, func_obj)->handler = handler;
    OBJPTR(FUNCTION, func_obj)->name    = name_obj;

    if ( ! args || IS_BAD_PTR(args->name))
        return KOS_SUCCESS;

    KOS_init_local_with(ctx, &func,     func_obj);
    KOS_init_local_with(ctx, &arg_map,  KOS_VOID);
    KOS_init_local_with(ctx, &defaults, KOS_VOID);

    arg_map.o = KOS_new_object(ctx);
    TRY_OBJID(arg_map.o);

    do {
        assert(min_args + num_def_args < 256);

        if (defaults.o == KOS_VOID) {
            if ( ! IS_BAD_PTR(args->default_value)) {
                defaults.o = KOS_new_array(ctx, count_args(args));
                TRY_OBJID(defaults.o);

                TRY(KOS_array_resize(ctx, defaults.o, 0));
            }
        }

        TRY(KOS_set_property(ctx, arg_map.o, args->name, TO_SMALL_INT((int64_t)(min_args + num_def_args))));

        if (defaults.o == KOS_VOID)
            ++min_args;
        else {
            ++num_def_args;

            assert( ! IS_BAD_PTR(args->default_value));

            TRY(KOS_array_push(ctx, defaults.o, args->default_value, KOS_NULL));
        }

        ++args;
    } while ( ! IS_BAD_PTR(args->name));

    OBJPTR(FUNCTION, func.o)->opts.min_args     = (uint8_t)min_args;
    OBJPTR(FUNCTION, func.o)->opts.num_def_args = (uint8_t)num_def_args;
    OBJPTR(FUNCTION, func.o)->defaults          = defaults.o;
    OBJPTR(FUNCTION, func.o)->arg_map           = arg_map.o;

cleanup:
    KOS_destroy_top_locals(ctx, &defaults, &func);

    return error;
}

KOS_OBJ_ID KOS_new_builtin_function(KOS_CONTEXT          ctx,
                                    KOS_OBJ_ID           name_obj,
                                    KOS_FUNCTION_HANDLER handler,
                                    const KOS_CONVERT   *args)
{
    KOS_LOCAL func;
    KOS_LOCAL name;

    KOS_init_local(     ctx, &func);
    KOS_init_local_with(ctx, &name, name_obj);

    func.o = KOS_new_function(ctx);

    if ( ! IS_BAD_PTR(func.o)) {
        if (init_builtin_function(ctx, func.o, name.o, handler, args))
            func.o = KOS_BADPTR;
    }

    return KOS_destroy_top_locals(ctx, &name, &func);
}

KOS_OBJ_ID KOS_new_builtin_class(KOS_CONTEXT          ctx,
                                 KOS_OBJ_ID           name_obj,
                                 KOS_FUNCTION_HANDLER handler,
                                 const KOS_CONVERT   *args)
{
    KOS_OBJ_ID proto_obj;
    KOS_LOCAL  func;
    KOS_LOCAL  name;

    KOS_init_local(     ctx, &func);
    KOS_init_local_with(ctx, &name, name_obj);

    proto_obj = KOS_new_object(ctx);

    if ( ! IS_BAD_PTR(proto_obj)) {

        func.o = KOS_new_class(ctx, proto_obj);

        if ( ! IS_BAD_PTR(func.o)) {
            if (init_builtin_function(ctx, func.o, name.o, handler, args))
                func.o = KOS_BADPTR;
        }
    }

    return KOS_destroy_top_locals(ctx, &name, &func);
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

int KOS_lock_object(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id)
{
    switch (GET_OBJ_TYPE(obj_id)) {
        case OBJ_SMALL_INTEGER:
            /* fall through */
        case OBJ_INTEGER:
            /* fall through */
        case OBJ_FLOAT:
            /* fall through */
        case OBJ_STRING:
            /* fall through */
        case OBJ_BOOLEAN:
            /* fall through */
        case OBJ_VOID:
            /* fall through */
        case OBJ_FUNCTION:
            return KOS_SUCCESS;

        case OBJ_ARRAY:
            KOS_atomic_write_relaxed_u32(OBJPTR(ARRAY, obj_id)->flags, KOS_READ_ONLY);
            return KOS_SUCCESS;

        case OBJ_BUFFER: {
            uint32_t old_flags;
            do old_flags = KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->flags);
            while ( ! KOS_atomic_cas_weak_u32(OBJPTR(BUFFER, obj_id)->flags, old_flags, old_flags | KOS_READ_ONLY));
            return KOS_SUCCESS;
        }

        default:
            break;
    }

    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_cannot_make_read_only));
    return KOS_ERROR_EXCEPTION;
}

KOS_OBJ_ID KOS_get_named_arg(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  func_obj,
                             uint32_t    i)
{
    KOS_LOCAL iter;
    KOS_TYPE  type;
    int       error = KOS_SUCCESS;

    KOS_init_local(ctx, &iter);

    type = GET_OBJ_TYPE(func_obj);

    if ((type != OBJ_FUNCTION) && (type != OBJ_CLASS))
        RAISE_EXCEPTION_STR(str_err_not_callable);

    iter.o = KOS_new_iterator(ctx, OBJPTR(FUNCTION, func_obj)->arg_map, KOS_SHALLOW);
    TRY_OBJID(iter.o);

    while ( ! KOS_iterator_next(ctx, iter.o)) {

        const KOS_OBJ_ID idx_id = KOS_get_walk_value(iter.o);

        if (IS_SMALL_INT(idx_id) && (idx_id == TO_SMALL_INT((int64_t)i))) {
            iter.o = KOS_get_walk_key(iter.o);
            goto cleanup;
        }
    }

    if ( ! KOS_is_exception_pending(ctx))
        KOS_raise_printf(ctx, "invalid argument index %u\n", i);
    error = KOS_ERROR_EXCEPTION;

cleanup:
    iter.o = KOS_destroy_top_local(ctx, &iter);

    return error ? KOS_BADPTR : iter.o;
}

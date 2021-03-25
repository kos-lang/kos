/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_OBJECT_H_INCLUDED
#define KOS_OBJECT_H_INCLUDED

#include "kos_entity.h"

struct KOS_PRIVATE_CLASS_S {
    uint8_t dummy;
};

#define KOS_DECLARE_PRIVATE_CLASS(name) static const struct KOS_PRIVATE_CLASS_S name = { 0U }

#ifdef __cplusplus
template<typename T>
void KOS_object_set_private_ptr(KOS_OBJ_ID obj,
                                T*         value)
{
    KOS_OBJECT_WITH_PRIVATE *const obj_ptr = reinterpret_cast<KOS_OBJECT_WITH_PRIVATE *>(OBJPTR(OBJECT, obj));
    KOS_atomic_write_relaxed_ptr(obj_ptr->priv, static_cast<void *>(value));
}

static inline KOS_OBJ_ID KOS_get_walk_key(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, walk)->last_key);
}

static inline KOS_OBJ_ID KOS_get_walk_value(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, walk)->last_value);
}
#else
#define KOS_object_set_private_ptr(obj, value)  KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT_WITH_PRIVATE, (obj))->priv, value)
#define KOS_get_walk_key(walk)                  (KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, (walk))->last_key))
#define KOS_get_walk_value(walk)                (KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, (walk))->last_value))
#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_new_object(KOS_CONTEXT ctx);

KOS_API
KOS_OBJ_ID KOS_new_object_with_prototype(KOS_CONTEXT ctx,
                                         KOS_OBJ_ID  prototype);

KOS_API
KOS_OBJ_ID KOS_new_object_with_private(KOS_CONTEXT       ctx,
                                       KOS_OBJ_ID        prototype,
                                       KOS_PRIVATE_CLASS priv_class,
                                       KOS_FINALIZE      finalize);

KOS_API
KOS_OBJ_ID KOS_get_property_with_depth(KOS_CONTEXT      ctx,
                                       KOS_OBJ_ID       obj_id,
                                       KOS_OBJ_ID       prop,
                                       enum KOS_DEPTH_E shallow);

KOS_API
int KOS_set_property(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  obj_id,
                     KOS_OBJ_ID  prop,
                     KOS_OBJ_ID  value);

KOS_API
int KOS_delete_property(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  obj_id,
                        KOS_OBJ_ID  prop);

KOS_API
int KOS_set_builtin_dynamic_property(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_OBJ_ID           prop,
                                     KOS_OBJ_ID           module_obj,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter);

KOS_API
KOS_OBJ_ID KOS_get_prototype(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id);

KOS_API
int KOS_has_prototype(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      KOS_OBJ_ID  proto_id);

KOS_API
void* KOS_object_get_private(KOS_OBJ_ID obj, KOS_PRIVATE_CLASS priv_class);

KOS_API
void* KOS_object_swap_private(KOS_OBJ_ID obj, KOS_PRIVATE_CLASS priv_class, void *new_priv);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static inline KOS_OBJ_ID KOS_get_property(KOS_CONTEXT ctx,
                                          KOS_OBJ_ID  obj_id,
                                          KOS_OBJ_ID  prop)
{
    return KOS_get_property_with_depth(ctx, obj_id, prop, KOS_DEEP);
}

static inline KOS_OBJ_ID KOS_get_property_shallow(KOS_CONTEXT ctx,
                                                  KOS_OBJ_ID  obj_id,
                                                  KOS_OBJ_ID  prop)
{
    return KOS_get_property_with_depth(ctx, obj_id, prop, KOS_SHALLOW);
}
#else
#define KOS_get_property(ctx, obj_id, prop) KOS_get_property_with_depth(ctx, obj_id, prop, KOS_DEEP)
#define KOS_get_property_shallow(ctx, obj_id, prop) KOS_get_property_with_depth(ctx, obj_id, prop, KOS_SHALLOW)
#endif

#endif

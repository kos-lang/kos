/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_OBJECT_H_INCLUDED
#define KOS_OBJECT_H_INCLUDED

#include "kos_entity.h"

#ifdef __cplusplus
template<typename T>
T* KOS_object_swap_private_ptr(KOS_OBJ_ID obj,
                               T*         value)
{
    return KOS_atomic_swap_ptr(OBJPTR(OBJECT, obj)->priv, static_cast<void *>(value));
}

template<typename T>
void KOS_object_set_private_ptr(KOS_OBJ_ID obj,
                                T*         value)
{
    KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT, obj)->priv, static_cast<void *>(value));
}

static inline void* KOS_object_get_private_ptr(KOS_OBJ_ID obj)
{
    return KOS_atomic_read_relaxed_ptr(OBJPTR(OBJECT, obj)->priv);
}

static inline KOS_OBJ_ID KOS_get_walk_key(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, walk)->last_key);
}

static inline KOS_OBJ_ID KOS_get_walk_value(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, walk)->last_value);
}
#else
#define KOS_object_swap_private_ptr(obj, value) ((void *)KOS_atomic_swap_ptr(OBJPTR(OBJECT, (obj))->priv, value))
#define KOS_object_set_private_ptr(obj, value)  KOS_atomic_write_relaxed_ptr(OBJPTR(OBJECT, (obj))->priv, value)
#define KOS_object_get_private_ptr(obj)         KOS_atomic_read_relaxed_ptr(OBJPTR(OBJECT, (obj))->priv)
#define KOS_get_walk_key(walk)                  (KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, (walk))->last_key))
#define KOS_get_walk_value(walk)                (KOS_atomic_read_relaxed_obj(OBJPTR(OBJECT_WALK, (walk))->last_value))
#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_object(KOS_CONTEXT ctx);

KOS_OBJ_ID KOS_new_object_with_prototype(KOS_CONTEXT ctx,
                                         KOS_OBJ_ID  prototype);

enum KOS_OBJECT_WALK_DEPTH_E {
    KOS_SHALLOW,
    KOS_DEEP
};

KOS_OBJ_ID KOS_get_property_with_depth(KOS_CONTEXT                  ctx,
                                       KOS_OBJ_ID                   obj_id,
                                       KOS_OBJ_ID                   prop,
                                       enum KOS_OBJECT_WALK_DEPTH_E deep);

int KOS_set_property(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  obj_id,
                     KOS_OBJ_ID  prop,
                     KOS_OBJ_ID  value);

int KOS_delete_property(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  obj_id,
                        KOS_OBJ_ID  prop);

int KOS_set_builtin_dynamic_property(KOS_CONTEXT          ctx,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_OBJ_ID           prop,
                                     KOS_OBJ_ID           module_obj,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter);

KOS_OBJ_ID KOS_get_prototype(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  obj_id);

int KOS_has_prototype(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      KOS_OBJ_ID  proto_id);

KOS_OBJ_ID KOS_new_object_walk(KOS_CONTEXT                  ctx,
                               KOS_OBJ_ID                   obj_id,
                               enum KOS_OBJECT_WALK_DEPTH_E deep);

KOS_OBJ_ID KOS_new_object_walk_copy(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  walk_id);

int KOS_object_walk(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  walk_id);

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
#else
#define KOS_get_property(ctx, obj_id, prop) KOS_get_property_with_depth(ctx, obj_id, prop, KOS_DEEP)
#endif

#endif

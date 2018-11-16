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

#ifndef KOS_OBJECT_H_INCLUDED
#define KOS_OBJECT_H_INCLUDED

#include "kos_object_base.h"

#ifdef __cplusplus
template<typename T>
void *KOS_object_swap_private(KOS_OBJECT& obj,
                              T*          value)
{
    return static_cast<T*>(KOS_atomic_swap_ptr(obj.priv, static_cast<void*>(value)));
}

template<typename T>
void KOS_object_set_private(KOS_OBJECT& obj,
                            T*          value)
{
    KOS_atomic_write_ptr(obj.priv, static_cast<void*>(value));
}

static inline void* KOS_object_get_private(KOS_OBJECT& obj)
{
    return KOS_atomic_read_ptr(obj.priv);
}

static inline KOS_OBJ_ID KOS_get_walk_key(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_obj(OBJPTR(OBJECT_WALK, walk)->last_key);
}

static inline KOS_OBJ_ID KOS_get_walk_value(KOS_OBJ_ID walk)
{
    return KOS_atomic_read_obj(OBJPTR(OBJECT_WALK, walk)->last_value);
}
#else
#define KOS_object_swap_private(obj, value) KOS_atomic_swap_ptr((obj).priv, value)
#define KOS_object_set_private(obj, value)  KOS_atomic_write_ptr((obj).priv, value)
#define KOS_object_get_private(obj)         KOS_atomic_read_ptr((obj).priv)
#define KOS_get_walk_key(walk)              (KOS_atomic_read_obj(OBJPTR(OBJECT_WALK, (walk))->last_key))
#define KOS_get_walk_value(walk)            (KOS_atomic_read_obj(OBJPTR(OBJECT_WALK, (walk))->last_value))
#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_object(KOS_CONTEXT ctx);

KOS_OBJ_ID KOS_new_object_with_prototype(KOS_CONTEXT ctx,
                                         KOS_OBJ_ID  prototype);

KOS_OBJ_ID KOS_get_property(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            KOS_OBJ_ID  prop);

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

enum KOS_OBJECT_WALK_DEPTH_E {
    KOS_SHALLOW,
    KOS_DEEP
};

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

#endif

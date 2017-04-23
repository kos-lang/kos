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

#ifndef __KOS_OBJECT_H
#define __KOS_OBJECT_H

#include "kos_object_base.h"

#ifdef __cplusplus
template<typename T>
void KOS_object_set_private(KOS_OBJECT& obj,
                            T*          value)
{
    obj.priv = value;
}

static inline void* KOS_object_get_private(KOS_OBJECT& obj)
{
    return obj.priv;
}
#else
#define KOS_object_set_private(obj, value) do { (obj).priv = (void*)(value); } while (0)
#define KOS_object_get_private(obj) ((obj).priv)
#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_object(KOS_FRAME frame);

KOS_OBJ_ID KOS_new_object_with_prototype(KOS_FRAME  frame,
                                         KOS_OBJ_ID prototype);

KOS_OBJ_ID KOS_get_property(KOS_FRAME  frame,
                            KOS_OBJ_ID obj_id,
                            KOS_OBJ_ID prop);

int KOS_set_property(KOS_FRAME  frame,
                     KOS_OBJ_ID obj_id,
                     KOS_OBJ_ID prop,
                     KOS_OBJ_ID value);

int KOS_delete_property(KOS_FRAME  frame,
                        KOS_OBJ_ID obj_id,
                        KOS_OBJ_ID prop);

KOS_OBJ_ID KOS_new_builtin_dynamic_property(KOS_FRAME            frame,
                                            KOS_FUNCTION_HANDLER getter,
                                            KOS_FUNCTION_HANDLER setter);

int KOS_set_builtin_dynamic_property(KOS_FRAME            frame,
                                     KOS_OBJ_ID           obj_id,
                                     KOS_OBJ_ID           prop,
                                     KOS_FUNCTION_HANDLER getter,
                                     KOS_FUNCTION_HANDLER setter);

KOS_OBJ_ID KOS_get_prototype(KOS_FRAME  frame,
                             KOS_OBJ_ID obj_id);

enum KOS_OBJECT_WALK_DEPTH {
    KOS_SHALLOW,
    KOS_DEEP
};

KOS_OBJ_ID KOS_new_object_walk(KOS_FRAME                  frame,
                               KOS_OBJ_ID                 obj_id,
                               enum KOS_OBJECT_WALK_DEPTH deep);

int KOS_object_walk_init(KOS_FRAME                  frame,
                         KOS_OBJECT_WALK           *walk,
                         KOS_OBJ_ID                 obj_id,
                         enum KOS_OBJECT_WALK_DEPTH deep);

#define KOS_object_walk_init_shallow(ctx, walk, obj_id) KOS_object_walk_init(ctx, walk, obj_id, KOS_SHALLOW)
#define KOS_object_walk_init_deep(ctx, walk, obj_id)    KOS_object_walk_init(ctx, walk, obj_id, KOS_DEEP)

typedef struct _KOS_OBJECT_WALK_ELEM {
    KOS_OBJ_ID key;
    KOS_OBJ_ID value;
} KOS_OBJECT_WALK_ELEM;

KOS_OBJECT_WALK_ELEM KOS_object_walk(KOS_FRAME        frame,
                                     KOS_OBJECT_WALK *walk);

#ifdef __cplusplus
}
#endif

#endif

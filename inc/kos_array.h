/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_ARRAY_H_INCLUDED
#define KOS_ARRAY_H_INCLUDED

#include "kos_api.h"
#include "kos_atomic.h"
#include "kos_entity.h"
#include <assert.h>

#ifdef __cplusplus

static inline uint32_t KOS_get_array_size(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);
    KOS_ARRAY *const array = OBJPTR(ARRAY, obj_id);
    return KOS_atomic_read_relaxed_u32(array->size);
}

#else

#define KOS_get_array_size(obj_id) (KOS_atomic_read_relaxed_u32(OBJPTR(ARRAY, (obj_id))->size))

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_new_array(KOS_CONTEXT ctx,
                         uint32_t    size);

KOS_API
KOS_OBJ_ID KOS_array_read(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  obj_id,
                          int         idx);

KOS_API
int KOS_array_write(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int         idx,
                    KOS_OBJ_ID  value);

KOS_API
KOS_OBJ_ID KOS_array_cas(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id,
                         int         idx,
                         KOS_OBJ_ID  old_value,
                         KOS_OBJ_ID  new_value);

KOS_API
int KOS_array_reserve(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      uint32_t    new_capacity);

KOS_API
int KOS_array_resize(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  obj_id,
                     uint32_t    size);

KOS_API
KOS_OBJ_ID KOS_array_slice(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           int64_t     begin,
                           int64_t     end);

KOS_API
int KOS_array_insert(KOS_CONTEXT ctx,
                     KOS_OBJ_ID  dest_obj_id,
                     int64_t     dest_begin,
                     int64_t     dest_end,
                     KOS_OBJ_ID  src_obj_id,
                     int64_t     src_begin,
                     int64_t     src_end);

KOS_API
int KOS_array_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  obj_id,
                   KOS_OBJ_ID  value,
                   uint32_t   *idx);

KOS_API
KOS_OBJ_ID KOS_array_pop(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id);

KOS_API
int KOS_array_fill(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  obj_id,
                   int64_t     begin,
                   int64_t     end,
                   KOS_OBJ_ID  value);

#ifdef __cplusplus
}
#endif

#endif

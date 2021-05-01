/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_BUFFER_H_INCLUDED
#define KOS_BUFFER_H_INCLUDED

#include "kos_atomic.h"
#include "kos_entity.h"
#include <assert.h>

typedef struct KOS_BUFFER_STORAGE_S {
    KOS_OBJ_HEADER       header;
    uint8_t             *ptr;
    KOS_ATOMIC(uint32_t) capacity;
    uint32_t             flags;
    uint8_t              buf[1];
} KOS_BUFFER_STORAGE;

/* When flags is set to KOS_EXTERNAL_STORAGE */
typedef struct KOS_BUFFER_EXTERNAL_STORAGE_S {
    KOS_OBJ_HEADER       header;
    uint8_t             *ptr;
    KOS_ATOMIC(uint32_t) capacity;
    uint32_t             flags;
    void                *priv;
    KOS_FINALIZE         finalize;
} KOS_BUFFER_EXTERNAL_STORAGE;

#ifdef __cplusplus

static inline uint32_t KOS_get_buffer_size(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    return KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);
}

static inline const uint8_t *KOS_buffer_data_const(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data);
    assert( ! IS_BAD_PTR(buf_obj));
    KOS_BUFFER_STORAGE *data = OBJPTR(BUFFER_STORAGE, buf_obj);
    return data->ptr;
}

#else

#define KOS_get_buffer_size(obj_id) (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, (obj_id))->size))
#define KOS_buffer_data_const(obj_id) ((const uint8_t *)(OBJPTR(BUFFER_STORAGE, KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, (obj_id))->data))->ptr))

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_new_buffer(KOS_CONTEXT ctx,
                          unsigned    size);

KOS_API
KOS_OBJ_ID KOS_new_external_buffer(KOS_CONTEXT  ctx,
                                   void        *ptr,
                                   unsigned     size,
                                   void        *priv,
                                   KOS_FINALIZE finalize);

KOS_API
int KOS_buffer_reserve(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  obj_id,
                       unsigned    new_capacity);

KOS_API
int KOS_buffer_resize(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      unsigned    size);

KOS_API
uint8_t *KOS_buffer_make_room(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id,
                              unsigned    size_delta);

KOS_API
uint8_t *KOS_buffer_data(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id);

KOS_API
uint8_t *KOS_buffer_data_volatile(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id);

KOS_API
int KOS_buffer_fill(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t     begin,
                    int64_t     end,
                    uint8_t     value);

KOS_API
int KOS_buffer_copy(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  destptr,
                    int64_t     dest_begin,
                    KOS_OBJ_ID  srcptr,
                    int64_t     src_begin,
                    int64_t     src_end);

KOS_API
KOS_OBJ_ID KOS_buffer_slice(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            int64_t     begin,
                            int64_t     end);

#ifdef __cplusplus
}
#endif

#endif

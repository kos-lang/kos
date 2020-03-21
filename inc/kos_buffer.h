/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_BUFFER_H_INCLUDED
#define KOS_BUFFER_H_INCLUDED

#include "kos_atomic.h"
#include "kos_entity.h"
#include <assert.h>

typedef struct KOS_BUFFER_STORAGE_S {
    KOS_OBJ_HEADER       header;
    KOS_ATOMIC(uint32_t) capacity;
    uint8_t              buf[1];
} KOS_BUFFER_STORAGE;

#ifdef __cplusplus

static inline uint32_t KOS_get_buffer_size(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    return KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, obj_id)->size);
}

static inline uint8_t *KOS_buffer_data_volatile(KOS_OBJ_ID obj_id)
{
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);
    const KOS_OBJ_ID buf_obj = KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, obj_id)->data);
    assert( ! IS_BAD_PTR(buf_obj));
    KOS_BUFFER_STORAGE *data = OBJPTR(BUFFER_STORAGE, buf_obj);
    return &data->buf[0];
}

#else

#define KOS_get_buffer_size(obj_id) (KOS_atomic_read_relaxed_u32(OBJPTR(BUFFER, (obj_id))->size))
#define KOS_buffer_data_volatile(obj_id) (&OBJPTR(BUFFER_STORAGE, KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, (obj_id))->data))->buf[0])

#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_buffer(KOS_CONTEXT ctx,
                          unsigned    size);

int KOS_buffer_reserve(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  obj_id,
                       unsigned    new_capacity);

int KOS_buffer_resize(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  obj_id,
                      unsigned    size);

uint8_t *KOS_buffer_make_room(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  obj_id,
                              unsigned    size_delta);

uint8_t *KOS_buffer_data(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  obj_id);

int KOS_buffer_fill(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t     begin,
                    int64_t     end,
                    uint8_t     value);

int KOS_buffer_copy(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  destptr,
                    int64_t     dest_begin,
                    KOS_OBJ_ID  srcptr,
                    int64_t     src_begin,
                    int64_t     src_end);

KOS_OBJ_ID KOS_buffer_slice(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  obj_id,
                            int64_t     begin,
                            int64_t     end);

#ifdef __cplusplus
}
#endif

#endif

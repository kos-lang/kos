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

#ifndef __KOS_OBJECT_BASE_H
#define __KOS_OBJECT_BASE_H

#include <assert.h>
#include <stdint.h>
#include "kos_threads.h"

typedef enum KOS_OBJECT_TYPE {
    OBJ_SMALL_INTEGER,  /* Returned by GET_OBJ_TYPE, never used in any object */

    /* Language types */
    OBJ_INTEGER,
    OBJ_FLOAT,
    OBJ_VOID,
    OBJ_BOOLEAN,
    OBJ_STRING,
    OBJ_OBJECT,
    OBJ_ARRAY,
    OBJ_BUFFER,
    OBJ_FUNCTION,

    OBJ_LAST_TYPE = OBJ_FUNCTION,

    OBJ_OPAQUE,         /* Contains binary user data, contents not recognized by GC */

    OBJ_OBJECT_STORAGE,
    OBJ_ARRAY_STORAGE,
    OBJ_BUFFER_STORAGE,
    OBJ_DYNAMIC_PROP,
    OBJ_OBJECT_WALK,
    OBJ_MODULE,
    OBJ_STACK_FRAME
} KOS_TYPE;

struct _KOS_OBJECT_PLACEHOLDER;

/* KOS_OBJ_ID contains either a pointer to the object or an integer number.
 * The least significant bit (bit 0) indicates whether it is a pointer or a
 * number.
 *
 * KOS_OBJ_ID's layout is:
 * - "Small" integer     ...iiii iiii iiii iii0 (31- or 63-bit signed integer)
 * - Object pointer      ...pppp pppp pppp p001 (8 byte-aligned pointer)
 *
 * If bit 0 is a '1', the rest of KOS_OBJ_ID is treated as the pointer without
 * that bit set.  The actual pointer to the object is KOS_OBJ_ID minus 1.
 */
typedef struct _KOS_OBJECT_PLACEHOLDER *KOS_OBJ_ID;

#define KOS_BADPTR ((KOS_OBJ_ID)(intptr_t)1)

typedef struct _KOS_OBJ_HEADER {
    uint8_t  type;        /* KOS_OBJECT_TYPE */
    uint8_t  reserved[3]; /* alignment       */
    uint32_t alloc_size;
} KOS_OBJ_HEADER;

#ifdef __cplusplus

static inline bool IS_SMALL_INT(KOS_OBJ_ID obj_id) {
    return ! (reinterpret_cast<intptr_t>(obj_id) & 1);
}
static inline intptr_t GET_SMALL_INT(KOS_OBJ_ID obj_id) {
    assert(IS_SMALL_INT(obj_id));
    return reinterpret_cast<intptr_t>(obj_id) / 2;
}
static inline KOS_OBJ_ID TO_SMALL_INT(intptr_t value) {
    return reinterpret_cast<KOS_OBJ_ID>(value << 1);
}
static inline bool IS_BAD_PTR(KOS_OBJ_ID obj_id) {
    return reinterpret_cast<intptr_t>(obj_id) == 1;
}
static inline KOS_TYPE READ_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    assert( ! IS_SMALL_INT(obj_id));
    assert( ! IS_BAD_PTR(obj_id));
    return static_cast<KOS_TYPE>(reinterpret_cast<const uint8_t *>(obj_id)[-1]);
}
static inline KOS_TYPE GET_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    if (IS_SMALL_INT(obj_id))
        return OBJ_SMALL_INTEGER;
    assert( ! IS_BAD_PTR(obj_id));
    return static_cast<KOS_TYPE>(reinterpret_cast<const uint8_t *>(obj_id)[-1]);
}
static inline bool IS_NUMERIC_OBJ(KOS_OBJ_ID obj_id) {
    return GET_OBJ_TYPE(obj_id) <= OBJ_FLOAT;
}
template<typename T>
static inline T* KOS_object_ptr(KOS_OBJ_ID obj_id, KOS_OBJECT_TYPE type) {
    assert( ! IS_SMALL_INT(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == type);
    return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(obj_id) - 1);
}
#define OBJPTR(tag, obj_id) KOS_object_ptr<KOS_##tag>(obj_id, OBJ_##tag)
template<typename T>
static inline KOS_OBJ_ID KOS_object_id(KOS_TYPE type, T *ptr)
{
    assert( ! ptr || ptr->header.type == type);
    return reinterpret_cast<KOS_OBJ_ID>(reinterpret_cast<uint8_t *>(ptr) + 1);
}
#define OBJID(tag, ptr) KOS_object_id<KOS_##tag>(OBJ_##tag, ptr)

#else

#define IS_SMALL_INT(obj_id)   ( ! (( (intptr_t)(obj_id) & 1 ))                    )
#define GET_SMALL_INT(obj_id)  ( (intptr_t)(obj_id) / 2                            )
#define TO_SMALL_INT(value)    ( (KOS_OBJ_ID) ((uintptr_t)(intptr_t)(value) << 1)  )
#define IS_NUMERIC_OBJ(obj_id) ( GET_OBJ_TYPE(obj_id) <= OBJ_FLOAT                 )
#define OBJPTR(tag, obj_id)    ( (KOS_##tag *) ((intptr_t)(obj_id) - 1)            )
#define IS_BAD_PTR(obj_id)     ( (intptr_t)(obj_id) == 1                           )
#define OBJID(tag, ptr)        ( (KOS_OBJ_ID) ((intptr_t)(ptr) + 1)                )
#define READ_OBJ_TYPE(obj_id)  ( (KOS_TYPE) ((uint8_t *)(obj_id))[-1]              )
#define GET_OBJ_TYPE(obj_id)   ( IS_SMALL_INT(obj_id) ? OBJ_SMALL_INTEGER : READ_OBJ_TYPE(obj_id) )

#endif

struct _KOS_CONTEXT;
typedef struct _KOS_CONTEXT KOS_CONTEXT;

struct _KOS_ALLOCATOR;

struct _KOS_STACK_FRAME;

typedef struct _KOS_STACK_FRAME *KOS_FRAME;

typedef struct _KOS_INTEGER {
    KOS_OBJ_HEADER header;
    int64_t        value;
} KOS_INTEGER;

typedef struct _KOS_FLOAT {
    KOS_OBJ_HEADER header;
    double         value;
} KOS_FLOAT;

typedef struct _KOS_VOID {
    KOS_OBJ_HEADER header;
} KOS_VOID;

typedef union _KOS_BOOLEAN {
    KOS_OBJ_HEADER header;
    struct {
        uint8_t    type;
        uint8_t    value;
    }              boolean;
} KOS_BOOLEAN;

typedef struct _KOS_OPAQUE {
    KOS_OBJ_HEADER header;
} KOS_OPAQUE;

enum _KOS_STRING_FLAGS {
    /* Two lowest bits specify string element (character) size in bytes */
    KOS_STRING_ELEM_8    = 0,
    KOS_STRING_ELEM_16   = 1,
    KOS_STRING_ELEM_32   = 2,
    KOS_STRING_ELEM_MASK = 3,

    KOS_STRING_LOCAL     = 4, /* The string is stored entirely in the string object.          */
    KOS_STRING_PTR       = 0, /* The string is stored somewhere else, we only have a pointer. */
    KOS_STRING_REF       = 8  /* The string is stored in another string, we have a reference. */
};

typedef struct _KOS_STR_HEADER {
    uint8_t              type;
    uint8_t              flags;
    uint16_t             length;
    uint32_t             alloc_size;
    KOS_ATOMIC(uint32_t) hash;
} KOS_STR_HEADER;

struct _KOS_STRING_LOCAL {
    KOS_STR_HEADER header;
    uint8_t        data[1];
};

struct _KOS_STRING_PTR {
    KOS_STR_HEADER header;
    const void    *data_ptr;
};

struct _KOS_STRING_REF {
    KOS_STR_HEADER header;
    const void    *data_ptr;
    KOS_OBJ_ID     obj_id;
};

typedef union _KOS_STRING {
    KOS_STR_HEADER           header;
    struct _KOS_STRING_LOCAL local;
    struct _KOS_STRING_PTR   ptr;
    struct _KOS_STRING_REF   ref;
} KOS_STRING;

typedef void (*KOS_FINALIZE)(KOS_FRAME frame,
                             void     *priv);

typedef struct _KOS_OBJECT {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(KOS_OBJ_ID) props;
    KOS_OBJ_ID             prototype;
    KOS_ATOMIC(void *)     priv;
    KOS_FINALIZE           finalize;
} KOS_OBJECT;

typedef struct _KOS_BUFFER {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(KOS_OBJ_ID) data;
} KOS_BUFFER;

typedef struct _KOS_BUFFER KOS_ARRAY;

typedef KOS_OBJ_ID (*KOS_FUNCTION_HANDLER)(KOS_FRAME  frame,
                                           KOS_OBJ_ID this_obj,
                                           KOS_OBJ_ID args_obj);

enum _KOS_FUNCTION_STATE {
    KOS_FUN,            /* regular function                                     */
    KOS_CTOR,           /* constructor function                                 */
    KOS_GEN_INIT,       /* generator initializer object                         */
    KOS_GEN_READY,      /* initialized generator function, but not executed yet */
    KOS_GEN_ACTIVE,     /* generator function halted in the middle of execution */
    KOS_GEN_RUNNING,    /* generator function is being run                      */
    KOS_GEN_DONE        /* generator function reached the return statement      */
};

typedef struct _KOS_FUN_HEADER {
    uint8_t              type;
    uint8_t              flags;
    uint8_t              num_args;
    uint8_t              num_regs;
    uint32_t             alloc_size;
} KOS_FUN_HEADER;

enum _KOS_FUNCTION_FLAGS {
    KOS_FUN_ELLIPSIS  = 1,  /* store remaining args in array */
    KOS_FUN_OLD_STYLE = 2   /* TODO delete with min_args     */
};

typedef struct _KOS_FUNCTION {
    KOS_FUN_HEADER         header;
    uint8_t                min_args; /* TODO delete */
    uint8_t                args_reg;
    uint8_t                state;    /* TODO convert to KOS_ATOMIC(uint32_t) */
    uint32_t               instr_offs;
    KOS_ATOMIC(KOS_OBJ_ID) prototype;
    KOS_OBJ_ID             module;
    KOS_OBJ_ID             closures;
    KOS_OBJ_ID             defaults;
    KOS_FUNCTION_HANDLER   handler;
    KOS_FRAME              generator_stack_frame;
} KOS_FUNCTION;

enum _KOS_MODULE_FLAGS {
    KOS_MODULE_OWN_BYTECODE   = 1,
    KOS_MODULE_OWN_LINE_ADDRS = 2,
    KOS_MODULE_OWN_FUNC_ADDRS = 4
};

typedef struct _KOS_LINE_ADDR {
    uint32_t offs;
    uint32_t line;
} KOS_LINE_ADDR;

typedef struct _KOS_FUNC_ADDR {
    uint32_t offs;
    uint32_t line;
    uint32_t str_idx;
    uint32_t num_instr;
    uint32_t code_size;
} KOS_FUNC_ADDR;

typedef struct _KOS_MODULE {
    KOS_OBJ_HEADER       header;
    uint8_t              flags;
    uint16_t             num_regs;
    uint32_t             instr_offs;
    KOS_OBJ_ID           name;
    KOS_OBJ_ID           path;
    KOS_CONTEXT         *context;
    KOS_OBJ_ID           strings;
    KOS_OBJ_ID           global_names;
    KOS_OBJ_ID           globals;
    KOS_OBJ_ID           module_names; /* Map of directly referenced modules to their indices, for REPL */
    const uint8_t       *bytecode;
    const KOS_LINE_ADDR *line_addrs;
    const KOS_FUNC_ADDR *func_addrs;
    uint32_t             num_line_addrs;
    uint32_t             num_func_addrs;
    uint32_t             bytecode_size;
} KOS_MODULE;

typedef struct _KOS_DYNAMIC_PROP {
    KOS_OBJ_HEADER       header;
    KOS_OBJ_ID           getter;
    KOS_OBJ_ID           setter;
} KOS_DYNAMIC_PROP;

typedef struct _KOS_OBJECT_WALK {
    KOS_OBJ_HEADER       header;
    KOS_ATOMIC(uint32_t) index;
    KOS_OBJ_ID           obj;
    KOS_OBJ_ID           key_table_obj;
    void                *key_table;
} KOS_OBJECT_WALK;

enum KOS_ALLOC_HINT {
    KOS_ALLOC_DEFAULT,      /* Default placement in the global heap         */
    KOS_ALLOC_LOCAL,        /* Prefer frame-local allocation                */
    KOS_ALLOC_PERSISTENT    /* Prefer persistent allocation for root object */
};

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_int(KOS_FRAME frame,
                       int64_t   value);

KOS_OBJ_ID KOS_new_float(KOS_FRAME frame,
                         double    value);

KOS_OBJ_ID KOS_new_function(KOS_FRAME  frame,
                            KOS_OBJ_ID proto_obj);

KOS_OBJ_ID KOS_new_builtin_function(KOS_FRAME            frame,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args);

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_FRAME  frame,
                                KOS_OBJ_ID getter,
                                KOS_OBJ_ID setter);

KOS_OBJ_ID KOS_new_void(KOS_FRAME frame);

KOS_OBJ_ID KOS_new_boolean(KOS_FRAME frame, int value);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline bool KOS_get_bool(KOS_OBJ_ID obj_id)
{
    assert( ! IS_SMALL_INT(obj_id));
    assert( ! IS_BAD_PTR(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == OBJ_BOOLEAN);
    return OBJPTR(BOOLEAN, obj_id)->boolean.value != 0;
}

#else

#define KOS_get_bool(obj_id) (OBJPTR(BOOLEAN, obj_id)->boolean.value != 0)

#endif

#endif

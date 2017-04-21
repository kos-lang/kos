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

#ifndef __KOS_OBJECT_BASE_H
#define __KOS_OBJECT_BASE_H

#include <assert.h>
#include <stdint.h>
#include "../core/kos_threads.h"

enum KOS_OBJECT_TAG {
    OBJ_SMALL_INTEGER = 0x0,  /*   00 */
    OBJ_INTEGER       = 0x2,  /*  010 */
    OBJ_FLOAT         = 0x6,  /*  110 */
    OBJ_IMMEDIATE     = 0x1,  /* 0001 */
    OBJ_STRING        = 0x3,  /* 0011 */
    OBJ_OBJECT        = 0x5,  /* 0101 */
    OBJ_ARRAY         = 0x7,  /* 0111 */
    OBJ_BUFFER        = 0x9,  /* 1001 */
    OBJ_FUNCTION      = 0xB,  /* 1011 */
    OBJ_INTERNAL      = 0xF,  /* 1111 */

    /* Not really tags, but GET_OBJ_TYPE will return them for integers and floats */
    OBJ_INTEGER2      = 0xA,  /* 1010 */
    OBJ_FLOAT2        = 0xE   /* 1110 */
};

enum KOS_NUMERIC_TAG {
    OBJ_NUM_SMALL_INTEGER  = 0x0,  /* 000 */
    OBJ_NUM_SMALL_INTEGER2 = 0x4,  /* 100 */
    OBJ_NUM_INTEGER        = 0x2,  /* 010 */
    OBJ_NUM_FLOAT          = 0x6   /* 110 */
};

enum KOS_OBJECT_IMMEDIATE {
    OBJ_BADPTR        = 0x01, /* 0000 0001 */
    OBJ_FALSE         = 0x21, /* 0010 0001 */
    OBJ_TRUE          = 0x31, /* 0011 0001 */
    OBJ_VOID          = 0x41  /* 0100 0001 */
};

enum KOS_OBJECT_SUBTYPE {
    OBJ_SUBTYPE,
    OBJ_DYNAMIC_PROP,
    OBJ_OBJECT_WALK,
    OBJ_MODULE
};

enum KOS_OBJECT_TYPE_TO_TAG {
    OBJ_TAG_INTEGER      = OBJ_INTEGER,
    OBJ_TAG_FLOAT        = OBJ_FLOAT,
    OBJ_TAG_STRING       = OBJ_STRING,
    OBJ_TAG_OBJECT       = OBJ_OBJECT,
    OBJ_TAG_ARRAY        = OBJ_ARRAY,
    OBJ_TAG_BUFFER       = OBJ_BUFFER,
    OBJ_TAG_FUNCTION     = OBJ_FUNCTION,
    OBJ_TAG_SUBTYPE      = OBJ_INTERNAL,
    OBJ_TAG_DYNAMIC_PROP = OBJ_INTERNAL,
    OBJ_TAG_OBJECT_WALK  = OBJ_INTERNAL,
    OBJ_TAG_MODULE       = OBJ_INTERNAL
};

struct _KOS_OBJECT_PLACEHOLDER;

/* KOS_OBJ_ID contains a tag and information specific to the object,
 * which is either pointer to the object or object data.
 *
 * KOS_OBJ_ID's layout is:
 * - OBJ_SMALL_INTEGER   ...iiii iiii iiii ii00 (30- or 62-bit signed integer)
 * - OBJ_INTEGER         ...pppp pppp pppp p010 (8 byte-aligned pointer)
 * - OBJ_FLOAT           ...pppp pppp pppp p110 (8 byte-aligned pointer)
 * - OBJ_IMMEDIATE       ...xxxx xxxx xxxx 0001 (object value, e.g. void, true)
 * - other               ...pppp pppp pppp tttt (16 byte-aligned pointer)
 *
 * Integer and float pointers are always aligned on 8 bytes and other object
 * pointers are always aligned on 16 bytes or more, so their 3 or 4 lowest bits,
 * respectively, are all 0.  We take advantage of this fact and we store a tag
 * indicating object type in these bits.  Before we cast KOS_OBJ_ID to an
 * actual pointer, we check the object type anyway, therefore the cast to
 * a specific object type includes an object-specific offset.  Access to fields
 * within the object structures includes an offset anyway, so this additional
 * offset comes free. */
typedef struct _KOS_OBJECT_PLACEHOLDER *KOS_OBJ_ID;

#define KOS_BADPTR      ((KOS_OBJ_ID)(intptr_t)OBJ_BADPTR)
#define KOS_TRUE        ((KOS_OBJ_ID)(intptr_t)OBJ_TRUE)
#define KOS_FALSE       ((KOS_OBJ_ID)(intptr_t)OBJ_FALSE)
#define KOS_BOOL(value) ((value) ? KOS_TRUE : KOS_FALSE)
#define KOS_VOID        ((KOS_OBJ_ID)(intptr_t)OBJ_VOID)

typedef uint8_t KOS_SUBTYPE;

#ifdef __cplusplus

static inline bool IS_SMALL_INT(KOS_OBJ_ID obj_id) {
    return ! (reinterpret_cast<intptr_t>(obj_id) & 3);
}
static inline intptr_t GET_SMALL_INT(KOS_OBJ_ID obj_id) {
    return reinterpret_cast<intptr_t>(obj_id) >> 2;
}
static inline KOS_OBJ_ID TO_SMALL_INT(intptr_t value) {
    return reinterpret_cast<KOS_OBJ_ID>(value << 2);
}
static inline bool IS_NUMERIC_OBJ(KOS_OBJ_ID obj_id) {
    return ! (reinterpret_cast<intptr_t>(obj_id) & 1);
}
static inline KOS_NUMERIC_TAG GET_NUMERIC_TYPE(KOS_OBJ_ID obj_id) {
    return static_cast<KOS_NUMERIC_TAG>(reinterpret_cast<intptr_t>(obj_id) & 0x7);
}
template<typename T>
static inline T* KOS_object_ptr(KOS_OBJ_ID obj_id, KOS_OBJECT_TYPE_TO_TAG tag) {
    return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(obj_id) - tag);
}
#define OBJPTR(tag, obj_id) KOS_object_ptr<KOS_##tag>(obj_id, OBJ_TAG_##tag)
static inline bool IS_BAD_PTR(KOS_OBJ_ID obj_id) {
    return reinterpret_cast<intptr_t>(obj_id) == OBJ_BADPTR;
}
static inline KOS_OBJECT_TAG GET_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    return static_cast<KOS_OBJECT_TAG>(reinterpret_cast<intptr_t>(obj_id) & 0xF);
}
template<KOS_OBJECT_TYPE_TO_TAG tag, typename T>
static inline KOS_OBJ_ID KOS_object_id(T *ptr) {
    if (ptr) {
        const KOS_OBJ_ID obj_id =
            reinterpret_cast<KOS_OBJ_ID>(reinterpret_cast<intptr_t>(ptr) + tag);
        assert(GET_OBJ_TYPE(obj_id) == static_cast<KOS_OBJECT_TAG>(tag));
        return obj_id;
    }
    else
        return KOS_BADPTR;
}
#define OBJID(tag, ptr) KOS_object_id<OBJ_TAG_##tag, KOS_##tag>(ptr)
static inline KOS_OBJECT_SUBTYPE GET_OBJ_SUBTYPE(KOS_OBJ_ID obj_id) {
    return GET_OBJ_TYPE(obj_id) == OBJ_INTERNAL
            ?  static_cast<KOS_OBJECT_SUBTYPE>(*OBJPTR(SUBTYPE, obj_id))
            : OBJ_SUBTYPE;
}

#else

#define IS_SMALL_INT(obj_id)    ( ! (( (intptr_t)(obj_id) & 3 ))                    )
#define GET_SMALL_INT(obj_id)   ( (intptr_t)(obj_id) >> 2                           )
#define TO_SMALL_INT(value)     ( (KOS_OBJ_ID) ((uintptr_t)(intptr_t)(value) << 2)  )
#define IS_NUMERIC_OBJ(obj_id)  ( ! (( (intptr_t)(obj_id) & 1 ))                    )
#define GET_NUMERIC_TYPE(obj_id)( (enum KOS_NUMERIC_TAG)((intptr_t)(obj_id) & 0x7)  )
#define OBJPTR(tag, obj_id)     ( (KOS_##tag *) ((intptr_t)(obj_id) - OBJ_TAG_##tag))
#define IS_BAD_PTR(obj_id)      ( (intptr_t)(obj_id) == OBJ_BADPTR                  )
#define OBJID(tag, ptr)         ( (ptr) ? (KOS_OBJ_ID) ((intptr_t)(ptr) + OBJ_TAG_##tag) : KOS_BADPTR )
#define GET_OBJ_TYPE(obj_id)    ( (enum KOS_OBJECT_TAG)((intptr_t)(obj_id) & 0xF)   )
#define GET_OBJ_SUBTYPE(obj_id) ( GET_OBJ_TYPE(obj_id) == OBJ_INTERNAL                \
                                  ? (enum KOS_OBJECT_SUBTYPE)*OBJPTR(SUBTYPE, (obj_id)) \
                                  : OBJ_SUBTYPE )

#endif

struct _KOS_CONTEXT;
typedef struct _KOS_CONTEXT KOS_CONTEXT;

struct _KOS_ALLOC_DEBUG;

typedef int64_t KOS_INTEGER;

typedef double KOS_FLOAT;

struct _KOS_STR_REF {
    const void *ptr;
    KOS_OBJ_ID  str;
};

enum _KOS_STRING_FLAGS {
    KOS_STRING_LOCAL,  /* The string is stored entirely in the string object (data.buf).          */
    KOS_STRING_BUFFER, /* The string is stored in an allocated buffer (data.ptr).                 */
    KOS_STRING_PTR,    /* The string is stored somewhere else, we only have a pointer (data.ptr). */
    KOS_STRING_REF     /* The string is stored in another string, we have a reference (data.ref). */
};

enum _KOS_STRING_ELEM_SIZE {
    KOS_STRING_ELEM_8,
    KOS_STRING_ELEM_16,
    KOS_STRING_ELEM_32
};

typedef struct _KOS_STRING {
    uint8_t                 elem_size; /* KOS_STRING_ELEM_* */
    uint8_t                 flags;
    uint16_t                length;
    KOS_ATOMIC(uint32_t)    hash;
    union {
        const void         *ptr;
        char                buf[24];
        struct _KOS_STR_REF ref;
    }                       data;
} KOS_STRING;

enum _KOS_YIELD_STATE {
    KOS_CANNOT_YIELD = 0x1000000U, /* indicates a regular function */
    KOS_CAN_YIELD    = 0x2000000U  /* indicates a generator        */
};

enum _KOS_CATCH_STATE {
    KOS_NO_CATCH = 0x7FFFFFFFU
};

typedef struct _KOS_STACK_FRAME {
    uint8_t                  catch_reg;
    uint32_t                 instr_offs;
    KOS_OBJ_ID               registers;
    struct _KOS_MODULE      *module;
    struct _KOS_ALLOC_DEBUG *allocator;
    KOS_OBJ_ID               exception;
    KOS_OBJ_ID               retval;
    struct _KOS_STACK_FRAME *parent;
    uint32_t                 yield_reg;    /* index of the yield register */
    uint32_t                 catch_offs;
} KOS_STACK_FRAME;

typedef void (*KOS_FINALIZE)(KOS_STACK_FRAME *frame,
                             void            *priv);

typedef struct _KOS_OBJECT {
    KOS_ATOMIC(void *) props;
    KOS_OBJ_ID         prototype;
    void              *priv;
    KOS_FINALIZE       finalize;
} KOS_OBJECT;

typedef struct _KOS_BUFFER {
    KOS_ATOMIC(void *)   data;
    KOS_ATOMIC(uint32_t) size;
} KOS_BUFFER;

typedef struct _KOS_BUFFER KOS_ARRAY;

typedef KOS_OBJ_ID (*KOS_FUNCTION_HANDLER)(KOS_STACK_FRAME *frame,
                                           KOS_OBJ_ID       this_obj,
                                           KOS_OBJ_ID       args_obj);

enum _KOS_GENERATOR_STATE {
    KOS_NOT_GEN,        /* not a generator, regular function                    */
    KOS_GEN_INIT,       /* generator initializer object                         */
    KOS_GEN_READY,      /* initialized generator function, but not executed yet */
    KOS_GEN_ACTIVE,     /* generator function halted in the middle of execution */
    KOS_GEN_RUNNING,    /* generator function is being run                      */
    KOS_GEN_DONE        /* generator function reached the return statement      */
};

typedef struct _KOS_FUNCTION {
    uint8_t                   min_args;
    uint8_t                   num_regs;
    uint8_t                   args_reg;
    KOS_ATOMIC(void *)        prototype; /* actual type: KOS_OBJ_ID */
    KOS_OBJ_ID                closures;
    struct _KOS_MODULE       *module;
    KOS_FUNCTION_HANDLER      handler;
    KOS_STACK_FRAME          *generator_stack_frame;
    uint32_t                  instr_offs;
    enum _KOS_GENERATOR_STATE generator_state;
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
    KOS_SUBTYPE          type; /* OBJ_MODULE */
    uint8_t              flags;
    KOS_OBJ_ID           name;
    KOS_OBJ_ID           path;
    KOS_CONTEXT         *context;
    KOS_OBJ_ID           strings;
    KOS_OBJ_ID           global_names;
    KOS_OBJ_ID           globals;
    const uint8_t       *bytecode;
    const KOS_LINE_ADDR *line_addrs;
    const KOS_FUNC_ADDR *func_addrs;
    uint32_t             bytecode_size;
    uint32_t             num_line_addrs;
    uint32_t             num_func_addrs;
    uint32_t             instr_offs;
    uint32_t             num_regs;
} KOS_MODULE;

typedef struct _KOS_DYNAMIC_PROP {
    KOS_SUBTYPE          type; /* OBJ_DYNAMIC_PROP */
    KOS_OBJ_ID           getter;
    KOS_OBJ_ID           setter;
} KOS_DYNAMIC_PROP;

typedef struct _KOS_OBJECT_WALK {
    KOS_SUBTYPE          type; /* OBJ_OBJECT_WALK */
    KOS_OBJ_ID           obj;
    KOS_OBJ_ID           key_table_obj;
    void                *key_table;
    KOS_ATOMIC(uint32_t) index;
} KOS_OBJECT_WALK;

typedef union _KOS_INTERNAL_OBJECT {
    KOS_SUBTYPE          type;
    KOS_DYNAMIC_PROP     dynamic_prop;
    KOS_OBJECT_WALK      walk;
    KOS_MODULE           module;
#ifdef KOS_CPP11
    _KOS_INTERNAL_OBJECT() = delete;
#endif
} KOS_INTERNAL_OBJECT;

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_int(KOS_STACK_FRAME *frame,
                       int64_t          value);

KOS_OBJ_ID KOS_new_float(KOS_STACK_FRAME *frame,
                         double           value);

KOS_OBJ_ID KOS_new_function(KOS_STACK_FRAME *frame,
                            KOS_OBJ_ID       proto_obj);

KOS_OBJ_ID KOS_new_builtin_function(KOS_STACK_FRAME     *frame,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args);

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_STACK_FRAME *frame,
                                KOS_OBJ_ID       getter,
                                KOS_OBJ_ID       setter);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline int KOS_get_bool(KOS_OBJ_ID obj_id)
{
    return obj_id == KOS_TRUE;
}

#else

#define KOS_get_bool(obj_id) (obj_id == KOS_TRUE)

#endif

#endif

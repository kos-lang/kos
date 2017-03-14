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

#include <stdint.h>
#include "../core/kos_threads.h"

struct _KOS_CONTEXT;
typedef struct _KOS_CONTEXT KOS_CONTEXT;

struct _KOS_ALLOC_DEBUG;

enum KOS_OBJECT_TYPE {
    /* Official types, visible in the language */
    OBJ_INTEGER         = 0x49, /* 'I' */
    OBJ_FLOAT           = 0x46, /* 'F' */
    OBJ_BOOLEAN         = 0x42, /* 'B' */
    OBJ_VOID            = 0x56, /* 'V' */
    OBJ_STRING_8        = 0x31, /* '1' */
    OBJ_STRING_16       = 0x32, /* '2' */
    OBJ_STRING_32       = 0x33, /* '3' */
    OBJ_OBJECT          = 0x6F, /* 'o' */
    OBJ_ARRAY           = 0x41, /* 'A' */
    OBJ_BUFFER          = 0x55, /* 'U' */
    OBJ_FUNCTION        = 0x66, /* 'f' */

    /* Internal types, never directly accessible from the language */
    OBJ_STACK_FRAME     = 0x52, /* 'R' */
    OBJ_MODULE          = 0x4D, /* 'M' */
    OBJ_DYNAMIC_PROP    = 0x44, /* 'D' */
    OBJ_OBJECT_WALK     = 0x57, /* 'W' */
    OBJ_SPECIAL         = 0x53  /* 'S' */
};

/* enum storage is typically an int, but we really want a byte */
typedef uint8_t _KOS_TYPE_STORAGE;

struct _KOS_OBJECT_PLACEHOLDER;

/* KOS_OBJ_PTR is either an integer (small int) or an object pointer.
 *
 * KOS_OBJ_PTR's layout is:
 * - pointer: ...pppp pppp pppp ppp1 (total 32 or 64 bits)
 * - integer: ...iiii iiii iiii iii0 (signed integer takes 31 or 63 bits)
 *
 * Pointers are always aligned on 4 or 8 bytes, so their two lowest bits are
 * always 0.  We take advantage of this fact, and when we convert an object
 * pointer to KOS_OBJ_PTR, we simply add 1 to it.  This way, if bit 0 of
 * KOS_OBJ_PTR is set, it indicates it is a pointer to an object.  Otherwise
 * the KOS_OBJ_PTR is a signed integer shifted left by 1. */
typedef struct _KOS_OBJECT_PLACEHOLDER *KOS_OBJ_PTR;

#ifdef __cplusplus

#define IS_SMALL_INT    KOS_is_small_int
#define GET_SMALL_INT   KOS_get_small_int
#define TO_SMALL_INT    KOS_to_small_int
#define IS_BAD_PTR      KOS_is_bad_ptr
#define TO_OBJPTR       KOS_to_object_ptr
#define GET_OBJ_TYPE    KOS_get_object_type
#define IS_NUMERIC_OBJ  KOS_is_numeric_object
#define IS_STRING_OBJ   KOS_is_string_object

static inline bool IS_SMALL_INT(KOS_OBJ_PTR objptr) {
    return ! (reinterpret_cast<intptr_t>(objptr) & 1);
}
static inline intptr_t GET_SMALL_INT(KOS_OBJ_PTR objptr) {
    return reinterpret_cast<intptr_t>(objptr) >> 1;
}
static inline KOS_OBJ_PTR TO_SMALL_INT(intptr_t value) {
    return reinterpret_cast<KOS_OBJ_PTR>(value << 1);
}
template<typename T>
static inline T* KOS_object_ptr(KOS_OBJ_PTR objptr) {
    return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(objptr) - 1);
}
#define OBJPTR(type, objptr) KOS_object_ptr<type>(objptr)
static inline bool IS_BAD_PTR(KOS_OBJ_PTR objptr) {
    return reinterpret_cast<intptr_t>(objptr) == 1;
}
static inline KOS_OBJ_PTR TO_OBJPTR(void *ptr) {
    return reinterpret_cast<KOS_OBJ_PTR>(reinterpret_cast<intptr_t>(ptr) + 1);
}
static inline KOS_OBJECT_TYPE GET_OBJ_TYPE(KOS_OBJ_PTR objptr) {
    return static_cast<KOS_OBJECT_TYPE>(*OBJPTR(_KOS_TYPE_STORAGE, objptr));
}
static inline bool IS_NUMERIC_OBJ(KOS_OBJ_PTR objptr) {
    return IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) == OBJ_INTEGER || GET_OBJ_TYPE(objptr) == OBJ_FLOAT;
}
static inline bool IS_STRING_OBJ(KOS_OBJ_PTR objptr) {
    return ! IS_SMALL_INT(objptr) && (GET_OBJ_TYPE(objptr) <= OBJ_STRING_32);
}
template<enum KOS_OBJECT_TYPE type>
bool KOS_is_type(KOS_OBJ_PTR objptr) {
#ifdef KOS_CPP11
    static_assert(type != OBJ_INTEGER   &&
                  type != OBJ_FLOAT     &&
                  type != OBJ_STRING_8  &&
                  type != OBJ_STRING_16 &&
                  type != OBJ_STRING_32, "Unsupported type specified");
#endif
    return ! IS_SMALL_INT(objptr) && (GET_OBJ_TYPE(objptr) == type);
}
#define IS_TYPE(type, objptr) KOS_is_type<type>(objptr)

#else

#define IS_SMALL_INT(objptr)  ( ! (( (intptr_t)(objptr) & 1 ))                    )
#define GET_SMALL_INT(objptr) ( (intptr_t)(objptr) >> 1                           )
#define TO_SMALL_INT(value)   ( (KOS_OBJ_PTR) ((uintptr_t)(intptr_t)(value) << 1) )
#define OBJPTR(type, objptr)  ( (type *) ((intptr_t)(objptr) - 1)                 )
#define IS_BAD_PTR(objptr)    ( (intptr_t)(objptr) == 1                           )
#define TO_OBJPTR(ptr)        ( (KOS_OBJ_PTR) ((intptr_t)(ptr) + 1)               )
#define GET_OBJ_TYPE(objptr)  ( OBJPTR(KOS_ANY_OBJECT, (objptr))->type            )
#define IS_NUMERIC_OBJ(objptr)( IS_SMALL_INT(objptr) || GET_OBJ_TYPE(objptr) == OBJ_INTEGER || GET_OBJ_TYPE(objptr) == OBJ_FLOAT )
#define IS_STRING_OBJ(objptr) ( ! IS_SMALL_INT(objptr) && (GET_OBJ_TYPE(objptr) <= OBJ_STRING_32) )
#define IS_TYPE(type, objptr) ( ! IS_SMALL_INT(objptr) && (GET_OBJ_TYPE(objptr) == (type)       ) )

#endif

typedef struct _KOS_INTEGER {
    _KOS_TYPE_STORAGE type; /* OBJ_INTEGER */
    int64_t           number;
} KOS_INTEGER;

typedef struct _KOS_FLOAT {
    _KOS_TYPE_STORAGE type; /* OBJ_FLOAT */
    double            number;
} KOS_FLOAT;

struct _KOS_BOOLEAN {
    _KOS_TYPE_STORAGE type; /* OBJ_BOOLEAN */
    int               value;
};

struct _KOS_VOID {
    _KOS_TYPE_STORAGE type; /* OBJ_VOID */
};

struct _KOS_STR_REF {
    const void *ptr;
    KOS_OBJ_PTR str;
};

enum _KOS_STRING_FLAGS {
    KOS_STRING_LOCAL,  /* The string is stored entirely in the string object (data.buf).          */
    KOS_STRING_BUFFER, /* The string is stored in an allocated buffer (data.ptr).                 */
    KOS_STRING_PTR,    /* The string is stored somewhere else, we only have a pointer (data.ptr). */
    KOS_STRING_REF     /* The string is stored in another string, we have a reference (data.ref). */
};

typedef struct _KOS_STRING {
    _KOS_TYPE_STORAGE       type; /* OBJ_STRING_8 or OBJ_STRING_16 or OBJ_STRING_32 */
    uint8_t                 flags;
    uint16_t                length;
    KOS_ATOMIC(uint32_t)    hash;
    union {
        const void         *ptr;
        char                buf[24];
        struct _KOS_STR_REF ref;
    }                       data;
#ifdef KOS_CPP11
    _KOS_STRING() { }
    _KOS_STRING(const char* s, uint16_t length)
        : type(OBJ_STRING_8),
          flags(KOS_STRING_PTR),
          length(length)
    {
        hash     = 0;
        data.ptr = s;
    }
#endif
} KOS_STRING;

typedef void (*KOS_FINALIZE)(void *priv);

typedef struct _KOS_OBJECT {
    _KOS_TYPE_STORAGE  type; /* OBJ_OBJECT */
    KOS_ATOMIC(void *) props;
    KOS_OBJ_PTR        prototype;
    void              *priv;
    KOS_FINALIZE       finalize;
} KOS_OBJECT;

typedef struct _KOS_ARRAY {
    _KOS_TYPE_STORAGE    type; /* OBJ_ARRAY */
    KOS_ATOMIC(uint32_t) size;
    KOS_ATOMIC(void *)   buffer;
} KOS_ARRAY;

typedef struct _KOS_BUFFER {
    _KOS_TYPE_STORAGE    type; /* OBJ_BUFFER */
    KOS_ATOMIC(uint32_t) size;
    KOS_ATOMIC(void *)   data;
} KOS_BUFFER;

enum _KOS_YIELD_STATE {
    KOS_CANNOT_YIELD = 0x1000000U, /* indicates a regular function */
    KOS_CAN_YIELD    = 0x2000000U  /* indicates a generator        */
};

enum _KOS_CATCH_STATE {
    KOS_NO_CATCH = 0x7FFFFFFFU
};

typedef struct _KOS_STACK_FRAME {
    _KOS_TYPE_STORAGE        type; /* OBJ_STACK_FRAME */
    uint8_t                  catch_reg;
    uint32_t                 instr_offs;
    KOS_OBJ_PTR              registers;
    KOS_OBJ_PTR              module;
    struct _KOS_ALLOC_DEBUG *allocator;
    KOS_OBJ_PTR              exception;
    KOS_OBJ_PTR              retval;
    KOS_OBJ_PTR              parent;
    uint32_t                 yield_reg;    /* index of the yield register */
    uint32_t                 catch_offs;
} KOS_STACK_FRAME;

typedef KOS_OBJ_PTR (*KOS_FUNCTION_HANDLER)(KOS_STACK_FRAME *frame,
                                            KOS_OBJ_PTR      this_obj,
                                            KOS_OBJ_PTR      args_obj);

enum _KOS_GENERATOR_STATE {
    KOS_NOT_GEN,        /* not a generator, regular function                    */
    KOS_GEN_INIT,       /* generator initializer object                         */
    KOS_GEN_READY,      /* initialized generator function, but not executed yet */
    KOS_GEN_ACTIVE,     /* generator function halted in the middle of execution */
    KOS_GEN_RUNNING,    /* generator function is being run                      */
    KOS_GEN_DONE        /* generator function reached the return statement      */
};

typedef struct _KOS_FUNCTION {
    _KOS_TYPE_STORAGE         type; /* OBJ_FUNCTION */
    uint8_t                   min_args;
    uint8_t                   num_regs;
    uint8_t                   args_reg;
    KOS_ATOMIC(void *)        prototype; /* actual type: KOS_OBJ_PTR */
    KOS_OBJ_PTR               closures;
    KOS_OBJ_PTR               module;
    KOS_FUNCTION_HANDLER      handler;
    KOS_OBJ_PTR               generator_stack_frame;
    uint32_t                  instr_offs;
    enum _KOS_GENERATOR_STATE generator_state;
} KOS_FUNCTION;

enum _KOS_MODULE_FLAGS {
    KOS_MODULE_OWN_BYTECODE   = 1,
    KOS_MODULE_OWN_STRINGS    = 2,
    KOS_MODULE_OWN_LINE_ADDRS = 4,
    KOS_MODULE_OWN_FUNC_ADDRS = 8
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
    _KOS_TYPE_STORAGE    type; /* OBJ_MODULE */
    uint8_t              flags;
    KOS_OBJ_PTR          name;
    KOS_OBJ_PTR          path;
    KOS_CONTEXT         *context;
    KOS_STRING          *strings;
    KOS_OBJ_PTR          global_names;
    KOS_OBJ_PTR          globals;
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
    _KOS_TYPE_STORAGE type; /* OBJ_DYNAMIC_PROP */
    KOS_OBJ_PTR       getter;
    KOS_OBJ_PTR       setter;
} KOS_DYNAMIC_PROP;

typedef struct _KOS_OBJECT_WALK {
    _KOS_TYPE_STORAGE    type; /* OBJ_OBJECT_WALK */
    KOS_OBJ_PTR          obj;
    KOS_OBJ_PTR          key_table_obj;
    void                *key_table;
    KOS_ATOMIC(uint32_t) index;
} KOS_OBJECT_WALK;

typedef struct _KOS_SPECIAL {
    _KOS_TYPE_STORAGE type; /* OBJ_SPECIAL */
    void             *value;
} KOS_SPECIAL;

typedef union _KOS_ANY_OBJECT {
    _KOS_TYPE_STORAGE      type;
    KOS_INTEGER            integer;
    KOS_FLOAT              floatpt;
    KOS_STRING             string;
    struct _KOS_BOOLEAN    boolean;
    struct _KOS_VOID       a_void;
    KOS_OBJECT             object;
    KOS_ARRAY              array;
    KOS_BUFFER             buffer;
    KOS_FUNCTION           function;
    KOS_STACK_FRAME        stack_frame;
    KOS_MODULE             module;
    KOS_DYNAMIC_PROP       dynamic_prop;
    KOS_OBJECT_WALK        walk;
    KOS_SPECIAL            special;
#ifdef KOS_CPP11
    _KOS_ANY_OBJECT() = delete;
#endif
} KOS_ANY_OBJECT;

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_PTR KOS_new_int(KOS_STACK_FRAME *frame,
                        int64_t          value);

KOS_OBJ_PTR KOS_new_float(KOS_STACK_FRAME *frame,
                          double           value);

KOS_OBJ_PTR KOS_new_function(KOS_STACK_FRAME *frame,
                             KOS_OBJ_PTR      proto_obj);

KOS_OBJ_PTR KOS_new_builtin_function(KOS_STACK_FRAME     *frame,
                                     KOS_FUNCTION_HANDLER handler,
                                     int                  min_args);

KOS_OBJ_PTR KOS_new_dynamic_prop(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      getter,
                                 KOS_OBJ_PTR      setter);

extern struct _KOS_BOOLEAN kos_static_object_true;
extern struct _KOS_BOOLEAN kos_static_object_false;
extern struct _KOS_VOID    kos_static_object_void;

#ifdef __cplusplus
}
#endif

#define KOS_TRUE        (TO_OBJPTR(&kos_static_object_true))
#define KOS_FALSE       (TO_OBJPTR(&kos_static_object_false))
#define KOS_BOOL(value) ((value) ? KOS_TRUE : KOS_FALSE)
#define KOS_VOID        (TO_OBJPTR(&kos_static_object_void))

#ifdef __cplusplus

static inline int KOS_get_bool(KOS_OBJ_PTR objptr)
{
    return OBJPTR(struct _KOS_BOOLEAN, objptr)->value;
}

#else

#define KOS_get_bool(objptr) (OBJPTR(struct _KOS_BOOLEAN, objptr)->value)

#endif

#endif

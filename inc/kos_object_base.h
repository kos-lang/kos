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
    OBJ_CLASS,

    OBJ_LAST_TYPE = OBJ_CLASS,

    OBJ_OPAQUE,         /* Contains binary user data, contents not recognized by GC */

    OBJ_OBJECT_STORAGE,
    OBJ_ARRAY_STORAGE,
    OBJ_BUFFER_STORAGE,
    OBJ_DYNAMIC_PROP,
    OBJ_OBJECT_WALK,
    OBJ_MODULE,
    OBJ_STACK
} KOS_TYPE;

struct _KOS_OBJECT_PLACEHOLDER;

/* KOS_OBJ_ID contains either a pointer to the object or an integer number.
 * The least significant bit (bit 0) indicates whether it is a pointer or a
 * number.
 *
 * KOS_OBJ_ID's layout is:
 * - "Small" integer       ...iiii iiii iiii iii0 (31- or 63-bit signed integer)
 * - Heap object pointer   ...pppp pppp pppp p001 (8 byte-aligned pointer)
 * - Static object pointer ...pppp pppp pppp p101 (8 byte-aligned pointer)
 *
 * If bit 0 is a '1', the rest of KOS_OBJ_ID is treated as the pointer without
 * that bit set.  The actual pointer to the object is KOS_OBJ_ID minus 1.
 */
typedef struct _KOS_OBJECT_PLACEHOLDER *KOS_OBJ_ID;

/* Reference to a KOS_OBJ_ID, typically held on stack.  This allows GC to
 * update KOS_OBJ_IDs on the stack. */
typedef struct _KOS_OBJ_REF {
    KOS_ATOMIC(KOS_OBJ_ID) obj_id;
    struct _KOS_OBJ_REF   *next;
} KOS_OBJ_REF;

#define KOS_BADPTR ((KOS_OBJ_ID)(intptr_t)1)

typedef struct _KOS_OBJ_HEADER {
    /* During normal operation, alloc_size contains a small integer, which
     * encodes size of the allocation.
     *
     * When objects are moved to a new page during garbage collections,
     * alloc_size contains an object identifier of the new, target object that
     * has been allocated in the new page.
     */
    KOS_OBJ_ID alloc_size;
    uint8_t    type;        /* KOS_TYPE */
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
static inline bool IS_HEAP_OBJECT(KOS_OBJ_ID obj_id) {
    return (reinterpret_cast<intptr_t>(obj_id) & 7) == 1;
}
static inline KOS_TYPE READ_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    assert( ! IS_SMALL_INT(obj_id));
    assert( ! IS_BAD_PTR(obj_id));
    return static_cast<KOS_TYPE>(
               reinterpret_cast<const KOS_OBJ_HEADER *>(
                   reinterpret_cast<intptr_t>(obj_id) - 1)->type);
}
static inline KOS_TYPE GET_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    if (IS_SMALL_INT(obj_id))
        return OBJ_SMALL_INTEGER;
    assert( ! IS_BAD_PTR(obj_id));
    return static_cast<KOS_TYPE>(
               reinterpret_cast<const KOS_OBJ_HEADER *>(
                   reinterpret_cast<intptr_t>(obj_id) - 1)->type);
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
#define IS_HEAP_OBJECT(obj_id) ( ((intptr_t)(obj_id) & 7) == 1                     )
#define OBJID(tag, ptr)        ( (KOS_OBJ_ID) ((intptr_t)(ptr) + 1)                )
#define READ_OBJ_TYPE(obj_id)  ( (KOS_TYPE) ((KOS_OBJ_HEADER *)((uint8_t *)(obj_id) - 1))->type   )
#define GET_OBJ_TYPE(obj_id)   ( IS_SMALL_INT(obj_id) ? OBJ_SMALL_INTEGER : READ_OBJ_TYPE(obj_id) )

#endif

struct _KOS_INSTANCE;
typedef struct _KOS_INSTANCE KOS_INSTANCE;

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
        KOS_OBJ_ID alloc_size;
        uint8_t    type;
        uint8_t    value;
    }              boolean;
} KOS_BOOLEAN;

typedef struct _KOS_OPAQUE {
    KOS_OBJ_HEADER header;
} KOS_OPAQUE;

struct _KOS_CONST_OBJECT {
    uint32_t _align4;
    uint8_t  _alloc_size[sizeof(KOS_OBJ_ID)];
    uint8_t  type;
    uint8_t  value;
};

#define KOS_CONST_ID(obj) ( (KOS_OBJ_ID) ((intptr_t)&(obj)._alloc_size + 1) )

#ifdef KOS_CPP11
#   define DECLARE_CONST_OBJECT(name) alignas(8) const struct _KOS_CONST_OBJECT name
#   define DECLARE_STATIC_CONST_OBJECT(name) alignas(8) static const struct _KOS_CONST_OBJECT name
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#   include <stdalign.h>
#   define DECLARE_CONST_OBJECT(name) alignas(8) const struct _KOS_CONST_OBJECT name
#   define DECLARE_STATIC_CONST_OBJECT(name) alignas(8) static const struct _KOS_CONST_OBJECT name
#elif defined(__GNUC__)
#   define DECLARE_CONST_OBJECT(name) const struct _KOS_CONST_OBJECT name __attribute__ ((aligned (8)))
#   define DECLARE_STATIC_CONST_OBJECT(name) static const struct _KOS_CONST_OBJECT name __attribute__ ((aligned (8)))
#elif defined(_MSC_VER)
#   define DECLARE_CONST_OBJECT(name) __declspec(align(8)) const struct _KOS_CONST_OBJECT name
#   define DECLARE_STATIC_CONST_OBJECT(name) __declspec(align(8)) static const struct _KOS_CONST_OBJECT name
#endif

#define KOS_CONST_OBJECT_INIT(type, value) { 0, "", (type), (value) }

#ifdef __cplusplus
extern "C" {
#endif

extern const struct _KOS_CONST_OBJECT _kos_void;
extern const struct _KOS_CONST_OBJECT _kos_false;
extern const struct _KOS_CONST_OBJECT _kos_true;

#ifdef __cplusplus
}
#endif

#define KOS_VOID    KOS_CONST_ID(_kos_void)
#define KOS_FALSE   KOS_CONST_ID(_kos_false)
#define KOS_TRUE    KOS_CONST_ID(_kos_true)
#define KOS_BOOL(v) ( (v) ? KOS_TRUE : KOS_FALSE )

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
    KOS_OBJ_ID           alloc_size;
    uint8_t              type;
    uint8_t              flags;
    uint16_t             length;
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

typedef void (*KOS_FINALIZE)(KOS_CONTEXT ctx,
                             void       *priv);

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

typedef KOS_OBJ_ID (*KOS_FUNCTION_HANDLER)(KOS_CONTEXT ctx,
                                           KOS_OBJ_ID  this_obj,
                                           KOS_OBJ_ID  args_obj);

enum _KOS_FUNCTION_STATE {
    KOS_FUN,            /* regular function                                     */
    KOS_CTOR,           /* class constructor                                    */
    KOS_GEN_INIT,       /* generator initializer object                         */
    KOS_GEN_READY,      /* initialized generator function, but not executed yet */
    KOS_GEN_ACTIVE,     /* generator function halted in the middle of execution */
    KOS_GEN_RUNNING,    /* generator function is being run                      */
    KOS_GEN_DONE        /* generator function reached the return statement      */
};

typedef struct _KOS_FUN_HEADER {
    KOS_OBJ_ID           alloc_size;
    uint8_t              type;
    uint8_t              flags;
    uint8_t              num_args;
    uint8_t              num_regs;
} KOS_FUN_HEADER;

enum _KOS_FUNCTION_FLAGS {
    KOS_FUN_CLOSURE  = 1, /* Function's stack frame is a closure */
    KOS_FUN_ELLIPSIS = 2  /* Store remaining args in array       */
};

typedef struct _KOS_FUNCTION {
    KOS_FUN_HEADER         header;
    uint8_t                args_reg;
    uint8_t                state;    /* TODO convert to KOS_ATOMIC(uint32_t) */
    uint32_t               instr_offs;
    KOS_OBJ_ID             module;
    KOS_OBJ_ID             closures;
    KOS_OBJ_ID             defaults;
    KOS_FUNCTION_HANDLER   handler;
    KOS_OBJ_ID             generator_stack_frame;
} KOS_FUNCTION;

typedef struct _KOS_CLASS {
    KOS_FUN_HEADER         header;
    uint8_t                args_reg;
    uint8_t                _dummy;
    uint32_t               instr_offs;
    KOS_OBJ_ID             module;
    KOS_OBJ_ID             closures;
    KOS_OBJ_ID             defaults;
    KOS_FUNCTION_HANDLER   handler;
    KOS_ATOMIC(KOS_OBJ_ID) prototype;
    KOS_ATOMIC(KOS_OBJ_ID) props;
} KOS_CLASS;

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
    KOS_OBJ_HEADER          header;
    uint8_t                 flags;
    KOS_OBJ_ID              name;
    KOS_OBJ_ID              path;
    KOS_INSTANCE           *inst;
    KOS_OBJ_ID              constants_storage;
    KOS_ATOMIC(KOS_OBJ_ID) *constants;
    KOS_OBJ_ID              global_names;
    KOS_OBJ_ID              globals;
    KOS_OBJ_ID              module_names; /* Map of directly referenced modules to their indices, for REPL */
    const uint8_t          *bytecode;
    const KOS_LINE_ADDR    *line_addrs;
    const KOS_FUNC_ADDR    *func_addrs;
    uint32_t                num_line_addrs;
    uint32_t                num_func_addrs;
    uint32_t                bytecode_size;
    uint32_t                main_idx;     /* Index of constant with main function */
} KOS_MODULE;

typedef struct _KOS_DYNAMIC_PROP {
    KOS_OBJ_HEADER       header;
    KOS_OBJ_ID           getter;
    KOS_OBJ_ID           setter;
} KOS_DYNAMIC_PROP;

typedef struct _KOS_OBJECT_WALK {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   index;
    KOS_OBJ_ID             obj;
    KOS_OBJ_ID             key_table;
    KOS_ATOMIC(KOS_OBJ_ID) last_key;
    KOS_ATOMIC(KOS_OBJ_ID) last_value;
} KOS_OBJECT_WALK;

#ifdef __cplusplus
extern "C" {
#endif

KOS_OBJ_ID KOS_new_int(KOS_CONTEXT ctx,
                       int64_t     value);

KOS_OBJ_ID KOS_new_float(KOS_CONTEXT ctx,
                         double      value);

KOS_OBJ_ID KOS_new_function(KOS_CONTEXT ctx);

KOS_OBJ_ID KOS_new_class(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  proto_obj);

KOS_OBJ_ID KOS_new_builtin_function(KOS_CONTEXT          ctx,
                                    KOS_FUNCTION_HANDLER handler,
                                    int                  min_args);

KOS_OBJ_ID KOS_new_builtin_class(KOS_CONTEXT          ctx,
                                 KOS_FUNCTION_HANDLER handler,
                                 int                  min_args);

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  getter,
                                KOS_OBJ_ID  setter);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline bool KOS_get_bool(KOS_OBJ_ID obj_id)
{
    assert(obj_id == KOS_TRUE || obj_id == KOS_FALSE);
    return obj_id == KOS_TRUE;
}

#else

#define KOS_get_bool(obj_id) ((obj_id) == KOS_TRUE)

#endif

#endif

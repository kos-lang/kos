/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#ifndef KOS_OBJECT_BASE_H_INCLUDED
#define KOS_OBJECT_BASE_H_INCLUDED

#include <assert.h>
#include <stdint.h>
#include "kos_atomic.h"

/* Note: object types are always even.  See the description of size_and_type field
 * to find out why. */
typedef enum KOS_OBJECT_TYPE_E {
    OBJ_SMALL_INTEGER  = 0, /* Returned by GET_OBJ_TYPE, never used in any object */

    /* Language types */
    OBJ_INTEGER        = 2,
    OBJ_FLOAT          = 4,
    OBJ_VOID           = 6,
    OBJ_BOOLEAN        = 8,
    OBJ_STRING         = 10,
    OBJ_OBJECT         = 12,
    OBJ_ARRAY          = 14,
    OBJ_BUFFER         = 16,
    OBJ_FUNCTION       = 18,
    OBJ_CLASS          = 20,

    OBJ_LAST_TYPE = OBJ_CLASS, /* Last type exposed to the language */

    /* Internal types */

    OBJ_OPAQUE         = 22, /* Contains binary user data, contents not recognized by GC */

    OBJ_HUGE_TRACKER   = 24,
    OBJ_OBJECT_STORAGE = 26,
    OBJ_ARRAY_STORAGE  = 28,
    OBJ_BUFFER_STORAGE = 30,
    OBJ_DYNAMIC_PROP   = 32,
    OBJ_OBJECT_WALK    = 34,
    OBJ_MODULE         = 36,
    OBJ_STACK          = 38,
    OBJ_LOCAL_REFS     = 40,

    /* Just the last valid object id, not a real object type */
    OBJ_LAST           = OBJ_LOCAL_REFS
} KOS_TYPE;

struct KOS_OBJECT_PLACEHOLDER;

/* KOS_OBJ_ID contains either a pointer to the object or an integer number.
 * The least significant bit (bit 0) indicates whether it is a pointer or a
 * number.
 *
 * KOS_OBJ_ID's layout is:
 * - "Small" integer         ...iiii iiii iiii iii0 (31- or 63-bit signed integer)
 * - Heap object pointer     ...pppp pppp ppp0 0001 (32 byte-aligned pointer)
 * - Off-heap object pointer ...pppp pppp ppp0 1001 (8 byte-aligned pointer)
 * - Static object pointer   ...pppp pppp ppp1 0001 (16 byte-aligned pointer)
 *
 * If bit 0 is a '1', the rest of KOS_OBJ_ID is treated as the pointer without
 * that bit set.  The actual pointer to the object is KOS_OBJ_ID minus 1.
 *
 * Heap objects are tracked by the garbage collector.
 */
typedef struct KOS_OBJECT_PLACEHOLDER *KOS_OBJ_ID;

#define KOS_BADPTR ((KOS_OBJ_ID)(intptr_t)1)

typedef struct KOS_OBJ_HEADER_S {
    /* During normal operation, size_and_type contains a small integer, which
     * encodes size of the allocation and object type.
     *
     * Bits 0..7 contain object type, with bit 0 always being set to 0.
     * Bits 8..n contain allocation size in bytes.
     *
     * When objects are being moved to a new page during garbage collections,
     * size_and_type contains an object identifier of the new, target object that
     * has been allocated in the new page.
     *
     * For off-heap objects, the size field stores the offset from the pointer
     * to the allocation to the object itself (where the object header is).
     * For static objects (e.g. KOS_VOID, KOS_TRUE), the size field is zero.
     */
    KOS_OBJ_ID size_and_type;
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
    return static_cast<KOS_TYPE>(
               static_cast<uint8_t>(
                   reinterpret_cast<uintptr_t>(
                       reinterpret_cast<const KOS_OBJ_HEADER *>(
                           reinterpret_cast<intptr_t>(obj_id) - 1)->size_and_type)));
}
static inline KOS_TYPE GET_OBJ_TYPE(KOS_OBJ_ID obj_id) {
    if (IS_SMALL_INT(obj_id))
        return OBJ_SMALL_INTEGER;
    return READ_OBJ_TYPE(obj_id);
}
static inline bool IS_NUMERIC_OBJ(KOS_OBJ_ID obj_id) {
    return GET_OBJ_TYPE(obj_id) <= OBJ_FLOAT;
}
template<typename T>
static inline T* KOS_object_ptr(KOS_OBJ_ID obj_id, KOS_TYPE type) {
    assert( ! IS_SMALL_INT(obj_id));
    assert(GET_OBJ_TYPE(obj_id) == type ||
           (type == OBJ_FUNCTION && GET_OBJ_TYPE(obj_id) == OBJ_CLASS));
    return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(obj_id) - 1);
}
#define OBJPTR(tag, obj_id) KOS_object_ptr<KOS_##tag>(obj_id, OBJ_##tag)
template<typename T>
static inline KOS_OBJ_ID KOS_object_id(KOS_TYPE type, T *ptr)
{
    assert( ! ptr || type ==
           static_cast<KOS_TYPE>(
               static_cast<uint8_t>(
                   reinterpret_cast<uintptr_t>(
                       ptr->header.size_and_type))));
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
#define READ_OBJ_TYPE(obj_id)  ( (KOS_TYPE)(uint8_t)(uintptr_t)(((KOS_OBJ_HEADER *)((uint8_t *)(obj_id) - 1))->size_and_type) )
#define GET_OBJ_TYPE(obj_id)   ( IS_SMALL_INT(obj_id) ? OBJ_SMALL_INTEGER : READ_OBJ_TYPE(obj_id) )

#endif

struct KOS_THREAD_CONTEXT_S;
typedef struct KOS_THREAD_CONTEXT_S *KOS_CONTEXT;

struct KOS_INSTANCE_S;
typedef struct KOS_INSTANCE_S KOS_INSTANCE;

typedef struct KOS_INTEGER_S {
    KOS_OBJ_HEADER header;
    int64_t        value;
} KOS_INTEGER;

typedef struct KOS_FLOAT_S {
    KOS_OBJ_HEADER header;
    double         value;
} KOS_FLOAT;

typedef struct KOS_VOID_S {
    KOS_OBJ_HEADER header;
} KOS_VOID;

typedef struct KOS_BOOLEAN_S {
    KOS_OBJ_HEADER header;
    uint8_t        value;
} KOS_BOOLEAN;

typedef struct KOS_OPAQUE_S {
    KOS_OBJ_HEADER header;
} KOS_OPAQUE;

/* Huge object tracker, allocated on the heap */
typedef struct KOS_HUGE_TRACKER_S {
    KOS_OBJ_HEADER header;
    void          *data;   /* Pointer to the memory allocation   */
    KOS_OBJ_ID     object; /* Id of the object in the allocation */
    uint32_t       size;   /* Size of the memory allocation      */
} KOS_HUGE_TRACKER;

typedef enum KOS_STRING_FLAGS_E {
    /* Two lowest bits specify string element (character) size in bytes */
    KOS_STRING_ELEM_8    = 0,
    KOS_STRING_ELEM_16   = 1,
    KOS_STRING_ELEM_32   = 2,
    KOS_STRING_ELEM_MASK = 3,

    KOS_STRING_LOCAL     = 4, /* The string is stored entirely in the string object.          */
    KOS_STRING_PTR       = 0, /* The string is stored somewhere else, we only have a pointer. */
    KOS_STRING_REF       = 8  /* The string is stored in another string, we have a reference. */
} KOS_STRING_FLAGS;

typedef struct KOS_STR_HEADER_S {
    KOS_OBJ_ID           size_and_type;
    KOS_ATOMIC(uint32_t) hash;
    uint16_t             length;
    uint8_t              flags;
} KOS_STR_HEADER;

struct KOS_STRING_LOCAL_S {
    KOS_STR_HEADER header;
    uint8_t        data[1];
};

struct KOS_STRING_PTR_S {
    KOS_STR_HEADER header;
    const void    *data_ptr;
};

struct KOS_STRING_REF_S {
    KOS_STR_HEADER header;
    const void    *data_ptr;
    KOS_OBJ_ID     obj_id;
};

typedef union KOS_STRING_U {
    KOS_STR_HEADER            header;
    struct KOS_STRING_LOCAL_S local;
    struct KOS_STRING_PTR_S   ptr;
    struct KOS_STRING_REF_S   ref;
} KOS_STRING;

#define KOS_CONST_ID(obj) ( (KOS_OBJ_ID) ((intptr_t)&(obj).object + 1) )

#ifdef KOS_CPP11
#   define KOS_DECLARE_ALIGNED(alignment, object) alignas(alignment) object
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#   include <stdalign.h>
#   define KOS_DECLARE_ALIGNED(alignment, object) alignas(alignment) object
#elif defined(__GNUC__)
#   define KOS_DECLARE_ALIGNED(alignment, object) object __attribute__((aligned(alignment)))
#elif defined(_MSC_VER)
#   define KOS_DECLARE_ALIGNED(alignment, object) __declspec(align(alignment)) object
#endif

struct KOS_CONST_OBJECT_ALIGNMENT_S {
    uint64_t align[2];
};

struct KOS_CONST_OBJECT_S {
    struct KOS_CONST_OBJECT_ALIGNMENT_S align;
    struct {
        uintptr_t size_and_type;
        uint8_t   value;
    } object;
};

struct KOS_CONST_STRING_S {
    struct KOS_CONST_OBJECT_ALIGNMENT_S align;
    struct {
        uintptr_t   size_and_type;
        uint32_t    hash;
        uint16_t    length;
        uint8_t     flags;
        const char *data_ptr;
    } object;
};

#define DECLARE_CONST_OBJECT(name, type, value)                     \
    KOS_DECLARE_ALIGNED(32, const struct KOS_CONST_OBJECT_S name) = \
    { { { 0, 0 } }, { (type), (value) } }

#define DECLARE_STATIC_CONST_OBJECT(name, type, value)                     \
    KOS_DECLARE_ALIGNED(32, static const struct KOS_CONST_OBJECT_S name) = \
    { { { 0, 0 } }, { (type), (value) } }

#define KOS_DECLARE_CONST_STRING_WITH_LENGTH(name, length, str) \
    KOS_DECLARE_ALIGNED(32, struct KOS_CONST_STRING_S name) =  \
    { { { 0, 0 } }, { OBJ_STRING, 0, (length), KOS_STRING_ELEM_8 | KOS_STRING_PTR, (str) } }

#define KOS_CONCAT_NAME_INTERNAL(a, b) a ## b

#define KOS_CONCAT_NAME(a, b) KOS_CONCAT_NAME_INTERNAL(a, b)

#define KOS_DECLARE_CONST_STRING(name, str)                               \
    static const char KOS_CONCAT_NAME(str_ ## name, __LINE__)[] = str;    \
    KOS_DECLARE_CONST_STRING_WITH_LENGTH(name,                            \
            (uint16_t)sizeof(KOS_CONCAT_NAME(str_##name, __LINE__)) - 1U, \
            KOS_CONCAT_NAME(str_##name, __LINE__))

#ifdef __cplusplus
extern "C" {
#endif

extern const struct KOS_CONST_OBJECT_S KOS_void;
extern const struct KOS_CONST_OBJECT_S KOS_false;
extern const struct KOS_CONST_OBJECT_S KOS_true;

#ifdef __cplusplus
}
#endif

#define KOS_VOID    KOS_CONST_ID(KOS_void)
#define KOS_FALSE   KOS_CONST_ID(KOS_false)
#define KOS_TRUE    KOS_CONST_ID(KOS_true)
#define KOS_BOOL(v) ( (v) ? KOS_TRUE : KOS_FALSE )

typedef void (*KOS_FINALIZE)(KOS_CONTEXT ctx,
                             void       *priv);

typedef struct KOS_OBJECT_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(KOS_OBJ_ID) props;
    KOS_OBJ_ID             prototype;
    KOS_ATOMIC(void *)     priv;
    KOS_FINALIZE           finalize;
} KOS_OBJECT;

typedef struct KOS_BUFFER_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(KOS_OBJ_ID) data;
} KOS_BUFFER;

typedef struct KOS_BUFFER_S KOS_ARRAY;

typedef KOS_OBJ_ID (*KOS_FUNCTION_HANDLER)(KOS_CONTEXT ctx,
                                           KOS_OBJ_ID  this_obj,
                                           KOS_OBJ_ID  args_obj);

typedef enum KOS_FUNCTION_STATE_E {
    KOS_FUN,            /* regular function                                     */
    KOS_CTOR,           /* class constructor                                    */
    KOS_GEN_INIT,       /* generator initializer object                         */
    KOS_GEN_READY,      /* initialized generator function, but not executed yet */
    KOS_GEN_ACTIVE,     /* generator function halted in the middle of execution */
    KOS_GEN_RUNNING,    /* generator function is being run                      */
    KOS_GEN_DONE        /* generator function reached the return statement      */
} KOS_FUNCTION_STATE;

enum KOS_FUNCTION_FLAGS_E {
    KOS_FUN_CLOSURE  = 1, /* Function's stack frame is a closure */
    KOS_FUN_ELLIPSIS = 2  /* Store remaining args in array       */
};

typedef struct KOS_FUNCTION_S {
    KOS_OBJ_HEADER       header;
    uint8_t              flags;
    uint8_t              num_args;
    uint8_t              num_regs;
    uint8_t              args_reg;
    uint8_t              state;    /* TODO convert to KOS_ATOMIC(uint32_t) */
    uint32_t             instr_offs;
    KOS_OBJ_ID           module;
    KOS_OBJ_ID           closures;
    KOS_OBJ_ID           defaults;
    KOS_FUNCTION_HANDLER handler;
    KOS_OBJ_ID           generator_stack_frame;
} KOS_FUNCTION;

typedef struct KOS_CLASS_S {
    KOS_OBJ_HEADER         header;
    uint8_t                flags;
    uint8_t                num_args;
    uint8_t                num_regs;
    uint8_t                args_reg;
    uint8_t                dummy;
    uint32_t               instr_offs;
    KOS_OBJ_ID             module;
    KOS_OBJ_ID             closures;
    KOS_OBJ_ID             defaults;
    KOS_FUNCTION_HANDLER   handler;
    KOS_ATOMIC(KOS_OBJ_ID) prototype;
    KOS_ATOMIC(KOS_OBJ_ID) props;
} KOS_CLASS;

enum KOS_MODULE_FLAGS_E {
    KOS_MODULE_OWN_BYTECODE   = 1,
    KOS_MODULE_OWN_LINE_ADDRS = 2,
    KOS_MODULE_OWN_FUNC_ADDRS = 4
};

typedef struct KOS_LINE_ADDR_S {
    uint32_t offs;
    uint32_t line;
} KOS_LINE_ADDR;

typedef struct KOS_FUNC_ADDR_S {
    uint32_t offs;
    uint32_t line;
    uint32_t str_idx;
    uint32_t num_instr;
    uint32_t code_size;
} KOS_FUNC_ADDR;

typedef struct KOS_MODULE_S {
    KOS_OBJ_HEADER          header;
    uint8_t                 flags;
    KOS_OBJ_ID              name;
    KOS_OBJ_ID              path;
    KOS_INSTANCE           *inst;
    KOS_OBJ_ID              constants;
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

typedef struct KOS_DYNAMIC_PROP_S {
    KOS_OBJ_HEADER       header;
    KOS_OBJ_ID           getter;
    KOS_OBJ_ID           setter;
} KOS_DYNAMIC_PROP;

typedef struct KOS_OBJECT_WALK_S {
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

KOS_OBJ_ID KOS_new_dynamic_prop(KOS_CONTEXT ctx);

KOS_OBJ_ID KOS_new_builtin_dynamic_prop(KOS_CONTEXT          ctx,
                                        KOS_OBJ_ID           module_obj,
                                        KOS_FUNCTION_HANDLER getter,
                                        KOS_FUNCTION_HANDLER setter);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline bool KOS_get_bool(KOS_OBJ_ID obj_id)
{
    assert(obj_id == KOS_TRUE || obj_id == KOS_FALSE);
    return obj_id == KOS_TRUE;
}

static inline KOS_OBJ_ID KOS_atomic_read_relaxed_obj(KOS_ATOMIC(KOS_OBJ_ID)& src)
{
    return (KOS_OBJ_ID)KOS_atomic_read_relaxed_ptr(src);
}

static inline KOS_OBJ_ID KOS_atomic_read_acquire_obj(KOS_ATOMIC(KOS_OBJ_ID)& src)
{
    return (KOS_OBJ_ID)KOS_atomic_read_acquire_ptr(src);
}

#else

#define KOS_get_bool(obj_id) ((obj_id) == KOS_TRUE)

#define KOS_atomic_read_relaxed_obj(src) ((KOS_OBJ_ID)KOS_atomic_read_relaxed_ptr(src))

#define KOS_atomic_read_acquire_obj(src) ((KOS_OBJ_ID)KOS_atomic_read_acquire_ptr(src))

#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_ENTITY_H_INCLUDED
#define KOS_ENTITY_H_INCLUDED

#include "kos_api.h"
#include "kos_atomic.h"
#include <assert.h>
#include <stdint.h>

/* Note: entity types are always even.  See the description of size_and_type field
 * to find out why. */
typedef enum KOS_ENTITY_TYPE_E {
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
    OBJ_MODULE         = 22,

    OBJ_LAST_TYPE      = OBJ_MODULE, /* Last type exposed to the language */

    /* Internal types */
    OBJ_OPAQUE         = 24, /* Contains binary user data, contents not recognized by GC */
    OBJ_HUGE_TRACKER   = 26,
    OBJ_OBJECT_STORAGE = 28,
    OBJ_ARRAY_STORAGE  = 30,
    OBJ_BUFFER_STORAGE = 32,
    OBJ_DYNAMIC_PROP   = 34,
    OBJ_ITERATOR       = 36,
    OBJ_STACK          = 38,

    /* Just the last valid object id, not a real object type */
    OBJ_LAST_POSSIBLE  = OBJ_STACK
} KOS_TYPE;

struct KOS_ENTITY_PLACEHOLDER;

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
 * Heap objects are tracked by the garbage collector.  "Heap" in this context means
 * the VM's heap, managed by the garbage collector.
 *
 * Off-heap objects are allocated using malloc(), but they have a tracker object
 * (OBJ_HUGE_TRACKER) associated with them, which is allocated on the heap.
 */
typedef struct KOS_ENTITY_PLACEHOLDER *KOS_OBJ_ID;

#define KOS_BADPTR ((KOS_OBJ_ID)(intptr_t)1)

typedef struct KOS_OBJ_HEADER_S {
    /* During normal operation, size_and_type contains a small integer, which
     * encodes size of the allocation and object type.
     *
     * Bits 0..7 contain object type, with bit 0 always being set to 0.
     * Bits 8..n contain allocation size, in bytes.
     *
     * When objects are being moved to a new page during garbage collections,
     * size_and_type contains an object identifier of the new, target object that
     * has been allocated in the new page.
     *
     * For off-heap objects, the size field stores the offset from the pointer
     * to the allocation to the object itself (where the object header is).
     * It means that the pointer to the actual allocation is obtained by
     * subtracting the value of the size field from the KOS_OBJ_ID - 1.
     *
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
    return reinterpret_cast<KOS_OBJ_ID>(static_cast<uintptr_t>(value) << 1);
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
} KOS_VOID_TYPE;

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
    /* Bits 0..1 specify string element (character) size in bytes */
    KOS_STRING_ELEM_8    = 0,
    KOS_STRING_ELEM_16   = 1,
    KOS_STRING_ELEM_32   = 2,
    KOS_STRING_ELEM_MASK = 3,

    /* Bit 2 indicates whether it's an ASCII string */
    KOS_STRING_ASCII     = 4,

    /* Bits 3..4 specify how the string is stored */
    KOS_STRING_LOCAL     = 8,  /* The string is stored entirely in the string object.          */
    KOS_STRING_PTR       = 0,  /* The string is stored somewhere else, we only have a pointer. */
    KOS_STRING_REF       = 16, /* The string is stored in another string, we have a reference. */
    KOS_STRING_STOR_MASK = 24
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

#if defined(KOS_CPP11)
#   define KOS_ALIGN(alignment) alignas(alignment)
#   define KOS_ALIGN_ATTRIBUTE(alignment)
#   define KOS_ALIGN_PRE_STRUCT(alignment)
#   define KOS_ALIGN_POST_STRUCT(alignment) KOS_ALIGN(alignment)
#elif defined(__GNUC__)
#   define KOS_ALIGN(alignment)
#   define KOS_ALIGN_ATTRIBUTE(alignment) __attribute__((aligned(alignment)))
#   define KOS_ALIGN_PRE_STRUCT(alignment)
#   define KOS_ALIGN_POST_STRUCT(alignment) KOS_ALIGN_ATTRIBUTE(alignment)
#elif defined(_MSC_VER)
#   define KOS_ALIGN(alignment) __declspec(align(alignment))
#   define KOS_ALIGN_ATTRIBUTE(alignment)
#   define KOS_ALIGN_PRE_STRUCT(alignment) KOS_ALIGN(alignment)
#   define KOS_ALIGN_POST_STRUCT(alignment)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#   include <stdalign.h>
#   define KOS_ALIGN(alignment) _Alignas(alignment)
#   define KOS_ALIGN_ATTRIBUTE(alignment)
    /* C11 does not allow alignment on struct definitions or types */
#   define KOS_ALIGN_PRE_STRUCT(alignment)
#   define KOS_ALIGN_POST_STRUCT(alignment)
#endif

#define KOS_DECLARE_ALIGNED(alignment, object) KOS_ALIGN(alignment) object KOS_ALIGN_ATTRIBUTE(alignment)
#define KOS_ALIGNED_STRUCT(alignment) KOS_ALIGN_PRE_STRUCT(alignment) struct KOS_ALIGN_POST_STRUCT(alignment)

#ifdef _MSC_VER
#   define KOS_DECLARE_ALIGNED_API(alignment, object) KOS_API KOS_DECLARE_ALIGNED(alignment, object)
#elif defined(__GNUC__)
#   define KOS_DECLARE_ALIGNED_API(alignment, object) KOS_DECLARE_ALIGNED(alignment, object) KOS_API
#else
#   define KOS_DECLARE_ALIGNED_API KOS_DECLARE_ALIGNED
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

struct KOS_CONST_ARRAY_S {
    struct KOS_CONST_OBJECT_ALIGNMENT_S align;
    struct {
        uintptr_t  size_and_type;
        uint32_t   size;
        uint32_t   flags;
        KOS_OBJ_ID data;
    } object;
};

#define DECLARE_CONST_OBJECT(name, type, value)                     \
    KOS_DECLARE_ALIGNED(32, const struct KOS_CONST_OBJECT_S name) = \
    { { { 0, 0 } }, { (type), (value) } }

#define DECLARE_STATIC_CONST_OBJECT(name, type, value)                     \
    KOS_DECLARE_ALIGNED(32, static const struct KOS_CONST_OBJECT_S name) = \
    { { { 0, 0 } }, { (type), (value) } }

#define KOS_DECLARE_CONST_STRING_WITH_LENGTH(name, length, str) \
    KOS_DECLARE_ALIGNED(32, struct KOS_CONST_STRING_S name) =   \
    { { { 0, 0 } }, { OBJ_STRING, 0, (length), KOS_STRING_ASCII | KOS_STRING_PTR, (str) } }

#define KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(name, length, str) \
    KOS_DECLARE_ALIGNED(32, static struct KOS_CONST_STRING_S name) =   \
    { { { 0, 0 } }, { OBJ_STRING, 0, (length), KOS_STRING_ASCII | KOS_STRING_PTR, (str) } }

#define KOS_CONCAT_NAME_INTERNAL(a, b) a ## b

#define KOS_CONCAT_NAME(a, b) KOS_CONCAT_NAME_INTERNAL(a, b)

#define KOS_DECLARE_CONST_STRING(name, str)                               \
    static const char KOS_CONCAT_NAME(str_ ## name, __LINE__)[] = str;    \
    KOS_DECLARE_CONST_STRING_WITH_LENGTH(name,                            \
            (uint16_t)sizeof(KOS_CONCAT_NAME(str_##name, __LINE__)) - 1U, \
            KOS_CONCAT_NAME(str_##name, __LINE__))

#define KOS_DECLARE_STATIC_CONST_STRING(name, str)                        \
    static const char KOS_CONCAT_NAME(str_ ## name, __LINE__)[] = str;    \
    KOS_DECLARE_STATIC_CONST_STRING_WITH_LENGTH(name,                     \
            (uint16_t)sizeof(KOS_CONCAT_NAME(str_##name, __LINE__)) - 1U, \
            KOS_CONCAT_NAME(str_##name, __LINE__))

typedef void (*KOS_FINALIZE)(KOS_CONTEXT ctx,
                             void       *priv);

typedef const struct KOS_PRIVATE_CLASS_S *KOS_PRIVATE_CLASS;

typedef struct KOS_OBJECT_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(KOS_OBJ_ID) props;
    KOS_OBJ_ID             prototype;
    KOS_PRIVATE_CLASS      priv_class;
} KOS_OBJECT;

/* If priv_class is set, object has additional fields.  It's still OBJ_OBJECT type. */
typedef struct KOS_OBJECT_WITH_PRIVATE_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(KOS_OBJ_ID) props;
    KOS_OBJ_ID             prototype;
    KOS_PRIVATE_CLASS      priv_class;
    KOS_ATOMIC(void *)     priv;
    KOS_FINALIZE           finalize;
} KOS_OBJECT_WITH_PRIVATE;

enum KOS_BUF_FLAGS_E {
    KOS_READ_ONLY        = 1,   /* Buffer or array is read-only                          */
    KOS_EXTERNAL_STORAGE = 2    /* Buffer storage is not managed by Kos (e.g. from mmap) */
};

typedef struct KOS_BUFFER_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(uint32_t)   flags;
    KOS_ATOMIC(KOS_OBJ_ID) data;
} KOS_BUFFER;

typedef struct KOS_ARRAY_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   size;
    KOS_ATOMIC(uint32_t)   flags;
    KOS_ATOMIC(KOS_OBJ_ID) data;
} KOS_ARRAY;

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

#define KOS_NO_REG 255U

typedef struct KOS_FUNCTION_OPTS_S {
    uint8_t num_regs;     /* Number of registers used by the function */
    uint8_t closure_size; /* Number of registers preserved for a closure */

    uint8_t min_args;     /* Number of args without default values */
    uint8_t num_def_args; /* Number of args with default values */

    uint8_t num_binds;    /* Number of binds */

    uint8_t args_reg;     /* Register where first argument is stored */
    uint8_t rest_reg;     /* Register containing rest args */
    uint8_t ellipsis_reg; /* Register containing ellipsis */
    uint8_t this_reg;     /* Register containing 'this' */
    uint8_t bind_reg;     /* First bound register */
} KOS_FUNCTION_OPTS;

typedef struct KOS_FUNCTION_S {
    KOS_OBJ_HEADER       header;
    KOS_FUNCTION_OPTS    opts;
    KOS_ATOMIC(uint32_t) state;
    KOS_OBJ_ID           bytecode; /* Buffer storage with bytecode */
    KOS_OBJ_ID           module;
    KOS_OBJ_ID           name;     /* Function name */
    KOS_OBJ_ID           closures; /* Array with bound closures */
    KOS_OBJ_ID           defaults; /* Array with bound default values for arguments */
    KOS_OBJ_ID           arg_map;  /* Object which maps argument names to indexes */
    KOS_FUNCTION_HANDLER handler;  /* TODO store this in bytecode member */
    KOS_OBJ_ID           generator_stack_frame;
} KOS_FUNCTION;

typedef struct KOS_CLASS_S {
    KOS_OBJ_HEADER         header;
    KOS_FUNCTION_OPTS      opts;
    uint32_t               dummy;
    KOS_OBJ_ID             bytecode; /* Buffer storage with bytecode */
    KOS_OBJ_ID             module;
    KOS_OBJ_ID             name;     /* Function name */
    KOS_OBJ_ID             closures; /* Array with bound closures */
    KOS_OBJ_ID             defaults; /* Array with bound default values for arguments */
    KOS_OBJ_ID             arg_map;  /* Object which maps argument names to indexes */
    KOS_FUNCTION_HANDLER   handler;  /* TODO store this in bytecode member */
    KOS_ATOMIC(KOS_OBJ_ID) prototype;
    KOS_ATOMIC(KOS_OBJ_ID) props;
} KOS_CLASS;

typedef struct KOS_BYTECODE_S {
    KOS_OBJ_HEADER         header;
    uint32_t               bytecode_size;    /* Bytecode size in bytes                                  */
    uint32_t               addr2line_offset; /* Offset to addr2line in bytecode array                   */
    uint32_t               addr2line_size;   /* Addr2line size in bytes                                 */
    uint32_t               def_line;         /* First line in source code where the function is defined */
    uint32_t               num_instr;        /* Number of instructions in the function                  */
    uint8_t                bytecode[1];      /* Bytecode followed by KOS_LINE_ADDR structs              */
} KOS_BYTECODE;

typedef struct KOS_LINE_ADDR_S {
    uint32_t offs;
    uint32_t line;
} KOS_LINE_ADDR;

typedef void (*KOS_MODULE_FINALIZE)(void);

typedef struct KOS_MODULE_S {
    KOS_OBJ_HEADER         header;
    KOS_OBJ_ID             name;
    KOS_OBJ_ID             path;
    KOS_INSTANCE          *inst;
    KOS_OBJ_ID             constants;
    KOS_OBJ_ID             global_names;
    KOS_OBJ_ID             globals;
    KOS_OBJ_ID             module_names; /* Map of directly referenced modules to their indices, for REPL */
    KOS_ATOMIC(KOS_OBJ_ID) priv;
    KOS_MODULE_FINALIZE    finalize;     /* Function to call when unloading the module */
    uint32_t               main_idx;     /* Index of constant with global scope "function" */
} KOS_MODULE;

typedef struct KOS_DYNAMIC_PROP_S {
    KOS_OBJ_HEADER       header;
    KOS_OBJ_ID           getter;
    KOS_OBJ_ID           setter;
} KOS_DYNAMIC_PROP;

enum KOS_DEPTH_E {
    KOS_DEEP,    /* Iterate over properties of this object and prototypes        */
    KOS_SHALLOW, /* Iterate over properties of this object, but not prototypes   */
    KOS_CONTENTS /* Iterate over contents of the container (e.g. array elements) */
};

typedef struct KOS_ITERATOR_S {
    KOS_OBJ_HEADER         header;
    KOS_ATOMIC(uint32_t)   index;
    uint8_t                type;
    uint8_t                depth;
    KOS_ATOMIC(KOS_OBJ_ID) obj;
    KOS_ATOMIC(KOS_OBJ_ID) prop_obj;
    KOS_ATOMIC(KOS_OBJ_ID) key_table;
    KOS_ATOMIC(KOS_OBJ_ID) returned_keys;
    KOS_ATOMIC(KOS_OBJ_ID) last_key;
    KOS_ATOMIC(KOS_OBJ_ID) last_value;
} KOS_ITERATOR;

/* Used for converting data between Kos internal objects and native values.
 * Often used to describe function arguments, but it can also be used to extract
 * Kos values into native data or the other way around.
 */
typedef struct KOS_CONVERT_S {
    KOS_OBJ_ID name;            /* Name of the value.  E.g. argument or property name.  Used when printing errors.      */
    KOS_OBJ_ID default_value;   /* Default value if the value does not exist.  This is KOS_BADPTR if value is required. */
    uint16_t   offset;          /* Field offset, if used with native structures.                                        */
    uint16_t   size;            /* Field size in bytes.  This can be a multiple of type size for fixed-size arrays.     */
    uint8_t    type;            /* KOS_CONV_TYPE, type of the corresponding native storage for conversion.              */
} KOS_CONVERT;

#define KOS_DEFINE_OPTIONAL_ARG(name, default_value) { KOS_CONST_ID(name), (default_value), 0, 0, 0 }
#define KOS_DEFINE_MANDATORY_ARG(name)               { KOS_CONST_ID(name), KOS_BADPTR,      0, 0, 0 }
#define KOS_DEFINE_TAIL_ARG()                        { KOS_BADPTR,         KOS_BADPTR,      0, 0, 0 }

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_new_int(KOS_CONTEXT ctx,
                       int64_t     value);

KOS_API
KOS_OBJ_ID KOS_new_float(KOS_CONTEXT ctx,
                         double      value);

KOS_API
KOS_OBJ_ID KOS_new_function(KOS_CONTEXT ctx);

KOS_API
KOS_OBJ_ID KOS_new_class(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  proto_obj);

KOS_API
KOS_OBJ_ID KOS_new_builtin_function(KOS_CONTEXT          ctx,
                                    KOS_OBJ_ID           name_obj,
                                    KOS_FUNCTION_HANDLER handler,
                                    const KOS_CONVERT   *args);

KOS_API
KOS_OBJ_ID KOS_new_builtin_class(KOS_CONTEXT          ctx,
                                 KOS_OBJ_ID           name_obj,
                                 KOS_FUNCTION_HANDLER handler,
                                 const KOS_CONVERT   *args);

KOS_API
KOS_OBJ_ID KOS_new_dynamic_prop(KOS_CONTEXT ctx);

KOS_API
KOS_OBJ_ID KOS_new_iterator(KOS_CONTEXT      ctx,
                            KOS_OBJ_ID       obj_id,
                            enum KOS_DEPTH_E depth);

KOS_API
KOS_OBJ_ID KOS_new_iterator_copy(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  walk_id);

KOS_API
int KOS_iterator_next(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  walk_id);

KOS_API
int KOS_lock_object(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id);

KOS_API
KOS_OBJ_ID KOS_get_named_arg(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  func_obj,
                             uint32_t    i);

KOS_API
unsigned KOS_function_addr_to_line(KOS_OBJ_ID func_obj,
                                   uint32_t   offs);

KOS_API
uint32_t KOS_function_get_def_line(KOS_OBJ_ID func_obj);

KOS_API
uint32_t KOS_function_get_num_instr(KOS_OBJ_ID func_obj);

KOS_API
uint32_t KOS_function_get_code_size(KOS_OBJ_ID func_obj);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

static inline KOS_OBJ_ID KOS_atomic_read_relaxed_obj(const KOS_ATOMIC(KOS_OBJ_ID)& src)
{
    return (KOS_OBJ_ID)KOS_atomic_read_relaxed_ptr(src);
}

static inline KOS_OBJ_ID KOS_atomic_read_acquire_obj(const KOS_ATOMIC(KOS_OBJ_ID)& src)
{
    return (KOS_OBJ_ID)KOS_atomic_read_acquire_ptr(src);
}

#else

#define KOS_atomic_read_relaxed_obj(src) ((KOS_OBJ_ID)KOS_atomic_read_relaxed_ptr(src))

#define KOS_atomic_read_acquire_obj(src) ((KOS_OBJ_ID)KOS_atomic_read_acquire_ptr(src))

#endif

#endif

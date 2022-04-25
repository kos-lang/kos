/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#ifndef KOS_MODULE_H_INCLUDED
#define KOS_MODULE_H_INCLUDED

#include "kos_api.h"
#include "kos_entity.h"
#ifdef _WIN32
#   include <crtdbg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
KOS_OBJ_ID KOS_load_module(KOS_CONTEXT ctx,
                           const char *path,
                           unsigned    path_len);

KOS_API
KOS_OBJ_ID KOS_load_module_from_memory(KOS_CONTEXT ctx,
                                       const char *module_name,
                                       unsigned    module_name_len,
                                       const char *buf,
                                       unsigned    buf_size);

KOS_API
KOS_OBJ_ID KOS_run_module(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj);

enum KOS_REPL_FLAGS_E {
    KOS_RUN_NO_FLAGS     = 0,
    KOS_IMPORT_BASE      = 1,
    KOS_RUN_INTERACTIVE  = 2,
    KOS_RUN_TEMPORARY    = 4,
    KOS_RUN_CONTINUE     = 8,
    KOS_RUN_STDIN        = 16
};

KOS_API
KOS_OBJ_ID KOS_repl(KOS_CONTEXT ctx,
                    const char *module_name,
                    unsigned    flags,
                    KOS_OBJ_ID *out_module,
                    const char *buf,
                    unsigned    buf_size);

enum KOS_RUN_FUNC_FLAGS_E {
    KOS_FUNC_OPTIONAL,
    KOS_FUNC_REQUIRED
};

KOS_API
KOS_OBJ_ID KOS_module_run_function(KOS_CONTEXT               ctx,
                                   KOS_OBJ_ID                module_obj,
                                   KOS_OBJ_ID                func_name,
                                   enum KOS_RUN_FUNC_FLAGS_E run_flags);

KOS_API
KOS_OBJ_ID KOS_get_module(KOS_CONTEXT ctx);

KOS_API
int KOS_module_add_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID  value,
                          unsigned   *idx);

KOS_API
int KOS_module_get_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID *value,
                          unsigned   *idx);

KOS_API
int KOS_module_add_function(KOS_CONTEXT          ctx,
                            KOS_OBJ_ID           module_obj,
                            KOS_OBJ_ID           str_name,
                            KOS_FUNCTION_HANDLER handler,
                            const KOS_CONVERT   *args,
                            KOS_FUNCTION_STATE   gen_state);

KOS_API
int KOS_module_add_constructor(KOS_CONTEXT          ctx,
                               KOS_OBJ_ID           module_obj,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER handler,
                               const KOS_CONVERT   *args,
                               KOS_OBJ_ID          *ret_proto);

KOS_API
int KOS_module_add_member_function(KOS_CONTEXT          ctx,
                                   KOS_OBJ_ID           module_obj,
                                   KOS_OBJ_ID           proto_obj,
                                   KOS_OBJ_ID           str_name,
                                   KOS_FUNCTION_HANDLER handler,
                                   const KOS_CONVERT   *args,
                                   KOS_FUNCTION_STATE   gen_state);

KOS_API
unsigned KOS_module_addr_to_line(KOS_MODULE *module,
                                 uint32_t    offs);

KOS_API
unsigned KOS_module_addr_to_func_line(KOS_MODULE *module,
                                      uint32_t    offs);

KOS_API
uint32_t KOS_module_func_get_num_instr(KOS_MODULE *module,
                                       uint32_t    offs);

KOS_API
uint32_t KOS_module_func_get_code_size(KOS_MODULE *module,
                                       uint32_t    offs);

#ifdef __cplusplus
}
#endif

#define TRY_ADD_GLOBAL(ctx, module, name, value)                                            \
do {                                                                                        \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                                        \
    TRY(KOS_module_add_global((ctx), (module), KOS_CONST_ID(XstrNAME), (value), KOS_NULL)); \
} while (0)

#define TRY_ADD_FUNCTION(ctx, module, name, handler, args)               \
do {                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                     \
    TRY(KOS_module_add_function((ctx), (module), KOS_CONST_ID(XstrNAME), \
                                (handler), (args), KOS_FUN));            \
} while (0)

#define TRY_ADD_GENERATOR(ctx, module, name, handler, args)              \
do {                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                     \
    TRY(KOS_module_add_function((ctx), (module), KOS_CONST_ID(XstrNAME), \
                                (handler), (args), KOS_GEN_INIT));       \
} while (0)

#define TRY_ADD_CONSTRUCTOR(ctx, module, name, handler, args, ret_proto)    \
do {                                                                        \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                        \
    TRY(KOS_module_add_constructor((ctx), (module), KOS_CONST_ID(XstrNAME), \
                                   (handler), (args), (ret_proto)));        \
} while (0)

#define TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, name, handler, args)                 \
do {                                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                                     \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), KOS_CONST_ID(XstrNAME), \
                                       (handler), (args), KOS_FUN));                     \
} while (0)

#define TRY_ADD_MEMBER_GENERATOR(ctx, module, proto, name, handler, args)                \
do {                                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                                     \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), KOS_CONST_ID(XstrNAME), \
                                       (handler), (args), KOS_GEN_INIT));                \
} while (0)

#define TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, name, getter, setter)        \
do {                                                                             \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                             \
    TRY(KOS_set_builtin_dynamic_property((ctx), (proto), KOS_CONST_ID(XstrNAME), \
                                         (module), (getter), (setter)));         \
} while (0)

#define TRY_ADD_INTEGER_CONSTANT(ctx, module, name, value)             \
do {                                                                   \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                   \
    TRY(KOS_module_add_global((ctx), (module), KOS_CONST_ID(XstrNAME), \
                              TO_SMALL_INT((int)(value)), KOS_NULL));  \
} while (0)

#define TRY_ADD_STRING_CONSTANT(ctx, module, name, value)              \
do {                                                                   \
    KOS_DECLARE_STATIC_CONST_STRING(XstrNAME, name);                   \
    KOS_DECLARE_STATIC_CONST_STRING(XstrVALUE, value);                 \
    TRY(KOS_module_add_global((ctx), (module), KOS_CONST_ID(XstrNAME), \
                              KOS_CONST_ID(XstrVALUE), KOS_NULL));     \
} while (0)

#ifdef KOS_EXTERNAL_MODULES
#   define KOS_INIT_MODULE(name, flags) \
        KOS_EXTERN_C KOS_EXPORT_SYMBOL unsigned get_kos_module_flags(void) { return (flags); } \
        KOS_EXTERN_C KOS_EXPORT_SYMBOL int init_kos_module
#else
#   define KOS_INIT_MODULE(name, flags) int kos_module_##name##_init
#endif

#ifdef _WIN32
    /* Redirect debug messages to stderr */
#   define KOS_init_debug_output() do {                  \
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);   \
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR); \
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);   \
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR); \
} while (0)
#else
#   define KOS_init_debug_output() ((void)0)
#endif

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_MODULE_H_INCLUDED
#define KOS_MODULE_H_INCLUDED

#include "kos_api.h"
#include "kos_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

KOS_API
int KOS_load_module(KOS_CONTEXT ctx,
                    const char *path,
                    unsigned    path_len);

KOS_API
int KOS_load_module_from_memory(KOS_CONTEXT ctx,
                                const char *module_name,
                                unsigned    module_name_len,
                                const char *buf,
                                unsigned    buf_size);

KOS_API
KOS_OBJ_ID KOS_repl(KOS_CONTEXT ctx,
                    const char *module_name,
                    const char *buf,
                    unsigned    buf_size);

KOS_API
KOS_OBJ_ID KOS_repl_stdin(KOS_CONTEXT ctx,
                          const char *module_name);

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
                            int                  min_args,
                            KOS_FUNCTION_STATE   gen_state);

KOS_API
int KOS_module_add_constructor(KOS_CONTEXT          ctx,
                               KOS_OBJ_ID           module_obj,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER handler,
                               int                  min_args,
                               KOS_OBJ_ID          *ret_proto);

KOS_API
int KOS_module_add_member_function(KOS_CONTEXT          ctx,
                                   KOS_OBJ_ID           module_obj,
                                   KOS_OBJ_ID           proto_obj,
                                   KOS_OBJ_ID           str_name,
                                   KOS_FUNCTION_HANDLER handler,
                                   int                  min_args,
                                   KOS_FUNCTION_STATE   gen_state);

KOS_API
unsigned KOS_module_addr_to_line(KOS_MODULE *module,
                                 uint32_t    offs);

KOS_API
unsigned KOS_module_addr_to_func_line(KOS_MODULE *module,
                                      uint32_t    offs);

KOS_API
KOS_OBJ_ID KOS_module_addr_to_func_name(KOS_CONTEXT ctx,
                                        KOS_MODULE *module,
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

#define TRY_ADD_GLOBAL(ctx, module, name, value)                                     \
do {                                                                                 \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                                 \
    TRY(KOS_module_add_global((ctx), (module), KOS_CONST_ID(str_name), (value), 0)); \
} while (0)

#define TRY_ADD_FUNCTION(ctx, module, name, handler, min_args)           \
do {                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                     \
    TRY(KOS_module_add_function((ctx), (module), KOS_CONST_ID(str_name), \
                                (handler), (min_args), KOS_FUN));        \
} while (0)

#define TRY_ADD_GENERATOR(ctx, module, name, handler, min_args)          \
do {                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                     \
    TRY(KOS_module_add_function((ctx), (module), KOS_CONST_ID(str_name), \
                                (handler), (min_args), KOS_GEN_INIT));   \
} while (0)

#define TRY_ADD_CONSTRUCTOR(ctx, module, name, handler, min_args, ret_proto) \
do {                                                                         \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                         \
    TRY(KOS_module_add_constructor((ctx), (module), KOS_CONST_ID(str_name),  \
                                   (handler), (min_args), (ret_proto)));     \
} while (0)

#define TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, name, handler, min_args)             \
do {                                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                                     \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), KOS_CONST_ID(str_name), \
                                       (handler), (min_args), KOS_FUN));                 \
} while (0)

#define TRY_ADD_MEMBER_GENERATOR(ctx, module, proto, name, handler, min_args)            \
do {                                                                                     \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                                     \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), KOS_CONST_ID(str_name), \
                                       (handler), (min_args), KOS_GEN_INIT));            \
} while (0)

#define TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, name, getter, setter)        \
do {                                                                             \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                             \
    TRY(KOS_set_builtin_dynamic_property((ctx), (proto), KOS_CONST_ID(str_name), \
                                         (module), (getter), (setter)));         \
} while (0)

#define TRY_ADD_INTEGER_CONSTANT(ctx, module, name, value)             \
do {                                                                   \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                   \
    TRY(KOS_module_add_global((ctx), (module), KOS_CONST_ID(str_name), \
                              TO_SMALL_INT((int)(value)), 0));         \
} while (0)

#ifdef KOS_EXTERNAL_MODULES
#   define KOS_INIT_MODULE(name) KOS_EXTERN_C KOS_EXPORT_SYMBOL int init_kos_module
#else
#   define KOS_INIT_MODULE(name) int kos_module_##name##_init
#endif

#endif

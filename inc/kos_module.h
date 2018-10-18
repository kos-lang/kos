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

#ifndef __KOS_MODULE_H
#define __KOS_MODULE_H

#include "kos_object_base.h"

#ifdef __cplusplus
extern "C" {
#endif

int KOS_load_module(KOS_CONTEXT ctx,
                    const char *path);

int KOS_load_module_from_memory(KOS_CONTEXT ctx,
                                const char *module_name,
                                const char *buf,
                                unsigned    buf_size);

KOS_OBJ_ID KOS_repl(KOS_CONTEXT ctx,
                    const char *module_name,
                    const char *buf,
                    unsigned    buf_size);

KOS_OBJ_ID KOS_repl_stdin(KOS_CONTEXT ctx,
                          const char *module_name);

KOS_OBJ_ID KOS_get_module(KOS_CONTEXT ctx);

int KOS_module_add_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID  value,
                          unsigned   *idx);

int KOS_module_get_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID *value,
                          unsigned   *idx);

int KOS_module_add_function(KOS_CONTEXT              ctx,
                            KOS_OBJ_ID               module_obj,
                            KOS_OBJ_ID               str_name,
                            KOS_FUNCTION_HANDLER     handler,
                            int                      min_args,
                            enum _KOS_FUNCTION_STATE gen_state);

int KOS_module_add_constructor(KOS_CONTEXT          ctx,
                               KOS_OBJ_ID           module_obj,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER handler,
                               int                  min_args,
                               KOS_OBJ_ID          *ret_proto);

int KOS_module_add_member_function(KOS_CONTEXT              ctx,
                                   KOS_OBJ_ID               module_obj,
                                   KOS_OBJ_ID               proto_obj,
                                   KOS_OBJ_ID               str_name,
                                   KOS_FUNCTION_HANDLER     handler,
                                   int                      min_args,
                                   enum _KOS_FUNCTION_STATE gen_state);

unsigned KOS_module_addr_to_line(KOS_MODULE *module,
                                 uint32_t    offs);

unsigned KOS_module_addr_to_func_line(KOS_MODULE *module,
                                      uint32_t    offs);

KOS_OBJ_ID KOS_module_addr_to_func_name(KOS_MODULE *module,
                                        uint32_t    offs);

uint32_t KOS_module_func_get_num_instr(KOS_MODULE *module,
                                       uint32_t    offs);

uint32_t KOS_module_func_get_code_size(KOS_MODULE *module,
                                       uint32_t    offs);

#ifdef __cplusplus
}
#endif

#define TRY_ADD_FUNCTION(ctx, module, name, handler, min_args)                          \
do {                                                                                    \
    static const char str_name[] = name;                                                \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,          \
                                                              sizeof(str_name) - 1);    \
    TRY_OBJID(str);                                                                     \
    TRY(KOS_module_add_function((ctx), (module), str, (handler), (min_args), KOS_FUN)); \
} while (0)

#define TRY_ADD_GENERATOR(ctx, module, name, handler, min_args)                              \
do {                                                                                         \
    static const char str_name[] = name;                                                     \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,               \
                                                              sizeof(str_name) - 1);         \
    TRY_OBJID(str);                                                                          \
    TRY(KOS_module_add_function((ctx), (module), str, (handler), (min_args), KOS_GEN_INIT)); \
} while (0)

#define TRY_ADD_CONSTRUCTOR(ctx, module, name, handler, min_args, ret_proto)                   \
do {                                                                                           \
    static const char str_name[] = name;                                                       \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,                 \
                                                              sizeof(str_name) - 1);           \
    TRY_OBJID(str);                                                                            \
    TRY(KOS_module_add_constructor((ctx), (module), str, (handler), (min_args), (ret_proto))); \
} while (0)

#define TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, name, handler, min_args)                            \
do {                                                                                                    \
    static const char str_name[] = name;                                                                \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,                          \
                                                              sizeof(str_name) - 1);                    \
    TRY_OBJID(str);                                                                                     \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), str, (handler), (min_args), KOS_FUN)); \
} while (0)

#define TRY_ADD_MEMBER_GENERATOR(ctx, module, proto, name, handler, min_args)                                \
do {                                                                                                         \
    static const char str_name[] = name;                                                                     \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,                               \
                                                              sizeof(str_name) - 1);                         \
    TRY_OBJID(str);                                                                                          \
    TRY(KOS_module_add_member_function((ctx), (module), (proto), str, (handler), (min_args), KOS_GEN_INIT)); \
} while (0)

#define TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, name, getter, setter)                     \
do {                                                                                          \
    static const char str_name[] = name;                                                      \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,                \
                                                              sizeof(str_name) - 1);          \
    TRY_OBJID(str);                                                                           \
    TRY(KOS_set_builtin_dynamic_property((ctx), (proto), str, (module), (getter), (setter))); \
} while (0)

#define TRY_ADD_INTEGER_CONSTANT(ctx, module, name, value)                           \
do {                                                                                 \
    static const char str_name[] = name;                                             \
    KOS_OBJ_ID        str        = KOS_new_const_ascii_string((ctx), str_name,       \
                                                              sizeof(str_name) - 1); \
    TRY_OBJID(str);                                                                  \
    TRY(KOS_module_add_global((ctx), (module), str, TO_SMALL_INT((int)(value)), 0)); \
} while (0)

#endif

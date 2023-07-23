/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_module.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_string.h"
#include "../inc/kos_system.h"
#include "../inc/kos_utils.h"
#include "kos_compiler.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_disasm.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_parser.h"
#include "kos_perf.h"
#include "kos_try.h"
#include "kos_utf8_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#   pragma warning( disable : 4191 ) /* 'type cast': unsafe conversion from 'LIB_FUNCTION' to 'KOS_BUILTIN_INIT' */
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_cur_dir,        ".");
KOS_DECLARE_STATIC_CONST_STRING(str_eol,            "\n");
KOS_DECLARE_STATIC_CONST_STRING(str_err_internal,   "internal error");
KOS_DECLARE_STATIC_CONST_STRING(str_err_stdin,      "failed reading from stdin");
KOS_DECLARE_STATIC_CONST_STRING(str_format_colon,   ":");
KOS_DECLARE_STATIC_CONST_STRING(str_format_error,   ": error: ");
KOS_DECLARE_STATIC_CONST_STRING(str_format_warning, ": warning: ");
KOS_DECLARE_STATIC_CONST_STRING(str_global,         "<global>");
KOS_DECLARE_STATIC_CONST_STRING(str_path_sep,       KOS_PATH_SEPARATOR_STR);
KOS_DECLARE_STATIC_CONST_STRING(str_script_ext,     ".kos");

struct KOS_MODULE_LOAD_CHAIN_S {
    KOS_MODULE_LOAD_CHAIN *next;
    const char            *module_name;
    unsigned               length;
};

static unsigned rfind_path(const char *path,
                           unsigned    length,
                           char        dot)
{
    unsigned i;

    for (i = length; i > 0; i--) {
        const char c = path[i-1];
        if (c == '/' || c == '\\' || c == dot)
            break;
    }

    return i;
}

static int load_native(KOS_CONTEXT ctx, KOS_OBJ_ID module_name, KOS_VECTOR *cpath, KOS_OBJ_ID *mod_init)
{
    static const char ext[] = KOS_SHARED_LIB_EXT;
    KOS_BUILTIN_INIT  init  = KOS_NULL;
    KOS_SHARED_LIB    lib;
    KOS_VECTOR        error_cstr;
    unsigned          pos;
    unsigned          flags = KOS_MODULE_NEEDS_KOS_SOURCE;

    *mod_init = KOS_BADPTR;

    pos = rfind_path(cpath->buffer, (unsigned)cpath->size, '.');

    if (pos && (cpath->buffer[pos - 1] == '.'))
        --pos;
    else {
        assert(cpath->size);
        assert(cpath->buffer[cpath->size - 1] == 0);
        pos = (unsigned)cpath->size - 1;
    }

    if (KOS_vector_resize(cpath, pos + sizeof(ext))) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    memcpy(&cpath->buffer[pos], ext, sizeof(ext));

    /* Prepend ./ if there was no path specified */
    if ( ! memchr(cpath->buffer, KOS_PATH_SEPARATOR, cpath->size - 1)) {
        const size_t old_size = cpath->size;

        if (KOS_vector_resize(cpath, old_size + 2)) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            return KOS_ERROR_EXCEPTION;
        }

        memmove(cpath->buffer + 2, cpath->buffer, old_size);

        cpath->buffer[0] = '.';
        cpath->buffer[1] = KOS_PATH_SEPARATOR;
    }

    if ( ! KOS_does_file_exist(cpath->buffer))
        return KOS_SUCCESS;

    if (ctx->inst->flags & KOS_INST_VERBOSE)
        printf("Kos loading native code from %s\n", cpath->buffer);

    KOS_vector_init(&error_cstr);

    {
        KOS_LOCAL saved_name;

        KOS_init_local_with(ctx, &saved_name, module_name);

        KOS_suspend_context(ctx);

        lib = kos_seq_fail() ? KOS_NULL : KOS_load_library(cpath->buffer, &error_cstr);

        if (lib)
            init = kos_seq_fail() ? KOS_NULL : (KOS_BUILTIN_INIT)KOS_get_library_function(lib, "init_kos_module", &error_cstr);

        if (init) {
            const KOS_GET_FLAGS flags_fn =
                (KOS_GET_FLAGS)KOS_get_library_function(lib, "get_kos_module_flags", &error_cstr);
            if (flags_fn)
                flags = flags_fn();
        }

        KOS_resume_context(ctx);

        module_name = KOS_destroy_top_local(ctx, &saved_name);
    }

    if ( ! lib || ! init) {
        if (lib) {
            KOS_unload_library(lib);

            KOS_raise_printf(ctx, "failed to get init_kos_module function from %s: %s\n",
                             cpath->buffer, error_cstr.buffer);
        }
        else
            KOS_raise_printf(ctx, "failed to load module native code from %s: %s\n",
                             cpath->buffer, error_cstr.buffer);
        KOS_vector_destroy(&error_cstr);
        return KOS_ERROR_EXCEPTION;
    }

    KOS_vector_destroy(&error_cstr);

    *mod_init = kos_register_module_init(ctx, module_name, lib, init, flags);

    return IS_BAD_PTR(*mod_init) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int find_module(KOS_CONTEXT            ctx,
                       KOS_OBJ_ID             module_name,
                       const char            *maybe_path,
                       unsigned               length,
                       int                    is_path,
                       KOS_MODULE_LOAD_CHAIN *loading,
                       KOS_OBJ_ID            *out_abs_dir,
                       KOS_OBJ_ID            *out_abs_path,
                       KOS_OBJ_ID            *mod_init)
{
    KOS_LOCAL  path;
    KOS_LOCAL  dir;
    KOS_LOCAL  components[4];
    KOS_VECTOR cpath;
    unsigned   i;
    int        error           = KOS_ERROR_INTERNAL;
    int        native_mod_init = 1;
    int        has_dot         = 0;

    KOS_vector_init(&cpath);

    KOS_init_locals(ctx, &path, &dir, &components[0], &components[2], kos_end_locals);

    components[1].o = KOS_CONST_ID(str_path_sep);
    components[2].o = module_name;
    components[3].o = KOS_CONST_ID(str_script_ext);

    /* Try to get native module init object */
    *mod_init = KOS_get_property_shallow(ctx, ctx->inst->modules.module_inits, components[2].o);
    if (IS_BAD_PTR(*mod_init) ||
        ! ((struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, *mod_init))->init) {

        native_mod_init = 0;
        *mod_init       = KOS_BADPTR;
        KOS_clear_exception(ctx);
    }

    /* Check if module name is a path and a path is allowed */
    has_dot = rfind_path(maybe_path, length, '.') > 0;
    if (has_dot && ! is_path)
        RAISE_ERROR(KOS_ERROR_NOT_FOUND);

    /* Check if file exists */
    if (is_path) {
        TRY(KOS_vector_resize(&cpath, length+1));
        memcpy(cpath.buffer, maybe_path, length);
        cpath.buffer[length] = 0;

        if (KOS_get_absolute_path(&cpath) ||
            ! KOS_does_file_exist(cpath.buffer)) {

            /* Path was specified, but module source code on that path does not
             * exist.  Check if we can load module written in native code
             * (built-in or shared library) without source code.
             */
            if (has_dot) {
                if (native_mod_init)
                    RAISE_ERROR(KOS_SUCCESS_RETURN);

                TRY(load_native(ctx, components[2].o, &cpath, mod_init));

                if (IS_BAD_PTR(*mod_init))
                    RAISE_ERROR(KOS_ERROR_NOT_FOUND);
            }
            else
                is_path = 0;
        }
    }

    if (is_path) {

        path.o = KOS_new_string(ctx, cpath.buffer, (unsigned)(cpath.size-1));
        TRY_OBJID(path.o);

        /* Find and skip last path separator */
        i = rfind_path(cpath.buffer, (unsigned)(cpath.size-1), '/');
        if (i > 0)
            --i;

        dir.o = KOS_new_string(ctx, cpath.buffer, i);
        TRY_OBJID(dir.o);

        if ( ! native_mod_init) {
            assert(IS_BAD_PTR(*mod_init));
            TRY(load_native(ctx, components[2].o, &cpath, mod_init));
        }
    }
    else {

        KOS_INSTANCE *inst      = ctx->inst;
        uint32_t      num_paths = KOS_get_array_size(inst->modules.search_paths);

        if (!num_paths)
            RAISE_ERROR(native_mod_init ? KOS_SUCCESS_RETURN : KOS_ERROR_NOT_FOUND);

        for (i = 0; i < num_paths; i++) {
            int have_src;

            components[0].o = KOS_array_read(ctx, inst->modules.search_paths, (int)i);
            TRY_OBJID(components[0].o);

            path.o = KOS_string_add_n(ctx, components, sizeof(components)/sizeof(components[0]));
            TRY_OBJID(path.o);

            TRY(KOS_string_to_cstr_vec(ctx, path.o, &cpath));

            have_src = KOS_does_file_exist(cpath.buffer);

            dir.o = components[0].o;

            if ( ! native_mod_init) {
                assert(IS_BAD_PTR(*mod_init));

                TRY(load_native(ctx, components[2].o, &cpath, mod_init));

                if ( ! IS_BAD_PTR(*mod_init)) {
                    error = KOS_SUCCESS;
                    break;
                }
            }

            if (have_src) {
                error = KOS_SUCCESS;
                break;
            }

            dir.o = KOS_BADPTR;
        }

        if (IS_BAD_PTR(dir.o))
            error = native_mod_init ? KOS_SUCCESS_RETURN : KOS_ERROR_NOT_FOUND;
    }

cleanup:
    if (error == KOS_SUCCESS_RETURN) {
        unsigned flags;

        assert( ! IS_BAD_PTR(*mod_init));

        flags = ((struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, *mod_init))->flags;

        error = (flags & KOS_MODULE_NEEDS_KOS_SOURCE) ? KOS_ERROR_NOT_FOUND : KOS_SUCCESS;
    }

    if ( ! error) {
        *out_abs_dir  = dir.o;
        *out_abs_path = path.o;
    }

    KOS_destroy_top_locals(ctx, &path, &components[2]);

    KOS_vector_destroy(&cpath);

    assert(error != KOS_ERROR_INTERNAL);

    if (error == KOS_ERROR_NOT_FOUND) {
        KOS_raise_printf(ctx, "module \"%.*s\" not found",
                         (int)loading->length, loading->module_name);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static void get_module_name(const char            *module,
                            unsigned               length,
                            KOS_MODULE_LOAD_CHAIN *loading)
{
    unsigned i = rfind_path(module, length, '.');

    if (i > 0) {
        if (module[i-1] == '.') {
            length = --i;
            i      = rfind_path(module, i, '/');
        }
        module += i;
        length -= i;
    }

    loading->module_name = module;
    loading->length      = length;
}

static KOS_OBJ_ID alloc_module(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  module_name_obj,
                               int        *module_idx)
{
    KOS_LOCAL module_name;
    KOS_LOCAL obj;
    KOS_LOCAL module;
    uint32_t  u_idx = 0;
    int       error = KOS_SUCCESS;

    KOS_init_locals(ctx, &module_name, &obj, &module, kos_end_locals);

    module_name.o = module_name_obj;

    module.o = OBJID(MODULE, (KOS_MODULE *)
            kos_alloc_object(ctx, KOS_ALLOC_IMMOVABLE, OBJ_MODULE, sizeof(KOS_MODULE)));
    TRY_OBJID(module.o);

    {
        KOS_MODULE *const module_ptr = OBJPTR(MODULE, module.o);

        memset(((uint8_t *)module_ptr) + sizeof(module_ptr->header),
               0,
               sizeof(*module_ptr) - sizeof(module_ptr->header));
    }

    assert(READ_OBJ_TYPE(module.o) == OBJ_MODULE);

    OBJPTR(MODULE, module.o)->name         = module_name.o;
    OBJPTR(MODULE, module.o)->path         = KOS_STR_EMPTY;
    OBJPTR(MODULE, module.o)->inst         = ctx->inst;
    OBJPTR(MODULE, module.o)->constants    = KOS_BADPTR;
    OBJPTR(MODULE, module.o)->global_names = KOS_BADPTR;
    OBJPTR(MODULE, module.o)->globals      = KOS_BADPTR;
    OBJPTR(MODULE, module.o)->module_names = KOS_BADPTR;
    OBJPTR(MODULE, module.o)->priv         = KOS_BADPTR;

    obj.o = KOS_new_object(ctx);
    TRY_OBJID(obj.o);
    OBJPTR(MODULE, module.o)->global_names = obj.o;

    obj.o = KOS_new_array(ctx, 0);
    TRY_OBJID(obj.o);
    OBJPTR(MODULE, module.o)->globals = obj.o;

    obj.o = KOS_new_object(ctx);
    TRY_OBJID(obj.o);
    OBJPTR(MODULE, module.o)->module_names = obj.o;

    TRY(KOS_array_push(ctx, ctx->inst->modules.modules, KOS_VOID, &u_idx));

    *module_idx = (int)u_idx;
    assert(*module_idx >= 0);

cleanup:
    module.o = KOS_destroy_top_locals(ctx, &module_name, &module);

    return error ? KOS_BADPTR : module.o;
}

static int load_file(KOS_CONTEXT  ctx,
                     KOS_OBJ_ID   path_obj,
                     KOS_FILEBUF *file_buf,
                     unsigned     flags)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cpath;

    KOS_vector_init(&cpath);
    TRY(KOS_string_to_cstr_vec(ctx, path_obj, &cpath));

    if ( ! KOS_does_file_exist(cpath.buffer) && ! (flags & KOS_MODULE_NEEDS_KOS_SOURCE)) {
        file_buf->buffer = KOS_NULL;
        file_buf->size   = 0;
        goto cleanup;
    }

    error = KOS_load_file(cpath.buffer, file_buf);

    switch (error) {

        case KOS_ERROR_ERRNO: {

            static const char error_info[] = "unable to load file \"";
            const size_t      path_len     = cpath.size - 1;

            if ( ! KOS_vector_resize(&cpath, cpath.size + sizeof(error_info))) {
                memmove(&cpath.buffer[sizeof(error_info) - 1],
                        cpath.buffer, path_len);
                memcpy(cpath.buffer, error_info, sizeof(error_info) - 1);
                cpath.buffer[cpath.size - 2] = '"';
                cpath.buffer[cpath.size - 1] = 0;

                KOS_raise_errno(ctx, cpath.buffer);
                error = KOS_ERROR_EXCEPTION;
                break;
            }
        }

        /* fall through */

        case KOS_ERROR_OUT_OF_MEMORY:
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            error = KOS_ERROR_EXCEPTION;
            break;

        default:
            break;
    }

cleanup:
    KOS_vector_destroy(&cpath);

    return error;
}

static int predefine_globals(KOS_CONTEXT    ctx,
                             KOS_COMP_UNIT *program,
                             KOS_OBJ_ID     global_names,
                             KOS_OBJ_ID     module_names_obj)
{
    KOS_VECTOR cpath;
    KOS_LOCAL  module_names;
    KOS_LOCAL  walk;
    int        error = KOS_SUCCESS;

    KOS_vector_init(&cpath);

    KOS_init_local_with(ctx, &module_names, module_names_obj);
    KOS_init_local(ctx, &walk);

    walk.o = kos_new_object_walk(ctx, global_names, KOS_SHALLOW);
    TRY_OBJID(walk.o);

    while ( ! kos_object_walk(ctx, walk.o)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk.o)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk.o), &cpath));

        TRY(kos_compiler_predefine_global(program,
                                          cpath.buffer,
                                          (uint16_t)cpath.size - 1,
                                          (int)GET_SMALL_INT(KOS_get_walk_value(walk.o))));
    }

    walk.o = kos_new_object_walk(ctx, module_names.o, KOS_SHALLOW);
    TRY_OBJID(walk.o);

    while ( ! kos_object_walk(ctx, walk.o)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk.o)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk.o), &cpath));

        TRY(kos_compiler_predefine_module(program, cpath.buffer, (uint16_t)cpath.size - 1,
                                          (int)GET_SMALL_INT(KOS_get_walk_value(walk.o))));
    }

cleanup:
    KOS_vector_destroy(&cpath);

    KOS_destroy_top_locals(ctx, &walk, &module_names);

    return error;
}

static int alloc_globals(KOS_CONTEXT    ctx,
                         KOS_COMP_UNIT *program,
                         KOS_OBJ_ID     module_obj)
{
    int       error;
    KOS_VAR  *var;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY(KOS_array_resize(ctx,
                         OBJPTR(MODULE, module.o)->globals,
                         (uint32_t)program->num_globals));

    for (var = program->globals; var; var = var->next) {

        if (var->type == VAR_GLOBAL) {

            KOS_OBJ_ID name;

            name = KOS_new_string(ctx, var->token->begin, var->token->length);
            TRY_OBJID(name);

            assert(var->array_idx < program->num_globals);
            TRY(KOS_set_property(ctx,
                                 OBJPTR(MODULE, module.o)->global_names,
                                 name,
                                 TO_SMALL_INT(var->array_idx)));
        }
    }

cleanup:
    KOS_destroy_top_local(ctx, &module);
    return error;
}

static int save_direct_modules(KOS_CONTEXT    ctx,
                               KOS_COMP_UNIT *program,
                               KOS_OBJ_ID     module_obj)
{
    int                 error  = KOS_SUCCESS;
    KOS_VAR            *var;
    KOS_INSTANCE *const inst   = ctx->inst;
    KOS_LOCAL           module;

    KOS_init_local_with(ctx, &module, module_obj);

    for (var = program->modules; var; var = var->next) {

        KOS_OBJ_ID name;
        KOS_OBJ_ID module_idx_obj;

        name = KOS_new_string(ctx, var->token->begin, var->token->length);
        TRY_OBJID(name);

        module_idx_obj = KOS_get_property_shallow(ctx, inst->modules.module_names, name);
        TRY_OBJID(module_idx_obj);

        assert(IS_SMALL_INT(module_idx_obj));

        TRY(KOS_set_property(ctx, OBJPTR(MODULE, module.o)->module_names, name, module_idx_obj));
    }

cleanup:
    KOS_destroy_top_local(ctx, &module);
    return error;
}

typedef struct KOS_PRINT_CONST_COOKIE_S {
    KOS_CONTEXT ctx;
    KOS_OBJ_ID  constants;
} KOS_PRINT_CONST_COOKIE;

static int print_const(void       *cookie,
                       KOS_VECTOR *cstr_buf,
                       uint32_t    const_index)
{
    KOS_PRINT_CONST_COOKIE *data = (KOS_PRINT_CONST_COOKIE *)cookie;
    KOS_OBJ_ID              constant;
    int                     error;

    constant = KOS_array_read(data->ctx, data->constants, const_index);

    if (IS_BAD_PTR(constant)) {
        KOS_clear_exception(data->ctx);
        return KOS_ERROR_INVALID_NUMBER;
    }

    error = KOS_object_to_string_or_cstr_vec(data->ctx,
                                             constant,
                                             KOS_QUOTE_STRINGS,
                                             KOS_NULL,
                                             cstr_buf);

    if (error) {
        KOS_clear_exception(data->ctx);
        return KOS_ERROR_INVALID_NUMBER;
    }

    return KOS_SUCCESS;
}

static const char *lalign(const char *tag)
{
    static const char align[] = "            ";

    const size_t tag_len    = strlen(tag);
    const size_t max_align  = sizeof(align) - 1U;
    const size_t align_size = (tag_len > max_align) ? 0U : (max_align - tag_len);

    return &align[max_align - align_size];
}

static void print_regs(unsigned reg_idx, unsigned num_regs, const char *tag)
{
    if (reg_idx == KOS_NO_REG || ! num_regs)
        return;

    assert(num_regs <= KOS_NO_REG);

    if (num_regs == 1)
        printf("# %s%s : r%u\n", tag, lalign(tag), reg_idx);
    else
        printf("# %s%s : r%u..r%u\n", tag, lalign(tag), reg_idx, reg_idx + num_regs - 1);
}

static int disassemble_func(KOS_CONTEXT ctx, KOS_OBJ_ID func_obj)
{
    KOS_VECTOR             module_path_cstr;
    KOS_VECTOR             module_name_cstr;
    KOS_VECTOR             func_name_cstr;
    KOS_LOCAL              func;
    KOS_PRINT_CONST_COOKIE print_cookie;
    const char            *filename;
    const KOS_BYTECODE    *bytecode;
    KOS_OBJ_ID             bytecode_obj;
    KOS_OBJ_ID             module;
    KOS_FUNCTION_STATE     state;
    unsigned               num_args;
    int                    error;
    static const char      divider[]  =
                "==============================================================================";

    KOS_init_local_with(ctx, &func, func_obj);
    KOS_vector_init(&module_path_cstr);
    KOS_vector_init(&module_name_cstr);
    KOS_vector_init(&func_name_cstr);

    assert( ! IS_BAD_PTR(func.o));
    bytecode_obj = OBJPTR(FUNCTION, func.o)->bytecode;

    assert( ! IS_SMALL_INT(bytecode_obj));
    assert( ! IS_BAD_PTR(bytecode_obj));
    bytecode = (const KOS_BYTECODE *)OBJPTR(OPAQUE, bytecode_obj);

    module = OBJPTR(FUNCTION, func.o)->module;
    assert( ! IS_BAD_PTR(module));

    TRY(KOS_string_to_cstr_vec(ctx, OBJPTR(MODULE, module)->name, &module_name_cstr));
    TRY(KOS_string_to_cstr_vec(ctx, OBJPTR(FUNCTION, func.o)->name, &func_name_cstr));

    if (GET_OBJ_TYPE(OBJPTR(MODULE, module)->path) == OBJ_STRING &&
        KOS_get_string_length(OBJPTR(MODULE, module)->path)) {

        TRY(KOS_string_to_cstr_vec(ctx, OBJPTR(MODULE, module)->path, &module_path_cstr));
        assert(module_path_cstr.size);

        filename = module_path_cstr.buffer + module_path_cstr.size - 1;
        assert(*filename == 0);
        while (filename > module_path_cstr.buffer) {
            if (*filename == KOS_PATH_SEPARATOR) {
                ++filename;
                break;
            }
            --filename;
        }
    }
    else
        filename = module_name_cstr.buffer;

    state = (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state);
    printf("\n%s\nDisassembling %s %s.%s\n%s\n",
           divider,
           state == KOS_FUN  ? "function" :
           state == KOS_CTOR ? "class"    :
                               "generator",
           module_name_cstr.buffer,
           func_name_cstr.buffer,
           divider);

    printf("# registers    : %u\n", OBJPTR(FUNCTION, func.o)->opts.num_regs);
    printf("# closure size : %u\n", OBJPTR(FUNCTION, func.o)->opts.closure_size);
    printf("# minimum args : %u\n", OBJPTR(FUNCTION, func.o)->opts.min_args);
    printf("# default args : %u\n", OBJPTR(FUNCTION, func.o)->opts.num_def_args);

    num_args = KOS_min((unsigned)OBJPTR(FUNCTION, func.o)->opts.rest_reg,
                       (unsigned)OBJPTR(FUNCTION, func.o)->opts.args_reg +
                            (unsigned)OBJPTR(FUNCTION, func.o)->opts.min_args +
                            (unsigned)OBJPTR(FUNCTION, func.o)->opts.num_def_args);

    print_regs(OBJPTR(FUNCTION, func.o)->opts.args_reg, num_args, "args regs");
    print_regs(OBJPTR(FUNCTION, func.o)->opts.rest_reg, 1, "rest reg");
    print_regs(OBJPTR(FUNCTION, func.o)->opts.ellipsis_reg, 1, "ellipsis reg");
    print_regs(OBJPTR(FUNCTION, func.o)->opts.this_reg, 1, "this reg");
    print_regs(OBJPTR(FUNCTION, func.o)->opts.bind_reg, OBJPTR(FUNCTION, func.o)->opts.num_binds, "binds regs");

    print_cookie.ctx       = ctx;
    print_cookie.constants = OBJPTR(MODULE, module)->constants;

    TRY(kos_disassemble(filename,
                        bytecode->bytecode,
                        bytecode->bytecode_size,
                        (const KOS_LINE_ADDR *)&bytecode->bytecode[bytecode->addr2line_offset],
                        (const KOS_LINE_ADDR *)&bytecode->bytecode[bytecode->addr2line_offset
                                                                 + bytecode->addr2line_size],
                        print_const,
                        &print_cookie));

cleanup:
    KOS_vector_destroy(&func_name_cstr);
    KOS_vector_destroy(&module_name_cstr);
    KOS_vector_destroy(&module_path_cstr);
    KOS_destroy_top_local(ctx, &func);

    return error;
}

KOS_OBJ_ID kos_alloc_bytecode(KOS_CONTEXT ctx,
                              const void *bytecode,
                              uint32_t    bytecode_size,
                              const void *addr2line,
                              uint32_t    addr2line_size)
{
    const uint32_t aligned_bytecode_size = KOS_align_up(bytecode_size,
                                                        (uint32_t)sizeof(struct KOS_COMP_ADDR_TO_LINE_S));
    const uint32_t total_size = aligned_bytecode_size + addr2line_size;
    const uint32_t alloc_size = (uint32_t)sizeof(KOS_BYTECODE) + total_size - 1;

    KOS_BYTECODE *const bytecode_obj = (KOS_BYTECODE *)
        kos_alloc_object(ctx, KOS_ALLOC_IMMOVABLE, OBJ_OPAQUE, alloc_size);

    if (bytecode_obj) {
        bytecode_obj->bytecode_size    = bytecode_size;
        bytecode_obj->addr2line_offset = aligned_bytecode_size;
        bytecode_obj->addr2line_size   = addr2line_size;
        bytecode_obj->def_line         = 0;
        bytecode_obj->num_instr        = 0;

        memcpy(bytecode_obj->bytecode, bytecode, bytecode_size);

        if (addr2line_size)
            memcpy(&bytecode_obj->bytecode[aligned_bytecode_size], addr2line, addr2line_size);
    }

    return OBJID(OPAQUE, (KOS_OPAQUE *)bytecode_obj);
}

static KOS_OBJ_ID alloc_bytecode(KOS_CONTEXT        ctx,
                                 KOS_COMP_UNIT     *program,
                                 KOS_COMP_FUNCTION *func_const)
{
    assert(func_const->bytecode_offset + func_const->bytecode_size <= program->code_buf.size);
    if (func_const->addr2line_size) {
        assert(func_const->addr2line_offset + func_const->addr2line_size <= program->addr2line_buf.size);
    }

    return kos_alloc_bytecode(ctx,
                              &program->code_buf.buffer[func_const->bytecode_offset],
                              func_const->bytecode_size,
                              &program->addr2line_buf.buffer[func_const->addr2line_offset],
                              func_const->addr2line_size);
}

static uint32_t count_constants(KOS_COMP_UNIT *program)
{
    uint32_t i;

    KOS_COMP_CONST *str = program->first_constant;

    for (i = 0; str; str = str->next, ++i);

    return i;
}

static int alloc_constants(KOS_CONTEXT    ctx,
                           KOS_COMP_UNIT *program,
                           KOS_OBJ_ID     module_obj)
{
    int             error         = KOS_SUCCESS;
    const uint32_t  num_constants = count_constants(program);
    uint32_t        base_idx      = 0;
    KOS_COMP_CONST *constant      = program->first_constant;
    KOS_LOCAL       module;
    KOS_LOCAL       obj;
    int             i;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &obj);

    if (IS_BAD_PTR(OBJPTR(MODULE, module.o)->constants)) {
        const KOS_OBJ_ID constants = KOS_new_array(ctx, num_constants);

        TRY_OBJID(constants);
        OBJPTR(MODULE, module.o)->constants = constants;
    }
    else {
        base_idx = KOS_get_array_size(OBJPTR(MODULE, module.o)->constants);
        TRY(KOS_array_resize(ctx, OBJPTR(MODULE, module.o)->constants, base_idx + num_constants));
    }

    for (i = 0; constant; constant = constant->next, ++i) {

        obj.o = KOS_BADPTR;

        switch (constant->type) {

            default:
                assert(constant->type == KOS_COMP_CONST_INTEGER);
                obj.o = KOS_new_int(ctx, ((KOS_COMP_INTEGER *)constant)->value);
                break;

            case KOS_COMP_CONST_FLOAT:
                obj.o = KOS_new_float(ctx, ((KOS_COMP_FLOAT *)constant)->value);
                break;

            case KOS_COMP_CONST_STRING: {
                KOS_COMP_STRING *str = (KOS_COMP_STRING *)constant;

                obj.o = str->escape == KOS_UTF8_WITH_ESCAPE
                       ? KOS_new_string_esc(ctx, str->str, str->length)
                       : KOS_new_string(ctx, str->str, str->length);
                break;
            }

            /* Functions handled separately in the subsequent loop,
             * so that all other constants are resolved before disassembly. */
            case KOS_COMP_CONST_FUNCTION:
                continue;

            case KOS_COMP_CONST_PROTOTYPE:
                obj.o = KOS_new_object(ctx);
                break;
        }

        TRY_OBJID(obj.o);

        TRY(KOS_array_write(ctx, OBJPTR(MODULE, module.o)->constants, base_idx + i, obj.o));
    }

    for (i = 0, constant = program->first_constant; constant; constant = constant->next, ++i) {

        KOS_COMP_FUNCTION *func_const;
        KOS_OBJ_ID         name;
        uint8_t            arg_idx;

        if (constant->type != KOS_COMP_CONST_FUNCTION)
            continue;

        func_const = (KOS_COMP_FUNCTION *)constant;

        if (func_const->flags & KOS_COMP_FUN_CLASS)
            obj.o = KOS_new_class(ctx, KOS_VOID);
        else
            obj.o = KOS_new_function(ctx);
        TRY_OBJID(obj.o);

        if (func_const->flags & KOS_COMP_FUN_ELLIPSIS) {
            assert(func_const->ellipsis_reg != KOS_NO_REG);
        }
        else {
            assert(func_const->ellipsis_reg == KOS_NO_REG);
        }
        if (func_const->flags & KOS_COMP_FUN_CLOSURE) {
            assert(func_const->closure_size);
        }
        else {
            assert( ! func_const->closure_size);
        }

        OBJPTR(FUNCTION, obj.o)->opts.num_regs     = func_const->num_regs;
        OBJPTR(FUNCTION, obj.o)->opts.closure_size = func_const->closure_size;
        OBJPTR(FUNCTION, obj.o)->opts.min_args     = func_const->min_args;
        OBJPTR(FUNCTION, obj.o)->opts.num_def_args = func_const->num_used_def_args;
        OBJPTR(FUNCTION, obj.o)->opts.num_binds    = func_const->num_binds;
        OBJPTR(FUNCTION, obj.o)->opts.args_reg     = func_const->args_reg;
        OBJPTR(FUNCTION, obj.o)->opts.rest_reg     = func_const->rest_reg;
        OBJPTR(FUNCTION, obj.o)->opts.ellipsis_reg = func_const->ellipsis_reg;
        OBJPTR(FUNCTION, obj.o)->opts.this_reg     = func_const->this_reg;
        OBJPTR(FUNCTION, obj.o)->opts.bind_reg     = func_const->bind_reg;
        OBJPTR(FUNCTION, obj.o)->module            = module.o;

        {
            const KOS_OBJ_ID bytecode = alloc_bytecode(ctx, program, func_const);
            TRY_OBJID(bytecode);
            OBJPTR(FUNCTION, obj.o)->bytecode = bytecode;

            ((KOS_BYTECODE *)OBJPTR(OPAQUE, bytecode))->def_line  = func_const->def_line;
            ((KOS_BYTECODE *)OBJPTR(OPAQUE, bytecode))->num_instr = func_const->num_instr;
        }

        name = KOS_array_read(ctx, OBJPTR(MODULE, module.o)->constants, (int)func_const->name_str_idx);
        TRY_OBJID(name);
        assert(GET_OBJ_TYPE(name) == OBJ_STRING);
        OBJPTR(FUNCTION, obj.o)->name = name;

        if (func_const->num_named_args) {
            KOS_OBJ_ID arg_map = KOS_new_object(ctx);
            TRY_OBJID(arg_map);

            OBJPTR(FUNCTION, obj.o)->arg_map = arg_map;
        }

        for (arg_idx = 0; arg_idx < func_const->num_named_args; arg_idx++) {
            const uint32_t str_idx = func_const->arg_name_str_idx[arg_idx];

            name = KOS_array_read(ctx, OBJPTR(MODULE, module.o)->constants, (int)str_idx);
            TRY_OBJID(name);
            assert(GET_OBJ_TYPE(name) == OBJ_STRING);

            TRY(KOS_set_property(ctx,
                                 OBJPTR(FUNCTION, obj.o)->arg_map,
                                 name,
                                 TO_SMALL_INT((int64_t)arg_idx)));
        }

        if (func_const->flags & KOS_COMP_FUN_GENERATOR)
            OBJPTR(FUNCTION, obj.o)->state = KOS_GEN_INIT;

        TRY_OBJID(obj.o);

        TRY(KOS_array_write(ctx, OBJPTR(MODULE, module.o)->constants, base_idx + i, obj.o));
    }

    if (ctx->inst->flags & KOS_INST_DISASM) {
        for (i = 0, constant = program->first_constant; constant; constant = constant->next, ++i) {

            if (constant->type != KOS_COMP_CONST_FUNCTION)
                continue;

            obj.o = KOS_array_read(ctx, OBJPTR(MODULE, module.o)->constants, base_idx + i);
            TRY_OBJID(obj.o);

            TRY(disassemble_func(ctx, obj.o));
        }
    }


cleanup:
    KOS_destroy_top_locals(ctx, &obj, &module);
    return error;
}

static KOS_OBJ_ID get_line(KOS_CONTEXT ctx,
                           const char *buf,
                           unsigned    buf_size,
                           unsigned    line)
{
    const char *const end = buf + buf_size;
    const char       *begin;
    unsigned          len       = 0;
    KOS_VECTOR        line_buf;
    KOS_OBJ_ID        ret       = KOS_BADPTR;

    KOS_vector_init(&line_buf);

    /* Find the desired line */
    for ( ; line > 1 && buf < end; line--) {
        for ( ; buf < end; buf++) {

            char c = *buf;

            if (c == '\r') {
                buf++;
                if (buf < end) {
                    c = *buf;
                    if (c == '\n')
                        buf++;
                }
                break;
            }
            else if (c == '\n') {
                buf++;
                break;
            }
        }
    }

    /* Count number of characters in the line */
    begin = buf;
    for ( ; buf < end; buf++) {

        const char c = *buf;

        if (c == '\r' || c == '\n')
            break;

        if (c == '\t')
            len = (len + 8U) & ~7U;
        else
            ++len;
    }

    /* Copy line, expanding TABs */
    if ( ! KOS_vector_resize(&line_buf, len)) {

        unsigned dest = 0;

        memset(line_buf.buffer, ' ', len);

        for ( ; begin < buf; begin++) {

            const char c = *begin;

            assert(dest < len);

            if (c == '\t')
                dest = (dest + 8U) & ~7U;
            else
                line_buf.buffer[dest++] = c;
        }

        ret = KOS_new_string(ctx, line_buf.buffer, len);
    }
    else
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    KOS_vector_destroy(&line_buf);

    return ret;
}

struct KOS_COMP_CTX {
    KOS_CONTEXT ctx;
    KOS_LOCAL   module;
    const char *data;
    unsigned    data_size;
};

enum KOS_ERROR_OR_WARNING {
    KOS_WARNING,
    KOS_ERROR
};

static KOS_OBJ_ID format_error(struct KOS_COMP_CTX      *comp_ctx,
                               enum KOS_ERROR_OR_WARNING is_error,
                               const char               *error_str,
                               KOS_FILE_POS              pos)
{
    KOS_CONTEXT ctx   = comp_ctx->ctx;
    KOS_OBJ_ID  ret   = KOS_BADPTR;
    KOS_VECTOR  cstr;
    KOS_LOCAL   parts[11];
    int         error = KOS_SUCCESS;

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, &parts[0], &parts[2], &parts[4], &parts[6], &parts[8], &parts[10],
                    kos_end_locals);

    parts[0].o = KOS_get_file_name(ctx, OBJPTR(MODULE, comp_ctx->module.o)->path);
    TRY_OBJID(parts[0].o);
    if (KOS_get_string_length(parts[0].o) == 0)
        parts[0].o = OBJPTR(MODULE, comp_ctx->module.o)->name;

    parts[1].o = KOS_CONST_ID(str_format_colon);

    parts[2].o = KOS_object_to_string(ctx, TO_SMALL_INT((int)pos.line));
    TRY_OBJID(parts[2].o);

    parts[3].o = KOS_CONST_ID(str_format_colon);

    parts[4].o = KOS_object_to_string(ctx, TO_SMALL_INT((int)pos.column));
    TRY_OBJID(parts[4].o);

    parts[5].o = is_error ? KOS_CONST_ID(str_format_error) : KOS_CONST_ID(str_format_warning);

    parts[6].o = KOS_new_const_ascii_cstring(ctx, error_str);
    TRY_OBJID(parts[6].o);

    parts[7].o = KOS_CONST_ID(str_eol);

    parts[8].o = get_line(ctx, comp_ctx->data, comp_ctx->data_size, pos.line);
    TRY_OBJID(parts[8].o);

    parts[9].o = KOS_CONST_ID(str_eol);

    error = KOS_vector_resize(&cstr, pos.column);
    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        goto cleanup;
    }

    memset(cstr.buffer, ' ', pos.column-1);
    cstr.buffer[pos.column-1] = '^';

    parts[10].o = KOS_new_string(ctx, cstr.buffer, pos.column);
    TRY_OBJID(parts[10].o);

    ret = KOS_string_add_n(ctx, parts, sizeof(parts)/sizeof(parts[0]));

cleanup:
    if (error)
        ret = KOS_BADPTR;

    KOS_destroy_top_locals(ctx, &parts[0], &parts[10]);

    KOS_vector_destroy(&cstr);

    return ret;
}

int kos_comp_resolve_global(void                          *vframe,
                            int                            module_idx,
                            const char                    *name,
                            uint16_t                       length,
                            KOS_COMP_WALK_GLOBALS_CALLBACK callback,
                            void                          *cookie)
{
    KOS_LOCAL            str;
    struct KOS_COMP_CTX *comp_ctx = (struct KOS_COMP_CTX *)vframe;
    KOS_CONTEXT          ctx      = comp_ctx->ctx;
    KOS_INSTANCE        *inst     = ctx->inst;
    KOS_OBJ_ID           module_obj;
    KOS_OBJ_ID           glob_idx_obj;
    int                  error    = KOS_SUCCESS;

    assert(module_idx >= 0);

    KOS_init_local(ctx, &str);

    str.o = KOS_new_const_ascii_string(ctx, name, length);
    TRY_OBJID(str.o);

    module_obj = KOS_array_read(ctx, inst->modules.modules, module_idx);
    TRY_OBJID(module_obj);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    glob_idx_obj = KOS_get_property_shallow(ctx, OBJPTR(MODULE, module_obj)->global_names, str.o);
    TRY_OBJID(glob_idx_obj);

    assert(IS_SMALL_INT(glob_idx_obj));

    TRY(callback(name,
                 length,
                 module_idx,
                 (int)GET_SMALL_INT(glob_idx_obj),
                 cookie));

cleanup:
    KOS_destroy_top_local(ctx, &str);

    if (error == KOS_ERROR_EXCEPTION) {
        error = (KOS_get_exception(ctx) == KOS_STR_OUT_OF_MEMORY) ? KOS_ERROR_OUT_OF_MEMORY : KOS_ERROR_NOT_FOUND;
        KOS_clear_exception(ctx);
    }

    return error;
}

int kos_comp_walk_globals(void                          *vframe,
                          int                            module_idx,
                          KOS_COMP_WALK_GLOBALS_CALLBACK callback,
                          void                          *cookie)
{
    struct KOS_COMP_CTX *comp_ctx = (struct KOS_COMP_CTX *)vframe;
    KOS_CONTEXT          ctx      = comp_ctx->ctx;
    KOS_INSTANCE *const  inst     = ctx->inst;
    KOS_VECTOR           name;
    KOS_LOCAL            walk;
    KOS_OBJ_ID           module_obj;
    int                  error    = KOS_SUCCESS;

    KOS_vector_init(&name);

    KOS_init_local(ctx, &walk);

    module_obj = KOS_array_read(ctx, inst->modules.modules, module_idx);
    TRY_OBJID(module_obj);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    walk.o = kos_new_object_walk(ctx, OBJPTR(MODULE, module_obj)->global_names, KOS_SHALLOW);
    TRY_OBJID(walk.o);

    while ( ! kos_object_walk(ctx, walk.o)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk.o)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk.o), &name));

        TRY(callback(name.buffer,
                     (uint16_t)name.size - 1U,
                     module_idx,
                     (int)GET_SMALL_INT(KOS_get_walk_value(walk.o)),
                     cookie));
    }

cleanup:
    KOS_destroy_top_local(ctx, &walk);
    KOS_vector_destroy(&name);

    if (error == KOS_ERROR_EXCEPTION) {
        error = (KOS_get_exception(ctx) == KOS_STR_OUT_OF_MEMORY) ? KOS_ERROR_OUT_OF_MEMORY : KOS_ERROR_NOT_FOUND;
        KOS_clear_exception(ctx);
    }

    return error;
}

int kos_comp_check_private_global(void            *vframe,
                                  const KOS_TOKEN *token)
{
    static const char main_str[] = "main";

    struct KOS_COMP_CTX *const comp_ctx = (struct KOS_COMP_CTX *)vframe;
    KOS_OBJ_ID                 err_str;
    KOS_VECTOR                 err_cstr;
    int                        error    = KOS_SUCCESS;

    KOS_vector_init(&err_cstr);

    if (sizeof(main_str) - 1 != token->length || memcmp(token->begin, main_str, token->length) != 0)
        goto cleanup;

    err_str = format_error(comp_ctx,
                           KOS_WARNING,
                           "main function is not declared as public",
                           get_token_pos(token));
    TRY_OBJID(err_str);

    TRY(KOS_string_to_cstr_vec(comp_ctx->ctx, err_str, &err_cstr));

    printf("%s\n", err_cstr.buffer);

cleanup:
    KOS_vector_destroy(&err_cstr);

    return error;
}

static void print_search_paths(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  paths)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;
    uint32_t   num_paths;
    uint32_t   i;

    static const char str_paths[] = "Kos module search paths: ";

    assert(GET_OBJ_TYPE(paths) == OBJ_ARRAY);

    KOS_vector_init(&cstr);

    TRY(KOS_vector_reserve(&cstr, 128));

    TRY(KOS_vector_resize(&cstr, sizeof(str_paths)));

    memcpy(cstr.buffer, str_paths, sizeof(str_paths));

    num_paths = KOS_get_array_size(paths);

    for (i = 0; i < num_paths; i++) {

        static const char str_comma[] = ", ";

        KOS_OBJ_ID path = KOS_array_read(ctx, paths, (int)i);
        TRY_OBJID(path);

        assert(GET_OBJ_TYPE(path) == OBJ_STRING);

        TRY(KOS_object_to_string_or_cstr_vec(ctx, path, KOS_DONT_QUOTE, KOS_NULL, &cstr));

        if (i + 1 < num_paths) {
            TRY(KOS_vector_resize(&cstr, cstr.size + sizeof(str_comma) - 1));
            memcpy(cstr.buffer + cstr.size - sizeof(str_comma), str_comma, sizeof(str_comma));
        }
    }

cleanup:
    if (error) {
        KOS_clear_exception(ctx);
        printf("%sout of memory\n", str_paths);
    }
    else
        printf("%s\n", cstr.buffer);

    KOS_vector_destroy(&cstr);
}

static void print_load_info(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  module_name,
                            KOS_OBJ_ID  module_path)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;

    static const char str_loading[] = "Kos loading module ";
    static const char str_from[]    = " from ";

    assert(GET_OBJ_TYPE(module_name) == OBJ_STRING);
    assert(GET_OBJ_TYPE(module_path) == OBJ_STRING);

    KOS_vector_init(&cstr);

    TRY(KOS_vector_reserve(&cstr, 128));

    TRY(KOS_vector_resize(&cstr, sizeof(str_loading)));

    memcpy(cstr.buffer, str_loading, sizeof(str_loading));

    TRY(KOS_object_to_string_or_cstr_vec(ctx, module_name, KOS_DONT_QUOTE, KOS_NULL, &cstr));

    TRY(KOS_vector_resize(&cstr, cstr.size + sizeof(str_from) - 1));
    memcpy(cstr.buffer + cstr.size - sizeof(str_from), str_from, sizeof(str_from));

    TRY(KOS_object_to_string_or_cstr_vec(ctx, module_path, KOS_DONT_QUOTE, KOS_NULL, &cstr));

cleanup:
    if (error) {
        KOS_clear_exception(ctx);
        printf("%sout of memory\n", str_loading);
    }
    else
        printf("%s\n", cstr.buffer);

    KOS_vector_destroy(&cstr);
}

static int compile_module(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          uint16_t    module_idx,
                          const char *data,
                          unsigned    data_size,
                          unsigned    flags)
{
    PROF_ZONE(MODULE)

    uint64_t            time_0;
    uint64_t            time_1;
    uint64_t            time_2;
    KOS_INSTANCE *const inst              = ctx->inst;
    KOS_AST_NODE       *ast;
    KOS_PARSER          parser;
    KOS_COMP_UNIT       program;
    struct KOS_COMP_CTX comp_ctx;
    int                 error             = KOS_SUCCESS;
    unsigned            num_opt_passes    = 0;

    time_0 = KOS_get_time_us();

    KOS_init_local_with(ctx, &comp_ctx.module, module_obj);

    comp_ctx.ctx       = ctx;
    comp_ctx.data      = data;
    comp_ctx.data_size = data_size;

    /* Initialize parser and compiler */
    kos_compiler_init(&program, module_idx);
    if ( ! IS_BAD_PTR(OBJPTR(MODULE, comp_ctx.module.o)->constants))
        program.num_constants = (int)KOS_get_array_size(OBJPTR(MODULE, comp_ctx.module.o)->constants);
    kos_parser_init(&parser,
                    &program.allocator,
                    module_idx,
                    data,
                    data + data_size);

    /* Construct AST from source code */
    error = kos_parser_parse(&parser, &ast);

    if (error == KOS_ERROR_SCANNING_FAILED ||
        error == KOS_ERROR_PARSE_FAILED) {

        const KOS_FILE_POS pos = (error == KOS_ERROR_SCANNING_FAILED)
                                 ? parser.lexer.pos : get_token_pos(&parser.token);
        KOS_OBJ_ID error_obj = format_error(&comp_ctx, KOS_ERROR, parser.error_str, pos);
        assert( ! IS_BAD_PTR(error_obj) || KOS_is_exception_pending(ctx));
        if ( ! IS_BAD_PTR(error_obj))
            KOS_raise_exception(ctx, error_obj);

        error = KOS_ERROR_EXCEPTION;
    }
    TRY(error);

    /* Import base module if necessary */
    if ((flags & KOS_IMPORT_BASE)) {
        if (kos_parser_import_base(&parser, ast)) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    time_1 = KOS_get_time_us();

    /* Save base module index */
    if (module_idx == KOS_BASE_MODULE_IDX)
        TRY(KOS_array_write(ctx, inst->modules.modules, module_idx, comp_ctx.module.o));

    /* Prepare compiler */
    program.ctx            = &comp_ctx;
    program.is_interactive = (flags & KOS_RUN_INTERACTIVE) ? 1 : 0;
    TRY(predefine_globals(ctx,
                          &program,
                          OBJPTR(MODULE, comp_ctx.module.o)->global_names,
                          OBJPTR(MODULE, comp_ctx.module.o)->module_names));

    /* Compile source code into bytecode */
    error = kos_compiler_compile(&program, ast, &num_opt_passes);

    if (error == KOS_ERROR_COMPILE_FAILED) {

        KOS_OBJ_ID error_obj = format_error(&comp_ctx,
                                            KOS_ERROR,
                                            program.error_str,
                                            get_token_pos(program.error_token));
        assert( ! IS_BAD_PTR(error_obj) || KOS_is_exception_pending(ctx));
        if ( ! IS_BAD_PTR(error_obj))
            KOS_raise_exception(ctx, error_obj);

        error = KOS_ERROR_EXCEPTION;
    }
    TRY(error);

    time_2 = KOS_get_time_us();

    /* Print number of optimization passes */
    if (inst->flags & KOS_INST_DEBUG) {
        KOS_VECTOR cname;

        KOS_vector_init(&cname);
        error = KOS_string_to_cstr_vec(ctx, OBJPTR(MODULE, comp_ctx.module.o)->name, &cname);

        if ( ! error) {
            printf("%s: parsing             : %u us\n", cname.buffer, (unsigned)(time_1 - time_0));
            printf("%s: compilation         : %u us\n", cname.buffer, (unsigned)(time_2 - time_1));
            printf("%s: optimization passes : %u\n",    cname.buffer, num_opt_passes);
        }

        KOS_vector_destroy(&cname);
        TRY(error);
    }

    TRY(alloc_globals(ctx, &program, comp_ctx.module.o));
    TRY(alloc_constants(ctx, &program, comp_ctx.module.o));
    TRY(save_direct_modules(ctx, &program, comp_ctx.module.o));

    {
        KOS_FRAME  *frame;

        /* Get function's constant index corresponding to global scope */
        assert(ast->is_scope);
        frame = (KOS_FRAME *)ast->u.scope;
        assert(frame->scope.has_frame);

        OBJPTR(MODULE, comp_ctx.module.o)->main_idx = frame->constant->header.index;
    }

cleanup:
    kos_parser_destroy(&parser);
    kos_compiler_destroy(&program);
    KOS_destroy_top_local(ctx, &comp_ctx.module);
    return error;
}

static KOS_OBJ_ID import_module(KOS_CONTEXT ctx,
                                const char *module_name,
                                unsigned    name_size,
                                int         is_path,
                                const char *data,
                                unsigned    data_size,
                                int        *out_already_loaded,
                                int        *out_module_idx);

static int load_base_module(KOS_CONTEXT ctx,
                            const char *module_name,
                            unsigned    name_size)
{
    KOS_INSTANCE *const inst   = ctx->inst;
    KOS_OBJ_ID          base_obj;
    int                 base_idx;
    int                 error  = KOS_SUCCESS;
    static const char   base[] = "base";

    if ((KOS_get_array_size(inst->modules.modules) != 0) || ! strncmp(module_name, base, name_size))
        return KOS_SUCCESS;

    if (inst->flags & KOS_INST_VERBOSE)
        print_search_paths(ctx, inst->modules.search_paths);

    base_obj = import_module(ctx, base, sizeof(base) - 1, 0, KOS_NULL, 0, KOS_NULL, &base_idx);
    TRY_OBJID(base_obj);

    assert(base_idx == KOS_BASE_MODULE_IDX);

    if (IS_BAD_PTR(KOS_run_module(ctx, base_obj))) {
        assert(KOS_is_exception_pending(ctx));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    return error;
}

static int insert_first_search_path(KOS_CONTEXT ctx,
                                    KOS_OBJ_ID  dir_obj)
{
    KOS_LOCAL path_array;
    KOS_LOCAL dir;
    int       error;

    KOS_init_local(     ctx, &path_array);
    KOS_init_local_with(ctx, &dir, dir_obj);

    path_array.o = KOS_new_array(ctx, 1);
    TRY_OBJID(path_array.o);

    TRY(KOS_array_write(ctx, path_array.o, 0, dir.o));
    TRY(KOS_array_insert(ctx, ctx->inst->modules.search_paths, 0, 0, path_array.o, 0, 1));

cleanup:
    KOS_destroy_top_locals(ctx, &dir, &path_array);

    return error;
}

static void handle_interpreter_error(KOS_CONTEXT ctx, int error)
{
    if (error == KOS_ERROR_EXCEPTION)
        assert(KOS_is_exception_pending(ctx));
    else if (error == KOS_ERROR_OUT_OF_MEMORY) {
        if ( ! KOS_is_exception_pending(ctx))
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
    }
    else {
        if ( ! KOS_is_exception_pending(ctx))
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_internal));
    }
}

static KOS_OBJ_ID import_module(KOS_CONTEXT ctx,
                                const char *module_name, /* Module name or path, ASCII or UTF-8    */
                                unsigned    name_size,   /* Length of module name or path in bytes */
                                int         is_path,     /* Module name can be a path              */
                                const char *data,        /* Module data or 0 if load from file     */
                                unsigned    data_size,   /* Data length if data is not 0           */
                                int        *out_already_loaded,
                                int        *out_module_idx)
{
    PROF_ZONE(MODULE)

    int                   error              = KOS_SUCCESS;
    int                   module_idx         = -1;
    int                   chain_init         = 0;
    int                   source_found       = 0;
    KOS_LOCAL             actual_module_name;
    KOS_LOCAL             module_dir;
    KOS_LOCAL             module_path;
    KOS_LOCAL             mod_init;
    KOS_LOCAL             module;
    KOS_INSTANCE   *const inst               = ctx->inst;
    KOS_MODULE_LOAD_CHAIN loading            = { KOS_NULL, KOS_NULL, 0 };
    KOS_FILEBUF           file_buf;

    KOS_filebuf_init(&file_buf);

    get_module_name(module_name, name_size, &loading);
    PROF_ZONE_NAME(loading.module_name, loading.length)

    KOS_init_locals(ctx, &actual_module_name, &module_dir, &module_path, &mod_init, &module,
                    kos_end_locals);

    if (name_size > 0xFFFFU) {
        KOS_raise_printf(ctx, "Module name length %u exceeds 65535 bytes\n", name_size);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    /* Determine actual module name */
    actual_module_name.o = KOS_new_string(ctx, loading.module_name, loading.length);
    TRY_OBJID(actual_module_name.o);

    if (data) {
        module_dir.o  = KOS_STR_EMPTY;
        module_path.o = actual_module_name.o;
        source_found  = 1;
    }

    /* TODO use global mutex for thread safety */

    /* Add path of the top-most module being loaded to search paths */
    if (KOS_get_array_size(inst->modules.modules) == 0) {

        KOS_OBJ_ID dir_obj;

        /* Find module source file */
        if ( ! source_found) {
            TRY(find_module(ctx,
                            actual_module_name.o,
                            module_name,
                            name_size,
                            is_path,
                            &loading,
                            (KOS_OBJ_ID *)&module_dir.o,
                            (KOS_OBJ_ID *)&module_path.o,
                            (KOS_OBJ_ID *)&mod_init.o));
            source_found = 1;
        }

        /* Add search path */
        if (KOS_get_string_length(module_dir.o) == 0)
            dir_obj = KOS_CONST_ID(str_cur_dir);
        else
            dir_obj = module_dir.o;
        TRY(insert_first_search_path(ctx, dir_obj));
    }

    /* Load base module first, so that it ends up at index 0 */
    TRY(load_base_module(ctx, module_name, name_size));

    /* Add module to the load chain to prevent and detect circular dependencies */
    {
        KOS_MODULE_LOAD_CHAIN *chain = inst->modules.load_chain;
        loading.next = chain;
        for ( ; chain; chain = chain->next) {
            if (loading.length == chain->length &&
                    0 == memcmp(loading.module_name, chain->module_name, loading.length)) {

                KOS_raise_printf(ctx, "circular dependencies detected for module \"%.*s\"",
                                 (int)name_size, module_name);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
        }
    }
    inst->modules.load_chain = &loading;
    chain_init               = 1;

    /* Return the module object if it was already loaded */
    {
        KOS_OBJ_ID module_idx_obj = KOS_get_property_shallow(ctx, inst->modules.module_names, actual_module_name.o);
        if ( ! IS_BAD_PTR(module_idx_obj)) {
            assert(IS_SMALL_INT(module_idx_obj));
            module.o = KOS_array_read(ctx, inst->modules.modules, (int)GET_SMALL_INT(module_idx_obj));
            if (IS_BAD_PTR(module.o))
                error = KOS_ERROR_EXCEPTION;
            else {
                module_idx = (int)GET_SMALL_INT(module_idx_obj);
                if (out_already_loaded)
                    *out_already_loaded = 1;
            }
            goto cleanup;
        }
    }
    KOS_clear_exception(ctx);

    /* Find module source file */
    if ( ! source_found) {
        TRY(find_module(ctx,
                        actual_module_name.o,
                        module_name,
                        name_size,
                        is_path,
                        &loading,
                        (KOS_OBJ_ID *)&module_dir.o,
                        (KOS_OBJ_ID *)&module_path.o,
                        (KOS_OBJ_ID *)&mod_init.o));
    }

    if (inst->flags & KOS_INST_VERBOSE)
        print_load_info(ctx, actual_module_name.o, module_path.o);

    /* Allocate module object and reserve module index */
    module.o = alloc_module(ctx, actual_module_name.o, &module_idx);
    TRY_OBJID(module.o);
    OBJPTR(MODULE, module.o)->path = module_path.o;

    /* Load module file */
    if ( ! data) {
        unsigned flags = KOS_MODULE_NEEDS_KOS_SOURCE;

        if ( ! IS_BAD_PTR(mod_init.o))
            flags = ((struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, mod_init.o))->flags;

        TRY(load_file(ctx, module_path.o, &file_buf, flags));
        data      = file_buf.buffer;
        data_size = (unsigned)file_buf.size;
    }

    /* Run built-in module initialization */
    if ( ! IS_BAD_PTR(mod_init.o)) {
        PROF_ZONE_N(MODULE, builtin_init)

        KOS_OBJ_ID func_obj;

        func_obj = KOS_new_function(ctx);
        TRY_OBJID(func_obj);

        OBJPTR(FUNCTION, func_obj)->module = module.o;
        OBJPTR(FUNCTION, func_obj)->name   = KOS_CONST_ID(str_global);

        TRY(kos_stack_push(ctx, func_obj, KOS_NO_REG, INSTR_CALL));

        error = ((struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, mod_init.o))->init(ctx, module.o);

        kos_stack_pop(ctx);

        if (error) {
            assert(KOS_is_exception_pending(ctx));
            goto cleanup;
        }
    }

    /* Compile module source to bytecode */
    TRY(compile_module(ctx, module.o, (uint16_t)module_idx, data, data_size, KOS_RUN_NO_FLAGS));

    /* Free file buffer */
    KOS_unload_file(&file_buf);

    /* Put module on the list */
    TRY(KOS_array_write(ctx, inst->modules.modules, (uint16_t)module_idx, module.o));
    TRY(KOS_set_property(ctx, inst->modules.module_names, actual_module_name.o, TO_SMALL_INT(module_idx)));

cleanup:
    if (chain_init)
        inst->modules.load_chain = loading.next;

    module.o = KOS_destroy_top_locals(ctx, &actual_module_name, &module);

    KOS_unload_file(&file_buf);

    if (error) {
        handle_interpreter_error(ctx, error);
        module.o = KOS_BADPTR;
    }
    else {
        *out_module_idx = module_idx;
        assert(!KOS_is_exception_pending(ctx));
    }

    return module.o;
}

KOS_OBJ_ID KOS_load_module(KOS_CONTEXT ctx, const char *path, unsigned path_len)
{
    int idx;

    return import_module(ctx, path, path_len, 1, KOS_NULL, 0, KOS_NULL, &idx);
}

KOS_OBJ_ID KOS_load_module_from_memory(KOS_CONTEXT ctx,
                                       const char *module_name,
                                       unsigned    module_name_len,
                                       const char *buf,
                                       unsigned    buf_size)
{
    int idx;

    return import_module(ctx, module_name, module_name_len, 0, buf, buf_size, KOS_NULL, &idx);
}

int kos_comp_import_module(void       *vframe,
                           const char *name,
                           uint16_t    length,
                           int        *module_idx)
{
    struct KOS_COMP_CTX *comp_ctx = (struct KOS_COMP_CTX *)vframe;
    KOS_CONTEXT          ctx      = comp_ctx->ctx;
    KOS_OBJ_ID           module_obj;
    int                  already_loaded = 0;

    assert(module_idx);

    module_obj = import_module(ctx, name, length, 0, KOS_NULL, 0, &already_loaded, module_idx);

    if (IS_BAD_PTR(module_obj)) {
        assert(KOS_is_exception_pending(ctx));
        return KOS_ERROR_EXCEPTION;
    }

    if ( ! already_loaded) {
        const KOS_OBJ_ID ret = KOS_run_module(ctx, module_obj);

        if (IS_BAD_PTR(ret)) {
            assert(KOS_is_exception_pending(ctx));
            return KOS_ERROR_EXCEPTION;
        }
    }

    return KOS_SUCCESS;
}

static int append_stdin(KOS_CONTEXT ctx, KOS_VECTOR *buf)
{
    int error = KOS_SUCCESS;

    for (;;) {
        const size_t last_size = buf->size;
        size_t       num_read;

        TRY(KOS_vector_resize(buf, last_size + KOS_BUF_ALLOC_SIZE));

        num_read = fread(buf->buffer + last_size, 1, KOS_BUF_ALLOC_SIZE, stdin);

        if (num_read < KOS_BUF_ALLOC_SIZE) {

            TRY(KOS_vector_resize(buf, last_size + num_read));

            if (ferror(stdin)) {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_stdin));
                error = KOS_ERROR_EXCEPTION;
            }
            else {
                assert(feof(stdin));
            }
            break;
        }
    }

cleanup:
    return error;
}

KOS_OBJ_ID KOS_repl(KOS_CONTEXT ctx,
                    const char *module_name,
                    unsigned    flags,
                    KOS_OBJ_ID *out_module,
                    const char *buf,
                    unsigned    buf_size)
{
    KOS_MODULE_LOAD_CHAIN loading    = { KOS_NULL, KOS_NULL, 0 };
    KOS_VECTOR            storage;
    KOS_LOCAL             module;
    KOS_LOCAL             module_name_str;
    KOS_OBJ_ID            ret        = KOS_BADPTR;
    KOS_INSTANCE         *inst       = ctx->inst;
    static const char     unnamed[]  = "<unnamed>";
    const unsigned        name_size  = module_name[0] ? (unsigned)strlen(module_name) : (unsigned)sizeof(unnamed) - 1;
    int                   chain_init = 0;
    int                   module_idx = -1;
    int                   error      = KOS_SUCCESS;

    assert(flags);

    if ( ! module_name[0])
        module_name = unnamed;

    KOS_vector_init(&storage);

    KOS_init_locals(ctx, &module_name_str, &module, kos_end_locals);

    /* TODO use global mutex for thread safety */

    if ((flags & KOS_RUN_STDIN)) {
        assert( ! buf);
        assert( ! buf_size);

        TRY(append_stdin(ctx, &storage));

        buf      = storage.buffer;
        buf_size = (unsigned)storage.size;
    }
    else if ((flags & KOS_IMPORT_BASE)) {
        if ( ! buf_size) {
            buf      = " ";
            buf_size = 1;
        }
    }

    if ( ! buf_size) {
        ret = KOS_VOID;
        goto cleanup;
    }

    assert(buf);

    if ( ! (flags & KOS_RUN_CONTINUE))
        TRY(insert_first_search_path(ctx, KOS_CONST_ID(str_cur_dir)));

    module_name_str.o = KOS_new_string(ctx, module_name, name_size);
    TRY_OBJID(module_name_str.o);

    /* Load base module first, so that it ends up at index 0 */
    TRY(load_base_module(ctx, module_name, name_size));

    /* Find module object */
    if ( ! (flags & KOS_RUN_TEMPORARY)) {
        KOS_OBJ_ID module_idx_obj = KOS_get_property_shallow(ctx, inst->modules.module_names, module_name_str.o);

        KOS_clear_exception(ctx);

        if ((flags & KOS_RUN_CONTINUE) && IS_BAD_PTR(module_idx_obj)) {
            KOS_raise_printf(ctx, "module \"%s\" not found", module_name);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if ( ! (flags & KOS_RUN_CONTINUE) && ! IS_BAD_PTR(module_idx_obj)) {
            KOS_raise_printf(ctx, "module \"%s\" already loaded", module_name);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if ( ! IS_BAD_PTR(module_idx_obj)) {
            assert(IS_SMALL_INT(module_idx_obj));
            module.o = KOS_array_read(ctx, inst->modules.modules, (int)GET_SMALL_INT(module_idx_obj));
            TRY_OBJID(module.o);
            module_idx = (int)GET_SMALL_INT(module_idx_obj);
        }
    }

    /* Add module to the load chain to prevent and detect circular dependencies */
    loading.module_name      = module_name;
    loading.length           = name_size;
    loading.next             = inst->modules.load_chain;
    inst->modules.load_chain = &loading;
    chain_init               = 1;

    /* Allocate module object and reserve module index */
    if (module_idx == -1) {
        assert( ! (flags & KOS_RUN_CONTINUE));
        /* TODO don't accumulate if KOS_RUN_TEMPORARY */
        module.o = alloc_module(ctx, module_name_str.o, &module_idx);
        TRY_OBJID(module.o);
    }
    else {
        assert((flags & KOS_RUN_CONTINUE));
    }

    assert(module_idx <= 0xFFFF);

    /* Compile evaluated source to bytecode */
    TRY(compile_module(ctx, module.o, (uint16_t)module_idx, buf, buf_size, flags));

    /* Put module on the list */
    if ( ! (flags & (KOS_RUN_CONTINUE | KOS_RUN_TEMPORARY))) {
        TRY(KOS_array_write(ctx, inst->modules.modules, (uint16_t)module_idx, module.o));
        TRY(KOS_set_property(ctx, inst->modules.module_names, module_name_str.o, TO_SMALL_INT(module_idx)));
    }

    /* Run module */
    ret = KOS_run_module(ctx, module.o);

    if (IS_BAD_PTR(ret)) {
        assert(KOS_is_exception_pending(ctx));
        error = KOS_ERROR_EXCEPTION;
    }

cleanup:
    if (chain_init)
        inst->modules.load_chain = loading.next;

    module.o = KOS_destroy_top_locals(ctx, &module_name_str, &module);

    KOS_vector_destroy(&storage);

    if (error)
        handle_interpreter_error(ctx, error);
    else {
        assert(!KOS_is_exception_pending(ctx));
    }

    if (out_module)
        *out_module = module.o;

    return error ? KOS_BADPTR : ret;
}

static void print_run_info(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  module_obj,
                           KOS_OBJ_ID  func_name,
                           const char* str1,
                           const char* str2)
{
    KOS_VECTOR cstr_module;
    KOS_VECTOR cstr_func;

    if ( ! (ctx->inst->flags & KOS_INST_VERBOSE))
        return;

    KOS_vector_init(&cstr_module);
    KOS_vector_init(&cstr_func);

    if ( ! KOS_string_to_cstr_vec(ctx, OBJPTR(MODULE, module_obj)->name, &cstr_module) &&
         ! KOS_string_to_cstr_vec(ctx, func_name, &cstr_func))
        printf("%s%s.%s%s\n", str1, cstr_module.buffer, cstr_func.buffer, str2);
    else
        KOS_clear_exception(ctx);

    KOS_vector_destroy(&cstr_func);
    KOS_vector_destroy(&cstr_module);
}

KOS_OBJ_ID KOS_module_run_function(KOS_CONTEXT               ctx,
                                   KOS_OBJ_ID                module_obj,
                                   KOS_OBJ_ID                func_name,
                                   enum KOS_RUN_FUNC_FLAGS_E run_flags)
{
    KOS_LOCAL  func;
    KOS_OBJ_ID args;
    KOS_OBJ_ID ret = KOS_BADPTR;

    const int error = KOS_module_get_global(ctx, module_obj, func_name, &func.o, KOS_NULL);

    if (error) {
        if (run_flags != KOS_FUNC_OPTIONAL)
            return KOS_BADPTR;

        KOS_clear_exception(ctx);

        print_run_info(ctx, module_obj, func_name, "Function ", " not found");

        return KOS_VOID;
    }

    print_run_info(ctx, module_obj, func_name, "Running function ", "");

    KOS_init_local_with(ctx, &func, func.o);

    args = KOS_array_slice(ctx, ctx->inst->args, 0, MAX_INT64);

    if ( ! IS_BAD_PTR(args))
        ret = KOS_call_function(ctx, func.o, KOS_VOID, args);

    KOS_destroy_top_local(ctx, &func);

    return ret;
}

int KOS_module_add_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name_obj,
                          KOS_OBJ_ID  value_obj,
                          unsigned   *idx)
{
    int         error;
    uint32_t    new_idx;
    KOS_OBJ_ID  prop;
    KOS_LOCAL   module;
    KOS_LOCAL   name;
    KOS_LOCAL   value;

    assert( ! IS_BAD_PTR(module_obj));

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &name,   name_obj);
    KOS_init_local_with(ctx, &value,  value_obj);

    prop = KOS_get_property_shallow(ctx, OBJPTR(MODULE, module.o)->global_names, name.o);

    KOS_clear_exception(ctx);

    if ( ! IS_BAD_PTR(prop)) {
        KOS_VECTOR global_cstr;

        KOS_vector_init(&global_cstr);

        error = KOS_string_to_cstr_vec(ctx, name.o, &global_cstr);
        if ( ! error) {
            KOS_raise_printf(ctx, "duplicate global \"%s\"", global_cstr.buffer);
            error = KOS_ERROR_EXCEPTION;
        }

        KOS_vector_destroy(&global_cstr);
        goto cleanup;
    }

    TRY(KOS_array_push(ctx, OBJPTR(MODULE, module.o)->globals, value.o, &new_idx));
    TRY(KOS_set_property(ctx, OBJPTR(MODULE, module.o)->global_names, name.o, TO_SMALL_INT((int)new_idx)));

    if (idx)
        *idx = new_idx;

cleanup:
    KOS_destroy_top_locals(ctx, &value, &module);
    return error;
}

int KOS_module_get_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID *value,
                          unsigned   *idx)
{
    int         error  = KOS_SUCCESS;
    KOS_OBJ_ID  idx_obj;
    KOS_MODULE* module = OBJPTR(MODULE, module_obj);

    assert(module);

    idx_obj = KOS_get_property_shallow(ctx, module->global_names, name);
    TRY_OBJID(idx_obj);

    assert(IS_SMALL_INT(idx_obj));

    if (value) {
        KOS_OBJ_ID ret = KOS_array_read(ctx, module->globals, (int)GET_SMALL_INT(idx_obj));
        TRY_OBJID(ret);

        *value = ret;
    }

    if (idx)
        *idx = (unsigned)GET_SMALL_INT(idx_obj);

cleanup:
    return error;
}

int KOS_module_add_function(KOS_CONTEXT          ctx,
                            KOS_OBJ_ID           module_obj,
                            KOS_OBJ_ID           name_obj,
                            KOS_FUNCTION_HANDLER handler,
                            const KOS_CONVERT   *args,
                            KOS_FUNCTION_STATE   gen_state)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID func_obj;
    KOS_LOCAL  module;
    KOS_LOCAL  name;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &name,   name_obj);

    func_obj = KOS_new_builtin_function(ctx, name_obj, handler, args);

    assert(GET_OBJ_TYPE(module.o) == OBJ_MODULE);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module.o;
    OBJPTR(FUNCTION, func_obj)->state  = (uint8_t)gen_state;

    TRY(KOS_module_add_global(ctx,
                              module.o,
                              name.o,
                              func_obj,
                              KOS_NULL));

cleanup:
    KOS_destroy_top_locals(ctx, &name, &module);
    return error;
}

int KOS_module_add_constructor(KOS_CONTEXT          ctx,
                               KOS_OBJ_ID           module_obj,
                               KOS_OBJ_ID           name_obj,
                               KOS_FUNCTION_HANDLER handler,
                               const KOS_CONVERT   *args,
                               KOS_OBJ_ID          *ret_proto)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL func;
    KOS_LOCAL module;
    KOS_LOCAL name;

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    KOS_init_local(     ctx, &func);
    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &name,   name_obj);

    func.o = KOS_new_builtin_class(ctx, name_obj, handler, args);
    TRY_OBJID(func.o);

    OBJPTR(CLASS, func.o)->module = module.o;

    TRY(KOS_module_add_global(ctx,
                              module.o,
                              name.o,
                              func.o,
                              KOS_NULL));

    *ret_proto = KOS_atomic_read_relaxed_obj(OBJPTR(CLASS, func.o)->prototype);
    assert( ! IS_BAD_PTR(*ret_proto));

cleanup:
    KOS_destroy_top_locals(ctx, &name, &func);
    return error;
}

int KOS_module_add_member_function(KOS_CONTEXT          ctx,
                                   KOS_OBJ_ID           module_obj,
                                   KOS_OBJ_ID           proto_obj,
                                   KOS_OBJ_ID           name_obj,
                                   KOS_FUNCTION_HANDLER handler,
                                   const KOS_CONVERT   *args,
                                   KOS_FUNCTION_STATE   gen_state)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID func_obj;
    KOS_LOCAL  module;
    KOS_LOCAL  proto;
    KOS_LOCAL  name;

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &proto,  proto_obj);
    KOS_init_local_with(ctx, &name,   name_obj);

    func_obj = KOS_new_builtin_function(ctx, name.o, handler, args);
    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module.o;
    OBJPTR(FUNCTION, func_obj)->state  = (uint8_t)gen_state;

    TRY(KOS_set_property(ctx, proto.o, name.o, func_obj));

cleanup:
    KOS_destroy_top_locals(ctx, &name, &module);
    return error;
}

int KOS_module_add_static_function(KOS_CONTEXT          ctx,
                                   KOS_OBJ_ID           module_obj,
                                   KOS_OBJ_ID           class_name_obj,
                                   KOS_OBJ_ID           name_obj,
                                   KOS_FUNCTION_HANDLER handler,
                                   const KOS_CONVERT   *args,
                                   KOS_FUNCTION_STATE   gen_state)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID func_obj;
    KOS_LOCAL  module;
    KOS_LOCAL  ctor;
    KOS_LOCAL  name;

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &name,   name_obj);
    KOS_init_local(     ctx, &ctor);

    TRY(KOS_module_get_global(ctx, module.o, class_name_obj, &ctor.o, KOS_NULL));

    func_obj = KOS_new_builtin_function(ctx, name.o, handler, args);
    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module.o;
    OBJPTR(FUNCTION, func_obj)->state  = (uint8_t)gen_state;

    TRY(KOS_set_property(ctx, ctor.o, name.o, func_obj));

cleanup:
    KOS_destroy_top_locals(ctx, &ctor, &module);
    return error;
}

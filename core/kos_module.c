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

#include "../inc/kos_module.h"
#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_compiler.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_disasm.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_object_internal.h"
#include "kos_parser.h"
#include "kos_system.h"
#include "kos_try.h"
#include "kos_utf8.h"
#include "kos_vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_cur_dir[]              = ".";
static const char str_eol[]                  = "\n";
static const char str_err_circular_deps[]    = "circular dependencies detected for module \"";
static const char str_err_duplicate_global[] = "duplicate global \"";
static const char str_err_end[]              = "\"";
static const char str_err_internal[]         = "internal error";
static const char str_err_module[]           = "module \"";
static const char str_err_not_found[]        = "\" not found";
static const char str_err_stdin[]            = "failed reading from stdin";
static const char str_err_unable_to_open[]   = "unable to open file \"";
static const char str_err_unable_to_read[]   = "unable to read file \"";
static const char str_format_colon[]         = ":";
static const char str_format_error[]         = ": error: ";
static const char str_path_sep[]             = KOS_PATH_SEPARATOR_STR;
static const char str_script_ext[]           = ".kos";

struct _KOS_MODULE_LOAD_CHAIN {
    struct _KOS_MODULE_LOAD_CHAIN *next;
    const char                    *module_name;
    unsigned                       length;
};

static void _raise_3(KOS_CONTEXT ctx,
                     const char *s1,
                     KOS_OBJ_ID  s2,
                     const char *s3)
{
    KOS_OBJ_ID str_err[3];

    str_err[0] = KOS_BADPTR;
    str_err[1] = KOS_BADPTR;
    str_err[2] = KOS_BADPTR;

    kos_track_refs(ctx, 3, &str_err[0], &str_err[1], &str_err[2]);

    str_err[0] = KOS_new_const_ascii_cstring(ctx, s1);
    str_err[1] = s2;
    if ( ! IS_BAD_PTR(str_err[0])) {
        str_err[2] = KOS_new_const_ascii_cstring(ctx, s3);

        if ( ! IS_BAD_PTR(str_err[2])) {

            const KOS_OBJ_ID str_err_full = KOS_string_add_n(ctx, str_err, 3);

            if ( ! IS_BAD_PTR(str_err_full))
                KOS_raise_exception(ctx, str_err_full);
        }
    }

    kos_untrack_refs(ctx, 3);
}

static unsigned _rfind_path(const char *path,
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

static int _find_module(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  module_name,
                        const char *maybe_path,
                        unsigned    length,
                        KOS_OBJ_ID *out_abs_dir,
                        KOS_OBJ_ID *out_abs_path)
{
    int        error = KOS_ERROR_INTERNAL;
    KOS_OBJ_ID path  = KOS_BADPTR;
    KOS_OBJ_ID dir   = KOS_BADPTR;
    KOS_VECTOR cpath;
    unsigned   i;

    kos_vector_init(&cpath);

    TRY(KOS_push_locals(ctx, 2, &path, &dir));

    /* Find dot or path separator, it indicates it's a path to a file */
    if (_rfind_path(maybe_path, length, '.') > 0) {
        TRY(kos_vector_resize(&cpath, length+1));
        memcpy(cpath.buffer, maybe_path, length);
        cpath.buffer[length] = 0;

        TRY(kos_get_absolute_path(&cpath));

        if (!kos_does_file_exist(cpath.buffer))
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        path = KOS_new_string(ctx, cpath.buffer, (unsigned)(cpath.size-1));
        TRY_OBJID(path);

        /* Find and skip last path separator */
        i = _rfind_path(cpath.buffer, (unsigned)(cpath.size-1), '/');
        if (i > 0)
            --i;

        dir = KOS_new_string(ctx, cpath.buffer, i);
        TRY_OBJID(dir);
    }
    else {

        KOS_INSTANCE *inst      = ctx->inst;
        uint32_t      num_paths = KOS_get_array_size(inst->modules.search_paths);

        KOS_OBJ_ID components[4];

        components[0] = KOS_BADPTR;
        components[1] = KOS_BADPTR;
        components[2] = KOS_BADPTR;
        components[3] = KOS_BADPTR;

        if (!num_paths)
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        TRY(KOS_push_locals(ctx, 5, &module_name,
                &components[0], &components[1], &components[2], &components[3]));

        for (i = 0; i < num_paths; i++) {
            components[0] = KOS_BADPTR;
            components[1] = KOS_BADPTR;
            components[2] = KOS_BADPTR;
            components[3] = KOS_BADPTR;

            components[0] = KOS_array_read(ctx, inst->modules.search_paths, (int)i);
            TRY_OBJID(components[0]);
            components[1] = KOS_new_const_ascii_string(ctx, str_path_sep,
                                                       sizeof(str_path_sep) - 1);
            TRY_OBJID(components[1]);
            components[2] = module_name;
            components[3] = KOS_new_const_ascii_string(ctx, str_script_ext,
                                                       sizeof(str_script_ext) - 1);
            TRY_OBJID(components[3]);

            path = KOS_string_add_n(ctx, components, sizeof(components)/sizeof(components[0]));
            TRY_OBJID(path);

            TRY(KOS_string_to_cstr_vec(ctx, path, &cpath));

            if (kos_does_file_exist(cpath.buffer)) {
                dir   = components[0];
                error = KOS_SUCCESS;
                break;
            }
        }

        KOS_pop_locals(ctx, 5);

        if (IS_BAD_PTR(dir))
            error = KOS_ERROR_NOT_FOUND;
    }

    KOS_pop_locals(ctx, 2);

cleanup:
    if (!error) {
        *out_abs_dir  = dir;
        *out_abs_path = path;
    }

    kos_vector_destroy(&cpath);

    assert(error != KOS_ERROR_INTERNAL);

    return error;
}

static void _get_module_name(const char                    *module,
                             unsigned                       length,
                             struct _KOS_MODULE_LOAD_CHAIN *loading)
{
    unsigned i = _rfind_path(module, length, '.');

    if (i > 0) {
        if (module[i-1] == '.') {
            length = --i;
            i      = _rfind_path(module, i, '/');
        }
        module += i;
        length -= i;
    }

    loading->module_name = module;
    loading->length      = length;
}

static KOS_OBJ_ID _alloc_module(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  module_name)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID module = KOS_BADPTR;
    KOS_OBJ_ID obj_id = KOS_BADPTR;

    kos_track_refs(ctx, 3, &module_name, &module, &obj_id);

    module = OBJID(MODULE, (KOS_MODULE *)
            kos_alloc_object(ctx, OBJ_MODULE, sizeof(KOS_MODULE)));
    TRY_OBJID(module);

    {
        KOS_MODULE *const module_ptr = OBJPTR(MODULE, module);

        memset(((uint8_t *)module_ptr) + sizeof(module_ptr->header),
               0,
               sizeof(*module_ptr) - sizeof(module_ptr->header));
    }

    assert(OBJPTR(MODULE, module)->header.type == OBJ_MODULE);

    OBJPTR(MODULE, module)->name         = module_name;
    OBJPTR(MODULE, module)->inst         = ctx->inst;
    OBJPTR(MODULE, module)->constants    = KOS_BADPTR;
    OBJPTR(MODULE, module)->global_names = KOS_BADPTR;
    OBJPTR(MODULE, module)->globals      = KOS_BADPTR;
    OBJPTR(MODULE, module)->module_names = KOS_BADPTR;

    obj_id = KOS_new_object(ctx);
    TRY_OBJID(obj_id);
    OBJPTR(MODULE, module)->global_names = obj_id;

    obj_id = KOS_new_array(ctx, 0);
    TRY_OBJID(obj_id);
    OBJPTR(MODULE, module)->globals = obj_id;

    obj_id = KOS_new_object(ctx);
    TRY_OBJID(obj_id);
    OBJPTR(MODULE, module)->module_names = obj_id;

cleanup:
    kos_untrack_refs(ctx, 3);

    return error ? KOS_BADPTR : module;
}

static int _load_file(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  path,
                      KOS_VECTOR *file_buf)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cpath;

    kos_track_refs(ctx, 1, &path);

    kos_vector_init(&cpath);
    TRY(KOS_string_to_cstr_vec(ctx, path, &cpath));

    error = kos_load_file(cpath.buffer, file_buf);

    switch (error) {

        case KOS_ERROR_CANNOT_OPEN_FILE:
            _raise_3(ctx, str_err_unable_to_open, path, str_err_end);
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_CANNOT_READ_FILE:
            _raise_3(ctx, str_err_unable_to_read, path, str_err_end);
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_OUT_OF_MEMORY:
            KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
            error = KOS_ERROR_EXCEPTION;
            break;

        default:
            break;
    }

cleanup:
    kos_vector_destroy(&cpath);

    kos_untrack_refs(ctx, 1);

    return error;
}

static int _predefine_globals(KOS_CONTEXT    ctx,
                              KOS_COMP_UNIT *program,
                              KOS_OBJ_ID     global_names,
                              KOS_OBJ_ID     module_names,
                              int            is_repl)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cpath;
    KOS_OBJ_ID walk;

    kos_vector_init(&cpath);

    walk = KOS_new_object_walk(ctx, global_names, KOS_SHALLOW);
    TRY_OBJID(walk);

    while ( ! KOS_object_walk(ctx, walk)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk), &cpath));

        TRY(kos_compiler_predefine_global(program,
                                          cpath.buffer,
                                          (int)GET_SMALL_INT(KOS_get_walk_value(walk)),
                                          is_repl ? 0 : 1));
    }

    walk = KOS_new_object_walk(ctx, module_names, KOS_SHALLOW);
    TRY_OBJID(walk);

    while ( ! KOS_object_walk(ctx, walk)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk), &cpath));

        TRY(kos_compiler_predefine_module(program, cpath.buffer,
                                          (int)GET_SMALL_INT(KOS_get_walk_value(walk))));
    }

cleanup:
    kos_vector_destroy(&cpath);

    return error;
}

static int _alloc_globals(KOS_CONTEXT    ctx,
                          KOS_COMP_UNIT *program,
                          KOS_MODULE    *module)
{
    int      error;
    KOS_VAR *var;

    TRY(KOS_array_resize(ctx, module->globals, (uint32_t)program->num_globals));

    for (var = program->globals; var; var = var->next) {

        if (var->type == VAR_GLOBAL) {

            KOS_OBJ_ID name;

            name = KOS_new_string(ctx, var->token->begin, var->token->length);

            TRY_OBJID(name);
            assert(var->array_idx < program->num_globals);
            TRY(KOS_set_property(ctx, module->global_names, name, TO_SMALL_INT(var->array_idx)));
        }
    }

cleanup:
    return error;
}

static int _save_direct_modules(KOS_CONTEXT    ctx,
                                KOS_COMP_UNIT *program,
                                KOS_MODULE    *module)
{
    int                 error = KOS_SUCCESS;
    KOS_VAR            *var;
    KOS_INSTANCE *const inst  = ctx->inst;

    for (var = program->modules; var; var = var->next) {

        KOS_OBJ_ID name;
        KOS_OBJ_ID module_idx_obj;

        name = KOS_new_string(ctx, var->token->begin, var->token->length);
        TRY_OBJID(name);

        module_idx_obj = KOS_get_property(ctx, inst->modules.module_names, name);
        TRY_OBJID(module_idx_obj);

        assert(IS_SMALL_INT(module_idx_obj));

        TRY(KOS_set_property(ctx, module->module_names, name, module_idx_obj));
    }

cleanup:
    return error;
}

static uint32_t _count_constants(KOS_COMP_UNIT *program)
{
    uint32_t i;

    KOS_COMP_CONST *str = program->first_constant;

    for (i = 0; str; str = str->next, ++i);

    return i;
}

static int _alloc_constants(KOS_CONTEXT    ctx,
                            KOS_COMP_UNIT *program,
                            KOS_MODULE    *module)
{
    int             error         = KOS_SUCCESS;
    const uint32_t  num_constants = _count_constants(program);
    uint32_t        base_idx      = 0;
    KOS_COMP_CONST *constant      = program->first_constant;
    int             i;

    if (IS_BAD_PTR(module->constants)) {
        module->constants = KOS_new_array(ctx, num_constants);

        TRY_OBJID(module->constants);
    }
    else {
        base_idx = KOS_get_array_size(module->constants);
        TRY(KOS_array_resize(ctx, module->constants, base_idx + num_constants));
    }

    for (i = 0; constant; constant = constant->next, ++i) {

        KOS_OBJ_ID obj_id = KOS_BADPTR;

        switch (constant->type) {

            default:
                assert(constant->type == KOS_COMP_CONST_INTEGER);
                obj_id = KOS_new_int(ctx, ((KOS_COMP_INTEGER *)constant)->value);
                break;

            case KOS_COMP_CONST_FLOAT:
                obj_id = KOS_new_float(ctx, ((KOS_COMP_FLOAT *)constant)->value);
                break;

            case KOS_COMP_CONST_STRING: {
                KOS_COMP_STRING *str = (KOS_COMP_STRING *)constant;

                obj_id = str->escape == KOS_UTF8_WITH_ESCAPE
                       ? KOS_new_string_esc(ctx, str->str, str->length)
                       : KOS_new_string(ctx, str->str, str->length);
                break;
            }

            case KOS_COMP_CONST_FUNCTION: {
                KOS_COMP_FUNCTION *func_const = (KOS_COMP_FUNCTION *)constant;
                KOS_FUNCTION      *func;

                if (func_const->flags & KOS_COMP_FUN_CLASS) {
                    obj_id = KOS_new_class(ctx, KOS_VOID);
                    if (IS_BAD_PTR(obj_id))
                        break;
                    func = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
                }
                else {
                    obj_id = KOS_new_function(ctx);
                    if (IS_BAD_PTR(obj_id))
                        break;
                    func = OBJPTR(FUNCTION, obj_id);
                }

                func->header.flags    = (uint8_t)(func_const->flags &
                                        (KOS_COMP_FUN_ELLIPSIS | KOS_COMP_FUN_CLOSURE));
                func->header.num_args = func_const->num_args;
                func->header.num_regs = func_const->num_regs;
                func->args_reg        = func_const->args_reg;
                func->instr_offs      = module->bytecode_size + func_const->offset;
                func->module          = OBJID(MODULE, module);

                if (func_const->flags & KOS_COMP_FUN_GENERATOR)
                    func->state = KOS_GEN_INIT;
                break;
            }

            case KOS_COMP_CONST_PROTOTYPE:
                obj_id = KOS_new_object(ctx);
                break;
        }

        TRY_OBJID(obj_id);

        TRY(KOS_array_write(ctx, module->constants, base_idx + i, obj_id));
    }

cleanup:
    return error;
}

static KOS_OBJ_ID _get_line(KOS_CONTEXT ctx,
                            const char *buf,
                            unsigned    buf_size,
                            unsigned    line)
{
    const char *const end = buf + buf_size;
    const char       *begin;
    unsigned          len       = 0;
    KOS_VECTOR        line_buf;
    KOS_OBJ_ID        ret       = KOS_BADPTR;

    kos_vector_init(&line_buf);

    /* Find the desired line */
    for ( ; line > 1 && buf < end; line--) {
        for ( ; buf < end; buf++) {

            char c = *buf;

            if (c == '\r') {
                buf++;
                if (buf < end)
                    c = *buf;
            }
            if (c == '\n') {
                buf++;
                c = '\r';
            }

            if (c == '\r')
                break;
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
    if ( ! kos_vector_resize(&line_buf, len)) {

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
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));

    kos_vector_destroy(&line_buf);

    return ret;
}

static KOS_OBJ_ID _format_error(KOS_CONTEXT  ctx,
                                KOS_OBJ_ID   module_obj,
                                const char  *data,
                                unsigned     data_size,
                                const char  *error_str,
                                KOS_FILE_POS pos)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    KOS_VECTOR cstr;
    int        pushed = 0;
    KOS_OBJ_ID parts[11];

    parts[0]  = KOS_BADPTR;
    parts[1]  = KOS_BADPTR;
    parts[2]  = KOS_BADPTR;
    parts[3]  = KOS_BADPTR;
    parts[4]  = KOS_BADPTR;
    parts[5]  = KOS_BADPTR;
    parts[6]  = KOS_BADPTR;
    parts[7]  = KOS_BADPTR;
    parts[8]  = KOS_BADPTR;
    parts[9]  = KOS_BADPTR;
    parts[10] = KOS_BADPTR;

    kos_vector_init(&cstr);

    TRY(KOS_push_locals(ctx, 11,
                &parts[0], &parts[1], &parts[2], &parts[3], &parts[4],
                &parts[5], &parts[6], &parts[7], &parts[8], &parts[9],
                &parts[10]));
    pushed = 1;

    parts[0] = KOS_get_file_name(ctx, OBJPTR(MODULE, module_obj)->path);
    TRY_OBJID(parts[0]);

    parts[1] = KOS_new_const_ascii_string(ctx, str_format_colon,
                                          sizeof(str_format_colon) - 1);
    TRY_OBJID(parts[1]);

    parts[2] = KOS_object_to_string(ctx, TO_SMALL_INT((int)pos.line));
    TRY_OBJID(parts[2]);

    parts[3] = KOS_new_const_ascii_string(ctx, str_format_colon,
                                          sizeof(str_format_colon) - 1);
    TRY_OBJID(parts[3]);

    parts[4] = KOS_object_to_string(ctx, TO_SMALL_INT((int)pos.column));
    TRY_OBJID(parts[4]);

    parts[5] = KOS_new_const_ascii_string(ctx, str_format_error,
                                          sizeof(str_format_error) - 1);
    TRY_OBJID(parts[5]);

    parts[6] = KOS_new_const_ascii_cstring(ctx, error_str);
    TRY_OBJID(parts[6]);

    parts[7] = KOS_new_const_ascii_string(ctx, str_eol,
                                          sizeof(str_eol) - 1);
    TRY_OBJID(parts[7]);

    parts[8] = _get_line(ctx, data, data_size, pos.line);
    TRY_OBJID(parts[8]);

    parts[9] = KOS_new_const_ascii_string(ctx, str_eol,
                                          sizeof(str_eol) - 1);
    TRY_OBJID(parts[9]);

    error = kos_vector_resize(&cstr, pos.column);
    if (error) {
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
        goto cleanup;
    }

    memset(cstr.buffer, ' ', pos.column-1);
    cstr.buffer[pos.column-1] = '^';

    parts[10] = KOS_new_string(ctx, cstr.buffer, pos.column);
    TRY_OBJID(parts[10]);

    ret = KOS_string_add_n(ctx, parts, sizeof(parts)/sizeof(parts[0]));

cleanup:
    if (error)
        ret = KOS_BADPTR;

    if (pushed)
        KOS_pop_locals(ctx, 11);

    kos_vector_destroy(&cstr);

    return ret;
}

int KOS_load_module(KOS_CONTEXT ctx, const char *path)
{
    int        idx;
    KOS_OBJ_ID module = kos_module_import(ctx,
                                          path,
                                          (unsigned)strlen(path),
                                          0,
                                          0,
                                          &idx);

    return IS_BAD_PTR(module) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

int KOS_load_module_from_memory(KOS_CONTEXT ctx,
                                const char *module_name,
                                const char *buf,
                                unsigned    buf_size)
{
    int        idx;
    KOS_OBJ_ID module = kos_module_import(ctx,
                                          module_name,
                                          (unsigned)strlen(module_name),
                                          buf,
                                          buf_size,
                                          &idx);

    return IS_BAD_PTR(module) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _import_module(void       *vframe,
                          const char *name,
                          unsigned    length,
                          int        *module_idx)
{
    KOS_CONTEXT ctx = (KOS_CONTEXT)vframe;
    KOS_OBJ_ID  module_obj;

    assert(module_idx);

    module_obj = kos_module_import(ctx, name, length, 0, 0, module_idx);

    return IS_BAD_PTR(module_obj) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _get_global_idx(void       *vframe,
                           int         module_idx,
                           const char *name,
                           unsigned    length,
                           int        *global_idx)
{
    int           error = KOS_SUCCESS;
    KOS_OBJ_ID    str;
    KOS_CONTEXT   ctx   = (KOS_CONTEXT)vframe;
    KOS_INSTANCE *inst  = ctx->inst;
    KOS_OBJ_ID    module_obj;
    KOS_OBJ_ID    glob_idx_obj;

    assert(module_idx >= 0);

    TRY(kos_seq_fail());

    str = KOS_new_string(ctx, name, length);
    TRY_OBJID(str);

    module_obj = KOS_array_read(ctx, inst->modules.modules, module_idx);
    TRY_OBJID(module_obj);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    glob_idx_obj = KOS_get_property(ctx, OBJPTR(MODULE, module_obj)->global_names, str);
    TRY_OBJID(glob_idx_obj);

    assert(IS_SMALL_INT(glob_idx_obj));

    *global_idx = (int)GET_SMALL_INT(glob_idx_obj);

cleanup:
    if (error) {
        KOS_clear_exception(ctx);
        error = KOS_ERROR_NOT_FOUND;
    }
    return error;
}

static int _walk_globals(void                          *vframe,
                         int                            module_idx,
                         KOS_COMP_WALL_GLOBALS_CALLBACK callback,
                         void                          *cookie)
{
    int                 error = KOS_SUCCESS;
    KOS_CONTEXT         ctx   = (KOS_CONTEXT)vframe;
    KOS_INSTANCE *const inst  = ctx->inst;
    KOS_VECTOR          name;
    KOS_OBJ_ID          walk;
    KOS_OBJ_ID          module_obj;

    kos_vector_init(&name);

    module_obj = KOS_array_read(ctx, inst->modules.modules, module_idx);
    TRY_OBJID(module_obj);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    walk = KOS_new_object_walk(ctx, OBJPTR(MODULE, module_obj)->global_names, KOS_SHALLOW);
    TRY_OBJID(walk);

    while ( ! KOS_object_walk(ctx, walk)) {

        assert(IS_SMALL_INT(KOS_get_walk_value(walk)));

        TRY(KOS_string_to_cstr_vec(ctx, KOS_get_walk_key(walk), &name));

        TRY(callback(name.buffer,
                     (unsigned)name.size - 1U,
                     module_idx,
                     (int)GET_SMALL_INT(KOS_get_walk_value(walk)),
                     cookie));
    }

cleanup:
    kos_vector_destroy(&name);

    return error;
}

static void _print_search_paths(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  paths)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;
    uint32_t   num_paths;
    uint32_t   i;

    static const char str_paths[] = "Kos module search paths: ";

    assert(GET_OBJ_TYPE(paths) == OBJ_ARRAY);

    kos_vector_init(&cstr);

    TRY(kos_vector_reserve(&cstr, 128));

    TRY(kos_vector_resize(&cstr, sizeof(str_paths)));

    memcpy(cstr.buffer, str_paths, sizeof(str_paths));

    num_paths = KOS_get_array_size(paths);

    for (i = 0; i < num_paths; i++) {

        static const char str_comma[] = ", ";

        KOS_OBJ_ID path = KOS_array_read(ctx, paths, (int)i);
        TRY_OBJID(path);

        assert(GET_OBJ_TYPE(path) == OBJ_STRING);

        TRY(KOS_object_to_string_or_cstr_vec(ctx, path, KOS_DONT_QUOTE, 0, &cstr));

        if (i + 1 < num_paths) {
            TRY(kos_vector_resize(&cstr, cstr.size + sizeof(str_comma) - 1));
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

    kos_vector_destroy(&cstr);
}

static void _print_load_info(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  module_name,
                             KOS_OBJ_ID  module_path)
{
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;

    static const char str_loading[] = "Kos loading module ";
    static const char str_from[]    = " from ";

    assert(GET_OBJ_TYPE(module_name) == OBJ_STRING);
    assert(GET_OBJ_TYPE(module_path) == OBJ_STRING);

    kos_vector_init(&cstr);

    TRY(kos_vector_reserve(&cstr, 128));

    TRY(kos_vector_resize(&cstr, sizeof(str_loading)));

    memcpy(cstr.buffer, str_loading, sizeof(str_loading));

    TRY(KOS_object_to_string_or_cstr_vec(ctx, module_name, KOS_DONT_QUOTE, 0, &cstr));

    TRY(kos_vector_resize(&cstr, cstr.size + sizeof(str_from) - 1));
    memcpy(cstr.buffer + cstr.size - sizeof(str_from), str_from, sizeof(str_from));

    TRY(KOS_object_to_string_or_cstr_vec(ctx, module_path, KOS_DONT_QUOTE, 0, &cstr));

cleanup:
    if (error) {
        KOS_clear_exception(ctx);
        printf("%sout of memory\n", str_loading);
    }
    else
        printf("%s\n", cstr.buffer);

    kos_vector_destroy(&cstr);
}

static int _append_buf(const uint8_t **dest,
                       uint32_t        dest_size,
                       const uint8_t  *src,
                       uint32_t        src_size)
{
    uint8_t *new_buf;

    assert(dest);
    assert(*dest);
    assert(dest_size);
    assert(src);
    assert(src_size);

    new_buf = (uint8_t *)kos_malloc(dest_size + src_size);
    if ( ! new_buf)
        return KOS_ERROR_OUT_OF_MEMORY;

    memcpy(new_buf, *dest, dest_size);
    memcpy(new_buf + dest_size, src, src_size);

    kos_free((void *)*dest);

    *dest = new_buf;

    return KOS_SUCCESS;
}

struct _PRINT_CONST_COOKIE {
    KOS_CONTEXT ctx;
    KOS_OBJ_ID  constants;
};

static int _print_const(void       *cookie,
                        KOS_VECTOR *cstr_buf,
                        uint32_t    const_index)
{
    struct _PRINT_CONST_COOKIE *data = (struct _PRINT_CONST_COOKIE *)cookie;
    KOS_OBJ_ID                  constant;

    constant = KOS_array_read(data->ctx, data->constants, const_index);

    if (IS_BAD_PTR(constant))
        return KOS_ERROR_EXCEPTION;

    return KOS_object_to_string_or_cstr_vec(data->ctx,
                                            constant,
                                            KOS_QUOTE_STRINGS,
                                            0,
                                            cstr_buf);
}

static int _compile_module(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  module_obj,
                           int         module_idx,
                           const char *data,
                           unsigned    data_size,
                           int         is_repl)
{
    int                 error             = KOS_SUCCESS;
    KOS_MODULE   *const module            = OBJPTR(MODULE, module_obj);
    KOS_INSTANCE *const inst              = ctx->inst;
    const uint32_t      old_bytecode_size = module->bytecode_size;
    KOS_PARSER          parser;
    KOS_COMP_UNIT       program;
    KOS_AST_NODE       *ast;

    /* Initialize parser and compiler */
    kos_compiler_init(&program, module_idx);
    if ( ! IS_BAD_PTR(module->constants))
        program.num_constants = (int)KOS_get_array_size(module->constants);
    kos_parser_init(&parser,
                    &program.allocator,
                    (unsigned)module_idx,
                    data,
                    data + data_size);

    /* Construct AST from source code */
    error = kos_parser_parse(&parser, &ast);

    if (error == KOS_ERROR_SCANNING_FAILED ||
        error == KOS_ERROR_PARSE_FAILED) {

        const KOS_FILE_POS pos = (error == KOS_ERROR_SCANNING_FAILED)
                                 ? parser.lexer.pos : parser.token.pos;
        KOS_OBJ_ID error_obj = _format_error(ctx,
                                             module_obj,
                                             data,
                                             data_size,
                                             parser.error_str,
                                             pos);
        assert( ! IS_BAD_PTR(error_obj) || KOS_is_exception_pending(ctx));
        if ( ! IS_BAD_PTR(error_obj))
            KOS_raise_exception(ctx, error_obj);

        error = KOS_ERROR_EXCEPTION;
    }
    TRY(error);

    /* Save base module index */
    if (module_idx == 0)
        TRY(KOS_array_write(ctx, inst->modules.modules, module_idx, module_obj));

    /* Prepare compiler */
    program.ctx            = ctx;
    program.import_module  = _import_module;
    program.get_global_idx = _get_global_idx;
    program.walk_globals   = _walk_globals;
    TRY(_predefine_globals(ctx, &program, module->global_names, module->module_names, is_repl));

    /* Compile source code into bytecode */
    error = kos_compiler_compile(&program, ast);

    if (error == KOS_ERROR_COMPILE_FAILED) {

        KOS_OBJ_ID error_obj = _format_error(ctx,
                                             module_obj,
                                             data,
                                             data_size,
                                             program.error_str,
                                             program.error_token->pos);
        assert( ! IS_BAD_PTR(error_obj) || KOS_is_exception_pending(ctx));
        if ( ! IS_BAD_PTR(error_obj))
            KOS_raise_exception(ctx, error_obj);

        error = KOS_ERROR_EXCEPTION;
    }
    TRY(error);

    TRY(_alloc_globals(ctx, &program, module));
    TRY(_alloc_constants(ctx, &program, module));
    TRY(_save_direct_modules(ctx, &program, module));

    /* Move compiled program to module */
    {
        KOS_VECTOR *code_buf     = &program.code_buf;
        KOS_VECTOR *addr_to_line = &program.addr2line_buf;
        KOS_VECTOR *addr_to_func = &program.addr2func_buf;

        const uint32_t old_num_line_addrs = module->num_line_addrs;
        const uint32_t old_num_func_addrs = module->num_func_addrs;

        /* REPL within the bounds of a module */
        if (old_bytecode_size) {

            KOS_LINE_ADDR *line_addr;
            KOS_LINE_ADDR *end_line_addr;
            KOS_FUNC_ADDR *func_addr;
            KOS_FUNC_ADDR *end_func_addr;

            assert(module->line_addrs);

            assert(module->bytecode_size);
            assert(code_buf->size);
            TRY(_append_buf(&module->bytecode, module->bytecode_size,
                            (const uint8_t *)code_buf->buffer, (uint32_t)code_buf->size));
            module->bytecode_size += (uint32_t)code_buf->size;

            if (addr_to_line->size) {
                assert(module->num_line_addrs);
                TRY(_append_buf((const uint8_t **)&module->line_addrs, module->num_line_addrs * sizeof(KOS_LINE_ADDR),
                                (const uint8_t *)addr_to_line->buffer, (uint32_t)addr_to_line->size));
                module->num_line_addrs += (uint32_t)(addr_to_line->size / sizeof(KOS_LINE_ADDR));
            }

            if (addr_to_func->size) {
                if (old_num_func_addrs) {
                    TRY(_append_buf((const uint8_t **)&module->func_addrs, module->num_func_addrs * sizeof(KOS_FUNC_ADDR),
                                    (const uint8_t *)addr_to_func->buffer, (uint32_t)addr_to_func->size));
                    module->num_func_addrs += (uint32_t)(addr_to_func->size / sizeof(KOS_FUNC_ADDR));
                }
                else {
                    module->func_addrs     = (KOS_FUNC_ADDR *)addr_to_func->buffer;
                    module->num_func_addrs = (uint32_t)(addr_to_func->size / sizeof(KOS_FUNC_ADDR));
                    module->flags         |= KOS_MODULE_OWN_FUNC_ADDRS;
                    addr_to_func->buffer   = 0;
                }
            }

            line_addr     = (KOS_LINE_ADDR *)module->line_addrs + old_num_line_addrs;
            end_line_addr = (KOS_LINE_ADDR *)module->line_addrs + module->num_line_addrs;
            for ( ; line_addr < end_line_addr; ++line_addr)
                line_addr->offs += old_bytecode_size;

            func_addr     = (KOS_FUNC_ADDR *)module->func_addrs + old_num_func_addrs;
            end_func_addr = (KOS_FUNC_ADDR *)module->func_addrs + module->num_func_addrs;
            for ( ; func_addr < end_func_addr; ++func_addr)
                func_addr->offs += old_bytecode_size;
        }
        /* New module */
        else {

            module->bytecode      = (uint8_t *)code_buf->buffer;
            module->bytecode_size = (uint32_t)code_buf->size;
            module->flags        |= KOS_MODULE_OWN_BYTECODE;
            code_buf->buffer      = 0;

            if (addr_to_line->size) {
                module->line_addrs     = (KOS_LINE_ADDR *)addr_to_line->buffer;
                module->num_line_addrs = (uint32_t)(addr_to_line->size / sizeof(KOS_LINE_ADDR));
                module->flags         |= KOS_MODULE_OWN_LINE_ADDRS;
                addr_to_line->buffer   = 0;
            }

            if (addr_to_func->size) {
                module->func_addrs     = (KOS_FUNC_ADDR *)addr_to_func->buffer;
                module->num_func_addrs = (uint32_t)(addr_to_func->size / sizeof(KOS_FUNC_ADDR));
                module->flags         |= KOS_MODULE_OWN_FUNC_ADDRS;
                addr_to_func->buffer   = 0;
            }
        }

        module->main_idx = program.cur_frame->constant->header.index;
    }

    /* Disassemble */
    if (inst->flags & KOS_INST_DISASM) {
        KOS_VECTOR         cname;
        KOS_VECTOR         ptrs;
        const char *const *func_names = 0;
        size_t             i_filename = 0;
        const char        *filename   = "";
        static const char  divider[]  =
                "==============================================================================";

        const KOS_FUNC_ADDR *const func_addrs     = module->func_addrs;
        const uint32_t             num_func_addrs = module->num_func_addrs;
        struct _PRINT_CONST_COOKIE print_const_cookie;

        kos_vector_init(&cname);
        kos_vector_init(&ptrs);

        error = KOS_string_to_cstr_vec(ctx, module->name, &cname);
        printf("\n");
        printf("%s\n", divider);
        if (!error) {
            printf("Disassembling module: %s\n", cname.buffer);
            printf("%s\n", divider);

            if (GET_OBJ_TYPE(module->path) == OBJ_STRING
                && KOS_get_string_length(module->path) > 0) {

                error = KOS_string_to_cstr_vec(ctx, module->path, &cname);
                if (!error) {
                    size_t i = cname.size - 2;
                    while (i > 0 && cname.buffer[i-1] != KOS_PATH_SEPARATOR)
                        --i;
                    i_filename = i;
                }
            }
            else
                i_filename = 0;
        }

        if (!error)
            error = kos_vector_resize(&ptrs, num_func_addrs * sizeof(void *));
        if (!error) {
            KOS_VECTOR buf;
            uint32_t   i;
            size_t     total_size = 0;
            char      *names      = 0;

            kos_vector_init(&buf);

            for (i = 0; i < num_func_addrs; i++) {
                const uint32_t   idx = func_addrs[i].str_idx;
                const KOS_OBJ_ID str = KOS_array_read(ctx, module->constants, (int)idx);
                if (IS_BAD_PTR(str)) {
                    error = KOS_ERROR_EXCEPTION;
                    break;
                }
                error = KOS_string_to_cstr_vec(ctx, str, &buf);
                if (error)
                    break;
                total_size += buf.size;
            }

            if (!error) {
                const size_t base = cname.size;
                error = kos_vector_resize(&cname, base + total_size);
                if (!error)
                    names = cname.buffer + base;
            }

            if (!error) {
                const char **func_names_new = (const char **)ptrs.buffer;
                for (i = 0; i < num_func_addrs; i++) {
                    const uint32_t   idx = func_addrs[i].str_idx;
                    const KOS_OBJ_ID str = KOS_array_read(ctx, module->constants, (int)idx);
                    if (IS_BAD_PTR(str)) {
                        error = KOS_ERROR_EXCEPTION;
                        break;
                    }
                    error = KOS_string_to_cstr_vec(ctx, str, &buf);
                    if (error)
                        break;
                    func_names_new[i] = names;
                    memcpy(names, buf.buffer, buf.size);
                    names += buf.size;
                }
            }

            kos_vector_destroy(&buf);
        }

        if (!error) {
            filename   = &cname.buffer[i_filename];
            func_names = (const char *const *)ptrs.buffer;
        }

        print_const_cookie.ctx       = ctx;
        print_const_cookie.constants = module->constants;

        kos_disassemble(filename,
                        old_bytecode_size,
                        module->bytecode,
                        module->bytecode_size,
                        (struct KOS_COMP_ADDR_TO_LINE_S *)module->line_addrs,
                        module->num_line_addrs,
                        func_names,
                        (struct KOS_COMP_ADDR_TO_FUNC_S *)func_addrs,
                        num_func_addrs,
                        _print_const,
                        &print_const_cookie);

        kos_vector_destroy(&ptrs);
        kos_vector_destroy(&cname);
        TRY(error);
    }

cleanup:
    kos_parser_destroy(&parser);
    kos_compiler_destroy(&program);
    return error;
}

static void _handle_interpreter_error(KOS_CONTEXT ctx, int error)
{
    if (error == KOS_ERROR_EXCEPTION)
        assert(KOS_is_exception_pending(ctx));
    else if (error == KOS_ERROR_OUT_OF_MEMORY) {
        if ( ! KOS_is_exception_pending(ctx))
            KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
    }
    else {
        if ( ! KOS_is_exception_pending(ctx))
            KOS_raise_exception_cstring(ctx, str_err_internal);
    }
}

KOS_OBJ_ID kos_module_import(KOS_CONTEXT ctx,
                             const char *module_name,
                             unsigned    name_size,
                             const char *data,
                             unsigned    data_size,
                             int        *out_module_idx)
{
    static const char             base[]             = "base";
    int                           error              = KOS_SUCCESS;
    int                           module_idx         = -1;
    KOS_OBJ_ID                    module_obj         = KOS_BADPTR;
    KOS_OBJ_ID                    actual_module_name = KOS_BADPTR;
    KOS_OBJ_ID                    module_dir         = KOS_BADPTR;
    KOS_OBJ_ID                    module_path        = KOS_BADPTR;
    KOS_OBJ_ID                    mod_init;
    KOS_OBJ_ID                    ret;
    KOS_OBJ_ID                    prev_locals;
    KOS_INSTANCE           *const inst               = ctx->inst;
    struct _KOS_MODULE_LOAD_CHAIN loading            = { 0, 0, 0 };
    KOS_VECTOR                    file_buf;
    int                           chain_init         = 0;
    int                           pushed             = 0;

    kos_vector_init(&file_buf);

    _get_module_name(module_name, name_size, &loading);

    TRY(KOS_push_local_scope(ctx, &prev_locals));
    pushed = 1;
    TRY(KOS_push_locals(ctx, 4, &module_obj, &actual_module_name, &module_dir, &module_path));

    /* Determine actual module name */
    actual_module_name = KOS_new_string(ctx, loading.module_name, loading.length);
    TRY_OBJID(actual_module_name);

    /* Find module source file */
    if (data) {
        module_dir  = KOS_get_string(ctx, KOS_STR_EMPTY);
        module_path = actual_module_name;
    }
    else
        error = _find_module(ctx,
                             actual_module_name,
                             module_name,
                             name_size,
                             (KOS_OBJ_ID *)&module_dir,
                             (KOS_OBJ_ID *)&module_path);
    if (error) {
        if (error == KOS_ERROR_NOT_FOUND) {
            _raise_3(ctx, str_err_module, actual_module_name, str_err_not_found);
            error = KOS_ERROR_EXCEPTION;
        }
        goto cleanup;
    }

    /* TODO use global mutex for thread safety */

    /* Load base module first, so that it ends up at index 0 */
    if (KOS_get_array_size(inst->modules.modules) == 0 && strcmp(module_name, base)) {
        int        base_idx;
        KOS_OBJ_ID path_array = KOS_BADPTR;
        KOS_OBJ_ID dir        = KOS_BADPTR;
        KOS_OBJ_ID base_obj;

        TRY(KOS_push_local(ctx, &path_array));
        TRY(KOS_push_local(ctx, &dir));

        /* Add search path - path of the topmost module being loaded */
        path_array = KOS_new_array(ctx, 1);
        TRY_OBJID(path_array);
        if (KOS_get_string_length(module_dir) == 0) {
            dir = KOS_new_const_ascii_string(ctx, str_cur_dir, sizeof(str_cur_dir) - 1);
            TRY_OBJID(dir);
        }
        else
            dir = module_dir;
        TRY(KOS_array_write(ctx, path_array, 0, dir));
        TRY(KOS_array_insert(ctx, inst->modules.search_paths, 0, 0, path_array, 0, 1));

        KOS_pop_local(ctx, &dir);
        KOS_pop_local(ctx, &path_array);

        if (inst->flags & KOS_INST_VERBOSE)
            _print_search_paths(ctx, inst->modules.search_paths);

        base_obj = kos_module_import(ctx, base, sizeof(base)-1, 0, 0, &base_idx);
        TRY_OBJID(base_obj);
        assert(base_idx == 0);
    }

    /* Add module to the load chain to prevent and detect circular dependencies */
    {
        struct _KOS_MODULE_LOAD_CHAIN *chain = inst->modules.load_chain;
        loading.next = chain;
        for ( ; chain; chain = chain->next) {
            if (loading.length == chain->length &&
                    0 == memcmp(loading.module_name, chain->module_name, loading.length)) {

                KOS_OBJ_ID name_str = KOS_BADPTR;

                TRY(KOS_push_local(ctx, &name_str));

                name_str = KOS_new_string(ctx, module_name, name_size);
                if (!IS_BAD_PTR(name_str))
                    _raise_3(ctx, str_err_circular_deps, name_str, str_err_end);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
        }
    }
    inst->modules.load_chain = &loading;
    chain_init               = 1;

    /* Return the module object if it was already loaded */
    {
        KOS_OBJ_ID module_idx_obj = KOS_get_property(ctx, inst->modules.module_names, actual_module_name);
        if (!IS_BAD_PTR(module_idx_obj)) {
            assert(IS_SMALL_INT(module_idx_obj));
            module_obj = KOS_array_read(ctx, inst->modules.modules, (int)GET_SMALL_INT(module_idx_obj));
            if (IS_BAD_PTR(module_obj))
                error = KOS_ERROR_EXCEPTION;
            else
                module_idx = (int)GET_SMALL_INT(module_idx_obj);
            goto cleanup;
        }
    }
    KOS_clear_exception(ctx);

    if (inst->flags & KOS_INST_VERBOSE)
        _print_load_info(ctx, actual_module_name, module_path);

    /* Make room for the new module and allocate index */
    {
        uint32_t u_idx = 0;
        TRY(KOS_array_push(ctx, inst->modules.modules, KOS_VOID, &u_idx));
        module_idx = (int)u_idx;
        assert(module_idx >= 0);
    }

    /* Allocate module object */
    module_obj = _alloc_module(ctx, actual_module_name);
    TRY_OBJID(module_obj);
    OBJPTR(MODULE, module_obj)->path = module_path;

    /* Load module file */
    if ( ! data) {
        TRY(_load_file(ctx, module_path, &file_buf));
        data      = file_buf.buffer;
        data_size = (unsigned)file_buf.size;
    }

    /* Run built-in module initialization */
    mod_init = KOS_get_property(ctx, inst->modules.module_inits, actual_module_name);

    if (IS_BAD_PTR(mod_init))
        KOS_clear_exception(ctx);
    else {
        KOS_OBJ_ID func_obj;

        TRY(KOS_push_local(ctx, &mod_init));

        func_obj = KOS_new_function(ctx);
        TRY_OBJID(func_obj);

        OBJPTR(FUNCTION, func_obj)->module = module_obj;

        TRY(kos_stack_push(ctx, func_obj));

        error = ((struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, mod_init))->init(ctx, module_obj);

        kos_stack_pop(ctx);

        KOS_pop_local(ctx, &mod_init);

        if (error) {
            assert( ! IS_BAD_PTR(ctx->exception));
            goto cleanup;
        }
    }

    /* Compile module source to bytecode */
    TRY(_compile_module(ctx, module_obj, module_idx, data, data_size, 0));

    /* Free file buffer */
    kos_vector_destroy(&file_buf);

    /* Put module on the list */
    TRY(KOS_array_write(ctx, inst->modules.modules, module_idx, module_obj));
    TRY(KOS_set_property(ctx, inst->modules.module_names, actual_module_name, TO_SMALL_INT(module_idx)));

    /* Run module */
    error = kos_vm_run_module(OBJPTR(MODULE, module_obj), &ret);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_raise_exception(ctx, ret);
    }

cleanup:
    if (chain_init)
        inst->modules.load_chain = loading.next;

    if (pushed)
        KOS_pop_local_scope(ctx, &prev_locals);

    kos_vector_destroy(&file_buf);

    /* TODO remove module from array if it failed to load? */

    if (error) {
        _handle_interpreter_error(ctx, error);
        module_obj = KOS_BADPTR;
    }
    else {
        *out_module_idx = module_idx;
        assert(!KOS_is_exception_pending(ctx));
    }

    return module_obj;
}

KOS_OBJ_ID KOS_repl(KOS_CONTEXT ctx,
                    const char *module_name,
                    const char *buf,
                    unsigned    buf_size)
{
    int           error       = KOS_SUCCESS;
    int           module_idx  = -1;
    KOS_OBJ_ID    module_obj  = KOS_BADPTR;
    KOS_OBJ_ID    module_name_str;
    KOS_OBJ_ID    ret         = KOS_BADPTR;
    KOS_INSTANCE *inst        = ctx->inst;
    KOS_OBJ_ID    prev_locals = KOS_BADPTR;
    int           pushed      = 0;

    TRY(KOS_push_local_scope(ctx, &prev_locals));
    pushed = 1;

    module_name_str = KOS_new_cstring(ctx, module_name);
    TRY_OBJID(module_name_str);

    /* Find module object */
    {
        KOS_OBJ_ID module_idx_obj = KOS_get_property(ctx, inst->modules.module_names, module_name_str);
        if (IS_BAD_PTR(module_idx_obj)) {
            _raise_3(ctx, str_err_module, module_name_str, str_err_not_found);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
        assert(IS_SMALL_INT(module_idx_obj));
        module_obj = KOS_array_read(ctx, inst->modules.modules, (int)GET_SMALL_INT(module_idx_obj));
        TRY_OBJID(module_obj);
        module_idx = (int)GET_SMALL_INT(module_idx_obj);
    }

    /* Compile evaluated source to bytecode */
    TRY(_compile_module(ctx, module_obj, module_idx, buf, buf_size, 1));

    /* Run module */
    error = kos_vm_run_module(OBJPTR(MODULE, module_obj), &ret);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_raise_exception(ctx, ret);
    }

cleanup:
    if (pushed)
        KOS_pop_local_scope(ctx, &prev_locals);

    if (error)
        _handle_interpreter_error(ctx, error);
    else {
        assert(!KOS_is_exception_pending(ctx));
    }

    return error ? KOS_BADPTR : ret;
}

static int _load_stdin(KOS_CONTEXT ctx, KOS_VECTOR *buf)
{
    int error = KOS_SUCCESS;

    TRY(kos_vector_resize(buf, 0));

    for (;;) {
        const size_t last_size = buf->size;
        size_t       num_read;

        TRY(kos_vector_resize(buf, last_size + _KOS_BUF_ALLOC_SIZE));

        num_read = fread(buf->buffer + last_size, 1, _KOS_BUF_ALLOC_SIZE, stdin);

        if (num_read < _KOS_BUF_ALLOC_SIZE) {

            TRY(kos_vector_resize(buf, last_size + num_read));

            if (ferror(stdin)) {
                KOS_raise_exception_cstring(ctx, str_err_stdin);
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

KOS_OBJ_ID KOS_repl_stdin(KOS_CONTEXT ctx,
                          const char *module_name)
{
    int                 error       = KOS_SUCCESS;
    int                 module_idx  = -1;
    KOS_OBJ_ID          module_obj  = KOS_BADPTR;
    KOS_OBJ_ID          module_name_str;
    KOS_OBJ_ID          ret         = KOS_BADPTR;
    KOS_OBJ_ID          prev_locals = KOS_BADPTR;
    KOS_INSTANCE *const inst        = ctx->inst;
    KOS_VECTOR          buf;
    int                 pushed      = 0;

    kos_vector_init(&buf);

    TRY(KOS_push_local_scope(ctx, &prev_locals));
    pushed = 1;

    TRY(_load_stdin(ctx, &buf));

    module_name_str = KOS_new_cstring(ctx, module_name);
    TRY_OBJID(module_name_str);

    /* Find module object */
    {
        KOS_OBJ_ID module_idx_obj = KOS_get_property(ctx, inst->modules.module_names, module_name_str);
        if (IS_BAD_PTR(module_idx_obj)) {
            _raise_3(ctx, str_err_module, module_name_str, str_err_not_found);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
        assert(IS_SMALL_INT(module_idx_obj));
        module_obj = KOS_array_read(ctx, inst->modules.modules, (int)GET_SMALL_INT(module_idx_obj));
        TRY_OBJID(module_obj);
        module_idx = (int)GET_SMALL_INT(module_idx_obj);
    }

    /* Compile evaluated source to bytecode */
    TRY(_compile_module(ctx, module_obj, module_idx, buf.buffer, (unsigned)buf.size, 1));

    /* Free the buffer */
    kos_vector_destroy(&buf);

    /* Run module */
    error = kos_vm_run_module(OBJPTR(MODULE, module_obj), &ret);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_raise_exception(ctx, ret);
    }

cleanup:
    if (error)
        _handle_interpreter_error(ctx, error);
    else {
        assert(!KOS_is_exception_pending(ctx));
    }

    if (pushed)
        KOS_pop_local_scope(ctx, &prev_locals);

    kos_vector_destroy(&buf);

    return error ? KOS_BADPTR : ret;
}

int KOS_module_add_global(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  module_obj,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID  value,
                          unsigned   *idx)
{
    int         error;
    uint32_t    new_idx;
    KOS_OBJ_ID  prop;
    KOS_MODULE* module = OBJPTR(MODULE, module_obj);

    assert(module);

    prop = KOS_get_property(ctx, module->global_names, name);

    KOS_clear_exception(ctx);

    if ( ! IS_BAD_PTR(prop)) {
        _raise_3(ctx, str_err_duplicate_global, name, str_err_end);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_array_push(ctx, module->globals, value, &new_idx));
    TRY(KOS_set_property(ctx, module->global_names, name, TO_SMALL_INT((int)new_idx)));

    if (idx)
        *idx = new_idx;

cleanup:
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

    idx_obj = KOS_get_property(ctx, module->global_names, name);
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
                            KOS_OBJ_ID           str_name,
                            KOS_FUNCTION_HANDLER handler,
                            int                  min_args,
                            KOS_FUNCTION_STATE   gen_state)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID func_obj = KOS_new_builtin_function(ctx, handler, min_args);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module_obj;
    OBJPTR(FUNCTION, func_obj)->state  = (uint8_t)gen_state;

    TRY(KOS_module_add_global(ctx,
                              module_obj,
                              str_name,
                              func_obj,
                              0));

cleanup:
    return error;
}

int KOS_module_add_constructor(KOS_CONTEXT          ctx,
                               KOS_OBJ_ID           module_obj,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER handler,
                               int                  min_args,
                               KOS_OBJ_ID          *ret_proto)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID func_obj = KOS_new_builtin_class(ctx, handler, min_args);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    TRY_OBJID(func_obj);

    OBJPTR(CLASS, func_obj)->module = module_obj;

    TRY(KOS_module_add_global(ctx,
                              module_obj,
                              str_name,
                              func_obj,
                              0));

    *ret_proto = KOS_atomic_read_obj(OBJPTR(CLASS, func_obj)->prototype);
    assert( ! IS_BAD_PTR(*ret_proto));

cleanup:
    return error;
}

int KOS_module_add_member_function(KOS_CONTEXT          ctx,
                                   KOS_OBJ_ID           module_obj,
                                   KOS_OBJ_ID           proto_obj,
                                   KOS_OBJ_ID           str_name,
                                   KOS_FUNCTION_HANDLER handler,
                                   int                  min_args,
                                   KOS_FUNCTION_STATE   gen_state)
{
    int        error    = KOS_SUCCESS;
    KOS_OBJ_ID func_obj = KOS_new_builtin_function(ctx, handler, min_args);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module_obj;
    OBJPTR(FUNCTION, func_obj)->state  = (uint8_t)gen_state;

    TRY(KOS_set_property(ctx, proto_obj, str_name, func_obj));

cleanup:
    return error;
}

unsigned KOS_module_addr_to_line(KOS_MODULE *module,
                                 uint32_t    offs)
{
    unsigned ret = 0;

    if (module && offs != ~0U && module->line_addrs) {

        const KOS_LINE_ADDR *ptr = module->line_addrs;
        const KOS_LINE_ADDR *end = ptr + module->num_line_addrs;

        while (ptr < end && offs > ptr->offs)
            ptr++;

        if (ptr >= end || offs < ptr->offs)
            ptr--;

        if (ptr < end) {

            assert(ptr >= module->line_addrs);
            assert(offs >= ptr->offs);

            ret = ptr->line;
        }
    }

    return ret;
}

static const KOS_FUNC_ADDR *_addr_to_func(KOS_MODULE *module,
                                          uint32_t    offs)
{
    const KOS_FUNC_ADDR *ret = 0;

    static const KOS_FUNC_ADDR global = {
        0,
        1,
        ~0U,
        0,
        0
    };

    if (module && offs != ~0U && module->func_addrs) {

        const KOS_FUNC_ADDR *ptr = module->func_addrs;
        const KOS_FUNC_ADDR *end = ptr + module->num_func_addrs;

        while (ptr < end && offs > ptr->offs)
            ptr++;

        if (ptr == end)
            ptr--;
        else if (ptr < end && offs < ptr->offs)
            ptr--;

        if (ptr < module->func_addrs)
            ret = &global;

        else if (ptr < end) {

            assert(offs >= ptr->offs);

            ret = ptr;
        }
    }

    return ret;
}

unsigned KOS_module_addr_to_func_line(KOS_MODULE *module,
                                      uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    unsigned line = 0;

    if (addr2func)
        line = addr2func->line;

    return line;
}

KOS_OBJ_ID KOS_module_addr_to_func_name(KOS_CONTEXT ctx,
                                        KOS_MODULE *module,
                                        uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    KOS_OBJ_ID ret = KOS_BADPTR;

    if (addr2func) {
        if (addr2func->str_idx == ~0U)
            ret = KOS_get_string(ctx, KOS_STR_GLOBAL);
        else
            ret = KOS_array_read(ctx, module->constants, (int)addr2func->str_idx);
    }

    return ret;
}

uint32_t KOS_module_func_get_num_instr(KOS_MODULE *module,
                                       uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    return addr2func ? addr2func->num_instr : 0U;
}

uint32_t KOS_module_func_get_code_size(KOS_MODULE *module,
                                       uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    return addr2func ? addr2func->code_size : 0U;
}

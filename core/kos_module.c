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

#include "../inc/kos_module.h"
#include "../inc/kos_array.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_compiler.h"
#include "kos_file.h"
#include "kos_malloc.h"
#include "kos_object_alloc.h"
#include "kos_object_internal.h"
#include "kos_parser.h"
#include "kos_try.h"
#include "kos_utf8.h"
#include "kos_vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_eol[]                  = "\n";
static const char str_err_circular_deps[]    = "circular dependencies detected for module \"";
static const char str_err_duplicate_global[] = "duplicate global \"";
static const char str_err_end[]              = "\"";
static const char str_err_internal[]         = "internal error";
static const char str_err_module[]           = "module \"";
static const char str_err_not_found[]        = "\" not found";
static const char str_err_out_of_memory[]    = "out of memory";
static const char str_err_unable_to_open[]   = "unable to open file \"";
static const char str_err_unable_to_read[]   = "unable to read file \"";
static const char str_format_colon[]         = ":";
static const char str_format_error[]         = ": error: ";
static const char str_global[]               = "<global>";
static const char str_path_sep[]             = KOS_PATH_SEPARATOR_STR;
static const char str_script_ext[]           = ".kos";

struct _KOS_MODULE_LOAD_CHAIN {
    struct _KOS_MODULE_LOAD_CHAIN *next;
    const char                    *module_name;
    unsigned                       length;
};

static void _raise_3(KOS_FRAME  frame,
                     KOS_OBJ_ID s1,
                     KOS_OBJ_ID s2,
                     KOS_OBJ_ID s3)
{
    KOS_OBJ_ID             str_err_full;
    KOS_ATOMIC(KOS_OBJ_ID) str_err[3];

    str_err[0] = s1;
    str_err[1] = s2;
    str_err[2] = s3;

    str_err_full = KOS_string_add_many(frame, str_err, 3);

    if (!IS_BAD_PTR(str_err_full))
        KOS_raise_exception(frame, str_err_full);
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

static int _find_module(KOS_FRAME   frame,
                        KOS_OBJ_ID  module_name,
                        const char *maybe_path,
                        unsigned    length,
                        KOS_OBJ_ID *out_abs_dir,
                        KOS_OBJ_ID *out_abs_path)
{
    int                error = KOS_ERROR_INTERNAL;
    KOS_OBJ_ID         path  = KOS_BADPTR;
    KOS_OBJ_ID         dir   = KOS_BADPTR;
    struct _KOS_VECTOR cpath;
    unsigned           i;

    _KOS_vector_init(&cpath);

    /* Find dot or path separator, it indicates it's a path to a file */
    if (_rfind_path(maybe_path, length, '.') > 0) {
        TRY(_KOS_vector_resize(&cpath, length+1));
        memcpy(cpath.buffer, maybe_path, length);
        cpath.buffer[length] = 0;

        TRY(_KOS_get_absolute_path(&cpath));

        if (!_KOS_does_file_exist(cpath.buffer))
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        path = KOS_new_string(frame, cpath.buffer, (unsigned)(cpath.size-1));
        TRY_OBJID(path);

        /* Find and skip last path separator */
        i = _rfind_path(cpath.buffer, (unsigned)(cpath.size-1), '/');
        if (i > 0)
            --i;

        dir = KOS_new_string(frame, cpath.buffer, i);
        TRY_OBJID(dir);
    }
    else {

        KOS_CONTEXT *ctx       = KOS_context_from_frame(frame);
        uint32_t     num_paths = KOS_get_array_size(ctx->module_search_paths);

        if (!num_paths)
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        for (i = 0; i < num_paths; i++) {
            KOS_ATOMIC(KOS_OBJ_ID) components[4];

            components[0] = KOS_array_read(frame, ctx->module_search_paths, (int)i);
            TRY_OBJID(components[0]);
            components[1] = KOS_context_get_cstring(frame, str_path_sep);
            components[2] = module_name;
            components[3] = KOS_context_get_cstring(frame, str_script_ext);

            path = KOS_string_add_many(frame, components, sizeof(components)/sizeof(components[0]));
            TRY_OBJID(path);

            TRY(KOS_string_to_cstr_vec(frame, path, &cpath));

            if (_KOS_does_file_exist(cpath.buffer)) {
                dir   = components[0];
                error = KOS_SUCCESS;
                break;
            }
        }

        if (IS_BAD_PTR(dir))
            error = KOS_ERROR_NOT_FOUND;
    }

_error:
    if (!error) {
        *out_abs_dir  = dir;
        *out_abs_path = path;
    }

    _KOS_vector_destroy(&cpath);

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

static KOS_OBJ_ID _alloc_module(KOS_FRAME  frame,
                                KOS_OBJ_ID module_name)
{
    const enum _KOS_AREA_TYPE alloc_mode = _KOS_alloc_get_mode(frame);
    KOS_MODULE               *module;

    _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);

    module = (KOS_MODULE *)_KOS_alloc_object(frame, MODULE);
    if (module) {
        module->type          = OBJ_MODULE;
        module->flags         = 0;
        module->name          = module_name;
        module->context       = KOS_context_from_frame(frame);
        module->strings       = 0;
        module->bytecode      = 0;
        module->bytecode_size = 0;
        module->instr_offs    = 0;
        module->num_regs      = 0;

        module->global_names = KOS_new_object(frame);
        if (IS_BAD_PTR(module->global_names))
            module = 0;
        else {
            module->globals = KOS_new_array(frame, 0);
            if (IS_BAD_PTR(module->globals))
                module = 0;
        }
    }

    _KOS_alloc_set_mode(frame, alloc_mode);

    return OBJID(MODULE, module);
}

static int _load_file(KOS_FRAME           frame,
                      KOS_OBJ_ID          path,
                      struct _KOS_VECTOR *file_buf)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpath;

    _KOS_vector_init(&cpath);
    TRY(KOS_string_to_cstr_vec(frame, path, &cpath));

    error = _KOS_load_file(cpath.buffer, file_buf);

    switch (error) {

        case KOS_ERROR_CANNOT_OPEN_FILE:
            _raise_3(frame, KOS_context_get_cstring(frame, str_err_unable_to_open), path, KOS_context_get_cstring(frame, str_err_end));
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_CANNOT_READ_FILE:
            _raise_3(frame, KOS_context_get_cstring(frame, str_err_unable_to_read), path, KOS_context_get_cstring(frame, str_err_end));
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_OUT_OF_MEMORY:
            RAISE_EXCEPTION(str_err_out_of_memory);

        default:
            break;
    }

_error:
    _KOS_vector_destroy(&cpath);

    return error;
}

static int _module_init_compare(void                       *what,
                                struct _KOS_RED_BLACK_NODE *node)
{
    KOS_OBJ_ID module_name = (KOS_OBJ_ID)what;

    struct _KOS_MODULE_INIT *mod_init = (struct _KOS_MODULE_INIT *)node;

    return KOS_string_compare(module_name, mod_init->name);
}

static int _predefine_globals(KOS_FRAME              frame,
                              struct _KOS_COMP_UNIT *program,
                              KOS_OBJ_ID             global_names)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpath;
    KOS_OBJECT_WALK    walk;

    _KOS_vector_init(&cpath);

    TRY(KOS_object_walk_init_shallow(frame, &walk, global_names));

    for (;;) {
        KOS_OBJECT_WALK_ELEM elem = KOS_object_walk(frame, &walk);
        if (IS_BAD_PTR(elem.key))
            break;

        assert(IS_SMALL_INT(elem.value));

        TRY(KOS_string_to_cstr_vec(frame, elem.key, &cpath));

        TRY(_KOS_compiler_predefine_global(program, cpath.buffer, (int)GET_SMALL_INT(elem.value)));
    }

_error:
    _KOS_vector_destroy(&cpath);

    return error;
}

static int _alloc_globals(KOS_FRAME              frame,
                          struct _KOS_COMP_UNIT *program,
                          KOS_MODULE            *module)
{
    int                       error;
    struct _KOS_VAR          *var;
    const enum _KOS_AREA_TYPE alloc_mode = _KOS_alloc_get_mode(frame);

    TRY(KOS_array_resize(frame, module->globals, (uint32_t)program->num_globals));

    for (var = program->globals; var; var = var->next) {
        KOS_OBJ_ID name;

        _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);
        name = KOS_new_string(frame, var->token->begin, var->token->length);
        _KOS_alloc_set_mode(frame, alloc_mode);

        TRY_OBJID(name);
        assert(var->array_idx < program->num_globals);
        TRY(KOS_set_property(frame, module->global_names, name, TO_SMALL_INT(var->array_idx)));
    }

_error:
    return error;
}

static uint32_t _count_strings(struct _KOS_COMP_UNIT *program)
{
    uint32_t i;

    struct _KOS_COMP_STRING *str = program->string_list;

    for (i = 0; str; str = str->next, ++i);

    return i;
}

static int _alloc_strings(KOS_FRAME              frame,
                          struct _KOS_COMP_UNIT *program,
                          KOS_MODULE            *module)
{
    int                       error       = KOS_SUCCESS;
    const uint32_t            num_strings = _count_strings(program);
    const enum _KOS_AREA_TYPE alloc_mode  = _KOS_alloc_get_mode(frame);
    struct _KOS_COMP_STRING  *str         = program->string_list;
    int                       i;

    _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);
    module->strings = KOS_new_array(frame, num_strings);
    _KOS_alloc_set_mode(frame, alloc_mode);

    TRY_OBJID(module->strings);

    for (i = 0; str; str = str->next, ++i) {

        KOS_OBJ_ID str_obj;

        _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);
        str_obj = str->escape == KOS_UTF8_WITH_ESCAPE
                ? KOS_new_string_esc(frame, str->str, str->length)
                : KOS_new_string(frame, str->str, str->length);
        _KOS_alloc_set_mode(frame, alloc_mode);

        TRY_OBJID(str_obj);

        TRY(KOS_array_write(frame, module->strings, i, str_obj));
    }

_error:
    return error;
}

static KOS_OBJ_ID _get_line(KOS_FRAME                 frame,
                            const struct _KOS_VECTOR *file_buf,
                            unsigned                  line)
{
    const char        *buf       = file_buf->buffer;
    const char        *const end = buf + file_buf->size;
    const char        *begin;
    unsigned           len       = 0;
    struct _KOS_VECTOR line_buf;
    KOS_OBJ_ID         ret       = KOS_BADPTR;

    _KOS_vector_init(&line_buf);

    /* Find the desired line */
    for ( ; line > 1 && buf < end; line--) {

        char c = 0;

        for ( ; buf < end; buf++) {

            c = *buf;

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
    if ( ! _KOS_vector_resize(&line_buf, len)) {

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

        ret = KOS_new_string(frame, line_buf.buffer, len);
    }

    _KOS_vector_destroy(&line_buf);

    return ret;
}

static KOS_OBJ_ID _format_error(KOS_FRAME                 frame,
                                KOS_OBJ_ID                module_obj,
                                const struct _KOS_VECTOR *file_buf,
                                const char               *error_str,
                                struct _KOS_FILE_POS      pos)
{
    int                    error = KOS_SUCCESS;
    KOS_OBJ_ID             ret   = KOS_BADPTR;
    KOS_ATOMIC(KOS_OBJ_ID) parts[11];
    struct _KOS_VECTOR     cstr;

    _KOS_vector_init(&cstr);

    parts[0] = KOS_get_file_name(frame, OBJPTR(MODULE, module_obj)->path);
    TRY_OBJID(parts[0]);

    parts[1] = KOS_context_get_cstring(frame, str_format_colon);

    parts[2] = KOS_object_to_string(frame, TO_SMALL_INT((int)pos.line));
    TRY_OBJID(parts[2]);

    parts[3] = KOS_context_get_cstring(frame, str_format_colon);

    parts[4] = KOS_object_to_string(frame, TO_SMALL_INT((int)pos.column));
    TRY_OBJID(parts[4]);

    parts[5] = KOS_context_get_cstring(frame, str_format_error);

    parts[6] = KOS_new_const_ascii_cstring(frame, error_str);
    TRY_OBJID(parts[6]);

    parts[7] = KOS_context_get_cstring(frame, str_eol);

    parts[8] = _get_line(frame, file_buf, pos.line);
    TRY_OBJID(parts[8]);

    parts[9] = KOS_context_get_cstring(frame, str_eol);

    error = _KOS_vector_resize(&cstr, pos.column);
    if (error)
        RAISE_EXCEPTION(str_err_out_of_memory);

    memset(cstr.buffer, ' ', pos.column-1);
    cstr.buffer[pos.column-1] = '^';

    parts[10] = KOS_new_string(frame, cstr.buffer, pos.column);
    TRY_OBJID(parts[10]);

    ret = KOS_string_add_many(frame, parts, sizeof(parts)/sizeof(parts[0]));

_error:
    if (error)
        ret = KOS_BADPTR;

    _KOS_vector_destroy(&cstr);

    return ret;
}

int KOS_load_module(KOS_FRAME frame, const char *path)
{
    int        idx;
    KOS_OBJ_ID module = _KOS_module_import(frame, path, (unsigned)strlen(path), KOS_MODULE_MANDATORY, &idx);

    return IS_BAD_PTR(module) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _import_module(void                   *vframe,
                          const char             *name,
                          unsigned                length,
                          enum _KOS_COMP_REQUIRED required,
                          int                     *module_idx)
{
    KOS_FRAME  frame = (KOS_FRAME)vframe;
    KOS_OBJ_ID module_obj;

    module_obj = _KOS_module_import(frame, name, length, (enum _KOS_MODULE_REQUIRED)required, module_idx);

    return IS_BAD_PTR(module_obj) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _get_global_idx(void       *vframe,
                           int         module_idx,
                           const char *name,
                           unsigned    length,
                           int        *global_idx)
{
    int          error = KOS_SUCCESS;
    KOS_OBJ_ID   str;
    KOS_FRAME    frame = (KOS_FRAME)vframe;
    KOS_CONTEXT *ctx   = KOS_context_from_frame(frame);
    KOS_OBJ_ID   module_obj;
    KOS_OBJ_ID   glob_idx_obj;

    assert(module_idx >= 0);

    str = KOS_new_string(frame, name, length);
    TRY_OBJID(str);

    module_obj = KOS_array_read(frame, ctx->modules, module_idx);
    TRY_OBJID(module_obj);

    assert(GET_OBJ_TYPE(module_obj) == OBJ_INTERNAL);
    assert(OBJPTR(MODULE, module_obj)->type == OBJ_MODULE);

    glob_idx_obj = KOS_get_property(frame, OBJPTR(MODULE, module_obj)->global_names, str);
    TRY_OBJID(glob_idx_obj);

    assert(IS_SMALL_INT(glob_idx_obj));

    *global_idx = (int)GET_SMALL_INT(glob_idx_obj);

_error:
    if (error) {
        KOS_clear_exception(frame);
        error = KOS_ERROR_NOT_FOUND;
    }
    return error;
}

KOS_OBJ_ID _KOS_module_import(KOS_FRAME                 frame,
                              const char               *module_name,
                              unsigned                  length,
                              enum _KOS_MODULE_REQUIRED required,
                              int                      *out_module_idx)
{
    static const char             lang[]          = "lang";
    int                           error           = KOS_SUCCESS;
    int                           module_idx      = -1;
    KOS_OBJ_ID                    module_obj      = KOS_BADPTR;
    KOS_OBJ_ID                    actual_module_name;
    KOS_OBJ_ID                    module_dir      = KOS_BADPTR;
    KOS_OBJ_ID                    module_path     = KOS_BADPTR;
    KOS_OBJ_ID                    ret;
    KOS_CONTEXT                  *ctx             = KOS_context_from_frame(frame);
    struct _KOS_MODULE_LOAD_CHAIN loading         = { 0, 0, 0 };
    struct _KOS_MODULE_INIT      *mod_init;
    struct _KOS_VECTOR            file_buf;
    struct _KOS_PARSER            parser;
    struct _KOS_COMP_UNIT         program;
    struct _KOS_AST_NODE         *ast;
    int                           search_path_set = 0;
    int                           chain_init      = 0;
    int                           compiler_init   = 0;

    _KOS_vector_init(&file_buf);

    _get_module_name(module_name, length, &loading);

    /* Determine actual module name */
    actual_module_name = KOS_new_string(frame, loading.module_name, loading.length);
    TRY_OBJID(actual_module_name);

    /* Find module source file */
    error = _find_module(frame, actual_module_name, module_name, length, &module_dir, &module_path);
    if (error) {
        if (error == KOS_ERROR_NOT_FOUND) {
            if (required == KOS_MODULE_MANDATORY) {
                _raise_3(frame, KOS_context_get_cstring(frame, str_err_module), actual_module_name, KOS_context_get_cstring(frame, str_err_not_found));
                error = KOS_ERROR_EXCEPTION;
            }
            else
                error = KOS_SUCCESS;
        }
        goto _error;
    }

    /* TODO use global mutex for thread safety */

    /* Load lang module first, so that it ends up at index 0 */
    if (KOS_get_array_size(ctx->modules) == 0 && module_name != lang) {
        int        lang_idx;
        KOS_OBJ_ID path_array;
        KOS_OBJ_ID lang_obj;

        /* Add search path - path of the topmost module being loaded */
        path_array = KOS_new_array(frame, 1);
        TRY(KOS_array_write(frame, path_array, 0, module_dir));
        TRY(KOS_array_insert(frame, ctx->module_search_paths, 0, 0, path_array, 0, 1));
        search_path_set = 1;

        lang_obj = _KOS_module_import(frame, lang, sizeof(lang)-1, KOS_MODULE_MANDATORY, &lang_idx);
        TRY_OBJID(lang_obj);
        assert(lang_idx == 0);
    }

    /* Add module to the load chain to prevent and detect circular dependencies */
    {
        struct _KOS_MODULE_LOAD_CHAIN *chain = ctx->module_load_chain;
        loading.next = chain;
        for ( ; chain; chain = chain->next) {
            if (loading.length == chain->length &&
                    0 == memcmp(loading.module_name, chain->module_name, loading.length)) {

                KOS_OBJ_ID name_str = KOS_new_string(frame, module_name, length);
                if (!IS_BAD_PTR(name_str))
                    _raise_3(frame, KOS_context_get_cstring(frame, str_err_circular_deps), name_str, KOS_context_get_cstring(frame, str_err_end));
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
        }
    }
    ctx->module_load_chain = &loading;
    chain_init             = 1;

    /* Return the module object if it was already loaded */
    {
        KOS_OBJ_ID module_idx_obj = KOS_get_property(frame, ctx->module_names, actual_module_name);
        if (!IS_BAD_PTR(module_idx_obj)) {
            assert(IS_SMALL_INT(module_idx_obj));
            module_obj = KOS_array_read(frame, ctx->modules, (int)GET_SMALL_INT(module_idx_obj));
            if (IS_BAD_PTR(module_obj))
                error = KOS_ERROR_EXCEPTION;
            else
                module_idx = (int)GET_SMALL_INT(module_idx_obj);
            goto _error;
        }
    }
    KOS_clear_exception(frame);

    /* Make room for the new module and allocate index */
    {
        uint32_t u_idx = 0;
        TRY(KOS_array_push(frame, ctx->modules, KOS_VOID, &u_idx));
        module_idx = (int)u_idx;
        assert(module_idx >= 0);
    }

    /* Allocate module object */
    module_obj = _alloc_module(frame, actual_module_name);
    TRY_OBJID(module_obj);
    OBJPTR(MODULE, module_obj)->path = module_path;

    /* Load module file */
    TRY(_load_file(frame, OBJPTR(MODULE, module_obj)->path, &file_buf));

    /* Run built-in module initialization */
    mod_init = (struct _KOS_MODULE_INIT *)_KOS_red_black_find(ctx->module_inits,
                                                              actual_module_name,
                                                              _module_init_compare);
    if (mod_init) {
        KOS_FRAME mod_frame = _KOS_stack_frame_push(frame,
                                                    OBJPTR(MODULE, module_obj),
                                                    0,
                                                    0);
        TRY(mod_init->init(mod_frame));
    }

    /* Initialize parser and compiler */
    _KOS_compiler_init(&program, module_idx);
    _KOS_parser_init(&parser,
                     &program.allocator,
                     (unsigned)module_idx,
                     file_buf.buffer,
                     file_buf.buffer + file_buf.size);
    compiler_init = 1;

    /* Construct AST from source code */
    error = _KOS_parser_parse(&parser, &ast);

    if (error == KOS_ERROR_SCANNING_FAILED ||
        error == KOS_ERROR_PARSE_FAILED) {

        const struct _KOS_FILE_POS pos = (error == KOS_ERROR_SCANNING_FAILED)
                                         ? parser.lexer.pos : parser.token.pos;
        KOS_OBJ_ID error_obj = _format_error(frame,
                                             module_obj,
                                             &file_buf,
                                             parser.error_str,
                                             pos);
        if (IS_BAD_PTR(error_obj)) {
            assert(KOS_is_exception_pending(frame));
        }
        else
            KOS_raise_exception(frame, error_obj);

        error = KOS_ERROR_EXCEPTION;
    }

    TRY(error);

    /* Save lang module index */
    if (module_idx == 0)
        TRY(KOS_array_write(frame, ctx->modules, module_idx, module_obj));

    /* Prepare compiler */
    program.frame          = frame;
    program.import_module  = _import_module;
    program.get_global_idx = _get_global_idx;
    TRY(_predefine_globals(frame, &program, OBJPTR(MODULE, module_obj)->global_names));

    /* Compile source code into bytecode */
    error = _KOS_compiler_compile(&program, ast);

    if (error == KOS_ERROR_COMPILE_FAILED) {

        KOS_OBJ_ID error_obj = _format_error(frame,
                                             module_obj,
                                             &file_buf,
                                             program.error_str,
                                             program.error_token->pos);
        if (IS_BAD_PTR(error_obj)) {
            assert(KOS_is_exception_pending(frame));
        }
        else
            KOS_raise_exception(frame, error_obj);
    }

    TRY(error);

    TRY(_alloc_globals(frame, &program, OBJPTR(MODULE, module_obj)));
    TRY(_alloc_strings(frame, &program, OBJPTR(MODULE, module_obj)));

    /* Move compiled program to module */
    {
        KOS_MODULE         *module       = OBJPTR(MODULE, module_obj);
        struct _KOS_VECTOR *code_buf     = &program.code_buf;
        struct _KOS_VECTOR *addr_to_line = &program.addr2line_buf;
        struct _KOS_VECTOR *addr_to_func = &program.addr2func_buf;

        module->bytecode      = (uint8_t *)code_buf->buffer;
        module->bytecode_size = (uint32_t)code_buf->size;
        module->flags        |= KOS_MODULE_OWN_BYTECODE;
        code_buf->buffer      = 0;

        module->line_addrs     = (KOS_LINE_ADDR *)addr_to_line->buffer;
        module->num_line_addrs = (uint32_t)(addr_to_line->size / sizeof(KOS_LINE_ADDR));
        module->flags         |= KOS_MODULE_OWN_LINE_ADDRS;
        addr_to_line->buffer   = 0;

        module->func_addrs     = (KOS_FUNC_ADDR *)addr_to_func->buffer;
        module->num_func_addrs = (uint32_t)(addr_to_func->size / sizeof(KOS_FUNC_ADDR));
        module->flags         |= KOS_MODULE_OWN_FUNC_ADDRS;
        addr_to_func->buffer   = 0;

        module->num_regs = (uint16_t)program.cur_frame->num_regs;

        /* Disassemble */
        if (ctx->flags & KOS_CTX_DEBUG) {
            struct _KOS_VECTOR cname;
            struct _KOS_VECTOR ptrs;
            const char *const *func_names = 0;
            size_t             i_filename = 0;
            const char        *filename   = "";
            static const char  divider[]  =
                    "==============================================================================";

            _KOS_vector_init(&cname);
            _KOS_vector_init(&ptrs);

            error = KOS_string_to_cstr_vec(frame, module->name, &cname);
            printf("\n");
            printf("%s\n", divider);
            if (!error) {
                printf("Disassembling module: %s\n", cname.buffer);
                printf("%s\n", divider);

                error = KOS_string_to_cstr_vec(frame, module->path, &cname);
                if (!error) {
                    size_t i = cname.size - 2;
                    while (i > 0 && cname.buffer[i-1] != KOS_PATH_SEPARATOR)
                        --i;
                    i_filename = i;
                }
            }

            if (!error)
                error = _KOS_vector_resize(&ptrs, module->num_func_addrs * sizeof(void *));
            if (!error) {
                struct _KOS_VECTOR buf;
                uint32_t           i;
                size_t             total_size = 0;
                char              *names      = 0;

                _KOS_vector_init(&buf);

                for (i = 0; i < module->num_func_addrs; i++) {
                    const uint32_t   idx = module->func_addrs[i].str_idx;
                    const KOS_OBJ_ID str = KOS_array_read(frame, module->strings, (int)idx);
                    if (IS_BAD_PTR(str)) {
                        error = KOS_ERROR_EXCEPTION;
                        break;
                    }
                    error = KOS_string_to_cstr_vec(frame, str, &buf);
                    if (error)
                        break;
                    total_size += buf.size;
                }

                if (!error) {
                    const size_t base = cname.size;
                    error = _KOS_vector_resize(&cname, base + total_size);
                    if (!error)
                        names = cname.buffer + base;
                }

                if (!error) {
                    const char **func_names_new = (const char **)ptrs.buffer;
                    for (i = 0; i < module->num_func_addrs; i++) {
                        const uint32_t   idx = module->func_addrs[i].str_idx;
                        const KOS_OBJ_ID str = KOS_array_read(frame, module->strings, (int)idx);
                        if (IS_BAD_PTR(str)) {
                            error = KOS_ERROR_EXCEPTION;
                            break;
                        }
                        error = KOS_string_to_cstr_vec(frame, str, &buf);
                        if (error)
                            break;
                        func_names_new[i] = names;
                        memcpy(names, buf.buffer, buf.size);
                        names += buf.size;
                    }
                }

                _KOS_vector_destroy(&buf);
            }

            if (!error) {
                filename   = &cname.buffer[i_filename];
                func_names = (const char *const *)ptrs.buffer;
            }

            _KOS_disassemble(filename,
                             module->bytecode,
                             module->bytecode_size,
                             (struct _KOS_COMP_ADDR_TO_LINE *)module->line_addrs,
                             module->num_line_addrs,
                             func_names,
                             (struct _KOS_COMP_ADDR_TO_FUNC *)module->func_addrs,
                             module->num_func_addrs);

            _KOS_vector_destroy(&ptrs);
            _KOS_vector_destroy(&cname);
            TRY(error);
        }
    }

    _KOS_parser_destroy(&parser);
    _KOS_compiler_destroy(&program);
    compiler_init = 0;

    /* Put module on the list */
    TRY(KOS_array_write(frame, ctx->modules, module_idx, module_obj));
    TRY(KOS_set_property(frame, ctx->module_names, actual_module_name, TO_SMALL_INT(module_idx)));

    /* Run module */
    error = _KOS_vm_run_module(OBJPTR(MODULE, module_obj), &ret);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_raise_exception(frame, ret);
    }

_error:
    if (search_path_set) {
        const uint32_t num_paths = KOS_get_array_size(ctx->module_search_paths);
        int            err;
        assert(num_paths > 0);
        err = KOS_array_resize(frame, ctx->module_search_paths, num_paths-1);
        if ( ! error)
            error = err;
    }
    if (compiler_init) {
        _KOS_parser_destroy(&parser);
        _KOS_compiler_destroy(&program);
    }
    if (chain_init)
        ctx->module_load_chain = loading.next;
    _KOS_vector_destroy(&file_buf);

    if (error) {
        if (error == KOS_ERROR_EXCEPTION)
            assert(KOS_is_exception_pending(frame));
        else if (error == KOS_ERROR_OUT_OF_MEMORY) {
            if ( ! KOS_is_exception_pending(frame))
                KOS_raise_exception_cstring(frame, str_err_out_of_memory);
        }
        else {
            if ( ! KOS_is_exception_pending(frame))
                KOS_raise_exception_cstring(frame, str_err_internal);
        }
        module_obj = KOS_BADPTR;
    }
    else {
        *out_module_idx = module_idx;
        assert(!KOS_is_exception_pending(frame));
    }

    return module_obj;
}

int KOS_module_add_global(KOS_FRAME  frame,
                          KOS_OBJ_ID name,
                          KOS_OBJ_ID value,
                          unsigned  *idx)
{
    int         error;
    uint32_t    new_idx;
    KOS_OBJ_ID  prop;
    KOS_MODULE *module = frame->module;

    assert(module);

    prop = KOS_get_property(frame, module->global_names, name);

    KOS_clear_exception(frame);

    if ( ! IS_BAD_PTR(prop)) {
        _raise_3(frame, KOS_context_get_cstring(frame, str_err_duplicate_global), name, KOS_context_get_cstring(frame, str_err_end));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    TRY(KOS_array_push(frame, module->globals, value, &new_idx));
    TRY(KOS_set_property(frame, module->global_names, name, TO_SMALL_INT((int)new_idx)));

    if (idx)
        *idx = new_idx;

_error:
    return error;
}

int KOS_module_get_global(KOS_FRAME   frame,
                          KOS_OBJ_ID  name,
                          KOS_OBJ_ID *value,
                          unsigned   *idx)
{
    int         error  = KOS_SUCCESS;
    KOS_OBJ_ID  idx_obj;
    KOS_OBJ_ID  ret;
    KOS_MODULE *module = frame->module;

    assert(module);

    idx_obj = KOS_get_property(frame, module->global_names, name);
    TRY_OBJID(idx_obj);

    assert(IS_SMALL_INT(idx_obj));

    ret = KOS_array_read(frame, module->globals, (int)GET_SMALL_INT(idx_obj));
    TRY_OBJID(ret);

    *value = ret;
    if (idx)
        *idx = (unsigned)GET_SMALL_INT(idx_obj);

_error:
    return error;
}

int KOS_module_add_function(KOS_FRAME                 frame,
                            KOS_OBJ_ID                str_name,
                            KOS_FUNCTION_HANDLER      handler,
                            int                       min_args,
                            enum _KOS_GENERATOR_STATE gen_state)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_ID  func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE *module   = frame->module;

    assert(module);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module          = module;
    OBJPTR(FUNCTION, func_obj)->generator_state = (uint8_t)gen_state;

    TRY(KOS_module_add_global(frame,
                              str_name,
                              func_obj,
                              0));

_error:
    return error;
}

int KOS_module_add_constructor(KOS_FRAME            frame,
                               KOS_OBJ_ID           str_name,
                               KOS_FUNCTION_HANDLER handler,
                               int                  min_args,
                               KOS_OBJ_ID          *ret_proto)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_ID  func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE *module   = frame->module;

    assert(module);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module = module;

    TRY(KOS_module_add_global(frame,
                              str_name,
                              func_obj,
                              0));

    *ret_proto = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(FUNCTION, func_obj)->prototype);
    assert( ! IS_BAD_PTR(*ret_proto));

_error:
    return error;
}

int KOS_module_add_member_function(KOS_FRAME                 frame,
                                   KOS_OBJ_ID                proto_obj,
                                   KOS_OBJ_ID                str_name,
                                   KOS_FUNCTION_HANDLER      handler,
                                   int                       min_args,
                                   enum _KOS_GENERATOR_STATE gen_state)
{
    int          error    = KOS_SUCCESS;
    KOS_OBJ_ID   func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE  *module   = frame->module;

    assert(module);

    TRY_OBJID(func_obj);

    OBJPTR(FUNCTION, func_obj)->module          = module;
    OBJPTR(FUNCTION, func_obj)->generator_state = (uint8_t)gen_state;

    TRY(KOS_set_property(frame, proto_obj, str_name, func_obj));

_error:
    return error;
}

unsigned KOS_module_addr_to_line(KOS_MODULE *module,
                                 uint32_t    offs)
{
    unsigned ret = 0;

    if (module && offs != ~0U) {

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

    if (module && offs != ~0U) {

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

KOS_OBJ_ID KOS_module_addr_to_func_name(KOS_MODULE *module,
                                        uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    KOS_OBJ_ID ret = KOS_BADPTR;

    if (addr2func) {
        KOS_FRAME frame = &module->context->main_thread.frame;
        if (addr2func->str_idx == ~0U)
            ret = KOS_context_get_cstring(frame, str_global);
        else
            ret = KOS_array_read(frame, module->strings, (int)addr2func->str_idx);
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

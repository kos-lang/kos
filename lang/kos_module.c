/*
 * Copyright (c) 2014-2016 Chris Dragan
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

static KOS_ASCII_STRING(str_eol,                  "\n");
static KOS_ASCII_STRING(str_err_circular_deps,    "circular dependencies detected for module \"");
static KOS_ASCII_STRING(str_err_duplicate_global, "duplicate global \"");
static KOS_ASCII_STRING(str_err_end,              "\"");
static KOS_ASCII_STRING(str_err_internal,         "internal error");
static KOS_ASCII_STRING(str_err_invalid_utf8,     "invalid UTF-8 character");
static KOS_ASCII_STRING(str_err_module,           "module \"");
static KOS_ASCII_STRING(str_err_not_found,        "\" not found");
static KOS_ASCII_STRING(str_err_out_of_memory,    "out of memory");
static KOS_ASCII_STRING(str_err_unable_to_open,   "unable to open file \"");
static KOS_ASCII_STRING(str_err_unable_to_read,   "unable to read file \"");
static KOS_ASCII_STRING(str_format_colon,         ":");
static KOS_ASCII_STRING(str_format_error,         ": error: ");
static KOS_ASCII_STRING(str_global,               "<global>");
static KOS_ASCII_STRING(str_path_sep,             KOS_PATH_SEPARATOR_STR);
static KOS_ASCII_STRING(str_script_ext,           ".kos");

struct _KOS_MODULE_LOAD_CHAIN {
    struct _KOS_MODULE_LOAD_CHAIN *next;
    const char                    *module_name;
    unsigned                       length;
};

static void _raise_3(KOS_STACK_FRAME *frame,
                     KOS_OBJ_PTR      s1,
                     KOS_OBJ_PTR      s2,
                     KOS_OBJ_PTR      s3)
{
    KOS_OBJ_PTR             str_err_full;
    KOS_ATOMIC(KOS_OBJ_PTR) str_err[3];

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

static int _find_module(KOS_STACK_FRAME *frame,
                        KOS_OBJ_PTR      module_name,
                        const char      *maybe_path,
                        unsigned         length,
                        KOS_OBJ_PTR     *out_abs_dir,
                        KOS_OBJ_PTR     *out_abs_path)
{
    int                error = KOS_ERROR_INTERNAL;
    KOS_OBJ_PTR        path  = TO_OBJPTR(0);
    KOS_OBJ_PTR        dir   = TO_OBJPTR(0);
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
            TRY(KOS_ERROR_NOT_FOUND);

        path = KOS_new_string(frame, cpath.buffer, (unsigned)(cpath.size-1));
        if (IS_BAD_PTR(path))
            TRY(KOS_ERROR_EXCEPTION);

        /* Find and skip last path separator */
        i = _rfind_path(cpath.buffer, (unsigned)(cpath.size-1), '/');
        if (i > 0)
            --i;

        dir = KOS_new_string(frame, cpath.buffer, i);
        if (IS_BAD_PTR(dir))
            TRY(KOS_ERROR_EXCEPTION);
    }
    else {

        KOS_CONTEXT *ctx;
        uint32_t    num_paths;

        assert( ! IS_BAD_PTR(frame->module));
        assert(OBJPTR(KOS_MODULE, frame->module)->context);

        ctx       = OBJPTR(KOS_MODULE, frame->module)->context;
        num_paths = KOS_get_array_size(TO_OBJPTR(&ctx->module_search_paths));

        if (!num_paths)
            TRY(KOS_ERROR_NOT_FOUND);

        for (i = 0; i < num_paths; i++) {
            KOS_ATOMIC(KOS_OBJ_PTR) components[4];

            components[0] = KOS_array_read(frame, TO_OBJPTR(&ctx->module_search_paths), (int)i);
            if (IS_BAD_PTR(components[0]))
                TRY(KOS_ERROR_EXCEPTION);
            components[1] = TO_OBJPTR(&str_path_sep);
            components[2] = module_name;
            components[3] = TO_OBJPTR(&str_script_ext);

            path = KOS_string_add_many(frame, components, sizeof(components)/sizeof(components[0]));
            if (IS_BAD_PTR(path))
                TRY(KOS_ERROR_EXCEPTION);

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

static KOS_OBJ_PTR _alloc_module(KOS_STACK_FRAME *frame,
                                 KOS_OBJ_PTR      module_name)
{
    KOS_ANY_OBJECT *obj = _KOS_alloc_object(frame, KOS_MODULE);
    if (obj) {
        assert( ! IS_BAD_PTR(frame->module));
        assert(OBJPTR(KOS_MODULE, frame->module)->context);

        obj->type                 = OBJ_MODULE;
        obj->module.flags         = 0;
        obj->module.name          = module_name;
        obj->module.context       = OBJPTR(KOS_MODULE, frame->module)->context;
        obj->module.strings       = 0;
        obj->module.bytecode      = 0;
        obj->module.bytecode_size = 0;
        obj->module.instr_offs    = 0;
        obj->module.num_regs      = 0;

        obj->module.global_names = KOS_new_object(frame);
        if (IS_BAD_PTR(obj->module.global_names))
            obj = 0; /* object is garbage-collected */
        else {
            obj->module.globals = KOS_new_array(frame, 0);
            if (IS_BAD_PTR(obj->module.globals))
                obj = 0; /* object is garbage-collected */
        }
    }

    return TO_OBJPTR(obj);
}

static int _load_file(KOS_STACK_FRAME    *frame,
                      KOS_OBJ_PTR         path,
                      struct _KOS_VECTOR *file_buf)
{
    int                error = KOS_SUCCESS;
    struct _KOS_VECTOR cpath;

    _KOS_vector_init(&cpath);
    TRY(KOS_string_to_cstr_vec(frame, path, &cpath));

    error = _KOS_load_file(cpath.buffer, file_buf);

    switch (error) {

        case KOS_ERROR_CANNOT_OPEN_FILE:
            _raise_3(frame, TO_OBJPTR(&str_err_unable_to_open), path, TO_OBJPTR(&str_err_end));
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_CANNOT_READ_FILE:
            _raise_3(frame, TO_OBJPTR(&str_err_unable_to_read), path, TO_OBJPTR(&str_err_end));
            error = KOS_ERROR_EXCEPTION;
            break;

        case KOS_ERROR_OUT_OF_MEMORY:
            KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
            error = KOS_ERROR_EXCEPTION;
            break;

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
    KOS_OBJ_PTR module_name = (KOS_OBJ_PTR)what;

    struct _KOS_MODULE_INIT *mod_init = (struct _KOS_MODULE_INIT *)node;

    return KOS_string_compare(module_name, mod_init->name);
}

static int _predefine_globals(KOS_STACK_FRAME       *frame,
                              struct _KOS_COMP_UNIT *program,
                              KOS_OBJ_PTR            global_names)
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

        assert(!IS_BAD_PTR(elem.value) && IS_SMALL_INT(elem.value));

        TRY(KOS_string_to_cstr_vec(frame, elem.key, &cpath));

        TRY(_KOS_compiler_predefine_global(program, cpath.buffer, (int)GET_SMALL_INT(elem.value)));
    }

_error:
    _KOS_vector_destroy(&cpath);

    return error;
}

static int _alloc_globals(KOS_STACK_FRAME       *frame,
                          struct _KOS_COMP_UNIT *program,
                          KOS_MODULE            *module)
{
    int              error;
    struct _KOS_VAR *var;

    TRY(KOS_array_resize(frame, module->globals, (uint32_t)program->num_globals));

    for (var = program->globals; var; var = var->next) {
        KOS_OBJ_PTR name = KOS_new_string(frame, var->token->begin, var->token->length);
        if (IS_BAD_PTR(name))
            TRY(KOS_ERROR_EXCEPTION);
        assert(var->array_idx < program->num_globals);
        TRY(KOS_set_property(frame, module->global_names, name, TO_SMALL_INT(var->array_idx)));
    }

_error:
    return error;
}

static int _decode_utf8_to_local(const char         *cstr,
                                 unsigned            length,
                                 KOS_STRING         *string,
                                 struct _KOS_VECTOR *storage)
{
    int            error;
    uint32_t       max_code;
    const unsigned len      = _KOS_utf8_get_len(cstr, length, KOS_UTF8_WITH_ESCAPE, &max_code);
    unsigned       byte_len = len;
    char          *buf;

    assert(len < 0xFFFFU);

    if (max_code > 0xFFFFU)
        byte_len <<= 2;
    else if (max_code > 0xFFU)
        byte_len <<= 1;

    if (byte_len <= sizeof(string->data)) {
        buf           = string->data.buf;
        string->flags = KOS_STRING_LOCAL;
    }
    else {
        byte_len = (byte_len + 7) & ~7U;
        TRY(_KOS_vector_resize(storage, byte_len));

        buf              =  storage->buffer;
        string->data.ptr =  buf;
        string->flags    =  KOS_STRING_PTR;
    }

    string->length = (uint16_t)len;
    string->hash   = 0;

    if (max_code > 0xFFFFU) {
        string->type = OBJ_STRING_32;
        TRY(_KOS_utf8_decode_32(cstr, length, KOS_UTF8_WITH_ESCAPE, (uint32_t *)buf));
    }
    else if (max_code > 0XFFU) {
        string->type = OBJ_STRING_16;
        TRY(_KOS_utf8_decode_16(cstr, length, KOS_UTF8_WITH_ESCAPE, (uint16_t *)buf));
    }
    else {
        string->type = OBJ_STRING_8;
        TRY(_KOS_utf8_decode_8(cstr, length, KOS_UTF8_WITH_ESCAPE, (uint8_t *)buf));
    }

_error:
    return error;
}

static size_t _calc_strings_storage(struct _KOS_COMP_UNIT *program)
{
    size_t size = program->num_strings * sizeof(KOS_STRING);

    struct _KOS_COMP_STRING *str = program->string_list;

    for ( ; str; str = str->next) {

        uint32_t max_code;
        unsigned len = _KOS_utf8_get_len(str->str, str->length, str->escape, &max_code);

        if (len == ~0U) {
            size = ~0U;
            break;
        }

        if (max_code > 0xFFFFU)
            len <<= 2;
        else if (max_code > 0xFFU)
            len <<= 1;

        if (len <= sizeof(((KOS_STRING *)0)->data))
            len = 0;
        else
            len = (len + 7) & ~7U;

        size += len;
    }

    return size;
}

static int _alloc_strings(KOS_STACK_FRAME       *frame,
                          struct _KOS_COMP_UNIT *program,
                          KOS_MODULE            *module)
{
    int          error = KOS_SUCCESS;
    const size_t size  = _calc_strings_storage(program);
    char        *buf   = 0;

    struct _KOS_COMP_STRING *str = 0;

    if (size == ~0U)
        TRY(KOS_ERROR_INVALID_UTF8_CHARACTER);

    if (!error) {
        module->strings = (KOS_STRING *)_KOS_alloc_buffer(frame, size);
        if (module->strings) {
            memset(module->strings, 0, size);
            module->flags |= KOS_MODULE_OWN_STRINGS;
            str           =  program->string_list;
            buf           =  (char *)module->strings + program->num_strings * sizeof(KOS_STRING);
        }
        else
            TRY(KOS_ERROR_OUT_OF_MEMORY);
    }

    for ( ; str; str = str->next) {

        uint32_t       max_code;
        const unsigned len      = _KOS_utf8_get_len(str->str, str->length, str->escape, &max_code);
        unsigned       byte_len = len;
        KOS_STRING    *string   = module->strings + str->index;
        char          *storage;

        assert(len < 0xFFFFU);
        assert(str->index < program->num_strings);

        if (max_code > 0xFFFFU)
            byte_len <<= 2;
        else if (max_code > 0xFFU)
            byte_len <<= 1;

        if (byte_len <= sizeof(string->data)) {
            storage       = string->data.buf;
            string->flags = KOS_STRING_LOCAL;
        }
        else {
            byte_len         =  (byte_len + 7) & ~7U;
            storage          =  buf;
            string->data.ptr =  buf;
            string->flags    =  KOS_STRING_PTR;
            buf              += byte_len;
            assert(buf <= (char*)module->strings + size);
        }

        string->length = (uint16_t)len;
        string->hash   = 0;

        if (max_code > 0xFFFFU) {
            string->type = OBJ_STRING_32;
            TRY(_KOS_utf8_decode_32(str->str, str->length, str->escape, (uint32_t *)storage));
        }
        else if (max_code > 0xFFU) {
            string->type = OBJ_STRING_16;
            TRY(_KOS_utf8_decode_16(str->str, str->length, str->escape, (uint16_t *)storage));
        }
        else {
            string->type = OBJ_STRING_8;
            TRY(_KOS_utf8_decode_8(str->str, str->length, str->escape, (uint8_t *)storage));
        }
    }

_error:
    return error;
}

static KOS_OBJ_PTR _get_line(KOS_STACK_FRAME          *frame,
                             const struct _KOS_VECTOR *file_buf,
                             unsigned                  line)
{
    const char        *buf       = file_buf->buffer;
    const char        *const end = buf + file_buf->size;
    const char        *begin;
    unsigned           len       = 0;
    struct _KOS_VECTOR line_buf;
    KOS_OBJ_PTR        ret       = TO_OBJPTR(0);

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

static KOS_OBJ_PTR _format_error(KOS_STACK_FRAME          *frame,
                                 KOS_OBJ_PTR               module_obj,
                                 const struct _KOS_VECTOR *file_buf,
                                 const char               *error_str,
                                 struct _KOS_FILE_POS      pos)
{
    int                     error = KOS_SUCCESS;
    KOS_OBJ_PTR             ret   = TO_OBJPTR(0);
    KOS_ATOMIC(KOS_OBJ_PTR) parts[11];
    struct _KOS_VECTOR      cstr;

    _KOS_vector_init(&cstr);

    parts[0] = KOS_get_file_name(frame, OBJPTR(KOS_MODULE, module_obj)->path);
    if (IS_BAD_PTR(parts[0]))
        TRY(KOS_ERROR_EXCEPTION);

    parts[1] = TO_OBJPTR(&str_format_colon);

    parts[2] = KOS_object_to_string(frame, TO_SMALL_INT((int)pos.line));
    if (IS_BAD_PTR(parts[2]))
        TRY(KOS_ERROR_EXCEPTION);

    parts[3] = TO_OBJPTR(&str_format_colon);

    parts[4] = KOS_object_to_string(frame, TO_SMALL_INT((int)pos.column));
    if (IS_BAD_PTR(parts[4]))
        TRY(KOS_ERROR_EXCEPTION);

    parts[5] = TO_OBJPTR(&str_format_error);

    parts[6] = KOS_new_const_ascii_cstring(frame, error_str);
    if (IS_BAD_PTR(parts[6]))
        TRY(KOS_ERROR_EXCEPTION);

    parts[7] = TO_OBJPTR(&str_eol);

    parts[8] = _get_line(frame, file_buf, pos.line);
    if (IS_BAD_PTR(parts[8]))
        TRY(KOS_ERROR_EXCEPTION);

    parts[9] = TO_OBJPTR(&str_eol);

    error = _KOS_vector_resize(&cstr, pos.column);
    if (error) {
        KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        TRY(KOS_ERROR_EXCEPTION);
    }

    memset(cstr.buffer, ' ', pos.column-1);
    cstr.buffer[pos.column-1] = '^';

    parts[10] = KOS_new_string(frame, cstr.buffer, pos.column);
    if (IS_BAD_PTR(parts[10]))
        TRY(KOS_ERROR_EXCEPTION);

    ret = KOS_string_add_many(frame, parts, sizeof(parts)/sizeof(parts[0]));

_error:
    if (error)
        ret = TO_OBJPTR(0);

    _KOS_vector_destroy(&cstr);

    return ret;
}

int KOS_load_module(KOS_STACK_FRAME *frame, const char *path)
{
    int         idx;
    KOS_OBJ_PTR module = _KOS_module_import(frame, path, (unsigned)strlen(path), KOS_MODULE_MANDATORY, &idx);

    return IS_BAD_PTR(module) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _import_module(void                   *vframe,
                          const char             *name,
                          unsigned                length,
                          enum _KOS_COMP_REQUIRED required,
                          int                     *module_idx)
{
    KOS_STACK_FRAME *frame = (KOS_STACK_FRAME *)vframe;
    KOS_OBJ_PTR      module_obj;

    module_obj = _KOS_module_import(frame, name, length, (enum _KOS_MODULE_REQUIRED)required, module_idx);

    return IS_BAD_PTR(module_obj) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
}

static int _get_global_idx(void       *vframe,
                           int         module_idx,
                           const char *name,
                           unsigned    length,
                           int        *global_idx)
{
    int                error;
    KOS_STRING         str;
    struct _KOS_VECTOR str_storage;
    KOS_STACK_FRAME   *frame = (KOS_STACK_FRAME *)vframe;
    KOS_CONTEXT       *ctx;
    KOS_OBJ_PTR        module_obj;
    KOS_OBJ_PTR        glob_idx_obj;

    assert( ! IS_BAD_PTR(frame->module));
    assert(OBJPTR(KOS_MODULE, frame->module)->context);

    ctx = OBJPTR(KOS_MODULE, frame->module)->context;

    _KOS_vector_init(&str_storage);
    TRY(_decode_utf8_to_local(name, length, &str, &str_storage));

    module_obj = KOS_array_read(frame, TO_OBJPTR(&ctx->modules), module_idx);
    if (IS_BAD_PTR(module_obj))
        TRY(KOS_ERROR_EXCEPTION);

    assert(!IS_SMALL_INT(module_obj));
    assert(GET_OBJ_TYPE(module_obj) == OBJ_MODULE);

    glob_idx_obj = KOS_get_property(frame, OBJPTR(KOS_MODULE, module_obj)->global_names, TO_OBJPTR(&str));
    if (IS_BAD_PTR(glob_idx_obj))
        TRY(KOS_ERROR_EXCEPTION);

    assert(IS_SMALL_INT(glob_idx_obj));

    *global_idx = (int)GET_SMALL_INT(glob_idx_obj);

_error:
    _KOS_vector_destroy(&str_storage);
    if (error) {
        KOS_clear_exception(frame);
        error = KOS_ERROR_NOT_FOUND;
    }
    return error;
}

KOS_OBJ_PTR _KOS_module_import(KOS_STACK_FRAME          *frame,
                               const char               *module_name,
                               unsigned                  length,
                               enum _KOS_MODULE_REQUIRED required,
                               int                      *out_module_idx)
{
    static const char             lang[]          = "lang";
    int                           error           = KOS_SUCCESS;
    int                           module_idx      = 0;
    KOS_OBJ_PTR                   module_obj      = TO_OBJPTR(0);
    KOS_OBJ_PTR                   actual_module_name;
    KOS_OBJ_PTR                   module_dir      = TO_OBJPTR(0);
    KOS_OBJ_PTR                   module_path     = TO_OBJPTR(0);
    KOS_OBJ_PTR                   ret;
    KOS_CONTEXT                  *ctx;
    struct _KOS_MODULE_LOAD_CHAIN loading         = { 0, 0, 0 };
    struct _KOS_MODULE_INIT      *mod_init;
    struct _KOS_VECTOR            file_buf;
    struct _KOS_PARSER            parser;
    struct _KOS_COMP_UNIT         program;
    struct _KOS_AST_NODE         *ast;
    int                           search_path_set = 0;
    int                           chain_init      = 0;
    int                           compiler_init   = 0;

    assert( ! IS_BAD_PTR(frame->module));
    assert(OBJPTR(KOS_MODULE, frame->module)->context);

    ctx = OBJPTR(KOS_MODULE, frame->module)->context;

    _KOS_vector_init(&file_buf);

    _get_module_name(module_name, length, &loading);

    /* Determine actual module name */
    actual_module_name = KOS_new_string(frame, loading.module_name, loading.length);
    if (IS_BAD_PTR(actual_module_name))
        TRY(KOS_ERROR_EXCEPTION);

    /* Find module source file */
    error = _find_module(frame, actual_module_name, module_name, length, &module_dir, &module_path);
    if (error) {
        if (error == KOS_ERROR_NOT_FOUND) {
            if (required == KOS_MODULE_MANDATORY) {
                _raise_3(frame, TO_OBJPTR(&str_err_module), actual_module_name, TO_OBJPTR(&str_err_not_found));
                error = KOS_ERROR_EXCEPTION;
            }
            else
                error = KOS_SUCCESS;
        }
        goto _error;
    }

    /* TODO use global mutex for thread safety */

    /* Load lang module first, so that it ends up at index 0 */
    if (KOS_get_array_size(TO_OBJPTR(&ctx->modules)) == 0 && module_name != lang) {
        int         lang_idx;
        KOS_OBJ_PTR path_array;
        KOS_OBJ_PTR lang_obj;

        /* Add search path - path of the topmost module being loaded */
        path_array = KOS_new_array(frame, 1);
        TRY(KOS_array_write(frame, path_array, 0, module_dir));
        TRY(KOS_array_insert(frame, TO_OBJPTR(&ctx->module_search_paths), 0, 0, path_array, 0, 1));
        search_path_set = 1;

        lang_obj = _KOS_module_import(frame, lang, sizeof(lang)-1, KOS_MODULE_MANDATORY, &lang_idx);
        if (IS_BAD_PTR(lang_obj))
            TRY(KOS_ERROR_EXCEPTION);
        assert(lang_idx == 0);
    }

    /* Add module to the load chain to prevent and detect circular dependencies */
    {
        struct _KOS_MODULE_LOAD_CHAIN *chain = ctx->module_load_chain;
        loading.next = chain;
        for ( ; chain; chain = chain->next) {
            if (loading.length == chain->length &&
                    0 == memcmp(loading.module_name, chain->module_name, loading.length)) {

                KOS_OBJ_PTR name_str = KOS_new_string(frame, module_name, length);
                if (!IS_BAD_PTR(name_str))
                    _raise_3(frame, TO_OBJPTR(&str_err_circular_deps), name_str, TO_OBJPTR(&str_err_end));
                TRY(KOS_ERROR_EXCEPTION);
            }
        }
    }
    ctx->module_load_chain = &loading;
    chain_init             = 1;

    /* Return the module object if it was already loaded */
    {
        KOS_OBJ_PTR module_idx_obj = KOS_get_property(frame, TO_OBJPTR(&ctx->module_names), actual_module_name);
        if (!IS_BAD_PTR(module_idx_obj)) {
            assert(IS_SMALL_INT(module_idx_obj));
            module_obj = KOS_array_read(frame, TO_OBJPTR(&ctx->modules), (int)GET_SMALL_INT(module_idx_obj));
            if (IS_BAD_PTR(module_obj))
                error = KOS_ERROR_EXCEPTION;
            goto _error;
        }
    }
    KOS_clear_exception(frame);

    /* Make room for the new module and allocate index */
    module_idx = (int)KOS_get_array_size(TO_OBJPTR(&ctx->modules));
    TRY(KOS_array_resize(frame, TO_OBJPTR(&ctx->modules), (uint32_t)module_idx+1));

    /* Allocate module object */
    module_obj = _alloc_module(frame, actual_module_name);
    if (IS_BAD_PTR(module_obj)) {
        assert(KOS_is_exception_pending(frame));
        TRY(KOS_ERROR_EXCEPTION);
    }
    OBJPTR(KOS_MODULE, module_obj)->path = module_path;

    /* Load module file */
    TRY(_load_file(frame, OBJPTR(KOS_MODULE, module_obj)->path, &file_buf));

    /* Run built-in module initialization */
    mod_init = (struct _KOS_MODULE_INIT *)_KOS_red_black_find(ctx->module_inits,
                                                              actual_module_name,
                                                              _module_init_compare);
    if (mod_init) {
        KOS_STACK_FRAME *mod_frame = _KOS_stack_frame_push(frame,
                                                           module_obj,
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
        KOS_OBJ_PTR error_obj = _format_error(frame,
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
        TRY(KOS_array_write(frame, TO_OBJPTR(&ctx->modules), module_idx, module_obj));

    /* Prepare compiler */
    program.frame          = frame;
    program.import_module  = _import_module;
    program.get_global_idx = _get_global_idx;
    TRY(_predefine_globals(frame, &program, OBJPTR(KOS_MODULE, module_obj)->global_names));

    /* Compile source code into bytecode */
    error = _KOS_compiler_compile(&program, ast);

    if (error == KOS_ERROR_COMPILE_FAILED) {

        KOS_OBJ_PTR error_obj = _format_error(frame,
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

    TRY(_alloc_globals(frame, &program, OBJPTR(KOS_MODULE, module_obj)));
    TRY(_alloc_strings(frame, &program, OBJPTR(KOS_MODULE, module_obj)));

    /* Move compiled program to module */
    {
        KOS_MODULE         *module       = OBJPTR(KOS_MODULE, module_obj);
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

        module->num_regs = (unsigned)program.cur_frame->num_regs;

        if (ctx->flags & KOS_CTX_DEBUG) {
            struct _KOS_VECTOR cname;

            _KOS_vector_init(&cname);
            error = KOS_string_to_cstr_vec(frame, module->name, &cname);
            if (!error)
                printf("Disassembling module %s:\n", cname.buffer);
            _KOS_vector_destroy(&cname);
            TRY(error);

            _KOS_disassemble(module->bytecode, module->bytecode_size,
                             (struct _KOS_COMP_ADDR_TO_LINE *)module->line_addrs,
                             module->num_line_addrs);
        }
    }

    _KOS_parser_destroy(&parser);
    _KOS_compiler_destroy(&program);
    compiler_init = 0;

    /* Put module on the list */
    TRY(KOS_array_write(frame, TO_OBJPTR(&ctx->modules), module_idx, module_obj));
    TRY(KOS_set_property(frame, TO_OBJPTR(&ctx->module_names), actual_module_name, TO_SMALL_INT(module_idx)));

    /* Run module */
    error = _KOS_vm_run_module(OBJPTR(KOS_MODULE, module_obj), &ret);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_raise_exception(frame, ret);
    }

_error:
    if (search_path_set) {
        const uint32_t num_paths = KOS_get_array_size(TO_OBJPTR(&ctx->module_search_paths));
        int            err;
        assert(num_paths > 0);
        err = KOS_array_resize(frame, TO_OBJPTR(&ctx->module_search_paths), num_paths-1);
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
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_out_of_memory));
        }
        else if (error == KOS_ERROR_INVALID_UTF8_CHARACTER) {
            if ( ! KOS_is_exception_pending(frame))
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_invalid_utf8));
        }
        else {
            if ( ! KOS_is_exception_pending(frame))
                KOS_raise_exception(frame, TO_OBJPTR(&str_err_internal));
        }
        module_obj = TO_OBJPTR(0);
    }
    else {
        *out_module_idx = module_idx;
        assert(!KOS_is_exception_pending(frame));
    }

    return module_obj;
}

int KOS_module_add_global(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      name,
                          KOS_OBJ_PTR      value,
                          unsigned        *idx)
{
    int         error;
    uint32_t    new_idx;
    KOS_OBJ_PTR prop;
    KOS_MODULE *module = OBJPTR(KOS_MODULE, frame->module);

    assert(module);

    prop = KOS_get_property(frame, module->global_names, name);

    KOS_clear_exception(frame);

    if ( ! IS_BAD_PTR(prop)) {
        _raise_3(frame, TO_OBJPTR(&str_err_duplicate_global), name, TO_OBJPTR(&str_err_end));
        TRY(KOS_ERROR_EXCEPTION);
    }

    /* TODO improve thread safety */

    new_idx = KOS_get_array_size(module->globals);
    TRY(KOS_array_resize(frame, module->globals, new_idx+1));
    TRY(KOS_array_write(frame, module->globals, (int)new_idx, value));
    TRY(KOS_set_property(frame, module->global_names, name, TO_SMALL_INT((int)new_idx)));

    if (idx)
        *idx = new_idx;

_error:
    return error;
}

int KOS_module_get_global(KOS_STACK_FRAME *frame,
                          KOS_OBJ_PTR      name,
                          KOS_OBJ_PTR     *value,
                          unsigned        *idx)
{
    int         error  = KOS_SUCCESS;
    KOS_OBJ_PTR idx_obj;
    KOS_OBJ_PTR ret;
    KOS_MODULE *module = OBJPTR(KOS_MODULE, frame->module);

    assert(module);

    idx_obj = KOS_get_property(frame, module->global_names, name);
    TRY_OBJPTR(idx_obj);

    assert(IS_SMALL_INT(idx_obj));

    ret = KOS_array_read(frame, module->globals, (int)GET_SMALL_INT(idx_obj));
    TRY_OBJPTR(ret);

    *value = ret;
    if (idx)
        *idx = (unsigned)GET_SMALL_INT(idx_obj);

_error:
    return error;
}

int KOS_module_add_function(KOS_STACK_FRAME          *frame,
                            KOS_STRING               *str_name,
                            KOS_FUNCTION_HANDLER      handler,
                            int                       min_args,
                            enum _KOS_GENERATOR_STATE gen_state)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE *module   = OBJPTR(KOS_MODULE, frame->module);

    assert(module);

    TRY_OBJPTR(func_obj);

    OBJPTR(KOS_FUNCTION, func_obj)->module          = TO_OBJPTR(module);
    OBJPTR(KOS_FUNCTION, func_obj)->generator_state = gen_state;

    TRY(KOS_module_add_global(frame,
                              TO_OBJPTR(str_name),
                              func_obj,
                              0));

_error:
    return error;
}

int KOS_module_add_constructor(KOS_STACK_FRAME     *frame,
                               KOS_STRING          *str_name,
                               KOS_FUNCTION_HANDLER handler,
                               int                  min_args,
                               KOS_OBJ_PTR         *ret_proto)
{
    int         error    = KOS_SUCCESS;
    KOS_OBJ_PTR func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE *module   = OBJPTR(KOS_MODULE, frame->module);

    assert(module);

    TRY_OBJPTR(func_obj);

    OBJPTR(KOS_FUNCTION, func_obj)->module = TO_OBJPTR(module);

    TRY(KOS_module_add_global(frame,
                              TO_OBJPTR(str_name),
                              func_obj,
                              0));

    *ret_proto = OBJPTR(KOS_FUNCTION, func_obj)->prototype;
    assert( ! IS_BAD_PTR(*ret_proto));

_error:
    return error;
}

int KOS_module_add_member_function(KOS_STACK_FRAME          *frame,
                                   KOS_OBJ_PTR               proto_obj,
                                   KOS_STRING               *str_name,
                                   KOS_FUNCTION_HANDLER      handler,
                                   int                       min_args,
                                   enum _KOS_GENERATOR_STATE gen_state)
{
    int          error    = KOS_SUCCESS;
    KOS_OBJ_PTR  func_obj = KOS_new_builtin_function(frame, handler, min_args);
    KOS_MODULE  *module   = OBJPTR(KOS_MODULE, frame->module);

    assert(module);

    TRY_OBJPTR(func_obj);

    OBJPTR(KOS_FUNCTION, func_obj)->module          = TO_OBJPTR(module);
    OBJPTR(KOS_FUNCTION, func_obj)->generator_state = gen_state;

    TRY(KOS_set_property(frame, proto_obj, TO_OBJPTR(str_name), func_obj));

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

KOS_OBJ_PTR KOS_module_addr_to_func_name(KOS_MODULE *module,
                                         uint32_t    offs)
{
    const KOS_FUNC_ADDR *addr2func = _addr_to_func(module, offs);

    KOS_OBJ_PTR ret = TO_OBJPTR(0);

    if (addr2func) {
        if (addr2func->str_idx == ~0U)
            ret = TO_OBJPTR(&str_global);
        else
            ret = TO_OBJPTR(module->strings + addr2func->str_idx);
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

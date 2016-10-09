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

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../lang/kos_file.h"
#include "../lang/kos_memory.h"
#include "../lang/kos_try.h"
#include <stdio.h>
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'fopen': This function may be unsafe */
#else
#   include <unistd.h>
#endif

static KOS_ASCII_STRING(str_err_bad_flags,           "incorrect file open flags");
static KOS_ASCII_STRING(str_err_file_not_open,       "file not open");
static KOS_ASCII_STRING(str_err_cannot_get_position, "unable to obtain file position");
static KOS_ASCII_STRING(str_err_cannot_get_size,     "unable to obtain file size");
static KOS_ASCII_STRING(str_err_cannot_set_position, "unable to update file position");
static KOS_ASCII_STRING(str_err_file_read,           "file read error");
static KOS_ASCII_STRING(str_err_file_write,          "file write error");
static KOS_ASCII_STRING(str_err_not_buffer,          "argument to file write is not a buffer");

static void _fix_path_separators(struct _KOS_VECTOR *buf)
{
    char       *ptr = buf->buffer;
    char *const end = ptr + buf->size;

    for ( ; ptr < end; ptr++) {
        const char c = *ptr;
        if (c == '/' || c == '\\')
            *ptr = KOS_PATH_SEPARATOR;
    }
}

static KOS_OBJ_PTR _open(KOS_CONTEXT *ctx,
                         KOS_OBJ_PTR  this_obj,
                         KOS_OBJ_PTR  args_obj)
{
    /* TODO add finalize function */

    int                error        = KOS_SUCCESS;
    KOS_OBJ_PTR        ret          = TO_OBJPTR(0);
    FILE              *file         = 0;
    KOS_OBJ_PTR        filename_obj = KOS_array_read(ctx, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;
    struct _KOS_VECTOR flags_cstr;

    _KOS_vector_init(&filename_cstr);
    _KOS_vector_init(&flags_cstr);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    _fix_path_separators(&filename_cstr);

    /* TODO use own flags */

    if (KOS_get_array_size(args_obj) > 1) {
        KOS_OBJ_PTR flags_obj = KOS_array_read(ctx, args_obj, 1);

        assert( ! IS_BAD_PTR(flags_obj));

        if (IS_BAD_PTR(flags_obj) || ! IS_STRING_OBJ(flags_obj)) {
            if ( ! IS_BAD_PTR(flags_obj))
                KOS_raise_exception(ctx, TO_OBJPTR(&str_err_bad_flags));
            TRY(KOS_ERROR_EXCEPTION);
        }

        TRY(KOS_string_to_cstr_vec(ctx, flags_obj, &flags_cstr));
    }

    file = fopen(filename_cstr.buffer, flags_cstr.size ? flags_cstr.buffer : "r+b");

    ret = KOS_new_object_with_prototype(ctx, this_obj);

    if (IS_BAD_PTR(ret))
        TRY(KOS_ERROR_EXCEPTION);

    KOS_object_set_private(*OBJPTR(KOS_OBJECT, ret), file);
    file = 0;

_error:
    _KOS_vector_destroy(&flags_cstr);
    _KOS_vector_destroy(&filename_cstr);
    if (file)
        fclose(file);

    return ret;
}

static int _get_file_object(KOS_CONTEXT *ctx,
                            KOS_OBJ_PTR  this_obj,
                            FILE       **file,
                            int          must_be_open)
{
    assert( ! IS_BAD_PTR(this_obj));

    if (IS_SMALL_INT(this_obj) || GET_OBJ_TYPE(this_obj) != OBJ_OBJECT) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_file_not_open));
        return KOS_ERROR_EXCEPTION;
    }

    *file = (FILE *)KOS_object_get_private(*OBJPTR(KOS_OBJECT, this_obj));

    if (must_be_open && ! *file) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_file_not_open));
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

static KOS_OBJ_PTR _close(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  this_obj,
                          KOS_OBJ_PTR  args_obj)
{
    FILE *file  = 0;
    int   error = _get_file_object(ctx, this_obj, &file, 0);

    if ( ! error && file) {
        fclose(file);
        KOS_object_set_private(*OBJPTR(KOS_OBJECT, this_obj), (void *)0);
    }

    return error ? TO_OBJPTR(0) : KOS_VOID;
}

/*
 *      input:  size as number (optional, defaults to 4096)
 *              buffer (optional)
 *      output: buffer
 *
 *      If buffer is provided as second arg, read data is appended to it and
 *      this buffer is returned.
 *
 *      Reads as much as possible in one shot.  Returns as much as was read.
 *      The returned buffer's size can be from 0 to the size entered as input.
 */
static KOS_OBJ_PTR _read_some(KOS_CONTEXT *ctx,
                              KOS_OBJ_PTR  this_obj,
                              KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    FILE       *file  = 0;
    KOS_OBJ_PTR buf   = TO_OBJPTR(0);
    uint32_t    offset;
    int64_t     to_read;
    size_t      num_read;

    TRY(_get_file_object(ctx, this_obj, &file, 1));

    if (KOS_get_array_size(args_obj) > 0) {
        KOS_OBJ_PTR arg = KOS_array_read(ctx, args_obj, 0);

        if (IS_BAD_PTR(arg))
            TRY(KOS_ERROR_EXCEPTION);

        TRY(KOS_get_integer(ctx, arg, &to_read));
    }
    else
        to_read = 0x1000U;

    if (to_read < 1)
        to_read = 1;

    if (KOS_get_array_size(args_obj) > 1) {
        buf = KOS_array_read(ctx, args_obj, 1);

        if (IS_BAD_PTR(buf))
            TRY(KOS_ERROR_EXCEPTION);

        if (IS_SMALL_INT(buf) || GET_OBJ_TYPE(buf) != OBJ_BUFFER) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_buffer));
            TRY(KOS_ERROR_EXCEPTION);
        }
    }
    else
        buf = KOS_new_buffer(ctx, 0);

    offset = KOS_get_buffer_size(buf);

    /* TODO Check if offset + to_read does not exceed uint32_t */

    TRY(KOS_buffer_resize(ctx, buf, (unsigned)(offset + to_read)));

    num_read = fread(KOS_buffer_data(ctx, buf)+offset, 1, (size_t)to_read, file);

    assert(num_read <= (size_t)to_read);

    TRY(KOS_buffer_resize(ctx, buf, (unsigned)(offset + num_read)));

    if (num_read < (size_t)to_read && ferror(file)) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_file_read));
        TRY(KOS_ERROR_EXCEPTION);
    }

_error:
    return error ? TO_OBJPTR(0) : buf;
}

static KOS_OBJ_PTR _write(KOS_CONTEXT *ctx,
                          KOS_OBJ_PTR  this_obj,
                          KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    FILE       *file  = 0;
    KOS_OBJ_PTR arg;
    size_t      to_write;
    size_t      num_writ;

    TRY(_get_file_object(ctx, this_obj, &file, 1));

    /* TODO support multiple buffers */

    arg = KOS_array_read(ctx, args_obj, 0);

    if (IS_BAD_PTR(arg))
        TRY(KOS_ERROR_EXCEPTION);

    if (IS_SMALL_INT(arg) || GET_OBJ_TYPE(arg) != OBJ_BUFFER) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_not_buffer));
        TRY(KOS_ERROR_EXCEPTION);
    }

    to_write = (size_t)KOS_get_buffer_size(arg);
    num_writ = fwrite(KOS_buffer_data(ctx, arg), 1, to_write, file);

    if (num_writ < to_write) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_file_write));
        TRY(KOS_ERROR_EXCEPTION);
    }

_error:
    return error ? TO_OBJPTR(0) : this_obj;
}

static KOS_OBJ_PTR _get_file_eof(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  this_obj,
                                 KOS_OBJ_PTR  args_obj)
{
    FILE *file   = 0;
    int   error  = _get_file_object(ctx, this_obj, &file, 1);
    int   status = 0;

    if ( ! error)
        status = feof(file);

    return error ? TO_OBJPTR(0) : KOS_BOOL(status);
}

static KOS_OBJ_PTR _get_file_error(KOS_CONTEXT *ctx,
                                   KOS_OBJ_PTR  this_obj,
                                   KOS_OBJ_PTR  args_obj)
{
    FILE *file   = 0;
    int   error  = _get_file_object(ctx, this_obj, &file, 1);
    int   status = 0;

    if ( ! error)
        status = ferror(file);

    return error ? TO_OBJPTR(0) : KOS_BOOL(status);
}

static KOS_OBJ_PTR _get_file_size(KOS_CONTEXT *ctx,
                                  KOS_OBJ_PTR  this_obj,
                                  KOS_OBJ_PTR  args_obj)
{
    int   error = KOS_SUCCESS;
    FILE *file  = 0;
    long  orig_pos;
    long  size  = 0;

    TRY(_get_file_object(ctx, this_obj, &file, 1));

    orig_pos = ftell(file);
    if (orig_pos < 0) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_get_size));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (fseek(file, 0, SEEK_END)) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_get_size));
        TRY(KOS_ERROR_EXCEPTION);
    }

    size = ftell(file);
    if (size < 0) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_get_size));
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (fseek(file, orig_pos, SEEK_SET)) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_get_size));
        TRY(KOS_ERROR_EXCEPTION);
    }

_error:
    return error ? TO_OBJPTR(0) : KOS_new_int(ctx, (int64_t)size);
}

static KOS_OBJ_PTR _get_file_pos(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  this_obj,
                                 KOS_OBJ_PTR  args_obj)
{
    FILE *file  = 0;
    int   error = _get_file_object(ctx, this_obj, &file, 1);
    long  pos   = 0;

    if ( ! error) {

        pos = ftell(file);

        if (pos < 0) {
            KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_get_position));
            error = KOS_ERROR_EXCEPTION;
        }
    }

    return error ? TO_OBJPTR(0) : KOS_new_int(ctx, (int64_t)pos);
}

static KOS_OBJ_PTR _set_file_pos(KOS_CONTEXT *ctx,
                                 KOS_OBJ_PTR  this_obj,
                                 KOS_OBJ_PTR  args_obj)
{
    int         error = KOS_SUCCESS;
    FILE       *file  = 0;
    int64_t     pos;
    KOS_OBJ_PTR arg;

    TRY(_get_file_object(ctx, this_obj, &file, 1));

    arg = KOS_array_read(ctx, args_obj, 0);

    if (IS_BAD_PTR(arg))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_get_integer(ctx, arg, &pos));

    if (fseek(file, (long)pos, SEEK_SET)) {
        KOS_raise_exception(ctx, TO_OBJPTR(&str_err_cannot_set_position));
        TRY(KOS_ERROR_EXCEPTION);
    }

_error:
    return error ? TO_OBJPTR(0) : KOS_VOID;
}

static KOS_OBJ_PTR _is_file(KOS_CONTEXT *ctx,
                            KOS_OBJ_PTR  this_obj,
                            KOS_OBJ_PTR  args_obj)
{
    int                error        = KOS_SUCCESS;
    KOS_OBJ_PTR        ret          = TO_OBJPTR(0);
    KOS_OBJ_PTR        filename_obj = KOS_array_read(ctx, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;

    _KOS_vector_init(&filename_cstr);

    if (IS_BAD_PTR(filename_obj))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    _fix_path_separators(&filename_cstr);

    ret = KOS_BOOL(_KOS_does_file_exist(filename_cstr.buffer));

_error:
    _KOS_vector_destroy(&filename_cstr);

    return ret;
}

static KOS_OBJ_PTR _remove(KOS_CONTEXT *ctx,
                           KOS_OBJ_PTR  this_obj,
                           KOS_OBJ_PTR  args_obj)
{
    int                error        = KOS_SUCCESS;
    KOS_OBJ_PTR        ret          = TO_OBJPTR(0);
    KOS_OBJ_PTR        filename_obj = KOS_array_read(ctx, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;

    _KOS_vector_init(&filename_cstr);

    if (IS_BAD_PTR(filename_obj))
        TRY(KOS_ERROR_EXCEPTION);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    _fix_path_separators(&filename_cstr);

#ifdef _WIN32
    ret = KOS_BOOL(DeleteFile(filename_cstr.buffer));
#else
    ret = KOS_BOOL(unlink(filename_cstr.buffer) == 0);
#endif

_error:
    _KOS_vector_destroy(&filename_cstr);

    return ret;
}

static int _add_std_file(KOS_MODULE *module,
                         KOS_OBJ_PTR proto,
                         KOS_STRING *str_name,
                         FILE*       file)
{
    KOS_OBJ_PTR obj = KOS_new_object_with_prototype(module->context, proto);

    if (IS_BAD_PTR(obj))
        return KOS_ERROR_EXCEPTION;

    KOS_object_set_private(*OBJPTR(KOS_OBJECT, obj), file);

    return KOS_module_add_global(module, TO_OBJPTR(str_name), obj, 0);
}

#define TRY_ADD_STD_FILE(module, proto, name, file)           \
do {                                                          \
    static KOS_ASCII_STRING(str_name, name);                  \
    TRY(_add_std_file((module), (proto), &str_name, (file))); \
} while (0)

int _KOS_module_file_init(KOS_MODULE *module)
{
    int         error = KOS_SUCCESS;
    KOS_OBJ_PTR proto;

    TRY_ADD_CONSTRUCTOR(    module,        "file",      _open,           1, &proto);
    TRY_ADD_MEMBER_FUNCTION(module, proto, "close",     _close,          0);
    TRY_ADD_MEMBER_FUNCTION(module, proto, "read_some", _read_some,      0);
    TRY_ADD_MEMBER_FUNCTION(module, proto, "release",   _close,          0);
    TRY_ADD_MEMBER_FUNCTION(module, proto, "seek",      _set_file_pos,   1);
    TRY_ADD_MEMBER_FUNCTION(module, proto, "write",     _write,          1);
    TRY_ADD_MEMBER_PROPERTY(module, proto, "eof",       _get_file_eof,   0);
    TRY_ADD_MEMBER_PROPERTY(module, proto, "error",     _get_file_error, 0);
    TRY_ADD_MEMBER_PROPERTY(module, proto, "position",  _get_file_pos,   0);
    TRY_ADD_MEMBER_PROPERTY(module, proto, "size",      _get_file_size,  0);

    TRY_ADD_FUNCTION(       module,        "is_file",   _is_file,        1);
    TRY_ADD_FUNCTION(       module,        "remove",    _remove,         1);

    TRY_ADD_STD_FILE(       module, proto, "stdin",     stdin);
    TRY_ADD_STD_FILE(       module, proto, "stdout",    stdout);
    TRY_ADD_STD_FILE(       module, proto, "stderr",    stderr);

_error:
    return error;
}

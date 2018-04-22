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

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
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

static const char str_err_bad_flags[]           = "incorrect file open flags";
static const char str_err_cannot_get_position[] = "unable to obtain file position";
static const char str_err_cannot_get_size[]     = "unable to obtain file size";
static const char str_err_cannot_open_file[]    = "unable to open file";
static const char str_err_cannot_set_position[] = "unable to update file position";
static const char str_err_file_not_open[]       = "file not open";
static const char str_err_file_read[]           = "file read error";
static const char str_err_file_write[]          = "file write error";
static const char str_err_invalid_buffer_size[] = "buffer size out of range";
static const char str_err_not_buffer[]          = "argument to file write is not a buffer";
static const char str_err_too_many_to_read[]    = "requested read size exceeds buffer size limit";
static const char str_position[]                = "position";

static KOS_OBJ_ID _get_file_pos(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj);

static KOS_OBJ_ID _set_file_pos(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj);

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

static void _finalize(KOS_FRAME frame,
                      void     *priv)
{
    if (priv)
        fclose((FILE *)priv);
}

/* @item file file()
 *
 *     file(pathname, flags = rw)
 *
 * File object class.
 *
 * Returns opened file object.
 *
 * `pathname` is the path to the file.
 *
 * `flags` is a string, which specifies file open mode compatible with
 * the C `fopen()` function.  It is normally recommended to use the
 * shorthand flag constants: `file.ro`, `file.rw` or the auxiliary
 * file functions `file.open()`, `file.create()` and `file.append()`
 * instead of specifying the flags explicitly.
 *
 * It is recommended to use the `file.file` class in conjunction with
 * the `with` statement.
 *
 * Example:
 *
 *     > with const f = file.file("my.txt", file.create_flag) { f.print("hello") }
 */
static KOS_OBJ_ID _open(KOS_FRAME  frame,
                        KOS_OBJ_ID this_obj,
                        KOS_OBJ_ID args_obj)
{
    int                error        = KOS_SUCCESS;
    KOS_OBJ_ID         ret          = KOS_BADPTR;
    FILE              *file         = 0;
    KOS_OBJ_ID         filename_obj = KOS_array_read(frame, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;
    struct _KOS_VECTOR flags_cstr;

    _KOS_vector_init(&filename_cstr);
    _KOS_vector_init(&flags_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(frame, filename_obj, &filename_cstr));

    _fix_path_separators(&filename_cstr);

    /* TODO use own flags */

    if (KOS_get_array_size(args_obj) > 1) {
        KOS_OBJ_ID flags_obj = KOS_array_read(frame, args_obj, 1);

        TRY_OBJID(flags_obj);

        if (GET_OBJ_TYPE(flags_obj) != OBJ_STRING)
            RAISE_EXCEPTION(str_err_bad_flags);

        TRY(KOS_string_to_cstr_vec(frame, flags_obj, &flags_cstr));
    }

    file = fopen(filename_cstr.buffer, flags_cstr.size ? flags_cstr.buffer : "r+b");

    if ( ! file)
        RAISE_EXCEPTION(str_err_cannot_open_file);

    ret = KOS_new_object_with_prototype(frame, this_obj);
    TRY_OBJID(ret);

    TRY(KOS_set_builtin_dynamic_property(frame,
                                         ret,
                                         KOS_context_get_cstring(frame, str_position),
                                         _get_file_pos,
                                         _set_file_pos));

    OBJPTR(OBJECT, ret)->finalize = _finalize;

    KOS_object_set_private(*OBJPTR(OBJECT, ret), file);
    file = 0;

_error:
    _KOS_vector_destroy(&flags_cstr);
    _KOS_vector_destroy(&filename_cstr);
    if (file)
        fclose(file);

    return ret;
}

static int _get_file_object(KOS_FRAME  frame,
                            KOS_OBJ_ID this_obj,
                            FILE     **file,
                            int        must_be_open)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT)
        RAISE_EXCEPTION(str_err_file_not_open);

    *file = (FILE *)KOS_object_get_private(*OBJPTR(OBJECT, this_obj));

    if (must_be_open && ! *file)
        RAISE_EXCEPTION(str_err_file_not_open);

_error:
    return error;
}

/* @item file file.prototype.close()
 *
 *     file.prototype.close()
 *
 * Closes the file object if it is still opened.
 */
static KOS_OBJ_ID _close(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    FILE *file  = 0;
    int   error = _get_file_object(frame, this_obj, &file, 0);

    if ( ! error && file) {
        fclose(file);
        KOS_object_set_private(*OBJPTR(OBJECT, this_obj), (void *)0);
    }

    return error ? KOS_BADPTR : KOS_VOID;
}

/* @item file file.prototype.print()
 *
 *     file.prototype.print(values...)
 *
 * Converts all arguments to printable strings and writes them to the file.
 *
 * Returns the file object to which the strings were written.
 *
 * Accepts zero or more arguments to write.
 *
 * Written values are separated with a single space.
 *
 * After printing all values writes an EOL character.  If no values are
 * provided, just writes an EOL character.
 */
static KOS_OBJ_ID _print(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    FILE              *file  = 0;
    int                error = _get_file_object(frame, this_obj, &file, 0);
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    if ( ! error && file) {

        TRY(KOS_print_to_cstr_vec(frame, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

        if (cstr.size) {
            cstr.buffer[cstr.size - 1] = '\n';
            fwrite(cstr.buffer, 1, cstr.size, file);
        }
        else
            fprintf(file, "\n");
    }

_error:
    _KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_obj;
}

/* @item file file.prototype.print_()
 *
 *     file.prototype.print_(values...)
 *
 * Converts all arguments to printable strings and writes them to the file.
 *
 * Returns the file object to which the strings were written.
 *
 * Accepts zero or more arguments to write.
 *
 * Written values are separated with a single space.
 *
 * Unlike `file.prototype.print()`, does not write an EOL character after finishing writing.
 */
static KOS_OBJ_ID _print_(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    FILE              *file  = 0;
    int                error = _get_file_object(frame, this_obj, &file, 0);
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    if ( ! error && file) {

        TRY(KOS_print_to_cstr_vec(frame, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

        if (cstr.size)
            fwrite(cstr.buffer, 1, cstr.size - 1, file);
    }

_error:
    _KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_obj;
}

static int _is_eol(char c)
{
    return c == '\n' || c == '\r';
}

/* @item file file.prototype.read_line()
 *
 *     file.prototype.read_line(reserved_size = 4096)
 *
 * Reads a single line of text from a file.
 *
 * Returns the string containing the line read, including EOL character sequence.
 *
 * `reserved_size` is the amount of bytes to reserve for the buffer into which
 * the file is read.  If the line is longer than that, the buffer will be
 * automatically resized.  This is an implementation detail and it may change
 * in the future.
 *
 * This is a low-level function, `file.prototype.read_lines()` is a better choice
 * in most cases.
 */
static KOS_OBJ_ID _read_line(KOS_FRAME  frame,
                             KOS_OBJ_ID this_obj,
                             KOS_OBJ_ID args_obj)
{
    int                error      = KOS_SUCCESS;
    FILE              *file       = 0;
    int                size_delta = 4096;
    int                last_size  = 0;
    int                num_read;
    struct _KOS_VECTOR buf;
    KOS_OBJ_ID         line       = KOS_BADPTR;

    _KOS_vector_init(&buf);

    TRY(_get_file_object(frame, this_obj, &file, 1));

    if (KOS_get_array_size(args_obj) > 0) {

        int64_t iarg = 0;

        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &iarg));

        if (iarg <= 0 || iarg > INT_MAX-1)
            RAISE_EXCEPTION(str_err_invalid_buffer_size);

        size_delta = (int)iarg + 1;
    }

    do {
        char *ret;

        TRY(_KOS_vector_resize(&buf, (size_t)(last_size + size_delta)));

        ret = fgets(buf.buffer + last_size, size_delta, file);

        if ( ! ret) {
            if (ferror(file))
                RAISE_EXCEPTION(str_err_file_read);
            else
                break;
        }

        num_read = (int)strlen(buf.buffer + last_size);

        last_size += num_read;
    } while (num_read != 0 &&
             num_read+1 == size_delta &&
             ! _is_eol(buf.buffer[last_size-1]));

    line = KOS_new_string(frame, buf.buffer, (unsigned)last_size);

_error:
    _KOS_vector_destroy(&buf);
    return error ? KOS_BADPTR : line;
}

/* @item file file.prototype.read_some()
 *
 *     file.prototype.read_some(size = 4096 [, buffer])
 *
 * Reads a variable number of bytes from an opened file object.
 *
 * Returns a buffer containing the bytes read.
 *
 * Reads as many bytes as it can, up to the specified `size`.
 *
 * `size` is the maximum bytes to read.  `size` defaults to 4096.  Less
 * bytes can be read if no more bytes are available.
 *
 * If `buffer` is specified, bytes are appended to it and that buffer is
 * returned instead of creating a new buffer.
 *
 * This is a low-level function, `file.prototype.read()` is a better choice
 * in most cases.
 */
static KOS_OBJ_ID _read_some(KOS_FRAME  frame,
                             KOS_OBJ_ID this_obj,
                             KOS_OBJ_ID args_obj)
{
    int        error = KOS_SUCCESS;
    FILE      *file  = 0;
    KOS_OBJ_ID buf   = KOS_BADPTR;
    uint32_t   offset;
    int64_t    to_read;
    size_t     num_read;

    TRY(_get_file_object(frame, this_obj, &file, 1));

    if (KOS_get_array_size(args_obj) > 0) {
        KOS_OBJ_ID arg = KOS_array_read(frame, args_obj, 0);

        TRY_OBJID(arg);

        TRY(KOS_get_integer(frame, arg, &to_read));
    }
    else
        to_read = 0x1000U;

    if (to_read < 1)
        to_read = 1;

    if (KOS_get_array_size(args_obj) > 1) {
        buf = KOS_array_read(frame, args_obj, 1);

        TRY_OBJID(buf);

        if (GET_OBJ_TYPE(buf) != OBJ_BUFFER)
            RAISE_EXCEPTION(str_err_not_buffer);
    }
    else
        buf = KOS_new_buffer(frame, 0);

    offset = KOS_get_buffer_size(buf);

    if (to_read > (int64_t)(0xFFFFFFFFU - offset))
        RAISE_EXCEPTION(str_err_too_many_to_read);

    TRY(KOS_buffer_resize(frame, buf, (unsigned)(offset + to_read)));

    num_read = fread(KOS_buffer_data(buf)+offset, 1, (size_t)to_read, file);

    assert(num_read <= (size_t)to_read);

    TRY(KOS_buffer_resize(frame, buf, (unsigned)(offset + num_read)));

    if (num_read < (size_t)to_read && ferror(file))
        RAISE_EXCEPTION(str_err_file_read);

_error:
    return error ? KOS_BADPTR : buf;
}

/* @item file file.prototype.write()
 *
 *     file.prototype.write(buffer)
 *
 * Writes a buffer containing bytes into an opened file object.
 *
 * Returns the file object to which bytes has been written.
 *
 * `buffer` is a buffer object.  Its size can be zero, in which case nothing
 * is written.
 */
static KOS_OBJ_ID _write(KOS_FRAME  frame,
                         KOS_OBJ_ID this_obj,
                         KOS_OBJ_ID args_obj)
{
    int        error    = KOS_SUCCESS;
    FILE      *file     = 0;
    KOS_OBJ_ID arg;
    size_t     to_write;
    size_t     num_writ = 0;

    TRY(_get_file_object(frame, this_obj, &file, 1));

    /* TODO support multiple buffers */

    arg = KOS_array_read(frame, args_obj, 0);

    TRY_OBJID(arg);

    if (GET_OBJ_TYPE(arg) != OBJ_BUFFER)
        RAISE_EXCEPTION(str_err_not_buffer);

    to_write = (size_t)KOS_get_buffer_size(arg);

    if (to_write > 0)
        num_writ = fwrite(KOS_buffer_data(arg), 1, to_write, file);

    if (num_writ < to_write)
        RAISE_EXCEPTION(str_err_file_write);

_error:
    return error ? KOS_BADPTR : this_obj;
}

/* @item file file.prototype.eof
 *
 *     file.prototype.eof
 *
 * A boolean read-only flag indicating whether the read/write pointer has
 * reached the end of the file object.
 */
static KOS_OBJ_ID _get_file_eof(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj)
{
    FILE *file   = 0;
    int   error  = _get_file_object(frame, this_obj, &file, 1);
    int   status = 0;

    if ( ! error)
        status = feof(file);

    return error ? KOS_BADPTR : KOS_BOOL(status);
}

/* @item file file.prototype.error
 *
 *     file.prototype.error
 *
 * A boolean read-only flag indicating whether there was an error during the
 * last file operation on the file object.
 */
static KOS_OBJ_ID _get_file_error(KOS_FRAME  frame,
                                  KOS_OBJ_ID this_obj,
                                  KOS_OBJ_ID args_obj)
{
    FILE *file   = 0;
    int   error  = _get_file_object(frame, this_obj, &file, 1);
    int   status = 0;

    if ( ! error)
        status = ferror(file);

    return error ? KOS_BADPTR : KOS_BOOL(status);
}

/* @item file file.prototype.size
 *
 *     file.prototype.size
 *
 * Read-only size of the opened file object.
 */
static KOS_OBJ_ID _get_file_size(KOS_FRAME  frame,
                                 KOS_OBJ_ID this_obj,
                                 KOS_OBJ_ID args_obj)
{
    int   error = KOS_SUCCESS;
    FILE *file  = 0;
    long  orig_pos;
    long  size  = 0;

    TRY(_get_file_object(frame, this_obj, &file, 1));

    orig_pos = ftell(file);
    if (orig_pos < 0)
        RAISE_EXCEPTION(str_err_cannot_get_size);

    if (fseek(file, 0, SEEK_END))
        RAISE_EXCEPTION(str_err_cannot_get_size);

    size = ftell(file);
    if (size < 0)
        RAISE_EXCEPTION(str_err_cannot_get_size);

    if (fseek(file, orig_pos, SEEK_SET))
        RAISE_EXCEPTION(str_err_cannot_get_size);

_error:
    return error ? KOS_BADPTR : KOS_new_int(frame, (int64_t)size);
}

/* @item file file.prototype.position
 *
 *     file.prototype.position
 *
 * Read-only position of the read/write pointer in the opened file object.
 */
static KOS_OBJ_ID _get_file_pos(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj)
{
    FILE *file  = 0;
    int   error = KOS_SUCCESS;
    long  pos   = 0;

    TRY(_get_file_object(frame, this_obj, &file, 1));

    pos = ftell(file);

    if (pos < 0)
        RAISE_EXCEPTION(str_err_cannot_get_position);

_error:
    return error ? KOS_BADPTR : KOS_new_int(frame, (int64_t)pos);
}

/* @item file file.prototype.seek()
 *
 *     file.prototype.seek(pos)
 *
 * Moves the read/write pointer to a different position in the file.
 *
 * Returns the file object for which the pointer has been moved.
 *
 * `pos` is the new, absolute position in the file where the pointer
 * is moved.  If it is negative, the pointer is moved relative to the end of
 * the file.  If it is a float, it is converted to integer using floor mode.
 *
 * Throws an exception if the pointer cannot be moved for whatever reason.
 */
static KOS_OBJ_ID _set_file_pos(KOS_FRAME  frame,
                                KOS_OBJ_ID this_obj,
                                KOS_OBJ_ID args_obj)
{
    int        error  = KOS_SUCCESS;
    FILE      *file   = 0;
    int        whence = SEEK_SET;
    int64_t    pos;
    KOS_OBJ_ID arg;

    TRY(_get_file_object(frame, this_obj, &file, 1));

    arg = KOS_array_read(frame, args_obj, 0);

    TRY_OBJID(arg);

    TRY(KOS_get_integer(frame, arg, &pos));

    if (pos < 0)
        whence = SEEK_END;

    if (fseek(file, (long)pos, whence))
        RAISE_EXCEPTION(str_err_cannot_set_position);

_error:
    return error ? KOS_BADPTR : this_obj;
}

/* @item file is_file()
 *
 *     is_file(pathname)
 *
 * Determines whether a file exists.
 *
 * Returns `true` if `pathname` exists and is a file, or `false` otherwise.
 */
static KOS_OBJ_ID _is_file(KOS_FRAME  frame,
                           KOS_OBJ_ID this_obj,
                           KOS_OBJ_ID args_obj)
{
    int                error        = KOS_SUCCESS;
    KOS_OBJ_ID         ret          = KOS_BADPTR;
    KOS_OBJ_ID         filename_obj = KOS_array_read(frame, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;

    _KOS_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(frame, filename_obj, &filename_cstr));

    _fix_path_separators(&filename_cstr);

    ret = KOS_BOOL(_KOS_does_file_exist(filename_cstr.buffer));

_error:
    _KOS_vector_destroy(&filename_cstr);

    return ret;
}

/* @item file remove()
 *
 *     remove(pathname)
 *
 * Deletes a file `pathname`.
 *
 * Returns `true` if the file was successfuly deleted or `false` if
 * the file could not be deleted or if it did not exist in the first
 * place.
 */
static KOS_OBJ_ID _remove(KOS_FRAME  frame,
                          KOS_OBJ_ID this_obj,
                          KOS_OBJ_ID args_obj)
{
    int                error        = KOS_SUCCESS;
    KOS_OBJ_ID         ret          = KOS_BADPTR;
    KOS_OBJ_ID         filename_obj = KOS_array_read(frame, args_obj, 0);
    struct _KOS_VECTOR filename_cstr;

    _KOS_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(frame, filename_obj, &filename_cstr));

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

static int _add_std_file(KOS_FRAME  frame,
                         KOS_OBJ_ID proto,
                         KOS_OBJ_ID str_name,
                         FILE      *file)
{
    int error = KOS_SUCCESS;

    KOS_OBJ_ID obj = KOS_new_object_with_prototype(frame, proto);

    TRY_OBJID(obj);

    KOS_object_set_private(*OBJPTR(OBJECT, obj), file);

    error = KOS_module_add_global(frame, str_name, obj, 0);

_error:
    return error;
}

#define TRY_ADD_STD_FILE(frame, proto, name, file)                             \
do {                                                                           \
    static const char str_name[] = name;                                       \
    KOS_OBJ_ID        str        = KOS_context_get_cstring((frame), str_name); \
    TRY(_add_std_file((frame), (proto), str, (file)));                         \
} while (0)

int _KOS_module_file_init(KOS_FRAME frame)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID proto;

    TRY_ADD_CONSTRUCTOR(    frame,        "file",      _open,           1, &proto);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "close",     _close,          0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "print",     _print,          0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "print_",    _print_,         0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "read_line", _read_line,      0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "read_some", _read_some,      0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "release",   _close,          0);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "seek",      _set_file_pos,   1);
    TRY_ADD_MEMBER_FUNCTION(frame, proto, "write",     _write,          1);
    TRY_ADD_MEMBER_PROPERTY(frame, proto, "eof",       _get_file_eof,   0);
    TRY_ADD_MEMBER_PROPERTY(frame, proto, "error",     _get_file_error, 0);
    TRY_ADD_MEMBER_PROPERTY(frame, proto, "position",  _get_file_pos,   0);
    TRY_ADD_MEMBER_PROPERTY(frame, proto, "size",      _get_file_size,  0);

    TRY_ADD_FUNCTION(       frame,        "is_file",   _is_file,        1);
    TRY_ADD_FUNCTION(       frame,        "remove",    _remove,         1);

    /* @item file stderr
     *
     *     stderr
     *
     * Write-only file object corresponding to standard error.
     */
    TRY_ADD_STD_FILE(       frame, proto, "stderr",    stderr);

    /* @item file stdin
     *
     *     stdin
     *
     * Read-only file object corresponding to standard input.
     */
    TRY_ADD_STD_FILE(       frame, proto, "stdin",     stdin);

    /* @item file stdout
     *
     *     stdout
     *
     * Write-only file object corresponding to standard output.
     *
     * Calling `file.stdout.print()` is equivalent to `lang.print()`.
     */
    TRY_ADD_STD_FILE(       frame, proto, "stdout",    stdout);

_error:
    return error;
}

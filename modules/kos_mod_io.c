/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "kos_mod_io.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_debug.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_system.h"
#include "../core/kos_try.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#   include <io.h>
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'fopen/getenv': This function may be unsafe */
#else
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif
#ifdef __linux__
#   include <sys/sysmacros.h>
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_err_bad_flags,                  "incorrect file open flags");
KOS_DECLARE_STATIC_CONST_STRING(str_err_file_not_open,              "file not open or not a file object");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_buffer_size,        "buffer size out of range");
KOS_DECLARE_STATIC_CONST_STRING(str_err_io_module_priv_data_failed, "failed to get private data from module io");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer,                 "argument to file.read_some is not a buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_buffer_or_str,          "argument to file.write is neither a buffer nor a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_many_to_read,           "requested read size exceeds buffer size limit");
KOS_DECLARE_STATIC_CONST_STRING(str_position,                       "position");
KOS_DECLARE_STATIC_CONST_STRING(str_read,                           "read");
KOS_DECLARE_STATIC_CONST_STRING(str_write,                          "write");

static KOS_OBJ_ID get_file_pos(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

static KOS_OBJ_ID set_file_pos(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj);

static void fix_path_separators(KOS_VECTOR *buf)
{
    char       *ptr = buf->buffer;
    char *const end = ptr + buf->size;

    for ( ; ptr < end; ptr++) {
        const char c = *ptr;
        if (c == '/' || c == '\\')
            *ptr = KOS_PATH_SEPARATOR;
    }
}

static void finalize(KOS_CONTEXT ctx,
                     void       *priv)
{
    if (priv)
        fclose((FILE *)priv);
}

KOS_DECLARE_PRIVATE_CLASS(file_priv_class);

enum CLOSE_FLAG {
    NO_CLOSE,
    AUTO_CLOSE
};

static KOS_OBJ_ID make_file_object(KOS_CONTEXT     ctx,
                                   KOS_OBJ_ID      io_module_obj,
                                   FILE           *file,
                                   enum CLOSE_FLAG auto_close)
{
    KOS_OBJ_ID obj_id;
    int        error = KOS_SUCCESS;

    obj_id = KOS_atomic_read_relaxed_obj(OBJPTR(MODULE, io_module_obj)->priv);
    if (IS_BAD_PTR(obj_id) || kos_seq_fail())
        RAISE_EXCEPTION_STR(str_err_io_module_priv_data_failed);

    obj_id = KOS_array_read(ctx, obj_id, 0);
    TRY_OBJID(obj_id);

    obj_id = KOS_new_object_with_private(ctx, obj_id, &file_priv_class, auto_close ? finalize : KOS_NULL);
    TRY_OBJID(obj_id);

    KOS_object_set_private_ptr(obj_id, file);

cleanup:
    return error ? KOS_BADPTR : obj_id;
}

/* @item io file()
 *
 *     file(filename, flags = rw)
 *
 * File object class.
 *
 * Returns opened file object.
 *
 * `filename` is the path to the file.
 *
 * `flags` is a string, which specifies file open mode compatible with
 * the C `fopen()` function.  It is normally recommended to use the
 * shorthand flag constants: `io.ro`, `io.rw` or the auxiliary
 * file functions `io.open()`, `io.create()` and `io.append()`
 * instead of specifying the flags explicitly.
 *
 * It is recommended to use the `io.file` class in conjunction with
 * the `with` statement.
 *
 * Example:
 *
 *     > with const f = io.file("my.txt", io.create_flag) { f.print("hello") }
 */
KOS_DECLARE_STATIC_CONST_STRING(str_filename, "filename");
KOS_DECLARE_STATIC_CONST_STRING(str_flags,    "flags");
KOS_DECLARE_STATIC_CONST_STRING(str_rw,       "r+b");

static const KOS_ARG_DESC open_args[3] = {
    { KOS_CONST_ID(str_filename), KOS_BADPTR           },
    { KOS_CONST_ID(str_flags),    KOS_CONST_ID(str_rw) },
    { KOS_BADPTR,                 KOS_BADPTR           }
};

static KOS_OBJ_ID kos_open(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int        error        = KOS_SUCCESS;
    int        stored_errno = 0;
    KOS_OBJ_ID filename_obj;
    KOS_OBJ_ID flags_obj;
    FILE      *file         = KOS_NULL;
    KOS_LOCAL  this_;
    KOS_LOCAL  args;
    KOS_LOCAL  ret;
    KOS_VECTOR filename_cstr;
    KOS_VECTOR flags_cstr;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_vector_init(&filename_cstr);
    KOS_vector_init(&flags_cstr);

    KOS_init_locals(ctx, 3, &this_, &args, &ret);

    this_.o = this_obj;
    args.o  = args_obj;

    filename_obj = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators(&filename_cstr);

    /* TODO use own flags */
    /* TODO add flag to avoid cloexec */

    flags_obj = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(flags_obj);

    if (GET_OBJ_TYPE(flags_obj) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_bad_flags);

    TRY(KOS_string_to_cstr_vec(ctx, flags_obj, &flags_cstr));

#ifndef _WIN32
    TRY(KOS_append_cstr(ctx, &flags_cstr, "e", 1));
#endif

    KOS_suspend_context(ctx);

    file = fopen(filename_cstr.buffer, flags_cstr.buffer);

    if ( ! file)
        stored_errno = errno;

#ifndef _WIN32
    if (file)
        (void)fcntl(fileno(file), F_SETFD, FD_CLOEXEC);
#endif

    KOS_resume_context(ctx);

    if ( ! file) {
        KOS_raise_errno_value(ctx, "fopen", stored_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    ret.o = KOS_new_object_with_private(ctx, this_.o, &file_priv_class, finalize);
    TRY_OBJID(ret.o);

    TRY(KOS_set_builtin_dynamic_property(ctx,
                                         ret.o,
                                         KOS_CONST_ID(str_position),
                                         KOS_get_module(ctx),
                                         get_file_pos,
                                         set_file_pos));

    KOS_object_set_private_ptr(ret.o, file);
    file = KOS_NULL;

cleanup:
    KOS_vector_destroy(&flags_cstr);
    KOS_vector_destroy(&filename_cstr);
    if (file)
        fclose(file);

    ret.o = KOS_destroy_top_locals(ctx, &this_, &ret);

    return error ? KOS_BADPTR : ret.o;
}

#ifdef _WIN32
static FILE *to_file(KOS_CONTEXT ctx, HANDLE *handle, const char *mode)
{
    FILE *file         = KOS_NULL;
    int   fd           = -1;
    int   stored_errno = 0;

    KOS_suspend_context(ctx);

    fd = _open_osfhandle((intptr_t)*handle, _O_BINARY | (strcmp(mode, "rb") ? _O_WRONLY : _O_RDONLY));

    if (fd == -1)
        /* This is not correct, but unlikely to happen */
        stored_errno = EPIPE;
    else {
        *handle = INVALID_HANDLE_VALUE;

        file = _fdopen(fd, mode);

        if ( ! file) {
            stored_errno = errno;

            _close(fd);
        }
    }

    KOS_resume_context(ctx);

    if ( ! file)
        KOS_raise_errno_value(ctx, "_fdopen", stored_errno);

    return file;
}
#else
static FILE *to_file(KOS_CONTEXT ctx, int *fd, const char *mode)
{
    FILE *file;
    int   stored_errno = 0;

    KOS_suspend_context(ctx);

    file = fdopen(*fd, mode);

    if ( ! file || kos_seq_fail())
        stored_errno = errno;

    KOS_resume_context(ctx);

    if ( ! file)
        KOS_raise_errno_value(ctx, "fdopen", stored_errno);
    else
        *fd = -1;

    return file;
}
#endif

/* @item io pipe()
 *
 *     pipe()
 *
 * Pipe class.
 *
 * Returns a pipe object, which contains two properties:
 *
 *  * `read` - file object which is the read end of the pipe.
 *  * `write` - file object which is the write end of the pipe.
 *
 * `pipe` objects are most useful with `os.spawn()`.
 */
static KOS_OBJ_ID kos_pipe(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL ret;
    KOS_LOCAL file_obj;
    KOS_LOCAL io_module;
    KOS_LOCAL this_;
    FILE     *file         = KOS_NULL;
#ifdef _WIN32
    HANDLE    read_pipe    = INVALID_HANDLE_VALUE;
    HANDLE    write_pipe   = INVALID_HANDLE_VALUE;
#else
    int       read_pipe    = -1;
    int       write_pipe   = -1;
#endif
    int       stored_errno = 0;
    int       error        = KOS_SUCCESS;

    KOS_init_local(     ctx, &ret);
    KOS_init_local(     ctx, &file_obj);
    KOS_init_local(     ctx, &io_module);
    KOS_init_local_with(ctx, &this_, this_obj);

    io_module.o = KOS_get_module(ctx);
    TRY_OBJID(io_module.o);

    ret.o = KOS_new_object_with_prototype(ctx, this_.o);
    TRY_OBJID(ret.o);

    KOS_suspend_context(ctx);

#ifdef _WIN32
    if ( ! CreatePipe(&read_pipe, &write_pipe, KOS_NULL, 0x10000))
        /* This is not correct, but unlikely to happen */
        stored_errno = EPIPE;
#else
    {
        int pipe_fd[2];

        if ( ! kos_seq_fail() && (pipe(pipe_fd) == 0)) {
            read_pipe  = pipe_fd[0];
            write_pipe = pipe_fd[1];
            (void)fcntl(read_pipe, F_SETFD, FD_CLOEXEC);
            (void)fcntl(write_pipe, F_SETFD, FD_CLOEXEC);
        }
        else
            stored_errno = errno ? errno : EPIPE;
    }
#endif

    KOS_resume_context(ctx);

    if (stored_errno) {
        KOS_raise_errno_value(ctx, "pipe", stored_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    file = to_file(ctx, &read_pipe, "rb");
    if ( ! file)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    file_obj.o = make_file_object(ctx, io_module.o, file, AUTO_CLOSE);
    TRY_OBJID(file_obj.o);
    file = KOS_NULL;

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_read), file_obj.o));

    file = to_file(ctx, &write_pipe, "wb");
    if ( ! file)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    file_obj.o = make_file_object(ctx, io_module.o, file, AUTO_CLOSE);
    TRY_OBJID(file_obj.o);
    file = KOS_NULL;

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_write), file_obj.o));

cleanup:
    if (file)
        fclose(file);
#ifdef _WIN32
    if (read_pipe)
        CloseHandle(read_pipe);
    if (write_pipe)
        CloseHandle(write_pipe);
#else
    if (read_pipe != -1)
        close(read_pipe);
    if (write_pipe != -1)
        close(write_pipe);
#endif

    ret.o = KOS_destroy_top_locals(ctx, &this_, &ret);

    return error ? KOS_BADPTR : ret.o;
}

static int get_file_object(KOS_CONTEXT                ctx,
                           KOS_OBJ_ID                 file_obj,
                           FILE                     **file)
{
    *file = (FILE *)KOS_object_get_private(file_obj, &file_priv_class);

    if ( ! *file) {
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_file_not_open));
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

KOS_FILE_HANDLE KOS_io_get_file(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  file_obj)
{
    FILE *file = KOS_NULL;

    assert( ! IS_BAD_PTR(file_obj));

    file = (FILE *)KOS_object_get_private(file_obj, &file_priv_class);

    if ( ! file)
        KOS_raise_exception(ctx, KOS_CONST_ID(str_err_file_not_open));

#ifdef _WIN32
    if (file) {
        const KOS_FILE_HANDLE handle = (KOS_FILE_HANDLE)_get_osfhandle(_fileno(file));

        if (handle == INVALID_HANDLE_VALUE)
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_file_not_open));

        return handle;
    }

    return INVALID_HANDLE_VALUE;
#else
    return file;
#endif
}

/* @item io file.prototype.close()
 *
 *     file.prototype.close()
 *
 * Closes the file object if it is still opened.
 */
static KOS_OBJ_ID kos_close(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    FILE *file  = KOS_NULL;
    int   error = KOS_SUCCESS;

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT)
        RAISE_EXCEPTION_STR(str_err_file_not_open);

    file = (FILE *)KOS_object_swap_private(this_obj, &file_priv_class, KOS_NULL);

    if (file)
        fclose(file);

cleanup:
    return error ? KOS_BADPTR : KOS_VOID;
}

/* @item io file.prototype.print()
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
static KOS_OBJ_ID print(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    FILE      *file  = (FILE *)KOS_object_get_private(this_obj, &file_priv_class);
    int        error = KOS_SUCCESS;
    KOS_VECTOR cstr;

    KOS_vector_init(&cstr);

    if (file) {

        KOS_LOCAL this_;

        TRY(KOS_print_to_cstr_vec(ctx, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

        KOS_init_local_with(ctx, &this_, this_obj);

        KOS_suspend_context(ctx);

        if (cstr.size) {
            cstr.buffer[cstr.size - 1] = '\n';
            fwrite(cstr.buffer, 1, cstr.size, file);
        }
        else
            fprintf(file, "\n");

        KOS_resume_context(ctx);

        this_obj = KOS_destroy_top_local(ctx, &this_);
    }

cleanup:
    KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_obj;
}

/* @item io file.prototype.flush()
 *
 *     file.prototype.flush()
 *
 * Flushes the file buffer.
 *
 * All the outstanding written bytes in the underlying buffer are written
 * to the file.  For files being read, the seek pointer is moved to the
 * end of the file.
 *
 * Returns the file object itself.
 */
static KOS_OBJ_ID flush(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL this_;
    FILE     *file  = (FILE *)KOS_object_get_private(this_obj, &file_priv_class);
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &this_, this_obj);

    if (file) {
        int stored_errno = 0;
        int result;

        KOS_suspend_context(ctx);

        result = fflush(file);
        if (result || kos_seq_fail()) {
            result       = 1;
            stored_errno = errno;
        }

        KOS_resume_context(ctx);

        if (result) {
            KOS_raise_errno_value(ctx, "fflush", stored_errno);
            error = KOS_ERROR_EXCEPTION;
        }
    }

    this_.o = KOS_destroy_top_local(ctx, &this_);

    return error ? KOS_BADPTR : this_.o;
}

static int is_eol(char c)
{
    return c == '\n' || c == '\r';
}

/* @item io file.prototype.read_line()
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
KOS_DECLARE_STATIC_CONST_STRING(str_reserved_size, "reserved_size");

static const KOS_ARG_DESC read_line_args[2] = {
    { KOS_CONST_ID(str_reserved_size), TO_SMALL_INT(4096) },
    { KOS_BADPTR,                      KOS_BADPTR         }
};

static KOS_OBJ_ID read_line(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int        error      = KOS_SUCCESS;
    FILE      *file       = KOS_NULL;
    int        size_delta;
    int        last_size  = 0;
    int        num_read;
    int64_t    iarg       = 0;
    KOS_OBJ_ID arg;
    KOS_VECTOR buf;
    KOS_OBJ_ID line       = KOS_BADPTR;

    assert(KOS_get_array_size(args_obj) >= 1);

    KOS_vector_init(&buf);

    TRY(get_file_object(ctx, this_obj, &file));

    arg = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &iarg));

    if (iarg <= 0 || iarg > INT_MAX-1)
        RAISE_EXCEPTION_STR(str_err_invalid_buffer_size);

    size_delta = (int)iarg + 1;

    KOS_suspend_context(ctx);

    do {
        char *ret;

        if (KOS_vector_resize(&buf, (size_t)(last_size + size_delta))) {
            KOS_resume_context(ctx);
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        ret = fgets(buf.buffer + last_size, size_delta, file);

        if ( ! ret) {
            if (ferror(file)) {
                const int stored_errno = errno;
                KOS_resume_context(ctx);
                KOS_raise_errno_value(ctx, "fgets", stored_errno);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
            else
                break;
        }

        num_read = (int)strlen(buf.buffer + last_size);

        last_size += num_read;
    } while (num_read != 0 &&
             num_read+1 == size_delta &&
             ! is_eol(buf.buffer[last_size-1]));

    KOS_resume_context(ctx);

    line = KOS_new_string(ctx, buf.buffer, (unsigned)last_size);

cleanup:
    KOS_vector_destroy(&buf);
    return error ? KOS_BADPTR : line;
}

/* @item io file.prototype.read_some()
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
KOS_DECLARE_STATIC_CONST_STRING(str_size,   "size");
KOS_DECLARE_STATIC_CONST_STRING(str_buffer, "buffer");

static const KOS_ARG_DESC read_some_args[3] = {
    { KOS_CONST_ID(str_size),   TO_SMALL_INT(4096) },
    { KOS_CONST_ID(str_buffer), KOS_VOID           },
    { KOS_BADPTR,               KOS_BADPTR         }
};

static KOS_OBJ_ID read_some(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int        error        = KOS_SUCCESS;
    int        stored_errno = 0;
    FILE      *file         = KOS_NULL;
    KOS_LOCAL  args;
    KOS_LOCAL  buf;
    KOS_OBJ_ID arg;
    uint8_t   *data;
    uint32_t   offset;
    int64_t    to_read;
    size_t     num_read;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_init_local(     ctx, &buf);
    KOS_init_local_with(ctx, &args, args_obj);

    TRY(get_file_object(ctx, this_obj, &file));

    arg = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &to_read));

    if (to_read < 1)
        to_read = 1;

    buf.o = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(buf.o);

    if (buf.o == KOS_VOID)
        buf.o = KOS_new_buffer(ctx, 0);
    else if (GET_OBJ_TYPE(buf.o) != OBJ_BUFFER)
        RAISE_EXCEPTION_STR(str_err_not_buffer);

    offset = KOS_get_buffer_size(buf.o);

    if (to_read > (int64_t)(0xFFFFFFFFU - offset))
        RAISE_EXCEPTION_STR(str_err_too_many_to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + to_read)));

    data = KOS_buffer_data(ctx, buf.o);

    if ( ! data)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    KOS_suspend_context(ctx);

    num_read = fread(data + offset, 1, (size_t)to_read, file);

    if (num_read < (size_t)to_read && ferror(file))
        stored_errno = errno;

    KOS_resume_context(ctx);

    assert(num_read <= (size_t)to_read);

    TRY(KOS_buffer_resize(ctx, buf.o, (unsigned)(offset + num_read)));

    if (stored_errno) {
        KOS_raise_errno_value(ctx, "fread", stored_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    buf.o = KOS_destroy_top_locals(ctx, &args, &buf);

    return error ? KOS_BADPTR : buf.o;
}

/* @item io file.prototype.write()
 *
 *     file.prototype.write(values...)
 *
 * Writes strings or buffers containing bytes into an opened file object.
 *
 * Returns the file object to which data has been written.
 *
 * Each argument is either a buffer or a string object.  Empty buffers
 * or strings are ignored and nothing is written to the file.
 *
 * If an argument is a string, it is converted to UTF-8 bytes representation
 * before being written.
 *
 * Invoking this function without any arguments doesn't write anything
 * to the file but ensures that the file object is correct.
 */
static KOS_OBJ_ID kos_write(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int            error    = KOS_SUCCESS;
    const uint32_t num_args = KOS_get_array_size(args_obj);
    FILE          *file     = KOS_NULL;
    uint32_t       i_arg;
    KOS_VECTOR     cstr;
    KOS_LOCAL      print_args;
    KOS_LOCAL      arg;
    KOS_LOCAL      args;
    KOS_LOCAL      this_;

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, 4, &print_args, &arg, &args, &this_);

    args.o  = args_obj;
    this_.o = this_obj;

    TRY(get_file_object(ctx, this_.o, &file));

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        size_t num_writ     = 0;
        int    stored_errno = 0;

        arg.o = KOS_array_read(ctx, args.o, i_arg);
        TRY_OBJID(arg.o);

        if (GET_OBJ_TYPE(arg.o) == OBJ_BUFFER) {

            const size_t to_write = (size_t)KOS_get_buffer_size(arg.o);

            if (to_write > 0) {

                const uint8_t *data = KOS_buffer_data_const(arg.o);

                if (kos_is_heap_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg.o)->data))) {

                    if (KOS_vector_resize(&cstr, to_write)) {
                        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                        RAISE_ERROR(KOS_ERROR_EXCEPTION);
                    }

                    memcpy(cstr.buffer, data, to_write);
                    data = (uint8_t *)cstr.buffer;
                }
                else {
                    assert(kos_is_tracked_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg.o)->data)));
                }

                KOS_suspend_context(ctx);

                num_writ = fwrite(data, 1, to_write, file);

                if (num_writ < to_write)
                    stored_errno = errno;

                KOS_resume_context(ctx);
            }

            if (stored_errno) {
                KOS_raise_errno_value(ctx, "fwrite", stored_errno);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
        }
        else if (GET_OBJ_TYPE(arg.o) == OBJ_STRING) {

            if (IS_BAD_PTR(print_args.o)) {
                print_args.o = KOS_new_array(ctx, 1);
                TRY_OBJID(print_args.o);
            }

            TRY(KOS_array_write(ctx, print_args.o, 0, arg.o));

            TRY(KOS_print_to_cstr_vec(ctx, print_args.o, KOS_DONT_QUOTE, &cstr, " ", 1));

            if (cstr.size) {
                KOS_suspend_context(ctx);

                num_writ = fwrite(cstr.buffer, 1, cstr.size - 1, file);

                if (num_writ < cstr.size - 1)
                    stored_errno = errno;

                KOS_resume_context(ctx);
            }

            if (stored_errno) {
                KOS_raise_errno_value(ctx, "fwrite", stored_errno);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            cstr.size = 0;
        }
        else
            RAISE_EXCEPTION_STR(str_err_not_buffer_or_str);
    }

cleanup:
    this_.o = KOS_destroy_top_locals(ctx, &print_args, &this_);

    KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_.o;
}

/* @item io file.prototype.eof
 *
 *     file.prototype.eof
 *
 * A boolean read-only flag indicating whether the read/write pointer has
 * reached the end of the file object.
 */
static KOS_OBJ_ID get_file_eof(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    FILE *file   = KOS_NULL;
    int   error  = get_file_object(ctx, this_obj, &file);
    int   status = 0;

    if ( ! error)
        status = feof(file);

    return error ? KOS_BADPTR : KOS_BOOL(status);
}

/* @item io file.prototype.error
 *
 *     file.prototype.error
 *
 * A boolean read-only flag indicating whether there was an error during the
 * last file operation on the file object.
 */
static KOS_OBJ_ID get_file_error(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    FILE *file   = KOS_NULL;
    int   error  = get_file_object(ctx, this_obj, &file);
    int   status = 0;

    if ( ! error)
        status = ferror(file);

    return error ? KOS_BADPTR : KOS_BOOL(status);
}

/* @item io file.prototype.fd
 *
 *     file.prototype.fd
 *
 * An integer number representing the underlying file descriptor number.
 */
static KOS_OBJ_ID get_file_fd(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    FILE *file   = KOS_NULL;
    int   error  = get_file_object(ctx, this_obj, &file);
    int   fd     = 0;

#ifdef _WIN32
#   define kos_fileno _fileno
#else
#   define kos_fileno fileno
#endif

    if ( ! error)
        fd = kos_fileno(file);

    return error ? KOS_BADPTR : KOS_new_int(ctx, fd);
}

#ifdef _WIN32
static int64_t get_epoch_time_us(const LARGE_INTEGER *time)
{
    const int64_t epoch  = (int64_t)116444736 * (int64_t)100000000;
    int64_t       time_s = (int64_t)time->QuadPart;

    /* Convert from 100s of ns to us */
    time_s /= 10;

    /* Convert from Windows time (1601) to Epoch time (1970) */
    time_s -= epoch;

    return time_s;
}
#endif

/* @item io file.prototype.info
 *
 *     file.prototype.info
 *
 * A read-only property which returns information about the file.
 *
 * This property populates a new object on every read.
 *
 * The property is an object containing the following elements:
 *
 *  * type       - type of the object, one of the following strings:
 *                 `"file"`, `"directory"`, `"char"` (character device),
 *                 `"device"` (block device), `"fifo"`, `"symlink"`, `"socket"`
 *  * size       - size of the file object, in bytes
 *  * blocks     - number of blocks allocated for the file object
 *  * block_size - ideal block size for reading/writing
 *  * flags      - bitflags representing OS-specific file attributes
 *  * inode      - inode number
 *  * hard_links - number of hard links
 *  * uid        - id of the owner
 *  * gid        - id of the owning group
 *  * device     - array containing major and minor device numbers if the object is a device
 *  * atime      - last access time, in microseconds since Epoch
 *  * mtime      - last modification time, in microseconds since Epoch
 *  * ctime      - creation time, in microseconds since Epoch
 *
 * The precision of time properties is OS-dependent.  For example,
 * on POSIX-compatible OS-es these properties have 1 second precision.
 *
 * On Windows, the `inode`, `uid` and `gid` elements are not produced.
 *
 * The `device` element is only produced for device objects on some
 * OS-es, for example Linux, *BSD, or MacOSX.
 */
static KOS_OBJ_ID get_file_info(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    FILE     *file  = KOS_NULL;
    int       error = get_file_object(ctx, this_obj, &file);
    KOS_LOCAL info;
    KOS_LOCAL aux;

    KOS_init_locals(ctx, 2, &aux, &info);

    if ( ! error) {
#define SET_INT_PROPERTY(name, value)                                           \
        do {                                                                    \
            KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                    \
                                                                                \
            KOS_OBJ_ID obj_id = KOS_new_int(ctx, (int64_t)(value));             \
            TRY_OBJID(obj_id);                                                  \
                                                                                \
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_name), obj_id)); \
        } while (0)

#ifdef _WIN32
        HANDLE             handle;
        FILE_BASIC_INFO    basic_info   = { 0 };
        FILE_STANDARD_INFO std_info     = { 0 };
        FILE_STORAGE_INFO  storage_info = { 0 };
        BOOL               ok           = FALSE;
        BOOL               have_storage = FALSE;

        KOS_DECLARE_STATIC_CONST_STRING(str_type,          "type");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_file,     "file");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dir,      "directory");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dev,      "device");
        KOS_DECLARE_STATIC_CONST_STRING(str_err_file_stat, "unable to obtain information about file");

        KOS_suspend_context(ctx);

        handle = (HANDLE)_get_osfhandle(_fileno(file));

        ok = handle != INVALID_HANDLE_VALUE;

        if (ok)
            ok = GetFileInformationByHandleEx(handle, FileBasicInfo,
                                              &basic_info, sizeof(basic_info));

        if (ok)
            ok = GetFileInformationByHandleEx(handle, FileStandardInfo,
                                              &std_info, sizeof(std_info));

        if (ok)
            have_storage = GetFileInformationByHandleEx(handle, FileStorageInfo,
                                                        &storage_info, sizeof(storage_info));

        KOS_resume_context(ctx);

        if ( ! ok)
            RAISE_EXCEPTION_STR(str_err_file_stat);

        if ( ! have_storage)
            storage_info.LogicalBytesPerSector = 1;

        info.o = KOS_new_object(ctx);
        TRY_OBJID(info.o);

        SET_INT_PROPERTY("flags",      basic_info.FileAttributes);
        SET_INT_PROPERTY("hard_links", std_info.NumberOfLinks);
        SET_INT_PROPERTY("size",       std_info.EndOfFile.QuadPart);
        SET_INT_PROPERTY("blocks",     (std_info.AllocationSize.QuadPart
                                            + storage_info.LogicalBytesPerSector - 1)
                                                / storage_info.LogicalBytesPerSector);
        SET_INT_PROPERTY("block_size", storage_info.LogicalBytesPerSector);
        SET_INT_PROPERTY("atime",      get_epoch_time_us(&basic_info.LastAccessTime));
        SET_INT_PROPERTY("mtime",      get_epoch_time_us(&basic_info.LastWriteTime));
        SET_INT_PROPERTY("ctime",      get_epoch_time_us(&basic_info.ChangeTime));

        if (basic_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
        else if (basic_info.FileAttributes & FILE_ATTRIBUTE_DEVICE)
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dev)));
        else
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
#else
        struct stat st;
        int         stored_errno = 0;

        KOS_DECLARE_STATIC_CONST_STRING(str_type,         "type");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_file,    "file");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dir,     "directory");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_char,    "char");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_device,  "device");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_fifo,    "fifo");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_link,    "symlink");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_socket,  "socket");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_unknown, "unknown");

        KOS_suspend_context(ctx);

        error = fstat(fileno(file), &st);

        if (error)
            stored_errno = errno;

        KOS_resume_context(ctx);

        if (error) {
            KOS_raise_errno_value(ctx, "fstat", stored_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        info.o = KOS_new_object(ctx);
        TRY_OBJID(info.o);

        SET_INT_PROPERTY("flags",      st.st_mode);
        SET_INT_PROPERTY("hard_links", st.st_nlink);
        SET_INT_PROPERTY("inode",      st.st_ino);
        SET_INT_PROPERTY("uid",        st.st_uid);
        SET_INT_PROPERTY("gid",        st.st_gid);
        SET_INT_PROPERTY("size",       st.st_size);
        SET_INT_PROPERTY("blocks",     st.st_blocks);
        SET_INT_PROPERTY("block_size", st.st_blksize);
        SET_INT_PROPERTY("atime",      st.st_atime * (int64_t)1000000);
        SET_INT_PROPERTY("mtime",      st.st_mtime * (int64_t)1000000);
        SET_INT_PROPERTY("ctime",      st.st_ctime * (int64_t)1000000);

#if !defined(__HAIKU__)
        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            aux.o = KOS_new_array(ctx, 2);
            TRY_OBJID(aux.o);

            TRY(KOS_array_write(ctx, aux.o, 0, TO_SMALL_INT(major(st.st_rdev))));
            TRY(KOS_array_write(ctx, aux.o, 1, TO_SMALL_INT(minor(st.st_rdev))));
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type_device), aux.o));
        }
#endif

        if (S_ISREG(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
        else if (S_ISDIR(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
        else if (S_ISCHR(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_char)));
        else if (S_ISBLK(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_device)));
        else if (S_ISFIFO(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_fifo)));
        else if (S_ISLNK(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_link)));
        else if (S_ISSOCK(st.st_mode))
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_socket)));
        else
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_unknown)));
#endif
    }

cleanup:
    info.o = KOS_destroy_top_locals(ctx, &aux, &info);

    return error ? KOS_BADPTR : info.o;
}

/* @item io file.prototype.size
 *
 *     file.prototype.size
 *
 * Read-only size of the opened file object.
 *
 * When writing data to a file, its size may not be immediately refleced, until flush is performed.
 */
static KOS_OBJ_ID get_file_size(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    int     error = KOS_SUCCESS;
    FILE   *file  = KOS_NULL;
    int64_t size  = 0;

    TRY(get_file_object(ctx, this_obj, &file));

#ifdef _WIN32
    {
        HANDLE             handle;
        FILE_STANDARD_INFO std_info = { 0 };
        BOOL               ok       = FALSE;

        KOS_DECLARE_STATIC_CONST_STRING(str_err_file_stat, "unable to obtain information about file");

        KOS_suspend_context(ctx);

        handle = (HANDLE)_get_osfhandle(_fileno(file));

        ok = handle != INVALID_HANDLE_VALUE;

        if (ok)
            ok = GetFileInformationByHandleEx(handle, FileStandardInfo,
                                              &std_info, sizeof(std_info));

        KOS_resume_context(ctx);

        if ( ! ok)
            RAISE_EXCEPTION_STR(str_err_file_stat);

        size = (int64_t)std_info.EndOfFile.QuadPart;
    }
#else
    {
        struct stat st;
        int         stored_errno = 0;

        KOS_suspend_context(ctx);

        error = fstat(fileno(file), &st);

        if (error)
            stored_errno = errno;

        KOS_resume_context(ctx);

        if (error) {
            KOS_raise_errno_value(ctx, "fstat", stored_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        size = (int64_t)st.st_size;
    }
#endif

cleanup:
    return error ? KOS_BADPTR : KOS_new_int(ctx, size);
}

/* @item io file.prototype.position
 *
 *     file.prototype.position
 *
 * Read-only position of the read/write pointer in the opened file object.
 *
 * This property is also added to every file object and is writable
 * and shadows the `position` property from the prototype.
 * Writing the `position` property on an open file object will move the
 * file pointer in the same way as invoking the `seek` function.
 */
static KOS_OBJ_ID get_file_pos(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    FILE *file         = KOS_NULL;
    int   error        = KOS_SUCCESS;
    int   stored_errno = 0;
    long  pos          = 0;

    TRY(get_file_object(ctx, this_obj, &file));

    KOS_suspend_context(ctx);

    pos = ftell(file);

    if (pos < 0)
        stored_errno = errno;

    KOS_resume_context(ctx);

    if (stored_errno) {
        KOS_raise_errno_value(ctx, "ftell", stored_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

cleanup:
    return error ? KOS_BADPTR : KOS_new_int(ctx, (int64_t)pos);
}

/* @item io file.prototype.seek()
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
 *
 * Each open file object also has a `postition` property which can be
 * written to in order to move the file pointer instead of invoking `seek`.
 */
KOS_DECLARE_STATIC_CONST_STRING(str_pos, "pos");

static const KOS_ARG_DESC set_file_pos_args[2] = {
    { KOS_CONST_ID(str_pos), KOS_BADPTR },
    { KOS_BADPTR,            KOS_BADPTR }
};

static KOS_OBJ_ID set_file_pos(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    int        error  = KOS_SUCCESS;
    FILE      *file   = KOS_NULL;
    int        whence = SEEK_SET;
    int64_t    pos;
    KOS_OBJ_ID arg;
    KOS_LOCAL  this_;

    assert(KOS_get_array_size(args_obj) >= 1);

    TRY(get_file_object(ctx, this_obj, &file));

    arg = KOS_array_read(ctx, args_obj, 0);

    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &pos));

    if (pos < 0)
        whence = SEEK_END;

    KOS_init_local_with(ctx, &this_, this_obj);

    KOS_suspend_context(ctx);

    if (fseek(file, (long)pos, whence)) {
        const int stored_errno = errno;
        KOS_resume_context(ctx);
        KOS_raise_errno_value(ctx, "fseek", stored_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    KOS_resume_context(ctx);

    this_obj = KOS_destroy_top_local(ctx, &this_);

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

static int add_std_file(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  module_obj,
                        KOS_OBJ_ID  name_obj,
                        FILE       *file)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID obj;
    KOS_LOCAL  module;
    KOS_LOCAL  name;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local_with(ctx, &name,   name_obj);

    obj = make_file_object(ctx, module.o, file, NO_CLOSE);
    TRY_OBJID(obj);

    error = KOS_module_add_global(ctx, module.o, name.o, obj, KOS_NULL);

cleanup:
    KOS_destroy_top_locals(ctx, &name, &module);

    return error;
}

#define TRY_ADD_STD_FILE(ctx, module, name, file)                       \
do {                                                                    \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                    \
    TRY(add_std_file((ctx), (module), KOS_CONST_ID(str_name), (file))); \
} while (0)

int kos_module_io_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int        error = KOS_SUCCESS;
    KOS_LOCAL  module;
    KOS_LOCAL  file_proto;
    KOS_LOCAL  priv;
    KOS_OBJ_ID pipe_proto;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &file_proto);
    KOS_init_local(     ctx, &priv);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,               "file",      kos_open,       open_args, &file_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "close",     kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "flush",     flush,          KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "print",     print,          KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "read_line", read_line,      read_line_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "read_some", read_some,      read_some_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "release",   kos_close,      KOS_NULL);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "seek",      set_file_pos,   set_file_pos_args);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, file_proto.o, "write",     kos_write,      KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "eof",       get_file_eof,   KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "error",     get_file_error, KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "fd",        get_file_fd,    KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "info",      get_file_info,  KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "position",  get_file_pos,   KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, file_proto.o, "size",      get_file_size,  KOS_NULL);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,               "pipe",      kos_pipe,       KOS_NULL, &pipe_proto);

    priv.o = KOS_new_array(ctx, 1);
    TRY_OBJID(priv.o);

    KOS_atomic_write_relaxed_ptr(OBJPTR(MODULE, module.o)->priv, priv.o);

    TRY(KOS_array_write(ctx, priv.o, 0, file_proto.o));

    /* @item io stdin
     *
     *     stdin
     *
     * Read-only file object corresponding to standard input.
     */
    TRY_ADD_STD_FILE(ctx, module.o, "stdin", stdin);

    /* @item io stdout
     *
     *     stdout
     *
     * Write-only file object corresponding to standard output.
     *
     * Calling `file.stdout.print()` is equivalent to `base.print()`.
     */
    TRY_ADD_STD_FILE(ctx, module.o, "stdout", stdout);

    /* @item io stderr
     *
     *     stderr
     *
     * Write-only file object corresponding to standard error.
     */
    TRY_ADD_STD_FILE(ctx, module.o, "stderr", stderr);

cleanup:
    KOS_destroy_top_locals(ctx, &priv, &module);

    return error;
}

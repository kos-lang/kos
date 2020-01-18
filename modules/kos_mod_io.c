/*
 * Copyright (c) 2014-2020 Chris Dragan
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
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_memory.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_system.h"
#include "../core/kos_try.h"
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
#endif
#ifdef __linux__
#   include <sys/sysmacros.h>
#endif
#ifdef _WIN32
#   pragma warning( disable : 4996 ) /* 'fopen': This function may be unsafe */
#endif

static const char str_err_bad_flags[]           = "incorrect file open flags";
static const char str_err_cannot_get_position[] = "unable to obtain file position";
static const char str_err_cannot_get_size[]     = "unable to obtain file size";
static const char str_err_cannot_open_file[]    = "unable to open file";
static const char str_err_cannot_set_position[] = "unable to update file position";
static const char str_err_file_not_open[]       = "file not open";
static const char str_err_file_read[]           = "file read error";
static const char str_err_file_stat[]           = "unable to obtain information about file";
static const char str_err_file_write[]          = "file write error";
static const char str_err_invalid_buffer_size[] = "buffer size out of range";
static const char str_err_not_buffer[]          = "argument to file.read_some is not a buffer";
static const char str_err_not_buffer_or_str[]   = "argument to file.write is neither a buffer nor a string";
static const char str_err_too_many_to_read[]    = "requested read size exceeds buffer size limit";
static const char str_position[]                = "position";

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

/* @item io file()
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
static KOS_OBJ_ID kos_open(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           KOS_OBJ_ID  args_obj)
{
    int        error        = KOS_SUCCESS;
    KOS_OBJ_ID ret          = KOS_BADPTR;
    KOS_OBJ_ID pos_id;
    FILE      *file         = 0;
    KOS_OBJ_ID filename_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR filename_cstr;
    KOS_VECTOR flags_cstr;

    kos_vector_init(&filename_cstr);
    kos_vector_init(&flags_cstr);

    TRY_OBJID(filename_obj);

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 4, &this_obj, &args_obj, &ret, &filename_obj));
    }

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators(&filename_cstr);

    /* TODO use own flags */

    if (KOS_get_array_size(args_obj) > 1) {
        KOS_OBJ_ID flags_obj = KOS_array_read(ctx, args_obj, 1);

        TRY_OBJID(flags_obj);

        if (GET_OBJ_TYPE(flags_obj) != OBJ_STRING)
            RAISE_EXCEPTION(str_err_bad_flags);

        TRY(KOS_string_to_cstr_vec(ctx, flags_obj, &flags_cstr));
    }

    KOS_suspend_context(ctx);

    file = fopen(filename_cstr.buffer, flags_cstr.size ? flags_cstr.buffer : "r+b");

    KOS_resume_context(ctx);

    if ( ! file)
        RAISE_EXCEPTION(str_err_cannot_open_file);

    ret = KOS_new_object_with_prototype(ctx, this_obj);
    TRY_OBJID(ret);

    pos_id = KOS_new_const_ascii_string(ctx, str_position, sizeof(str_position) - 1);
    TRY_OBJID(pos_id);

    TRY(KOS_set_builtin_dynamic_property(ctx,
                                         ret,
                                         pos_id,
                                         KOS_get_module(ctx),
                                         get_file_pos,
                                         set_file_pos));

    OBJPTR(OBJECT, ret)->finalize = finalize;

    KOS_object_set_private_ptr(ret, file);
    file = 0;

cleanup:
    kos_vector_destroy(&flags_cstr);
    kos_vector_destroy(&filename_cstr);
    if (file)
        fclose(file);

    return error ? KOS_BADPTR : ret;
}

static int get_file_object(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  this_obj,
                           FILE      **file,
                           int         must_be_open)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(this_obj));

    if (GET_OBJ_TYPE(this_obj) != OBJ_OBJECT)
        RAISE_EXCEPTION(str_err_file_not_open);

    *file = (FILE *)KOS_object_get_private_ptr(this_obj);

    if (must_be_open && ! *file)
        RAISE_EXCEPTION(str_err_file_not_open);

cleanup:
    return error;
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
    FILE *file  = 0;
    int   error = get_file_object(ctx, this_obj, &file, 0);

    if ( ! error && file) {
        fclose(file);
        KOS_object_set_private_ptr(this_obj, (void *)0);
    }

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
    FILE      *file  = 0;
    int        error = get_file_object(ctx, this_obj, &file, 0);
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    if ( ! error && file) {

        TRY(KOS_print_to_cstr_vec(ctx, args_obj, KOS_DONT_QUOTE, &cstr, " ", 1));

        kos_track_refs(ctx, 1, &this_obj);

        KOS_suspend_context(ctx);

        if (cstr.size) {
            cstr.buffer[cstr.size - 1] = '\n';
            fwrite(cstr.buffer, 1, cstr.size, file);
        }
        else
            fprintf(file, "\n");

        KOS_resume_context(ctx);

        kos_untrack_refs(ctx, 1);
    }

cleanup:
    kos_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_obj;
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
static KOS_OBJ_ID read_line(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int        error      = KOS_SUCCESS;
    FILE      *file       = 0;
    int        size_delta = 4096;
    int        last_size  = 0;
    int        num_read;
    KOS_VECTOR buf;
    KOS_OBJ_ID line       = KOS_BADPTR;

    kos_vector_init(&buf);

    TRY(get_file_object(ctx, this_obj, &file, 1));

    if (KOS_get_array_size(args_obj) > 0) {

        int64_t iarg = 0;

        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &iarg));

        if (iarg <= 0 || iarg > INT_MAX-1)
            RAISE_EXCEPTION(str_err_invalid_buffer_size);

        size_delta = (int)iarg + 1;
    }

    KOS_suspend_context(ctx);

    do {
        char *ret;

        if (kos_vector_resize(&buf, (size_t)(last_size + size_delta))) {
            KOS_resume_context(ctx);
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        ret = fgets(buf.buffer + last_size, size_delta, file);

        if ( ! ret) {
            if (ferror(file)) {
                KOS_resume_context(ctx);
                RAISE_EXCEPTION(str_err_file_read);
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
    kos_vector_destroy(&buf);
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
static KOS_OBJ_ID read_some(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    FILE      *file  = 0;
    KOS_OBJ_ID buf   = KOS_BADPTR;
    uint8_t   *data;
    uint32_t   offset;
    int64_t    to_read;
    size_t     num_read;

    TRY(get_file_object(ctx, this_obj, &file, 1));

    if (KOS_get_array_size(args_obj) > 0) {
        KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

        TRY_OBJID(arg);

        TRY(KOS_get_integer(ctx, arg, &to_read));
    }
    else
        to_read = 0x1000U;

    if (to_read < 1)
        to_read = 1;

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 2, &args_obj, &buf));
    }

    if (KOS_get_array_size(args_obj) > 1) {
        buf = KOS_array_read(ctx, args_obj, 1);

        TRY_OBJID(buf);

        if (GET_OBJ_TYPE(buf) != OBJ_BUFFER)
            RAISE_EXCEPTION(str_err_not_buffer);
    }
    else
        buf = KOS_new_buffer(ctx, 0);

    offset = KOS_get_buffer_size(buf);

    if (to_read > (int64_t)(0xFFFFFFFFU - offset))
        RAISE_EXCEPTION(str_err_too_many_to_read);

    TRY(KOS_buffer_resize(ctx, buf, (unsigned)(offset + to_read)));

    data = KOS_buffer_data(ctx, buf);

    if ( ! data)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    KOS_suspend_context(ctx);

    num_read = fread(data + offset, 1, (size_t)to_read, file);

    KOS_resume_context(ctx);

    assert(num_read <= (size_t)to_read);

    TRY(KOS_buffer_resize(ctx, buf, (unsigned)(offset + num_read)));

    if (num_read < (size_t)to_read && ferror(file))
        RAISE_EXCEPTION(str_err_file_read);

cleanup:
    return error ? KOS_BADPTR : buf;
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
    int            error      = KOS_SUCCESS;
    const uint32_t num_args   = KOS_get_array_size(args_obj);
    FILE      *    file       = 0;
    uint32_t       i_arg;
    KOS_OBJ_ID     arg        = KOS_BADPTR;
    KOS_OBJ_ID     print_args = KOS_BADPTR;
    KOS_VECTOR     cstr;

    kos_vector_init(&cstr);

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 4, &this_obj, &args_obj, &arg, &print_args));
    }

    TRY(get_file_object(ctx, this_obj, &file, 1));

    for (i_arg = 0; i_arg < num_args; i_arg++) {

        size_t num_writ = 0;

        arg = KOS_array_read(ctx, args_obj, i_arg);
        TRY_OBJID(arg);

        if (GET_OBJ_TYPE(arg) == OBJ_BUFFER) {

            const size_t to_write = (size_t)KOS_get_buffer_size(arg);

            if (to_write > 0) {

                uint8_t *data = KOS_buffer_data_volatile(arg);

                if (kos_is_heap_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg)->data))) {

                    if (kos_vector_resize(&cstr, to_write)) {
                        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                        RAISE_ERROR(KOS_ERROR_EXCEPTION);
                    }

                    memcpy(cstr.buffer, data, to_write);
                    data = (uint8_t *)cstr.buffer;
                }
                else {
                    assert(kos_is_tracked_object(KOS_atomic_read_relaxed_obj(OBJPTR(BUFFER, arg)->data)));
                }

                KOS_suspend_context(ctx);

                num_writ = fwrite(data, 1, to_write, file);

                KOS_resume_context(ctx);
            }

            if (num_writ < to_write)
                RAISE_EXCEPTION(str_err_file_write); /* TODO error from errno */
        }
        else if (GET_OBJ_TYPE(arg) == OBJ_STRING) {

            if (IS_BAD_PTR(print_args)) {
                print_args = KOS_new_array(ctx, 1);
                TRY_OBJID(print_args);
            }

            TRY(KOS_array_write(ctx, print_args, 0, arg));

            TRY(KOS_print_to_cstr_vec(ctx, print_args, KOS_DONT_QUOTE, &cstr, " ", 1));

            if (cstr.size) {
                KOS_suspend_context(ctx);

                num_writ = fwrite(cstr.buffer, 1, cstr.size - 1, file);

                KOS_resume_context(ctx);
            }

            if (num_writ < cstr.size - 1)
                RAISE_EXCEPTION(str_err_file_write);

            cstr.size = 0;
        }
        else
            RAISE_EXCEPTION(str_err_not_buffer_or_str);
    }

cleanup:
    kos_vector_destroy(&cstr);

    return error ? KOS_BADPTR : this_obj;
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
    FILE *file   = 0;
    int   error  = get_file_object(ctx, this_obj, &file, 1);
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
    FILE *file   = 0;
    int   error  = get_file_object(ctx, this_obj, &file, 1);
    int   status = 0;

    if ( ! error)
        status = ferror(file);

    return error ? KOS_BADPTR : KOS_BOOL(status);
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
 *  * atime      - last access time, in microseconds since Epoch)
 *  * mtime      - last modification time, in microseconds since Epoch)
 *  * ctime      - creation time, in microseconds since Epoch)
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
    FILE      *file  = 0;
    int        error = get_file_object(ctx, this_obj, &file, 1);
    KOS_OBJ_ID info  = KOS_BADPTR;

    if ( ! error) {
#define SET_INT_PROPERTY(name, value)                                         \
        do {                                                                  \
            KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                  \
                                                                              \
            KOS_OBJ_ID obj_id = KOS_new_int(ctx, (int64_t)(value));           \
            TRY_OBJID(obj_id);                                                \
                                                                              \
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_name), obj_id)); \
        } while (0)

#ifdef _WIN32
        HANDLE                    handle;
        FILE_BASIC_INFO           basic_info   = { 0 };
        FILE_STANDARD_INFO        std_info     = { 0 };
        FILE_STORAGE_INFO         storage_info = { 0 };
        BOOL                      ok           = FALSE;
        BOOL                      have_storage = FALSE;
        int                       pushed       = 0;

        KOS_DECLARE_STATIC_CONST_STRING(str_type,        "type");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_file,   "file");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dir,    "directory");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dev,    "device");

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
            RAISE_EXCEPTION(str_err_file_stat);

        if ( ! have_storage)
            storage_info.LogicalBytesPerSector = 1;

        TRY(KOS_push_locals(ctx, &pushed, 1, &info));

        info = KOS_new_object(ctx);
        TRY_OBJID(info);

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
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
        else if (basic_info.FileAttributes & FILE_ATTRIBUTE_DEVICE)
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dev)));
        else
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
#else
        struct stat st;
        int         pushed = 0;

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

        KOS_resume_context(ctx);

        if (error)
            RAISE_EXCEPTION(str_err_file_stat); /* TODO error from errno */

        TRY(KOS_push_locals(ctx, &pushed, 1, &info));

        info = KOS_new_object(ctx);
        TRY_OBJID(info);

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
            KOS_OBJ_ID a = KOS_new_array(ctx, 2);
            TRY_OBJID(a);

            pushed = 0;
            TRY(KOS_push_locals(ctx, &pushed, 1, &a));

            TRY(KOS_array_write(ctx, a, 0, TO_SMALL_INT(major(st.st_rdev))));
            TRY(KOS_array_write(ctx, a, 1, TO_SMALL_INT(minor(st.st_rdev))));
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type_device), a));

            KOS_pop_locals(ctx, pushed);
        }
#endif

        if (S_ISREG(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
        else if (S_ISDIR(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
        else if (S_ISCHR(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_char)));
        else if (S_ISBLK(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_device)));
        else if (S_ISFIFO(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_fifo)));
        else if (S_ISLNK(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_link)));
        else if (S_ISSOCK(st.st_mode))
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_socket)));
        else
            TRY(KOS_set_property(ctx, info, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_unknown)));
#endif
    }

cleanup:
    return error ? KOS_BADPTR : info;
}

/* @item io file.prototype.size
 *
 *     file.prototype.size
 *
 * Read-only size of the opened file object.
 */
static KOS_OBJ_ID get_file_size(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  this_obj,
                                KOS_OBJ_ID  args_obj)
{
    int   error = KOS_SUCCESS;
    FILE *file  = 0;
    long  orig_pos;
    long  size  = 0;

    TRY(get_file_object(ctx, this_obj, &file, 1));

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

cleanup:
    return error ? KOS_BADPTR : KOS_new_int(ctx, (int64_t)size);
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
    FILE *file  = 0;
    int   error = KOS_SUCCESS;
    long  pos   = 0;

    TRY(get_file_object(ctx, this_obj, &file, 1));

    KOS_suspend_context(ctx);

    pos = ftell(file);

    KOS_resume_context(ctx);

    if (pos < 0)
        RAISE_EXCEPTION(str_err_cannot_get_position);

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
static KOS_OBJ_ID set_file_pos(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    int        error  = KOS_SUCCESS;
    FILE      *file   = 0;
    int        whence = SEEK_SET;
    int64_t    pos;
    KOS_OBJ_ID arg;

    TRY(get_file_object(ctx, this_obj, &file, 1));

    arg = KOS_array_read(ctx, args_obj, 0);

    TRY_OBJID(arg);

    TRY(KOS_get_integer(ctx, arg, &pos));

    if (pos < 0)
        whence = SEEK_END;

    kos_track_refs(ctx, 1, &this_obj);

    KOS_suspend_context(ctx);

    if (fseek(file, (long)pos, whence))
        RAISE_EXCEPTION(str_err_cannot_set_position);

    KOS_resume_context(ctx);

    kos_untrack_refs(ctx, 1);

cleanup:
    return error ? KOS_BADPTR : this_obj;
}

static int add_std_file(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  module,
                        KOS_OBJ_ID  proto,
                        KOS_OBJ_ID  str_name,
                        FILE       *file)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    KOS_OBJ_ID obj;

    TRY(KOS_push_locals(ctx, &pushed, 3, &module, &proto, &str_name));

    obj = KOS_new_object_with_prototype(ctx, proto);
    TRY_OBJID(obj);

    KOS_object_set_private_ptr(obj, file);

    error = KOS_module_add_global(ctx, module, str_name, obj, 0);

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error;
}

#define TRY_ADD_STD_FILE(ctx, module, proto, name, file)                         \
do {                                                                             \
    KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                             \
    TRY(add_std_file((ctx), (module), (proto), KOS_CONST_ID(str_name), (file))); \
} while (0)

int kos_module_io_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int        error  = KOS_SUCCESS;
    int        pushed = 0;
    KOS_OBJ_ID proto  = KOS_BADPTR;

    TRY(KOS_push_locals(ctx, &pushed, 2, &module, &proto));

    TRY_ADD_CONSTRUCTOR(    ctx, module,        "file",      kos_open,       1, &proto);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "close",     kos_close,      0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "print",     print,          0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "read_line", read_line,      0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "read_some", read_some,      0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "release",   kos_close,      0);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "seek",      set_file_pos,   1);
    TRY_ADD_MEMBER_FUNCTION(ctx, module, proto, "write",     kos_write,      0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, "eof",       get_file_eof,   0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, "error",     get_file_error, 0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, "info",      get_file_info,  0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, "position",  get_file_pos,   0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module, proto, "size",      get_file_size,  0);

    /* @item io stderr
     *
     *     stderr
     *
     * Write-only file object corresponding to standard error.
     */
    TRY_ADD_STD_FILE(       ctx, module, proto, "stderr",    stderr);

    /* @item io stdin
     *
     *     stdin
     *
     * Read-only file object corresponding to standard input.
     */
    TRY_ADD_STD_FILE(       ctx, module, proto, "stdin",     stdin);

    /* @item io stdout
     *
     *     stdout
     *
     * Write-only file object corresponding to standard output.
     *
     * Calling `file.stdout.print()` is equivalent to `base.print()`.
     */
    TRY_ADD_STD_FILE(       ctx, module, proto, "stdout",    stdout);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_system_internal.h"
#include "../core/kos_try.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
#   include <errno.h>
#   include <dirent.h>
#   include <sys/types.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_deep,     "deep");
KOS_DECLARE_STATIC_CONST_STRING(str_filename, "filename");
KOS_DECLARE_STATIC_CONST_STRING(str_follow,   "follow");
KOS_DECLARE_STATIC_CONST_STRING(str_path,     "path");

static const KOS_CONVERT filename_arg[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_filename),
    KOS_DEFINE_TAIL_ARG()
};

static const KOS_CONVERT path_arg[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_path),
    KOS_DEFINE_TAIL_ARG()
};

static const KOS_CONVERT info_args[3] = {
    KOS_DEFINE_MANDATORY_ARG(str_filename         ),
    KOS_DEFINE_OPTIONAL_ARG( str_follow, KOS_FALSE),
    KOS_DEFINE_TAIL_ARG()
};

static const KOS_CONVERT mkdir_args[3] = {
    { KOS_CONST_ID(str_path), KOS_BADPTR, 0, 0, KOS_NATIVE_STRING_PTR },
    { KOS_CONST_ID(str_deep), KOS_FALSE,  0, 0, KOS_NATIVE_BOOL8      },
    KOS_DEFINE_TAIL_ARG()
};

static void fix_path_separators_buf(char *ptr, char *end)
{
    for ( ; ptr < end; ptr++) {
        const char c = *ptr;
        if (c == '/' || c == '\\')
            *ptr = KOS_PATH_SEPARATOR;
    }
}

static void fix_path_separators_vec(KOS_VECTOR *buf)
{
    fix_path_separators_buf(buf->buffer, buf->buffer + buf->size);
}

/* @item fs file_exists()
 *
 *     file_exists(filename)
 *
 * Determines whether a file exists.
 *
 * Returns `true` if `filename` exists and is a file, or `false` otherwise.
 */
static KOS_OBJ_ID file_exists(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  this_obj,
                              KOS_OBJ_ID  args_obj)
{
    int        error;
    KOS_OBJ_ID ret          = KOS_BADPTR;
    KOS_OBJ_ID filename_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR filename_cstr;

    KOS_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators_vec(&filename_cstr);

    ret = KOS_BOOL(KOS_does_file_exist(filename_cstr.buffer));

cleanup:
    KOS_vector_destroy(&filename_cstr);

    return ret;
}

#ifdef _WIN32
static int64_t get_epoch_time_us(const FILETIME *time)
{
    const int64_t  epoch = (int64_t)116444736 * (int64_t)100000000;
    int64_t        time_s;
    ULARGE_INTEGER li;

    li.u.LowPart  = time->dwLowDateTime;
    li.u.HighPart = time->dwHighDateTime;

    time_s = (int64_t)li.QuadPart;

    /* Convert from 100s of ns to us */
    time_s /= 10;

    /* Convert from Windows time (1601) to Epoch time (1970) */
    time_s -= epoch;

    return time_s;
}
#endif

/* @item fs info()
 *
 *     info(filename, follow = false)
 *
 * Returns object containing information about the object pointed by `filename`.
 *
 * The `follow` parameter specifies behavior for symbolic links.  If it is `false`
 * (the default), the function returns information about a symbolic link pointed
 * to by `filename`, otherwise it returns information about the actual object
 * after resolving the symbolic link.
 *
 * The returned object contains the following properties:
 *
 *  * type       - type of the object, one of the following strings:
 *                 `"file"`, `"directory"`, `"char"` (character device),
 *                 `"device"` (block device), `"fifo"`, `"symlink"`, `"socket"`
 *  * size       - size of the file object, in bytes
 *  * blocks     - number of blocks allocated for the file object
 *  * block_size - ideal block size for reading/writing
 *  * flags      - bitflags representing OS-specific file attributes
 *  * inode      - inode number
 *  * dev        - id of the device where the file is stored
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
 * On Windows, the `inode`, `dev`, `uid`, `gid`, `blocks`, `block_size` and `hard_links` properties are
 * not produced.
 *
 * The `device` property is only produced for device objects on some
 * OS-es, for example Linux, *BSD, or MacOSX.
 */
static KOS_OBJ_ID info(KOS_CONTEXT ctx,
                       KOS_OBJ_ID  this_obj,
                       KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL  data;
    KOS_OBJ_ID filename_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_OBJ_ID follow_obj;
    KOS_VECTOR filename_cstr;
    int        error;

    KOS_init_local(ctx, &data);

    KOS_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    follow_obj = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(follow_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators_vec(&filename_cstr);

#ifdef _WIN32
    {
#define SET_INT_PROPERTY(name, value)                                           \
        do {                                                                    \
            KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                    \
                                                                                \
            KOS_OBJ_ID obj_id = KOS_new_int(ctx, (int64_t)(value));             \
            TRY_OBJID(obj_id);                                                  \
                                                                                \
            TRY(KOS_set_property(ctx, data.o, KOS_CONST_ID(str_name), obj_id)); \
        } while (0)

        DWORD                     last_error = 0;
        WIN32_FILE_ATTRIBUTE_DATA attr;

        KOS_DECLARE_STATIC_CONST_STRING(str_type,          "type");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_file,     "file");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dir,      "directory");
        KOS_DECLARE_STATIC_CONST_STRING(str_type_dev,      "device");
        KOS_DECLARE_STATIC_CONST_STRING(str_err_file_stat, "unable to obtain information about file");

        KOS_suspend_context(ctx);

        if ( ! GetFileAttributesEx(filename_cstr.buffer, GetFileExInfoStandard, &attr))
            last_error = GetLastError();

        KOS_resume_context(ctx);

        if (last_error) {
            KOS_raise_last_error(ctx, filename_cstr.buffer, (unsigned)last_error);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        data.o = KOS_new_object(ctx);
        TRY_OBJID(data.o);

        SET_INT_PROPERTY("flags", attr.dwFileAttributes);
        SET_INT_PROPERTY("size",  ((uint64_t)(uint32_t)attr.nFileSizeHigh << 32) | (uint32_t)attr.nFileSizeLow);
        SET_INT_PROPERTY("atime", get_epoch_time_us(&attr.ftLastAccessTime));
        SET_INT_PROPERTY("mtime", get_epoch_time_us(&attr.ftLastWriteTime));
        SET_INT_PROPERTY("ctime", get_epoch_time_us(&attr.ftCreationTime));

        if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            TRY(KOS_set_property(ctx, data.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
        else if (attr.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
            TRY(KOS_set_property(ctx, data.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dev)));
        else
            TRY(KOS_set_property(ctx, data.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
    }
#else
    {
        struct stat st;
        int         ret;
        int         saved_errno = 0;

        KOS_suspend_context(ctx);

        errno = 0;

        if (KOS_get_bool(follow_obj))
            ret = stat(filename_cstr.buffer, &st);
        else
            ret = lstat(filename_cstr.buffer, &st);

        if (ret)
            saved_errno = errno;

        KOS_resume_context(ctx);

        if (ret) {
            KOS_raise_errno_value(ctx, filename_cstr.buffer, saved_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        data.o = kos_stat(ctx, &st);
    }
#endif

cleanup:
    KOS_vector_destroy(&filename_cstr);

    data.o = KOS_destroy_top_local(ctx, &data);

    return error ? KOS_BADPTR : data.o;
}

/* @item fs remove()
 *
 *     remove(filename)
 *
 * Deletes a file `filename`.
 *
 * If the file does not exist, returns `false`, otherwise returns `true`.
 *
 * If the file cannot be deleted, throws an error.
 */
static KOS_OBJ_ID kos_remove(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    int        error;
    KOS_OBJ_ID ret          = KOS_BADPTR;
    KOS_OBJ_ID filename_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR filename_cstr;

    KOS_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators_vec(&filename_cstr);

    KOS_suspend_context(ctx);

#ifdef _WIN32
    if (DeleteFile(filename_cstr.buffer))
        ret = KOS_TRUE;
    else {
        const unsigned last_error = (unsigned)GetLastError();

        if ((last_error == ERROR_PATH_NOT_FOUND) || (last_error == ERROR_FILE_NOT_FOUND))
            ret = KOS_FALSE;
        else {
            KOS_resume_context(ctx);

            KOS_raise_last_error(ctx, "DeleteFile", last_error);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
#else
    errno = 0;

    if ( ! unlink(filename_cstr.buffer))
        ret = KOS_TRUE;
    else if (errno == ENOENT)
        ret = KOS_FALSE;
    else {
        const int saved_errno = errno;

        KOS_resume_context(ctx);

        KOS_raise_errno_value(ctx, "unlink", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
#endif

    KOS_resume_context(ctx);

cleanup:
    KOS_vector_destroy(&filename_cstr);

    return ret;
}

/* @item fs cwd()
 *
 *     cwd()
 *
 * Returns a string containing the current working directory.
 *
 * Example:
 *
 *     > cwd()
 *     "/home/user"
 */
static KOS_OBJ_ID cwd(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  this_obj,
                      KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR path_cstr;
    KOS_OBJ_ID path_obj  = KOS_BADPTR;
    size_t     need_size = 128;
    int        error     = KOS_SUCCESS;

    KOS_vector_init(&path_cstr);

#ifdef _WIN32
    for (;;) {

        DWORD size;

        if (KOS_vector_resize(&path_cstr, need_size)) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        size = GetCurrentDirectory((DWORD)path_cstr.size, path_cstr.buffer);

        if ( ! size) {
            KOS_raise_last_error(ctx, "GetCurrentDirectory", (unsigned)GetLastError());
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if (size + 1 <= path_cstr.size)
            break;

        need_size = size;
    }
#else
    for (;;) {

        char *buf;

        if (KOS_vector_resize(&path_cstr, need_size)) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        errno = 0;

        buf = getcwd(path_cstr.buffer, path_cstr.size);

        if (buf)
            break;

        if ((errno != ERANGE) || (need_size >= 0x10000)) {
            KOS_raise_errno(ctx, "getcwd");
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        need_size *= 2;
    }
#endif

    path_obj = KOS_new_cstring(ctx, path_cstr.buffer);

cleanup:
    KOS_vector_destroy(&path_cstr);

    return error ? KOS_BADPTR : path_obj;
}

/* @item fs tempdir()
 *
 *     tempdir()
 *
 * Returns a string containing path to system-wide directory used for storing temporary files.
 *
 * On non-Windows operating systems this can be contents of the `TMPDIR` variable or the
 * path to `/tmp` directory, on Windows this can be the contents of the `TEMP` variable.
 *
 * Example:
 *
 *     > tempdir()
 *     "/tmp"
 */
static KOS_OBJ_ID tempdir(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
#ifdef _WIN32
    char buf[MAX_PATH + 1];

    const DWORD len = GetTempPath((DWORD)sizeof(buf), buf);

    return KOS_new_string(ctx, buf, len > MAX_PATH ? MAX_PATH : len);

#else

    KOS_DECLARE_STATIC_CONST_STRING(str_tmp,     "/tmp");
    KOS_DECLARE_STATIC_CONST_STRING(str_var_tmp, "/var/tmp");

    struct stat statbuf;

    const char *const tmpdir = getenv("TMPDIR");
    if (tmpdir)
        return KOS_new_cstring(ctx, tmpdir);

    if (0 == stat("/tmp", &statbuf) && S_ISDIR(statbuf.st_mode))
        return KOS_CONST_ID(str_tmp);

    if (0 == stat("/var/tmp", &statbuf) && S_ISDIR(statbuf.st_mode))
        return KOS_CONST_ID(str_var_tmp);

    return KOS_CONST_ID(str_tmp);
#endif
}

/* @item fs chdir()
 *
 *     chdir(path)
 *
 * Changes the current working directory to the one specified by `filename`.
 *
 * Throws an exception if the operation fails.
 *
 * Returns the argument.
 *
 * Example:
 *
 *     > chdir("/tmp")
 *     "/tmp"
 */
static KOS_OBJ_ID kos_chdir(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID path_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR path_cstr;
    int        error;

    KOS_vector_init(&path_cstr);

    TRY_OBJID(path_obj);

    TRY(KOS_string_to_cstr_vec(ctx, path_obj, &path_cstr));

    fix_path_separators_vec(&path_cstr);

#ifdef _WIN32
    if ( ! SetCurrentDirectory(path_cstr.buffer)) {
        KOS_raise_last_error(ctx, "SetCurrentDirectory", (unsigned)GetLastError());
        error = KOS_ERROR_EXCEPTION;
    }
#else
    errno = 0;

    if (chdir(path_cstr.buffer) == -1) {
        KOS_raise_errno(ctx, "chdir");
        error = KOS_ERROR_EXCEPTION;
    }
#endif

cleanup:
    KOS_vector_destroy(&path_cstr);

    return error ? KOS_BADPTR : path_obj;
}

#ifdef _WIN32
static int make_directory(KOS_CONTEXT ctx,
                          const char *path_cstr,
                          int         fail_if_exists)
{
    DWORD attr;

    KOS_suspend_context(ctx);

    attr = GetFileAttributes(path_cstr);

    if (fail_if_exists || (attr == INVALID_FILE_ATTRIBUTES) || ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)) {
        if ( ! CreateDirectory(path_cstr, KOS_NULL)) {
            const unsigned saved_last_error = (unsigned)GetLastError();

            KOS_resume_context(ctx);

            KOS_raise_last_error(ctx, "CreateDirectory", saved_last_error);
            return KOS_ERROR_EXCEPTION;
        }
    }

    KOS_resume_context(ctx);

    return KOS_SUCCESS;
}
#else
static int make_directory(KOS_CONTEXT ctx,
                          const char *path_cstr,
                          int         fail_if_exists)
{
    struct stat statbuf;

    KOS_suspend_context(ctx);

    errno = 0;

    if (mkdir(path_cstr, 0777)) {
        const int saved_errno = errno;

        if (fail_if_exists ||
            (saved_errno != EEXIST) ||
            (stat(path_cstr, &statbuf) != 0) ||
            ! S_ISDIR(statbuf.st_mode)) {

            KOS_resume_context(ctx);

            KOS_raise_errno_value(ctx, "mkdir", saved_errno);
            return KOS_ERROR_EXCEPTION;
        }
    }

    KOS_resume_context(ctx);

    return KOS_SUCCESS;
}
#endif

/* @item fs mkdir()
 *
 *     mkdir(path, deep = false)
 *
 * Creates a new directory specified by `path`.
 *
 * `path` can be absolute or relative to current working directory.
 *
 * `deep` specifies whether parent directories are also to be created.
 * If `deep` is true, missing parent directories will be created.
 * if `deep` is false, the parent directory must exist.  If `deep` is false
 * and the directory already exists, this function will fail.
 *
 * Throws an exception if the operation fails.
 *
 * Returns the `path` argument.
 *
 * Example:
 *
 *     > mkdir("/tmp/test")
 *     "/tmp/test"
 */
static KOS_OBJ_ID kos_mkdir(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL            path;
    struct KOS_MEMPOOL_S alloc;
    char                *path_cstr = KOS_NULL;
    size_t               path_len;
    int                  error;
    uint8_t              deep      = 0;

    KOS_mempool_init_small(&alloc, 512U);

    KOS_init_local(ctx, &path);

    path.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(path.o);

    TRY(KOS_extract_native_from_array(ctx, args_obj, "argument", mkdir_args, &alloc, &path_cstr, &deep));

    path_len = strlen(path_cstr);

    fix_path_separators_buf(path_cstr, path_cstr + path_len);

    if (deep && (path_len > 2)) {
        size_t pos = 1;

        for (;;) {
            char *const sep = strchr(path_cstr + pos, KOS_PATH_SEPARATOR);

            if ( ! sep)
                break;

            *sep = 0;

            TRY(make_directory(ctx, path_cstr, 0));

            *sep = KOS_PATH_SEPARATOR;

            pos = (size_t)(sep - path_cstr) + 1;
        }
    }

    TRY(make_directory(ctx, path_cstr, !deep));

cleanup:
    path.o = KOS_destroy_top_local(ctx, &path);

    KOS_mempool_destroy(&alloc);

    return error ? KOS_BADPTR : path.o;
}

/* @item fs rmdir()
 *
 *     rmdir(path)
 *
 * Removes an existing directory specified by `path`.
 *
 * `path` can be absolute or relative to current working directory.
 *
 * The directory to remove must be empty.
 *
 * If directory specified by `path` does not exist, the function returns `false`.  Otherwise
 * the function returns `true`.
 *
 * Throws an exception if the operation fails, e.g. if the directory cannot be removed, if it's
 * not empty or if it's a file.
 *
 * Example:
 *
 *     > rmdir("/tmp/test")
 */
static KOS_OBJ_ID kos_rmdir(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID path_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_OBJ_ID ret      = KOS_BADPTR;
    KOS_VECTOR path_cstr;
    int        error;

    KOS_vector_init(&path_cstr);

    TRY_OBJID(path_obj);

    TRY(KOS_string_to_cstr_vec(ctx, path_obj, &path_cstr));

    fix_path_separators_vec(&path_cstr);

    KOS_suspend_context(ctx);

#ifdef _WIN32
    if (RemoveDirectory(path_cstr.buffer))
        ret = KOS_TRUE;
    else {
        const unsigned last_error = (unsigned)GetLastError();

        if ((last_error == ERROR_PATH_NOT_FOUND) || (last_error == ERROR_FILE_NOT_FOUND))
            ret = KOS_FALSE;
        else {
            KOS_resume_context(ctx);

            KOS_raise_last_error(ctx, "RemoveDirectory", last_error);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
#else
    errno = 0;

    if ( ! rmdir(path_cstr.buffer))
        ret = KOS_TRUE;
    else if (errno == ENOENT)
        ret = KOS_FALSE;
    else {
        const int saved_errno = errno;

        KOS_resume_context(ctx);

        KOS_raise_errno_value(ctx, "rmdir", saved_errno);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
#endif

    KOS_resume_context(ctx);

cleanup:
    KOS_vector_destroy(&path_cstr);

    return ret;
}

KOS_DECLARE_PRIVATE_CLASS(dir_walk_class);

#ifdef _WIN32

static void dir_finalize(KOS_CONTEXT ctx,
                         void       *priv)
{
    if (priv)
        FindClose((HANDLE)priv);
}

static KOS_OBJ_ID get_next_dir_entry(KOS_CONTEXT ctx, KOS_OBJ_ID dir_walk_obj)
{
    KOS_LOCAL       dir_walk;
    HANDLE          h_find;
    WIN32_FIND_DATA find_data;
    DWORD           saved_error = 0;
    BOOL            found       = 0;

    KOS_init_local_with(ctx, &dir_walk, dir_walk_obj);

    /* TODO prevent conflict from multiple threads */
    h_find = (HANDLE)KOS_object_get_private(dir_walk.o, &dir_walk_class);
    if ((h_find == INVALID_HANDLE_VALUE) || ! h_find) {
        KOS_destroy_top_local(ctx, &dir_walk);
        return KOS_BADPTR;
    }

    KOS_suspend_context(ctx);

    found = FindNextFile(h_find, &find_data);
    if ( ! found)
        saved_error = GetLastError();

    KOS_resume_context(ctx);

    if ( ! found) {
        if (saved_error != ERROR_NO_MORE_FILES)
            KOS_raise_last_error(ctx, "FindNextFile", saved_error);

        FindClose(h_find);
        KOS_object_set_private_ptr(dir_walk.o, (void *)KOS_NULL);

        KOS_destroy_top_local(ctx, &dir_walk);
        return KOS_BADPTR;
    }

    KOS_destroy_top_local(ctx, &dir_walk);

    return KOS_new_cstring(ctx, find_data.cFileName);
}

static KOS_OBJ_ID find_first_file(KOS_CONTEXT ctx, KOS_VECTOR *path_cstr_vec, KOS_OBJ_ID *first_file_obj)
{
    KOS_LOCAL         obj;
    const size_t      old_size    = path_cstr_vec->size;
    static const char all_files[] = "\\*.*";
    int               error;

    KOS_init_local(ctx, &obj);

    assert(old_size > 0);
    assert(path_cstr_vec->buffer[old_size - 1] == 0);
    error = KOS_vector_resize(path_cstr_vec, old_size + sizeof(all_files) - 1);

    if ( ! error) {
        memcpy(&path_cstr_vec->buffer[old_size - 1], all_files, sizeof(all_files));

        obj.o = KOS_new_object_with_private(ctx, KOS_VOID, &dir_walk_class, dir_finalize);
    }
    else
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);

    if ( ! IS_BAD_PTR(obj.o)) {
        WIN32_FIND_DATA find_data;
        DWORD           saved_error;
        HANDLE          h_find;

        KOS_suspend_context(ctx);

        memset(&find_data, 0, sizeof(find_data));

        h_find = FindFirstFile(path_cstr_vec->buffer, &find_data);

        saved_error = (h_find == INVALID_HANDLE_VALUE) ? GetLastError() : 0;

        KOS_resume_context(ctx);

        if (h_find == INVALID_HANDLE_VALUE) {
            if (saved_error != ERROR_FILE_NOT_FOUND)
                KOS_raise_last_error(ctx, path_cstr_vec->buffer, (unsigned)saved_error);

            obj.o = KOS_BADPTR;
        }
        else {
            KOS_object_set_private_ptr(obj.o, (void *)h_find);

            *first_file_obj = KOS_new_cstring(ctx, find_data.cFileName);

            if (IS_BAD_PTR(*first_file_obj))
                obj.o = KOS_BADPTR;
        }
    }

    return KOS_destroy_top_local(ctx, &obj);
}

#else

static void dir_finalize(KOS_CONTEXT ctx,
                         void       *priv)
{
    if (priv)
        closedir((DIR *)priv);
}

static KOS_OBJ_ID get_next_dir_entry(KOS_CONTEXT ctx, KOS_OBJ_ID dir_walk_obj)
{
    KOS_LOCAL      dir_walk;
    DIR           *dir;
    struct dirent *entry;
    int            saved_errno;

    KOS_init_local_with(ctx, &dir_walk, dir_walk_obj);

    /* TODO prevent conflict from multiple threads */
    dir = (DIR *)KOS_object_get_private(dir_walk.o, &dir_walk_class);
    if ( ! dir) {
        KOS_destroy_top_local(ctx, &dir_walk);
        return KOS_BADPTR;
    }

    KOS_suspend_context(ctx);

    errno = 0;

    entry = readdir(dir);

    saved_errno = entry ? 0 : errno;

    KOS_resume_context(ctx);

    if ( ! entry) {

        if (saved_errno)
            KOS_raise_errno_value(ctx, "readdir", saved_errno);

        closedir(dir);
        KOS_object_set_private_ptr(dir_walk.o, (void *)KOS_NULL);

        KOS_destroy_top_local(ctx, &dir_walk);
        return KOS_BADPTR;
    }

    KOS_destroy_top_local(ctx, &dir_walk);

    return KOS_new_cstring(ctx, entry->d_name);
}

static KOS_OBJ_ID find_first_file(KOS_CONTEXT ctx, KOS_VECTOR *path_cstr_vec, KOS_OBJ_ID *first_file_obj)
{
    KOS_LOCAL obj;

    KOS_init_local(ctx, &obj);

    obj.o = KOS_new_object_with_private(ctx, KOS_VOID, &dir_walk_class, dir_finalize);

    if ( ! IS_BAD_PTR(obj.o)) {
        DIR *dir;

        KOS_suspend_context(ctx);

        errno = 0;

        dir = opendir(path_cstr_vec->buffer);

        KOS_resume_context(ctx);

        if (dir)
            KOS_object_set_private_ptr(obj.o, dir);
        else {
            KOS_raise_errno(ctx, path_cstr_vec->buffer);
            obj.o = KOS_BADPTR;
        }
    }

    if ( ! IS_BAD_PTR(obj.o)) {
        *first_file_obj = get_next_dir_entry(ctx, obj.o);

        if (IS_BAD_PTR(*first_file_obj))
            obj.o = KOS_BADPTR;
    }

    return KOS_destroy_top_local(ctx, &obj);
}

#endif

/* @item fs listdir()
 *
 *     listdir(path)
 *
 * A generator which produces filenames of subsequent files in the directory
 * specified by `path`.
 *
 * Example:
 *
 *     > const files = [ fs.listdir(".") ... ]
 */
static KOS_OBJ_ID listdir(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  regs_obj,
                          KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL  regs;
    KOS_LOCAL  dir_walk;
    KOS_OBJ_ID ret = KOS_BADPTR;
    int        error;

    KOS_init_locals(ctx, &regs, &dir_walk, kos_end_locals);

    regs.o = regs_obj;

    assert( ! IS_BAD_PTR(regs.o));
    assert(GET_OBJ_TYPE(regs.o) == OBJ_ARRAY);
    assert(KOS_get_array_size(regs.o) > 0);

    dir_walk.o = KOS_array_read(ctx, regs.o, 0);
    TRY_OBJID(dir_walk.o);

    if (GET_OBJ_TYPE(dir_walk.o) != OBJ_OBJECT) {

        KOS_VECTOR path_cstr;

        TRY(KOS_array_write(ctx, regs.o, 0, KOS_VOID));

        KOS_vector_init(&path_cstr);

        error = KOS_string_to_cstr_vec(ctx, dir_walk.o, &path_cstr);

        if (error)
            dir_walk.o = KOS_BADPTR;
        else  {
            fix_path_separators_vec(&path_cstr);

            dir_walk.o = find_first_file(ctx, &path_cstr, &ret);
        }

        KOS_vector_destroy(&path_cstr);

        TRY_OBJID(dir_walk.o);

        TRY(KOS_array_write(ctx, regs.o, 0, dir_walk.o));
    }
    else
        ret = get_next_dir_entry(ctx, dir_walk.o);

cleanup:
    KOS_destroy_top_locals(ctx, &regs, &dir_walk);

    return ret;
}

int kos_module_fs_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_FUNCTION(ctx,  module.o, "file_exists", file_exists, filename_arg);
    TRY_ADD_FUNCTION(ctx,  module.o, "info",        info,        info_args);
    TRY_ADD_FUNCTION(ctx,  module.o, "remove",      kos_remove,  filename_arg);
    TRY_ADD_FUNCTION(ctx,  module.o, "cwd",         cwd,         KOS_NULL);
    TRY_ADD_FUNCTION(ctx,  module.o, "tempdir",     tempdir,     KOS_NULL);
    TRY_ADD_FUNCTION(ctx,  module.o, "chdir",       kos_chdir,   path_arg);
    TRY_ADD_GENERATOR(ctx, module.o, "listdir",     listdir,     path_arg);
    TRY_ADD_FUNCTION(ctx,  module.o, "mkdir",       kos_mkdir,   mkdir_args);
    TRY_ADD_FUNCTION(ctx,  module.o, "rmdir",       kos_rmdir,   path_arg);

    /* @item fs path_separator
     *
     *     path_separator
     *
     * Constant string representing OS-specific path separator character.
     *
     * Example:
     *
     *     > path_separator
     *     "/"
     */
    TRY_ADD_STRING_CONSTANT(ctx, module.o, "path_separator", KOS_PATH_SEPARATOR_STR);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

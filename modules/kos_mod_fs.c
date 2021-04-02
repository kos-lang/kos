/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
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
#include "../core/kos_system.h"
#include "../core/kos_try.h"
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
#   include <errno.h>
#   include <dirent.h>
#   include <sys/dir.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif

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

/* @item fs is_file()
 *
 *     is_file(filename)
 *
 * Determines whether a file exists.
 *
 * Returns `true` if `filename` exists and is a file, or `false` otherwise.
 */
static KOS_OBJ_ID is_file(KOS_CONTEXT ctx,
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

    fix_path_separators(&filename_cstr);

    ret = KOS_BOOL(kos_does_file_exist(filename_cstr.buffer));

cleanup:
    KOS_vector_destroy(&filename_cstr);

    return ret;
}

/* @item fs remove()
 *
 *     remove(filename)
 *
 * Deletes a file `filename`.
 *
 * Returns `true` if the file was successfuly deleted or `false` if
 * the file could not be deleted or if it did not exist in the first
 * place.
 */
static KOS_OBJ_ID remove(KOS_CONTEXT ctx,
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

    fix_path_separators(&filename_cstr);

#ifdef _WIN32
    ret = KOS_BOOL(DeleteFile(filename_cstr.buffer));
#else
    ret = KOS_BOOL(unlink(filename_cstr.buffer) == 0);
#endif

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

    fix_path_separators(&path_cstr);

#ifdef _WIN32
    if ( ! SetCurrentDirectory(path_cstr.buffer)) {
        KOS_raise_last_error(ctx, "SetCurrentDirectory", (unsigned)GetLastError());
        error = KOS_ERROR_EXCEPTION;
    }
#else
    if (chdir(path_cstr.buffer) == -1) {
        KOS_raise_errno(ctx, "chdir");
        error = KOS_ERROR_EXCEPTION;
    }
#endif

cleanup:
    KOS_vector_destroy(&path_cstr);

    return error ? KOS_BADPTR : path_obj;
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

    KOS_init_locals(ctx, 2, &regs, &dir_walk);

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
            fix_path_separators(&path_cstr);

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

KOS_DECLARE_STATIC_CONST_STRING(str_filename, "filename");
KOS_DECLARE_STATIC_CONST_STRING(str_path,     "path");

static const KOS_ARG_DESC filename_arg[2] = {
    { KOS_CONST_ID(str_filename), KOS_BADPTR },
    { KOS_BADPTR,                 KOS_BADPTR }
};

static const KOS_ARG_DESC path_arg[2] = {
    { KOS_CONST_ID(str_path), KOS_BADPTR },
    { KOS_BADPTR,             KOS_BADPTR }
};

int kos_module_fs_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_FUNCTION(ctx,  module.o, "is_file", is_file,   filename_arg);
    TRY_ADD_FUNCTION(ctx,  module.o, "remove",  remove,    filename_arg);
    TRY_ADD_FUNCTION(ctx,  module.o, "cwd",     cwd,       KOS_NULL);
    TRY_ADD_FUNCTION(ctx,  module.o, "chdir",   kos_chdir, path_arg);
    TRY_ADD_GENERATOR(ctx, module.o, "listdir", listdir,   path_arg);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

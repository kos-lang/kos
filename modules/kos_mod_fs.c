/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
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
    char      *buf;
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
            KOS_raise_last_error(ctx, "GetCurrentDirectory", GetLastError());
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if (size + 1 <= path_cstr.size)
            break;

        need_size = size;
    }
#else
    for (;;) {

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
    int        error;
    KOS_OBJ_ID path_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR path_cstr;

    KOS_vector_init(&path_cstr);

    TRY_OBJID(path_obj);

    TRY(KOS_string_to_cstr_vec(ctx, path_obj, &path_cstr));

    fix_path_separators(&path_cstr);

#ifdef _WIN32
    if ( ! SetCurrentDirectory(path_cstr.buffer)) {
        KOS_raise_last_error(ctx, "SetCurrentDirectory", GetLastError());
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

    TRY_ADD_FUNCTION(ctx, module.o, "is_file", is_file,   filename_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "remove",  remove,    filename_arg);
    TRY_ADD_FUNCTION(ctx, module.o, "cwd",     cwd,       KOS_NULL);
    TRY_ADD_FUNCTION(ctx, module.o, "chdir",   kos_chdir,  path_arg);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

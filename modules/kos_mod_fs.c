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
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_system.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
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
 *     is_file(pathname)
 *
 * Determines whether a file exists.
 *
 * Returns `true` if `pathname` exists and is a file, or `false` otherwise.
 */
static KOS_OBJ_ID is_file(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int        error;
    KOS_OBJ_ID ret          = KOS_BADPTR;
    KOS_OBJ_ID filename_obj = KOS_array_read(ctx, args_obj, 0);
    KOS_VECTOR filename_cstr;

    kos_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators(&filename_cstr);

    ret = KOS_BOOL(kos_does_file_exist(filename_cstr.buffer));

cleanup:
    kos_vector_destroy(&filename_cstr);

    return ret;
}

/* @item fs remove()
 *
 *     remove(pathname)
 *
 * Deletes a file `pathname`.
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

    kos_vector_init(&filename_cstr);

    TRY_OBJID(filename_obj);

    TRY(KOS_string_to_cstr_vec(ctx, filename_obj, &filename_cstr));

    fix_path_separators(&filename_cstr);

#ifdef _WIN32
    ret = KOS_BOOL(DeleteFile(filename_cstr.buffer));
#else
    ret = KOS_BOOL(unlink(filename_cstr.buffer) == 0);
#endif

cleanup:
    kos_vector_destroy(&filename_cstr);

    return ret;
}

int kos_module_fs_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int error  = KOS_SUCCESS;
    int pushed = 0;

    TRY(KOS_push_locals(ctx, &pushed, 1, &module));

    TRY_ADD_FUNCTION(ctx, module, "is_file", is_file, 1);
    TRY_ADD_FUNCTION(ctx, module, "remove",  remove,  1);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

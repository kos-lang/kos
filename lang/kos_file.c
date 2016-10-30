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

#include "kos_file.h"
#include "../inc/kos_error.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <errno.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'fopen/getenv': This function may be unsafe */
#else
#   include <sys/types.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif

static int _errno_to_error(void)
{
    int error;

    switch (errno)
    {
        case ENOENT:
            error = KOS_ERROR_NOT_FOUND;
            break;

        case ENOMEM:
            error = KOS_ERROR_OUT_OF_MEMORY;
            break;

        default:
            error = KOS_ERROR_CANNOT_OPEN_FILE;
            break;
    }

    return error;
}

#ifdef _WIN32
static int _is_file(const char *filename)
{
    const DWORD attr = GetFileAttributes(filename);

    return (attr != INVALID_FILE_ATTRIBUTES && 0 == (attr & FILE_ATTRIBUTE_DIRECTORY))
           ? KOS_SUCCESS : KOS_ERROR_NOT_FOUND;
}
#else
static int _is_file(const char *filename)
{
    int         error = KOS_SUCCESS;
    struct stat statbuf;

    if (0 == stat(filename, &statbuf)) {
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR)
            error = KOS_ERROR_NOT_FOUND;
    }
    else
        error = _errno_to_error();

    return error;
}
#endif

int _KOS_load_file(const char         *filename,
                   struct _KOS_VECTOR *buf)
{
    int    error;
    long   lsize;
    size_t size;
    FILE  *file = 0;

    TRY(_is_file(filename));

    file = fopen(filename, "rb");
    if (!file)
        RAISE_ERROR(KOS_ERROR_CANNOT_OPEN_FILE);

    if (0 != fseek(file, 0, SEEK_END))
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

    lsize = ftell(file);
    if (lsize < 0)
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);
    size = (size_t)lsize;
    if (0 != fseek(file, 0, SEEK_SET))
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

    TRY(_KOS_vector_resize(buf, size));

    if (size != fread(buf->buffer, 1, size, file))
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

_error:
    if (file)
        fclose(file);

    return error;
}

int _KOS_does_file_exist(const char *filename)
{
    return _is_file(filename) == KOS_SUCCESS;
}

int _KOS_get_absolute_path(struct _KOS_VECTOR *path)
{
    int   error;
    char *resolved_path;

#ifdef _WIN32
    resolved_path = _fullpath(0, path->buffer, 0);
#   define need_free 1
#else
#   ifdef PATH_MAX
        char   pathbuf[PATH_MAX];
#       define need_free 0
#   elif defined(MAXPATHLEN)
        char   pathbuf[MAXPATHLEN];
#       define need_free 0
#   else
#       define pathbuf   0
#       define need_free 1
#   endif
    resolved_path = realpath(path->buffer, pathbuf);
#endif

    if (resolved_path) {

        const size_t len = strlen(resolved_path) + 1;

        error = _KOS_vector_resize(path, len);

        if (!error)
            memcpy(path->buffer, resolved_path, len);

        if (need_free)
            free(resolved_path);
    }
    else
        error = _errno_to_error();

    return error;
}

int _KOS_get_env(const char         *name,
                 struct _KOS_VECTOR *buf)
{
    int         error;
    const char *value = getenv(name);

    if (value) {

        const size_t len = strlen(value);

        error = _KOS_vector_resize(buf, len+1);

        if (!error)
            memcpy(buf->buffer, value, len+1);
    }
    else
        error = KOS_ERROR_NOT_FOUND;

    return error;
}

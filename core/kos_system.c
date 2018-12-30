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

#include "kos_system.h"
#include "../inc/kos_error.h"
#include "kos_debug.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
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
#   include <sys/time.h>
#   include <sys/types.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <time.h>
#   include <unistd.h>
#endif
#ifdef __APPLE__
#   include <mach-o/dyld.h>
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

int kos_is_stdin_interactive(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdin));
#else
    return isatty(fileno(stdin));
#endif
}

#ifdef _WIN32
static int _is_file(const char *filename)
{
    const DWORD attr = GetFileAttributes(filename);

    return (attr != INVALID_FILE_ATTRIBUTES
                && 0 == (attr & FILE_ATTRIBUTE_DIRECTORY)
                && ! kos_seq_fail())
           ? KOS_SUCCESS : KOS_ERROR_NOT_FOUND;
}
#else
static int _is_file(const char *filename)
{
    int         error = KOS_SUCCESS;
    struct stat statbuf;

    if (0 == stat(filename, &statbuf)) {
        if ((statbuf.st_mode & S_IFMT) == S_IFDIR || kos_seq_fail())
            error = KOS_ERROR_NOT_FOUND;
    }
    else
        error = _errno_to_error();

    return error;
}
#endif

int kos_load_file(const char *filename,
                  KOS_VECTOR *buf)
{
    int    error;
    long   lsize;
    size_t size;
    FILE  *file = 0;

    TRY(_is_file(filename));

    file = fopen(filename, "rb");
    if (!file || kos_seq_fail())
        RAISE_ERROR(KOS_ERROR_CANNOT_OPEN_FILE);

    if (0 != fseek(file, 0, SEEK_END) || kos_seq_fail())
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

    lsize = ftell(file);
    if (lsize < 0)
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);
    size = (size_t)lsize;
    if (0 != fseek(file, 0, SEEK_SET))
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

    TRY(kos_vector_resize(buf, size));

    if (size != fread(buf->buffer, 1, size, file))
        RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);

cleanup:
    if (file)
        fclose(file);

    return error;
}

int kos_does_file_exist(const char *filename)
{
    return _is_file(filename) == KOS_SUCCESS;
}

int kos_get_absolute_path(KOS_VECTOR *path)
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

        error = kos_vector_resize(path, len);

        if (!error)
            memcpy(path->buffer, resolved_path, len);

        if (need_free)
            free(resolved_path);
    }
    else
        error = _errno_to_error();

    return error;
}

int kos_get_env(const char *name,
                KOS_VECTOR *buf)
{
    int         error;
    const char *value = getenv(name);

    if (value) {

        const size_t len = strlen(value);

        error = kos_vector_resize(buf, len+1);

        if (!error)
            memcpy(buf->buffer, value, len+1);
    }
    else
        error = KOS_ERROR_NOT_FOUND;

    return error;
}

#ifdef _WIN32
int kos_executable_path(KOS_VECTOR *buf)
{
    int  error = KOS_ERROR_NOT_FOUND;
    char path_buf[MAX_PATH];

    const DWORD size = GetModuleFileName(NULL, path_buf, sizeof(path_buf));

    if (size < MAX_PATH) {

        const size_t len = size + 1;

        assert(path_buf[size] == 0);

        error = kos_vector_resize(buf, len);

        if ( ! error)
            memcpy(buf->buffer, path_buf, len);
    }

    return error;
}
#elif defined(__APPLE__)
int kos_executable_path(KOS_VECTOR *buf)
{
    int      error = KOS_ERROR_NOT_FOUND;
    uint32_t size  = 0;

    if (_NSGetExecutablePath(0, &size) == -1) {

        error = kos_vector_resize(buf, (size_t)size);

        if ( ! error) {

            if (_NSGetExecutablePath(buf->buffer, &size) == 0) {
                assert(buf->buffer[size - 1] == 0);
            }
            else
                error = KOS_ERROR_NOT_FOUND;
        }
    }

    return error;
}
#elif defined(__linux__) || defined(__FreeBSD__)
int kos_executable_path(KOS_VECTOR *buf)
{
    int error = KOS_ERROR_NOT_FOUND;

    TRY(kos_vector_resize(buf, 256U));

    for (;;) {

#ifdef __linux__
        static const char proc_link[] = "/proc/self/exe";
#endif
#ifdef __FreeBSD__
        static const char proc_link[] = "/proc/curproc/file";
#endif

        const ssize_t num_read = readlink(proc_link, buf->buffer, buf->size - 1);

        if (num_read > 0) {

            TRY(kos_vector_resize(buf, num_read + 1));

            buf->buffer[num_read] = 0;

            break;
        }
        else if (num_read == 0)
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        if (buf->size > 16384U)
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        TRY(kos_vector_resize(buf, buf->size * 2U));
    }

cleanup:
    return error;
}
#else
int kos_executable_path(KOS_VECTOR *buf)
{
    return KOS_ERROR_NOT_FOUND;
}
#endif

#ifdef _WIN32
int kos_mem_protect(void *ptr, unsigned size, enum KOS_PROTECT_E protect)
{
    DWORD old = 0;
    return VirtualProtect(ptr, size, protect == KOS_NO_ACCESS ? PAGE_NOACCESS : PAGE_READWRITE, &old) != 0 ? 0 : 1;
}
#else
int kos_mem_protect(void *ptr, unsigned size, enum KOS_PROTECT_E protect)
{
    return mprotect(ptr, size, protect == KOS_NO_ACCESS ? PROT_NONE : PROT_READ | PROT_WRITE);
}
#endif

#ifdef _WIN32
int64_t kos_get_time_ms(void)
{
    const int64_t epoch = (int64_t)116444736 * (int64_t)100000;
    int64_t       time_ms;
    FILETIME      ft;

    GetSystemTimeAsFileTime(&ft);

    time_ms = (int64_t)ft.dwLowDateTime;

    time_ms += (int64_t)ft.dwHighDateTime << 32;

    /* Convert from 100s of ns to ms */
    time_ms /= 10000;

    /* Convert from Windows time (1601) to Epoch time (1970) */
    time_ms -= epoch;

    return time_ms;
}
#elif defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0) && defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 199309L)
int64_t kos_get_time_ms(void)
{
    int64_t         time_ms = 0;
    struct timespec ts;

    if (!clock_gettime(CLOCK_REALTIME, &ts)) {

        time_ms = (int64_t)ts.tv_sec * 1000;

        time_ms += (int64_t)ts.tv_nsec / 1000000;
    }

    return time_ms;
}
#else
int64_t kos_get_time_ms(void)
{
    int64_t        time_ms = 0;
    struct timeval tv;

    if (!gettimeofday(&tv, 0)) {

        time_ms = (int64_t)tv.tv_sec * 1000;

        time_ms += (int64_t)tv.tv_usec / 1000;
    }

    return time_ms;
}
#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "kos_system_internal.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_object.h"
#include "kos_debug.h"
#include "kos_perf.h"
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
#   pragma warning( disable : 4191 ) /* 'type cast': unsafe conversion from 'FARPROC' to 'LIB_FUNCTION' */
#   pragma warning( disable : 4996 ) /* 'fopen/getenv': This function may be unsafe */
#else
#   include <dlfcn.h>
#   include <fcntl.h>
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <sys/time.h>
#   include <sys/types.h>
#   include <time.h>
#   include <unistd.h>
#endif
#ifdef __APPLE__
#   include <mach-o/dyld.h>
#endif
#ifdef __linux__
#   include <sys/sysmacros.h>
#endif
#if defined(__APPLE__)   || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__)  || \
    defined(__OpenBSD__)
#   include <sys/sysctl.h>
#endif
#ifdef __QNX__
#   include <sys/syspage.h>
#endif

int KOS_is_file_interactive(FILE *file)
{
#ifdef _WIN32
    const int fd = _fileno(file);

    if (_isatty(fd)) {
        const HANDLE handle = (HANDLE)_get_osfhandle(fd);

        if (handle != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;

            return GetConsoleMode(handle, &mode) != 0;
        }
    }

    return 0;
#else
    return isatty(fileno(file));
#endif
}

#ifdef _WIN32
static int is_file(const char *filename)
{
    const DWORD attr = GetFileAttributes(filename);

    return (attr != INVALID_FILE_ATTRIBUTES
                && 0 == (attr & FILE_ATTRIBUTE_DIRECTORY)
                && ! kos_seq_fail())
           ? KOS_SUCCESS : KOS_ERROR_NOT_FOUND;
}
#else
static int is_file(const char *filename)
{
    int         error = KOS_SUCCESS;
    struct stat statbuf;

    errno = 0;

    if (0 == stat(filename, &statbuf)) {
        if (S_ISDIR(statbuf.st_mode) || kos_seq_fail())
            error = KOS_ERROR_NOT_FOUND;
    }
    else
        error = KOS_ERROR_ERRNO;

    return error;
}
#endif

#ifndef _WIN32
#   ifdef CONFIG_NO_O_CLOEXEC
#       define KOS_O_CLOEXEC 0
#   else
#       define KOS_O_CLOEXEC O_CLOEXEC
#   endif

int kos_unix_open(const char *filename, int flags)
{
    const int fd = kos_seq_fail() ? -1 :
                   open(filename, flags | KOS_O_CLOEXEC);

    if (fd != -1)
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);

    return fd;
}
#endif

#if defined(_WIN32) || defined(__HAIKU__)
void KOS_filebuf_init(KOS_FILEBUF *file_buf)
{
    assert(file_buf);
    file_buf->buffer = KOS_NULL;
    file_buf->size   = 0;
}

/* TODO Consider using MapViewOfFile() on Windows */
int KOS_load_file(const char  *filename,
                  KOS_FILEBUF *file_buf)
{
    int    error;
    long   lsize;
    size_t size;
    FILE  *file = KOS_NULL;

    assert(filename);
    assert(file_buf);
    assert( ! file_buf->buffer);
    assert( ! file_buf->size);

    TRY(is_file(filename));

    errno = 0;

    file = kos_seq_fail() ? KOS_NULL :
           fopen(filename, "rb" KOS_FOPEN_CLOEXEC);
    if ( ! file)
        RAISE_ERROR(KOS_ERROR_ERRNO);

#ifndef _WIN32
    (void)fcntl(fileno(file), F_SETFD, FD_CLOEXEC);
#endif

    if (0 != fseek(file, 0, SEEK_END) || kos_seq_fail())
        RAISE_ERROR(KOS_ERROR_ERRNO);

    /* TODO get file size */
    lsize = ftell(file);
    if (lsize < 0)
        RAISE_ERROR(KOS_ERROR_ERRNO);
    size = (size_t)lsize;
    if (0 != fseek(file, 0, SEEK_SET))
        RAISE_ERROR(KOS_ERROR_ERRNO);

    file_buf->buffer = (const char *)malloc(size);
    if ( ! file_buf->buffer)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    PROF_MALLOC((void *)file_buf->buffer, size)

    if (size != fread((char *)file_buf->buffer, 1, size, file)) {
        PROF_FREE((void *)file_buf->buffer)
        free((void *)file_buf->buffer);
        file_buf->buffer = KOS_NULL;
        RAISE_ERROR(KOS_ERROR_ERRNO);
    }

    file_buf->size = size;

cleanup:
    if (file)
        fclose(file);

    return error;
}

void KOS_unload_file(KOS_FILEBUF *file_buf)
{
    assert(file_buf);
    if (file_buf->buffer) {
        PROF_FREE((void *)file_buf->buffer)
        free((void *)file_buf->buffer);
        file_buf->buffer = KOS_NULL;
        file_buf->size   = 0;
    }
}
#else
void KOS_filebuf_init(KOS_FILEBUF *file_buf)
{
    assert(file_buf);
    file_buf->buffer = KOS_NULL;
    file_buf->size   = 0;
}

int KOS_load_file(const char  *filename,
                  KOS_FILEBUF *file_buf)
{
    int         error = KOS_SUCCESS;
    int         fd    = -1;
    struct stat st;
    void       *addr  = KOS_NULL;

    assert(filename);
    assert(file_buf);
    assert( ! file_buf->buffer);
    assert( ! file_buf->size);

    errno = 0;

    fd = kos_unix_open(filename, O_RDONLY);
    if (fd == -1)
        RAISE_ERROR(KOS_ERROR_ERRNO);

    if ((fstat(fd, &st) != 0) || kos_seq_fail())
        RAISE_ERROR(KOS_ERROR_ERRNO);

    if (st.st_size) {
        addr = kos_seq_fail() ? MAP_FAILED :
               mmap(KOS_NULL, st.st_size, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED)
            RAISE_ERROR(KOS_ERROR_ERRNO);
    }

    file_buf->buffer = (const char *)addr;
    file_buf->size   = st.st_size;

cleanup:
    if (fd != -1)
        close(fd);

    return error;
}

void KOS_unload_file(KOS_FILEBUF *file_buf)
{
    assert(file_buf);
    if (file_buf->buffer) {
        munmap((void *)file_buf->buffer, file_buf->size);
        file_buf->buffer = KOS_NULL;
        file_buf->size   = 0;
    }
}
#endif

int KOS_does_file_exist(const char *filename)
{
    return is_file(filename) == KOS_SUCCESS;
}

int KOS_get_absolute_path(KOS_VECTOR *path)
{
    int   error;
    char *resolved_path;

#ifdef _WIN32
    errno = 0;
    resolved_path = _fullpath(KOS_NULL, path->buffer, 0);
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
    errno = 0;
    resolved_path = realpath(path->buffer, pathbuf);
#endif

    if (need_free) {
        PROF_MALLOC((void *)resolved_path, strlen(resolved_path) + 1)
    }

    if (resolved_path) {

        const size_t len = strlen(resolved_path) + 1;

        error = KOS_vector_resize(path, len);

        if (!error)
            memcpy(path->buffer, resolved_path, len);

        if (need_free) {
            PROF_FREE((void *)resolved_path)
            free(resolved_path);
        }
    }
    else
        error = KOS_ERROR_ERRNO;

    return error;
}

int KOS_get_env(const char *name,
                KOS_VECTOR *buf)
{
    int         error;
    const char *value = getenv(name);

    if (value) {

        const size_t len = strlen(value);

        error = KOS_vector_resize(buf, len+1);

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

    const DWORD size = GetModuleFileName(KOS_NULL, path_buf, sizeof(path_buf));

    if (size < MAX_PATH) {

        const size_t len = size + 1;

        assert(path_buf[size] == 0);

        error = KOS_vector_resize(buf, len);

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

    if (_NSGetExecutablePath(KOS_NULL, &size) == -1) {

        error = KOS_vector_resize(buf, (size_t)size);

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

    /* Smaller starting buffer size in debug builds for better test coverage */
#ifdef NDEBUG
#   define INITIAL_EXE_SIZE 256U
#else
#   define INITIAL_EXE_SIZE 16U
#endif

    TRY(KOS_vector_resize(buf, INITIAL_EXE_SIZE));

    for (;;) {

#ifdef __linux__
        static const char proc_link[] = "/proc/self/exe";
#endif
#ifdef __FreeBSD__
        static const char proc_link[] = "/proc/curproc/file";
#endif

        ssize_t num_read = readlink(proc_link, buf->buffer, buf->size - 1);

        if (kos_seq_fail())
            num_read = 0;

        if ((num_read > 0) && (num_read < (ssize_t)buf->size - 1)) {

            TRY(KOS_vector_resize(buf, num_read + 1));

            buf->buffer[num_read] = 0;

            break;
        }

        if ((num_read == 0) || (buf->size > 0x7FFFU))
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        TRY(KOS_vector_resize(buf, buf->size * 2U));
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

#ifdef CONFIG_MAD_GC
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
#endif

#ifdef _WIN32
int64_t KOS_get_time_us(void)
{
    const int64_t epoch = (int64_t)116444736 * (int64_t)100000000;
    int64_t       time_us;
    FILETIME      ft;

    GetSystemTimeAsFileTime(&ft);

    time_us = (int64_t)ft.dwLowDateTime;

    time_us += (int64_t)ft.dwHighDateTime << 32;

    /* Convert from 100s of ns to us */
    time_us /= 10;

    /* Convert from Windows time (1601) to Epoch time (1970) */
    time_us -= epoch;

    return time_us;
}
#elif defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
int64_t KOS_get_time_us(void)
{
    int64_t         time_us = 0;
    struct timespec ts;

    if (!clock_gettime(CLOCK_REALTIME, &ts)) {

        time_us = (int64_t)ts.tv_sec * 1000000;

        time_us += (int64_t)ts.tv_nsec / 1000;
    }

    return time_us;
}
#else
int64_t KOS_get_time_us(void)
{
    int64_t        time_us = 0;
    struct timeval tv;

    if (!gettimeofday(&tv, KOS_NULL)) {

        time_us = (int64_t)tv.tv_sec * 1000000;

        time_us += (int64_t)tv.tv_usec;
    }

    return time_us;
}
#endif

#ifdef _WIN32
static void get_lib_error(KOS_VECTOR *error_cstr)
{
    const DWORD error = GetLastError();
    char       *msg   = KOS_NULL;
    DWORD       msg_size;

    msg_size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                             KOS_NULL,
                             error,
                             LANG_USER_DEFAULT,
                             (LPTSTR)&msg,
                             1024,
                             KOS_NULL);

    while (msg_size && ((uint8_t)msg[msg_size - 1] <= 0x20U))
        --msg_size;

    if (KOS_vector_resize(error_cstr, msg_size + 1) == KOS_SUCCESS) {
        memcpy(error_cstr->buffer, msg, msg_size);
        error_cstr->buffer[msg_size] = 0;
    }
    else {
        error_cstr->size      = 1;
        error_cstr->buffer[0] = 0;
    }

    if (msg)
        LocalFree(msg);
}
#else
static void get_lib_error(KOS_VECTOR *error_cstr)
{
    const char  *error = dlerror();
    const size_t len   = strlen(error);

    if (KOS_vector_resize(error_cstr, len + 1) == KOS_SUCCESS)
        memcpy(error_cstr->buffer, error, len + 1);
    else {
        error_cstr->size      = 1;
        error_cstr->buffer[0] = 0;
    }
}
#endif

#ifdef _WIN32
KOS_SHARED_LIB KOS_load_library(const char *filename, KOS_VECTOR *error_cstr)
{
    const KOS_SHARED_LIB lib = (KOS_SHARED_LIB)LoadLibrary(filename);
    if ( ! lib)
        get_lib_error(error_cstr);
    return lib;
}
#else
KOS_SHARED_LIB KOS_load_library(const char *filename, KOS_VECTOR *error_cstr)
{
    const KOS_SHARED_LIB lib = (KOS_SHARED_LIB)dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
    if ( ! lib)
        get_lib_error(error_cstr);
    return lib;
}
#endif

#ifdef _WIN32
void KOS_unload_library(KOS_SHARED_LIB lib)
{
    assert(lib);
    FreeLibrary((HMODULE)lib);
}
#else
void KOS_unload_library(KOS_SHARED_LIB lib)
{
    assert(lib);
    dlclose(lib);
}
#endif

#ifdef _WIN32
LIB_FUNCTION KOS_get_library_function(KOS_SHARED_LIB lib, const char *func_name, KOS_VECTOR *error_cstr)
{
    LIB_FUNCTION func;

    assert(lib);

    func = (LIB_FUNCTION)GetProcAddress((HMODULE)lib, func_name);
    if ( ! func)
        get_lib_error(error_cstr);
    return func;
}
#else
LIB_FUNCTION KOS_get_library_function(KOS_SHARED_LIB lib, const char *func_name, KOS_VECTOR *error_cstr)
{
    union {
        void        *void_ptr;
        LIB_FUNCTION func;
    } convert;

    assert(lib);
    convert.void_ptr = dlsym(lib, func_name);

    if ( ! convert.func)
        get_lib_error(error_cstr);
    return convert.func;
}
#endif

#ifndef _WIN32
#define SET_INT_PROPERTY(name, value)                                           \
        do {                                                                    \
            KOS_DECLARE_STATIC_CONST_STRING(str_name, name);                    \
                                                                                \
            KOS_OBJ_ID obj_id = KOS_new_int(ctx, (int64_t)(value));             \
            TRY_OBJID(obj_id);                                                  \
                                                                                \
            TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_name), obj_id)); \
        } while (0)

KOS_OBJ_ID kos_stat(KOS_CONTEXT ctx, struct stat *st)
{
    KOS_LOCAL info;
    KOS_LOCAL aux;
    int       error;

    KOS_DECLARE_STATIC_CONST_STRING(str_type,         "type");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_file,    "file");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_dir,     "directory");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_char,    "char");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_device,  "device");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_fifo,    "fifo");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_link,    "symlink");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_socket,  "socket");
    KOS_DECLARE_STATIC_CONST_STRING(str_type_unknown, "unknown");

    KOS_init_locals(ctx, &aux, &info, kos_end_locals);

    info.o = KOS_new_object(ctx);
    TRY_OBJID(info.o);

    SET_INT_PROPERTY("dev",        st->st_dev);
    SET_INT_PROPERTY("flags",      st->st_mode);
    SET_INT_PROPERTY("hard_links", st->st_nlink);
    SET_INT_PROPERTY("inode",      st->st_ino);
    SET_INT_PROPERTY("uid",        st->st_uid);
    SET_INT_PROPERTY("gid",        st->st_gid);
    SET_INT_PROPERTY("size",       st->st_size);
    SET_INT_PROPERTY("blocks",     st->st_blocks);
    SET_INT_PROPERTY("block_size", st->st_blksize);
    SET_INT_PROPERTY("atime",      st->st_atime * (int64_t)1000000);
    SET_INT_PROPERTY("mtime",      st->st_mtime * (int64_t)1000000);
    SET_INT_PROPERTY("ctime",      st->st_ctime * (int64_t)1000000);

#if !defined(__HAIKU__)
    if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) {
        aux.o = KOS_new_array(ctx, 2);
        TRY_OBJID(aux.o);

        TRY(KOS_array_write(ctx, aux.o, 0, TO_SMALL_INT(major(st->st_rdev))));
        TRY(KOS_array_write(ctx, aux.o, 1, TO_SMALL_INT(minor(st->st_rdev))));
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type_device), aux.o));
    }
#endif

    if (S_ISREG(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_file)));
    else if (S_ISDIR(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_dir)));
    else if (S_ISCHR(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_char)));
    else if (S_ISBLK(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_device)));
    else if (S_ISFIFO(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_fifo)));
    else if (S_ISLNK(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_link)));
    else if (S_ISSOCK(st->st_mode))
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_socket)));
    else
        TRY(KOS_set_property(ctx, info.o, KOS_CONST_ID(str_type), KOS_CONST_ID(str_type_unknown)));

cleanup:
    info.o = KOS_destroy_top_locals(ctx, &aux, &info);

    return error ? KOS_BADPTR : info.o;
}
#endif

unsigned KOS_get_num_cpus(void)
{
#ifdef _WIN32

    SYSTEM_INFO sysinfo;

    GetSystemInfo(&sysinfo);

    return (unsigned)sysinfo.dwNumberOfProcessors;

#elif defined(__ANDROID__) || \
      defined(__HAIKU__)   || \
      defined(__linux__)

    const long ncpus = sysconf(_SC_NPROCESSORS_ONLN);

    return (ncpus < 1) ? 1 : (unsigned)ncpus;

#elif defined(__APPLE__)   || \
      defined(__FreeBSD__) || \
      defined(__NetBSD__)  || \
      defined(__OpenBSD__)

    int    mib[2] = { CTL_HW, HW_NCPU };
    int    ncpus  = 0;
    size_t len    = sizeof(ncpus);

    if (sysctl(mib, 2, &ncpus, &len, KOS_NULL, 0) < 0)
        return 1;

    return (ncpus < 1) ? 1 : (unsigned)ncpus;

#elif defined(__QNX__)

    return (unsigned)_syspage_ptr->num_cpu;

#else
    return 1;
#endif
}

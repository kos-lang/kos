/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_FILE_H_INCLUDED
#define KOS_FILE_H_INCLUDED

#include "../inc/kos_instance.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#   define KOS_PATH_SEPARATOR          '\\'
#   define KOS_PATH_SEPARATOR_STR      "\\"
#   define KOS_PATH_LIST_SEPARATOR     ';'
#   define KOS_PATH_LIST_SEPARATOR_STR ";"
#   define KOS_SHARED_LIB_EXT          ".dll"
#else
#   define KOS_PATH_SEPARATOR          '/'
#   define KOS_PATH_SEPARATOR_STR      "/"
#   define KOS_PATH_LIST_SEPARATOR     ':'
#   define KOS_PATH_LIST_SEPARATOR_STR ":"
#   ifdef __APPLE__
#       define KOS_SHARED_LIB_EXT      ".dylib"
#   else
#       define KOS_SHARED_LIB_EXT      ".so"
#   endif
#endif

struct KOS_VECTOR_S;

int kos_is_stdin_interactive(void);

int kos_does_file_exist(const char *filename);

int kos_get_absolute_path(struct KOS_VECTOR_S *path);

int kos_get_env(const char          *name,
                struct KOS_VECTOR_S *buf);

int kos_executable_path(struct KOS_VECTOR_S *buf);

enum KOS_PROTECT_E {
    KOS_NO_ACCESS,
    KOS_READ_WRITE
};

int kos_mem_protect(void *ptr, unsigned size, enum KOS_PROTECT_E protect);

int64_t kos_get_time_us(void);

#if defined(_WIN32) || defined(__HAIKU__)
struct KOS_FILEBUF_S {
    const char *buffer;
    size_t      size;
};
#else
struct KOS_FILEBUF_S {
    const char *buffer;
    size_t      size;
    int         fd;
};
#endif

typedef struct KOS_FILEBUF_S KOS_FILEBUF;

void kos_filebuf_init(KOS_FILEBUF *file_buf);

int kos_load_file(const char  *filename,
                  KOS_FILEBUF *file_buf);

void kos_unload_file(KOS_FILEBUF *file_buf);

int kos_load_library(const char *filename, KOS_SHARED_LIB *lib);

void kos_unload_library(KOS_SHARED_LIB lib);

char *kos_get_library_function(KOS_SHARED_LIB lib, const char *func_name);

#endif

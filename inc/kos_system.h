/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_SYSTEM_H_INCLUDED
#define KOS_SYSTEM_H_INCLUDED

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

#ifdef __cplusplus
extern "C" {
#endif

struct KOS_VECTOR_S;

KOS_API
int KOS_is_stdin_interactive(void);

KOS_API
int KOS_does_file_exist(const char *filename);

KOS_API
int KOS_get_absolute_path(struct KOS_VECTOR_S *path);

KOS_API
int KOS_get_env(const char          *name,
                struct KOS_VECTOR_S *buf);

KOS_API
int64_t KOS_get_time_us(void);

struct KOS_FILEBUF_S {
    const char *buffer;
    size_t      size;
};

typedef struct KOS_FILEBUF_S KOS_FILEBUF;

KOS_API
void KOS_filebuf_init(KOS_FILEBUF *file_buf);

KOS_API
int KOS_load_file(const char  *filename,
                  KOS_FILEBUF *file_buf);

KOS_API
void KOS_unload_file(KOS_FILEBUF *file_buf);

KOS_API
KOS_SHARED_LIB KOS_load_library(const char *filename, struct KOS_VECTOR_S *error_cstr);

KOS_API
void KOS_unload_library(KOS_SHARED_LIB lib);

typedef void (* LIB_FUNCTION)(void);

KOS_API
LIB_FUNCTION KOS_get_library_function(KOS_SHARED_LIB lib, const char *func_name, struct KOS_VECTOR_S *error_cstr);

KOS_API
unsigned KOS_get_num_cpus(void);

#ifdef __cplusplus
}
#endif

#endif

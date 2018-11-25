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

#ifndef KOS_FILE_H_INCLUDED
#define KOS_FILE_H_INCLUDED

#ifdef _WIN32
#   define KOS_PATH_SEPARATOR          '\\'
#   define KOS_PATH_SEPARATOR_STR      "\\"
#   define KOS_PATH_LIST_SEPARATOR     ';'
#   define KOS_PATH_LIST_SEPARATOR_STR ";"
#else
#   define KOS_PATH_SEPARATOR          '/'
#   define KOS_PATH_SEPARATOR_STR      "/"
#   define KOS_PATH_LIST_SEPARATOR     ':'
#   define KOS_PATH_LIST_SEPARATOR_STR ":"
#endif

struct KOS_VECTOR_S;

int kos_is_stdin_interactive(void);

int kos_load_file(const char          *filename,
                  struct KOS_VECTOR_S *buf);

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

#endif

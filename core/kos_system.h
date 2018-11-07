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

#ifndef __KOS_FILE_H
#define __KOS_FILE_H

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

struct _KOS_VECTOR;

int _KOS_is_stdin_interactive(void);

int _KOS_load_file(const char         *filename,
                   struct _KOS_VECTOR *buf);

int _KOS_does_file_exist(const char *filename);

int _KOS_get_absolute_path(struct _KOS_VECTOR *path);

int _KOS_get_env(const char         *name,
                 struct _KOS_VECTOR *buf);

int _KOS_executable_path(struct _KOS_VECTOR *buf);

enum _KOS_PROTECT {
    _KOS_NO_ACCESS,
    _KOS_READ_WRITE
};

int _KOS_mem_protect(void *ptr, unsigned size, enum _KOS_PROTECT protect);

#endif

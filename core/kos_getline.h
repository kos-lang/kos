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

#ifndef __KOS_GETLINE_H
#define __KOS_GETLINE_H

#ifdef CONFIG_READLINE

struct _KOS_GETLINE {
    char dummy;
};

struct _KOS_VECTOR;

int _KOS_getline_init(struct _KOS_GETLINE *state);

#define _KOS_getline_destroy(state) ((void)0)

#elif defined(CONFIG_EDITLINE)

#include <histedit.h>

struct _KOS_GETLINE {
    EditLine *e;
    History  *h;
    HistEvent ev;
};

struct _KOS_VECTOR;

int _KOS_getline_init(struct _KOS_GETLINE *state);

void _KOS_getline_destroy(struct _KOS_GETLINE *state);

#else

struct _KOS_GETLINE {
    int interactive;
};

struct _KOS_VECTOR;

int _KOS_getline_init(struct _KOS_GETLINE *state);

#define _KOS_getline_destroy(state) ((void)0)

#endif

enum _KOS_PROMPT {
    PROMPT_FIRST_LINE,
    PROMPT_SUBSEQUENT_LINE
};

int _KOS_getline(struct _KOS_GETLINE *state,
                 enum _KOS_PROMPT     prompt,
                 struct _KOS_VECTOR  *buf);

#endif

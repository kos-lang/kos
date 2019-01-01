/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#ifndef KOS_GETLINE_H_INCLUDED
#define KOS_GETLINE_H_INCLUDED

#ifdef CONFIG_READLINE

typedef struct KOS_GETLINE_S {
    char dummy;
} KOS_GETLINE;

int kos_getline_init(KOS_GETLINE *state);

#define kos_getline_destroy(state) ((void)0)

#elif defined(CONFIG_EDITLINE)

#include <histedit.h>

typedef struct KOS_GETLINE_S {
    EditLine *e;
    History  *h;
    HistEvent ev;
} KOS_GETLINE;

int kos_getline_init(KOS_GETLINE *state);

void kos_getline_destroy(KOS_GETLINE *state);

#else

typedef struct KOS_GETLINE_S {
    int interactive;
} KOS_GETLINE;

int kos_getline_init(KOS_GETLINE *state);

#define kos_getline_destroy(state) ((void)0)

#endif

enum KOS_PROMPT_E {
    PROMPT_FIRST_LINE,
    PROMPT_SUBSEQUENT_LINE
};

struct KOS_VECTOR_S;

int kos_getline(KOS_GETLINE         *state,
                enum KOS_PROMPT_E    prompt,
                struct KOS_VECTOR_S *buf);

#endif

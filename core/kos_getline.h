/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
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

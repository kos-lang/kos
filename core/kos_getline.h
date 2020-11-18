/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_GETLINE_H_INCLUDED
#define KOS_GETLINE_H_INCLUDED

#ifdef _WIN32

typedef struct KOS_GETLINE_S {
    int interactive;
} KOS_GETLINE;

#define kos_getline_destroy(state) ((void)0)

#else

#include "../inc/kos_memory.h"

typedef struct KOS_GETLINE_S {
    struct KOS_GETLINE_HISTORY_NODE_S *head;
    struct KOS_MEMPOOL_S               allocator;
} KOS_GETLINE;

void kos_getline_destroy(KOS_GETLINE *state);

#endif

enum KOS_PROMPT_E {
    PROMPT_FIRST_LINE,
    PROMPT_SUBSEQUENT_LINE
};

struct KOS_VECTOR_S;

int kos_getline_init(KOS_GETLINE *state);

int kos_getline(KOS_GETLINE         *state,
                enum KOS_PROMPT_E    prompt,
                struct KOS_VECTOR_S *buf);

#endif

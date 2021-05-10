/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_GETLINE_H_INCLUDED
#define KOS_GETLINE_H_INCLUDED

#include "../inc/kos_memory.h"

typedef struct KOS_GETLINE_S {
    struct KOS_GETLINE_HISTORY_NODE_S *head;
    KOS_MEMPOOL                        allocator;
} KOS_GETLINE;

enum KOS_PROMPT_E {
    PROMPT_FIRST_LINE,
    PROMPT_SUBSEQUENT_LINE
};

int kos_getline_init(KOS_GETLINE *state);

void kos_getline_destroy(KOS_GETLINE *state);

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf);

#endif

/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_COMPILER_HASH_H_INCLUDED
#define KOS_COMPILER_HASH_H_INCLUDED

#include "../inc/kos_memory.h"
#include <stdint.h>

struct KOS_VAR_S;
struct KOS_TOKEN_S;

typedef struct KOS_VAR_HASH_TABLE_S {
    KOS_VECTOR buffer;
    uint32_t   size_mask;
} KOS_VAR_HASH_TABLE;

int kos_init_hash_table(KOS_VAR_HASH_TABLE *hash_table);

void kos_destroy_hash_table(KOS_VAR_HASH_TABLE *hash_table);

int kos_add_to_hash_table(KOS_VAR_HASH_TABLE *hash_table, struct KOS_VAR_S *var);

void kos_remove_from_hash_table(KOS_VAR_HASH_TABLE *hash_table, struct KOS_VAR_S *var);

struct KOS_VAR_S *kos_lookup_var(KOS_VAR_HASH_TABLE *hash_table, const struct KOS_TOKEN_S *token);

#endif

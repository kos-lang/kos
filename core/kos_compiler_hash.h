/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_COMPILER_HASH_H_INCLUDED
#define KOS_COMPILER_HASH_H_INCLUDED

#include "../inc/kos_memory.h"
#include <stdint.h>

typedef struct KOS_VAR_S KOS_VAR;
typedef struct KOS_TOKEN_S KOS_TOKEN;
typedef struct KOS_VAR_HASH_ENTRY_S KOS_VAR_HASH_ENTRY;

typedef struct KOS_VAR_HASH_TABLE_S {
    KOS_VECTOR buffer;
    uint32_t   size_mask;
} KOS_VAR_HASH_TABLE;

int kos_init_hash_table(KOS_VAR_HASH_TABLE *hash_table);

void kos_destroy_hash_table(KOS_VAR_HASH_TABLE *hash_table);

int kos_add_to_hash_table(KOS_VAR_HASH_TABLE *hash_table, KOS_VAR *var);

void kos_remove_from_hash_table(KOS_VAR_HASH_TABLE *hash_table, KOS_VAR *var);

KOS_VAR *kos_lookup_var(KOS_VAR_HASH_TABLE *hash_table, const KOS_TOKEN *token);

#endif

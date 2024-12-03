/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "kos_compiler_hash.h"
#include "kos_compiler.h"
#include "../inc/kos_defs.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <string.h>

struct KOS_VAR_HASH_ENTRY_S {
    KOS_VAR *var;
    uint32_t hash;
};

static KOS_VAR deleted;

/* TODO This limit can cause it to fail if there are lots of collisions */
#define MAX_HASH_REPROBES 16

int kos_init_hash_table(KOS_VAR_HASH_TABLE *hash_table)
{
    const uint32_t num_entries = 1024;
    int            error;

    KOS_vector_init(&hash_table->buffer);

    error = KOS_vector_resize(&hash_table->buffer, sizeof(KOS_VAR_HASH_ENTRY) * num_entries);
    if (error)
        return error;

    memset(hash_table->buffer.buffer, 0, sizeof(KOS_VAR_HASH_ENTRY) * num_entries);

    hash_table->size_mask = num_entries - 1;

    return KOS_SUCCESS;
}

void kos_destroy_hash_table(KOS_VAR_HASH_TABLE *hash_table)
{
    KOS_vector_destroy(&hash_table->buffer);

    hash_table->size_mask = 0;
}

static int grow_hash_table(KOS_VAR_HASH_TABLE *hash_table)
{
    KOS_VECTOR          new_buffer;
    KOS_VAR_HASH_ENTRY *entries         = (KOS_VAR_HASH_ENTRY *)hash_table->buffer.buffer;
    KOS_VAR_HASH_ENTRY *end             = entries + (hash_table->size_mask + 1);
    KOS_VAR_HASH_ENTRY *dst_entries;
    const uint32_t      num_entries     = (hash_table->size_mask + 1) * 2;
    const uint32_t      size_mask       = num_entries - 1;
    int                 error;

    /* Limit how much the hahs table can grow */
    if (num_entries >= 0x1000000U)
        return KOS_ERROR_INTERNAL;

    KOS_vector_init(&new_buffer);

    error = KOS_vector_resize(&new_buffer, sizeof(KOS_VAR_HASH_ENTRY) * num_entries);
    if (error)
        return error;

    dst_entries = (KOS_VAR_HASH_ENTRY *)new_buffer.buffer;

    memset(dst_entries, 0, sizeof(KOS_VAR_HASH_ENTRY) * num_entries);

    for ( ; entries < end; entries++) {
        KOS_VAR *const var  = entries->var;
        const uint32_t hash = entries->hash;
        uint32_t       idx;
#ifndef NDEBUG
        uint32_t       reprobes = 0;
#endif

        if ( ! var || var == &deleted)
            continue;

        idx = hash & size_mask;

        while (dst_entries[idx].var) {
            assert(++reprobes <= MAX_HASH_REPROBES);
            idx = (idx + 1) & size_mask;
        }

        dst_entries[idx].var  = var;
        dst_entries[idx].hash = hash;
    }

    KOS_vector_destroy(&hash_table->buffer);

    hash_table->buffer    = new_buffer;
    hash_table->size_mask = size_mask;

    return KOS_SUCCESS;
}

static uint32_t calculate_hash(const KOS_TOKEN *token)
{
    /* djb2a algorithm */
    const char       *str  = token->begin;
    const char *const end  = str + token->length;
    uint32_t          hash = 5381;

    while (str < end)
        hash = (hash * 33U) ^ (uint32_t)*(str++);

    assert(hash);

    return hash;
}

static int compare_var_against_hash(const KOS_VAR_HASH_ENTRY *entry,
                                    const KOS_TOKEN          *token,
                                    uint32_t                  hash)
{
    const KOS_TOKEN *var_token;

    assert(entry->var);

    if (entry->hash != hash)
        return 0;

    var_token = entry->var->token;

    if (var_token->length != token->length)
        return 0;

    return (memcmp(var_token->begin, token->begin, token->length) == 0) ? 1 : 0;
}

int kos_add_to_hash_table(KOS_VAR_HASH_TABLE *hash_table, KOS_VAR *var)
{
    KOS_VAR_HASH_ENTRY    *entries   = (KOS_VAR_HASH_ENTRY *)hash_table->buffer.buffer;
    const KOS_TOKEN *const token     = var->token;
    uint32_t               size_mask = hash_table->size_mask;
    uint32_t               reprobes  = 0;
    uint32_t               hash;
    uint32_t               idx;

    if ( ! var->hash) {
        hash = calculate_hash(token);
        var->hash = hash;
    }
    else {
        hash = var->hash;
        assert(hash == calculate_hash(token));
    }
    idx = hash & size_mask;

    while (entries[idx].var &&
           entries[idx].var != &deleted &&
           ! compare_var_against_hash(&entries[idx], token, hash)) {

        /* Grow the hash table if there were too many collisions */
        if (++reprobes > MAX_HASH_REPROBES) {
            const int error = grow_hash_table(hash_table);
            if (error)
                return error;

            entries   = (KOS_VAR_HASH_ENTRY *)hash_table->buffer.buffer;
            size_mask = hash_table->size_mask;
            idx       = hash & size_mask;
            reprobes  = 0;
        }
        else
            idx = (idx + 1) & size_mask;
    }

    /* New variable */
    if ( ! entries[idx].var || entries[idx].var == &deleted) {
        entries[idx].var  = var;
        entries[idx].hash = hash;
        assert(var->shadowed_var == KOS_NULL);
    }
    /* Shadow an existing variable */
    else {
        /* TODO print message about shadowing */

        assert(entries[idx].var != var);
        assert(entries[idx].var != &deleted);
        var->shadowed_var = entries[idx].var;
        entries[idx].var = var;
        assert(entries[idx].hash == hash);
    }

    return KOS_SUCCESS;
}

void kos_remove_from_hash_table(KOS_VAR_HASH_TABLE *hash_table, KOS_VAR *var)
{
    KOS_VAR_HASH_ENTRY *entries   = (KOS_VAR_HASH_ENTRY *)hash_table->buffer.buffer;
    const uint32_t      size_mask = hash_table->size_mask;
    const uint32_t      hash      = var->hash;
    uint32_t            idx       = hash & size_mask;
#ifndef NDEBUG
    uint32_t            reprobes  = 0;
#endif

    while (entries[idx].var != var) {

        /* Variable must already be in the hash table to be removed */
        assert(++reprobes <= MAX_HASH_REPROBES);
        assert(entries[idx].var);

        idx = (idx + 1) & size_mask;
    }

    assert(entries[idx].hash == hash);

    entries[idx].var  = var->shadowed_var;
    var->shadowed_var = KOS_NULL;

    if ( ! entries[idx].var && entries[(idx + 1) & size_mask].var) {
        entries[idx].var  = &deleted;
        entries[idx].hash = 0;
    }
}

KOS_VAR *kos_lookup_var(KOS_VAR_HASH_TABLE *hash_table, const KOS_TOKEN *token)
{
    KOS_VAR_HASH_ENTRY *entries   = (KOS_VAR_HASH_ENTRY *)hash_table->buffer.buffer;
    const uint32_t      hash      = calculate_hash(token);
    const uint32_t      size_mask = hash_table->size_mask;
    uint32_t            idx       = hash & size_mask;
    uint32_t            reprobes  = 0;

    while (entries[idx].var && ! compare_var_against_hash(&entries[idx], token, hash)) {
        if (++reprobes <= MAX_HASH_REPROBES)
            idx = (idx + 1) & size_mask;
        else
            return KOS_NULL;
    }

    return entries[idx].var;
}

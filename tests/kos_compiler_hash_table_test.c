/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../core/kos_compiler_hash.h"
#include "../core/kos_compiler.h"
#include "../core/kos_misc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_LENGTH 31

typedef struct ENTRY_S {
    KOS_TOKEN token;
    KOS_VAR   var;
    char      str[MAX_LENGTH + 1];
} ENTRY;

static ENTRY    entries[8192];
static uint32_t num_entries;

static int init(KOS_VAR_HASH_TABLE *table)
{
    num_entries = 0;

    return kos_init_hash_table(table);
}

static void destroy(KOS_VAR_HASH_TABLE *table)
{
    kos_destroy_hash_table(table);
    
    memset(entries, 0, sizeof(entries));

    num_entries = 0;
}

static ENTRY *alloc_entry()
{
    ENTRY *entry;

    assert(num_entries < sizeof(entries) / sizeof(entries[0]));

    entry = &entries[num_entries++];

    entry->var.token   = &entry->token;
    entry->token.begin = entry->str;

    return entry;
}

static ENTRY *alloc_random_token(struct KOS_RNG *rng, uint32_t length)
{
    const uint32_t new_idx = num_entries;
    ENTRY *const   entry   = alloc_entry();
    uint32_t       i;

    assert(length < MAX_LENGTH);

    entry->token.length = (uint16_t)length;

    for (i = 0; i < length; i++)
        entry->str[i] = 'a' + (uint8_t)kos_rng_random_range(rng, 26);

    /* Make sure there is no other token like this */
    for (i = 0; i < new_idx; i++) {
        uint32_t ridx;
        
        if (entries[i].token.length != entry->token.length ||
            memcmp(entries[i].str, entry->str, entry->token.length)) {

            continue;
        }

        /* Re-generate one randomly chosen letter */
        ridx = (uint32_t)kos_rng_random_range(rng, entry->token.length - 1);
        entry->str[ridx] = 'a' + (uint8_t)kos_rng_random_range(rng, 26);

        /* Check from the beginning */
        i = 0;
        --i;
    }

    return entry;
}

static ENTRY *alloc_shadow_token(struct KOS_RNG *rng, const KOS_TOKEN *shadowed)
{
    ENTRY *const entry = alloc_entry();

    memcpy(entry->str, shadowed->begin, shadowed->length);

    entry->token.length = shadowed->length;

    return entry;
}

static ENTRY *alloc_specific_token(const char *str)
{
    const uint16_t len = (uint16_t)strlen(str);

    ENTRY *const entry = alloc_entry();

    assert(len <= MAX_LENGTH);

    memcpy(entry->str, str, len + 1);

    entry->token.length = len;

    return entry;
}

static void increment_string(ENTRY *entry)
{
    uint32_t i = entry->token.length;

    while (i > 0) {
        --i;

        if ((uint8_t)(++entry->str[i]) < 0x7FU)
            break;

        entry->str[i] = '0';
    }
}

static uint32_t calculate_hash(const ENTRY *entry)
{
    const char       *str  = entry->str;
    const char *const end  = str + entry->token.length;
    uint32_t          hash = 5381;

    while (str < end)
        hash = (hash * 33U) ^ (uint8_t)*(str++);

    return hash;
}

static ENTRY *generate_collision(ENTRY *init)
{
    ENTRY *const   entry = alloc_entry();
    const uint32_t hash  = calculate_hash(init);

    entry->token.length = init->token.length;
    memcpy(entry->str, init->str, init->token.length);

    do {
        increment_string(entry);
    } while (calculate_hash(entry) != hash);

    return entry;
}

#define CHECK(test)      \
    do {                 \
        if ( ! (test)) { \
            fprintf(stderr, "%s:%d: FAILED test: %s\n", __FILE__, __LINE__, #test); \
            return 1;    \
        }                \
    } while (0)

int main()
{
    KOS_VAR_HASH_TABLE table = { { KOS_NULL, 0, 0, { 0.0, 0.0 } }, 0 };
    struct KOS_RNG     rng;

    kos_rng_init(&rng);

    /********************************************************************/
    /* Basic test with a few entries */
    {
        const uint32_t num_elems = 16;
        uint32_t       i;

        CHECK(init(&table) == 0);

        /* Allocate a bunch of entries and add them to hash table */
        for (i = 0; i < num_elems; i++) {
            ENTRY *const entry = alloc_random_token(&rng, 16);

            CHECK(kos_add_to_hash_table(&table, &entry->var) == 0);
        }

        /* Make sure that all these entries are in the hash table */
        for (i = 0; i < num_elems; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);
            CHECK(var != KOS_NULL);
            CHECK(var == &entries[i].var);
        }

        /* Allocate different entries and make sure they are NOT in the hash table */
        for (i = 0; i < num_elems * 2; i++) {
            const int      len_diff = (i & 1) ? 1 : -1;
            ENTRY *const   entry    = alloc_random_token(&rng, 16U + len_diff);
            KOS_VAR *const var      = kos_lookup_var(&table, &entry->token);
            CHECK(var == KOS_NULL);
        }

        /* Remove some entries from the hash table */
        for (i = 0; i < num_elems / 2; i++)
            kos_remove_from_hash_table(&table, &entries[i].var);

        /* Make sure that the correct entries are in the hash table */
        for (i = 0; i < num_elems; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);

            if (i < num_elems / 2)
                CHECK(var == KOS_NULL);
            else
                CHECK(var == &entries[i].var);
        }

        destroy(&table);
    }

    /********************************************************************/
    /* Allocate lots of entries and test shadowing */
    {
        const uint32_t num_elems = 2048;
        uint32_t       i;

        CHECK(init(&table) == 0);

        /* Allocate a bunch of entries and add them to hash table */
        for (i = 0; i < num_elems; i++) {
            ENTRY *const entry = alloc_random_token(&rng, 16);

            CHECK(kos_add_to_hash_table(&table, &entry->var) == 0);
        }

        /* Make sure that all these entries are in the hash table */
        for (i = 0; i < num_elems; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);
            CHECK(var != KOS_NULL);
            CHECK(var == &entries[i].var);
        }

        /* Shadow half of the entries */
        for (i = 0; i < num_elems / 2; i++) {
            ENTRY *const entry = alloc_shadow_token(&rng, &entries[i].token);

            CHECK(kos_add_to_hash_table(&table, &entry->var) == 0);
        }

        /* Make sure the correct entries are in the hash table */
        for (i = 0; i < num_elems + num_elems / 2; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);
            CHECK(var != KOS_NULL);
            if (i < num_elems / 2)
                /* Shadowed */
                CHECK(var == &entries[i + num_elems].var);
            else
                /* Not shadowed */
                CHECK(var == &entries[i].var);
        }

        /* Allocate different entries and make sure they are NOT in the hash table */
        for (i = 0; i < num_elems / 2; i++) {
            const int      len_diff = (i & 1) ? 1 : -1;
            ENTRY *const   entry    = alloc_random_token(&rng, 16U + len_diff);
            KOS_VAR *const var      = kos_lookup_var(&table, &entry->token);
            CHECK(var == KOS_NULL);
        }

        /* Remove some entries from the hash table */
        for (i = num_elems * 3 / 4; i < num_elems + num_elems / 4; i++)
            kos_remove_from_hash_table(&table, &entries[i].var);

        /* Make sure that the correct entries are in the hash table */
        for (i = 0; i < num_elems; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);

            if (i < num_elems / 4)
                /* Not shadowed */
                CHECK(var == &entries[i].var);
            else if (i < num_elems / 2)
                /* Shadowed */
                CHECK(var == &entries[i + num_elems].var);
            else if (i < num_elems * 3 / 4)
                /* Not shadowed */
                CHECK(var == &entries[i].var);
            else
                /* Removed */
                CHECK(var == KOS_NULL);
        }

        destroy(&table);
    }

    /********************************************************************/
    /* Test collisions */
    {
        const uint32_t num_elems = 16;
        uint32_t       i;

        CHECK(init(&table) == 0);

        {
            ENTRY *prev = alloc_specific_token("00000000");

            CHECK(kos_add_to_hash_table(&table, &prev->var) == 0);

            /* Allocate a bunch of colloding entries and add them to hash table */
            for (i = 1; i < num_elems; i++) {
                ENTRY *const entry = generate_collision(prev);

                CHECK(kos_add_to_hash_table(&table, &entry->var) == 0);

                prev = entry;
            }
        }

        /* Allocate a bunch more entries and add them to hash table */
        for (i = 0; i < num_elems; i++) {
            ENTRY *const entry = alloc_random_token(&rng, 15);

            CHECK(kos_add_to_hash_table(&table, &entry->var) == 0);
        }

        /* Make sure that all these entries are in the hash table */
        for (i = 0; i < num_elems * 2; i++) {
            KOS_VAR *const var = kos_lookup_var(&table, &entries[i].token);
            CHECK(var != KOS_NULL);
            CHECK(var == &entries[i].var);
        }

        destroy(&table);
    }

    return 0;
}

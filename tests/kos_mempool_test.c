/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
 */

#include "../inc/kos_memory.h"
#include "../core/kos_config.h"
#include "kos_test_tools.h"
#include <string.h>

int main(void)
{
    struct KOS_MEMPOOL_S pool;
    void                *obj1;
    void                *obj2;

    KOS_mempool_init(&pool);

    obj1 = KOS_mempool_alloc(&pool, KOS_BUF_ALLOC_SIZE);
    obj2 = KOS_mempool_alloc(&pool, KOS_BUF_ALLOC_SIZE);

    TEST(obj1);
    TEST(obj2);
    TEST(obj1 != obj2);

    memset(obj1, 0x21, KOS_BUF_ALLOC_SIZE);
    memset(obj2, 0x34, KOS_BUF_ALLOC_SIZE);

    KOS_mempool_destroy(&pool);

    return 0;
}

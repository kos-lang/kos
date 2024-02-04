/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_TEST_TOOLS_H_INCLUDED
#define KOS_TEST_TOOLS_H_INCLUDED

#include "../inc/kos_entity.h"
#include "../inc/kos_threads.h"
#include <stdio.h>
#include <stdlib.h>

int create_thread(KOS_CONTEXT          ctx,
                  KOS_FUNCTION_HANDLER proc,
                  void                *cookie,
                  KOS_THREAD         **thread);

int join_thread(KOS_CONTEXT ctx,
                KOS_THREAD *thread);

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

int get_num_cpus(void);

#endif

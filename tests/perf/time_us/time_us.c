/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../../../core/kos_system.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>

int main()
{
    printf("%" PRId64 "\n", kos_get_time_us());
    return 0;
}

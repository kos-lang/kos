/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../../../inc/kos_system.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>

int main()
{
    printf("%" PRId64 "\n", KOS_get_time_us());
    return 0;
}

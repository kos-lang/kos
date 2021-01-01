/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_UTF8_INTERNAL_H_INCLUDED
#define KOS_UTF8_INTERNAL_H_INCLUDED

#include <stdint.h>

enum KOS_ESCAPE_TYPE_E {
    KOS_ET_INVALID     = 0,
    KOS_ET_INTERPOLATE = 40,
    KOS_ET_HEX         = 120
};

extern const uint8_t kos_utf8_len[32];
extern const char    kos_escape_sequence_map[256];

#endif

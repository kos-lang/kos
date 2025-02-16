/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_UTF8_H_INCLUDED
#define KOS_UTF8_H_INCLUDED

#include "kos_api.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KOS_UTF8_ESCAPE_E {
    KOS_UTF8_NO_ESCAPE,
    KOS_UTF8_WITH_ESCAPE
} KOS_UTF8_ESCAPE;

KOS_API
unsigned KOS_utf8_get_len(const char     *str,
                          unsigned        length,
                          KOS_UTF8_ESCAPE escape,
                          uint32_t       *max_code);

KOS_API
int KOS_utf8_decode_8(const char     *str,
                      unsigned        length,
                      KOS_UTF8_ESCAPE escape,
                      uint8_t        *out);

KOS_API
int KOS_utf8_decode_16(const char     *str,
                       unsigned        length,
                       KOS_UTF8_ESCAPE escape,
                       uint16_t       *out);

KOS_API
int KOS_utf8_decode_32(const char     *str,
                       unsigned        length,
                       KOS_UTF8_ESCAPE escape,
                       uint32_t       *out);

KOS_API
unsigned KOS_utf8_calc_buf_size_8(const uint8_t *buf, unsigned length);

KOS_API
unsigned KOS_utf8_calc_buf_size_16(const uint16_t *buf, unsigned length);

KOS_API
unsigned KOS_utf8_calc_buf_size_32(const uint32_t *buf, unsigned length);

KOS_API
void KOS_utf8_encode_8(const uint8_t *str,
                       unsigned       length,
                       uint8_t        *out);

KOS_API
void KOS_utf8_encode_16(const uint16_t *str,
                        unsigned        length,
                        uint8_t         *out);

KOS_API
void KOS_utf8_encode_32(const uint32_t *str,
                        unsigned        length,
                        uint8_t         *out);

#ifdef __cplusplus
}
#endif

#endif

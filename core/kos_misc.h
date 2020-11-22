/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#ifndef KOS_MISC_H_INCLUDED
#define KOS_MISC_H_INCLUDED

#include "../inc/kos_utils.h"

#define MAX_INT64 ( (int64_t)(((uint64_t)((int64_t)-1))>>1) )

int kos_is_integer(const char *begin,
                   const char *end);

int kos_parse_int(const char *begin,
                  const char *end,
                  int64_t    *value);

int kos_parse_double(const char *begin,
                     const char *end,
                     double     *value);

int kos_parse_numeric(const char  *begin,
                      const char  *end,
                      KOS_NUMERIC *value);

uint64_t kos_double_to_uint64_t(double value);

uint32_t kos_float_to_uint32_t(float value);

unsigned kos_print_float(char *buf, unsigned size, double value);

struct KOS_RNG_PCG32 {
    uint64_t state;
    uint64_t stream;
};

struct KOS_RNG {
    struct KOS_RNG_PCG32 pcg[2];
};

void     kos_rng_init(struct KOS_RNG *rng);
void     kos_rng_init_seed(struct KOS_RNG *rng, uint64_t seed);
uint64_t kos_rng_random(struct KOS_RNG *rng);
uint64_t kos_rng_random_range(struct KOS_RNG *rng, uint64_t max_value);
void     kos_get_entropy_fallback(uint8_t *bytes, unsigned size);

#endif

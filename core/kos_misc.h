/*
 * Copyright (c) 2014-2018 Chris Dragan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __KOS_MISC_H
#define __KOS_MISC_H

#include "../inc/kos_utils.h"

#define MAX_INT64 ( (int64_t)(((uint64_t)((int64_t)-1))>>1) )

int _KOS_is_integer(const char *begin,
                    const char *end);

int _KOS_parse_int(const char *begin,
                   const char *end,
                   int64_t    *value);

int _KOS_parse_double(const char *begin,
                      const char *end,
                      double     *value);

int _KOS_parse_numeric(const char          *begin,
                       const char          *end,
                       struct _KOS_NUMERIC *value);

uint64_t _KOS_double_to_uint64_t(double value);

uint32_t _KOS_float_to_uint32_t(float value);

unsigned _KOS_print_float(char *buf, unsigned size, double value);

struct KOS_RNG_PCG32 {
    uint64_t state;
    uint64_t stream;
};

struct KOS_RNG {
    struct KOS_RNG_PCG32 pcg[2];
};

void     _KOS_rng_init(struct KOS_RNG *rng);
void     _KOS_rng_init_seed(struct KOS_RNG *rng, uint64_t seed);
uint64_t _KOS_rng_random(struct KOS_RNG *rng);
uint64_t _KOS_rng_random_range(struct KOS_RNG *rng, uint64_t max_value);
void     _KOS_get_entropy_fallback(uint8_t *bytes);

int64_t _KOS_fix_index(int64_t idx, unsigned length);

#endif

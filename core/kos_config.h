/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#ifndef KOS_CONFIG_H_INCLUDED
#define KOS_CONFIG_H_INCLUDED

#ifdef CONFIG_FUZZ
#   define KOS_POOL_BITS        16
#else
#   define KOS_POOL_BITS        19
#endif
#if defined(__powerpc64__) || defined(CONFIG_MAD_GC)
#   define KOS_PAGE_BITS        16
#else
#   define KOS_PAGE_BITS        12
#endif
#define KOS_POOL_SIZE           (1U << KOS_POOL_BITS)
#define KOS_PAGE_SIZE           (1U << KOS_PAGE_BITS)
#define KOS_OBJ_ALIGN_BITS      5
#define KOS_MAX_PAGE_SEEK       8   /* Max number of non-full pages to check for free space */
#define KOS_MIGRATION_THRESH    90U /* Percentage of page utilization after GC */
#ifdef CONFIG_FUZZ
#   define KOS_MAX_HEAP_SIZE    (2U * KOS_POOL_SIZE)
#elif defined(CONFIG_MAD_GC)
#   define KOS_MAX_HEAP_SIZE    (16U * 1024U * 1024U)
#else
#   define KOS_MAX_HEAP_SIZE    (64U * 1024U * 1024U)
#endif
#define KOS_GC_STEP             (2U * 1024U * 1024U)
#define KOS_MAX_HEAP_OBJ_SIZE   512U
#define KOS_STACK_OBJ_SIZE      4096U

#define KOS_MAX_AST_DEPTH       100
#define KOS_BUF_ALLOC_SIZE      4096U
#define KOS_VEC_MAX_INC_SIZE    262144U
#ifdef CONFIG_FUZZ
#   define KOS_MAX_CODE_SIZE    0x10000U
#   define KOS_MAX_STACK_DEPTH  64U
#   define KOS_MAX_THREADS      2U
#   define KOS_MAX_ARGS_IN_REGS 4U
#else
#   define KOS_MAX_CODE_SIZE    0x200000U
#   define KOS_MAX_STACK_DEPTH  2048U
#   define KOS_MAX_THREADS      32U
#   define KOS_MAX_ARGS_IN_REGS 32U
#endif

#if defined(__GNUC__) && defined(CONFIG_FAST_DISPATCH)
#   define KOS_DISPATCH_TABLE   1
#else
#   define KOS_DISPATCH_TABLE   0
#endif

#endif

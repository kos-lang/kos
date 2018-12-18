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

#ifndef KOS_CONFIG_H_INCLUDED
#define KOS_CONFIG_H_INCLUDED

#define KOS_MAX_AST_DEPTH      100
#define KOS_BUF_ALLOC_SIZE     4096U
#define KOS_VEC_MAX_INC_SIZE   262144U
#define KOS_POOL_BITS          19
#define KOS_PAGE_BITS          12
#define KOS_POOL_SIZE          (1U << KOS_POOL_BITS)
#define KOS_PAGE_SIZE          (1U << KOS_PAGE_BITS)
#define KOS_OBJ_ALIGN_BITS     4
#define KOS_MAX_PAGE_SEEK      8   /* Max number of non-full pages to check for free space */
#define KOS_MIGRATION_THRESH   90U /* Percentage of page utilization after GC */
#define KOS_GC_STEP            (KOS_PAGE_SIZE * 16U)
#define KOS_MIN_REG_CAPACITY   32U
#define KOS_MAX_ARGS_IN_REGS   32U
#define KOS_MAX_STACK_DEPTH    4096U
#ifdef CONFIG_FUZZ
#   define KOS_MAX_HEAP_SIZE   (32U * 1024U * 1024U)
#elif defined(CONFIG_MAD_GC)
#   define KOS_MAX_HEAP_SIZE   (16U * 1024U * 1024U)
#else
#   define KOS_MAX_HEAP_SIZE   (1024U * 1024U * 1024U)
#endif

#endif

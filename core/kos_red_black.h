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

#ifndef KOS_RED_BLACK_H_INCLUDED
#define KOS_RED_BLACK_H_INCLUDED

typedef struct KOS_RED_BLACK_NODE_S {
    struct KOS_RED_BLACK_NODE_S *parent;
    struct KOS_RED_BLACK_NODE_S *left;
    struct KOS_RED_BLACK_NODE_S *right;

    int red;
} KOS_RED_BLACK_NODE;

typedef int (*KOS_RED_BLACK_COMPARE_ITEM)(void               *what,
                                          KOS_RED_BLACK_NODE *node);

KOS_RED_BLACK_NODE *kos_red_black_find(KOS_RED_BLACK_NODE        *root,
                                       void                      *what,
                                       KOS_RED_BLACK_COMPARE_ITEM compare);

typedef int (*KOS_RED_BLACK_WALK)(KOS_RED_BLACK_NODE *node,
                                  void               *cookie);

int kos_red_black_walk(KOS_RED_BLACK_NODE *node,
                       KOS_RED_BLACK_WALK  walk,
                       void               *cookie);

typedef int (*KOS_RED_BLACK_COMPARE_NODE)(KOS_RED_BLACK_NODE *a,
                                          KOS_RED_BLACK_NODE *b);

void kos_red_black_insert(KOS_RED_BLACK_NODE       **out_root,
                          KOS_RED_BLACK_NODE        *new_node,
                          KOS_RED_BLACK_COMPARE_NODE compare);

void kos_red_black_delete(KOS_RED_BLACK_NODE **out_root,
                          KOS_RED_BLACK_NODE  *node);

#endif

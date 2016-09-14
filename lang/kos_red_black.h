/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#ifndef __KOS_RED_BLACK_H
#define __KOS_RED_BLACK_H

struct _KOS_RED_BLACK_NODE {
    struct _KOS_RED_BLACK_NODE *parent;
    struct _KOS_RED_BLACK_NODE *left;
    struct _KOS_RED_BLACK_NODE *right;

    int red;
};

typedef int (*_KOS_RED_BLACK_COMPARE_ITEM)(void                       *what,
                                           struct _KOS_RED_BLACK_NODE *node);

struct _KOS_RED_BLACK_NODE *_KOS_red_black_find(struct _KOS_RED_BLACK_NODE *root,
                                                void                       *what,
                                                _KOS_RED_BLACK_COMPARE_ITEM compare);

typedef int (*_KOS_RED_BLACK_WALK)(struct _KOS_RED_BLACK_NODE *node,
                                   void                       *cookie);

int _KOS_red_black_walk(struct _KOS_RED_BLACK_NODE *node,
                        _KOS_RED_BLACK_WALK         walk,
                        void                       *cookie);

typedef int (*_KOS_RED_BLACK_COMPARE_NODE)(struct _KOS_RED_BLACK_NODE *a,
                                           struct _KOS_RED_BLACK_NODE *b);

void _KOS_red_black_insert(struct _KOS_RED_BLACK_NODE **root,
                           struct _KOS_RED_BLACK_NODE  *new_node,
                           _KOS_RED_BLACK_COMPARE_NODE  compare);

void _KOS_red_black_delete(struct _KOS_RED_BLACK_NODE **root,
                           struct _KOS_RED_BLACK_NODE  *node);

#endif

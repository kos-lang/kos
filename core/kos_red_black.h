/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
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

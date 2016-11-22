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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../inc/kos_error.h"
#include "../lang/kos_malloc.h"
#include "../lang/kos_misc.h"
#include "../lang/kos_red_black.h"

struct MYNODE {
    struct _KOS_RED_BLACK_NODE node;
    intptr_t                   value;
};

enum RB_ERROR {
    ERROR_WRONG_NODE_ORDER  = 10,
    ERROR_TREE_NOT_BALANCED = 11,
    ERROR_WRONG_WALK_ORDER  = 12
};

static int cmp_node(struct _KOS_RED_BLACK_NODE *a,
                    struct _KOS_RED_BLACK_NODE *b)
{
    struct MYNODE *aa = (struct MYNODE *)a;
    struct MYNODE *bb = (struct MYNODE *)b;
    if (aa->value == bb->value)
        return 0;
    else
        return aa->value < bb->value ? -1 : 1;
}

static int cmp_value(void                       *value,
                     struct _KOS_RED_BLACK_NODE *node)
{
    const intptr_t node_value = ((struct MYNODE *)node)->value;

    if ((intptr_t)value == node_value)
        return 0;
    else
        return ((intptr_t)value < node_value) ? -1 : 1;
}

static int check_tree_order(struct MYNODE *n)
{
    int e = 0;
    if (n) {
        if (n->node.left) {
            if (cmp_value((void *)n->value, n->node.left) != 1)
                e = ERROR_WRONG_NODE_ORDER;
        }
        if (!e && n->node.right) {
            if (cmp_value((void *)n->value, n->node.right) != -1)
                e = ERROR_WRONG_NODE_ORDER;
        }
        if (!e)
            e = check_tree_order((struct MYNODE *)n->node.left);
        if (!e)
            e = check_tree_order((struct MYNODE *)n->node.right);
    }
    return e;
}

static int check_node_order(struct _KOS_RED_BLACK_NODE *node, void *cookie)
{
    struct MYNODE      *n     = (struct MYNODE *)node;
    intptr_t           *prev  = (intptr_t *)cookie;

    return (n->value <= *prev) ? ERROR_WRONG_WALK_ORDER : 0;
}

static int check_walk_order(struct MYNODE *n)
{
    uintptr_t prev = (uintptr_t)1U << (sizeof(intptr_t)*8-1);

    return _KOS_red_black_walk((struct _KOS_RED_BLACK_NODE *)n, check_node_order, &prev);
}

static int count_black_nodes(struct MYNODE *n)
{
    int num = 1;

    if (n->node.red)
        return -1;

    while (n) {
        if (!n->node.red)
            ++num;
        n = (struct MYNODE *)n->node.left;
    }

    return num;
}

static int check_black_nodes(struct MYNODE *n, int num_black)
{
    int e = 0;
    if (n) {
        if (!n->node.red)
            --num_black;
        e = check_black_nodes((struct MYNODE *)n->node.left, num_black);
        if (!e)
            e = check_black_nodes((struct MYNODE *)n->node.right, num_black);
    }
    else
        if (num_black != 1)
            e = ERROR_TREE_NOT_BALANCED;
    return e;
}

static int check_tree(struct MYNODE *n)
{
    int e = check_tree_order(n);
    if (!e)
        e = check_walk_order(n);
    if (!e) {
        const int num_black = count_black_nodes(n);
        e = check_black_nodes(n, num_black);
    }
    return e;
}

static void free_tree(struct MYNODE *n)
{
    if (n) {
        free_tree((struct MYNODE *)n->node.left);
        free_tree((struct MYNODE *)n->node.right);
        _KOS_free(n);
    }
}

static int print_tree_node(struct _KOS_RED_BLACK_NODE *node, void *cookie)
{
    struct MYNODE *n     = (struct MYNODE *)node;
    const intptr_t value = n->value;
    printf(" %08" PRIx64, (int64_t)value);
    return KOS_SUCCESS;
}

static void print_tree(struct MYNODE *root)
{
    printf("tree:");
    _KOS_red_black_walk((struct _KOS_RED_BLACK_NODE *)root, print_tree_node, 0);
    printf("\n");
}

int main(int argc, char *argv[])
{
    int            i;
    int            size;
    intptr_t      *values;
    struct MYNODE *root = 0;
    struct KOS_RNG rng;
    int            total = 0;
    int            error = 0;

    if (argc < 1 || argc > 2)
        return 1;

    size = argc == 2 ? atoi(argv[1]) : 100000;

    values = (intptr_t *)_KOS_malloc(size * sizeof(intptr_t));

    _KOS_rng_init(&rng);

    for (i = 0; i < size; i++) {
        const uintptr_t v = (uintptr_t)_KOS_rng_random(&rng);
        values[i]         = v == ((uintptr_t)1U << (sizeof(intptr_t)*8-1)) ? 0 : (intptr_t)v;
    }

    for (i = 0; i < size*2; i++) {
        struct MYNODE *node;

        if (_KOS_red_black_find((struct _KOS_RED_BLACK_NODE *)root,
                                (void *)values[i % size],
                                cmp_value))
            continue;

        node = (struct MYNODE *)_KOS_malloc(sizeof(struct MYNODE));

        node->value = values[i % size];

        _KOS_red_black_insert((struct _KOS_RED_BLACK_NODE **)&root,
                              (struct _KOS_RED_BLACK_NODE *)node,
                              cmp_node);

        ++total;
    }

    if (total > size) {
        printf("Error: Inserted too many nodes\n");
        return 1;
    }

    error = check_tree(root);

    for (i = 0 ; i < total*4; i++) {
        const uint64_t idx  = _KOS_rng_random(&rng);
        struct MYNODE *node = (struct MYNODE *)_KOS_red_black_find(
                (struct _KOS_RED_BLACK_NODE *)root,
                (void *)values[(unsigned)idx % size],
                cmp_value);

        if (node) {
            _KOS_red_black_delete((struct _KOS_RED_BLACK_NODE **)&root,
                                  (struct _KOS_RED_BLACK_NODE *)node);
            _KOS_free(node);
            --total;
        }
    }

    i = check_tree(root);
    if (i && !error)
        error = i;

    if (total < 20)
        print_tree(root);

    free_tree(root);
    _KOS_free(values);

    switch (error) {
        case ERROR_WRONG_NODE_ORDER:
            printf("Error: Nodes are in incorrect order\n");
            break;

        case ERROR_TREE_NOT_BALANCED:
            printf("Error: Tree is not balanced\n");
            break;

        case ERROR_WRONG_WALK_ORDER:
            printf("Error: Nodes walked in wrong order\n");
            break;

        default:
            break;
    }

    return error;
}

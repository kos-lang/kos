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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../inc/kos_error.h"
#include "../core/kos_malloc.h"
#include "../core/kos_misc.h"
#include "../core/kos_red_black.h"

struct MYNODE {
    KOS_RED_BLACK_NODE node;
    intptr_t           value;
};

enum RB_ERROR {
    ERROR_WRONG_NODE_ORDER  = 10,
    ERROR_TREE_NOT_BALANCED = 11,
    ERROR_WRONG_WALK_ORDER  = 12
};

static int cmp_node(KOS_RED_BLACK_NODE *a,
                    KOS_RED_BLACK_NODE *b)
{
    struct MYNODE *aa = (struct MYNODE *)a;
    struct MYNODE *bb = (struct MYNODE *)b;
    if (aa->value == bb->value)
        return 0;
    else
        return aa->value < bb->value ? -1 : 1;
}

static int cmp_value(void               *value,
                     KOS_RED_BLACK_NODE *node)
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

static int check_node_order(KOS_RED_BLACK_NODE *node, void *cookie)
{
    struct MYNODE      *n     = (struct MYNODE *)node;
    intptr_t           *prev  = (intptr_t *)cookie;

    return (n->value <= *prev) ? ERROR_WRONG_WALK_ORDER : 0;
}

static int check_walk_order(struct MYNODE *n)
{
    uintptr_t prev = (uintptr_t)1U << (sizeof(intptr_t)*8-1);

    return kos_red_black_walk((KOS_RED_BLACK_NODE *)n, check_node_order, &prev);
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

static void print_error(int error)
{
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
}

static int check_tree(struct MYNODE *n)
{
    int error = check_tree_order(n);
    if (!error)
        error = check_walk_order(n);
    if (!error) {
        const int num_black = count_black_nodes(n);
        if (n && (n->node.left || n->node.right || n->node.red))
            error = check_black_nodes(n, num_black);
    }
    print_error(error);
    return error;
}

static void free_tree(struct MYNODE *n)
{
    if (n) {
        free_tree((struct MYNODE *)n->node.left);
        free_tree((struct MYNODE *)n->node.right);
        kos_free(n);
    }
}

static int print_tree_node(KOS_RED_BLACK_NODE *node, void *cookie)
{
    struct MYNODE *n     = (struct MYNODE *)node;
    const intptr_t value = n->value;
    printf(" %08" PRIx64, (int64_t)value);
    return KOS_SUCCESS;
}

static void print_tree(struct MYNODE *root)
{
    printf("tree:");
    kos_red_black_walk((KOS_RED_BLACK_NODE *)root, print_tree_node, 0);
    printf("\n");
}

static int test_sequence(const int *insert_seq,
                         const int *delete_seq,
                         int        count)
{
    int            i;
    struct MYNODE *root = 0;
    struct MYNODE  nodes[31];

    assert(count <= (int)(sizeof(nodes) / sizeof(nodes[0])));

    memset(nodes, 0, sizeof(nodes));

    for (i = 0; i < count; i++)
        nodes[i].value = i;

    for (i = 0; i < count; i++) {
        int       error;
        const int idx = insert_seq[i];

        kos_red_black_insert((KOS_RED_BLACK_NODE **)&root,
                             (KOS_RED_BLACK_NODE *)&nodes[idx],
                             cmp_node);

        error = check_tree(root);
        print_error(error);
        if (error)
            return error;
    }

    i = 0;
    for (;;) {
        int       error;
        const int idx = delete_seq[i];

        kos_red_black_delete((KOS_RED_BLACK_NODE **)&root,
                             (KOS_RED_BLACK_NODE *)&nodes[idx]);

        if (++i == count)
            break;

        error = check_tree(root);
        print_error(error);
        if (error)
            return error;
    }

    if (root) {
        printf("Unexpected root\n");
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int error;
    int size;

    if (argc < 1 || argc > 2)
        return 1;

    size = argc == 2 ? atoi(argv[1]) : 10000;

    /* Ascending */
    {
        const int insert_seq[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        const int delete_seq[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

        error = test_sequence(insert_seq, delete_seq, 16);
        if (error)
            return error;
    }

    /* Descending */
    {
        const int insert_seq[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
        const int delete_seq[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };

        error = test_sequence(insert_seq, delete_seq, 16);
        if (error)
            return error;
    }

    /* Ascending/descending */
    {
        const int insert_seq[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        const int delete_seq[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };

        error = test_sequence(insert_seq, delete_seq, 16);
        if (error)
            return error;
    }

    /* Root-to-leaves */
    {
        const int insert_seq[15] = { 7, 3, 11, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14 };
        const int delete_seq[15] = { 7, 3, 11, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14 };

        error = test_sequence(insert_seq, delete_seq, 15);
        if (error)
            return error;
    }

    /* Leaves-to-root */
    {
        const int insert_seq[15] = { 0, 2, 4, 6, 8, 10, 12, 14, 1, 5, 9, 13, 3, 11, 7 };
        const int delete_seq[15] = { 0, 2, 4, 6, 8, 10, 12, 14, 1, 5, 9, 13, 3, 11, 7 };

        error = test_sequence(insert_seq, delete_seq, 15);
        if (error)
            return error;
    }

    /* Left side left-to right, then right side right-to-left */
    {
        const int insert_seq[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8 };
        const int delete_seq[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8 };

        error = test_sequence(insert_seq, delete_seq, 15);
        if (error)
            return error;
    }

    /* Right side right-to-left, then Left side left-to right */
    {
        const int insert_seq[15] = { 14, 13, 12, 11, 10, 9, 8, 7, 0, 1, 2, 3, 4, 5, 6 };
        const int delete_seq[15] = { 14, 13, 12, 11, 10, 9, 8, 7, 0, 1, 2, 3, 4, 5, 6 };

        error = test_sequence(insert_seq, delete_seq, 15);
        if (error)
            return error;
    }

    /* Levels in order: 0, 3, 1, 2 */
    {
        const int insert_seq[15] = { 7, 0, 2, 4, 6, 8, 10, 12, 14, 3, 11, 1, 5, 9, 13 };
        const int delete_seq[15] = { 7, 0, 2, 4, 6, 8, 10, 12, 14, 3, 11, 1, 5, 9, 13 };

        error = test_sequence(insert_seq, delete_seq, 15);
        if (error)
            return error;
    }

    /* Random test */
    {
        int            i;
        int            total = 0;
        struct KOS_RNG rng;
        struct MYNODE *root   = 0;
        intptr_t      *values = (intptr_t *)kos_malloc(size * sizeof(intptr_t));

        kos_rng_init(&rng);

        for (i = 0; i < size; i++) {
            const uintptr_t v = (uintptr_t)kos_rng_random(&rng);
            values[i]         = v == ((uintptr_t)1U << (sizeof(intptr_t)*8-1)) ? 0 : (intptr_t)v;
        }

        for (i = 0; i < size*2; i++) {
            struct MYNODE *node;

            if (kos_red_black_find((KOS_RED_BLACK_NODE *)root,
                                   (void *)values[i % size],
                                   cmp_value))
                continue;

            node = (struct MYNODE *)kos_malloc(sizeof(struct MYNODE));

            node->value = values[i % size];

            kos_red_black_insert((KOS_RED_BLACK_NODE **)&root,
                                 (KOS_RED_BLACK_NODE *)node,
                                 cmp_node);

            ++total;
        }

        if (total > size) {
            printf("Error: Inserted too many nodes\n");
            return 1;
        }

        error = check_tree(root);

        for (i = 0 ; i < total*4; i++) {
            const uint64_t idx  = kos_rng_random(&rng);
            struct MYNODE *node = (struct MYNODE *)kos_red_black_find(
                    (KOS_RED_BLACK_NODE *)root,
                    (void *)values[(unsigned)idx % size],
                    cmp_value);

            if (node) {
                kos_red_black_delete((KOS_RED_BLACK_NODE **)&root,
                                     (KOS_RED_BLACK_NODE *)node);
                kos_free(node);
                --total;
            }
        }

        i = check_tree(root);
        if (i && !error)
            error = i;

        if (total < 20)
            print_tree(root);

        free_tree(root);
        kos_free(values);

        print_error(error);
    }

    return error;
}

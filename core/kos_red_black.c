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

#include "kos_red_black.h"
#include "../inc/kos_error.h"
#include <stdio.h>
#include <assert.h>

KOS_RED_BLACK_NODE *kos_red_black_find(KOS_RED_BLACK_NODE        *root,
                                       void                      *what,
                                       KOS_RED_BLACK_COMPARE_ITEM compare)
{
    while (root) {
        const int result = compare(what, root);
        if (!result)
            break;
        if (result < 0)
            root = root->left;
        else
            root = root->right;
    }

    return root;
}

int kos_red_black_walk(KOS_RED_BLACK_NODE *node,
                       KOS_RED_BLACK_WALK  walk,
                       void               *cookie)

{
    int error = KOS_SUCCESS;

    if (node) {

        while (node->left)
            node = node->left;

        while (node) {

            error = walk(node, cookie);

            if (error)
                break;

            if (node->right) {
                node = node->right;

                while (node->left)
                    node = node->left;
            }
            else {

                KOS_RED_BLACK_NODE *prev;

                do {
                    prev = node;
                    node = node->parent;
                }
                while (node && node->right == prev);
            }
        }
    }

    return error;
}

static void _insert_binary(KOS_RED_BLACK_NODE       **node,
                           KOS_RED_BLACK_NODE        *new_node,
                           KOS_RED_BLACK_COMPARE_NODE compare)
{
    KOS_RED_BLACK_NODE *parent = 0;

    while (*node) {
        parent = *node;
        if (compare(new_node, parent) < 0)
            node = &parent->left;
        else
            node = &parent->right;
    }

    *node            = new_node;
    new_node->parent = parent;
}

static void _left_rotate(KOS_RED_BLACK_NODE **root,
                         KOS_RED_BLACK_NODE  *node)
{
    KOS_RED_BLACK_NODE *other = node->right;

    node->right = other->left;

    if (other->left)
        other->left->parent = node;

    other->parent = node->parent;

    if (node->parent) {
        if (node == node->parent->left)
            node->parent->left = other;
        else
            node->parent->right = other;
    }
    else
        *root = other;

    other->left = node;

    node->parent = other;
}

static void _right_rotate(KOS_RED_BLACK_NODE **root,
                          KOS_RED_BLACK_NODE  *node)
{
    KOS_RED_BLACK_NODE *other = node->left;

    node->left = other->right;

    if (other->right)
        other->right->parent = node;

    other->parent = node->parent;

    if (node->parent) {
        if (node == node->parent->right)
            node->parent->right = other;
        else
            node->parent->left = other;
    }
    else
        *root = other;

    other->right = node;

    node->parent = other;
}

void kos_red_black_insert(KOS_RED_BLACK_NODE       **out_root,
                          KOS_RED_BLACK_NODE        *new_node,
                          KOS_RED_BLACK_COMPARE_NODE compare)
{
    new_node->red   = 1;
    new_node->left  = 0;
    new_node->right = 0;

    _insert_binary(out_root, new_node, compare);

    /* Restore red-black property */
    while (new_node != *out_root && new_node->parent->red) {
        KOS_RED_BLACK_NODE *pp_left  = new_node->parent->parent->left;
        KOS_RED_BLACK_NODE *pp_right = new_node->parent->parent->right;

        if (new_node->parent == pp_left) {
            if (pp_right && pp_right->red) {
                pp_left->red  = 0;
                pp_right->red = 0;

                new_node = new_node->parent->parent;
                new_node->red = 1;
            }
            else {
                if (new_node == new_node->parent->right) {
                    new_node = new_node->parent;
                    _left_rotate(out_root, new_node);
                }
                new_node->parent->red = 0;
                new_node->parent->parent->red = 1;
                _right_rotate(out_root, new_node->parent->parent);
            }
        }
        else {
            if (pp_left && pp_left->red) {
                pp_left->red  = 0;
                pp_right->red = 0;

                new_node = new_node->parent->parent;
                new_node->red = 1;
            }
            else {
                if (new_node == new_node->parent->left) {
                    new_node = new_node->parent;
                    _right_rotate(out_root, new_node);
                }
                new_node->parent->red = 0;
                new_node->parent->parent->red = 1;
                _left_rotate(out_root, new_node->parent->parent);
            }
        }
    }

    if (new_node == *out_root)
        new_node->red = 0;
}

void kos_red_black_delete(KOS_RED_BLACK_NODE **out_root,
                          KOS_RED_BLACK_NODE  *node)
{
    KOS_RED_BLACK_NODE *succ;
    KOS_RED_BLACK_NODE *parent;
    KOS_RED_BLACK_NODE  leaf = { 0, 0, 0, /*red=*/ 3 };
    KOS_RED_BLACK_NODE *root = *out_root;

    /* If deleted node has two children, swap it with the successor and delete the successor */
    if (node->left && node->right) {
        KOS_RED_BLACK_NODE *a, *b;
        int color;

        succ = node->right;
        while (succ->left)
            succ = succ->left;

        a           = node->left;
        succ->left  = a;
        node->left  = 0;
        a->parent   = succ;

        a           = node->right;
        b           = succ->right;
        node->right = b;
        if (b)
            b->parent   = node;
        if (a == succ)
            succ->right = node;
        else {
            succ->right = a;
            a->parent   = succ;
        }

        a            = node->parent;
        b            = succ->parent;
        succ->parent = a;
        node->parent = b == node ? succ : b;

        if (a) {
            if (a->left == node)
                a->left  = succ;
            else
                a->right = succ;
        }
        else
            root = succ;

        if (b != node) {
            assert(b->left == succ);
            b->left = node;
        }

        color     = node->red;
        node->red = succ->red;
        succ->red = color;
    }

    if (node->left)
        succ = node->left;
    else
        succ = node->right;

    /* Use dummy leaf node if the deleted node has no children */
    if ( ! succ) {
        succ = &leaf;
        leaf.left  = 0;
        leaf.right = 0;
        leaf.red   = 0;
    }

    parent = node->parent;
    if (parent) {
        if (parent->left == node)
            parent->left = succ;
        else
            parent->right = succ;
    }
    else
        root = succ;

    succ->parent = parent;

    if (parent) {
        if (node->red) {
            if (succ == &leaf)
                leaf.red = 0; /* ignore if deleted node was red or if it was root */
        }
        else if (succ->red == 1)
            succ->red = 0; /* turn red into black */
        else
            succ->red = 2; /* special: double black, trigger rebalancing */
    }

    node = succ;

    /* Restore red-black property */
    while (node->red == 2) {

        KOS_RED_BLACK_NODE *sibling;
        int                 sibling_left;

        /* If root, just make it black */
        parent = node->parent;
        if ( ! parent) {
            node->red = 0;
            break;
        }

        sibling_left = (parent->left == node) ? 0 : 1;
        sibling      = sibling_left ? parent->left : parent->right;

        assert(sibling);

        if (sibling->red) {
            assert( ! parent->red);
            if (sibling_left)
                _right_rotate(&root, parent);
            else
                _left_rotate(&root, parent);
            parent->red  = 1;
            sibling->red = 0;
        }
        else {
            int left_red  = sibling->left  && sibling->left->red  ? 1 : 0;
            int right_red = sibling->right && sibling->right->red ? 1 : 0;

            if (sibling_left && left_red) {
                _right_rotate(&root, parent);
                sibling->red       = parent->red;
                parent->red        = 0;
                sibling->left->red = 0;
                node->red          = 0;
                node               = parent;
            }
            else if (!sibling_left && right_red) {
                _left_rotate(&root, parent);
                sibling->red        = parent->red;
                parent->red         = 0;
                sibling->right->red = 0;
                node->red           = 0;
                node                = parent;
            }
            else if (left_red) {
                sibling->left->red = 0;
                sibling->red       = 1;
                _right_rotate(&root, sibling);
            }
            else if (right_red) {
                sibling->right->red = 0;
                sibling->red        = 1;
                _left_rotate(&root, sibling);
            }
            else {
                parent->red  = parent->red ? 0 : 2;
                node->red    = 0;
                sibling->red = 1;
                node         = parent;
            }
        }
    }

    /* Remove dummy leaf from the tree */
    if (leaf.red != 3) {
        assert(leaf.red == 0);
        if (leaf.parent) {
            parent = leaf.parent;
            if (parent->left == &leaf)
                parent->left  = 0;
            else
                parent->right = 0;
        }
        else
            root = 0;
    }

    if (root)
        root->red = 0;

    *out_root = root;
}

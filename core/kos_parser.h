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

#ifndef KOS_PARSER_H_INCLUDED
#define KOS_PARSER_H_INCLUDED

#include "kos_lexer.h"

struct KOS_AST_NODE_S;

struct KOS_MEMPOOL_S;

typedef struct KOS_PARSER_STATE_S {
    struct KOS_AST_NODE_S *last_fallthrough;
    int                    unary_depth; /* For detecting ambiguous syntax */
    char                   allow_continue;
    char                   allow_break;
    char                   allow_fallthrough;
    char                   in_constructor;
    char                   in_derived_class;
    char                   in_class_member;
} KOS_PARSER_STATE;

typedef struct KOS_PARSER_S {
    struct KOS_MEMPOOL_S *ast_buf;
    const char           *error_str;
    KOS_LEXER             lexer;
    KOS_TOKEN             token;
    KOS_PARSER_STATE      state;
    char                  unget;
    char                  had_eol;
    int                   ast_depth;   /* For limiting statement/expression depth */
} KOS_PARSER;

void kos_parser_init(KOS_PARSER           *parser,
                     struct KOS_MEMPOOL_S *mempool,
                     unsigned              file_id,
                     const char           *begin,
                     const char           *end);

int  kos_parser_parse(KOS_PARSER             *parser,
                      struct KOS_AST_NODE_S **ret);

void kos_parser_destroy(KOS_PARSER *parser);

#endif

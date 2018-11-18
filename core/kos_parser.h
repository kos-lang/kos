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

#ifndef KOS_PARSER_H_INCLUDED
#define KOS_PARSER_H_INCLUDED

#include "kos_lexer.h"

struct KOS_AST_NODE_S;

struct KOS_MEMPOOL_S;

struct _KOS_PARSER {
    struct KOS_MEMPOOL_S  *ast_buf;
    const char            *error_str;
    KOS_LEXER              lexer;
    KOS_TOKEN              token;
    int                    unget;
    int                    had_eol;
    int                    allow_continue;
    int                    allow_break;
    int                    allow_fallthrough;
    struct KOS_AST_NODE_S *last_fallthrough;
    int                    in_constructor;
    int                    ast_depth;   /* For limiting statement/expression depth */
    int                    unary_depth; /* For detecting ambiguous syntax */
};

void kos_parser_init(struct _KOS_PARSER   *parser,
                     struct KOS_MEMPOOL_S *mempool,
                     unsigned              file_id,
                     const char           *begin,
                     const char           *end);

int  kos_parser_parse(struct _KOS_PARSER     *parser,
                      struct KOS_AST_NODE_S **ret);

void kos_parser_destroy(struct _KOS_PARSER *parser);

#endif

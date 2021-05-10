/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#ifndef KOS_PARSER_H_INCLUDED
#define KOS_PARSER_H_INCLUDED

#include "kos_lexer.h"

struct KOS_AST_NODE_S;

typedef struct KOS_MEMPOOL_S KOS_MEMPOOL;

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
    KOS_MEMPOOL     *ast_buf;
    const char      *error_str;
    KOS_LEXER        lexer;
    KOS_TOKEN        token;
    KOS_PARSER_STATE state;
    char             unget;
    char             had_eol;
    int              ast_depth;   /* For limiting statement/expression depth */
} KOS_PARSER;

void kos_parser_init(KOS_PARSER  *parser,
                     KOS_MEMPOOL *mempool,
                     uint16_t     file_id,
                     const char  *begin,
                     const char  *end);

int  kos_parser_parse(KOS_PARSER             *parser,
                      struct KOS_AST_NODE_S **ret);

int  kos_parser_import_base(KOS_PARSER            *parser,
                            struct KOS_AST_NODE_S *ast);

void kos_parser_destroy(KOS_PARSER *parser);

#endif

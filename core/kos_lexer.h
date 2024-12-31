/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_LEXER_H_INCLUDED
#define KOS_LEXER_H_INCLUDED

#include <stdint.h>

typedef struct KOS_FILE_POS_S {
    uint32_t line;
    uint32_t column;
    uint16_t file_id;
} KOS_FILE_POS;

typedef void (* KOS_REPORT_ERROR)(void *cookie, uint16_t file_id, uint32_t line, uint32_t column, uint32_t length, const char *error_str);

typedef struct KOS_LEXER_S {
    const char      *buf;
    const char      *buf_end;
    const char      *prefetch_begin;
    const char      *prefetch_end;
    KOS_REPORT_ERROR report_error;
    void            *report_cookie;
    KOS_FILE_POS     pos;
    KOS_FILE_POS     old_pos;
} KOS_LEXER;

typedef enum KOS_TOKEN_TYPE_E {
    TT_INVALID,
    TT_WHITESPACE,
    TT_EOL,
    TT_COMMENT,
    TT_EOF,
    TT_IDENTIFIER,
    TT_KEYWORD,
    TT_NUMERIC,
    TT_STRING,
    TT_STRING_OPEN,
    TT_OPERATOR,
    TT_SEPARATOR,
    TT_NUMERIC_BINARY /* used during optimization, not emitted by lexer */
} KOS_TOKEN_TYPE;

typedef enum KOS_KEYWORD_TYPE_E {
    KW_NONE,
    KW_UNDERSCORE,
    KW_LINE,
    KW_ASSERT,
    KW_ASYNC,
    KW_BREAK,
    KW_CASE,
    KW_CATCH,
    KW_CLASS,
    KW_CONST,
    KW_CONSTRUCTOR,
    KW_CONTINUE,
    KW_DEFAULT,
    KW_DEFER,
    KW_DELETE,
    KW_DO,
    KW_ELSE,
    KW_EXTENDS,
    KW_FALLTHROUGH,
    KW_FALSE,
    KW_FOR,
    KW_FUN,
    KW_GET,
    KW_IF,
    KW_IMPORT,
    KW_IN,
    KW_INSTANCEOF,
    KW_LOOP,
    KW_MATCH,
    KW_PROPERTYOF,
    KW_PUBLIC,
    KW_REPEAT,
    KW_RETURN,
    KW_SET,
    KW_STATIC,
    KW_SUPER,
    KW_SWITCH,
    KW_THIS,
    KW_THROW,
    KW_TRUE,
    KW_TRY,
    KW_TYPEOF,
    KW_VAR,
    KW_VOID,
    KW_WHILE,
    KW_WITH,
    KW_YIELD
} KOS_KEYWORD_TYPE;

typedef enum KOS_OPERATOR_TYPE_E {
    OT_NONE,

    OT_MASK           = 0xF8, /* 1111 1000 */
    OT_ARITHMETIC     = 0x80, /* 1000 0000 */
    OT_UNARY          = 0x40, /* 0100 0000 */
    OT_MULTIPLICATIVE = 0x88, /* 1000 1000 */
    OT_BITWISE        = 0x20, /* 0010 0000 */
    OT_COMPARISON     = 0x28, /* 0010 1000 */
    OT_ASSIGNMENT     = 0x10, /* 0001 0000 */

    OT_ADD            = 0xC0, /* 1100 0000 */
    OT_SUB            = 0xC1, /* 1100 0001 */
    OT_MUL            = 0x89, /* 1000 1000 */
    OT_DIV            = 0x88, /* 1000 1001 */
    OT_MOD            = 0x8A, /* 1000 1010 */

    OT_CONCAT         = 0x32, /* 0011 0010 */

    OT_NOT            = 0x40, /* 0100 0100 */
    OT_LOGNOT         = 0x41, /* 0100 0101 */

    OT_AND            = 0x20, /* 0010 0000 */
    OT_OR             = 0x21, /* 0010 0001 */
    OT_XOR            = 0x22, /* 0010 0010 */

    OT_SHL            = 0x01, /* 0000 0001 */
    OT_SHR            = 0x02, /* 0000 0010 */
    OT_SHRU           = 0x03, /* 0000 0011 */
    OT_LOGAND         = 0x04, /* 0000 0100 */
    OT_LOGOR          = 0x05, /* 0000 0101 */
    OT_LOGTRI         = 0x06, /* 0000 0110 */
    OT_DOT            = 0x07, /* 0000 0111 */
    OT_MORE           = 0x08, /* 0000 1000 */
    OT_ARROW          = 0x09, /* 0000 1001 */
    OT_LAMBDA         = 0x0A, /* 0000 1010 */

    OT_EQ             = 0x28, /* 0010 1000 */
    OT_NE             = 0x29, /* 0010 1001 */
    OT_GE             = 0x2A, /* 0010 1010 */
    OT_GT             = 0x2B, /* 0010 1011 */
    OT_LE             = 0x2C, /* 0010 1100 */
    OT_LT             = 0x2D, /* 0010 1101 */

    OT_SET            = 0x10, /* 0001 0000 */
    OT_SETADD         = 0x11, /* 0001 0001 */
    OT_SETSUB         = 0x12, /* 0001 0010 */
    OT_SETMUL         = 0x13, /* 0001 0011 */
    OT_SETDIV         = 0x14, /* 0001 0100 */
    OT_SETMOD         = 0x15, /* 0001 0101 */
    OT_SETAND         = 0x16, /* 0001 0110 */
    OT_SETOR          = 0x17, /* 0001 0111 */
    OT_SETXOR         = 0x18, /* 0001 1000 */
    OT_SETSHL         = 0x19, /* 0001 1001 */
    OT_SETSHR         = 0x1A, /* 0001 1010 */
    OT_SETSHRU        = 0x1B, /* 0001 1011 */
    OT_SETCONCAT      = 0x1C  /* 0001 1100 */
} KOS_OPERATOR_TYPE;

typedef enum KOS_SEPARATOR_TYPE_E {
    ST_NONE,
    ST_PAREN_OPEN,
    ST_PAREN_CLOSE,
    ST_COMMA,
    ST_COLON,
    ST_SEMICOLON,
    ST_SQUARE_OPEN,
    ST_SQUARE_CLOSE,
    ST_CURLY_OPEN,
    ST_CURLY_CLOSE
} KOS_SEPARATOR_TYPE;

typedef struct KOS_TOKEN_S {
    const char *begin;
    uint32_t    line;
    uint32_t    column;
    uint16_t    file_id;
    uint16_t    length;
    unsigned    type    : 8;
    unsigned    keyword : 8;
    unsigned    op      : 8;
    unsigned    sep     : 8;
} KOS_TOKEN;

typedef enum KOS_NEXT_TOKEN_MODE_E {
    NT_ANY,             /* Next token can be of any type */
    NT_CONTINUE_STRING  /* Next token continues a string */
} KOS_NEXT_TOKEN_MODE;

void kos_lexer_init(KOS_LEXER  *lexer,
                    uint16_t    file_id,
                    const char *begin,
                    const char *end);

void kos_lexer_update(KOS_LEXER  *lexer,
                      const char *begin,
                      const char *end);

int  kos_lexer_next_token(KOS_LEXER          *lexer,
                          KOS_NEXT_TOKEN_MODE mode,
                          KOS_TOKEN          *token);

void kos_lexer_unget_token(KOS_LEXER       *lexer,
                           const KOS_TOKEN *token);

KOS_FILE_POS get_token_pos(const KOS_TOKEN *token);

#endif

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

#ifndef __KOS_LEXER_H
#define __KOS_LEXER_H

struct _KOS_FILE_POS {
    unsigned file_id;
    unsigned line;
    unsigned column;
};

struct _KOS_LEXER {
    const char          *buf;
    const char          *buf_end;
    const char          *prefetch_begin;
    const char          *prefetch_end;
    const char          *error_str;
    struct _KOS_FILE_POS pos;
    struct _KOS_FILE_POS old_pos;
};

enum _KOS_TOKEN_TYPE {
    TT_WHITESPACE,
    TT_EOL,
    TT_COMMENT,
    TT_EOF,
    TT_IDENTIFIER,
    TT_KEYWORD,
    TT_NUMERIC,
    TT_STRING,
    TT_STRING_OPEN_SQ,
    TT_STRING_OPEN_DQ,
    TT_OPERATOR,
    TT_SEPARATOR,
    TT_NUMERIC_BINARY /* used during optimization, not emitted by lexer */
};

enum _KOS_KEYWORD_TYPE {
    KW_NONE,
    KW_LINE,
    KW_ASSERT,
    KW_BREAK,
    KW_CATCH,
    KW_CONST,
    KW_CONTINUE,
    KW_DEFER,
    KW_DELETE,
    KW_DO,
    KW_ELSE,
    KW_FALLTHROUGH,
    KW_FALSE,
    KW_FINALLY,
    KW_FOR,
    KW_FUN,
    KW_GET,
    KW_IF,
    KW_IMPORT,
    KW_IN,
    KW_INSTANCEOF,
    KW_NEW,
    KW_PRIVATE,
    KW_PROTOTYPE,
    KW_PUBLIC,
    KW_RETURN,
    KW_SET,
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
    KW_YIELD,
    KW_LAMBDA
};

enum _KOS_OPERATOR_TYPE {
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

    OT_NOT            = 0x40, /* 0100 0100 */
    OT_LOGNOT         = 0x41, /* 0100 0101 */

    OT_AND            = 0x20, /* 0010 0000 */
    OT_OR             = 0x21, /* 0010 0001 */
    OT_XOR            = 0x22, /* 0010 0010 */

    OT_SHL            = 0x01, /* 0000 0001 */
    OT_SHR            = 0x02, /* 0000 0010 */
    OT_SSR            = 0x03, /* 0000 0011 */
    OT_LOGAND         = 0x04, /* 0000 0100 */
    OT_LOGOR          = 0x05, /* 0000 0101 */
    OT_LOGTRI         = 0x06, /* 0000 0110 */
    OT_DOT            = 0x07, /* 0000 0111 */
    OT_MORE           = 0x08, /* 0000 1000 */
    OT_ARROW          = 0x09, /* 0000 1001 */

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
    OT_SETSSR         = 0x1B  /* 0001 1011 */ /* TODO rename SSR to SHRU everywhere */
};

enum _KOS_SEPARATOR_TYPE {
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
};

struct _KOS_TOKEN {
    const char              *begin;
    unsigned                 length;
    struct _KOS_FILE_POS     pos;
    enum _KOS_TOKEN_TYPE     type;
    enum _KOS_KEYWORD_TYPE   keyword;
    enum _KOS_OPERATOR_TYPE  op;
    enum _KOS_SEPARATOR_TYPE sep;
};

enum _KOS_NEXT_TOKEN_MODE {
    NT_ANY,             /* Next token can be of any type */
    NT_SINGLE_Q_STRING, /* Next token continues a single-quoted string */
    NT_DOUBLE_Q_STRING  /* Next token continues a double-quoted string */
};

void _KOS_lexer_init(struct _KOS_LEXER *lexer,
                     unsigned           file_id,
                     const char        *begin,
                     const char        *end);

int  _KOS_lexer_next_token(struct _KOS_LEXER        *lexer,
                           enum _KOS_NEXT_TOKEN_MODE mode,
                           struct _KOS_TOKEN        *token);

void _KOS_lexer_unget_token(struct _KOS_LEXER       *lexer,
                            const struct _KOS_TOKEN *token);

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "kos_lexer.h"
#include "../inc/kos_defs.h"
#include "../inc/kos_error.h"
#include "kos_debug.h"
#include "kos_misc.h"
#include "kos_utf8_internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char str_err_bin[]                 = "unexpected character, binary digit expected";
static const char str_err_char[]                = "unexpected character";
static const char str_err_cont[]                = "unexpected character, ')' expected as string continuation";
static const char str_err_eof_bin[]             = "unexpected end of file, binary digit expected";
static const char str_err_eof_cont[]            = "unexpected end of file, string continuation expected";
static const char str_err_eof_esc[]             = "unexpected end of file, unfinished escape sequence";
static const char str_err_eof_hex[]             = "unexpected end of file, hexadecimal digit expected";
static const char str_err_eof_str[]             = "unexpected end of file, unfinished string literal, expected '\"'";
static const char str_err_eol_str[]             = "unexpected end of line, unfinished string literal, expected '\"'";
static const char str_err_hex[]                 = "hexadecimal digit expected";
static const char str_err_invalid_char[]        = "invalid character";
static const char str_err_invalid_dec[]         = "invalid decimal literal";
static const char str_err_invalid_utf8[]        = "invalid UTF-8 character sequence";
static const char str_err_no_hex_digits[]       = "invalid escape sequence, no hex digits specified";
static const char str_err_tab[]                 = "unexpected tab character, tabs are not allowed";
static const char str_err_too_many_hex_digits[] = "invalid escape sequence, more than 6 hex digits specified";
static const char str_err_token_too_long[]      = "token length exceeds 65535 bytes";

enum KOS_LEXEM_TYPE_E {
    LT_INVALID,
    LT_WHITESPACE,
    LT_COMMENT,
    LT_BACKSLASH,
    LT_SEPARATOR,
    LT_OPERATOR,
    LT_SLASH,
    LT_STRING,
    LT_TAB,

    LT_ALPHANUMERIC = 0x10,
    LT_DIGIT        = 0x10,
    LT_LETTER       = 0x11,
    LT_UNDERSCORE   = 0x12,

    LT_EOL          = 0x20,
    LT_EOF          = 0x21,

    LT_UTF8_MULTI   = 0x40,
    LT_UTF8_MASK    = 0x0F,
    LT_UTF8_TAIL    = 0x40,
    LT_UTF8_2       = 0x42,
    LT_UTF8_3       = 0x43,
    LT_UTF8_4       = 0x44,
    LT_INVALID_UTF8 = 0x4F
};

static const unsigned char lexem_types[] = {
    /* 0..7 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID,
    /* 8 */
    LT_INVALID,
    /* 9(TAB) */
    LT_TAB,
    /* 10(LF) */
    LT_EOL,
    /* 11(VTAB), 12(FF) */
    LT_WHITESPACE, LT_WHITESPACE,
    /* 13(CR) */
    LT_EOL,
    /* 14..15 */
    LT_INVALID, LT_INVALID,
    /* 16..23 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID,
    /* 24..31 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID,
    /* 32(SPACE) */
    LT_WHITESPACE,
    /* 33(!) */
    LT_OPERATOR,
    /* 34(") */
    LT_STRING,
    /* 35(#) */
    LT_COMMENT,
    /* 36($) */
    LT_INVALID,
    /* 37(%) 38(&) */
    LT_OPERATOR, LT_OPERATOR,
    /* 39(') */
    LT_INVALID,
    /* 40"(" */
    LT_SEPARATOR,
    /* 41")" */
    LT_SEPARATOR,
    /* 42(*) 43(+) */
    LT_OPERATOR, LT_OPERATOR,
    /* 44(,) */
    LT_SEPARATOR,
    /* 45(-) */
    LT_OPERATOR,
    /* .(46) */
    LT_OPERATOR,
    /* 47(/) */
    LT_SLASH,
    /* 48(0)..57(9) */
    LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT, LT_DIGIT,
    /* 58(:) */
    LT_SEPARATOR,
    /* 59(;) */
    LT_SEPARATOR,
    /* 60(<) 61(=) 62(>) 63(?) */
    LT_OPERATOR, LT_OPERATOR, LT_OPERATOR, LT_OPERATOR,
    /* 64(@) */
    LT_INVALID,
    /* 65(A)..71(G) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 72(H)..79(O) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 80(P)..87(W) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 88(X)..90(Z) */
    LT_LETTER, LT_LETTER, LT_LETTER,
    /* 91([) */
    LT_SEPARATOR,
    /* 92(\) */
    LT_BACKSLASH,
    /* 93(]) */
    LT_SEPARATOR,
    /* 94(^) */
    LT_OPERATOR,
    /* 95(_) */
    LT_UNDERSCORE,
    /* 96(`) */
    LT_INVALID,
    /* 97(a)..106(g) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 104(h)..111(o) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 112(p)..119(w) */
    LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER, LT_LETTER,
    /* 120(x)..122(z) */
    LT_LETTER, LT_LETTER, LT_LETTER,
    /* 123({) */
    LT_SEPARATOR,
    /* 124(|) */
    LT_OPERATOR,
    /* 125(}) */
    LT_SEPARATOR,
    /* 126(~) */
    LT_OPERATOR,
    /* 127 */
    LT_INVALID,
    /* 128..191 */
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL, LT_UTF8_TAIL,
    /* 192..223 */
    LT_INVALID, LT_INVALID, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2,  LT_UTF8_2,  LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2,  LT_UTF8_2,  LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2,  LT_UTF8_2,  LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    /* 224..239 */
    LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3,
    LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3,
    /* 240..247 */
    LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4,
    /* 248..255 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID
};

enum OPERATOR_MAP_INDEX_E {
    OMI_NONE,
    OMI_BANG,
    OMI_PERCENT,
    OMI_AND,
    OMI_STAR,
    OMI_PLUS,
    OMI_MINUS,
    OMI_DOT,
    OMI_SLASH,
    OMI_LESS,
    OMI_EQUAL,
    OMI_GREATER,
    OMI_QUESTION,
    OMI_XOR,
    OMI_OR,
    OMI_TILDE,
    OMI_HEX
};

static const unsigned char hex_and_operator_map[] = {
    /* 0..15 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 16..31 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 32..36 */
    0, OMI_BANG, 0, 0, 0,
    /* 37..39 */
    OMI_PERCENT, OMI_AND, 0,
    /* 40(() 41()) */
    ST_PAREN_OPEN, ST_PAREN_CLOSE,
    /* 42(*) 43(+) */
    OMI_STAR, OMI_PLUS,
    /* 44(,) */
    ST_COMMA,
    /* 45(-) 46(.) 47(/) */
    OMI_MINUS, OMI_DOT, OMI_SLASH,
    /* 48(0)..57(9) */
    OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX,
    /* 58(:) 59(;) */
    ST_COLON, ST_SEMICOLON,
    /* 60(<) 61(=) 62(>) 63(?) */
    OMI_LESS, OMI_EQUAL, OMI_GREATER, OMI_QUESTION,
    /* 64 */
    0,
    /* 65(A)..71(G) */
    OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, 0,
    /* 72(H)..87(W) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 88(X)..90(Z) */
    0, 0, 0,
    /* 91([) 92 93(]) 94(^) 95 96 */
    ST_SQUARE_OPEN, 0, ST_SQUARE_CLOSE, OMI_XOR, 0, 0,
    /* 97(a)..106(g) */
    OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, OMI_HEX, 0,
    /* 104(h)..119(w) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 120(x)..122(z) */
    0, 0, 0,
    /* 123({) 124(|) 125(}) 126(~) 127 */
    ST_CURLY_OPEN, OMI_OR, ST_CURLY_CLOSE, OMI_TILDE, 0,
    /* 128..255 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

struct KOS_OP_SPECIFIER_S {
    const char       *str;
    KOS_OPERATOR_TYPE type;
};

static const struct KOS_OP_SPECIFIER_S operator_map[][7] = {
    { { KOS_NULL, OT_NONE   } },
    { { "!",      OT_LOGNOT }, { "!=",     OT_NE     }, { KOS_NULL, OT_NONE   } },
    { { "%",      OT_MOD    }, { "%=",     OT_SETMOD }, { KOS_NULL, OT_NONE   } },
    { { "&",      OT_AND    }, { "&&",     OT_LOGAND }, { "&=",     OT_SETAND }, { KOS_NULL, OT_NONE   } },
    { { "*",      OT_MUL    }, { "*=",     OT_SETMUL }, { KOS_NULL, OT_NONE } },
    { { "+",      OT_ADD    }, { "+=",     OT_SETADD }, { KOS_NULL, OT_NONE   } },
    { { "-",      OT_SUB    }, { "-=",     OT_SETSUB }, { "->",     OT_ARROW  }, { KOS_NULL, OT_NONE   } },
    { { ".",      OT_DOT    }, { "...",    OT_MORE   }, { KOS_NULL, OT_NONE   } },
    { { "/",      OT_DIV    }, { "/=",     OT_SETDIV }, { KOS_NULL, OT_NONE   } },
    { { "<",      OT_LT     }, { "<<",     OT_SHL    }, { "<<=",    OT_SETSHL }, { "<=\0",   OT_LE     }, { KOS_NULL, OT_NONE } },
    { { "=",      OT_SET    }, { "==",     OT_EQ     }, { "=>",     OT_LAMBDA }, { KOS_NULL, OT_NONE } },
    { { ">",      OT_GT     }, { ">=",     OT_GE     }, { ">>",     OT_SHR    }, { ">>=",    OT_SETSHR }, { ">>>",    OT_SHRU }, { ">>>=", OT_SETSHRU }, { KOS_NULL, OT_NONE } },
    { { "?",      OT_LOGTRI }, { KOS_NULL, OT_NONE   } },
    { { "^",      OT_XOR    }, { "^=",     OT_SETXOR }, { KOS_NULL, OT_NONE   } },
    { { "|",      OT_OR     }, { "|=",     OT_SETOR  }, { "||",     OT_LOGOR  }, { KOS_NULL, OT_NONE   } },
    { { "~",      OT_NOT    }, { KOS_NULL, OT_NONE   } }
};

static const char *const keywords[] = {
    KOS_NULL,
    "_",
    "__line__",
    "assert",
    "async",
    "break",
    "case",
    "catch",
    "class",
    "const",
    "constructor",
    "continue",
    "default",
    "defer",
    "delete",
    "do",
    "else",
    "extends",
    "fallthrough",
    "false",
    "for",
    "fun",
    "get",
    "if",
    "import",
    "in",
    "instanceof",
    "loop",
    "match",
    "propertyof",
    "public",
    "repeat",
    "return",
    "set",
    "static",
    "super",
    "switch",
    "this",
    "throw",
    "true",
    "try",
    "typeof",
    "var",
    "void",
    "while",
    "with",
    "yield"
};

static unsigned prefetch_next(KOS_LEXER *lexer, const char **begin, const char **end)
{
    unsigned lt;

    const char *b = lexer->prefetch_end;
    const char *e = b;

    const unsigned line = lexer->pos.line;
    const unsigned col  = lexer->pos.column;

    if (b < lexer->buf_end) {
        lt = lexem_types[(unsigned char)*b];

        if (lt == LT_UTF8_TAIL)
            lt = LT_INVALID_UTF8;
        else if ((lt & LT_UTF8_MULTI) != 0) {
            const unsigned len = lt & LT_UTF8_MASK;

            assert(len > 0);

            if (b + len > lexer->buf_end) {
                lt = LT_INVALID_UTF8;
                e  = lexer->buf_end;
            }
            else {
                unsigned code = ((((unsigned char)*b) << len) & 0xFFU) >> len;
                unsigned i;
                for (i = 1; i < len; ++i) {
                    const unsigned char c = (unsigned char)b[i];
                    if (lexem_types[c] != LT_UTF8_TAIL) {
                        lt = LT_INVALID_UTF8;
                        break;
                    }
                    code = (code << 6) | (c & 0x3F);
                }
                e = b + i;

                if (code == 0x00A0 || /* NBSP */
                    code == 0x2028 || /* line separator */
                    code == 0x2029 || /* paragraph separator */
                    code == 0xFEFF    /* BOM */ )

                    lt = LT_WHITESPACE;
            }
        }
        else
            e = b + 1;
    }
    else
        lt = LT_EOF;

    if (lt == LT_EOL && *b == '\r' && (b + 1 < lexer->buf_end) && b[1] == '\n')
        ++e;

    *begin = b;
    *end   = e;

    lexer->prefetch_end = e;

    lexer->old_pos.line   = line;
    lexer->old_pos.column = col;

    if (lt == LT_EOL) {
        lexer->pos.line   = line + 1;
        lexer->pos.column = 1;
    }
    else if (lt == LT_TAB)
        lexer->pos.column = ((col + 8U) & ~7U) + 1U;
    else
        lexer->pos.column = col + 1;

    return lt;
}

static void retract(KOS_LEXER *lexer, const char *back)
{
    lexer->prefetch_end = back;
    lexer->pos          = lexer->old_pos;
}

static void collect_whitespace(KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    while (c == LT_WHITESPACE)
        c = prefetch_next(lexer, &begin, &end);

    retract(lexer, begin);
}

static void collect_all_until_eol(KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    while ((c & LT_EOL) == 0 && c != LT_INVALID_UTF8)
        c = prefetch_next(lexer, &begin, &end);

    retract(lexer, begin);
}

static int char_is_hex(char c)
{
    return hex_and_operator_map[(unsigned char)c] == OMI_HEX;
}

static int char_is_hex_or_underscore(char c)
{
    return hex_and_operator_map[(unsigned char)c] == OMI_HEX || c == '_';
}

static int char_is_bin_or_underscore(char c)
{
    return c == '0' || c == '1' || c == '_';
}

static int collect_escape(KOS_LEXER *lexer, int *format)
{
    const char *begin, *end;

    int error = KOS_SUCCESS;

    unsigned c = prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_esc;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        const char esc_type = kos_escape_sequence_map[(unsigned char)*begin];
        if (esc_type == KOS_ET_HEX) {
            c = prefetch_next(lexer, &begin, &end);
            if (c == LT_EOF) {
                lexer->error_str = str_err_eof_esc;
                error = KOS_ERROR_SCANNING_FAILED;
            }
            else if (*begin == '{') {
                int count = 0;
                for (;; ++count) {
                    c = prefetch_next(lexer, &begin, &end);
                    if (c == LT_EOF) {
                        lexer->error_str = str_err_eof_esc;
                        error = KOS_ERROR_SCANNING_FAILED;
                        break;
                    }
                    else if (*begin == '}')
                        break;
                    else if (!char_is_hex(*begin)) {
                        lexer->error_str = str_err_hex;
                        error = KOS_ERROR_SCANNING_FAILED;
                        break;
                    }
                }
                if (count == 0) {
                    lexer->error_str = str_err_no_hex_digits;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
                else if (count > 6) {
                    lexer->error_str = str_err_too_many_hex_digits;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
            }
            else if (char_is_hex(*begin)) {
                c = prefetch_next(lexer, &begin, &end);
                if (c == LT_EOF) {
                    lexer->error_str = str_err_eof_esc;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
                else if (!char_is_hex(*begin)) {
                    lexer->error_str = str_err_hex;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
            }
            else {
                lexer->error_str = str_err_hex;
                error = KOS_ERROR_SCANNING_FAILED;
            }
        }
        else if (esc_type == KOS_ET_INTERPOLATE)
            *format = 1;
    }

    return error;
}

static int collect_string(KOS_LEXER *lexer)
{
    const char *begin, *end;

    int error = KOS_SUCCESS;

    unsigned   c      = prefetch_next(lexer, &begin, &end);
    int        format = 0;

    while ((c != LT_EOF) && (c != LT_INVALID_UTF8) && (c != LT_STRING)) {

        if (c == LT_BACKSLASH) {
            error = collect_escape(lexer, &format);
            if (error || format)
                break;
        }
        else if (c == LT_EOL) {
            lexer->error_str = str_err_eol_str;
            error            = KOS_ERROR_SCANNING_FAILED;
            break;
        }

        c = prefetch_next(lexer, &begin, &end);
    }

    if (!error) {
        if (c == LT_EOF) {
            lexer->error_str = str_err_eof_str;
            error = KOS_ERROR_SCANNING_FAILED;
        }
        else if (c == LT_INVALID_UTF8) {
            lexer->error_str = str_err_invalid_utf8;
            error = KOS_ERROR_SCANNING_FAILED;
        }
    }

    return error;
}

static int collect_raw_string(KOS_LEXER *lexer)
{
    const char *begin, *end;

    int error = KOS_SUCCESS;

    unsigned c = prefetch_next(lexer, &begin, &end);

    while ((c != LT_EOF) && (c != LT_INVALID_UTF8) && (c != LT_STRING)) {

        if (c == LT_BACKSLASH) {
            c = prefetch_next(lexer, &begin, &end);
            if (c == LT_EOF || c == LT_INVALID_UTF8)
                break;
        }

        c = prefetch_next(lexer, &begin, &end);
    }

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_str;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else if (c == LT_INVALID_UTF8) {
        lexer->error_str = str_err_invalid_utf8;
        error = KOS_ERROR_SCANNING_FAILED;
    }

    return error;
}

static void collect_identifier(KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    while ((c & LT_ALPHANUMERIC) != 0)
        c = prefetch_next(lexer, &begin, &end);

    retract(lexer, begin);
}

static void collect_block_comment(KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    while (c != LT_EOF) {
        const char prev = *begin;

        c = prefetch_next(lexer, &begin, &end);

        if (c == LT_INVALID_UTF8) {
            retract(lexer, begin);
            break;
        }

        if (prev == '*' && c != LT_EOF && *begin == '/')
            break;
    }
}

static int is_digit_or_underscore(unsigned c)
{
    return c == LT_DIGIT || c == LT_UNDERSCORE;
}

static int collect_decimal(KOS_LEXER *lexer)
{
    const char *begin, *end;
    unsigned    c;
    int         error = KOS_SUCCESS;

    if (*lexer->prefetch_begin != '0') {
        do
            c = prefetch_next(lexer, &begin, &end);
        while (is_digit_or_underscore(c));
    }
    else
        c = prefetch_next(lexer, &begin, &end);

    if (c == LT_OPERATOR && *begin == '.')
        do
            c = prefetch_next(lexer, &begin, &end);
        while (is_digit_or_underscore(c));

    if (c == LT_LETTER && (*begin == 'e' || *begin == 'E' || *begin == 'p' || *begin == 'P')) {
        c = prefetch_next(lexer, &begin, &end);

        if (c == LT_OPERATOR && (*begin == '+' || *begin == '-'))
            c = prefetch_next(lexer, &begin, &end);

        if (is_digit_or_underscore(c)) {
            if (*begin != '0') {
                do
                    c = prefetch_next(lexer, &begin, &end);
                while (is_digit_or_underscore(c));
            }
            else
                c = prefetch_next(lexer, &begin, &end);
        }
        else
            c = LT_DIGIT; /* trigger error */
    }

    retract(lexer, begin);

    if ((c & LT_ALPHANUMERIC) != 0) {
        lexer->error_str = str_err_invalid_dec;
        error = KOS_ERROR_SCANNING_FAILED;
    }

    return error;
}

static int collect_hex(KOS_LEXER *lexer)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_hex;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else if (!char_is_hex_or_underscore(*begin)) {
        lexer->error_str = str_err_hex;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        do
            c = prefetch_next(lexer, &begin, &end);
        while (c != LT_EOF && char_is_hex_or_underscore(*begin));
        retract(lexer, begin);
    }

    return error;
}

static int collect_bin(KOS_LEXER *lexer)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;

    unsigned c = prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_bin;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else if (!char_is_bin_or_underscore(*begin)) {
        lexer->error_str = str_err_bin;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        do
            c = prefetch_next(lexer, &begin, &end);
        while (c != LT_EOF && char_is_bin_or_underscore(*begin));
        retract(lexer, begin);
    }

    return error;
}

static KOS_OPERATOR_TYPE collect_operator(KOS_LEXER *lexer)
{
    const struct KOS_OP_SPECIFIER_S *op_group =
            operator_map[hex_and_operator_map[(unsigned char)*lexer->prefetch_begin]];

    const char       *begin, *end;
    int               idx = 1;
    KOS_OPERATOR_TYPE op  = OT_NONE;
    char              cur;

    do {
        unsigned c;

        cur = op_group->str[idx];
        op  = op_group->type;

        c = prefetch_next(lexer, &begin, &end);

        if (c != LT_OPERATOR)
            break;

        while (*begin > cur) {
            ++op_group;
            if (op_group->type == OT_NONE) {
                cur = 0;
                break;
            }
            cur = op_group->str[idx];
        }

        ++idx;
    }
    while (cur && *begin == cur);

    retract(lexer, begin);

    /* operator_map currently poorly handles the transition from
     * . to ... - for now we need to fix it up to avoid incorrectly
     * interpreting two dots */
    if ((op == OT_MORE) && (lexer->prefetch_end - lexer->prefetch_begin == 2)) {
        op = OT_DOT;
        --lexer->prefetch_end;
        --lexer->pos.column;
    }

    return op;
}

static KOS_KEYWORD_TYPE find_keyword(const char *begin, const char *end)
{
    KOS_KEYWORD_TYPE kw   = KW_NONE;
    const int        num  = (int)(end - begin);
    unsigned char    low  = 1;
    unsigned char    high = sizeof(keywords) / sizeof(keywords[0]) - 1;

    while (low <= high) {
        const unsigned char mid = (low + high) / 2U;
        int                 cmp = strncmp(begin, keywords[mid], (size_t)num);
        if (!cmp) {
            if (keywords[mid][num])
                high = mid - 1U;
            else {
                kw = (KOS_KEYWORD_TYPE)mid;
                break;
            }
        }
        else if (cmp < 0)
            high = mid - 1U;
        else
            low = mid + 1U;
    }

    return kw;
}

#if defined(CONFIG_SEQFAIL) || defined(CONFIG_FUZZ)
static void set_seq_fail(const char *begin, const char *end)
{
    static const char str_seq[] = "seq";
    int64_t           value;

    while ((begin < end) && (*begin == ' '))
        ++begin;

    if ((begin + sizeof(str_seq) >= end) || memcmp(begin, str_seq, sizeof(str_seq) - 1))
        return;

    begin += sizeof(str_seq) - 1;

    while ((begin < end) && ((uint8_t)end[-1] <= 0x20U))
        --end;

    if (begin == end)
        return;

    if (kos_parse_int(begin, end, &value))
        return;

    kos_set_seq_point((int)value);
}
#else
#   define set_seq_fail(begin, end) ((void)0)
#endif

static void skip_bom(KOS_LEXER *lexer)
{
    const char *begin   = lexer->prefetch_begin;
    const char *buf_end = lexer->buf_end;
    const char *bom_end = begin + 3;

    if (bom_end <= buf_end &&
        begin[0] == '\xEF' &&
        begin[1] == '\xBB' &&
        begin[2] == '\xBF') {

        lexer->prefetch_begin = bom_end;
        lexer->prefetch_end   = bom_end;
    }
}

void kos_lexer_init(KOS_LEXER  *lexer,
                    uint16_t    file_id,
                    const char *begin,
                    const char *end)
{
    lexer->buf             = begin;
    lexer->buf_end         = end;
    lexer->prefetch_begin  = begin;
    lexer->prefetch_end    = begin;
    lexer->error_str       = KOS_NULL;
    lexer->pos.file_id     = file_id;
    lexer->pos.line        = 1;
    lexer->pos.column      = 1;
    lexer->old_pos.file_id = file_id;
    lexer->old_pos.line    = 0;
    lexer->old_pos.column  = 0;

    /* Ignore UTF-8 byte order mark at the beginning of a file */
    skip_bom(lexer);
}

void kos_lexer_update(KOS_LEXER  *lexer,
                      const char *begin,
                      const char *end)
{
    assert(lexer->pos.line >= 1);
    assert(lexer->error_str == KOS_NULL);

    lexer->buf             = begin;
    lexer->buf_end         = end;
    lexer->prefetch_begin  = begin;
    lexer->prefetch_end    = begin;

    --lexer->pos.column;
}

int kos_lexer_next_token(KOS_LEXER          *lexer,
                         KOS_NEXT_TOKEN_MODE mode,
                         KOS_TOKEN          *token)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;

    token->keyword = KW_NONE;
    token->op      = OT_NONE;
    token->sep     = ST_NONE;
    token->file_id = lexer->pos.file_id;
    token->column  = lexer->pos.column;
    token->line    = lexer->pos.line;

    if (mode != NT_ANY) {
        token->type = TT_STRING;
        begin       = lexer->prefetch_begin;

        if (begin >= lexer->buf_end) {
            token->type      = TT_EOF;
            end              = begin;
            lexer->error_str = str_err_eof_cont;
            error            = KOS_ERROR_SCANNING_FAILED;
        }
        else if (*begin != ')') {
            end                 = begin + 1;
            lexer->prefetch_end = end;
            lexer->error_str    = str_err_cont;
            error               = KOS_ERROR_SCANNING_FAILED;
        }
        else {
            error = collect_string(lexer);
            end   = lexer->prefetch_end;
            if (*(end-1) == '(')
                token->type = TT_STRING_OPEN;
        }
    }
    else {

        unsigned c = prefetch_next(lexer, &begin, &end);

        switch (c) {
            case LT_WHITESPACE:
                token->type = TT_WHITESPACE;
                collect_whitespace(lexer);
                end = lexer->prefetch_end;
                break;
            case LT_EOL:
                token->type = TT_EOL;
                break;
            case LT_LETTER:
                if (*begin == 'r' || *begin == 'R') {
                    const char *end2;
                    c = prefetch_next(lexer, &end, &end2);
                    if (c == LT_STRING) {
                        token->type = TT_STRING;
                        error = collect_raw_string(lexer);
                        end = lexer->prefetch_end;
                        break;
                    }
                    else
                        retract(lexer, end);
                }
                /* fall through */
            case LT_UNDERSCORE:
                collect_identifier(lexer);
                end = lexer->prefetch_end;
                token->keyword = find_keyword(begin, end);
                token->type    = (token->keyword == KW_NONE) ? TT_IDENTIFIER : TT_KEYWORD;
                break;
            case LT_STRING:
                token->type = TT_STRING;
                error = collect_string(lexer);
                end = lexer->prefetch_end;
                if (*(end-1) == '(')
                    token->type = TT_STRING_OPEN;
                break;
            case LT_DIGIT:
                token->type = TT_NUMERIC;
                if (*begin == '0') {
                    const char *end2;
                    c = prefetch_next(lexer, &end, &end2);
                    if (c != LT_EOF && (*end == 'x' || *end == 'X')) {
                        error = collect_hex(lexer);
                        end   = lexer->prefetch_end;
                    }
                    else if (c != LT_EOF && (*end == 'b' || *end == 'B')) {
                        error = collect_bin(lexer);
                        end   = lexer->prefetch_end;
                    }
                    else {
                        retract(lexer, end);
                        error = collect_decimal(lexer);
                        end   = lexer->prefetch_end;
                    }
                }
                else {
                    error = collect_decimal(lexer);
                    end = lexer->prefetch_end;
                }
                break;
            case LT_OPERATOR:
                token->type = TT_OPERATOR;
                token->op   = collect_operator(lexer);
                end = lexer->prefetch_end;
                break;
            case LT_SEPARATOR:
                token->type = TT_SEPARATOR;
                token->sep  = (KOS_SEPARATOR_TYPE)hex_and_operator_map[
                              (unsigned char)*lexer->prefetch_begin];
                break;
            case LT_SLASH: {
                const char *begin2;
                c = prefetch_next(lexer, &begin2, &end);
                if (c != LT_EOF) {
                    if (c == LT_SLASH) {
                        token->type = TT_COMMENT;
                        collect_all_until_eol(lexer);
                        end = lexer->prefetch_end;
                        set_seq_fail(begin2 + 1, end);
                    }
                    else if (*begin2 == '*') {
                        token->type = TT_COMMENT;
                        collect_block_comment(lexer);
                        end = lexer->prefetch_end;
                        set_seq_fail(begin2 + 1, end - 2);
                    }
                    else {
                        token->type = TT_OPERATOR;
                        retract(lexer, begin2);
                        token->op = collect_operator(lexer);
                        end = lexer->prefetch_end;
                    }
                }
                else
                    token->type = TT_OPERATOR;
                break;
            }
            case LT_COMMENT:
                token->type = TT_COMMENT;
                collect_all_until_eol(lexer);
                end = lexer->prefetch_end;
                set_seq_fail(begin + 1, end);
                break;
            case LT_EOF:
                token->type = TT_EOF;
                break;
            case LT_TAB:
                lexer->error_str = str_err_tab;
                error = KOS_ERROR_SCANNING_FAILED;
                break;
            case LT_INVALID:
                lexer->error_str = str_err_invalid_char;
                error = KOS_ERROR_SCANNING_FAILED;
                break;
            case LT_INVALID_UTF8:
                lexer->error_str = str_err_invalid_utf8;
                error = KOS_ERROR_SCANNING_FAILED;
                break;
            default:
                lexer->error_str = str_err_char;
                error = KOS_ERROR_SCANNING_FAILED;
                break;
        }
    }

    lexer->prefetch_begin = lexer->prefetch_end;

    token->begin = begin;

    if (end - begin > 0xFFFF) {
        token->length = 0xFFFFU;
        if ( ! error) {
            lexer->error_str = str_err_token_too_long;
            error            = KOS_ERROR_SCANNING_FAILED;
        }
    }
    else
        token->length = (uint16_t)(end - begin);

    if (error) {
        if (lexer->pos.column == 1)
            retract(lexer, lexer->prefetch_end);
        else
            lexer->pos.column--;
    }

    return error;
}

void kos_lexer_unget_token(KOS_LEXER *lexer, const KOS_TOKEN *token)
{
    lexer->prefetch_begin = token->begin;
    lexer->prefetch_end   = token->begin;
    lexer->pos            = get_token_pos(token);
}

KOS_FILE_POS get_token_pos(const KOS_TOKEN *token)
{
    KOS_FILE_POS pos;

    pos.file_id = token->file_id;
    pos.column  = token->column;
    pos.line    = token->line;

    return pos;
}

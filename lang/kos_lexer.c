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

#include "kos_lexer.h"
#include "../inc/kos_error.h"
#include "kos_utf8.h"
#include <string.h>

static const char str_err_bin[]                 = "unexpected character, binary digit expected";
static const char str_err_char[]                = "unexpected character";
static const char str_err_cont[]                = "unexpected character, ')' expected as string continuation";
static const char str_err_eof_bin[]             = "unexpected end of file, binary digit expected";
static const char str_err_eof_cont[]            = "unexpected end of file, string continuation expected";
static const char str_err_eof_esc[]             = "unexpected end of file, unfinished escape sequence";
static const char str_err_eof_hex[]             = "unexpected end of file, hexadecimal digit expected";
static const char str_err_eof_str[]             = "unexpected end of file, unfinished string literal";
static const char str_err_hex[]                 = "hexadecimal digit expected";
static const char str_err_invalid_char[]        = "invalid character";
static const char str_err_invalid_dec[]         = "invalid decimal literal";
static const char str_err_invalid_esc[]         = "unsupported escape sequence";
static const char str_err_invalid_utf8[]        = "invalid UTF-8 character sequence";
static const char str_err_no_hex_digits[]       = "invalid escape sequence, no hex digits specified";
static const char str_err_too_many_hex_digits[] = "invalid escape sequence, more than 6 hex digits specified";
static const char str_err_tab[]                 = "unexpected tab character, tabs are not allowed";

enum _KOS_LEXEM_TYPE {
    LT_INVALID,
    LT_WHITESPACE,
    LT_COMMENT,
    LT_BACKSLASH,
    LT_SEPARATOR,
    LT_OPERATOR,
    LT_SLASH,
    LT_STRING,

    LT_ALPHANUMERIC = 0x10,
    LT_DIGIT        = 0x10,
    LT_LETTER       = 0x11,

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
    /* 0 */
    LT_WHITESPACE,
    /* 1..7 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID,
    /* 8 */
    LT_INVALID,
    /* 9(TAB) */
    LT_INVALID,
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
    LT_STRING,
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
    LT_LETTER,
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
    LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2, LT_UTF8_2,
    /* 224..239 */
    LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3,
    LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3, LT_UTF8_3,
    /* 240..247 */
    LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4, LT_UTF8_4,
    /* 248..255 */
    LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID, LT_INVALID
};

enum _OPERATOR_MAP_INDEX {
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

struct _KOS_OP_SPECIFIER {
    const char             *str;
    enum _KOS_OPERATOR_TYPE type;
};

static const struct _KOS_OP_SPECIFIER operator_map[][7] = {
    { { 0,   OT_NONE   } },
    { { "!", OT_LOGNOT }, { "!=",  OT_NE     }, { 0,     OT_NONE   } },
    { { "%", OT_MOD    }, { "%=",  OT_SETMOD }, { 0,     OT_NONE   } },
    { { "&", OT_AND    }, { "&&",  OT_LOGAND }, { "&=",  OT_SETAND }, { 0,     OT_NONE   } },
    { { "*", OT_MUL    }, { "*=",  OT_SETMUL }, { 0,     OT_NONE } },
    { { "+", OT_ADD    }, { "+=",  OT_SETADD }, { 0,     OT_NONE   } },
    { { "-", OT_SUB    }, { "-=",  OT_SETSUB }, { "->",  OT_FUN    }, { 0,     OT_NONE   } },
    { { ".", OT_DOT    }, { "...", OT_MORE   }, { 0,     OT_NONE   } },
    { { "/", OT_DIV    }, { "/=",  OT_SETDIV }, { 0,     OT_NONE   } },
    { { "<", OT_LT     }, { "<<",  OT_SHL    }, { "<<=", OT_SETSHL }, { "<=",  OT_LE     }, { 0,     OT_NONE } },
    { { "=", OT_SET    }, { "==",  OT_EQ     }, { 0,     OT_NONE } },
    { { ">", OT_GT     }, { ">=",  OT_GE     }, { ">>",  OT_SHR    }, { ">>=", OT_SETSHR }, { ">>>", OT_SSR  }, { ">>>=", OT_SETSSR }, { 0, OT_NONE } },
    { { "?", OT_LOGTRI }, { 0,     OT_NONE   } },
    { { "^", OT_XOR    }, { "^=",  OT_SETXOR }, { 0,     OT_NONE   } },
    { { "|", OT_OR     }, { "|=",  OT_SETOR  }, { "||",  OT_LOGOR  }, { 0,     OT_NONE   } },
    { { "~", OT_NOT    }, { 0,     OT_NONE   } }
};

static const char *const keywords[] = {
    0,
    "__line__",
    "assert",
    "break",
    "catch",
    "const",
    "continue",
    "defer",
    "delete",
    "do",
    "else",
    "fallthrough",
    "false",
    "finally",
    "for",
    "fun",
    "get",
    "if",
    "import",
    "in",
    "instanceof",
    "new",
    "private",
    "prototype",
    "public",
    "return",
    "set",
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
    "yield",
    "λ"
};

static unsigned _prefetch_next(struct _KOS_LEXER *lexer, const char **begin, const char **end)
{
    unsigned lt;

    const char *b = lexer->prefetch_end;
    const char *e = b;

    const unsigned line = lexer->pos.line;
    const unsigned col  = lexer->pos.column;

    if (b < lexer->buf_end) {
        lt = lexem_types[(unsigned char)*b];

        if ((lt & LT_UTF8_MULTI) != 0) {
            unsigned len = lt & LT_UTF8_MASK;

            if (len == 0) {
                lt = LT_INVALID_UTF8;
                e  = b + 1;
            }
            else if (b + len > lexer->buf_end) {
                lt = LT_INVALID_UTF8;
                e  = lexer->buf_end;
            }
            else {
                unsigned code = ((((unsigned char)*b) << len) & 0xFFU) >> len;
                unsigned i;
                for (i=1; i < len; ++i) {
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

    if (lt == LT_EOL && *b == '\r' && b < lexer->buf_end && b[1] == '\n')
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
    else
        lexer->pos.column = col + 1;

    return lt;
}

static void _retract(struct _KOS_LEXER *lexer, const char *back)
{
    lexer->prefetch_end = back;
    lexer->pos          = lexer->old_pos;
}

static void _collect_whitespace(struct _KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    while (c == LT_WHITESPACE)
        c = _prefetch_next(lexer, &begin, &end);

    _retract(lexer, begin);
}

static void _collect_all_until_eol(struct _KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    while ((c & LT_EOL) == 0)
        c = _prefetch_next(lexer, &begin, &end);

    _retract(lexer, begin);
}

static int _char_is_hex(char c)
{
    return hex_and_operator_map[(unsigned char)c] == OMI_HEX;
}

static int _char_is_bin(char c)
{
    return c == '0' || c == '1';
}

static int _collect_escape(struct _KOS_LEXER *lexer, int *format)
{
    const char *begin, *end;

    int error = KOS_SUCCESS;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_esc;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        const char esc_type = _KOS_escape_sequence_map[(unsigned char)*begin];
        if (esc_type == KOS_ET_HEX) {
            c = _prefetch_next(lexer, &begin, &end);
            if (c == LT_EOF) {
                lexer->error_str = str_err_eof_esc;
                error = KOS_ERROR_SCANNING_FAILED;
            }
            else if (*begin == '{') {
                int count = 0;
                for (;; ++count) {
                    c = _prefetch_next(lexer, &begin, &end);
                    if (c == LT_EOF) {
                        lexer->error_str = str_err_eof_esc;
                        error = KOS_ERROR_SCANNING_FAILED;
                    }
                    else if (*begin == '}')
                        break;
                    else if (!_char_is_hex(*begin)) {
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
            else if (_char_is_hex(*begin)) {
                c = _prefetch_next(lexer, &begin, &end);
                if (c == LT_EOF) {
                    lexer->error_str = str_err_eof_esc;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
                else if (!_char_is_hex(*begin)) {
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
        else if (esc_type == KOS_ET_INVALID) {
            lexer->error_str = str_err_invalid_esc;
            error = KOS_ERROR_SCANNING_FAILED;
        }
    }

    return error;
}

static int _collect_string(struct _KOS_LEXER *lexer, char delim)
{
    const char *begin, *end;

    int error = KOS_SUCCESS;

    unsigned   c      = _prefetch_next(lexer, &begin, &end);
    int        format = 0;

    while ((c != LT_EOF) && (c != LT_INVALID_UTF8) &&
           (c != LT_STRING || *(end - 1) != delim)) {

        if (c == LT_BACKSLASH) {
            error = _collect_escape(lexer, &format);
            if (error || format)
                break;
        }

        c = _prefetch_next(lexer, &begin, &end);
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

static void _collect_identifier(struct _KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    while ((c & LT_ALPHANUMERIC) != 0)
        c = _prefetch_next(lexer, &begin, &end);

    _retract(lexer, begin);
}

static void _collect_block_comment(struct _KOS_LEXER *lexer)
{
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    while (c != LT_EOF) {
        const char prev = *begin;

        c = _prefetch_next(lexer, &begin, &end);

        if (prev == '*' && c != LT_EOF && *begin == '/')
            break;
    }
}

static int _collect_decimal(struct _KOS_LEXER *lexer)
{
    const char *begin, *end;
    unsigned    c;
    int         error = KOS_SUCCESS;

    if (*lexer->prefetch_begin != '0') {
        do
            c = _prefetch_next(lexer, &begin, &end);
        while (c == LT_DIGIT);
    }
    else
        c = _prefetch_next(lexer, &begin, &end);

    if (c == LT_OPERATOR && *begin == '.')
        do
            c = _prefetch_next(lexer, &begin, &end);
        while (c == LT_DIGIT);

    if (c == LT_LETTER && (*begin == 'e' || *begin == 'E' || *begin == 'p' || *begin == 'P')) {
        c = _prefetch_next(lexer, &begin, &end);

        if (c == LT_OPERATOR && (*begin == '+' || *begin == '-'))
            c = _prefetch_next(lexer, &begin, &end);

        if (c == LT_DIGIT) {
            if (*begin != '0') {
                do
                    c = _prefetch_next(lexer, &begin, &end);
                while (c == LT_DIGIT);
            }
            else
                c = _prefetch_next(lexer, &begin, &end);
        }
        else
            c = LT_DIGIT; /* trigger error */
    }

    _retract(lexer, begin);

    if ((c & LT_ALPHANUMERIC) != 0) {
        lexer->error_str = str_err_invalid_dec;
        error = KOS_ERROR_SCANNING_FAILED;
    }

    return error;
}

static int _collect_hex(struct _KOS_LEXER *lexer)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_hex;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else if (!_char_is_hex(*begin)) {
        lexer->error_str = str_err_hex;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        do
            c = _prefetch_next(lexer, &begin, &end);
        while (c != LT_EOF && _char_is_hex(*begin));
        _retract(lexer, begin);
    }

    return error;
}

static int _collect_bin(struct _KOS_LEXER *lexer)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;

    unsigned c = _prefetch_next(lexer, &begin, &end);

    if (c == LT_EOF) {
        lexer->error_str = str_err_eof_bin;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else if (!_char_is_bin(*begin)) {
        lexer->error_str = str_err_bin;
        error = KOS_ERROR_SCANNING_FAILED;
    }
    else {
        do
            c = _prefetch_next(lexer, &begin, &end);
        while (c != LT_EOF && _char_is_bin(*begin));
        _retract(lexer, begin);
    }

    return error;
}

static void _collect_operator(struct _KOS_LEXER *lexer, enum _KOS_OPERATOR_TYPE *op)
{
    const struct _KOS_OP_SPECIFIER *op_group =
            operator_map[hex_and_operator_map[(unsigned char)*lexer->prefetch_begin]];

    const char *begin, *end;
    int         idx = 1;
    unsigned    c;
    char        cur;

    do {
        cur = op_group->str[idx];
        *op = op_group->type;

        c = _prefetch_next(lexer, &begin, &end);

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

    _retract(lexer, begin);
}

static void _find_keyword(const char *begin, const char *end, enum _KOS_KEYWORD_TYPE *kw)
{
    const int     num  = (int)(end - begin);
    unsigned char low  = 1;
    unsigned char high = sizeof(keywords) / sizeof(keywords[0]) - 1;
    while (low <= high) {
        const unsigned char mid = (low + high) / 2U;
        int                 cmp = strncmp(begin, keywords[mid], (size_t)num);
        if (!cmp) {
            if (keywords[mid][num])
                high = mid - 1U;
            else {
                *kw = (enum _KOS_KEYWORD_TYPE)mid;
                break;
            }
        }
        else if (cmp < 0)
            high = mid - 1U;
        else
            low = mid + 1U;
    }
}

void _KOS_lexer_init(struct _KOS_LEXER *lexer,
                     unsigned           file_id,
                     const char        *begin,
                     const char        *end)
{
    lexer->buf             = begin;
    lexer->buf_end         = end;
    lexer->prefetch_begin  = begin;
    lexer->prefetch_end    = begin;
    lexer->error_str       = 0;
    lexer->pos.file_id     = file_id;
    lexer->pos.line        = 1;
    lexer->pos.column      = 1;
    lexer->old_pos.file_id = file_id;
    lexer->old_pos.line    = 0;
    lexer->old_pos.column  = 0;

    /* Ignore UTF-8 byte order mark at the beginning of a file */
    if (begin + 3 <= end   &&
        begin[0] == '\xEF' &&
        begin[1] == '\xBB' &&
        begin[2] == '\xBF') {

        lexer->prefetch_begin += 3;
        lexer->prefetch_end   += 3;
    }
}

int _KOS_lexer_next_token(struct _KOS_LEXER        *lexer,
                          enum _KOS_NEXT_TOKEN_MODE mode,
                          struct _KOS_TOKEN        *token)
{
    int error = KOS_SUCCESS;
    const char *begin, *end;
    unsigned c;

    token->keyword = KW_NONE;
    token->op      = OT_NONE;
    token->sep     = ST_NONE;
    token->pos     = lexer->pos;

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
            error = _collect_string(lexer, mode == NT_SINGLE_Q_STRING ? '\'' : '"');
            end   = lexer->prefetch_end;
            if (*(end-1) == '(') {
                if (mode == NT_SINGLE_Q_STRING)
                    token->type = TT_STRING_OPEN_SQ;
                else
                    token->type = TT_STRING_OPEN_DQ;
            }
        }
    }
    else {

        c = _prefetch_next(lexer, &begin, &end);

        switch (c) {
            case LT_WHITESPACE:
                token->type = TT_WHITESPACE;
                _collect_whitespace(lexer);
                end = lexer->prefetch_end;
                break;
            case LT_EOL:
                token->type = TT_EOL;
                break;
            case LT_LETTER:
                _collect_identifier(lexer);
                end = lexer->prefetch_end;
                _find_keyword(begin, end, &token->keyword);
                token->type = (token->keyword == KW_NONE) ? TT_IDENTIFIER : TT_KEYWORD;
                break;
            case LT_UTF8_2:
                end = lexer->prefetch_end;
                _find_keyword(begin, end, &token->keyword);
                if (token->keyword != KW_NONE)
                    token->type = TT_KEYWORD;
                else {
                    lexer->error_str = str_err_char;
                    error = KOS_ERROR_SCANNING_FAILED;
                }
                break;
            case LT_STRING:
                token->type = TT_STRING;
                error = _collect_string(lexer, *begin);
                end = lexer->prefetch_end;
                if (*(end-1) == '(') {
                    if (*begin == '"')
                        token->type = TT_STRING_OPEN_DQ;
                    else
                        token->type = TT_STRING_OPEN_SQ;
                }
                break;
            case LT_DIGIT:
                token->type = TT_NUMERIC;
                if (*begin == '0') {
                    const char *end2;
                    c = _prefetch_next(lexer, &end, &end2);
                    if (c != LT_EOF && (*end == 'x' || *end == 'X')) {
                        error = _collect_hex(lexer);
                        end   = lexer->prefetch_end;
                    }
                    else if (c != LT_EOF && (*end == 'b' || *end == 'B')) {
                        error = _collect_bin(lexer);
                        end   = lexer->prefetch_end;
                    }
                    else {
                        _retract(lexer, end);
                        error = _collect_decimal(lexer);
                        end   = lexer->prefetch_end;
                    }
                }
                else {
                    error = _collect_decimal(lexer);
                    end = lexer->prefetch_end;
                }
                break;
            case LT_OPERATOR:
                token->type = TT_OPERATOR;
                _collect_operator(lexer, &token->op);
                end = lexer->prefetch_end;
                break;
            case LT_SEPARATOR:
                token->type = TT_SEPARATOR;
                token->sep  = (enum _KOS_SEPARATOR_TYPE)hex_and_operator_map[
                              (unsigned char)*lexer->prefetch_begin];
                break;
            case LT_SLASH: {
                const char *begin2;
                c = _prefetch_next(lexer, &begin2, &end);
                if (c != LT_EOF) {
                    if (c == LT_SLASH) {
                        token->type = TT_COMMENT;
                        _collect_all_until_eol(lexer);
                        end = lexer->prefetch_end;
                    }
                    else if (*begin2 == '*') {
                        token->type = TT_COMMENT;
                        _collect_block_comment(lexer);
                        end = lexer->prefetch_end;
                    }
                    else {
                        token->type = TT_OPERATOR;
                        _retract(lexer, begin2);
                        _collect_operator(lexer, &token->op);
                        end = lexer->prefetch_end;
                    }
                }
                else
                    token->type = TT_OPERATOR;
                break;
            }
            case LT_COMMENT:
                token->type = TT_COMMENT;
                _collect_all_until_eol(lexer);
                end = lexer->prefetch_end;
                break;
            case LT_EOF:
                token->type = TT_EOF;
                break;
            case LT_INVALID:
                if (*begin == '\t')
                    lexer->error_str = str_err_tab;
                else
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

    token->begin  = begin;
    token->length = (unsigned)(end - begin);

    if (error)
        lexer->pos.column--;

    return error;
}

void _KOS_lexer_unget_token(struct _KOS_LEXER *lexer, const struct _KOS_TOKEN *token)
{
    lexer->prefetch_begin = token->begin;
    lexer->prefetch_end   = token->begin;
    lexer->pos            = token->pos;
}

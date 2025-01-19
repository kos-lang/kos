/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_utf8.h"
#include "../inc/kos_utils.h"
#include "../core/kos_debug.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_math.h"
#include "../core/kos_try.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_begin,                  "begin");
KOS_DECLARE_STATIC_CONST_STRING(str_end,                    "end");
KOS_DECLARE_STATIC_CONST_STRING(str_err_regex_not_a_string, "regular expression is not a string");
KOS_DECLARE_STATIC_CONST_STRING(str_groups,                 "groups");
KOS_DECLARE_STATIC_CONST_STRING(str_match,                  "match");
KOS_DECLARE_STATIC_CONST_STRING(str_match_groups,           "match_groups");
KOS_DECLARE_STATIC_CONST_STRING(str_regex,                  "regex");
KOS_DECLARE_STATIC_CONST_STRING(str_string,                 "string");

/*
 * Regular expression special characters
 * -------------------------------------
 *
 *  Characters with special meaning
 *  - .  One (any) character
 *  - *  Zero or more of the preceding
 *  - +  One or more of the preceding
 *  - ?  Option
 *  - {  Open count of the preceding
 *  - ^  Line begin
 *  - $  Line end
 *  - \  Escape
 *  - [  Class open
 *  - |  Alternative
 *  - (  Group begin
 *
 *  Characters which only have a special meaning in some context
 *  - }  Close count
 *  - ,  Count range
 *  - ]  Class close
 *  - ^  Class negation (only first char in class)
 *  - -  Class range
 *  - )  Group end
 *
 *  Escape sequences
 *  - <  Word begin
 *  - >  Word end
 *  - A  Start of string
 *  - b  Word transition (begin or end)
 *  - B  Empty string inside a word
 *  - d  Any digit (flag selects Unicode or ASCII), for ASCII: [0-9]
 *  - D  Not digit
 *  - s  Any whitespace character (flag selects Unicode or ASCII), for ASCII: [ \t\n\r\f\v]
 *  - S  Any non-whitespace character
 *  - w  Any word character (flag selects Unicode or ASCII), for ASCII: [a-zA-Z0-9_]
 *  - W  Any non-word character
 *  - Z  End of string
 *
 *
 * Regular expression syntax
 * -------------------------
 *
 * REG_EX ::= AlternateMatchSequence
 *
 * AlternateMatchSequence ::= MatchSequence ( "|" MatchSequence )*
 *
 * MatchSequence ::= ( SingleMatch [ Multiplicity ] )*
 *
 * Multiplicity ::= ZeroOrMore
 *                | OneOrMore
 *                | ZeroOrOne
 *                | Count
 *
 * ZeroOrMore ::= "*" [ "?" ]
 *
 * OneOrMore ::= "+" [ "?" ]
 *
 * ZeroOrOne ::= "?" [ "?" ]
 *
 * Count ::= "{" Number [ "," Number ] "}" [ "?" ]
 *
 * Number ::= Digit ( Digit )*
 *
 * Digit ::= "0" .. "9"
 *
 * SingleMatch ::= OneCharacter
 *               | AnyCharacter
 *               | LineBegin
 *               | LineEnd
 *               | EscapeSequence
 *               | CharacterClass
 *               | Group
 *
 * OneCharacter ::= UTF8_CHARACTER except ( "." | "*" | "+" | "?" | "^" | "$" | "\\" | "|" | "{" | "(" | "[" )
 *
 * AnyCharacter ::= "."
 *
 * LineBegin ::= "^"
 *
 * LineEnd ::= "$"
 *
 * EscapeSequence ::= "\\" ( LiteralEscapeChar | "<" | ">" | "A" | "b" | "B" | "d" | "D" | "s" | "S" | "w" | "W" | "Z" | Digit )
 *
 * LiteralEscapeChar ::= "*" | "+" | "?" "{" | "^" | "$" | "\" | "[" | "]" | "|" | "(" | """
 *
 * CharacterClass ::= "[" [ "^" ] ClassGroup ( ClassGroup )* "]"
 *
 * ClassGroup ::= ClassCharacter [ "-" ClassCharacter ]
 *
 * ClassCharacter ::= OneClassCharacter
 *                  | EscapedClassCharacter
 *
 * OneClassCharacter ::= UTF8_CHARACTER except ( "]" | "-" | "\\" )
 *
 * EscapedClassCharacter ::= "\\" ( "]" | "-" | "\\" )
 *
 * Group ::= "(" [ GroupOpt ] AlternateMatchSequence ")"
 *
 * GroupOpt ::= "?" ":"
 */

enum RE_FLAG {
    F_ASCII       = 1, /* a -  ASCII - matches ASCII, otherwise matches Unicode */
    F_IGNORE_CASE = 2, /* i -  ignore case - case-insensitive matching */
    F_MULTI_LINE  = 4, /* m -  multi-line - ^/$ match lines, not just beginning/end of string */
    T_DOT_ALL     = 8  /* s -  dotall - dot matches any char, including \n (without this flag dot does not match \n) */
};

enum RE_INSTR {
    INSTR_MATCH_ONE_CHAR,   /* MATCH.ONE.CHAR <code>                     */
    INSTR_MATCH_ONE_CHAR32, /* MATCH.ONE.CHAR <code_hi> <code_lo>        */
    INSTR_MATCH_ANY_CHAR,   /* MATCH.ANY.CHAR                            */
    INSTR_MATCH_CLASS,      /* MATCH.CLASS <class_id>                    */
    INSTR_MATCH_NOT_CLASS,  /* MATCH.NOT.CLASS <class_id>                */
    INSTR_MATCH_LINE_BEGIN, /* MATCH.LINE.BEGIN                          */
    INSTR_MATCH_LINE_END,   /* MATCH.LINE.END                            */
    INSTR_MATCH_BOUNDARY,   /* MATCH.BOUNDARY <kind>                     */
    INSTR_BEGIN_GROUP,      /* BEGIN.GROUP <group_id>                    */
    INSTR_END_GROUP,        /* END.GROUP <group_id>                      */
    INSTR_FORK,             /* FORK <offs>                               */
    INSTR_JUMP,             /* JUMP <offs>                               */
    INSTR_GREEDY_COUNT,     /* GREEDY.COUNT <offs> <count_id> <min>      */
    INSTR_LAZY_COUNT,       /* LAZY.COUNT <offs> <count_id> <min>        */
    INSTR_GREEDY_JUMP,      /* GREEDY.JUMP <offs> <count_id> <min> <max> */
    INSTR_LAZY_JUMP         /* LAZY.JUMP <offs> <count_id> <min> <max>   */
};

enum BOUNDARY_KIND {
    BOUNDARY_NONE       = 1,
    BOUNDARY_WORD_BEGIN = 2,
    BOUNDARY_WORD_END   = 4,
    BOUNDARY_WORD       = 6
};

struct RE_INSTR_DESC_S {
    const char *str_instr;
    unsigned    num_args          : 3;
    unsigned    first_arg_is_offs : 1;
};

typedef struct RE_INSTR_DESC_S RE_INSTR_DESC;

static const RE_INSTR_DESC re_instr_descs[] = {
    { "MATCH.ONE.CHAR",   1, 0 },
    { "MATCH.ONE.CHAR32", 2, 0 },
    { "MATCH.ANY.CHAR",   0, 0 },
    { "MATCH.CLASS",      1, 0 },
    { "MATCH.NOT.CLASS",  1, 0 },
    { "MATCH.LINE.BEGIN", 0, 0 },
    { "MATCH.LINE.END",   0, 0 },
    { "MATCH.BOUNDARY",   1, 0 },
    { "BEGIN.GROUP",      1, 0 },
    { "END.GROUP",        1, 0 },
    { "FORK",             1, 1 },
    { "JUMP",             1, 1 },
    { "GREEDY.COUNT",     3, 1 },
    { "LAZY.COUNT",       3, 1 },
    { "GREEDY.JUMP",      4, 1 },
    { "LAZY.JUMP",        4, 1 }
};

struct RE_PARSE_CTX {
    KOS_CONTEXT     ctx; /* For error reporting */
    KOS_STRING_ITER iter;
    int             idx;
    int             can_be_multiplicity;
    unsigned        num_groups;
    unsigned        group_depth;
    unsigned        num_counts;
    KOS_VECTOR      buf;
    KOS_VECTOR      class_descs;
    KOS_VECTOR      class_data;

    /* Reuse character classes */
    uint16_t        digit_class_id;
    uint16_t        word_class_id;
};

#define NO_CLASS_ID 0xFFFFU

struct RE_CLASS_DESC {
    uint16_t begin_idx;
    uint16_t num_ranges;
};

struct RE_CLASS_RANGE {
    uint32_t begin_code;
    uint32_t end_code;
};

struct RE_OBJ {
    struct RE_CLASS_DESC  *class_descs;
    struct RE_CLASS_RANGE *class_data;

    uint16_t num_groups;
    uint16_t num_counts;
    uint16_t num_classes;
    uint16_t bytecode_size;
    uint16_t bytecode[1];
};

KOS_DECLARE_STATIC_CONST_STRING(str_err_not_string, "object is not a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_re,     "object is not a regular expression");
KOS_DECLARE_STATIC_CONST_STRING(str_err_too_long,   "regular expression too long");

/* End of regular expression */
#define END_OF_STR (~0U)

static uint32_t peek_next_char(KOS_STRING_ITER *iter)
{
    if (KOS_is_string_iter_end(iter))
        return END_OF_STR;

    return KOS_string_iter_peek_next_code(iter);
}

static uint32_t peek_prev_char(KOS_STRING_ITER *iter)
{
    KOS_STRING_ITER prev_iter = *iter;

    prev_iter.ptr -= (intptr_t)1 << prev_iter.elem_size;

    return KOS_string_iter_peek_next_code(&prev_iter);
}

static void consume_next_char(struct RE_PARSE_CTX *re_ctx)
{
    KOS_string_iter_advance(&re_ctx->iter);

    ++re_ctx->idx;
}

static int emit_instr(struct RE_PARSE_CTX *re_ctx,
                      enum RE_INSTR        code,
                      int                  num_args,
                      ...)
{
    const size_t pos   = re_ctx->buf.size;
    const int    error = KOS_vector_resize(&re_ctx->buf, pos + 2 + num_args * 2);
    va_list      args;
    int          i;
    uint16_t    *dest;

    if (error)
        return error;

    if (re_ctx->buf.size > 0xFFFFU) {
        KOS_raise_exception(re_ctx->ctx, KOS_CONST_ID(str_err_too_long));
        return KOS_ERROR_EXCEPTION;
    }

    dest      = (uint16_t *)(re_ctx->buf.buffer + pos);
    *(dest++) = (uint8_t)code;

    va_start(args, num_args);

    for (i = 0; i < num_args; i++) {
        const uint32_t arg = (uint32_t)va_arg(args, uint32_t);

        assert(arg <= 0xFFFFU);

        *(dest++) = (uint16_t)arg;
    }

    va_end(args);

    re_ctx->can_be_multiplicity = 1;

    return KOS_SUCCESS;
}

static int emit_instr0(struct RE_PARSE_CTX *re_ctx, enum RE_INSTR code)
{
    return emit_instr(re_ctx, code, 0);
}

static int emit_instr1(struct RE_PARSE_CTX *re_ctx, enum RE_INSTR code, uint32_t arg)
{
    return emit_instr(re_ctx, code, 1, arg);
}

static int emit_instr2(struct RE_PARSE_CTX *re_ctx, enum RE_INSTR code, uint32_t arg1, uint32_t arg2)
{
    return emit_instr(re_ctx, code, 2, arg1, arg2);
}

static int emit_instr3(struct RE_PARSE_CTX *re_ctx, enum RE_INSTR code,
                       uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    return emit_instr(re_ctx, code, 3, arg1, arg2, arg3);
}

static int emit_instr4(struct RE_PARSE_CTX *re_ctx, enum RE_INSTR code,
                       uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    return emit_instr(re_ctx, code, 4, arg1, arg2, arg3, arg4);
}

static void encode_utf8(uint32_t code, char *buf, size_t buf_size)
{
    const unsigned len = KOS_utf8_calc_buf_size_32(&code, 1);

    if (len != ~0U) {
        assert(len < buf_size);
        KOS_utf8_encode_32(&code, 1, (uint8_t *)buf);
        buf[len] = 0;
    }
    else {
        buf[0] = '?';
        buf[1] = 0;
    }
}

static int expect_char(struct RE_PARSE_CTX *re_ctx, char c)
{
    const uint32_t next_char = peek_next_char(&re_ctx->iter);

    if (next_char == END_OF_STR) {
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "expected %c at position %d but reached end of regular expression",
                         c, re_ctx->idx);
        return KOS_ERROR_EXCEPTION;
    }

    if (next_char != (uint32_t)(uint8_t)c) {
        char str_next[6];

        encode_utf8(next_char, &str_next[0], sizeof(str_next));
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "found character %s but expected %c at position %d",
                         str_next, c, re_ctx->idx);
        return KOS_ERROR_EXCEPTION;
    }

    consume_next_char(re_ctx);

    return KOS_SUCCESS;
}

static void rotate_instr(struct RE_PARSE_CTX *re_ctx, uint32_t begin, uint32_t mid)
{
    char        *tmp[5];
    const size_t size1     = mid - begin;
    const size_t size2     = re_ctx->buf.size - mid;
    char  *const begin_ptr = re_ctx->buf.buffer + begin;

    assert(size2 <= sizeof(tmp));

    memcpy(tmp, re_ctx->buf.buffer + mid, size2);

    memmove(begin_ptr + size2, begin_ptr, size1);

    memcpy(begin_ptr, tmp, size2);
}

static void patch_jump_offs(struct RE_PARSE_CTX *re_ctx, uint32_t instr_offs, uint32_t target_offs)
{
    const int32_t delta_offs = (int32_t)target_offs - (int32_t)instr_offs;

    uint16_t *const offs_ptr = (uint16_t *)(re_ctx->buf.buffer + instr_offs + 2);

    assert( ! (delta_offs & 1));

    *offs_ptr = (uint16_t)(int16_t)(delta_offs >> 1);
}

static int parse_alternative_match_seq(struct RE_PARSE_CTX *re_ctx);

static int parse_number(struct RE_PARSE_CTX *re_ctx, uint32_t* number)
{
    uint32_t  value = 0;
    uint32_t  code  = peek_next_char(&re_ctx->iter);
    const int pos   = re_ctx->idx;

    if (code == END_OF_STR) {
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "expected a decimal digit at position %d", pos);
        return KOS_ERROR_EXCEPTION;
    }

    if (code < '0' || code > '9') {
        char str_code[6];

        encode_utf8(code, &str_code[0], sizeof(str_code));
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "found character %s but expected a decimal digit at position %d",
                         str_code, pos);
        return KOS_ERROR_EXCEPTION;
    }

    value = code - '0';

    for (;;) {
        consume_next_char(re_ctx);

        code = peek_next_char(&re_ctx->iter);
        if (code < '0' || code > '9')
            break;

        value = (value * 10U) + (code - '0');

        if (value > 0xFFFFU) {
            KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                             "number at position %d too large",
                             pos);
            return KOS_ERROR_EXCEPTION;
        }
    }

    *number = value;

    return KOS_SUCCESS;
}

/* Escape characters in their natural order, used for parsing:
 * \t : 9
 * \n : 10
 * \v : 11
 * \f : 12
 * \r : 13
 */
static const char esc_whitespace[] = "tnvfr";

static int parse_class_char_escape_seq(struct RE_PARSE_CTX *re_ctx, uint32_t *out_code)
{
    const uint32_t code = peek_next_char(&re_ctx->iter);
    int            error;

    if (code == END_OF_STR) {
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "expected an escape sequence at position %d but reached end of regular expression",
                         re_ctx->idx);
        return KOS_ERROR_EXCEPTION;
    }

    consume_next_char(re_ctx);

    switch (code) {

        case '[':
            /* fall through */
        case ']':
            /* fall through */
        case '(':
            /* fall through */
        case ')':
            /* fall through */
        case '^':
            /* fall through */
        case '"':
            /* fall through */
        case '\\':
            /* fall through */
        case '-':
            *out_code = code;
            error = KOS_SUCCESS;
            break;

        default: {
            const char *const found_esc = (code < 0x7F) ? strchr(esc_whitespace, (char)code) : KOS_NULL;

            if (found_esc) {
                *out_code = 9 + (uint32_t)(found_esc - esc_whitespace);
                error = KOS_SUCCESS;
            }
            else {
                char str_code[6];

                encode_utf8(code, &str_code[0], sizeof(str_code));
                KOS_raise_printf(re_ctx->ctx, "unsupported escape sequence \\%s at position %d",
                                 str_code, re_ctx->idx);
                error = KOS_ERROR_EXCEPTION;
            }
        }
    }

    return error;
}

static int parse_class_char(struct RE_PARSE_CTX *re_ctx, uint32_t *out_code)
{
    const uint32_t code = peek_next_char(&re_ctx->iter);

    if (code == END_OF_STR) {
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "expected a class character at position %d but reached end of regular expression",
                         re_ctx->idx);
        return KOS_ERROR_EXCEPTION;
    }

    consume_next_char(re_ctx);

    if (code == '\\')
        return parse_class_char_escape_seq(re_ctx, out_code);

    *out_code = code;

    return KOS_SUCCESS;
}

static uint16_t generate_class(struct RE_PARSE_CTX *re_ctx)
{
    struct RE_CLASS_DESC *desc;
    uint16_t              class_id;

    const size_t   new_size  = re_ctx->class_descs.size + sizeof(struct RE_CLASS_DESC);
    const uint16_t begin_idx = (uint16_t)(re_ctx->class_data.size / sizeof(struct RE_CLASS_RANGE));
    const int      error     = KOS_vector_resize(&re_ctx->class_descs, new_size);

    if (error) {
        KOS_raise_exception(re_ctx->ctx, KOS_STR_OUT_OF_MEMORY);
        return NO_CLASS_ID;
    }

    class_id = (uint16_t)(re_ctx->class_descs.size / sizeof(struct RE_CLASS_DESC)) - 1;

    desc = (struct RE_CLASS_DESC *)re_ctx->class_descs.buffer + class_id;

    desc->begin_idx  = begin_idx;
    desc->num_ranges = 0;

    return class_id;
}

static int add_class_range(struct RE_PARSE_CTX *re_ctx,
                           uint16_t             class_id,
                           uint32_t             begin_code,
                           uint32_t             end_code)
{
    struct RE_CLASS_DESC  *desc = (struct RE_CLASS_DESC *)re_ctx->class_descs.buffer + class_id;
    struct RE_CLASS_RANGE *range;
    size_t                 begin;
    size_t                 end;
    const size_t           old_size = re_ctx->class_data.size;
    const size_t           new_size = old_size + sizeof(struct RE_CLASS_RANGE);
    const int              error    = KOS_vector_resize(&re_ctx->class_data, new_size);

    assert(class_id == (re_ctx->class_descs.size / sizeof(struct RE_CLASS_DESC)) - 1);

    if (error) {
        KOS_raise_exception(re_ctx->ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    assert(begin_code <= end_code);

    range = (struct RE_CLASS_RANGE *)re_ctx->class_data.buffer + desc->begin_idx;
    begin = 0;
    end   = desc->num_ranges;

    assert((char *)(range + end + 1) == re_ctx->class_data.buffer + re_ctx->class_data.size);

    while (begin < end) {

        const size_t   mid      = (begin + end) / 2;
        const uint32_t mid_code = range[mid].begin_code;

        assert(mid < end);

        if (begin_code == mid_code) {
            begin = mid;
            end   = mid;
            break;
        }

        if (begin_code < mid_code)
            end = mid;
        else
            begin = mid + 1;
    }

    if (begin == desc->num_ranges) {

        range += desc->num_ranges - 1;

        assert( ! desc->num_ranges || (begin_code > range->begin_code));

        if (desc->num_ranges && (begin_code <= range->end_code + 1)) {

            if (end_code > range->end_code)
                range->end_code = end_code;

            return KOS_vector_resize(&re_ctx->class_data, old_size);
        }

        ++range;
        ++desc->num_ranges;

        range->begin_code = begin_code;
        range->end_code   = end_code;

        return KOS_SUCCESS;
    }

    assert(begin_code <= range[begin].begin_code);

    if (begin && (begin_code <= range[begin - 1].end_code + 1))
        range[--begin].end_code = end_code;

    for ( ; end < desc->num_ranges; ++end)
        if (end_code + 1 < range[end].begin_code)
            break;

    if (begin < end) {

        struct RE_CLASS_RANGE *joined_range  = &range[begin];
        const uint32_t         last_end_code = range[end - 1].end_code;
        const size_t           num_to_delete = end - begin - 1;

        if (begin_code < joined_range->begin_code)
            joined_range->begin_code = begin_code;

        if (last_end_code > end_code)
            end_code = last_end_code;

        if (end_code > joined_range->end_code)
            joined_range->end_code = end_code;

        if (num_to_delete && (end < desc->num_ranges))
            memmove((void *)(joined_range + 1),
                    (void *)&range[end],
                    (desc->num_ranges - end) * sizeof(struct RE_CLASS_RANGE));

        assert(num_to_delete < desc->num_ranges);

        desc->num_ranges -= (uint16_t)num_to_delete;

        return KOS_vector_resize(&re_ctx->class_data,
                                 old_size - num_to_delete * sizeof(struct RE_CLASS_RANGE));
    }

    assert(begin == end);
    assert(begin < desc->num_ranges);
    assert(end_code + 1 < range[begin].begin_code);

    range += begin;

    memmove((void *)(range + 1),
            (void *)range,
            (desc->num_ranges - end) * sizeof(struct RE_CLASS_RANGE));

    range->begin_code = begin_code;
    range->end_code   = end_code;

    ++desc->num_ranges;

    return KOS_SUCCESS;
}

static int parse_class(struct RE_PARSE_CTX *re_ctx)
{
    uint32_t       code     = peek_next_char(&re_ctx->iter);
    enum RE_INSTR  instr    = INSTR_MATCH_CLASS;
    const uint16_t class_id = generate_class(re_ctx);

    if (class_id == NO_CLASS_ID)
        return KOS_ERROR_EXCEPTION;

    if (code == '^') {
        consume_next_char(re_ctx);
        instr = INSTR_MATCH_NOT_CLASS;

        code = peek_next_char(&re_ctx->iter);
    }

    do {

        int      error;
        uint32_t end_code;
        int      pos = re_ctx->idx;

        error = parse_class_char(re_ctx, &code);
        if (error)
            return error;

        end_code = peek_next_char(&re_ctx->iter);

        if (end_code == '-') {

            consume_next_char(re_ctx);

            error = parse_class_char(re_ctx, &end_code);
            if (error)
                return error;
        }
        else
            end_code = code;

        if (code > end_code) {
            char str_code1[6];
            char str_code2[6];

            encode_utf8(code, &str_code1[0], sizeof(str_code1));
            encode_utf8(end_code, &str_code2[0], sizeof(str_code2));
            KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                             "invalid character range %s-%s at position %d",
                             str_code1, str_code2, pos);
            return KOS_ERROR_EXCEPTION;
        }

        error = add_class_range(re_ctx, class_id, code, end_code);
        if (error)
            return error;

        code = peek_next_char(&re_ctx->iter);

    } while (code != ']');

    consume_next_char(re_ctx);

    return emit_instr1(re_ctx, instr, class_id);
}

static uint16_t get_digit_class_id(struct RE_PARSE_CTX *re_ctx)
{
    if (re_ctx->digit_class_id == NO_CLASS_ID) {
        const uint16_t class_id = generate_class(re_ctx);

        if ((class_id != NO_CLASS_ID) && ! add_class_range(re_ctx, class_id, '0', '9'))
            re_ctx->digit_class_id = class_id;
    }

    return re_ctx->digit_class_id;
}

static uint16_t get_word_class_id(struct RE_PARSE_CTX *re_ctx)
{
    if (re_ctx->word_class_id == NO_CLASS_ID) {
        const uint16_t class_id = generate_class(re_ctx);

        if ((class_id != NO_CLASS_ID) &&
            ! add_class_range(re_ctx, class_id, 'a', 'z') &&
            ! add_class_range(re_ctx, class_id, '_', '_') &&
            ! add_class_range(re_ctx, class_id, 'A', 'Z') &&
            ! add_class_range(re_ctx, class_id, '0', '9'))
            re_ctx->word_class_id = class_id;
    }

    return re_ctx->word_class_id;
}

static int parse_escape_seq(struct RE_PARSE_CTX *re_ctx)
{
    int            error;
    const uint32_t code = peek_next_char(&re_ctx->iter);

    if (code == END_OF_STR) {
        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                         "expected an escape sequence at position %d but reached end of regular expression",
                         re_ctx->idx);
        return KOS_ERROR_EXCEPTION;
    }

    consume_next_char(re_ctx);

    switch (code) {

        case '.':
            /* fall through */
        case '*':
            /* fall through */
        case '+':
            /* fall through */
        case '?':
            /* fall through */
        case '{':
            /* fall through */
        case '^':
            /* fall through */
        case '$':
            /* fall through */
        case '\\':
            /* fall through */
        case '[':
            /* fall through */
        case ']':
            /* fall through */
        case '|':
            /* fall through */
        case '(':
            /* fall through */
        case ')':
            /* fall through */
        case '"':
            error = emit_instr1(re_ctx, INSTR_MATCH_ONE_CHAR, code);
            break;

        case '<':
            error = emit_instr1(re_ctx, INSTR_MATCH_BOUNDARY, BOUNDARY_WORD_BEGIN);
            break;

        case '>':
            error = emit_instr1(re_ctx, INSTR_MATCH_BOUNDARY, BOUNDARY_WORD_END);
            break;

        case 'b':
            error = emit_instr1(re_ctx, INSTR_MATCH_BOUNDARY, BOUNDARY_WORD);
            break;

        case 'B':
            error = emit_instr1(re_ctx, INSTR_MATCH_BOUNDARY, BOUNDARY_NONE);
            break;

        case 'd': {
            const uint16_t class_id = get_digit_class_id(re_ctx);
            if (class_id != NO_CLASS_ID)
                error = emit_instr1(re_ctx, INSTR_MATCH_CLASS, class_id);
            else
                error = KOS_ERROR_EXCEPTION;
            break;
        }

        case 'D': {
            const uint16_t class_id = get_digit_class_id(re_ctx);
            if (class_id != NO_CLASS_ID)
                error = emit_instr1(re_ctx, INSTR_MATCH_NOT_CLASS, class_id);
            else
                error = KOS_ERROR_EXCEPTION;
            break;
        }

        case 'w': {
            const uint16_t class_id = get_word_class_id(re_ctx);
            if (class_id != NO_CLASS_ID)
                error = emit_instr1(re_ctx, INSTR_MATCH_CLASS, class_id);
            else
                error = KOS_ERROR_EXCEPTION;
            break;
        }

        case 'W': {
            const uint16_t class_id = get_word_class_id(re_ctx);
            if (class_id != NO_CLASS_ID)
                error = emit_instr1(re_ctx, INSTR_MATCH_NOT_CLASS, class_id);
            else
                error = KOS_ERROR_EXCEPTION;
            break;
        }

        default: {
            const char *const found_esc = (code < 0x7F) ? strchr(esc_whitespace, (char)code) : KOS_NULL;

            if (found_esc) {
                const uint32_t actual_code = 9 + (uint32_t)(found_esc - esc_whitespace);

                error = emit_instr1(re_ctx, INSTR_MATCH_ONE_CHAR, actual_code);
            }
            else {
                char str_code[6];

                encode_utf8(code, &str_code[0], sizeof(str_code));
                KOS_raise_printf(re_ctx->ctx, "unsupported escape sequence \\%s at position %d",
                                 str_code, re_ctx->idx);
                error = KOS_ERROR_EXCEPTION;
            }
        }
    }

    return error;
}

static int is_capturing_group(uint16_t group_id)
{
    return group_id < 0x7FFFU;
}

static int parse_group(struct RE_PARSE_CTX *re_ctx)
{
    int      error;
    unsigned group_id   = re_ctx->num_groups++;
    uint32_t group_type = peek_next_char(&re_ctx->iter);

    if (group_type == '?') {
        consume_next_char(re_ctx);

        group_type = peek_next_char(&re_ctx->iter);

        if (group_type == END_OF_STR)
            return expect_char(re_ctx, ')');

        consume_next_char(re_ctx);

        switch (group_type) {

            /* non-capturing group */
            case ':':
                --re_ctx->num_groups;
                group_id = 0xFFFFU;
                break;

            default: {
                char str_code[6];

                encode_utf8(group_type, &str_code[0], sizeof(str_code));
                KOS_raise_printf(re_ctx->ctx, "unsupported group type '%s' at position %d",
                                 str_code, re_ctx->idx);
                return KOS_ERROR_EXCEPTION;
            }
        }
    }

    error = emit_instr1(re_ctx, INSTR_BEGIN_GROUP, group_id);
    if (error)
        return error;

    ++re_ctx->group_depth;

    error = parse_alternative_match_seq(re_ctx);

    --re_ctx->group_depth;

    if ( ! error)
        error = expect_char(re_ctx, ')');

    if ( ! error)
        error = emit_instr1(re_ctx, INSTR_END_GROUP, group_id);

    return error;
}

static int parse_single_match(struct RE_PARSE_CTX *re_ctx)
{
    int      error = KOS_SUCCESS;
    uint32_t code  = peek_next_char(&re_ctx->iter);

    switch (code) {

        case '.':
            consume_next_char(re_ctx);
            error = emit_instr0(re_ctx, INSTR_MATCH_ANY_CHAR);
            break;

        case '^':
            consume_next_char(re_ctx);
            error = emit_instr0(re_ctx, INSTR_MATCH_LINE_BEGIN);
            break;

        case '$':
            consume_next_char(re_ctx);
            error = emit_instr0(re_ctx, INSTR_MATCH_LINE_END);
            break;

        case '\\':
            consume_next_char(re_ctx);
            error = parse_escape_seq(re_ctx);
            break;

        case '[':
            consume_next_char(re_ctx);
            error = parse_class(re_ctx);
            break;

        case '(':
            consume_next_char(re_ctx);
            error = parse_group(re_ctx);
            break;

        case '|':
            /* fall through */
        case '*':
            /* fall through */
        case '+':
            /* fall through */
        case '?':
            /* fall through */
        case '{':
            break;

        case ')':
            if (re_ctx->group_depth)
                break;
            /* fall through */
        default:
            consume_next_char(re_ctx);
            if (code < 0x10000U)
                error = emit_instr1(re_ctx, INSTR_MATCH_ONE_CHAR, (uint16_t)code);
            else
                error = emit_instr2(re_ctx, INSTR_MATCH_ONE_CHAR32, (uint16_t)(code >> 16), (uint16_t)code);
            break;
    }

    return error;
}

static int emit_multiplicity(struct RE_PARSE_CTX *re_ctx,
                             uint32_t             begin_offs,
                             uint32_t             min_count,
                             uint32_t             max_count)
{
    int            error;
    const uint32_t pivot    = (uint32_t)re_ctx->buf.size;
    const int      lazy     = peek_next_char(&re_ctx->iter) == '?';
    const unsigned count_id = re_ctx->num_counts++;
    uint32_t       jump_offs;
    uint32_t       count_size;

    if (lazy)
        consume_next_char(re_ctx);

    TRY(emit_instr3(re_ctx, lazy ? INSTR_LAZY_COUNT : INSTR_GREEDY_COUNT, 0, count_id, min_count));

    count_size = (uint32_t)re_ctx->buf.size - pivot;

    rotate_instr(re_ctx, begin_offs, pivot);

    jump_offs = (uint32_t)re_ctx->buf.size;

    TRY(emit_instr4(re_ctx,
                    lazy ? INSTR_LAZY_JUMP : INSTR_GREEDY_JUMP,
                    0,
                    count_id,
                    min_count,
                    max_count));

    patch_jump_offs(re_ctx, begin_offs, (uint32_t)re_ctx->buf.size);
    patch_jump_offs(re_ctx, jump_offs, begin_offs + count_size);

cleanup:
    return error;
}

static int parse_optional_multiplicity(struct RE_PARSE_CTX *re_ctx, uint32_t begin)
{
    int            error = KOS_SUCCESS;
    const uint32_t code  = peek_next_char(&re_ctx->iter);

    if ((code == '*') || (code == '+') || (code == '?') || (code == '{')) {

        uint32_t min_count = 0U;
        uint32_t max_count = 0U;

        if ( ! re_ctx->can_be_multiplicity) {
            KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                             "found unexpected character %c at position %d",
                             (char)(uint8_t)code, re_ctx->idx);
            return KOS_ERROR_EXCEPTION;
        }

        consume_next_char(re_ctx);

        switch (code) {

            case '*':
                max_count = 0xFFFFU;
                break;

            case '+':
                min_count = 1U;
                max_count = 0xFFFFU;
                break;

            case '?':
                max_count = 1U;
                break;

            default: {
                const int pos = re_ctx->idx;

                assert(code == '{');

                error = parse_number(re_ctx, &min_count);
                if (error)
                    break;

                if (peek_next_char(&re_ctx->iter) == ',') {
                    consume_next_char(re_ctx);

                    if (peek_next_char(&re_ctx->iter) == '}')
                        max_count = 0xFFFFU;
                    else {
                        error = parse_number(re_ctx, &max_count);
                        if (error)
                            break;
                    }

                    if (max_count < min_count) {
                        KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                                         "invalid count range {%u,%u} at position %d",
                                         min_count, max_count, pos);
                        return KOS_ERROR_EXCEPTION;
                    }
                }
                else
                    max_count = min_count;

                if ( ! max_count) {
                    KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                                     "invalid count %u at position %d", max_count, pos);
                    return KOS_ERROR_EXCEPTION;
                }

                error = expect_char(re_ctx, '}');
                break;
            }
        }

        if ( ! error)
            error = emit_multiplicity(re_ctx, begin, min_count, max_count);

        re_ctx->can_be_multiplicity = 0;
    }

    return error;
}

static int parse_match_seq(struct RE_PARSE_CTX *re_ctx)
{
    int error = KOS_SUCCESS;

    for (;;) {
        const uint32_t begin = (uint32_t)re_ctx->buf.size;
        const uint32_t code  = peek_next_char(&re_ctx->iter);
        if (code == END_OF_STR || code == '|' || code == ')')
            break;

        error = parse_single_match(re_ctx);
        if (error)
            break;

        error = parse_optional_multiplicity(re_ctx, begin);
        if (error)
            break;
    }

    return error;
}

static int parse_alternative_match_seq(struct RE_PARSE_CTX *re_ctx)
{
    uint32_t fork_offs = (uint32_t)re_ctx->buf.size;
    uint32_t jump_offs = ~0U;
    int      error;
    uint32_t code;

    re_ctx->can_be_multiplicity = 0;

    TRY(parse_match_seq(re_ctx));

    while ((code = peek_next_char(&re_ctx->iter)) == '|') {

        const uint32_t pivot = (uint32_t)re_ctx->buf.size;

        consume_next_char(re_ctx);

        TRY(emit_instr1(re_ctx, INSTR_FORK, 0));

        rotate_instr(re_ctx, fork_offs, pivot);

        if (jump_offs != ~0U)
            patch_jump_offs(re_ctx, jump_offs, (uint32_t)re_ctx->buf.size);

        jump_offs = (uint32_t)re_ctx->buf.size;

        TRY(emit_instr1(re_ctx, INSTR_JUMP, 0));

        patch_jump_offs(re_ctx, fork_offs, (uint32_t)re_ctx->buf.size);

        fork_offs = (uint32_t)re_ctx->buf.size;

        re_ctx->can_be_multiplicity = 0;

        TRY(parse_match_seq(re_ctx));
    }

    if (jump_offs != ~0U)
        patch_jump_offs(re_ctx, jump_offs, (uint32_t)re_ctx->buf.size);

    if (code != ')' || ! re_ctx->group_depth) {
        if (code != END_OF_STR) {
            char str_code[6];

            encode_utf8(code, &str_code[0], sizeof(str_code));
            KOS_raise_printf(re_ctx->ctx, "error parsing regular expression: "
                             "found unexpected character %s at position %d",
                             str_code, re_ctx->idx);
            return KOS_ERROR_EXCEPTION;
        }
    }

cleanup:
    return error;
}

static void disassemble(struct RE_OBJ *re, const char *re_cstr)
{
    const uint16_t       *ptr = re->bytecode;
    const uint16_t *const end = ptr + re->bytecode_size;
    char                  buf[79];
#define MNEMONIC_SIZE 24
    char                 *bytes = &buf[MNEMONIC_SIZE];

    assert(MNEMONIC_SIZE < sizeof(buf));

    memset(buf, '=', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    printf("\n%s\nDisassembling regular expression: %s\n%s\n\n", buf, re_cstr, buf);

    while (ptr < end) {
        const uintptr_t            offs            = (uintptr_t)ptr - (uintptr_t)re->bytecode;
        const uint16_t             instr           = *(ptr++);
        const RE_INSTR_DESC *const desc            = &re_instr_descs[instr];
        const uint16_t      *const instr_end       = ptr + desc->num_args;
        char                      *mnem_buf        = &buf[0];
        char                      *bytes_buf       = bytes;
#ifndef NDEBUG
        char                *const mnem_buf_end    = mnem_buf + MNEMONIC_SIZE;
        char                *const bytes_buf_end   = bytes_buf + (sizeof(buf) - MNEMONIC_SIZE);
#endif
        int                        i_arg           = 0;
        size_t                     num_printed;

        num_printed = snprintf(bytes_buf, sizeof(buf) - MNEMONIC_SIZE, " %04X", instr);

        assert(num_printed < sizeof(buf) - MNEMONIC_SIZE);
        bytes_buf += num_printed;

        for (; ptr < instr_end; i_arg++) {
            uint16_t operand = *(ptr++);
            char     pr_buf[11];

            num_printed = snprintf(pr_buf, sizeof(pr_buf), " %04X", operand);
            assert(bytes_buf + num_printed < bytes_buf_end);

            memcpy(bytes_buf, pr_buf, num_printed);
            bytes_buf += num_printed;

            if ( ! i_arg && desc->first_arg_is_offs) {
                const unsigned target = (unsigned)offs + ((int)(int16_t)operand * 2);

                assert(target <= re->bytecode_size * 2U);
                if (target == re->bytecode_size * 2U) {
                    static const char end_str[] = "END";
                    memcpy(pr_buf, end_str, sizeof(end_str) - 1);
                    num_printed = sizeof(end_str) - 1;
                }
                else
                    num_printed = snprintf(pr_buf, sizeof(pr_buf), "%08X", target);
            }
            else if ( ! i_arg && instr == INSTR_MATCH_ONE_CHAR && (operand >= 0x20 && operand < 0x7F))
                num_printed = snprintf(pr_buf, sizeof(pr_buf), "'%c'", (char)operand);
            else
                num_printed = snprintf(pr_buf, sizeof(pr_buf), "%u", operand);

            assert(mnem_buf + num_printed < mnem_buf_end);
            memcpy(mnem_buf, pr_buf, num_printed);
            mnem_buf += num_printed;

            if (ptr < instr_end) {
                assert(mnem_buf + 2 <= mnem_buf_end);
                mnem_buf[0] = ',';
                mnem_buf[1] = ' ';
                mnem_buf += 2;
            }
        }

        assert(bytes_buf < bytes_buf_end);
        *bytes_buf = 0;
        assert(mnem_buf < mnem_buf_end);
        *mnem_buf = 0;

        printf("%08X:%-25s %-17s%s\n", (unsigned)offs, bytes, desc->str_instr, buf);
    }

    fflush(stdout);
}

static void finalize(KOS_CONTEXT ctx, void *priv)
{
    if (priv)
        KOS_free(priv);
}

KOS_DECLARE_PRIVATE_CLASS(regex_priv_class);

static int parse_re(KOS_CONTEXT ctx, KOS_OBJ_ID regex_str, KOS_OBJ_ID regex)
{
    int                 error;
    struct RE_PARSE_CTX re_ctx;
    struct RE_OBJ      *re;
    uint32_t            hdr_and_bytecode_size;
    uint32_t            class_descs_size;
    uint32_t            class_data_size;

    KOS_init_string_iter(&re_ctx.iter, regex_str);

    KOS_vector_init(&re_ctx.buf);
    KOS_vector_init(&re_ctx.class_descs);
    KOS_vector_init(&re_ctx.class_data);

    TRY(KOS_vector_reserve(&re_ctx.buf, KOS_get_string_length(regex_str) * 2U));

    re_ctx.ctx                 = ctx;
    re_ctx.idx                 = 1;
    re_ctx.can_be_multiplicity = 0;
    re_ctx.num_groups          = 0U;
    re_ctx.group_depth         = 0U;
    re_ctx.num_counts          = 0U;
    re_ctx.digit_class_id      = NO_CLASS_ID;
    re_ctx.word_class_id       = NO_CLASS_ID;

    TRY(parse_alternative_match_seq(&re_ctx));

    hdr_and_bytecode_size = (uint32_t)KOS_align_up(sizeof(struct RE_OBJ) + re_ctx.buf.size - sizeof(uint16_t), sizeof(uint32_t));
    class_descs_size      = (uint32_t)KOS_align_up(re_ctx.class_descs.size, sizeof(uint32_t));
    class_data_size       = (uint32_t)KOS_align_up(re_ctx.class_data.size, sizeof(uint32_t));

    re = (struct RE_OBJ *)KOS_malloc(hdr_and_bytecode_size + class_descs_size + class_data_size);
    if ( ! re) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    re->class_descs   = KOS_NULL;
    re->class_data    = KOS_NULL;
    re->num_groups    = (uint16_t)re_ctx.num_groups;
    re->num_counts    = (uint16_t)re_ctx.num_counts;
    re->num_classes   = (class_descs_size && class_data_size) ? (uint16_t)(class_descs_size / sizeof(struct RE_CLASS_DESC)) : 0;
    re->bytecode_size = (uint16_t)(re_ctx.buf.size / sizeof(uint16_t));
    memcpy(re->bytecode, re_ctx.buf.buffer, re_ctx.buf.size);

    if (class_descs_size && class_data_size) {
        re->class_descs = (struct RE_CLASS_DESC *)((uint8_t *)re + hdr_and_bytecode_size);
        re->class_data  = (struct RE_CLASS_RANGE *)((uint8_t *)re + hdr_and_bytecode_size + class_descs_size);
        memcpy(re->class_descs, re_ctx.class_descs.buffer, re_ctx.class_descs.size);
        memcpy(re->class_data,  re_ctx.class_data.buffer,  re_ctx.class_data.size);
    }

    if (ctx->inst->flags & KOS_INST_DISASM) {
        const char *re_cstr = "?";

        re_ctx.buf.size = 0;
        if ( ! KOS_string_to_cstr_vec(ctx, regex_str, &re_ctx.buf))
            re_cstr = re_ctx.buf.buffer;

        disassemble(re, re_cstr);
    }

    KOS_object_set_private_ptr(regex, re);

cleanup:
    KOS_vector_destroy(&re_ctx.class_data);
    KOS_vector_destroy(&re_ctx.class_descs);
    KOS_vector_destroy(&re_ctx.buf);

    return error;
}

struct RE_POSS_STACK_ITEM {
    uint16_t instr_idx;
    uint16_t str_end_offs; /* char idx in the string, from the end of the string */
    uint16_t counts_and_groups[1];
};

struct RE_POSS_STACK {
    KOS_VECTOR                 buffer;
    struct RE_POSS_STACK_ITEM *current;
};

static int get_num_slots(const struct RE_OBJ *re)
{
    return (re->num_groups * 2) + re->num_counts;
}

static size_t get_item_size(const struct RE_OBJ *re)
{
    const size_t num_slots = get_num_slots(re);
    return sizeof(struct RE_POSS_STACK_ITEM) + sizeof(uint16_t) * (num_slots - 1);
}

static struct RE_POSS_STACK_ITEM *push_item(struct RE_POSS_STACK *poss_stack,
                                            KOS_CONTEXT           ctx,
                                            size_t                item_size)
{
    const size_t old_size = poss_stack->buffer.size;

    if (KOS_vector_resize(&poss_stack->buffer, old_size + item_size)) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_NULL;
    }

    poss_stack->current = (struct RE_POSS_STACK_ITEM *)poss_stack->buffer.buffer;

    return (struct RE_POSS_STACK_ITEM *)(poss_stack->buffer.buffer + old_size);
}

static void init_possibility_stack(struct RE_POSS_STACK *poss_stack)
{
    KOS_vector_init(&poss_stack->buffer);
}

static int reset_possibility_stack(struct RE_POSS_STACK *poss_stack,
                                   KOS_CONTEXT           ctx,
                                   const struct RE_OBJ  *re)
{
    const size_t item_size  = get_item_size(re);
    const size_t group_size = re->num_groups * 2 * sizeof(uint16_t);

    poss_stack->buffer.size = 0;
    poss_stack->current     = KOS_NULL;

    if ( ! push_item(poss_stack, ctx, item_size))
        return KOS_ERROR_EXCEPTION;

    memset(poss_stack->current, 0, item_size - group_size);

    if (group_size) {
        memset(&poss_stack->current->counts_and_groups[re->num_counts], 0xFFU, group_size);

        if (re->num_counts) {
            assert(poss_stack->current->counts_and_groups[0] == 0);
            assert(poss_stack->current->counts_and_groups[re->num_counts - 1] == 0);
        }
        assert(poss_stack->current->counts_and_groups[re->num_counts] == 0xFFFFU);
        assert(poss_stack->current->counts_and_groups[re->num_counts + re->num_groups * 2 - 1] == 0xFFFFU);
    }

    return KOS_SUCCESS;
}

static void destroy_possibility_stack(struct RE_POSS_STACK *poss_stack)
{
    KOS_vector_destroy(&poss_stack->buffer);
}

static int push_possibility(struct RE_POSS_STACK *poss_stack,
                            KOS_CONTEXT           ctx,
                            const struct RE_OBJ  *re,
                            const uint16_t       *target_ptr,
                            KOS_STRING_ITER      *iter)
{
    struct RE_POSS_STACK_ITEM *saved_item;
    const size_t               item_size = get_item_size(re);

    assert(poss_stack->current);
    assert(poss_stack->buffer.size);

    saved_item = push_item(poss_stack, ctx, item_size);
    if ( ! saved_item)
        return KOS_ERROR_EXCEPTION;

    memcpy(saved_item, poss_stack->current, item_size);

    saved_item->instr_idx    = (uint16_t)(target_ptr - &re->bytecode[0]);
    saved_item->str_end_offs = (uint16_t)((iter->end - iter->ptr) >> iter->elem_size);

    return KOS_SUCCESS;
}

static void pop_possibility(struct RE_POSS_STACK *poss_stack,
                            const struct RE_OBJ  *re)
{
    struct RE_POSS_STACK_ITEM *saved_item;
    const size_t               item_size = get_item_size(re);

    assert(poss_stack->buffer.size >= item_size);

    saved_item = (struct RE_POSS_STACK_ITEM *)(poss_stack->buffer.buffer + poss_stack->buffer.size - item_size);

    if (saved_item > poss_stack->current) {

        memcpy(poss_stack->current, saved_item, item_size);

        poss_stack->buffer.size -= item_size;
    }
    else {
        poss_stack->buffer.size = 0;
        poss_stack->current     = KOS_NULL;
    }
}

static int match_class(uint32_t             code,
                       uint16_t             class_id,
                       const struct RE_OBJ *re)
{
    struct RE_CLASS_DESC   class_desc;
    struct RE_CLASS_RANGE *range;
    uint32_t               begin = 0;
    uint32_t               end;

    assert(class_id < re->num_classes);

    class_desc = re->class_descs[class_id];

    range = re->class_data + class_desc.begin_idx;
    end   = class_desc.num_ranges;

    assert(end);

    do {
        const uint32_t mid = (begin + end) / 2;

        assert(mid < end);

        if (code < range[mid].begin_code)
            end = mid;
        else if (code > range[mid].end_code)
            begin = mid + 1;
        else
            return 1;
    } while (begin < end);

    return 0;
}

static int create_found_groups(KOS_CONTEXT                      ctx,
                               KOS_OBJ_ID                       groups_obj,
                               KOS_OBJ_ID                       match_groups_obj,
                               KOS_OBJ_ID                       str_obj,
                               const struct RE_OBJ             *re,
                               const struct RE_POSS_STACK_ITEM *found)
{
    KOS_LOCAL       groups;
    KOS_LOCAL       match_groups;
    KOS_LOCAL       str;
    KOS_LOCAL       group;
    const uint16_t *group_ptr;
    const uint16_t *end_ptr;
    unsigned        i     = 0;
    int             error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &groups, groups_obj);
    KOS_init_local_with(ctx, &match_groups, match_groups_obj);
    KOS_init_local_with(ctx, &str, str_obj);
    KOS_init_local(ctx, &group);

    group_ptr = &found->counts_and_groups[re->num_counts];
    end_ptr   = group_ptr + 2 * re->num_groups;

    for ( ; group_ptr < end_ptr; group_ptr += 2, ++i) {

        const int begin = (int)(unsigned int)group_ptr[0];
        const int end   = (int)(unsigned int)group_ptr[1];

        group.o = KOS_VOID;

        if ((begin != 0xFFFFU) && (end != 0xFFFFU)) {

            KOS_OBJ_ID match_obj;

            group.o = KOS_new_object(ctx);
            TRY_OBJID(group.o);

            TRY(KOS_array_write(ctx, groups.o, i, group.o));

            TRY(KOS_set_property(ctx, group.o, KOS_CONST_ID(str_begin), TO_SMALL_INT(begin)));
            TRY(KOS_set_property(ctx, group.o, KOS_CONST_ID(str_end),   TO_SMALL_INT(end)));

            match_obj = KOS_string_slice(ctx, str.o, begin, end);
            TRY_OBJID(match_obj);

            TRY(KOS_array_write(ctx, match_groups.o, i, match_obj));
        }
    }

cleanup:
    KOS_destroy_top_locals(ctx, &group, &groups);

    return error;
}

static uint16_t get_iter_pos(KOS_OBJ_ID str_obj, KOS_STRING_ITER *iter)
{
    KOS_STRING_ITER iter0;
    uintptr_t       pos;

    KOS_init_string_iter(&iter0, str_obj);

    pos = ((uintptr_t)iter->ptr - (uintptr_t)iter0.ptr) >> iter0.elem_size;

    return (uint16_t)pos;
}

static uint16_t *get_group(struct RE_POSS_STACK *poss_stack, uint16_t num_counts, uint16_t group_id)
{
    return &poss_stack->current->counts_and_groups[num_counts + group_id * 2];
}

static int is_word_char(uint32_t code)
{
    return ((code >= 'A') && (code <= 'Z')) ||
           ((code >= 'a') && (code <= 'z')) ||
           ((code >= '0') && (code <= '9')) ||
           (code == '_');
}

#define BEGIN_INSTRUCTION(instr) case INSTR_ ## instr
#define NEXT_INSTRUCTION         KOS_INSTR_FUZZ_LIMIT(); break

static KOS_OBJ_ID match_string(KOS_CONTEXT           ctx,
                               const struct RE_OBJ  *re,
                               KOS_OBJ_ID            str_obj,
                               uint32_t              begin_pos,
                               uint32_t              pos,
                               uint32_t              end_pos,
                               struct RE_POSS_STACK *poss_stack)
{
    const uint16_t       *bytecode     = &re->bytecode[0];
    const uint16_t *const bytecode_end = bytecode + re->bytecode_size;
    KOS_OBJ_ID            retval       = KOS_VOID;
    KOS_LOCAL             ret;
    KOS_LOCAL             str;
    KOS_LOCAL             match_groups;
    KOS_LOCAL             groups;
    KOS_STRING_ITER       iter;
    int                   error        = KOS_SUCCESS;

    KOS_init_locals(ctx, &groups, &match_groups, &str, &ret, kos_end_locals);
    TRY(reset_possibility_stack(poss_stack, ctx, re));

    str.o = str_obj;

    KOS_init_string_iter(&iter, str.o);
    iter.end = iter.ptr + ((uintptr_t)end_pos << iter.elem_size);
    iter.ptr += (uintptr_t)pos << iter.elem_size;

    while (bytecode < bytecode_end) {
        const enum RE_INSTR instr = (enum RE_INSTR)*bytecode;

        assert(bytecode + re_instr_descs[instr].num_args < bytecode_end);

        switch (instr) {

            BEGIN_INSTRUCTION(MATCH_ONE_CHAR): {
                const uint16_t expected_code = bytecode[1];
                const uint32_t actual_code   = peek_next_char(&iter);

                if (expected_code != actual_code)
                    goto try_other_possibility;

                KOS_string_iter_advance(&iter);
                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_ONE_CHAR32): {
                const uint32_t expected_code = ((uint32_t)bytecode[1] << 16) | bytecode[2];
                const uint32_t actual_code   = peek_next_char(&iter);

                if (expected_code != actual_code)
                    goto try_other_possibility;

                KOS_string_iter_advance(&iter);
                bytecode += 3;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_ANY_CHAR): {
                const uint32_t actual_code = peek_next_char(&iter);

                if (actual_code == END_OF_STR)
                    goto try_other_possibility;

                KOS_string_iter_advance(&iter);
                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_CLASS): {
                const uint16_t class_id = bytecode[1];
                const uint32_t code     = peek_next_char(&iter);

                if (code == END_OF_STR)
                    goto try_other_possibility;

                if ( ! match_class(code, class_id, re))
                    goto try_other_possibility;

                KOS_string_iter_advance(&iter);
                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_NOT_CLASS): {
                const uint16_t class_id = bytecode[1];
                const uint32_t code     = peek_next_char(&iter);

                if (code == END_OF_STR)
                    goto try_other_possibility;

                if (match_class(code, class_id, re))
                    goto try_other_possibility;

                KOS_string_iter_advance(&iter);
                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_LINE_BEGIN): {
                KOS_STRING_ITER iter0;

                KOS_init_string_iter(&iter0, str.o);
                iter0.ptr += (uintptr_t)begin_pos << iter0.elem_size;

                if (iter.ptr > iter0.ptr) {
                    const uint32_t prev_code = peek_prev_char(&iter);
                    if ((prev_code != '\r') && (prev_code != '\n'))
                        goto try_other_possibility;
                }

                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_LINE_END): {
                const uint32_t cur_code = peek_next_char(&iter);

                if ((cur_code != END_OF_STR) && (cur_code != '\r') && (cur_code != '\n'))
                    goto try_other_possibility;

                bytecode += 1;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(MATCH_BOUNDARY): {
                const uint16_t  boundary  = bytecode[1];
                uint16_t        cur_state = 0;
                uint32_t        cur_code;
                uint32_t        prev_code;
                KOS_STRING_ITER iter0;

                KOS_init_string_iter(&iter0, str.o);

                prev_code = (iter.ptr > iter0.ptr) ? peek_prev_char(&iter) : ' ';
                cur_code  = peek_next_char(&iter);

                if (is_word_char(prev_code)) {
                    cur_state = (uint16_t)(is_word_char(cur_code) ? BOUNDARY_NONE : BOUNDARY_WORD_END);
                }
                else {
                    cur_state = (uint16_t)(is_word_char(cur_code) ? BOUNDARY_WORD_BEGIN : BOUNDARY_NONE);
                }

                if ( ! (boundary & cur_state))
                    goto try_other_possibility;

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(BEGIN_GROUP): {
                const uint16_t group_id = bytecode[1];

                if (is_capturing_group(group_id))
                    get_group(poss_stack, re->num_counts, group_id)[0] = get_iter_pos(str.o, &iter);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(END_GROUP): {
                const uint16_t group_id = bytecode[1];

                if (is_capturing_group(group_id))
                    get_group(poss_stack, re->num_counts, group_id)[1] = get_iter_pos(str.o, &iter);

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(FORK): {
                const int16_t delta = (int16_t)bytecode[1];

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                TRY(push_possibility(poss_stack, ctx, re, bytecode + delta, &iter));

                bytecode += 2;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(JUMP): {
                const int16_t delta = (int16_t)bytecode[1];

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                bytecode += delta;
                assert(bytecode <= bytecode_end);
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GREEDY_COUNT): {
                const int16_t  delta     = (int16_t)bytecode[1];
                const uint16_t count_id  = bytecode[2];
                const uint16_t min_count = bytecode[3];

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                poss_stack->current->counts_and_groups[count_id] = 0;

                if (min_count == 0)
                    TRY(push_possibility(poss_stack, ctx, re, bytecode + delta, &iter));

                bytecode += 4;
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LAZY_COUNT): {
                const int16_t  delta     = (int16_t)bytecode[1];
                const uint16_t count_id  = bytecode[2];
                const uint16_t min_count = bytecode[3];

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                poss_stack->current->counts_and_groups[count_id] = 0;

                if (min_count)
                    bytecode += 4;
                else {
                    TRY(push_possibility(poss_stack, ctx, re, bytecode + 4, &iter));

                    bytecode += delta;
                }
                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(GREEDY_JUMP): {
                const int16_t  delta     = (int16_t)bytecode[1];
                const uint16_t count_id  = bytecode[2];
                const uint16_t min_count = bytecode[3];
                const uint16_t max_count = bytecode[4];
                unsigned       count;

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                count = ++poss_stack->current->counts_and_groups[count_id];

                if (count < min_count)
                    bytecode += delta;
                else if (count < max_count) {
                    TRY(push_possibility(poss_stack, ctx, re, bytecode + 5, &iter));
                    bytecode += delta;
                }
                else
                    bytecode += 5;

                NEXT_INSTRUCTION;
            }

            BEGIN_INSTRUCTION(LAZY_JUMP): {
                const int16_t  delta     = (int16_t)bytecode[1];
                const uint16_t count_id  = bytecode[2];
                const uint16_t min_count = bytecode[3];
                const uint16_t max_count = bytecode[4];
                unsigned       count;

                assert(delta);
                assert(poss_stack->current);

                if ( ! poss_stack->current)
                    RAISE_ERROR(KOS_ERROR_INTERNAL);

                count = ++poss_stack->current->counts_and_groups[count_id];

                if (count < min_count)
                    bytecode += delta;
                else {
                    if (count < max_count)
                        TRY(push_possibility(poss_stack, ctx, re, bytecode + delta, &iter));
                    bytecode += 5;
                }

                NEXT_INSTRUCTION;
            }

            default:
                KOS_raise_printf(ctx, "unknown instruction 0x%x\n", (unsigned)instr);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);

            try_other_possibility: {
                assert(poss_stack->current);

                pop_possibility(poss_stack, re);

                if ( ! poss_stack->current)
                    goto cleanup;

                bytecode = &re->bytecode[poss_stack->current->instr_idx];
                iter.ptr = iter.end - ((unsigned)poss_stack->current->str_end_offs << iter.elem_size);
            }
        }
    }

    end_pos -= (uint32_t)((iter.end - iter.ptr) >> iter.elem_size);

    ret.o = KOS_new_object(ctx);
    TRY_OBJID(ret.o);

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_string), str.o));

    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_begin), TO_SMALL_INT(pos)));
    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_end),   TO_SMALL_INT(end_pos)));

    {
        const KOS_OBJ_ID match_obj = KOS_string_slice(ctx, str.o, pos, end_pos);
        TRY_OBJID(match_obj);
        TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_match), match_obj));
    }

    groups.o = KOS_new_array(ctx, re->num_groups);
    TRY_OBJID(groups.o);
    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_groups), groups.o));

    match_groups.o = KOS_new_array(ctx, re->num_groups);
    TRY_OBJID(match_groups.o);
    TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_match_groups), match_groups.o));

    assert(poss_stack->current);
    if (re->num_groups)
        TRY(create_found_groups(ctx, groups.o, match_groups.o, str.o, re, poss_stack->current));

    retval = ret.o;

cleanup:
    KOS_destroy_top_locals(ctx, &groups, &ret);

    return error ? KOS_BADPTR : retval;
}

/* @item re re()
 *
 *     re(regex)
 *
 * Regular expression class.
 *
 * `regex` is a string containing a regular expression.
 *
 * Stores regular expressions in a cache, so subsequent invocations with the
 * same regular expression string just take the precompiled regular expression
 * from the cache instead of recompiling it every single time.
 *
 * For the uncached version, use `re_uncached`.
 *
 * Example:
 *
 *     > re("...")
 */
static const KOS_CONVERT re_uncached_args[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_regex),
    KOS_DEFINE_TAIL_ARG()
};

static KOS_OBJ_ID re_ctor(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL regex_str;
    KOS_LOCAL regex;

    assert(KOS_get_array_size(args_obj) >= 1);

    KOS_init_locals(ctx, &regex_str, &regex, kos_end_locals);

    regex_str.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(regex_str.o);

    if (GET_OBJ_TYPE(regex_str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_regex_not_a_string);

    regex.o = KOS_new_object_with_private(ctx, this_obj, &regex_priv_class, finalize);
    TRY_OBJID(regex.o);

    TRY(parse_re(ctx, regex_str.o, regex.o));

    TRY(KOS_set_property(ctx, regex.o, KOS_CONST_ID(str_string), regex_str.o));

cleanup:
    regex.o = KOS_destroy_top_locals(ctx, &regex_str, &regex);

    return error ? KOS_BADPTR : regex.o;
}

/* @item re re.prototype.find()
 *
 *     re.prototype.find(string, begin = 0, end = void)
 *
 * Finds the first location in the `string` which matches the regular
 * expression object.
 *
 * `string` is a string which matched against the regular expression
 * object.
 *
 * `begin` is the starting position for the search.  `begin` defaults to `0`.
 * `begin` also matches against `^`.
 *
 * `end` is the ending position for the search, the regular expression
 * will not be matched any characters at or after `end`.  `end`
 * defaults to `void`, which indicates the end of the string.  `end`
 * also matches against `$`.
 *
 * Returns a match object if a match was found or `void` if no match was
 * found.
 *
 * Example:
 *
 *     > re(r"down.*(rabbit)").find("tumbling down the rabbit hole")
 */
static KOS_OBJ_ID re_find(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int                  error = KOS_SUCCESS;
    int                  begin_pos;
    int                  end_pos;
    int                  pos;
    KOS_LOCAL            str;
    KOS_LOCAL            match;
    struct RE_POSS_STACK poss_stack;
    struct RE_OBJ       *re;

    assert(KOS_get_array_size(args_obj) >= 3);

    init_possibility_stack(&poss_stack);

    KOS_init_local(ctx, &match);

    KOS_init_local_with(ctx, &str, KOS_array_read(ctx, args_obj, 0));
    TRY_OBJID(str.o);

    if (GET_OBJ_TYPE(str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_not_string);

    end_pos = (int)KOS_get_string_length(str.o);

    re = (struct RE_OBJ *)KOS_object_get_private(this_obj, &regex_priv_class);
    if ( ! re)
        RAISE_EXCEPTION_STR(str_err_not_re);

    TRY(KOS_get_index_arg(ctx, args_obj, 1, 0,         end_pos, KOS_VOID_INDEX_IS_BEGIN, &begin_pos));
    TRY(KOS_get_index_arg(ctx, args_obj, 2, begin_pos, end_pos, KOS_VOID_INDEX_IS_END,   &end_pos));

    for (pos = begin_pos; pos <= end_pos; pos++) {
        /* TODO optimize the case when the re begins with ^: don't look beyond begin_pos */
        match.o = match_string(ctx, re, str.o, (uint32_t)begin_pos, (uint32_t)pos, (uint32_t)end_pos, &poss_stack);
        if (match.o != KOS_VOID)
            break;
    }

cleanup:
    match.o = KOS_destroy_top_locals(ctx, &str, &match);
    destroy_possibility_stack(&poss_stack);

    return error ? KOS_BADPTR : match.o;
}

KOS_INIT_MODULE(re, KOS_MODULE_NEEDS_KOS_SOURCE)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL proto;

    const KOS_CONVERT find_args[4] = {
        KOS_DEFINE_MANDATORY_ARG(str_string                  ),
        KOS_DEFINE_OPTIONAL_ARG( str_begin,  TO_SMALL_INT(0) ),
        KOS_DEFINE_OPTIONAL_ARG( str_end,    KOS_VOID        ),
        KOS_DEFINE_TAIL_ARG()
    };

    KOS_init_debug_output();

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &proto);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,          "re_uncached", re_ctor, re_uncached_args, &proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, proto.o, "find",        re_find, find_args);

cleanup:
    KOS_destroy_top_locals(ctx, &proto, &module);

    return error;
}

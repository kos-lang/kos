/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"

KOS_DECLARE_STATIC_CONST_STRING(str_err_regex_not_a_string, "regular expression is not a string");

/*
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
 * EscapeSequence ::= "\\" ( "\\" | "<" | ">" | "A" | "b" | "B" | "d" | "D" | "s" | "S" | "w" | "W" | "Z" | Digit )
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
 * GroupOpt ::= TODO
 *
 *
 * Instructions
 * ------------
 *
 * - group
 *      BEGIN_GROUP <group.no> - save string index in group slot
 *      END_GROUP <group.no> - save string index in group slot
 * - alternative
 *      FORK <jump.offs> - push current string index and jump offset, used to retry if matching fails
 *          -- TODO how to undo group matches during back tracking?
 *      JUMP <jump.offs>
 * - count
 *      BEGIN_COUNT <jump.offs> <min> <max> <lazy|greedy>
 *          - if greedy (default), at every iteration, push the skip jump on the stack
 *            and try to match the inside of the loop
 *          - if lazy, at every iteration, push the inside of the loop on the stack
 *            and execute the jump skipping the loop
 *
 *    +---------+
 *    v         |
 *  --+--> a -> + -+->
 *    |            ^
 *    +------------+
 */

enum RE_TOKEN {
    /* Any non-special character */
    T_SINGLE,

    /* Characters with special meaning */
    T_ANY,              /* .  One (any) character           */
    T_ZERO_OR_MORE,     /* *  Zero or more of the preceding */
    T_ONE_OR_MORE,      /* +  One or more of the preceding  */
    T_OPTION,           /* ?  Option                        */
    T_COUNT_OPEN,       /* {  Open count of the preceding   */
    T_LINE_BEGIN,       /* ^  Line begin                    */
    T_LINE_END,         /* $  Line end                      */
    T_ESCAPE,           /* \  Escape                        */
    T_CLASS_OPEN,       /* [  Class open                    */
    T_ALTERNATIVE,      /* |  Alternative                   */
    T_GROUP_BEGIN,      /* (  Group begin                   */

    /* Characters which only have a special meaning in some context */
    T_COUNT_CLOSE,      /* }  Close count                               */
    T_COUNT_RANGE,      /* ,  Count range                               */
    T_CLASS_CLOSE,      /* ]  Class close                               */
    T_CLASS_NEG,        /* ^  Class negation (only first char in class) */
    T_CLASS_RANGE,      /* -  Class range                               */
    T_GROUP_END,        /* )  Group end                                 */

    /* Escape sequences */
    T_WORD_BEGIN,       /* <  Word begin                                */
    T_WORD_END,         /* >  Word end                                  */
    T_STRING_BEGIN,     /* A  Start of string                           */
    T_WORD_TRANSITION,  /* b  Word transition (begin or end)            */
    T_INSIDE_WORD,      /* B  Empty string inside a word                */
    T_DIGIT,            /* d  Any digit (flag selects Unicode or ASCII), for ASCII: [0-9] */
    T_NON_DIGIT,        /* D  Not digit                                 */
    T_WHITESPACE,       /* s  Any whitespace character (flag selects Unicode or ASCII), for ASCII: [ \t\n\r\f\v] */
    T_NON_WHITESPACE,   /* S  Any non-whitespace character              */
    T_WORD,             /* w  Any word character (flag selects Unicode or ASCII), for ASCII: [a-zA-Z0-9_] */
    T_NON_WORD,         /* W  Any non-word character                    */
    T_STRING_END        /* Z  End of string                             */
};

enum RE_FLAG {
    F_ASCII       = 1, /* a -  ASCII - matches ASCII, otherwise matches Unicode */
    F_IGNORE_CASE = 2, /* i -  ignore case - case-insensitive matching */
    F_MULTI_LINE  = 4, /* m -  multi-line - ^/$ match lines, not just beginning/end of string */
    T_DOT_ALL     = 8  /* s -  dotall - dot matches any char, including \n (without this flag dot does not match \n) */
};

enum RE_INSTR {
    INSTR_MATCH_ONE_CHAR,
    INSTR_MATCH_ANY_CHAR,
    INSTR_MATCH_LINE_BEGIN,
    INSTR_MATCH_LINE_END,
    INSTR_BEGIN_GROUP,
    INSTR_END_GROUP,
    INSTR_FORK,
    INSTR_JUMP,
    INSTR_GREEDY_COUNT,
    INSTR_LAZY_COUNT
};

struct RE_CTX {
    KOS_STRING_ITER iter;
    int             idx;
    unsigned        num_groups;
    unsigned        group_depth;
    uint32_t        re_size;
    KOS_VECTOR      buf;
};

/* End of regular expression */
#define EORE (~0U)

static uint32_t peek_next_char(struct RE_CTX *re_ctx)
{
    uint32_t code;

    if (kos_is_string_iter_end(&re_ctx->iter))
        return EORE;

    code = kos_string_iter_peek_next_code(&re_ctx->iter);

    ++re_ctx->idx;

    return code;
}

static void consume_next_char(struct RE_CTX *re_ctx)
{
    kos_string_iter_advance(&re_ctx->iter);
}

#if 0
static int gen_instr(struct RE_CTX *re_ctx, enum RE_TOKEN token, uint32_t code)
{
    const size_t  offs = re_ctx->buf.size;
    const uint8_t size = (code < 0x100U)   ? 1U :
                         (code < 0x10000U) ? 2U :
                                             4U;

    int error = kos_vector_resize(&re_ctx->buf, offs + 1U + size);

    if ( ! error) {

        char *buf = re_ctx->buf.buffer + offs;

        *(buf++) = (char)((uint8_t)token | ((size << 5) & 0xC0U));

        if (code >= 0x10000U) {
            *(buf++) = (uint8_t)(code >> 24);
            *(buf++) = (uint8_t)(code >> 16);
        }

        if (code >= 0x100U)
            *(buf++) = (uint8_t)(code >> 8);

        *(buf++) = code & 0xFFU;
    }

    return error;
}
#endif

static int emit_instr1(struct RE_CTX *re_ctx, uint8_t code)
{
    /* TODO */
    return KOS_SUCCESS;
}

static int emit_instr2(struct RE_CTX *re_ctx, uint8_t code, uint32_t arg)
{
    /* TODO */
    return KOS_SUCCESS;
}

static int emit_instr4(struct RE_CTX *re_ctx, uint8_t code, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    /* TODO */
    return KOS_SUCCESS;
}

static int expect_char(struct RE_CTX *re_ctx, char c)
{
    if (peek_next_char(re_ctx) != (uint32_t)(uint8_t)c)
        return KOS_ERROR_INTERNAL; /* TODO actual error */

    consume_next_char(re_ctx);

    return KOS_SUCCESS;
}

static void rotate_instr(struct RE_CTX *re_ctx, uint32_t begin, uint32_t mid)
{
    /* TODO */
}

static void patch_jump_offs(struct RE_CTX *re_ctx, uint32_t instr_offs, uint32_t target_offs)
{
    /* TODO */
}

static int parse_alternative_match_seq(struct RE_CTX *re_ctx);

static int parse_number(struct RE_CTX *re_ctx, uint32_t* number)
{
    uint32_t value = 0;
    uint32_t code  = peek_next_char(re_ctx);

    if (code < '0' || code > '9')
        return KOS_ERROR_INTERNAL; /* TODO actual error */

    value = code - '0';

    for (;;) {
        uint64_t new_value;

        consume_next_char(re_ctx);

        code = peek_next_char(re_ctx);
        if (code < '0' || code > '9')
            break;

        new_value = ((uint64_t)value * 10U) + (code - '0');

        value = (uint32_t)new_value;

        if (value != new_value)
            return KOS_ERROR_INTERNAL; /* TODO actual error */
    }

    return KOS_SUCCESS;
}

static int parse_escape_seq(struct RE_CTX *re_ctx)
{
    /* TODO */
    return KOS_ERROR_INTERNAL;
}

static int parse_char_class(struct RE_CTX *re_ctx)
{
    /* TODO */
    return KOS_ERROR_INTERNAL;
}

static int parse_group(struct RE_CTX *re_ctx)
{
    int      error;
    unsigned group_id = re_ctx->num_groups++;

    error = emit_instr2(re_ctx, INSTR_BEGIN_GROUP, group_id);
    if (error)
        return error;

    ++re_ctx->group_depth;

    error = parse_alternative_match_seq(re_ctx);

    --re_ctx->group_depth;

    if ( ! error)
        error = expect_char(re_ctx, ')');

    if ( ! error)
        error = emit_instr2(re_ctx, INSTR_END_GROUP, group_id);

    return error;
}

static int parse_single_match(struct RE_CTX *re_ctx)
{
    int      error = KOS_SUCCESS;
    uint32_t code  = peek_next_char(re_ctx);

    switch (code) {

        case '.':
            consume_next_char(re_ctx);
            error = emit_instr1(re_ctx, INSTR_MATCH_ANY_CHAR);
            break;

        case '^':
            consume_next_char(re_ctx);
            error = emit_instr1(re_ctx, INSTR_MATCH_LINE_BEGIN);
            break;

        case '$':
            consume_next_char(re_ctx);
            error = emit_instr1(re_ctx, INSTR_MATCH_LINE_END);
            break;

        case '\\':
            consume_next_char(re_ctx);
            error = parse_escape_seq(re_ctx);
            break;

        case '[':
            consume_next_char(re_ctx);
            error = parse_char_class(re_ctx);
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
            error = emit_instr2(re_ctx, INSTR_MATCH_ONE_CHAR, code);
            break;
    }

    return error;
}

static int emit_multiplicity(struct RE_CTX *re_ctx,
                             uint32_t       begin,
                             uint32_t       min_count,
                             uint32_t       max_count)
{
    int            error;
    enum RE_INSTR  instr = INSTR_GREEDY_COUNT;
    const uint32_t pivot = re_ctx->re_size;

    if (peek_next_char(re_ctx) == '?') {
        consume_next_char(re_ctx);
        instr = INSTR_LAZY_COUNT;
    }

    TRY(emit_instr4(re_ctx, instr, pivot - begin, min_count, max_count));

    rotate_instr(re_ctx, begin, pivot);

cleanup:
    return error;
}

static int parse_optional_multiplicity(struct RE_CTX *re_ctx, uint32_t begin)
{
    int            error = KOS_SUCCESS;
    const uint32_t code  = peek_next_char(re_ctx);

    switch (code) {

        case '*':
            consume_next_char(re_ctx);
            error = emit_multiplicity(re_ctx, begin, 0U, ~0U);
            break;

        case '+':
            consume_next_char(re_ctx);
            error = emit_multiplicity(re_ctx, begin, 1U, ~0U);
            break;

        case '?':
            consume_next_char(re_ctx);
            error = emit_multiplicity(re_ctx, begin, 0U, 1U);
            break;

        case '{':
            consume_next_char(re_ctx);
            {
                uint32_t min_count = 0U;
                uint32_t max_count = 0U;

                error = parse_number(re_ctx, &min_count);
                if (error)
                    break;

                if (peek_next_char(re_ctx) == ',') {
                    consume_next_char(re_ctx);

                    error = parse_number(re_ctx, &max_count);
                    if (error)
                        break;
                }
                else
                    max_count = min_count;

                error = expect_char(re_ctx, '}');
                if (error)
                    break;

                error = emit_multiplicity(re_ctx, begin, min_count, max_count);
            }
            break;

        default:
            break;
    }

    return error;
}

static int parse_match_seq(struct RE_CTX *re_ctx)
{
    int error = KOS_SUCCESS;

    for (;;) {
        const uint32_t begin = re_ctx->re_size;
        const uint32_t code  = peek_next_char(re_ctx);
        if (code == EORE || code == '|')
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

static int parse_alternative_match_seq(struct RE_CTX *re_ctx)
{
    uint32_t fork_offs = re_ctx->re_size;
    uint32_t jump_offs = ~0U;
    int      error;
    uint32_t code;

    TRY(parse_match_seq(re_ctx));

    while ((code = peek_next_char(re_ctx)) == '|') {

        const uint32_t pivot = re_ctx->re_size;

        TRY(emit_instr2(re_ctx, INSTR_FORK, 0));

        rotate_instr(re_ctx, fork_offs, pivot);

        if (jump_offs != ~0U)
            patch_jump_offs(re_ctx, jump_offs, re_ctx->re_size);

        jump_offs = re_ctx->re_size;

        TRY(emit_instr2(re_ctx, INSTR_JUMP, 0));

        patch_jump_offs(re_ctx, fork_offs, re_ctx->re_size);

        fork_offs = re_ctx->re_size;

        TRY(parse_match_seq(re_ctx));
    }

    if (jump_offs != ~0U)
        patch_jump_offs(re_ctx, jump_offs, re_ctx->re_size);

    if (code != ')' || ! re_ctx->group_depth) {
        if (code != EORE)
            error = KOS_ERROR_INTERNAL; /* TODO actual error */
    }

cleanup:
    return error;
}

static int parse_re(KOS_OBJ_ID regex_str)
{
    int           error;
    struct RE_CTX re_ctx;

    kos_init_string_iter(&re_ctx.iter, regex_str);

    kos_vector_init(&re_ctx.buf);

    TRY(kos_vector_reserve(&re_ctx.buf, KOS_get_string_length(regex_str) * 2U));

    re_ctx.idx         = -1;
    re_ctx.num_groups  = 0U;
    re_ctx.group_depth = 0U;
    re_ctx.re_size     = 0U;

    error = parse_alternative_match_seq(&re_ctx);

cleanup:
    kos_vector_destroy(&re_ctx.buf);

    return error;
}

/* @item re re()
 *
 *     re(regex)
 *
 * Regular expression class.
 *
 * `regex` is a string containing a regular expression.
 *
 * Example:
 *
 *     > re("...")
 */
static KOS_OBJ_ID re_ctor(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL regex_str;

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_init_locals(ctx, 1, &regex_str);

    regex_str.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(regex_str.o);

    if (GET_OBJ_TYPE(regex_str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_regex_not_a_string);

    error = parse_re(regex_str.o);

cleanup:
    KOS_destroy_top_locals(ctx, &regex_str, &regex_str);

    return error ? KOS_BADPTR : KOS_VOID;
}

int kos_module_re_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL proto;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &proto);

    TRY_ADD_CONSTRUCTOR(ctx, module.o, "re", re_ctor, 1, &proto.o);

cleanup:
    KOS_destroy_top_locals(ctx, &proto, &module);

    return error;
}

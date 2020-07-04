/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_malloc.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_err_regex_not_a_string, "regular expression is not a string");

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
 *      (GREEDY|LAZY)_COUNT <jump.offs> <min> <max> <lazy|greedy>
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

enum RE_FLAG {
    F_ASCII       = 1, /* a -  ASCII - matches ASCII, otherwise matches Unicode */
    F_IGNORE_CASE = 2, /* i -  ignore case - case-insensitive matching */
    F_MULTI_LINE  = 4, /* m -  multi-line - ^/$ match lines, not just beginning/end of string */
    T_DOT_ALL     = 8  /* s -  dotall - dot matches any char, including \n (without this flag dot does not match \n) */
};

enum RE_INSTR {
    INSTR_MATCH_ONE_CHAR,   /* MATCH.ONE.CHAR <code>           */
    INSTR_MATCH_ANY_CHAR,   /* MATCH.ANY.CHAR                  */
    INSTR_MATCH_CLASS,      /* MATCH.CLASS <class_id>          */
    INSTR_MATCH_NOT_CLASS,  /* MATCH.NOT.CLASS <class_id>      */
    INSTR_MATCH_LINE_BEGIN, /* MATCH.LINE.BEGIN                */
    INSTR_MATCH_LINE_END,   /* MATCH.LINE.END                  */
    INSTR_BEGIN_GROUP,      /* BEGIN.GROUP <group_id>          */
    INSTR_END_GROUP,        /* END.GROUP <group_id>            */
    INSTR_FORK,             /* FORK <offs>                     */
    INSTR_JUMP,             /* JUMP <offs>                     */
    INSTR_GREEDY_COUNT,     /* GREEDY.COUNT <offs> <min> <max> */
    INSTR_LAZY_COUNT        /* LAZY.COUNT <offs> <min> <max>   */
};

struct RE_INSTR_DESC_S {
    const char *str_instr;
    unsigned    num_args          : 2;
    unsigned    first_arg_is_offs : 1;
};

typedef struct RE_INSTR_DESC_S RE_INSTR_DESC;

static const RE_INSTR_DESC re_instr_descs[] = {
    { "MATCH.ONE.CHAR",   1, 0 },
    { "MATCH.ANY.CHAR",   0, 0 },
    { "MATCH.CLASS",      1, 0 },
    { "MATCH.NOT.CLASS",  1, 0 },
    { "MATCH.LINE.BEGIN", 0, 0 },
    { "MATCH.LINE.END",   0, 0 },
    { "BEGIN.GROUP",      1, 0 },
    { "END.GROUP",        1, 0 },
    { "FORK",             1, 1 },
    { "JUMP",             1, 1 },
    { "GREEDY.COUNT",     3, 1 },
    { "LAZY.COUNT",       3, 1 }
};

struct RE_CTX {
    KOS_STRING_ITER iter;
    int             idx;
    unsigned        num_groups;
    unsigned        group_depth;
    KOS_VECTOR      buf;
};

struct RE {
    uint16_t num_groups;
    uint16_t bytecode_size;
    uint16_t bytecode[1];
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

static int emit_instr(struct RE_CTX *re_ctx,
                      enum RE_INSTR  code,
                      int            num_args,
                      ...)
{
    const size_t pos   = re_ctx->buf.size;
    const int    error = kos_vector_resize(&re_ctx->buf, pos + 2 + num_args * 2);
    va_list      args;
    int          i;
    uint16_t    *dest;

    if (error)
        return error;

    if (re_ctx->buf.size > 0xFFFFU)
        return KOS_ERROR_INTERNAL; /* TODO actual error */

    dest      = (uint16_t *)(re_ctx->buf.buffer + pos);
    *(dest++) = (uint8_t)code;

    va_start(args, num_args);

    for (i = 0; i < num_args; i++) {
        const uint32_t arg = (uint32_t)va_arg(args, uint32_t);

        assert((arg <= 0x7FFFU) || (arg >= 0xFFFF8000U));

        *(dest++) = (uint16_t)arg;
    }

    va_end(args);

    return KOS_SUCCESS;
}

static int emit_instr0(struct RE_CTX *re_ctx, enum RE_INSTR code)
{
    return emit_instr(re_ctx, code, 0);
}

static int emit_instr1(struct RE_CTX *re_ctx, enum RE_INSTR code, uint32_t arg)
{
    return emit_instr(re_ctx, code, 1, arg);
}

static int emit_instr3(struct RE_CTX *re_ctx, enum RE_INSTR code, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    return emit_instr(re_ctx, code, 3, arg1, arg2, arg3);
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
    char        *tmp[5];
    const size_t size1     = mid - begin;
    const size_t size2     = re_ctx->buf.size - mid;
    char  *const begin_ptr = re_ctx->buf.buffer + begin;

    assert(size2 <= sizeof(tmp));

    memcpy(tmp, re_ctx->buf.buffer + mid, size2);

    memmove(begin_ptr + size2, begin_ptr, size1);

    memcpy(begin_ptr, tmp, size2);
}

static void patch_jump_offs(struct RE_CTX *re_ctx, uint32_t instr_offs, uint32_t target_offs)
{
    const uint32_t delta_offs = target_offs - instr_offs;

    uint16_t *const offs_ptr = (uint16_t *)(re_ctx->buf.buffer + instr_offs + 2);

    *offs_ptr = (uint16_t)delta_offs;
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

static int parse_single_match(struct RE_CTX *re_ctx)
{
    int      error = KOS_SUCCESS;
    uint32_t code  = peek_next_char(re_ctx);

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
            error = emit_instr1(re_ctx, INSTR_MATCH_ONE_CHAR, code);
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
    const uint32_t pivot = (uint32_t)re_ctx->buf.size;

    if (peek_next_char(re_ctx) == '?') {
        consume_next_char(re_ctx);
        instr = INSTR_LAZY_COUNT;
    }

    TRY(emit_instr3(re_ctx, instr, 0, min_count, max_count));

    rotate_instr(re_ctx, begin, pivot);

    patch_jump_offs(re_ctx, begin, (uint32_t)re_ctx->buf.size);

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
        const uint32_t begin = (uint32_t)re_ctx->buf.size;
        const uint32_t code  = peek_next_char(re_ctx);
        if (code == EORE || code == '|' || code == ')')
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
    uint32_t fork_offs = (uint32_t)re_ctx->buf.size;
    uint32_t jump_offs = ~0U;
    int      error;
    uint32_t code;

    TRY(parse_match_seq(re_ctx));

    while ((code = peek_next_char(re_ctx)) == '|') {

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

        TRY(parse_match_seq(re_ctx));
    }

    if (jump_offs != ~0U)
        patch_jump_offs(re_ctx, jump_offs, (uint32_t)re_ctx->buf.size);

    if (code != ')' || ! re_ctx->group_depth) {
        if (code != EORE)
            error = KOS_ERROR_INTERNAL; /* TODO actual error */
    }

cleanup:
    return error;
}

static void disassemble(struct RE *re, const char *re_cstr)
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
        size_t                     mnem_remaining  = MNEMONIC_SIZE;
        char                      *bytes_buf       = bytes;
        size_t                     bytes_remaining = sizeof(buf) - MNEMONIC_SIZE;
        int                        i_arg           = 0;
        size_t                     num_printed;

        num_printed = snprintf(bytes_buf, bytes_remaining, " %04X", instr);

        assert(num_printed < bytes_remaining);
        bytes_buf       += num_printed;
        bytes_remaining -= num_printed;

        for (; ptr < instr_end; i_arg++) {
            uint16_t operand = *(ptr++);

            num_printed = snprintf(bytes_buf, bytes_remaining, " %04X", operand);

            assert(num_printed < bytes_remaining);
            bytes_buf       += num_printed;
            bytes_remaining -= num_printed;

            if ( ! i_arg && desc->first_arg_is_offs) {
                const unsigned target = (unsigned)offs + operand;

                assert(target <= re->bytecode_size * 2U);
                if (target == re->bytecode_size * 2U)
                    num_printed = snprintf(mnem_buf, mnem_remaining, "END");
                else
                    num_printed = snprintf(mnem_buf, mnem_remaining, "%08X", (unsigned)offs + operand);
            }
            else
                num_printed = snprintf(mnem_buf, mnem_remaining, "%u", operand);

            assert(num_printed <= mnem_remaining);
            mnem_buf       += num_printed;
            mnem_remaining -= num_printed;

            if (ptr < instr_end) {
                assert(mnem_remaining >= 2);
                mnem_buf[0] = ',';
                mnem_buf[1] = ' ';
                mnem_buf       += 2;
                mnem_remaining -= 2;
            }
        }

        assert(bytes_remaining >= 1);
        *bytes_buf = 0;
        assert(mnem_remaining >= 1);
        *mnem_buf = 0;

        printf("%08X:%-20s %-17s%s\n", (unsigned)offs, bytes, desc->str_instr, buf);
    }
}

static void finalize(KOS_CONTEXT ctx, void *priv)
{
    if (priv)
        kos_free(priv);
}

static int parse_re(KOS_CONTEXT ctx, KOS_OBJ_ID regex_str, KOS_OBJ_ID regex)
{
    int           error;
    struct RE_CTX re_ctx;
    struct RE    *re;

    kos_init_string_iter(&re_ctx.iter, regex_str);

    kos_vector_init(&re_ctx.buf);

    /* TODO cache */

    TRY(kos_vector_reserve(&re_ctx.buf, KOS_get_string_length(regex_str) * 2U));

    re_ctx.idx         = -1;
    re_ctx.num_groups  = 0U;
    re_ctx.group_depth = 0U;

    TRY(parse_alternative_match_seq(&re_ctx));

    re = (struct RE *)kos_malloc(sizeof(struct RE) + re_ctx.buf.size - sizeof(uint16_t));
    if ( ! re) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    re->num_groups    = (uint16_t)re_ctx.num_groups;
    re->bytecode_size = (uint16_t)(re_ctx.buf.size / sizeof(uint16_t));
    memcpy(re->bytecode, re_ctx.buf.buffer, re_ctx.buf.size);

    if (ctx->inst->flags & KOS_INST_DISASM) {
        const char *re_cstr = "?";

        re_ctx.buf.size = 0;
        if ( ! KOS_string_to_cstr_vec(ctx, regex_str, &re_ctx.buf))
            re_cstr = re_ctx.buf.buffer;

        disassemble(re, re_cstr);
    }

    OBJPTR(OBJECT, regex)->finalize = finalize;

    KOS_object_set_private_ptr(regex, re);

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
    KOS_LOCAL regex;

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_init_locals(ctx, 2, &regex_str, &regex);

    regex_str.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(regex_str.o);

    if (GET_OBJ_TYPE(regex_str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_regex_not_a_string);

    regex.o = KOS_new_object_with_prototype(ctx, this_obj);
    TRY_OBJID(regex.o);

    TRY(parse_re(ctx, regex_str.o, regex.o));

cleanup:
    regex.o = KOS_destroy_top_locals(ctx, &regex_str, &regex);

    return error ? KOS_BADPTR : regex.o;
}

/* @item re re.prototype.search()
 *
 *     re.prototype.search(string, pos=0, end_pos=void)
 *
 * Finds the first location in the `string` which matches the regular
 * expression object.
 *
 * `string` is a string which matched against the regular expression
 * object.
 *
 * `pos` is the starting position for the search.  `pos` defaults to `0`.
 * `pos` also matches against `^`.
 *
 * `end_pos` is the ending position for the search, the regular expression
 * will not be matched any characters at or after `end_pos`.  `end_pos`
 * defaults to `void`, which indicates the end of the string.  `end_pos`
 * also matches against `$`.
 *
 * Returns a match object if a match was found or `void` if no match was
 * found.
 *
 * Example:
 *
 *     > re(r"down.*(rabbit)").search("tumbling down the rabbit hole")
 */
static KOS_OBJ_ID re_search(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  this_obj,
                            KOS_OBJ_ID  args_obj)
{
    return KOS_VOID;
}

int kos_module_re_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL proto;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &proto);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,          "re",     re_ctor,   1, &proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, proto.o, "search", re_search, 1);

cleanup:
    KOS_destroy_top_locals(ctx, &proto, &module);

    return error;
}

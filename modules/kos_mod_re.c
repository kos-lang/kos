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

#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"

KOS_DECLARE_STATIC_CONST_STRING(str_err_regex_not_a_string, "regular expression is not a string");

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

struct RE_SCAN_CTX {
    KOS_STRING_ITER iter;
    int             idx;
};

/* End of regular expression */
#define EORE (~0U)

uint32_t get_next_char(struct RE_SCAN_CTX *scan_ctx)
{
    uint32_t code;

    if (kos_is_string_iter_end(&scan_ctx->iter))
        return EORE;

    code = kos_string_iter_next_code(&scan_ctx->iter);

    ++scan_ctx->idx;

    return code;
}

static int gen_instr(enum RE_TOKEN token, uint32_t code)
{
    return KOS_ERROR_INTERNAL;
}

static int parse_class(struct RE_SCAN_CTX *scan_ctx)
{
    return KOS_ERROR_INTERNAL;
}

static int parse_escape(struct RE_SCAN_CTX *scan_ctx)
{
    return KOS_ERROR_INTERNAL;
}

static int parse_group(struct RE_SCAN_CTX *scan_ctx, uint32_t group_start_code)
{
    int error = KOS_SUCCESS;

    if (group_start_code == '(') {
        /* TODO parse group type */
    }

    for (;;) {

        const uint32_t code = get_next_char(scan_ctx);

        switch (code) {

            case EORE:
                if (group_start_code != 0) {
                    /* TODO error */
                }
                goto cleanup;

            case '.':
                gen_instr(T_ANY, 0);
                break;

            case '*':
                /* TODO */
                break;

            case '+':
                /* TODO */
                break;

            case '?':
                /* TODO */
                break;

            case '{':
                /* TODO */
                break;

            case '^':
                gen_instr(T_LINE_BEGIN, 0);
                break;

            case '$':
                gen_instr(T_LINE_END, 0);
                break;

            case '|':
                /* TODO */
                break;

            case '[':
                TRY(parse_class(scan_ctx));
                break;

            case '\\':
                TRY(parse_escape(scan_ctx));
                break;

            case '(':
                TRY(parse_group(scan_ctx, '('));
                break;

            case ')':
                if (group_start_code != '(') {
                    /* TODO error */
                }
                /* TODO */
                return KOS_SUCCESS;

            default:
                gen_instr(T_SINGLE, code);
                break;
        }
    }

cleanup:
    return error;
}

static int parse_re(KOS_OBJ_ID regex_str)
{
    struct RE_SCAN_CTX scan_ctx;

    kos_init_string_iter(&scan_ctx.iter, regex_str);

    scan_ctx.idx = -1;

    return parse_group(&scan_ctx, 0);
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

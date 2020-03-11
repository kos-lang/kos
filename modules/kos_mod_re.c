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
    T_NONE,

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
    int             error = KOS_SUCCESS;
    KOS_LOCAL       regex_str;
    KOS_STRING_ITER str_iter;

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_init_locals(ctx, 1, &regex_str);

    regex_str.o = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(regex_str.o);

    if (GET_OBJ_TYPE(regex_str.o) != OBJ_STRING)
        RAISE_EXCEPTION_STR(str_err_regex_not_a_string);

    kos_init_string_iter(&str_iter, regex_str.o);

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

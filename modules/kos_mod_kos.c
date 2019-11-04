/*
 * Copyright (c) 2014-2019 Chris Dragan
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
#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_heap.h"
#include "../core/kos_lexer.h"
#include "../core/kos_malloc.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"
#include <string.h>

static const char str_column[]             = "column";
static const char str_err_not_buffer[]     = "object is not a buffer";
static const char str_err_not_paren[]      = "previous token was not ')'";
static const char str_err_invalid_arg[]    = "invalid argument";
static const char str_format_colon[]       = ":";
static const char str_format_colon_space[] = ": ";
static const char str_format_parse_error[] = "parse error ";
static const char str_keyword[]            = "keyword";
static const char str_line[]               = "line";
static const char str_op[]                 = "op";
static const char str_sep[]                = "sep";
static const char str_token[]              = "token";
static const char str_type[]               = "type";

typedef struct KOS_LEXER_OBJ_S {
    KOS_LEXER lexer;
    KOS_TOKEN token;
    int       ignore_errors;
    char      buf[1];
} KOS_LEXER_OBJ;

typedef enum IDS_E {
    CONST_STR_TOKEN,
    CONST_STR_LINE,
    CONST_STR_COLUMN,
    CONST_STR_TYPE,
    CONST_STR_KEYWORD,
    CONST_STR_OP,
    CONST_STR_SEP,
    CONST_STR_NUM_IDS
} IDS;

static void finalize(KOS_CONTEXT ctx,
                     void       *priv)
{
    if (priv)
        kos_free(priv);
}

static KOS_OBJ_ID raw_lexer(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  regs_obj,
                            KOS_OBJ_ID  args_obj)
{
    int                 error        = KOS_SUCCESS;
    KOS_OBJ_ID          retval       = KOS_BADPTR;
    KOS_OBJ_ID          lexer_obj_id = KOS_BADPTR;
    KOS_OBJ_ID          init_arg     = KOS_BADPTR;
    KOS_OBJ_ID          token_obj    = KOS_BADPTR;
    KOS_OBJ_ID          value        = KOS_BADPTR;
    KOS_OBJ_ID          ids          = KOS_BADPTR;
    KOS_LEXER_OBJ      *lexer;
    KOS_NEXT_TOKEN_MODE next_token   = NT_ANY;

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);

    {
        int pushed = 0;
        TRY(KOS_push_locals(ctx, &pushed, 7,
                            &regs_obj, &args_obj, &lexer_obj_id, &init_arg, &token_obj, &value, &ids));
    }

    lexer_obj_id = KOS_array_read(ctx, regs_obj, 0);
    assert( ! IS_BAD_PTR(lexer_obj_id));
    TRY_OBJID(lexer_obj_id);

    /* TODO improve passing args for the first time to built-in generators */

    /* Instantiate the lexer on first invocation */
    if (GET_OBJ_TYPE(lexer_obj_id) != OBJ_OBJECT ||
        ! KOS_object_get_private_ptr(lexer_obj_id)) {

        uint32_t buf_size;

        init_arg = lexer_obj_id;

        /* TODO support OBJ_STRING as argument */

        if (GET_OBJ_TYPE(init_arg) != OBJ_BUFFER)
            RAISE_EXCEPTION(str_err_not_buffer);

        buf_size = KOS_get_buffer_size(init_arg);

        lexer_obj_id = KOS_new_object(ctx);
        TRY_OBJID(lexer_obj_id);

        lexer = (KOS_LEXER_OBJ *)kos_malloc(sizeof(KOS_LEXER_OBJ) + buf_size - 1);
        if ( ! lexer) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            goto cleanup;
        }

        OBJPTR(OBJECT, lexer_obj_id)->finalize = finalize;

        KOS_object_set_private_ptr(lexer_obj_id, lexer);

        memcpy(&lexer->buf[0], KOS_buffer_data_volatile(init_arg), buf_size);

        kos_lexer_init(&lexer->lexer, 0, &lexer->buf[0], &lexer->buf[buf_size]);

        TRY(KOS_array_resize(ctx, regs_obj, 2));

        TRY(KOS_array_write(ctx, regs_obj, 0, lexer_obj_id));

        lexer->ignore_errors = 0;

        if (KOS_get_array_size(regs_obj) > 1) {

            const KOS_OBJ_ID arg = KOS_array_read(ctx, regs_obj, 1);
            TRY_OBJID(arg);

            lexer->ignore_errors = kos_is_truthy(arg);
        }

        ids = KOS_new_array(ctx, CONST_STR_NUM_IDS);
        TRY_OBJID(ids);

        token_obj = KOS_new_const_ascii_string(ctx, str_token,   sizeof(str_token)   - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_TOKEN,   token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_line,    sizeof(str_line)    - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_LINE,    token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_column,  sizeof(str_column)  - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_COLUMN,  token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_type,    sizeof(str_type)    - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_TYPE,    token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_keyword, sizeof(str_keyword) - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_KEYWORD, token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_op,      sizeof(str_op)      - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_OP,      token_obj));
        token_obj = KOS_new_const_ascii_string(ctx, str_sep,     sizeof(str_sep)     - 1);
        TRY_OBJID(token_obj);
        TRY(KOS_array_write(ctx, ids, CONST_STR_SEP,     token_obj));

        TRY(KOS_array_write(ctx, regs_obj, 1, ids));
    }
    else {
        assert(GET_OBJ_TYPE(lexer_obj_id) == OBJ_OBJECT);

        lexer = (KOS_LEXER_OBJ *)KOS_object_get_private_ptr(lexer_obj_id);

        if (KOS_get_array_size(args_obj) > 0) {

            int64_t    i_value;
            KOS_OBJ_ID arg = KOS_array_read(ctx, args_obj, 0);

            TRY_OBJID(arg);

            TRY(KOS_get_integer(ctx, arg, &i_value));

            if (i_value != 0 && i_value != 1)
                RAISE_EXCEPTION(str_err_invalid_arg);

            if (i_value) {
                next_token = NT_CONTINUE_STRING;

                if (lexer->token.sep != ST_PAREN_CLOSE)
                    RAISE_EXCEPTION(str_err_not_paren);

                kos_lexer_unget_token(&lexer->lexer, &lexer->token);
            }
        }

        ids = KOS_array_read(ctx, regs_obj, 1);
        TRY_OBJID(ids);
    }

    assert( ! lexer->lexer.error_str);

    error = kos_lexer_next_token(&lexer->lexer, next_token, &lexer->token);

    if (error) {
        if (lexer->ignore_errors) {

            KOS_TOKEN *token = &lexer->token;

            lexer->lexer.error_str = 0;

            token->type    = TT_WHITESPACE;
            token->keyword = KW_NONE;
            token->op      = OT_NONE;
            token->sep     = ST_NONE;

            lexer->lexer.pos.column += token->length;
        }
        else {
            KOS_OBJ_ID parts[6] = { KOS_BADPTR, KOS_BADPTR, KOS_BADPTR,
                                    KOS_BADPTR, KOS_BADPTR, KOS_BADPTR };
            KOS_OBJ_ID exception;
            int        pushed = 0;

            assert(error == KOS_ERROR_SCANNING_FAILED);

            TRY(KOS_push_locals(ctx, &pushed, 6,
                                &parts[0], &parts[1], &parts[2],
                                &parts[3], &parts[4], &parts[5]));

            parts[0] = KOS_new_const_ascii_string(ctx, str_format_parse_error,
                                                  sizeof(str_format_parse_error) - 1);
            TRY_OBJID(parts[0]);
            parts[1] = KOS_object_to_string(ctx, TO_SMALL_INT((int)lexer->lexer.pos.line));
            TRY_OBJID(parts[1]);
            parts[2] = KOS_new_const_ascii_string(ctx, str_format_colon,
                                                  sizeof(str_format_colon) - 1);
            TRY_OBJID(parts[2]);
            parts[3] = KOS_object_to_string(ctx, TO_SMALL_INT((int)lexer->lexer.pos.column));
            TRY_OBJID(parts[3]);
            parts[4] = KOS_new_const_ascii_string(ctx, str_format_colon_space,
                                                  sizeof(str_format_colon_space) - 1);
            TRY_OBJID(parts[4]);
            parts[5] = KOS_new_cstring(ctx, lexer->lexer.error_str);
            TRY_OBJID(parts[5]);

            exception = KOS_string_add_n(ctx, parts, sizeof(parts)/sizeof(parts[0]));
            TRY_OBJID(exception);

            KOS_raise_exception(ctx, exception);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    if (lexer->token.type != TT_EOF) {

        KOS_TOKEN *token = &lexer->token;
        KOS_OBJ_ID key;

        token_obj = KOS_new_object(ctx);
        TRY_OBJID(token_obj);

        value = KOS_new_string(ctx, token->begin, token->length);
        TRY_OBJID(value);

        key = KOS_array_read(ctx, ids, CONST_STR_TOKEN);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             value));

        key = KOS_array_read(ctx, ids, CONST_STR_LINE);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->pos.line)));

        key = KOS_array_read(ctx, ids, CONST_STR_COLUMN);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->pos.column)));

        key = KOS_array_read(ctx, ids, CONST_STR_TYPE);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->type)));

        key = KOS_array_read(ctx, ids, CONST_STR_KEYWORD);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->keyword)));

        key = KOS_array_read(ctx, ids, CONST_STR_OP);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->op)));

        key = KOS_array_read(ctx, ids, CONST_STR_SEP);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token_obj,
                             key,
                             TO_SMALL_INT((int)token->sep)));

        if (lexer->token.type == TT_STRING || lexer->token.type == TT_STRING_OPEN) {
            /* TODO parse string, raw/non-raw */
        }

        retval = token_obj;
    }

cleanup:
    if (error)
        retval = KOS_BADPTR;

    return retval;
}

int kos_module_kos_init(KOS_CONTEXT ctx, KOS_OBJ_ID module)
{
    int error  = KOS_SUCCESS;
    int pushed = 0;

    TRY(KOS_push_locals(ctx, &pushed, 1, &module));

    TRY_ADD_GENERATOR(       ctx, module, "raw_lexer", raw_lexer, 1);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "version_major",        KOS_VERSION_MAJOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "version_minor",        KOS_VERSION_MINOR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_whitespace",     TT_WHITESPACE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_eol",            TT_EOL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_comment",        TT_COMMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_eof",            TT_EOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_identifier",     TT_IDENTIFIER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_keyword",        TT_KEYWORD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_numeric",        TT_NUMERIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_string",         TT_STRING);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_string_open",    TT_STRING_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_operator",       TT_OPERATOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "token_separator",      TT_SEPARATOR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_none",         KW_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_line",         KW_LINE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_assert",       KW_ASSERT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_async",        KW_ASYNC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_break",        KW_BREAK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_case",         KW_CASE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_catch",        KW_CATCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_class",        KW_CLASS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_const",        KW_CONST);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_constructor",  KW_CONSTRUCTOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_continue",     KW_CONTINUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_default",      KW_DEFAULT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_defer",        KW_DEFER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_delete",       KW_DELETE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_do",           KW_DO);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_else",         KW_ELSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_extends",      KW_EXTENDS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_fallthrough",  KW_FALLTHROUGH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_false",        KW_FALSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_for",          KW_FOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_fun",          KW_FUN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_get",          KW_GET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_if",           KW_IF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_import",       KW_IMPORT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_in",           KW_IN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_instanceof",   KW_INSTANCEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_loop",         KW_LOOP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_repeat",       KW_REPEAT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_return",       KW_RETURN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_set",          KW_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_static",       KW_STATIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_super",        KW_SUPER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_switch",       KW_SWITCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_this",         KW_THIS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_throw",        KW_THROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_true",         KW_TRUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_try",          KW_TRY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_typeof",       KW_TYPEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_var",          KW_VAR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_void",         KW_VOID);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_while",        KW_WHILE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_with",         KW_WITH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "keyword_yield",        KW_YIELD);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_none",              OT_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_mask",              OT_MASK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_arithmetic",        OT_ARITHMETIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_unary",             OT_UNARY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_multiplicative",    OT_MULTIPLICATIVE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_bitwise",           OT_BITWISE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_comparison",        OT_COMPARISON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_assignment",        OT_ASSIGNMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_add",               OT_ADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_sub",               OT_SUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_mul",               OT_MUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_div",               OT_DIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_mod",               OT_MOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_not",               OT_NOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_lognot",            OT_LOGNOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_and",               OT_AND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_or",                OT_OR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_xor",               OT_XOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_shl",               OT_SHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_shr",               OT_SHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_shru",              OT_SHRU);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_logand",            OT_LOGAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_logor",             OT_LOGOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_logtri",            OT_LOGTRI);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_dot",               OT_DOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_more",              OT_MORE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_arrow",             OT_ARROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_lambda",            OT_LAMBDA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_eq",                OT_EQ);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_ne",                OT_NE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_ge",                OT_GE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_gt",                OT_GT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_le",                OT_LE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_lt",                OT_LT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_set",               OT_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setadd",            OT_SETADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setsub",            OT_SETSUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setmul",            OT_SETMUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setdiv",            OT_SETDIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setmod",            OT_SETMOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setand",            OT_SETAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setor",             OT_SETOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setxor",            OT_SETXOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setshl",            OT_SETSHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setshr",            OT_SETSHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "op_setshru",           OT_SETSHRU);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_none",             ST_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_paren_open",       ST_PAREN_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_paren_close",      ST_PAREN_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_comma",            ST_COMMA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_colon",            ST_COLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_semicolon",        ST_SEMICOLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_square_open",      ST_SQUARE_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_square_close",     ST_SQUARE_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_curly_open",       ST_CURLY_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "sep_curly_close",      ST_CURLY_CLOSE);

    TRY_ADD_INTEGER_CONSTANT(ctx, module, "any_token",            NT_ANY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module, "continue_string",      NT_CONTINUE_STRING);

cleanup:
    KOS_pop_locals(ctx, pushed);
    return error;
}

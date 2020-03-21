/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
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
    KOS_LOCAL           regs;
    KOS_LOCAL           args;
    KOS_LOCAL           lexer;
    KOS_LOCAL           init;
    KOS_LOCAL           token;
    KOS_LOCAL           value;
    KOS_LOCAL           ids;
    KOS_LEXER_OBJ      *kos_lexer;
    KOS_NEXT_TOKEN_MODE next_token   = NT_ANY;

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);

    KOS_init_locals(ctx, 7, &regs, &args, &lexer, &init, &token, &value, &ids);

    regs.o = regs_obj;
    args.o = args_obj;

    lexer.o = KOS_array_read(ctx, regs.o, 0);
    assert( ! IS_BAD_PTR(lexer.o));
    TRY_OBJID(lexer.o);

    /* TODO improve passing args for the first time to built-in generators */

    /* Instantiate the lexer on first invocation */
    if (GET_OBJ_TYPE(lexer.o) != OBJ_OBJECT ||
        ! KOS_object_get_private_ptr(lexer.o)) {

        uint32_t buf_size;

        init.o = lexer.o;

        /* TODO support OBJ_STRING as argument */

        if (GET_OBJ_TYPE(init.o) != OBJ_BUFFER)
            RAISE_EXCEPTION(str_err_not_buffer);

        buf_size = KOS_get_buffer_size(init.o);

        lexer.o = KOS_new_object(ctx);
        TRY_OBJID(lexer.o);

        kos_lexer = (KOS_LEXER_OBJ *)kos_malloc(sizeof(KOS_LEXER_OBJ) + buf_size - 1);
        if ( ! kos_lexer) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            goto cleanup;
        }

        OBJPTR(OBJECT, lexer.o)->finalize = finalize;

        KOS_object_set_private_ptr(lexer.o, kos_lexer);

        memcpy(&kos_lexer->buf[0], KOS_buffer_data_volatile(init.o), buf_size);

        kos_lexer_init(&kos_lexer->lexer, 0, &kos_lexer->buf[0], &kos_lexer->buf[buf_size]);

        TRY(KOS_array_resize(ctx, regs.o, 2));

        TRY(KOS_array_write(ctx, regs.o, 0, lexer.o));

        kos_lexer->ignore_errors = 0;

        if (KOS_get_array_size(regs.o) > 1) {

            const KOS_OBJ_ID arg = KOS_array_read(ctx, regs.o, 1);
            TRY_OBJID(arg);

            kos_lexer->ignore_errors = kos_is_truthy(arg);
        }

        ids.o = KOS_new_array(ctx, CONST_STR_NUM_IDS);
        TRY_OBJID(ids.o);

        token.o = KOS_new_const_ascii_string(ctx, str_token,   sizeof(str_token)   - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_TOKEN,   token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_line,    sizeof(str_line)    - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_LINE,    token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_column,  sizeof(str_column)  - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_COLUMN,  token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_type,    sizeof(str_type)    - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_TYPE,    token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_keyword, sizeof(str_keyword) - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_KEYWORD, token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_op,      sizeof(str_op)      - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_OP,      token.o));
        token.o = KOS_new_const_ascii_string(ctx, str_sep,     sizeof(str_sep)     - 1);
        TRY_OBJID(token.o);
        TRY(KOS_array_write(ctx, ids.o, CONST_STR_SEP,     token.o));

        TRY(KOS_array_write(ctx, regs.o, 1, ids.o));
    }
    else {
        assert(GET_OBJ_TYPE(lexer.o) == OBJ_OBJECT);

        kos_lexer = (KOS_LEXER_OBJ *)KOS_object_get_private_ptr(lexer.o);

        if (KOS_get_array_size(args.o) > 0) {

            int64_t    i_value;
            KOS_OBJ_ID arg = KOS_array_read(ctx, args.o, 0);

            TRY_OBJID(arg);

            TRY(KOS_get_integer(ctx, arg, &i_value));

            if (i_value != 0 && i_value != 1)
                RAISE_EXCEPTION(str_err_invalid_arg);

            if (i_value) {
                next_token = NT_CONTINUE_STRING;

                if (kos_lexer->token.sep != ST_PAREN_CLOSE)
                    RAISE_EXCEPTION(str_err_not_paren);

                kos_lexer_unget_token(&kos_lexer->lexer, &kos_lexer->token);
            }
        }

        ids.o = KOS_array_read(ctx, regs.o, 1);
        TRY_OBJID(ids.o);
    }

    assert( ! kos_lexer->lexer.error_str);

    error = kos_lexer_next_token(&kos_lexer->lexer, next_token, &kos_lexer->token);

    if (error) {
        if (kos_lexer->ignore_errors) {

            KOS_TOKEN *cur_token = &kos_lexer->token;

            kos_lexer->lexer.error_str = 0;

            cur_token->type    = TT_WHITESPACE;
            cur_token->keyword = KW_NONE;
            cur_token->op      = OT_NONE;
            cur_token->sep     = ST_NONE;

            kos_lexer->lexer.pos.column += cur_token->length;
        }
        else {
            KOS_LOCAL  parts[6];
            KOS_OBJ_ID exception;

            KOS_DECLARE_STATIC_CONST_STRING(str_format_parse_error, "parse error ");
            KOS_DECLARE_STATIC_CONST_STRING(str_format_colon,       ":");
            KOS_DECLARE_STATIC_CONST_STRING(str_format_colon_space, ": ");

            assert(error == KOS_ERROR_SCANNING_FAILED);

            KOS_init_locals(ctx, 3, &parts[1], &parts[3], &parts[5]);

            parts[0].o = KOS_CONST_ID(str_format_parse_error);

            parts[1].o = KOS_object_to_string(ctx, TO_SMALL_INT((int)kos_lexer->lexer.pos.line));
            if (IS_BAD_PTR(parts[1].o)) {
                KOS_destroy_top_locals(ctx, &parts[1], &parts[5]);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            parts[2].o = KOS_CONST_ID(str_format_colon);

            parts[3].o = KOS_object_to_string(ctx, TO_SMALL_INT((int)kos_lexer->lexer.pos.column));
            if (IS_BAD_PTR(parts[3].o)) {
                KOS_destroy_top_locals(ctx, &parts[1], &parts[5]);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            parts[4].o = KOS_CONST_ID(str_format_colon_space);

            parts[5].o = KOS_new_cstring(ctx, kos_lexer->lexer.error_str);
            if (IS_BAD_PTR(parts[5].o)) {
                KOS_destroy_top_locals(ctx, &parts[1], &parts[5]);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            exception = KOS_string_add_n(ctx, parts, sizeof(parts)/sizeof(parts[0]));

            KOS_destroy_top_locals(ctx, &parts[1], &parts[5]);

            TRY_OBJID(exception);

            KOS_raise_exception(ctx, exception);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    if (kos_lexer->token.type != TT_EOF) {

        KOS_TOKEN *cur_token = &kos_lexer->token;
        KOS_OBJ_ID key;

        token.o = KOS_new_object(ctx);
        TRY_OBJID(token.o);

        value.o = KOS_new_string(ctx, cur_token->begin, cur_token->length);
        TRY_OBJID(value.o);

        key = KOS_array_read(ctx, ids.o, CONST_STR_TOKEN);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             value.o));

        key = KOS_array_read(ctx, ids.o, CONST_STR_LINE);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->pos.line)));

        key = KOS_array_read(ctx, ids.o, CONST_STR_COLUMN);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->pos.column)));

        key = KOS_array_read(ctx, ids.o, CONST_STR_TYPE);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->type)));

        key = KOS_array_read(ctx, ids.o, CONST_STR_KEYWORD);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->keyword)));

        key = KOS_array_read(ctx, ids.o, CONST_STR_OP);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->op)));

        key = KOS_array_read(ctx, ids.o, CONST_STR_SEP);
        TRY_OBJID(key);
        TRY(KOS_set_property(ctx,
                             token.o,
                             key,
                             TO_SMALL_INT((int)cur_token->sep)));

        if (kos_lexer->token.type == TT_STRING || kos_lexer->token.type == TT_STRING_OPEN) {
            /* TODO parse string, raw/non-raw */
        }

        retval = token.o;
    }

cleanup:
    KOS_destroy_top_locals(ctx, &regs, &ids);

    if (error)
        retval = KOS_BADPTR;

    return retval;
}

int kos_module_kos_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_GENERATOR(       ctx, module.o, "raw_lexer", raw_lexer, 1);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "version_major",        KOS_VERSION_MAJOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "version_minor",        KOS_VERSION_MINOR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_whitespace",     TT_WHITESPACE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_eol",            TT_EOL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_comment",        TT_COMMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_eof",            TT_EOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_identifier",     TT_IDENTIFIER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_keyword",        TT_KEYWORD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_numeric",        TT_NUMERIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_string",         TT_STRING);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_string_open",    TT_STRING_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_operator",       TT_OPERATOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_separator",      TT_SEPARATOR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_none",         KW_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_line",         KW_LINE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_assert",       KW_ASSERT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_async",        KW_ASYNC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_break",        KW_BREAK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_case",         KW_CASE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_catch",        KW_CATCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_class",        KW_CLASS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_const",        KW_CONST);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_constructor",  KW_CONSTRUCTOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_continue",     KW_CONTINUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_default",      KW_DEFAULT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_defer",        KW_DEFER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_delete",       KW_DELETE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_do",           KW_DO);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_else",         KW_ELSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_extends",      KW_EXTENDS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_fallthrough",  KW_FALLTHROUGH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_false",        KW_FALSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_for",          KW_FOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_fun",          KW_FUN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_get",          KW_GET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_if",           KW_IF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_import",       KW_IMPORT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_in",           KW_IN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_instanceof",   KW_INSTANCEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_loop",         KW_LOOP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_repeat",       KW_REPEAT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_return",       KW_RETURN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_set",          KW_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_static",       KW_STATIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_super",        KW_SUPER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_switch",       KW_SWITCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_this",         KW_THIS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_throw",        KW_THROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_true",         KW_TRUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_try",          KW_TRY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_typeof",       KW_TYPEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_var",          KW_VAR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_void",         KW_VOID);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_while",        KW_WHILE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_with",         KW_WITH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_yield",        KW_YIELD);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_none",              OT_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mask",              OT_MASK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_arithmetic",        OT_ARITHMETIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_unary",             OT_UNARY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_multiplicative",    OT_MULTIPLICATIVE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_bitwise",           OT_BITWISE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_comparison",        OT_COMPARISON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_assignment",        OT_ASSIGNMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_add",               OT_ADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_sub",               OT_SUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mul",               OT_MUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_div",               OT_DIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mod",               OT_MOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_not",               OT_NOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lognot",            OT_LOGNOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_and",               OT_AND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_or",                OT_OR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_xor",               OT_XOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shl",               OT_SHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shr",               OT_SHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shru",              OT_SHRU);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logand",            OT_LOGAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logor",             OT_LOGOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logtri",            OT_LOGTRI);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_dot",               OT_DOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_more",              OT_MORE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_arrow",             OT_ARROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lambda",            OT_LAMBDA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_eq",                OT_EQ);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_ne",                OT_NE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_ge",                OT_GE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_gt",                OT_GT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_le",                OT_LE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lt",                OT_LT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_set",               OT_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setadd",            OT_SETADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setsub",            OT_SETSUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setmul",            OT_SETMUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setdiv",            OT_SETDIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setmod",            OT_SETMOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setand",            OT_SETAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setor",             OT_SETOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setxor",            OT_SETXOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshl",            OT_SETSHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshr",            OT_SETSHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshru",           OT_SETSHRU);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_none",             ST_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_paren_open",       ST_PAREN_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_paren_close",      ST_PAREN_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_comma",            ST_COMMA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_colon",            ST_COLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_semicolon",        ST_SEMICOLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_square_open",      ST_SQUARE_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_square_close",     ST_SQUARE_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_curly_open",       ST_CURLY_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_curly_close",      ST_CURLY_CLOSE);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "any_token",            NT_ANY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "continue_string",      NT_CONTINUE_STRING);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

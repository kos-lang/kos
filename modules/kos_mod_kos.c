/*
 * Copyright (c) 2014-2017 Chris Dragan
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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_lexer.h"
#include "../core/kos_object_alloc.h"
#include "../core/kos_try.h"

static const char str_column[]             = "column";
static const char str_err_not_buffer[]     = "object is not a buffer";
static const char str_format_colon[]       = ":";
static const char str_format_colon_space[] = ": ";
static const char str_format_parse_error[] = "parse error ";
static const char str_keyword[]            = "keyword";
static const char str_line[]               = "line";
static const char str_op[]                 = "op";
static const char str_sep[]                = "sep";
static const char str_token[]              = "token";
static const char str_type[]               = "type";

struct _KOS_LEXER_OBJECT {
    KOS_CUSTOM         custom;
    struct _KOS_LEXER *lexer;
    uint8_t           *last_buf;
};

static KOS_OBJ_ID _lexer(KOS_FRAME  frame,
                         KOS_OBJ_ID regs_obj,
                         KOS_OBJ_ID args_obj)
{
    int                       error  = KOS_SUCCESS;
    KOS_OBJ_ID                retval = KOS_BADPTR;
    KOS_OBJ_ID                lexer_obj_id;
    struct _KOS_LEXER_OBJECT *lexer;
    struct _KOS_TOKEN         token;
    uint32_t                  buf_size;
    uint8_t                  *buf_data;

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);

    lexer_obj_id = KOS_array_read(frame, regs_obj, 0);
    assert( ! IS_BAD_PTR(lexer_obj_id));
    TRY_OBJID(lexer_obj_id);

    /* On first invocation instantiate the lexer */
    if (GET_OBJ_SUBTYPE(lexer_obj_id) != OBJ_CUSTOM) {

        KOS_OBJ_ID init_arg = lexer_obj_id;

        if (GET_OBJ_TYPE(init_arg) != OBJ_BUFFER)
            RAISE_EXCEPTION(str_err_not_buffer);

        lexer_obj_id = KOS_new_custom(frame, (unsigned)sizeof(struct _KOS_LEXER_OBJECT));
        TRY_OBJID(lexer_obj_id);

        lexer = (struct _KOS_LEXER_OBJECT *)OBJPTR(CUSTOM, lexer_obj_id);

        lexer->lexer = (struct _KOS_LEXER *)_KOS_alloc_buffer(frame, sizeof(struct _KOS_LEXER));
        if (!lexer->lexer)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);

        buf_size = KOS_get_buffer_size(init_arg);
        buf_data = KOS_buffer_data(init_arg);

        _KOS_lexer_init(lexer->lexer, 0, (char *)buf_data, (char *)buf_data + buf_size);

        lexer->custom.owned = init_arg;
        lexer->last_buf     = buf_data;

        TRY(KOS_array_write(frame, regs_obj, 0, lexer_obj_id));
    }
    else {
        lexer = (struct _KOS_LEXER_OBJECT *)OBJPTR(CUSTOM, lexer_obj_id);

        buf_size = KOS_get_buffer_size(lexer->custom.owned);
        buf_data = KOS_buffer_data(lexer->custom.owned);

        /* Update pointers if the buffer's data storage was reallocated */
        if (buf_data != lexer->last_buf) {
            const intptr_t delta = (intptr_t)(buf_data - lexer->last_buf);

            lexer->lexer->buf            += delta;
            lexer->lexer->buf_end        += delta;
            lexer->lexer->prefetch_begin += delta;
            lexer->lexer->prefetch_end   += delta;
            lexer->last_buf              =  buf_data;
        }
    }

    assert( ! lexer->lexer->error_str);

    /* TODO string continuations? */
    error = _KOS_lexer_next_token(lexer->lexer, NT_ANY, &token);

    if (error) {
        KOS_ATOMIC(KOS_OBJ_ID) parts[6];
        KOS_OBJ_ID             exception;

        assert(error == KOS_ERROR_SCANNING_FAILED);

        parts[0] = KOS_context_get_cstring(frame, str_format_parse_error);
        parts[1] = KOS_object_to_string(frame, TO_SMALL_INT((int)lexer->lexer->pos.line));
        TRY_OBJID(parts[1]);
        parts[2] = KOS_context_get_cstring(frame, str_format_colon);
        parts[3] = KOS_object_to_string(frame, TO_SMALL_INT((int)lexer->lexer->pos.column));
        TRY_OBJID(parts[3]);
        parts[4] = KOS_context_get_cstring(frame, str_format_colon_space);
        parts[5] = KOS_new_cstring(frame, lexer->lexer->error_str);
        TRY_OBJID(parts[5]);

        exception = KOS_string_add_many(frame, parts, sizeof(parts)/sizeof(parts[0]));
        TRY_OBJID(exception);

        KOS_raise_exception(frame, exception);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    if (token.type != TT_EOF) {

        KOS_OBJ_ID value;

        KOS_OBJ_ID token_obj = KOS_new_object(frame);
        TRY_OBJID(token_obj);

        value = KOS_new_string_esc(frame, token.begin, token.length);
        TRY_OBJID(value);

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_token),
                             value));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_line),
                             TO_SMALL_INT((int)token.pos.line)));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_column),
                             TO_SMALL_INT((int)token.pos.column)));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_type),
                             TO_SMALL_INT((int)token.type)));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_keyword),
                             TO_SMALL_INT((int)token.keyword)));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_op),
                             TO_SMALL_INT((int)token.op)));

        TRY(KOS_set_property(frame,
                             token_obj,
                             KOS_context_get_cstring(frame, str_sep),
                             TO_SMALL_INT((int)token.sep)));

        retval = token_obj;
    }

_error:
    if (error)
        retval = KOS_BADPTR;

    return retval;
}

int _KOS_module_kos_init(KOS_FRAME frame)
{
    int error = KOS_SUCCESS;

    TRY_ADD_GENERATOR(frame, "lexer", _lexer, 1);

    TRY_ADD_INTEGER_CONSTANT(frame, "version_major",        KOS_VERSION_MAJOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "version_minor",        KOS_VERSION_MINOR);

    TRY_ADD_INTEGER_CONSTANT(frame, "token_whitespace",     TT_WHITESPACE);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_eol",            TT_EOL);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_comment",        TT_COMMENT);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_eof",            TT_EOF);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_identifier",     TT_IDENTIFIER);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_keyword",        TT_KEYWORD);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_numeric",        TT_NUMERIC);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_string",         TT_STRING);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_string_open_sq", TT_STRING_OPEN_SQ);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_string_open_dq", TT_STRING_OPEN_DQ);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_operator",       TT_OPERATOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "token_separator",      TT_SEPARATOR);

    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_none",         KW_NONE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_line",         KW_LINE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_assert",       KW_ASSERT);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_break",        KW_BREAK);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_catch",        KW_CATCH);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_class",        KW_CLASS);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_const",        KW_CONST);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_constructor",  KW_CONSTRUCTOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_continue",     KW_CONTINUE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_defer",        KW_DEFER);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_delete",       KW_DELETE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_do",           KW_DO);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_else",         KW_ELSE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_fallthrough",  KW_FALLTHROUGH);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_false",        KW_FALSE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_for",          KW_FOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_fun",          KW_FUN);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_get",          KW_GET);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_if",           KW_IF);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_import",       KW_IMPORT);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_in",           KW_IN);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_instanceof",   KW_INSTANCEOF);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_loop",         KW_LOOP);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_repeat",       KW_REPEAT);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_return",       KW_RETURN);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_set",          KW_SET);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_static",       KW_STATIC);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_switch",       KW_SWITCH);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_this",         KW_THIS);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_throw",        KW_THROW);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_true",         KW_TRUE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_try",          KW_TRY);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_typeof",       KW_TYPEOF);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_var",          KW_VAR);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_void",         KW_VOID);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_while",        KW_WHILE);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_with",         KW_WITH);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_yield",        KW_YIELD);
    TRY_ADD_INTEGER_CONSTANT(frame, "keyword_lambda",       KW_LAMBDA);

    TRY_ADD_INTEGER_CONSTANT(frame, "op_none",              OT_NONE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_mask",              OT_MASK);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_arithmetic",        OT_ARITHMETIC);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_unary",             OT_UNARY);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_multiplicative",    OT_MULTIPLICATIVE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_bitwise",           OT_BITWISE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_comparison",        OT_COMPARISON);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_assignment",        OT_ASSIGNMENT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_add",               OT_ADD);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_sub",               OT_SUB);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_mul",               OT_MUL);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_div",               OT_DIV);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_mod",               OT_MOD);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_not",               OT_NOT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_lognot",            OT_LOGNOT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_and",               OT_AND);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_or",                OT_OR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_xor",               OT_XOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_shl",               OT_SHL);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_shr",               OT_SHR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_shru",              OT_SHRU);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_logand",            OT_LOGAND);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_logor",             OT_LOGOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_logtri",            OT_LOGTRI);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_dot",               OT_DOT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_more",              OT_MORE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_arrow",             OT_ARROW);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_eq",                OT_EQ);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_ne",                OT_NE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_ge",                OT_GE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_gt",                OT_GT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_le",                OT_LE);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_lt",                OT_LT);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_set",               OT_SET);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setadd",            OT_SETADD);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setsub",            OT_SETSUB);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setmul",            OT_SETMUL);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setdiv",            OT_SETDIV);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setmod",            OT_SETMOD);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setand",            OT_SETAND);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setor",             OT_SETOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setxor",            OT_SETXOR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setshl",            OT_SETSHL);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setshr",            OT_SETSHR);
    TRY_ADD_INTEGER_CONSTANT(frame, "op_setshru",           OT_SETSHRU);

    TRY_ADD_INTEGER_CONSTANT(frame, "sep_none",             ST_NONE);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_paren_open",       ST_PAREN_OPEN);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_paren_close",      ST_PAREN_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_comma",            ST_COMMA);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_colon",            ST_COLON);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_semicolon",        ST_SEMICOLON);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_square_open",      ST_SQUARE_OPEN);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_square_close",     ST_SQUARE_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_curly_open",       ST_CURLY_OPEN);
    TRY_ADD_INTEGER_CONSTANT(frame, "sep_curly_close",      ST_CURLY_CLOSE);

_error:
    return error;
}

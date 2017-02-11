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

#include "kos_parser.h"
#include "kos_ast.h"
#include "kos_memory.h"
#include "kos_try.h"
#include "../inc/kos_error.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_err_duplicate_default[]         = "multiple 'default' labels in one switch";
static const char str_err_eol_before_par[]            = "ambiguous syntax: end of line before '(' - consider adding a ';'";
static const char str_err_eol_before_sq[]             = "ambiguous syntax: end of line before '[' - consider adding a ';'";
static const char str_err_eol_before_op[]             = "ambiguous syntax: end of line before operator - consider adding a ';'";
static const char str_err_expected_catch[]            = "expected 'catch'";
static const char str_err_expected_colon[]            = "expected ':'";
static const char str_err_expected_comma[]            = "expected ','";
static const char str_err_expected_const_or_expr[]    = "expected 'const' or expression";
static const char str_err_expected_curly_close[]      = "expected '}'";
static const char str_err_expected_curly_open[]       = "expected '{'";
static const char str_err_expected_expr_or_curly[]    = "expected expression or '{'";
static const char str_err_expected_expression[]       = "expected expression";
static const char str_err_expected_ident_or_str[]     = "expected identifier or string literal";
static const char str_err_expected_identifier[]       = "expected identifier";
static const char str_err_expected_lambda_form[]      = "expected '->'";
static const char str_err_expected_member_expr[]      = "expected literal, identifier or '('";
static const char str_err_expected_multi_assignment[] = "expected '=' after comma-separated variables or members";
static const char str_err_expected_paren_close[]      = "expected ')'";
static const char str_err_expected_paren_open[]       = "expected '('";
static const char str_err_expected_semicolon[]        = "expected ';'";
static const char str_err_expected_square_close[]     = "expected ']'";
static const char str_err_expected_string[]           = "unexpected interpolated string";
static const char str_err_expected_var[]              = "expected 'var'";
static const char str_err_expected_var_or_const[]     = "expected 'var' or 'const'";
static const char str_err_expected_var_assignment[]   = "expected '=' in variable declaration";
static const char str_err_expected_while[]            = "expected 'while'";
static const char str_err_mixed_operators[]           = "mixed operators, consider using parentheses";
static const char str_err_unexpected_break[]          = "unexpected 'break' statement; can only be used inside a loop";
static const char str_err_unexpected_continue[]       = "unexpected 'continue' statement; can only be used inside a loop";
static const char str_err_unexpected_import[]         = "unexpected 'import' statement";
static const char str_err_unsupported_slice_assign[]  = "unsupported assignment to slice, expected '='";

static int _next_statement(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret);
static int _member_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret);
static int _right_hand_side_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret);
static int _compound_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret);

static int _next_token(struct _KOS_PARSER *parser)
{
    int error = KOS_SUCCESS;

    if ( ! parser->unget) {

        enum _KOS_TOKEN_TYPE type;
        int                  had_eol = 0;

        for (;;) {

            error = _KOS_lexer_next_token(&parser->lexer, NT_ANY, &parser->token);

            if (error)
                break;

            type = parser->token.type;

            if (type == TT_COMMENT && parser->token.pos.line < parser->lexer.pos.line)
                had_eol = 1;
            else if (type == TT_EOL)
                had_eol = 1;
            else if (type > TT_COMMENT)
                break;
        }

        parser->had_eol = had_eol;
    }

    parser->unget = 0;

    return error;
}

static int _is_implicit_semicolon(struct _KOS_PARSER *parser)
{
    const struct _KOS_TOKEN *token = &parser->token;

    return (token->sep != ST_SEMICOLON &&
            (parser->had_eol || token->sep == ST_CURLY_CLOSE || token->type == TT_EOF))
           ? 1 : 0;
}

static int _assume_separator(struct _KOS_PARSER *parser, enum _KOS_SEPARATOR_TYPE sep)
{
    int error = _next_token(parser);

    if (!error) {
        const struct _KOS_TOKEN *token = &parser->token;

        if (token->sep != sep) {

            switch (sep) {
                case ST_COLON:
                    parser->error_str = str_err_expected_colon;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
                case ST_SEMICOLON:
                    if (_is_implicit_semicolon(parser))
                        parser->unget = 1;
                    else {
                        parser->error_str = str_err_expected_semicolon;
                        error = KOS_ERROR_PARSE_FAILED;
                    }
                    break;
                case ST_CURLY_OPEN:
                    parser->error_str = str_err_expected_curly_open;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
                case ST_PAREN_OPEN:
                    parser->error_str = str_err_expected_paren_open;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
                case ST_PAREN_CLOSE:
                    parser->error_str = str_err_expected_paren_close;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
                case ST_SQUARE_CLOSE:
                    /* fall through */
                default:
                    assert(sep == ST_SQUARE_CLOSE);
                    parser->error_str = str_err_expected_square_close;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
            }
        }
    }

    return error;
}

static int _new_node(struct _KOS_PARSER    *parser,
                     struct _KOS_AST_NODE **node,
                     enum _KOS_NODE_TYPE    type)
{
    int                   error = KOS_SUCCESS;
    struct _KOS_AST_NODE *ast_node;

    assert(parser->ast_buf);

    ast_node = (struct _KOS_AST_NODE *)_KOS_mempool_alloc(parser->ast_buf, sizeof(struct _KOS_AST_NODE));

    if (ast_node) {
        ast_node->next       = 0;
        ast_node->children   = 0;
        ast_node->last_child = 0;
        ast_node->token      = parser->token;
        ast_node->type       = type;

        *node = ast_node;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void _ast_push(struct _KOS_AST_NODE *parent, struct _KOS_AST_NODE *child)
{
    if (parent->last_child)
        parent->last_child->next = child;
    else
        parent->children = child;
    parent->last_child = child;
}

static int _push_node(struct _KOS_PARSER    *parser,
                      struct _KOS_AST_NODE  *node,
                      enum _KOS_NODE_TYPE    type,
                      struct _KOS_AST_NODE **ret)
{
    struct _KOS_AST_NODE *new_node = 0;

    int error = _new_node(parser, &new_node, type);

    _ast_push(node, new_node);

    if (!error && ret)
        *ret = new_node;

    return error;
}

static int _fetch_optional_paren(struct _KOS_PARSER *parser, int *was_paren)
{
    int error = _next_token(parser);

    if (!error) {

        *was_paren = parser->token.sep == ST_PAREN_OPEN;

        if (parser->token.sep != ST_PAREN_OPEN)
            parser->unget = 1;
    }

    return error;
}

static int _function_literal(struct _KOS_PARSER *parser, int need_compound, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;
    struct _KOS_AST_NODE *args;

    const int lambda            = parser->token.keyword == KW_LAMBDA;
    const int saved_unary_depth = parser->unary_depth;
    const int saved_allow_break = parser->allow_break;

    parser->unary_depth = 0;
    parser->allow_break = 0;

    TRY(_new_node(parser, ret, NT_FUNCTION_LITERAL));

    TRY(_next_token(parser));

    if (parser->token.sep == ST_PAREN_OPEN) {

        TRY(_push_node(parser, *ret, NT_PARAMETERS, &args));

        TRY(_next_token(parser));

        while (parser->token.type == TT_IDENTIFIER) {

            enum _KOS_NODE_TYPE node_type = NT_ASSIGNMENT;

            TRY(_new_node(parser, &node, NT_IDENTIFIER));

            TRY(_next_token(parser));

            if (parser->token.op == OT_MORE) {

                struct _KOS_AST_NODE *new_arg;

                node_type = NT_ELLIPSIS;

                TRY(_push_node(parser, args, node_type, &new_arg));

                _ast_push(new_arg, node);
                node = 0;

                TRY(_next_token(parser));
            }

            if (node) {
                _ast_push(args, node);
                node = 0;
            }

            if (node_type == NT_ELLIPSIS)
                break;

            if (parser->token.sep == ST_COMMA)
                TRY(_next_token(parser));

            else if (parser->token.sep != ST_PAREN_CLOSE) {
                parser->error_str = str_err_expected_paren_close;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }
        }

        parser->unget = 1;
        TRY(_assume_separator(parser, ST_PAREN_CLOSE));

        TRY(_next_token(parser));
    }
    else
        TRY(_push_node(parser, *ret, NT_PARAMETERS, &args));

    if (lambda && parser->token.op != OT_ARROW) {
        parser->error_str = str_err_expected_lambda_form;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    if (parser->token.op == OT_ARROW) {

        struct _KOS_AST_NODE *return_node;

        if (need_compound) {
            parser->error_str = str_err_expected_curly_open;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        parser->unary_depth = 1;

        TRY(_new_node(parser, &node, NT_SCOPE));

        TRY(_push_node(parser, node, NT_RETURN, &return_node));

        TRY(_assume_separator(parser, ST_PAREN_OPEN));

        TRY(_push_node(parser, *ret, NT_LANDMARK, 0));

        _ast_push(*ret, node);
        node = 0;

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(return_node, node);

        TRY(_assume_separator(parser, ST_PAREN_CLOSE));

        TRY(_push_node(parser, *ret, NT_LANDMARK, 0));

        assert(parser->unary_depth == 1);
    }
    else {

        parser->unget = 1;

        TRY(_push_node(parser, *ret, NT_LANDMARK, 0));

        TRY(_compound_stmt(parser, &node));

        _ast_push(*ret, node);

        assert(parser->token.sep == ST_CURLY_CLOSE);

        TRY(_push_node(parser, node, NT_RETURN, &node));

        TRY(_push_node(parser, node, NT_VOID_LITERAL, 0));

        TRY(_push_node(parser, *ret, NT_LANDMARK, 0));

        assert(parser->unary_depth == 0);
    }

_error:
    parser->unary_depth = saved_unary_depth;
    parser->allow_break = saved_allow_break;

    return error;
}

static int _interpolated_string(struct _KOS_PARSER    *parser,
                                struct _KOS_AST_NODE **ret)
{
    int                             error       = KOS_SUCCESS;
    struct _KOS_AST_NODE           *node        = 0;
    const enum _KOS_TOKEN_TYPE      token_type  = parser->token.type;
    const enum _KOS_NEXT_TOKEN_MODE string_mode =
            token_type == TT_STRING_OPEN_SQ
            ? NT_SINGLE_Q_STRING : NT_DOUBLE_Q_STRING;

    TRY(_new_node(parser, ret, NT_INTERPOLATED_STRING));

    TRY(_new_node(parser, &node, NT_STRING_LITERAL));

    _ast_push(*ret, node);
    node = 0;

    do {

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        _KOS_lexer_unget_token(&parser->lexer, &parser->token);
        parser->unget = 0;

        TRY(_KOS_lexer_next_token(&parser->lexer, string_mode, &parser->token));
        parser->unget = 0;

        assert(parser->token.type == token_type ||
               parser->token.type == TT_STRING);

        TRY(_new_node(parser, &node, NT_STRING_LITERAL));

        _ast_push(*ret, node);
        node = 0;
    }
    while (parser->token.type != TT_STRING);

_error:
    return error;
}

static int _array_literal(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_ARRAY_LITERAL));

    TRY(_next_token(parser));

    while (parser->token.sep != ST_SQUARE_CLOSE) {

        parser->unget = 1;

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        TRY(_next_token(parser));

        if (parser->token.sep == ST_COMMA)
            TRY(_next_token(parser));
        else if (parser->token.sep != ST_SQUARE_CLOSE) {
            parser->error_str = str_err_expected_square_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }
    }

_error:
    return error;
}

static int _object_literal(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int comma = 1;

    struct _KOS_AST_NODE *node = 0;
    struct _KOS_AST_NODE *prop = 0;

    TRY(_new_node(parser, ret, NT_OBJECT_LITERAL));

    for (;;) {

        TRY(_next_token(parser));

        if (parser->token.sep == ST_COMMA) {
            if (comma == 1) {
                parser->error_str = str_err_expected_ident_or_str;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }
            comma = 1;
            continue;
        }
        else if (parser->token.sep == ST_CURLY_CLOSE)
            break;

        if (!comma) {
            parser->error_str = str_err_expected_comma;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        parser->unget = 1;

        TRY(_push_node(parser, *ret, NT_PROPERTY, &prop));

        TRY(_next_token(parser));

        if (parser->token.type == TT_STRING)
            TRY(_push_node(parser, prop, NT_STRING_LITERAL, 0));
        else if (parser->token.type == TT_STRING_OPEN_SQ || parser->token.type == TT_STRING_OPEN_DQ) {
            parser->error_str = str_err_expected_string;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }
        else if (parser->token.type == TT_IDENTIFIER || parser->token.type == TT_KEYWORD)
            TRY(_push_node(parser, prop, NT_STRING_LITERAL, 0));
        else {
            parser->error_str = str_err_expected_ident_or_str;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_assume_separator(parser, ST_COLON));

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(prop, node);
        node = 0;

        comma = 0;
    }

_error:
    return error;
}

static int _primary_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int       error             = _next_token(parser);
    const int saved_unary_depth = parser->unary_depth;

    ++parser->unary_depth;

    if (!error) {
        const struct _KOS_TOKEN *token = &parser->token;

        switch (token->type) {
            case TT_NUMERIC:
                error = _new_node(parser, ret, NT_NUMERIC_LITERAL);
                break;
            case TT_STRING:
                error = _new_node(parser, ret, NT_STRING_LITERAL);
                break;
            case TT_STRING_OPEN_SQ:
            case TT_STRING_OPEN_DQ:
                error = _interpolated_string(parser, ret);
                break;
            case TT_IDENTIFIER:
                error = _new_node(parser, ret, NT_IDENTIFIER);
                break;
            case TT_KEYWORD:
                switch (token->keyword) {
                    case KW_FUN:
                    case KW_LAMBDA:
                        error = _function_literal(parser, 0, ret);
                        break;
                    case KW_THIS:
                        error = _new_node(parser, ret, NT_THIS_LITERAL);
                        break;
                    case KW_LINE:
                        error = _new_node(parser, ret, NT_LINE_LITERAL);
                        break;
                    case KW_TRUE:
                    case KW_FALSE:
                        error = _new_node(parser, ret, NT_BOOL_LITERAL);
                        break;
                    case KW_VOID:
                        error = _new_node(parser, ret, NT_VOID_LITERAL);
                        break;
                    default:
                        parser->error_str = str_err_expected_member_expr;
                        error = KOS_ERROR_PARSE_FAILED;
                        break;
                }
                break;
            case TT_SEPARATOR:
                switch (token->sep) {
                    case ST_SQUARE_OPEN:
                        error = _array_literal(parser, ret);
                        break;
                    case ST_CURLY_OPEN:
                        error = _object_literal(parser, ret);
                        break;
                    case ST_PAREN_OPEN:
                        error = _right_hand_side_expr(parser, ret);
                        if (!error)
                            error = _assume_separator(parser, ST_PAREN_CLOSE);
                        break;
                    default:
                        parser->error_str = str_err_expected_member_expr;
                        error = KOS_ERROR_PARSE_FAILED;
                        break;
                }
                break;
            default:
                parser->error_str = str_err_expected_member_expr;
                error = KOS_ERROR_PARSE_FAILED;
                break;
        }
    }

    assert(parser->unary_depth == saved_unary_depth + 1);
    parser->unary_depth = saved_unary_depth;

    return error;
}

static int _unary_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int                   error             = KOS_SUCCESS;
    const int             saved_unary_depth = parser->unary_depth;
    struct _KOS_AST_NODE *node              = 0;
    struct _KOS_AST_NODE *invocation        = 0;

    TRY(_next_token(parser));

    if ((parser->token.op & OT_UNARY)      ||
        parser->token.keyword == KW_TYPEOF ||
        parser->token.keyword == KW_DELETE) {

        ++parser->unary_depth;

        TRY(_new_node(parser, ret, NT_OPERATOR));

        TRY(_unary_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        assert(parser->unary_depth == saved_unary_depth + 1);
    }
    else if (parser->token.keyword == KW_NEW) {

        TRY(_new_node(parser, ret, NT_OPERATOR));

        TRY(_new_node(parser, &invocation, NT_INVOCATION));

        TRY(_member_expr(parser, &node));

        if (node->type != NT_INVOCATION) {
            _ast_push(invocation, node);
            node       = invocation;
            invocation = 0;
        }

        _ast_push(*ret, node);
        node = 0;

        assert(parser->unary_depth == saved_unary_depth);
    }
    else {

        parser->unget = 1;

        TRY(_member_expr(parser, ret));

        assert(parser->unary_depth == saved_unary_depth);
    }

_error:
    parser->unary_depth = saved_unary_depth;

    return error;
}

static int _arithm_bitwise_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;
    struct _KOS_AST_NODE *aux  = 0;

    TRY(_unary_expr(parser, &node));

    TRY(_next_token(parser));

    if ((parser->token.op & OT_ARITHMETIC)) {

        enum _KOS_OPERATOR_TYPE last_op = parser->token.op;

        if ((last_op == OT_ADD || last_op == OT_SUB)
            && parser->had_eol && parser->unary_depth == 0)
        {
            parser->error_str = str_err_eol_before_op;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_new_node(parser, ret, NT_OPERATOR));

        _ast_push(*ret, node);
        node = 0;

        TRY(_unary_expr(parser, &node));

        TRY(_next_token(parser));

        for (;;) {

            if (parser->token.op == OT_ADD || parser->token.op == OT_SUB) {

                if (parser->had_eol && parser->unary_depth == 0) {
                    parser->error_str = str_err_eol_before_op;
                    error = KOS_ERROR_PARSE_FAILED;
                    goto _error;
                }

                _ast_push(*ret, node);
                node = *ret;
                *ret = 0;

                last_op = parser->token.op;

                TRY(_new_node(parser, ret, NT_OPERATOR));

                _ast_push(*ret, node);
                node = 0;

                TRY(_unary_expr(parser, &node));

                TRY(_next_token(parser));
            }
            else if ((parser->token.op & OT_MASK) == OT_MULTIPLICATIVE) {

                while ((parser->token.op & OT_MASK) == OT_MULTIPLICATIVE) {

                    if ((last_op & OT_MASK) == OT_MULTIPLICATIVE) {

                        _ast_push(*ret, node);
                        node = *ret;
                        *ret = 0;

                        last_op = parser->token.op;

                        TRY(_new_node(parser, ret, NT_OPERATOR));

                        _ast_push(*ret, node);
                        node = 0;

                        TRY(_unary_expr(parser, &node));
                    }
                    else {

                        TRY(_new_node(parser, &aux, NT_OPERATOR));

                        _ast_push(aux, node);
                        node = 0;

                        TRY(_unary_expr(parser, &node));

                        _ast_push(aux, node);
                        node = aux;
                        aux  = 0;
                    }

                    TRY(_next_token(parser));
                }
            }
            else
                break;
        }

        _ast_push(*ret, node);
        node = 0;

        if ((parser->token.op & OT_MASK) == OT_BITWISE) {
            parser->error_str = str_err_mixed_operators;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        parser->unget = 1;
    }
    else if ((parser->token.op & OT_MASK) == OT_BITWISE) {

        const enum _KOS_OPERATOR_TYPE op = parser->token.op;

        *ret = node;
        node = 0;

        while (parser->token.op == op) {

            TRY(_new_node(parser, &node, NT_OPERATOR));

            _ast_push(node, *ret);
            *ret = node;
            node = 0;

            TRY(_unary_expr(parser, &node));

            _ast_push(*ret, node);
            node = 0;

            TRY(_next_token(parser));
        }

        {
            const enum _KOS_OPERATOR_TYPE next_op = parser->token.op;
            if ((next_op & OT_MASK) == OT_BITWISE     ||
                (next_op & OT_MASK) == OT_ARITHMETIC ||
                next_op == OT_SHL || next_op == OT_SHR || next_op == OT_SSR) {

                parser->error_str = str_err_mixed_operators;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }
        }

        parser->unget = 1;
    }
    else if (parser->token.op == OT_SHL || parser->token.op == OT_SHR || parser->token.op == OT_SSR) {

        TRY(_new_node(parser, ret, NT_OPERATOR));

        _ast_push(*ret, node);
        node = 0;

        TRY(_unary_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;
    }
    else {
        parser->unget = 1;
        *ret = node;
        node = 0;
    }

_error:
    return error;
}

static int _comparison_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;
    struct _KOS_AST_NODE *aux  = 0;

    TRY(_arithm_bitwise_expr(parser, &node));

    TRY(_next_token(parser));

    if ((parser->token.op & OT_MASK) == OT_COMPARISON
        || parser->token.keyword == KW_IN
        || parser->token.keyword == KW_INSTANCEOF) {

        TRY(_new_node(parser, ret, NT_OPERATOR));

        /* Swap operands of the 'in' operator */
        if (parser->token.keyword == KW_IN)
            aux = node;
        else
            _ast_push(*ret, node);
        node = 0;

        TRY(_arithm_bitwise_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        if (aux) {
            _ast_push(*ret, aux);
            aux = 0;
        }
    }
    else {
        parser->unget = 1;
        *ret = node;
        node = 0;
    }

_error:
    return error;
}

static int _logical_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_comparison_expr(parser, &node));

    TRY(_next_token(parser));

    if (parser->token.op == OT_LOGAND || parser->token.op == OT_LOGOR) {

        struct _KOS_AST_NODE *op_node = 0;

        const enum _KOS_OPERATOR_TYPE op = parser->token.op;

        TRY(_new_node(parser, ret, NT_OPERATOR));

        _ast_push(*ret, node);
        node = 0;

        op_node = *ret;

        for (;;) {

            TRY(_comparison_expr(parser, &node));

            TRY(_next_token(parser));

            if (parser->token.op == op) {

                TRY(_push_node(parser, op_node, NT_OPERATOR, &op_node));

                _ast_push(op_node, node);
                node = 0;
            }
            else
                break;
        }

        _ast_push(op_node, node);
        node = 0;

        if (parser->token.op == OT_LOGAND || parser->token.op == OT_LOGOR) {
            parser->error_str = str_err_mixed_operators;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }
    }
    else {
        *ret = node;
        node = 0;
    }

    parser->unget = 1;

_error:
    return error;
}

static int _conditional_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int                   error             = KOS_SUCCESS;
    struct _KOS_AST_NODE *node              = 0;
    const int             saved_unary_depth = parser->unary_depth;

    TRY(_logical_expr(parser, &node));

    TRY(_next_token(parser));

    if (parser->token.op == OT_LOGTRI) {

        TRY(_new_node(parser, ret, NT_OPERATOR));

        _ast_push(*ret, node);
        node = 0;

        ++parser->unary_depth;

        TRY(_conditional_expr(parser, &node));

        --parser->unary_depth;

        _ast_push(*ret, node);
        node = 0;

        TRY(_assume_separator(parser, ST_COLON));

        TRY(_conditional_expr(parser, &node));

        _ast_push(*ret, node);
    }
    else {
        parser->unget = 1;
        *ret = node;
    }

    assert(parser->unary_depth == saved_unary_depth);

_error:
    parser->unary_depth = saved_unary_depth;

    return error;
}

static int _stream_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    TRY(_conditional_expr(parser, ret));

    TRY(_next_token(parser));

    while (parser->token.op == OT_ARROW) {

        struct _KOS_AST_NODE *node = *ret;

        TRY(_new_node(parser, ret, NT_STREAM));

        _ast_push(*ret, node);
        node = 0;

        TRY(_conditional_expr(parser, &node));

        _ast_push(*ret, node);

        TRY(_next_token(parser));
    }

    parser->unget = 1;

_error:
    return error;
}

static int _right_hand_side_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_next_token(parser));

    if (parser->token.keyword == KW_YIELD) {

        TRY(_new_node(parser, ret, NT_YIELD));

        TRY(_stream_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;
    }
    else {

        parser->unget = 1;

        TRY(_stream_expr(parser, ret));
    }

_error:
    return error;
}

static int _refinement_identifier(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = *ret;
    *ret = 0;

    TRY(_new_node(parser, ret, NT_REFINEMENT));

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.type == TT_STRING_OPEN_SQ || parser->token.type == TT_STRING_OPEN_DQ) {
        parser->error_str = str_err_expected_string;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    if (parser->token.type != TT_IDENTIFIER && parser->token.type != TT_KEYWORD && parser->token.type != TT_STRING) {
        parser->error_str = str_err_expected_ident_or_str;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    TRY(_push_node(parser, *ret, NT_STRING_LITERAL, 0));

_error:
    return error;
}

static int _refinement_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = *ret;
    *ret = 0;

    TRY(_new_node(parser, ret, NT_REFINEMENT));

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.sep == ST_SQUARE_CLOSE) {
        parser->error_str = str_err_expected_expression;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    else if (parser->token.sep == ST_COLON) {

        (*ret)->type = NT_SLICE;

        TRY(_next_token(parser));

        if (parser->token.sep == ST_SQUARE_CLOSE) {

            TRY(_push_node(parser, *ret, NT_VOID_LITERAL, 0));

            TRY(_push_node(parser, *ret, NT_VOID_LITERAL, 0));

            parser->unget = 1;
        }
        else {

            TRY(_push_node(parser, *ret, NT_VOID_LITERAL, 0));

            parser->unget = 1;

            TRY(_right_hand_side_expr(parser, &node));

            _ast_push(*ret, node);
            node = 0;
        }
    }
    else {

        parser->unget = 1;

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        TRY(_next_token(parser));

        if (parser->token.sep == ST_COLON) {

            (*ret)->type = NT_SLICE;

            TRY(_next_token(parser));

            parser->unget = 1;

            if (parser->token.sep == ST_SQUARE_CLOSE)
                TRY(_new_node(parser, &node, NT_VOID_LITERAL));
            else
                TRY(_right_hand_side_expr(parser, &node));

            _ast_push(*ret, node);
            node = 0;
        }
        else
            parser->unget = 1;
    }

    TRY(_assume_separator(parser, ST_SQUARE_CLOSE));

_error:
    return error;
}

static int _invocation(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = *ret;
    *ret = 0;

    TRY(_new_node(parser, ret, NT_INVOCATION));

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.sep != ST_PAREN_CLOSE) {

        parser->unget = 1;

        for (;;) {

            TRY(_right_hand_side_expr(parser, &node));

            _ast_push(*ret, node);
            node = 0;

            TRY(_next_token(parser));

            if (parser->token.sep == ST_PAREN_CLOSE)
                break;

            if (parser->token.sep != ST_COMMA) {
                parser->error_str = str_err_expected_comma;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }
        }
    }

_error:
    return error;
}

static int _member_expr(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int       error;
    const int saved_unary_depth = parser->unary_depth;

    error = _primary_expr(parser, ret);

    ++parser->unary_depth;

    while (!error) {
        error = _next_token(parser);

        if (!error) {
            const struct _KOS_TOKEN *token = &parser->token;

            if (token->op == OT_DOT)
                error = _refinement_identifier(parser, ret);

            else if (token->sep == ST_SQUARE_OPEN) {
                if (parser->had_eol && parser->unary_depth == 1) {
                    parser->error_str = str_err_eol_before_sq;
                    error = KOS_ERROR_PARSE_FAILED;
                }
                else
                    error = _refinement_expr(parser, ret);
            }
            else if (token->sep == ST_PAREN_OPEN) {
                if (parser->had_eol && parser->unary_depth == 1) {
                    parser->error_str = str_err_eol_before_par;
                    error = KOS_ERROR_PARSE_FAILED;
                }
                else
                    error = _invocation(parser, ret);
            }
            else {
                parser->unget = 1;
                break;
            }
        }
    }

    assert(parser->unary_depth == saved_unary_depth + 1);
    parser->unary_depth = saved_unary_depth;

    return error;
}

static int _expr_var_const(struct _KOS_PARSER    *parser,
                           int                    allow_in,
                           int                    allow_multi_assignment,
                           struct _KOS_AST_NODE **ret)
{
    struct _KOS_AST_NODE *node = 0;

    int error = KOS_SUCCESS;

    enum _KOS_NODE_TYPE node_type = NT_ASSIGNMENT;

    struct _KOS_TOKEN const_token;

    /* Save token for const detection in for-in expression */
    if (allow_in && parser->token.keyword == KW_CONST)
        const_token = parser->token;
    else
        const_token.keyword = KW_NONE;

    TRY(_new_node(parser, &node, parser->token.keyword == KW_CONST ? NT_CONST : NT_VAR));

    TRY(_next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    TRY(_push_node(parser, node, NT_IDENTIFIER, 0));

    TRY(_next_token(parser));

    if (parser->token.sep == ST_COMMA) {

        if ( ! allow_multi_assignment) {
            parser->error_str = str_err_expected_var_assignment;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        node_type = NT_MULTI_ASSIGNMENT;
    }

    while (parser->token.sep == ST_COMMA) {

        TRY(_next_token(parser));

        if (parser->token.type != TT_IDENTIFIER) {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_push_node(parser, node, NT_IDENTIFIER, 0));

        TRY(_next_token(parser));
    }

    if ((parser->token.keyword != KW_IN || !allow_in) && (parser->token.op != OT_SET)) {
        parser->error_str = str_err_expected_var_assignment;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    /* const is not allowed in for-in expression */
    if (parser->token.keyword == KW_IN) {
        node_type = NT_IN;

        if (const_token.keyword == KW_CONST) {
            parser->token = const_token;
            parser->error_str = str_err_expected_var;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }
    }

    TRY(_new_node(parser, ret, node_type));

    _ast_push(*ret, node);
    node = 0;

    TRY(_right_hand_side_expr(parser, &node));

    _ast_push(*ret, node);
    node = 0;

_error:
    return error;
}

static int _expr_no_var(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    struct _KOS_AST_NODE *node = 0;
    struct _KOS_AST_NODE *lhs  = 0;

    int error = KOS_SUCCESS;

    enum _KOS_NODE_TYPE node_type;

    TRY(_right_hand_side_expr(parser, &node));

    node_type = node->type;

    TRY(_next_token(parser));

    if (parser->token.sep == ST_SEMICOLON || parser->token.sep == ST_PAREN_CLOSE
        || (node_type != NT_IDENTIFIER && node_type != NT_REFINEMENT && node_type != NT_SLICE)
        || (parser->token.sep != ST_COMMA && ! (parser->token.op & OT_ASSIGNMENT) && parser->had_eol))
    {

        parser->unget = 1;

        *ret = node;
        node = 0;
    }
    else {
        int num_assignees = 1;

        TRY(_new_node(parser, &lhs, NT_LEFT_HAND_SIDE));

        _ast_push(lhs, node);
        node = 0;

        while (parser->token.sep == ST_COMMA) {

            ++num_assignees;

            TRY(_member_expr(parser, &node));

            _ast_push(lhs, node);
            node = 0;

            TRY(_next_token(parser));
        }

        if (!(parser->token.op & OT_ASSIGNMENT)) {
            if (num_assignees > 1)
                parser->error_str = str_err_expected_multi_assignment;
            else
                parser->error_str = str_err_expected_semicolon;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        if (parser->token.op != OT_SET && num_assignees > 1) {
            parser->error_str = str_err_expected_multi_assignment;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        if (parser->token.op != OT_SET && node_type == NT_SLICE) {
            parser->error_str = str_err_unsupported_slice_assign;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_new_node(parser, ret, num_assignees > 1 ? NT_MULTI_ASSIGNMENT : NT_ASSIGNMENT));

        _ast_push(*ret, lhs);
        lhs = 0;

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;
    }

_error:
    return error;
}

static int _expr(struct _KOS_PARSER *parser, int allow_in, int allow_var, struct _KOS_AST_NODE **ret)
{
    int error;

    if (allow_var) {
        error = _next_token(parser);

        if (!error) {
            if (parser->token.keyword == KW_VAR || parser->token.keyword == KW_CONST)
                error = _expr_var_const(parser, allow_in, 1, ret);
            else {
                parser->unget = 1;
                error = _expr_no_var(parser, ret);
            }
        }
    }
    else
        error = _expr_no_var(parser, ret);

    return error;
}

static int _expr_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = _expr(parser, 0, 1, ret);

    if (!error)
        error = _assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int _compound_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_assume_separator(parser, ST_CURLY_OPEN));

    TRY(_new_node(parser, ret, NT_SCOPE));

    TRY(_next_token(parser));

    while (parser->token.sep != ST_CURLY_CLOSE) {

        if (parser->token.type == TT_EOF) {
            parser->error_str = str_err_expected_curly_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        parser->unget = 1;

        TRY(_next_statement(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        TRY(_next_token(parser));
    }

_error:
    return error;
}

static int _function_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int                   error = KOS_SUCCESS;
    struct _KOS_AST_NODE *const_node;
    struct _KOS_AST_NODE *fun_node    = 0;
    struct _KOS_TOKEN     fun_keyword = parser->token;

    TRY(_next_token(parser));

    if (parser->token.type == TT_IDENTIFIER) {

        /* To simplify operator selection in the compiler */
        fun_keyword.op = OT_SET;

        TRY(_new_node(parser, ret, NT_ASSIGNMENT));
        (*ret)->token = fun_keyword;

        TRY(_push_node(parser, *ret, NT_CONST, &const_node));
        const_node->token = fun_keyword;

        TRY(_push_node(parser, const_node, NT_IDENTIFIER, 0));

        TRY(_function_literal(parser, 1, &fun_node));

        _ast_push(*ret, fun_node);
        fun_node = 0;
    }
    else {

        _KOS_lexer_unget_token(&parser->lexer, &fun_keyword);
        parser->unget = 0;
        TRY(_expr_stmt(parser, ret));
    }

_error:
    return error;
}

static int _if_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_IF));

    TRY(_right_hand_side_expr(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    TRY(_compound_stmt(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.keyword == KW_ELSE) {

        TRY(_next_token(parser));

        if (parser->token.keyword == KW_IF)
            TRY(_if_stmt(parser, &node));

        else {

            parser->unget = 1;

            TRY(_compound_stmt(parser, &node));
        }

        _ast_push(*ret, node);
        node = 0;
    }
    else
        parser->unget = 1;

_error:
    return error;
}

static int _try_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_TRY));

    TRY(_compound_stmt(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.keyword != KW_CATCH) {
        parser->error_str = str_err_expected_catch;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    if (parser->token.keyword == KW_CATCH) {

        int                   has_paren;
        struct _KOS_AST_NODE *catch_node;
        struct _KOS_AST_NODE *var_node;

        TRY(_push_node(parser, *ret, NT_CATCH, &catch_node));

        TRY(_fetch_optional_paren(parser, &has_paren));

        TRY(_next_token(parser));

        if (parser->token.keyword != KW_VAR && parser->token.keyword != KW_CONST) {
            parser->error_str = str_err_expected_var_or_const;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_push_node(parser, catch_node,
                       parser->token.keyword == KW_VAR ? NT_VAR : NT_CONST,
                       &var_node));

        TRY(_next_token(parser));

        if (parser->token.type != TT_IDENTIFIER) {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_push_node(parser, var_node, NT_IDENTIFIER, 0));

        if (has_paren)
            TRY(_assume_separator(parser, ST_PAREN_CLOSE));

        TRY(_compound_stmt(parser, &node));

        _ast_push(catch_node, node);
        node = 0;

        TRY(_next_token(parser));
    }
    else {

        TRY(_push_node(parser, *ret, NT_EMPTY, 0));
    }

    TRY(_push_node(parser, *ret, NT_EMPTY, 0));

    parser->unget = 1;

_error:
    return error;
}

static int _defer_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *try_node     = 0;
    struct _KOS_AST_NODE *finally_node = 0;

    TRY(_new_node(parser, ret, NT_TRY));

    TRY(_push_node(parser, *ret, NT_SCOPE, &try_node));

    TRY(_push_node(parser, *ret, NT_EMPTY, 0));

    TRY(_compound_stmt(parser, &finally_node));

    TRY(_next_token(parser));

    if (parser->token.type == TT_EOF ||
        parser->token.sep  == ST_CURLY_CLOSE)

        *ret = finally_node;

    else {

        _ast_push(*ret, finally_node);

        do {

            struct _KOS_AST_NODE *node = 0;

            parser->unget = 1;

            TRY(_next_statement(parser, &node));

            _ast_push(try_node, node);

            TRY(_next_token(parser));
        }
        while (parser->token.type != TT_EOF &&
               parser->token.sep  != ST_CURLY_CLOSE);
    }

    parser->unget = 1;

_error:
    return error;
}

static int _gen_fake_const(struct _KOS_PARSER    *parser,
                           struct _KOS_AST_NODE  *parent_node)
{
    int                   error;
    struct _KOS_AST_NODE *node    = 0;
    char*                 name    = 0;
    const size_t          max_len = 32;

    TRY(_push_node(parser, parent_node, NT_CONST, &node));

    TRY(_push_node(parser, node, NT_IDENTIFIER, &node));

    name = (char *)_KOS_mempool_alloc(parser->ast_buf, max_len);

    if ( ! name) {
        error = KOS_ERROR_OUT_OF_MEMORY;
        goto _error;
    }

    snprintf(name, max_len, "%u:%u", parser->token.pos.line, parser->token.pos.column);

    node->token.begin   = name;
    node->token.length  = (unsigned)strlen(name);
    node->token.type    = TT_IDENTIFIER;
    node->token.keyword = KW_NONE;
    node->token.op      = OT_NONE;
    node->token.sep     = ST_NONE;

_error:
    return error;
}

static int _gen_acquire(struct _KOS_PARSER   *parser,
                        struct _KOS_AST_NODE *parent_node,
                        struct _KOS_AST_NODE *const_node)
{
    int error;

    static const char str_acquire[] = "acquire";

    struct _KOS_AST_NODE *if_node = 0;
    struct _KOS_AST_NODE *node    = 0;
    struct _KOS_AST_NODE *id_node = 0;

    assert(const_node);
    assert(const_node->type == NT_CONST);
    assert(const_node->children);

    const_node = const_node->children;
    assert(const_node->type == NT_IDENTIFIER);
    assert( ! const_node->next);

    TRY(_push_node(parser, parent_node, NT_IF, &if_node));

    if_node->token = const_node->token;

    TRY(_push_node(parser, if_node, NT_OPERATOR, &node));

    node->token         = const_node->token;
    node->token.keyword = KW_IN;
    node->token.op      = OT_NONE;
    node->token.sep     = ST_NONE;
    node->token.type    = TT_IDENTIFIER;

    TRY(_push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(_push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_acquire;
    id_node->token.length = sizeof(str_acquire) - 1;

    TRY(_push_node(parser, if_node, NT_SCOPE, &node));

    TRY(_push_node(parser, node, NT_INVOCATION, &node));

    TRY(_push_node(parser, node, NT_REFINEMENT, &node));

    TRY(_push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(_push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_acquire;
    id_node->token.length = sizeof(str_acquire) - 1;

_error:
    return error;
}

static int _gen_release(struct _KOS_PARSER   *parser,
                        struct _KOS_AST_NODE *parent_node,
                        struct _KOS_AST_NODE *const_node)
{
    int error;

    static const char str_release[] = "release";

    struct _KOS_AST_NODE *node    = 0;
    struct _KOS_AST_NODE *id_node = 0;

    assert(const_node);
    assert(const_node->type == NT_CONST);
    assert(const_node->children);

    const_node = const_node->children;
    assert(const_node->type == NT_IDENTIFIER);
    assert( ! const_node->next);

    TRY(_push_node(parser, parent_node, NT_SCOPE, &node));

    TRY(_push_node(parser, node, NT_INVOCATION, &node));

    TRY(_push_node(parser, node, NT_REFINEMENT, &node));

    TRY(_push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(_push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_release;
    id_node->token.length = sizeof(str_release) - 1;

_error:
    return error;
}

static int _with_stmt_continued(struct _KOS_PARSER   *parser,
                                int                   has_paren,
                                struct _KOS_AST_NODE *parent_node)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node     = 0;
    struct _KOS_AST_NODE *try_node = 0;

    if (parser->token.keyword == KW_CONST)
        TRY(_expr_var_const(parser, 0, 0, &node));

    else {

        struct _KOS_AST_NODE *aux_node = 0;

        TRY(_new_node(parser, &node, NT_ASSIGNMENT));

        node->token.op = OT_SET; /* Select correct operator */

        TRY(_gen_fake_const(parser, node));

        parser->unget = 1;
        TRY(_right_hand_side_expr(parser, &aux_node));

        _ast_push(node, aux_node);
    }

    _ast_push(parent_node, node);

    TRY(_gen_acquire(parser, parent_node, node->children));

    TRY(_next_token(parser));

    TRY(_push_node(parser, parent_node, NT_TRY, &try_node));

    if (parser->token.sep == ST_COMMA) {

        struct _KOS_AST_NODE *scope_node = 0;

        TRY(_next_token(parser));

        if (parser->token.keyword == KW_VAR        ||
            parser->token.sep     == ST_COMMA      ||
            parser->token.sep     == ST_CURLY_OPEN ||
            parser->token.sep     == ST_PAREN_CLOSE) {

            parser->error_str = str_err_expected_const_or_expr;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        TRY(_push_node(parser, try_node, NT_SCOPE, &scope_node));

        TRY(_with_stmt_continued(parser, has_paren, scope_node));
    }
    else {

        struct _KOS_AST_NODE *scope_node = 0;

        parser->unget = 1;

        if (has_paren)
            TRY(_assume_separator(parser, ST_PAREN_CLOSE));

        TRY(_compound_stmt(parser, &scope_node));

        _ast_push(try_node, scope_node);
    }

    TRY(_push_node(parser, try_node, NT_EMPTY, 0));

    TRY(_gen_release(parser, try_node, node->children));

_error:
    return error;
}

static int _with_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int has_paren;

    TRY(_new_node(parser, ret, NT_SCOPE));

    TRY(_fetch_optional_paren(parser, &has_paren));

    TRY(_next_token(parser));

    if (parser->token.keyword == KW_VAR ||
        (has_paren && parser->token.sep == ST_PAREN_CLOSE)) {

        parser->error_str = str_err_expected_const_or_expr;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    TRY(_with_stmt_continued(parser, has_paren, *ret));

_error:
    return error;
}

static int _switch_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error       = KOS_SUCCESS;
    int has_default = 0;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_SWITCH));

    TRY(_right_hand_side_expr(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    TRY(_assume_separator(parser, ST_CURLY_OPEN));

    TRY(_next_token(parser));

    while (parser->token.sep != ST_CURLY_CLOSE) {

        struct _KOS_AST_NODE *case_node = 0;

        if (parser->token.type == TT_EOF) {
            parser->error_str = str_err_expected_curly_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }

        if (parser->token.op == OT_MORE) {
            if (has_default) {
                parser->error_str = str_err_duplicate_default;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }

            has_default = 1;

            TRY(_push_node(parser, *ret, NT_DEFAULT, &case_node));

            TRY(_push_node(parser, case_node, NT_EMPTY, 0));
        }
        else {
            TRY(_push_node(parser, *ret, NT_CASE, &case_node));

            parser->unget = 1;

            TRY(_right_hand_side_expr(parser, &node));

            _ast_push(case_node, node);
            node = 0;
        }

        TRY(_next_token(parser));

        if (parser->token.keyword != KW_FALLTHROUGH) {

            parser->unget = 1;

            TRY(_compound_stmt(parser, &node));

            _ast_push(case_node, node);
            node = 0;

            TRY(_next_token(parser));
        }

        if (parser->token.keyword == KW_FALLTHROUGH) {
            TRY(_push_node(parser, case_node, NT_FALLTHROUGH, 0));

            TRY(_assume_separator(parser, ST_SEMICOLON));

            TRY(_next_token(parser));
        }
    }

_error:
    return error;
}

static int _loop_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_WHILE));

    TRY(_push_node(parser, *ret, NT_BOOL_LITERAL, &node));

    node->token.type    = TT_KEYWORD;
    node->token.keyword = KW_TRUE;

    node = 0;

    ++parser->allow_break;

    TRY(_compound_stmt(parser, &node));

    --parser->allow_break;

    _ast_push(*ret, node);
    node = 0;

_error:
    return error;
}

static int _do_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_DO));

    ++parser->allow_break;

    TRY(_compound_stmt(parser, &node));

    --parser->allow_break;

    _ast_push(*ret, node);
    node = 0;

    TRY(_next_token(parser));

    if (parser->token.keyword != KW_WHILE) {
        parser->error_str = str_err_expected_while;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    assert(parser->unary_depth == 0);

    TRY(_right_hand_side_expr(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    TRY(_assume_separator(parser, ST_SEMICOLON));

_error:
    return error;
}

static int _while_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_WHILE));

    TRY(_right_hand_side_expr(parser, &node));

    _ast_push(*ret, node);
    node = 0;

    ++parser->allow_break;

    TRY(_compound_stmt(parser, &node));

    --parser->allow_break;

    _ast_push(*ret, node);
    node = 0;

_error:
    return error;
}

static int _for_expr_list(struct _KOS_PARSER      *parser,
                          int                      allow_in,
                          enum _KOS_SEPARATOR_TYPE end_sep,
                          struct _KOS_AST_NODE   **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, NT_EXPRESSION_LIST));

    TRY(_expr(parser, allow_in, allow_in, &node));

    if (node->type == NT_IN) {

        *ret = node;
        node = 0;
    }
    else {

        _ast_push(*ret, node);
        node = 0;

        for (;;) {

            TRY(_next_token(parser));

            if (parser->token.sep == end_sep) {
                parser->unget = 1;
                break;
            }

            if (parser->token.sep != ST_COMMA)
                switch (end_sep) {
                    case ST_SEMICOLON:
                        parser->error_str = str_err_expected_semicolon;
                        error = KOS_ERROR_PARSE_FAILED;
                        goto _error;
                    case ST_CURLY_OPEN:
                        parser->error_str = str_err_expected_curly_open;
                        error = KOS_ERROR_PARSE_FAILED;
                        goto _error;
                    case ST_PAREN_CLOSE:
                        /* fall through */
                    default:
                        assert(end_sep == ST_PAREN_CLOSE);
                        parser->error_str = str_err_expected_paren_close;
                        error = KOS_ERROR_PARSE_FAILED;
                        goto _error;
                }

            TRY(_expr(parser, 0, allow_in, &node));

            _ast_push(*ret, node);
            node = 0;
        }
    }

_error:
    return error;
}

static int _for_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error  = KOS_SUCCESS;
    int for_in = 0;
    int has_paren;

    struct _KOS_AST_NODE *node       = 0;
    struct _KOS_AST_NODE *scope_node = 0;
    struct _KOS_AST_NODE *for_node   = 0;

    TRY(_new_node(parser, &for_node, NT_FOR));

    TRY(_new_node(parser, &scope_node, NT_SCOPE));

    TRY(_fetch_optional_paren(parser, &has_paren));

    TRY(_next_token(parser));

    if (parser->token.sep == ST_SEMICOLON) {

        *ret = for_node;

        parser->unget = 1;
    }
    else {

        parser->unget = 1;

        TRY(_for_expr_list(parser, 1, ST_SEMICOLON, &node));

        if (node->type == NT_IN) {
            for_in         = 1;
            for_node->type = NT_FOR_IN;
            *ret           = for_node;

            _ast_push(for_node, node);
        }
        else {
            *ret = scope_node;

            _ast_push(scope_node, node);
            _ast_push(scope_node, for_node);
        }

        node = 0;
    }

    if (!for_in) {

        TRY(_assume_separator(parser, ST_SEMICOLON));

        TRY(_next_token(parser));

        if (parser->token.sep == ST_SEMICOLON)

            TRY(_push_node(parser, for_node, NT_EMPTY, 0));

        else {

            parser->unget = 1;

            TRY(_right_hand_side_expr(parser, &node));

            _ast_push(for_node, node);
            node = 0;

            TRY(_next_token(parser));

            if (parser->token.sep != ST_SEMICOLON) {
                parser->error_str = str_err_expected_semicolon;
                error = KOS_ERROR_PARSE_FAILED;
                goto _error;
            }
        }

        TRY(_next_token(parser));

        if ((has_paren  && parser->token.sep == ST_PAREN_CLOSE) ||
            (!has_paren && parser->token.sep == ST_CURLY_OPEN)) {

            TRY(_push_node(parser, for_node, NT_EMPTY, 0));

            parser->unget = 1;
        }
        else {

            parser->unget = 1;

            TRY(_for_expr_list(parser, 0,
                               has_paren ? ST_PAREN_CLOSE : ST_CURLY_OPEN,
                               &node));

            _ast_push(for_node, node);
            node = 0;
        }
    }

    if (has_paren)
        TRY(_assume_separator(parser, ST_PAREN_CLOSE));

    ++parser->allow_break;

    TRY(_compound_stmt(parser, &node));

    --parser->allow_break;

    _ast_push(for_node, node);
    node = 0;

_error:
    return error;
}

static int _continue_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    if (!parser->allow_break) {
        parser->error_str = str_err_unexpected_continue;
        error = KOS_ERROR_PARSE_FAILED;
    }

    if (!error)
        error = _new_node(parser, ret, NT_CONTINUE);

    if (!error)
        error = _assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int _break_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    if (!parser->allow_break) {
        parser->error_str = str_err_unexpected_break;
        error = KOS_ERROR_PARSE_FAILED;
    }

    if (!error)
        error = _new_node(parser, ret, NT_BREAK);

    if (!error)
        error = _assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int _import_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    TRY(_new_node(parser, ret, NT_IMPORT));

    TRY(_next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    TRY(_push_node(parser, *ret, NT_IDENTIFIER, 0));

    TRY(_next_token(parser));

    if (parser->token.op == OT_DOT) {

        TRY(_next_token(parser));

        if (parser->token.op == OT_MUL || parser->token.type == TT_IDENTIFIER || parser->token.type == TT_KEYWORD)
            TRY(_push_node(parser, *ret, NT_IDENTIFIER, 0));
        else {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto _error;
        }
    }
    else
        parser->unget = 1;

    TRY(_assume_separator(parser, ST_SEMICOLON));

_error:
    return error;
}

static int _try_import_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    TRY(_new_node(parser, ret, NT_TRY_IMPORT));

    TRY(_next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    TRY(_push_node(parser, *ret, NT_IDENTIFIER, 0));

    TRY(_assume_separator(parser, ST_SEMICOLON));

_error:
    return error;
}

static int _end_of_return(const struct _KOS_PARSER *parser)
{
    const struct _KOS_TOKEN *token = &parser->token;

    if (token->sep  == ST_SEMICOLON   ||
        token->sep  == ST_CURLY_CLOSE ||
        token->type == TT_EOF)

        return 1;

    return 0;
}

static int _return_throw_assert_stmt(struct _KOS_PARSER    *parser,
                                     enum _KOS_NODE_TYPE    type,
                                     struct _KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    struct _KOS_AST_NODE *node = 0;

    TRY(_new_node(parser, ret, type));

    TRY(_next_token(parser));

    if (type == NT_RETURN && _end_of_return(parser)) {
        if (parser->token.sep != ST_SEMICOLON)
            parser->unget = 1;
    }
    else {

        parser->unget = 1;

        TRY(_right_hand_side_expr(parser, &node));

        _ast_push(*ret, node);
        node = 0;

        if (type == NT_ASSERT) {

            TRY(_next_token(parser));

            TRY(_push_node(parser, *ret, NT_LANDMARK, 0));

            parser->unget = 1;
        }

        TRY(_assume_separator(parser, ST_SEMICOLON));
    }

_error:
    return error;
}

static int _expr_or_compound_stmt(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    struct _KOS_TOKEN saved_token = parser->token;

    int error   = KOS_SUCCESS;
    int is_expr = 0;

    TRY(_next_token(parser));

    if (parser->token.type == TT_EOF) {
        parser->error_str = str_err_expected_expr_or_curly;
        error = KOS_ERROR_PARSE_FAILED;
        goto _error;
    }

    if (parser->token.type == TT_STRING     ||
        parser->token.type == TT_IDENTIFIER ||
        parser->token.type == TT_KEYWORD) {

        TRY(_next_token(parser));

        if (parser->token.sep == ST_COLON)
            is_expr = 1;
    }

    _KOS_lexer_unget_token(&parser->lexer, &saved_token);
    parser->unget = 0;

    if (is_expr)
        TRY(_expr_stmt(parser, ret));
    else
        TRY(_compound_stmt(parser, ret));

_error:
    return error;
}

static int _next_statement(struct _KOS_PARSER *parser, struct _KOS_AST_NODE **ret)
{
    int error = _next_token(parser);

    if (!error) {
        const struct _KOS_TOKEN *token = &parser->token;

        assert(parser->unary_depth == 0);

        switch (token->keyword) {
            case KW_FUN:
                error = _function_stmt(parser, ret);
                break;
            case KW_IF:
                error = _if_stmt(parser, ret);
                break;
            case KW_TRY:
                error = _try_stmt(parser, ret);
                break;
            case KW_DEFER:
                error = _defer_stmt(parser, ret);
                break;
            case KW_WITH:
                error = _with_stmt(parser, ret);
                break;
            case KW_SWITCH:
                error = _switch_stmt(parser, ret);
                break;
            case KW_LOOP:
                error = _loop_stmt(parser, ret);
                break;
            case KW_DO:
                error = _do_stmt(parser, ret);
                break;
            case KW_WHILE:
                error = _while_stmt(parser, ret);
                break;
            case KW_FOR:
                error = _for_stmt(parser, ret);
                break;
            case KW_CONTINUE:
                error = _continue_stmt(parser, ret);
                break;
            case KW_BREAK:
                error = _break_stmt(parser, ret);
                break;
            case KW_RETURN:
                error = _return_throw_assert_stmt(parser, NT_RETURN, ret);
                break;
            case KW_THROW:
                error = _return_throw_assert_stmt(parser, NT_THROW, ret);
                break;
            case KW_ASSERT:
                error = _return_throw_assert_stmt(parser, NT_ASSERT, ret);
                break;
            case KW_IMPORT:
                parser->error_str = str_err_unexpected_import;
                error = KOS_ERROR_PARSE_FAILED;
                break;
            case KW_NONE:
                if (token->sep == ST_SEMICOLON) {
                    error = _new_node(parser, ret, NT_EMPTY);
                    break;
                }
                else if (token->sep == ST_CURLY_OPEN) {
                    error = _expr_or_compound_stmt(parser, ret);
                    break;
                }
                else if (token->type == TT_EOF) {
                    *ret = 0;
                    break;
                }
                /* fall through */
            default:
                parser->unget = 1;
                error = _expr_stmt(parser, ret);
                break;
        }
    }

    return error;
}

static int _handle_imports(struct _KOS_PARSER *parser, struct _KOS_AST_NODE *root)
{
    int error = _next_token(parser);

    while (!error) {

        const struct _KOS_TOKEN *token = &parser->token;

        if (token->keyword == KW_IMPORT) {

            struct _KOS_AST_NODE *node = 0;

            error = _import_stmt(parser, &node);

            if (!error)
                _ast_push(root, node);
            else
                break;
        }
        else if (token->keyword == KW_TRY) {

            struct _KOS_TOKEN saved_token = parser->token;

            error = _next_token(parser);

            if (!error) {

                if (parser->token.keyword == KW_IMPORT) {

                    struct _KOS_AST_NODE *node = 0;

                    error = _try_import_stmt(parser, &node);

                    if (!error)
                        _ast_push(root, node);
                    else
                        break;
                }
                else {

                    _KOS_lexer_unget_token(&parser->lexer, &saved_token);
                    parser->unget = 0;
                    break;
                }
            }
            else
                break;
        }
        else if (token->type == TT_EOF)
            break;
        else if (token->sep != ST_SEMICOLON) {
            parser->unget = 1;
            break;
        }

        error = _next_token(parser);
    }

    return error;
}

void _KOS_parser_init(struct _KOS_PARSER  *parser,
                      struct _KOS_MEMPOOL *mempool,
                      unsigned             file_id,
                      const char          *begin,
                      const char          *end)
{
    _KOS_lexer_init(&parser->lexer, file_id, begin, end);

    parser->ast_buf       = mempool;
    parser->error_str     = 0;
    parser->unget         = 0;
    parser->had_eol       = 0;
    parser->allow_break   = 0;
    parser->unary_depth   = 0;

    parser->token.length  = 0;
    parser->token.pos     = parser->lexer.pos;
    parser->token.type    = TT_EOF;
    parser->token.keyword = KW_NONE;
    parser->token.op      = OT_NONE;
    parser->token.sep     = ST_NONE;
}

int _KOS_parser_parse(struct _KOS_PARSER    *parser,
                      struct _KOS_AST_NODE **ret)
{
    struct _KOS_AST_NODE *root = 0;
    struct _KOS_AST_NODE *node = 0;

    int error = KOS_SUCCESS;

    TRY(_new_node(parser, &root, NT_SCOPE));

    TRY(_handle_imports(parser, root));

    TRY(_next_statement(parser, &node));

    while (node) {

        _ast_push(root, node);
        node = 0;

        TRY(_next_statement(parser, &node));
    }

_error:
    if (!error)
        *ret = root;
    else if (error == KOS_ERROR_SCANNING_FAILED)
        parser->error_str = parser->lexer.error_str;

    return error;
}

void _KOS_parser_destroy(struct _KOS_PARSER *parser)
{
    parser->ast_buf = 0;
}

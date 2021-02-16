/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "kos_parser.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "kos_ast.h"
#include "kos_config.h"
#include "kos_perf.h"
#include "kos_try.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char str_err_cannot_expand_named_arg[]   = "named arguments cannot be expanded";
static const char str_err_duplicate_default[]         = "multiple 'default' labels in one switch";
static const char str_err_eol_before_par[]            = "ambiguous syntax: end of line before '(' - consider adding a ';'";
static const char str_err_eol_before_sq[]             = "ambiguous syntax: end of line before '[' - consider adding a ';'";
static const char str_err_eol_before_op[]             = "ambiguous syntax: end of line before operator - consider adding a ';'";
static const char str_err_exceeded_ast_depth[]        = "expression depth exceeded";
static const char str_err_expected_assignable[]       = "expected identifier, refinement, slice or 'void' for multi-assignment";
static const char str_err_expected_case[]             = "expected 'case'";
static const char str_err_expected_case_or_default[]  = "expected 'case' or 'default'";
static const char str_err_expected_case_statements[]  = "expected statements after 'case'";
static const char str_err_expected_catch[]            = "expected 'catch'";
static const char str_err_expected_colon[]            = "expected ':'";
static const char str_err_expected_comma[]            = "expected ','";
static const char str_err_expected_const_or_expr[]    = "expected 'const' or expression";
static const char str_err_expected_curly_close[]      = "expected '}'";
static const char str_err_expected_curly_open[]       = "expected '{'";
static const char str_err_expected_expression[]       = "expected expression";
static const char str_err_expected_for_in[]           = "expected 'in' expression";
static const char str_err_expected_ident_or_str[]     = "expected identifier or string literal";
static const char str_err_expected_identifier[]       = "expected identifier";
static const char str_err_expected_invocation[]       = "expected invocation";
static const char str_err_expected_lambda_op[]        = "expected '=>'";
static const char str_err_expected_member_expr[]      = "expected literal, identifier or '('";
static const char str_err_expected_multi_assignment[] = "expected '=' after comma-separated variables or members";
static const char str_err_expected_named_arg[]        = "expected named argument";
static const char str_err_expected_named_assignment[] = "expected '=' after named argument";
static const char str_err_expected_param_default[]    = "expected default value for parameter";
static const char str_err_expected_paren_close[]      = "expected ')'";
static const char str_err_expected_paren_open[]       = "expected '('";
static const char str_err_expected_semicolon[]        = "expected ';'";
static const char str_err_expected_square_close[]     = "expected ']'";
static const char str_err_expected_string[]           = "unexpected interpolated string";
static const char str_err_expected_this[]             = "expected 'this' inside a constructor function";
static const char str_err_expected_var_or_const[]     = "expected 'var' or 'const'";
static const char str_err_expected_var_assignment[]   = "expected '=' in variable declaration";
static const char str_err_expected_while[]            = "expected 'while'";
static const char str_err_fallthrough_in_last_case[]  = "unexpected 'fallthrough' statement in last switch case";
static const char str_err_invalid_public[]            = "incorrect 'public' declaration, must be a constant, variable, function or class";
static const char str_err_mixed_operators[]           = "mixed operators, consider using parentheses";
static const char str_err_too_many_non_default[]      = "too many non-default arguments (more than 255) preceding an argument with default value";
static const char str_err_unexpected_break[]          = "unexpected 'break' statement; can only be used inside a loop or switch";
static const char str_err_unexpected_continue[]       = "unexpected 'continue' statement; can only be used inside a loop";
static const char str_err_unexpected_ctor[]           = "constructor already defined for this class";
static const char str_err_unexpected_import[]         = "unexpected 'import' statement";
static const char str_err_unexpected_fallthrough[]    = "unexpected 'fallthrough' statement; can only be used inside a switch";
static const char str_err_unexpected_public[]         = "'public' declaration can only occur in global scope";
static const char str_err_unexpected_super[]          = "unexpected 'super' literal; can only be used inside a derived class member function";
static const char str_err_unexpected_super_ctor[]     = "'super()' constructor can only be invoked from another constructor";
static const char str_err_unsupported_slice_assign[]  = "unsupported assignment to slice, expected '='";
static const char str_err_yield_in_constructor[]      = "'yield' not allowed in constructors";

static int next_statement(KOS_PARSER *parser, KOS_AST_NODE **ret);
static int member_expr(KOS_PARSER *parser, KOS_AST_NODE **ret);
static int right_hand_side_expr(KOS_PARSER *parser, KOS_AST_NODE **ret);
static int compound_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret);
static int do_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret);

static int next_token(KOS_PARSER *parser)
{
    int error = KOS_SUCCESS;

    if ( ! parser->unget) {

        KOS_TOKEN_TYPE type;
        char           had_eol = 0;

        for (;;) {

            error = kos_lexer_next_token(&parser->lexer, NT_ANY, &parser->token);

            if (error)
                break;

            type = (KOS_TOKEN_TYPE)parser->token.type;

            if (type == TT_COMMENT && parser->token.line < parser->lexer.pos.line)
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

static int is_implicit_semicolon(KOS_PARSER *parser)
{
    const KOS_TOKEN *token = &parser->token;

    return (token->sep != ST_SEMICOLON &&
            (parser->had_eol || token->sep == ST_CURLY_CLOSE || token->type == TT_EOF))
           ? 1 : 0;
}

static int assume_separator(KOS_PARSER *parser, KOS_SEPARATOR_TYPE sep)
{
    int error = next_token(parser);

    if (!error) {
        const KOS_TOKEN *token = &parser->token;

        if ((KOS_SEPARATOR_TYPE)token->sep != sep) {

            switch (sep) {
                case ST_COLON:
                    parser->error_str = str_err_expected_colon;
                    error = KOS_ERROR_PARSE_FAILED;
                    break;
                case ST_SEMICOLON:
                    if (is_implicit_semicolon(parser))
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
                case ST_CURLY_CLOSE:
                    parser->error_str = str_err_expected_curly_close;
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

static int increase_ast_depth(KOS_PARSER *parser)
{
    int error = KOS_SUCCESS;

    ++parser->ast_depth;

    if (parser->ast_depth > KOS_MAX_AST_DEPTH) {
        parser->error_str = str_err_exceeded_ast_depth;
        error = KOS_ERROR_PARSE_FAILED;
    }

    return error;
}

static int new_node(KOS_PARSER    *parser,
                    KOS_AST_NODE **node,
                    KOS_NODE_TYPE  type)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *ast_node;

    assert(parser->ast_buf);

    ast_node = (KOS_AST_NODE *)KOS_mempool_alloc(parser->ast_buf, sizeof(KOS_AST_NODE));

    if (ast_node) {
        memset(ast_node, 0, sizeof(*ast_node));

        ast_node->token = parser->token;
        ast_node->type  = type;

        *node = ast_node;
    }
    else
        error = KOS_ERROR_OUT_OF_MEMORY;

    return error;
}

static void ast_push(KOS_AST_NODE *parent, KOS_AST_NODE *child)
{
    if (parent->u.last_child)
        parent->u.last_child->next = child;
    else
        parent->children = child;
    parent->u.last_child = child;
}

static int push_node(KOS_PARSER    *parser,
                     KOS_AST_NODE  *node,
                     KOS_NODE_TYPE  type,
                     KOS_AST_NODE **ret)
{
    KOS_AST_NODE *created_node = KOS_NULL;

    int error = new_node(parser, &created_node, type);

    ast_push(node, created_node);

    if (!error && ret)
        *ret = created_node;

    return error;
}

static int fetch_optional_paren(KOS_PARSER *parser, int *was_paren)
{
    int error = next_token(parser);

    if (!error) {

        *was_paren = parser->token.sep == ST_PAREN_OPEN;

        if (parser->token.sep != ST_PAREN_OPEN)
            parser->unget = 1;
    }

    return error;
}

static int set_function_name(KOS_PARSER   *parser,
                             KOS_AST_NODE *node,
                             KOS_TOKEN    *token,
                             int           can_self_refer)
{
    int error = KOS_SUCCESS;

    if (node->type == NT_CLASS_LITERAL) {

        node = node->children;
        assert(node);
        node = node->next;
        assert(node);
        node = node->next;
        assert(node);
        assert( ! node->next);
        assert(node->type == NT_CONSTRUCTOR_LITERAL);
    }

    assert(node->type == NT_FUNCTION_LITERAL ||
           node->type == NT_CONSTRUCTOR_LITERAL);

    node = node->children;
    assert(node);
    assert(node->type == NT_NAME);

    assert(token->type == TT_IDENTIFIER ||
           token->type == TT_KEYWORD    ||
           token->type == TT_STRING);

    if (can_self_refer) {
        assert(token->type == TT_IDENTIFIER);
        node->type = NT_NAME_CONST;
    }

    TRY(push_node(parser,
                  node,
                  token->type == TT_STRING ? NT_STRING_LITERAL : NT_IDENTIFIER,
                  &node));

    node->token = *token;

cleanup:
    return error;
}

static int parameters(KOS_PARSER    *parser,
                      KOS_AST_NODE **ret)
{
    int error        = KOS_SUCCESS;
    int num_non_def  = 0;
    int has_defaults = 0;

    TRY(new_node(parser, ret, NT_PARAMETERS));

    TRY(next_token(parser));

    while (parser->token.type == TT_IDENTIFIER) {

        KOS_NODE_TYPE node_type = NT_ASSIGNMENT;
        KOS_AST_NODE *node      = KOS_NULL;

        TRY(new_node(parser, &node, NT_IDENTIFIER));

        TRY(next_token(parser));

        if (parser->token.op == OT_ASSIGNMENT) {

            KOS_AST_NODE *assign_node;

            has_defaults = 1;

            if (num_non_def > 255) {
                parser->error_str = str_err_too_many_non_default;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }

            TRY(push_node(parser, *ret, NT_ASSIGNMENT, &assign_node));

            ast_push(assign_node, node);
            node = KOS_NULL;

            TRY(right_hand_side_expr(parser, &node));

            ast_push(assign_node, node);
            node = KOS_NULL;

            TRY(next_token(parser));
        }
        else if (parser->token.op == OT_MORE) {

            KOS_AST_NODE *new_arg;

            node_type = NT_ELLIPSIS;

            TRY(push_node(parser, *ret, node_type, &new_arg));

            ast_push(new_arg, node);
            node = KOS_NULL;

            TRY(next_token(parser));
        }
        else {
            ++num_non_def;

            if (has_defaults) {
                parser->error_str = str_err_expected_param_default;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }
        }

        if (node) {
            ast_push(*ret, node);
            node = KOS_NULL;
        }

        if (node_type == NT_ELLIPSIS)
            break;

        if (parser->token.sep == ST_COMMA)
            TRY(next_token(parser));

        else if (parser->token.sep != ST_PAREN_CLOSE) {
            parser->error_str = str_err_expected_paren_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }

    parser->unget = 1;
    TRY(assume_separator(parser, ST_PAREN_CLOSE));

cleanup:
    return error;
}

static void save_function_state(KOS_PARSER       *parser,
                                KOS_PARSER_STATE *state)
{
    *state = parser->state;

    parser->state.last_fallthrough  = KOS_NULL;
    parser->state.unary_depth       = 0;
    parser->state.allow_continue    = 0;
    parser->state.allow_break       = 0;
    parser->state.allow_fallthrough = 0;
    parser->state.in_constructor    = 0;
    parser->state.in_derived_class  = 0;
    parser->state.in_class_member   = 0;
}

static void restore_function_state(KOS_PARSER       *parser,
                                   KOS_PARSER_STATE *state)
{
    parser->state = *state;
}

static int function_literal(KOS_PARSER      *parser,
                            KOS_KEYWORD_TYPE keyword,
                            KOS_AST_NODE   **ret)
{
    int        error        = KOS_SUCCESS;
    const char constructor  = keyword == KW_CONSTRUCTOR;
    const char class_member = parser->state.in_derived_class;

    KOS_AST_NODE    *node = KOS_NULL;
    KOS_AST_NODE    *args;
    KOS_PARSER_STATE state;

    save_function_state(parser, &state);

    parser->state.in_constructor = constructor;

    TRY(new_node(parser, ret,
                 constructor ? NT_CONSTRUCTOR_LITERAL : NT_FUNCTION_LITERAL));

    TRY(push_node(parser, *ret, NT_NAME, KOS_NULL));

    TRY(next_token(parser));

    if (parser->token.sep == ST_PAREN_OPEN) {

        TRY(parameters(parser, &args));

        ast_push(*ret, args);

        TRY(next_token(parser));
    }
    else
        TRY(push_node(parser, *ret, NT_PARAMETERS, &args));

    parser->unget = 1;

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    parser->state.in_class_member = class_member;

    TRY(compound_stmt(parser, &node));

    ast_push(*ret, node);

    assert(parser->token.sep == ST_CURLY_CLOSE);

    TRY(push_node(parser, node, NT_RETURN, &node));

    TRY(push_node(parser, node, constructor ? NT_THIS_LITERAL : NT_VOID_LITERAL, KOS_NULL));

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    assert(parser->state.unary_depth == 0);

cleanup:
    restore_function_state(parser, &state);

    return error;
}

static int is_lambda_literal(KOS_PARSER *parser,
                             int        *is_lambda)
{
    int       error       = KOS_SUCCESS;
    KOS_TOKEN saved_token = parser->token;

    assert(parser->token.sep == ST_PAREN_OPEN);

    *is_lambda = 0;

    TRY(next_token(parser));

    if (parser->token.sep == ST_PAREN_CLOSE) {

        TRY(next_token(parser));

        if (parser->token.op == OT_LAMBDA)
            *is_lambda = 1;
    }
    else if (parser->token.type == TT_IDENTIFIER) {

        TRY(next_token(parser));

        if (parser->token.op  == OT_ASSIGNMENT ||
            parser->token.op  == OT_MORE       ||
            parser->token.sep == ST_COMMA)

            *is_lambda = 1;

        else if (parser->token.sep == ST_PAREN_CLOSE) {

            TRY(next_token(parser));

            if (parser->token.op == OT_LAMBDA)
                *is_lambda = 1;
        }
    }

cleanup:
    if ( ! error) {
        kos_lexer_unget_token(&parser->lexer, &saved_token);
        parser->unget = 0;
        error = next_token(parser);
    }

    return error;
}

static int lambda_literal_body(KOS_PARSER    *parser,
                               KOS_AST_NODE  *args,
                               KOS_AST_NODE **ret)
{
    int              error       = KOS_SUCCESS;
    KOS_AST_NODE    *node        = KOS_NULL;
    KOS_AST_NODE    *return_node = KOS_NULL;
    KOS_PARSER_STATE state;

    save_function_state(parser, &state);

    assert(parser->token.op == OT_LAMBDA);
    assert(args && args->type == NT_PARAMETERS);

    TRY(new_node(parser, ret, NT_FUNCTION_LITERAL));

    TRY(push_node(parser, *ret, NT_NAME, KOS_NULL));

    ast_push(*ret, args);

    parser->state.unary_depth = 1;

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    TRY(push_node(parser, *ret, NT_SCOPE, &node));

    TRY(push_node(parser, node, NT_RETURN, &return_node));

    TRY(right_hand_side_expr(parser, &node));

    ast_push(return_node, node);

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    assert(parser->state.unary_depth == 1);

cleanup:
    restore_function_state(parser, &state);

    return error;
}

static int lambda_literal(KOS_PARSER    *parser,
                          KOS_AST_NODE **ret)
{
    KOS_AST_NODE *args  = KOS_NULL;
    int           error = KOS_SUCCESS;

    TRY(parameters(parser, &args));

    TRY(next_token(parser));

    if (parser->token.op != OT_LAMBDA) {
        parser->error_str = str_err_expected_lambda_op;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    TRY(lambda_literal_body(parser, args, ret));

cleanup:
    return error;
}

static int gen_empty_constructor(KOS_PARSER    *parser,
                                 KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_CONSTRUCTOR_LITERAL));

    TRY(push_node(parser, *ret, NT_NAME, KOS_NULL));

    TRY(next_token(parser));

    TRY(push_node(parser, *ret, NT_PARAMETERS, KOS_NULL));

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    TRY(push_node(parser, *ret, NT_SCOPE, &node));

    TRY(push_node(parser, node, NT_RETURN, &node));

    TRY(push_node(parser, node, NT_THIS_LITERAL, KOS_NULL));

    TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

    parser->unget = 1;

cleanup:
    return error;
}

static int class_literal(KOS_PARSER    *parser,
                         KOS_AST_NODE **ret)
{
    int           error           = KOS_SUCCESS;
    int           had_constructor = 0;
    KOS_AST_NODE *members_node    = KOS_NULL;
    KOS_AST_NODE *empty_ctor      = KOS_NULL;

    assert( ! parser->state.in_derived_class);

    TRY(new_node(parser, ret, NT_CLASS_LITERAL));

    TRY(gen_empty_constructor(parser, &empty_ctor));

    TRY(next_token(parser));

    if (parser->token.keyword == KW_EXTENDS) {
        KOS_AST_NODE *extends_node = KOS_NULL;

        TRY(member_expr(parser, &extends_node));

        ast_push(*ret, extends_node);

        parser->state.in_derived_class = 1;
    }
    else {
        TRY(push_node(parser, *ret, NT_EMPTY, KOS_NULL));

        parser->unget = 1;
    }

    TRY(assume_separator(parser, ST_CURLY_OPEN));

    TRY(push_node(parser, *ret, NT_OBJECT_LITERAL, &members_node));

    for (;;) {

        TRY(next_token(parser));

        if (parser->token.keyword == KW_CONSTRUCTOR) {

            KOS_AST_NODE *ctor_node = KOS_NULL;

            if (had_constructor) {
                parser->error_str = str_err_unexpected_ctor;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }

            had_constructor = 1;

            TRY(function_literal(parser, (KOS_KEYWORD_TYPE)parser->token.keyword, &ctor_node));

            ast_push(*ret, ctor_node);
        }
        else if (parser->token.type == TT_IDENTIFIER || parser->token.type == TT_KEYWORD) {

            KOS_AST_NODE *prop_node      = KOS_NULL;
            KOS_AST_NODE *fun_node       = KOS_NULL;
            KOS_TOKEN     fun_name_token = parser->token;

            TRY(push_node(parser, members_node, NT_PROPERTY, &prop_node));

            TRY(push_node(parser, prop_node, NT_STRING_LITERAL, KOS_NULL));

            TRY(function_literal(parser, KW_FUN, &fun_node));

            TRY(set_function_name(parser, fun_node, &fun_name_token, 0));

            ast_push(prop_node, fun_node);
        }
        else {
            parser->unget = 1;
            break;
        }
    }

    if ( ! had_constructor)
        ast_push(*ret, empty_ctor);

    TRY(assume_separator(parser, ST_CURLY_CLOSE));

cleanup:
    parser->state.in_derived_class = 0;

    return error;
}

static int interpolated_string(KOS_PARSER    *parser,
                               KOS_AST_NODE **ret)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *node  = KOS_NULL;

    TRY(new_node(parser, ret, NT_INTERPOLATED_STRING));

    TRY(new_node(parser, &node, NT_STRING_LITERAL));

    ast_push(*ret, node);
    node = KOS_NULL;

    do {

        TRY(right_hand_side_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        kos_lexer_unget_token(&parser->lexer, &parser->token);
        parser->unget = 0;

        TRY(kos_lexer_next_token(&parser->lexer, NT_CONTINUE_STRING, &parser->token));
        parser->unget = 0;

        assert(parser->token.type == TT_STRING_OPEN ||
               parser->token.type == TT_STRING);

        TRY(new_node(parser, &node, NT_STRING_LITERAL));

        ast_push(*ret, node);
        node = KOS_NULL;
    }
    while (parser->token.type != TT_STRING);

cleanup:
    return error;
}

static int array_literal(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_ARRAY_LITERAL));

    TRY(next_token(parser));

    while (parser->token.sep != ST_SQUARE_CLOSE) {

        parser->unget = 1;

        TRY(right_hand_side_expr(parser, &node));

        TRY(next_token(parser));

        if (parser->token.op == OT_MORE) {

            KOS_AST_NODE *expanded = node;

            node = KOS_NULL;
            TRY(new_node(parser, &node, NT_EXPAND));

            ast_push(node, expanded);

            TRY(next_token(parser));
        }

        ast_push(*ret, node);
        node = KOS_NULL;

        if (parser->token.sep == ST_COMMA)
            TRY(next_token(parser));
        else if (parser->token.sep != ST_SQUARE_CLOSE) {
            parser->error_str = str_err_expected_square_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }

cleanup:
    return error;
}

static int object_literal(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int comma = 1;

    KOS_AST_NODE *node = KOS_NULL;
    KOS_AST_NODE *prop = KOS_NULL;

    TRY(new_node(parser, ret, NT_OBJECT_LITERAL));

    for (;;) {

        KOS_TOKEN_TYPE prop_name_type;

        TRY(next_token(parser));

        if (parser->token.sep == ST_COMMA) {
            if (comma == 1) {
                parser->error_str = str_err_expected_ident_or_str;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }
            comma = 1;
            continue;
        }
        else if (parser->token.sep == ST_CURLY_CLOSE)
            break;

        if (!comma) {
            parser->error_str = str_err_expected_comma;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        parser->unget = 1;

        TRY(push_node(parser, *ret, NT_PROPERTY, &prop));

        TRY(next_token(parser));

        prop_name_type = (KOS_TOKEN_TYPE)parser->token.type;

        if (prop_name_type == TT_STRING)
            TRY(push_node(parser, prop, NT_STRING_LITERAL, KOS_NULL));
        else if (prop_name_type == TT_STRING_OPEN) {
            parser->error_str = str_err_expected_string;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
        else if (prop_name_type == TT_IDENTIFIER || prop_name_type == TT_KEYWORD)
            TRY(push_node(parser, prop, NT_STRING_LITERAL, KOS_NULL));
        else {
            parser->error_str = str_err_expected_ident_or_str;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(assume_separator(parser, ST_COLON));

        TRY(right_hand_side_expr(parser, &node));

        if ((node->type == NT_FUNCTION_LITERAL    ||
             node->type == NT_CONSTRUCTOR_LITERAL ||
             node->type == NT_CLASS_LITERAL) && prop_name_type != TT_STRING_OPEN) {

            TRY(set_function_name(parser, node, &prop->children->token, 0));
        }

        ast_push(prop, node);
        node = KOS_NULL;

        comma = 0;
    }

cleanup:
    return error;
}

static int primary_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int       error             = next_token(parser);
    const int saved_unary_depth = parser->state.unary_depth;

    ++parser->state.unary_depth;

    if (!error) {
        const KOS_TOKEN *token = &parser->token;

        switch (token->type) {
            case TT_NUMERIC:
                error = new_node(parser, ret, NT_NUMERIC_LITERAL);
                break;
            case TT_STRING:
                error = new_node(parser, ret, NT_STRING_LITERAL);
                break;
            case TT_STRING_OPEN:
                error = interpolated_string(parser, ret);
                break;
            case TT_IDENTIFIER:
                error = new_node(parser, ret, NT_IDENTIFIER);
                if ( ! error)
                    error = next_token(parser);
                if ( ! error) {
                    if (token->op == OT_LAMBDA) {
                        KOS_AST_NODE *args = KOS_NULL;
                        error = new_node(parser, &args, NT_PARAMETERS);
                        if ( ! error) {
                            ast_push(args, *ret);
                            error = lambda_literal_body(parser, args, ret);
                        }
                    }
                    else
                        parser->unget = 1;
                }
                break;
            case TT_KEYWORD:
                switch (token->keyword) {
                    case KW_FUN:
                        error = function_literal(parser, (KOS_KEYWORD_TYPE)token->keyword, ret);
                        break;
                    case KW_CLASS:
                        error = class_literal(parser, ret);
                        break;
                    case KW_THIS:
                        error = new_node(parser, ret, NT_THIS_LITERAL);
                        break;
                    case KW_SUPER:
                        if (parser->state.in_class_member)
                            error = new_node(parser, ret, NT_SUPER_PROTO_LITERAL);
                        else {
                            parser->error_str = str_err_unexpected_super;
                            error = KOS_ERROR_PARSE_FAILED;
                        }
                        break;
                    case KW_LINE:
                        error = new_node(parser, ret, NT_LINE_LITERAL);
                        break;
                    case KW_TRUE:
                        /* fall through */
                    case KW_FALSE:
                        error = new_node(parser, ret, NT_BOOL_LITERAL);
                        break;
                    case KW_VOID:
                        error = new_node(parser, ret, NT_VOID_LITERAL);
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
                        error = array_literal(parser, ret);
                        break;
                    case ST_CURLY_OPEN:
                        error = object_literal(parser, ret);
                        break;
                    case ST_PAREN_OPEN: {
                        int is_lambda = 0;
                        error = is_lambda_literal(parser, &is_lambda);
                        if (!error) {
                            if (is_lambda)
                                error = lambda_literal(parser, ret);
                            else {
                                error = right_hand_side_expr(parser, ret);
                                if (!error)
                                    error = assume_separator(parser, ST_PAREN_CLOSE);
                            }
                        }
                        break;
                    }
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

    assert(parser->state.unary_depth == saved_unary_depth + 1);
    parser->state.unary_depth = saved_unary_depth;

    return error;
}

static int unary_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int           error             = KOS_SUCCESS;
    const int     saved_unary_depth = parser->state.unary_depth;
    KOS_AST_NODE *node              = KOS_NULL;

    TRY(next_token(parser));

    if ((parser->token.op & OT_UNARY)      ||
        parser->token.keyword == KW_TYPEOF ||
        parser->token.keyword == KW_DELETE) {

        TRY(increase_ast_depth(parser));

        ++parser->state.unary_depth;

        TRY(new_node(parser, ret, NT_OPERATOR));

        TRY(unary_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        --parser->ast_depth;

        assert(parser->state.unary_depth == saved_unary_depth + 1);
    }
    else {

        parser->unget = 1;

        TRY(member_expr(parser, ret));

        assert(parser->state.unary_depth == saved_unary_depth);
    }

cleanup:
    parser->state.unary_depth = saved_unary_depth;

    return error;
}

static int arithm_bitwise_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;
    KOS_AST_NODE *aux  = KOS_NULL;

    TRY(unary_expr(parser, &node));

    TRY(next_token(parser));

    if ((parser->token.op & OT_ARITHMETIC)) {

        int               depth   = 0;
        KOS_OPERATOR_TYPE last_op = (KOS_OPERATOR_TYPE)parser->token.op;

        if ((last_op == OT_ADD || last_op == OT_SUB)
            && parser->had_eol && parser->state.unary_depth == 0)
        {
            parser->error_str = str_err_eol_before_op;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(new_node(parser, ret, NT_OPERATOR));

        ast_push(*ret, node);
        node = KOS_NULL;

        TRY(unary_expr(parser, &node));

        TRY(next_token(parser));

        for (;;) {

            TRY(increase_ast_depth(parser));
            ++depth;

            if (parser->token.op == OT_ADD || parser->token.op == OT_SUB) {

                if (parser->had_eol && parser->state.unary_depth == 0) {
                    parser->error_str = str_err_eol_before_op;
                    error = KOS_ERROR_PARSE_FAILED;
                    goto cleanup;
                }

                ast_push(*ret, node);
                node = *ret;
                *ret = KOS_NULL;

                last_op = (KOS_OPERATOR_TYPE)parser->token.op;

                TRY(new_node(parser, ret, NT_OPERATOR));

                ast_push(*ret, node);
                node = KOS_NULL;

                TRY(unary_expr(parser, &node));

                TRY(next_token(parser));
            }
            else if ((parser->token.op & OT_MASK) == OT_MULTIPLICATIVE) {

                int mul_depth = 0;

                do {

                    TRY(increase_ast_depth(parser));
                    ++mul_depth;

                    if ((last_op & OT_MASK) == OT_MULTIPLICATIVE) {

                        ast_push(*ret, node);
                        node = *ret;
                        *ret = KOS_NULL;

                        last_op = (KOS_OPERATOR_TYPE)parser->token.op;

                        TRY(new_node(parser, ret, NT_OPERATOR));

                        ast_push(*ret, node);
                        node = KOS_NULL;

                        TRY(unary_expr(parser, &node));
                    }
                    else {

                        TRY(new_node(parser, &aux, NT_OPERATOR));

                        ast_push(aux, node);
                        node = KOS_NULL;

                        TRY(unary_expr(parser, &node));

                        ast_push(aux, node);
                        node = aux;
                        aux  = KOS_NULL;
                    }

                    TRY(next_token(parser));

                } while ((parser->token.op & OT_MASK) == OT_MULTIPLICATIVE);

                parser->ast_depth -= mul_depth;
            }
            else
                break;
        }

        ast_push(*ret, node);
        node = KOS_NULL;

        parser->ast_depth -= depth;

        if ((parser->token.op & OT_MASK) == OT_BITWISE) {
            parser->error_str = str_err_mixed_operators;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        parser->unget = 1;
    }
    else if ((parser->token.op & OT_MASK) == OT_BITWISE) {

        int                     depth = 0;
        const KOS_OPERATOR_TYPE op    = (KOS_OPERATOR_TYPE)parser->token.op;

        *ret = node;
        node = KOS_NULL;

        do {

            TRY(increase_ast_depth(parser));
            ++depth;

            TRY(new_node(parser, &node, NT_OPERATOR));

            ast_push(node, *ret);
            *ret = node;
            node = KOS_NULL;

            TRY(unary_expr(parser, &node));

            ast_push(*ret, node);
            node = KOS_NULL;

            TRY(next_token(parser));
        } while ((KOS_OPERATOR_TYPE)parser->token.op == op);

        parser->ast_depth -= depth;

        {
            const KOS_OPERATOR_TYPE next_op = (KOS_OPERATOR_TYPE)parser->token.op;
            if ((next_op & OT_MASK) == OT_BITWISE     ||
                (next_op & OT_MASK) == OT_ARITHMETIC ||
                next_op == OT_SHL || next_op == OT_SHR || next_op == OT_SHRU) {

                parser->error_str = str_err_mixed_operators;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }
        }

        parser->unget = 1;
    }
    else if (parser->token.op == OT_SHL || parser->token.op == OT_SHR || parser->token.op == OT_SHRU) {

        TRY(new_node(parser, ret, NT_OPERATOR));

        ast_push(*ret, node);
        node = KOS_NULL;

        TRY(unary_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;
    }
    else {
        parser->unget = 1;
        *ret = node;
        node = KOS_NULL;
    }

cleanup:
    return error;
}

static int comparison_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(arithm_bitwise_expr(parser, &node));

    TRY(next_token(parser));

    if ((parser->token.op & OT_MASK) == OT_COMPARISON
        || parser->token.keyword == KW_IN
        || parser->token.keyword == KW_INSTANCEOF
        || parser->token.keyword == KW_PROPERTYOF) {

        KOS_AST_NODE *aux = KOS_NULL;

        TRY(new_node(parser, ret, NT_OPERATOR));

        /* Swap operands of the 'in' and 'propertyof' operator */
        if (parser->token.keyword == KW_IN
            || parser->token.keyword == KW_PROPERTYOF)
            aux = node;
        else
            ast_push(*ret, node);
        node = KOS_NULL;

        TRY(arithm_bitwise_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        if (aux)
            ast_push(*ret, aux);
    }
    else {
        parser->unget = 1;
        *ret = node;
        node = KOS_NULL;
    }

cleanup:
    return error;
}

static int logical_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(comparison_expr(parser, &node));

    TRY(next_token(parser));

    if (parser->token.op == OT_LOGAND || parser->token.op == OT_LOGOR) {

        KOS_AST_NODE           *op_node = KOS_NULL;
        const KOS_OPERATOR_TYPE op      = (KOS_OPERATOR_TYPE)parser->token.op;
        int                     depth   = 0;

        TRY(new_node(parser, ret, NT_OPERATOR));

        ast_push(*ret, node);
        node = KOS_NULL;

        op_node = *ret;

        for (;;) {

            TRY(next_token(parser));
            TRY(increase_ast_depth(parser));
            ++depth;
            parser->unget = 1;

            TRY(comparison_expr(parser, &node));

            TRY(next_token(parser));

            if ((KOS_OPERATOR_TYPE)parser->token.op == op) {

                TRY(push_node(parser, op_node, NT_OPERATOR, &op_node));

                ast_push(op_node, node);
                node = KOS_NULL;
            }
            else
                break;
        }

        parser->ast_depth -= depth;

        ast_push(op_node, node);
        node = KOS_NULL;

        if (parser->token.op == OT_LOGAND || parser->token.op == OT_LOGOR) {
            parser->error_str = str_err_mixed_operators;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }
    else {
        *ret = node;
        node = KOS_NULL;
    }

    parser->unget = 1;

cleanup:
    return error;
}

static int conditional_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int           error             = KOS_SUCCESS;
    KOS_AST_NODE *node              = KOS_NULL;
    const int     saved_unary_depth = parser->state.unary_depth;

    TRY(logical_expr(parser, &node));

    TRY(next_token(parser));

    if (parser->token.op == OT_LOGTRI) {

        TRY(increase_ast_depth(parser));

        TRY(new_node(parser, ret, NT_OPERATOR));

        ast_push(*ret, node);
        node = KOS_NULL;

        ++parser->state.unary_depth;

        TRY(conditional_expr(parser, &node));

        --parser->state.unary_depth;

        ast_push(*ret, node);
        node = KOS_NULL;

        TRY(assume_separator(parser, ST_COLON));

        TRY(conditional_expr(parser, &node));

        ast_push(*ret, node);

        --parser->ast_depth;
    }
    else {
        parser->unget = 1;
        *ret = node;
    }

    assert(parser->state.unary_depth == saved_unary_depth);

cleanup:
    parser->state.unary_depth = saved_unary_depth;

    return error;
}

static int stream_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int depth = 1;

    TRY(next_token(parser));
    TRY(increase_ast_depth(parser));
    parser->unget = 1;

    TRY(conditional_expr(parser, ret));

    TRY(next_token(parser));

    while (parser->token.op == OT_ARROW) {

        KOS_AST_NODE *arg_node = *ret;
        KOS_AST_NODE *fun_node = KOS_NULL;

        TRY(increase_ast_depth(parser));
        ++depth;

        TRY(new_node(parser, ret, NT_INVOCATION));

        TRY(conditional_expr(parser, &fun_node));

        if (fun_node->type == NT_SUPER_PROTO_LITERAL) {
            if ( ! parser->state.in_constructor) {
                parser->token     = fun_node->token;
                parser->error_str = str_err_unexpected_super_ctor;
                RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
            }

            assert(parser->state.in_class_member);

            fun_node->type = NT_SUPER_CTOR_LITERAL;
        }

        ast_push(*ret, fun_node);
        ast_push(*ret, arg_node);

        TRY(next_token(parser));
    }

    parser->unget = 1;

    parser->ast_depth -= depth;

cleanup:
    return error;
}

static int async_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int           error      = KOS_SUCCESS;
    KOS_AST_NODE *node       = KOS_NULL;
    KOS_TOKEN     name_token = parser->token;

    TRY(new_node(parser, ret, NT_ASYNC));

    TRY(next_token(parser));

    if (parser->token.keyword == KW_DO) {

        KOS_AST_NODE    *fun_node = KOS_NULL;
        KOS_AST_NODE    *tmp_node = KOS_NULL;
        KOS_AST_NODE    *sub_node = KOS_NULL;
        KOS_PARSER_STATE state;

        save_function_state(parser, &state);

        TRY(new_node(parser, &node, NT_INVOCATION));

        TRY(new_node(parser, &fun_node, NT_FUNCTION_LITERAL));
        ast_push(node, fun_node);

        TRY(new_node(parser, &tmp_node, NT_NAME));
        tmp_node->token = name_token;
        ast_push(fun_node, tmp_node);

        TRY(new_node(parser, &sub_node, NT_IDENTIFIER));
        sub_node->token = name_token;
        ast_push(tmp_node, sub_node);
        sub_node = KOS_NULL;
        tmp_node = KOS_NULL;

        TRY(new_node(parser, &tmp_node, NT_PARAMETERS));
        ast_push(fun_node, tmp_node);
        tmp_node = KOS_NULL;

        TRY(new_node(parser, &tmp_node, NT_LANDMARK));
        ast_push(fun_node, tmp_node);
        tmp_node = KOS_NULL;

        TRY(new_node(parser, &sub_node, NT_RETURN));

        parser->state.unary_depth = 0;

        error = do_stmt(parser, &tmp_node);
        assert(error || parser->state.unary_depth == 0);
        restore_function_state(parser, &state);
        if (error)
            goto cleanup;

        assert(tmp_node->type == NT_SCOPE);
        ast_push(tmp_node, sub_node);
        ast_push(fun_node, tmp_node);
        tmp_node = KOS_NULL;
        sub_node = KOS_NULL;

        TRY(new_node(parser, &tmp_node, NT_LANDMARK));
        ast_push(fun_node, tmp_node);
        tmp_node = KOS_NULL;
    }
    else {
        KOS_TOKEN saved_token = parser->token;

        parser->unget = 1;

        TRY(stream_expr(parser, &node));

        if (node->type != NT_INVOCATION) {
            parser->token     = saved_token;
            parser->error_str = str_err_expected_invocation;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }

    ast_push(*ret, node);
    node = KOS_NULL;

cleanup:
    return error;
}

static int right_hand_side_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.keyword == KW_YIELD) {

        if (parser->state.in_constructor) {
            parser->error_str = str_err_yield_in_constructor;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(new_node(parser, ret, NT_YIELD));

        TRY(next_token(parser));

        if (parser->token.keyword == KW_ASYNC)
            TRY(async_expr(parser, &node));
        else {
            parser->unget = 1;
            TRY(stream_expr(parser, &node));
        }

        ast_push(*ret, node);
        node = KOS_NULL;
    }
    else if (parser->token.keyword == KW_ASYNC) {

        TRY(async_expr(parser, ret));
    }
    else {

        parser->unget = 1;

        TRY(stream_expr(parser, ret));
    }

cleanup:
    return error;
}

static int refinement_identifier(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *node  = *ret;

    *ret = KOS_NULL;

    TRY(new_node(parser, ret, NT_REFINEMENT));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.type == TT_STRING_OPEN) {
        parser->error_str = str_err_expected_string;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    if (parser->token.type != TT_IDENTIFIER && parser->token.type != TT_KEYWORD && parser->token.type != TT_STRING) {
        parser->error_str = str_err_expected_ident_or_str;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    TRY(push_node(parser, *ret, NT_STRING_LITERAL, KOS_NULL));

cleanup:
    return error;
}

static int refinement_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int           error = KOS_SUCCESS;
    KOS_AST_NODE *node  = *ret;

    *ret = KOS_NULL;

    TRY(new_node(parser, ret, NT_REFINEMENT));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.sep == ST_SQUARE_CLOSE) {
        parser->error_str = str_err_expected_expression;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    else if (parser->token.sep == ST_COLON) {

        (*ret)->type = NT_SLICE;

        TRY(next_token(parser));

        if (parser->token.sep == ST_SQUARE_CLOSE) {

            TRY(push_node(parser, *ret, NT_VOID_LITERAL, KOS_NULL));

            TRY(push_node(parser, *ret, NT_VOID_LITERAL, KOS_NULL));

            parser->unget = 1;
        }
        else {

            TRY(push_node(parser, *ret, NT_VOID_LITERAL, KOS_NULL));

            parser->unget = 1;

            TRY(right_hand_side_expr(parser, &node));

            ast_push(*ret, node);
            node = KOS_NULL;
        }
    }
    else {

        parser->unget = 1;

        TRY(right_hand_side_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        TRY(next_token(parser));

        if (parser->token.sep == ST_COLON) {

            (*ret)->type = NT_SLICE;

            TRY(next_token(parser));

            parser->unget = 1;

            if (parser->token.sep == ST_SQUARE_CLOSE)
                TRY(new_node(parser, &node, NT_VOID_LITERAL));
            else
                TRY(right_hand_side_expr(parser, &node));

            ast_push(*ret, node);
            node = KOS_NULL;
        }
        else
            parser->unget = 1;
    }

    TRY(assume_separator(parser, ST_SQUARE_CLOSE));

cleanup:
    return error;
}

static int named_argument(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    KOS_AST_NODE *value_node;
    int           error = KOS_SUCCESS;

    TRY(next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_named_arg;
        RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
    }

    TRY(new_node(parser, ret, NT_PROPERTY));

    TRY(push_node(parser, *ret, NT_STRING_LITERAL, KOS_NULL));

    TRY(next_token(parser));

    if (parser->token.op != OT_SET) {
        parser->error_str = str_err_expected_named_assignment;
        RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
    }

    TRY(right_hand_side_expr(parser, &value_node));

    ast_push(*ret, value_node);

cleanup:
    return error;
}

static int invocation(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = *ret;
    *ret = KOS_NULL;

    TRY(new_node(parser, ret, NT_INVOCATION));

    if (node->type == NT_SUPER_PROTO_LITERAL) {
        if ( ! parser->state.in_constructor) {
            parser->token     = node->token;
            parser->error_str = str_err_unexpected_super_ctor;
            RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
        }

        assert(parser->state.in_class_member);

        node->type = NT_SUPER_CTOR_LITERAL;
    }

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.sep != ST_PAREN_CLOSE) {

        KOS_AST_NODE *args_container_node = *ret;
        int           named_args          = 0;

        if (parser->token.type == TT_IDENTIFIER) {

            const KOS_TOKEN saved_token = parser->token;

            TRY(next_token(parser));

            if (parser->token.op == OT_SET)
                named_args = 1;

            kos_lexer_unget_token(&parser->lexer, &saved_token);
            parser->unget = 0;

            if (named_args) {
                TRY(push_node(parser, *ret, NT_NAMED_ARGUMENTS, &args_container_node));

                TRY(push_node(parser, args_container_node, NT_OBJECT_LITERAL, &args_container_node));
            }
        }
        else
            parser->unget = 1;

        for (;;) {

            if (named_args)
                TRY(named_argument(parser, &node));
            else
                TRY(right_hand_side_expr(parser, &node));

            TRY(next_token(parser));

            if (parser->token.op == OT_MORE) {

                KOS_AST_NODE *expanded = node;

                if (named_args) {
                    parser->error_str = str_err_cannot_expand_named_arg;
                    RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
                }

                node = KOS_NULL;
                TRY(new_node(parser, &node, NT_EXPAND));

                ast_push(node, expanded);

                TRY(next_token(parser));
            }

            ast_push(args_container_node, node);
            node = KOS_NULL;

            if (parser->token.sep == ST_PAREN_CLOSE)
                break;

            if (parser->token.sep != ST_COMMA) {
                parser->error_str = str_err_expected_comma;
                RAISE_ERROR(KOS_ERROR_PARSE_FAILED);
            }
        }
    }

cleanup:
    return error;
}

static int member_expr(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int       error;
    const int saved_unary_depth = parser->state.unary_depth;

    error = primary_expr(parser, ret);

    ++parser->state.unary_depth;

    while (!error) {
        error = next_token(parser);

        if (!error) {
            const KOS_TOKEN *token = &parser->token;

            if (token->op == OT_DOT)
                error = refinement_identifier(parser, ret);

            else if (token->sep == ST_SQUARE_OPEN) {
                if (parser->had_eol && parser->state.unary_depth == 1) {
                    parser->error_str = str_err_eol_before_sq;
                    error = KOS_ERROR_PARSE_FAILED;
                }
                else
                    error = refinement_expr(parser, ret);
            }
            else if (token->sep == ST_PAREN_OPEN) {
                if (parser->had_eol && parser->state.unary_depth == 1) {
                    parser->error_str = str_err_eol_before_par;
                    error = KOS_ERROR_PARSE_FAILED;
                }
                else
                    error = invocation(parser, ret);
            }
            else {
                parser->unget = 1;
                break;
            }
        }
    }

    assert(parser->state.unary_depth == saved_unary_depth + 1);
    parser->state.unary_depth = saved_unary_depth;

    return error;
}

static int expr_var_const(KOS_PARSER    *parser,
                          int            allow_in,
                          int            allow_multi_assignment,
                          int            is_public,
                          KOS_AST_NODE **ret)
{
    int           error         = KOS_SUCCESS;
    KOS_AST_NODE *node          = KOS_NULL;
    KOS_AST_NODE *ident_node    = KOS_NULL;
    KOS_NODE_TYPE node_type     = NT_ASSIGNMENT;
    KOS_NODE_TYPE var_node_type = parser->token.keyword == KW_CONST
                                          ? NT_CONST : NT_VAR;

    TRY(new_node(parser, &node, var_node_type));

    TRY(next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    TRY(push_node(parser, node, NT_IDENTIFIER, &ident_node));

    if (is_public)
        TRY(push_node(parser, ident_node, NT_EXPORT, KOS_NULL));

    TRY(next_token(parser));

    if (parser->token.sep == ST_COMMA) {

        if ( ! allow_multi_assignment) {
            parser->error_str = str_err_expected_var_assignment;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        node_type = NT_MULTI_ASSIGNMENT;
    }

    while (parser->token.sep == ST_COMMA) {

        TRY(next_token(parser));

        if (parser->token.type != TT_IDENTIFIER) {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(push_node(parser, node, NT_IDENTIFIER, KOS_NULL));

        TRY(next_token(parser));
    }

    if ((parser->token.keyword != KW_IN || !allow_in) && (parser->token.op != OT_SET)) {
        parser->error_str = str_err_expected_var_assignment;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    if (parser->token.keyword == KW_IN)
        node_type = NT_IN;

    TRY(new_node(parser, ret, node_type));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(right_hand_side_expr(parser, &node));

    /* TODO error out when doing multi-assignment from unsupported types, like function */

    if ((node->type == NT_FUNCTION_LITERAL    ||
         node->type == NT_CONSTRUCTOR_LITERAL ||
         node->type == NT_CLASS_LITERAL) && node_type != NT_IN) {

        TRY(set_function_name(parser, node, &ident_node->token, var_node_type == NT_CONST));
    }

    ast_push(*ret, node);
    node = KOS_NULL;

cleanup:
    return error;
}

static int check_multi_assgn_lhs(KOS_PARSER         *parser,
                                 const KOS_AST_NODE *node)
{
    const KOS_NODE_TYPE type = (KOS_NODE_TYPE)node->type;

    if (type == NT_REFINEMENT ||
        type == NT_IDENTIFIER ||
        type == NT_VOID_LITERAL ||
        type == NT_SLICE)

        return KOS_SUCCESS;

    parser->error_str = str_err_expected_assignable;
    parser->token     = node->token;
    return KOS_ERROR_PARSE_FAILED;
}

static int expr_no_var(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    KOS_AST_NODE *node = KOS_NULL;
    KOS_AST_NODE *lhs  = KOS_NULL;

    int error = KOS_SUCCESS;

    KOS_NODE_TYPE node_type;

    TRY(right_hand_side_expr(parser, &node));

    node_type = (KOS_NODE_TYPE)node->type;

    TRY(next_token(parser));

    if (parser->token.sep == ST_SEMICOLON || parser->token.sep == ST_PAREN_CLOSE
        || (node_type != NT_IDENTIFIER && node_type != NT_REFINEMENT &&
            node_type != NT_SLICE && node_type != NT_VOID_LITERAL)
        || (parser->token.sep != ST_COMMA && ! (parser->token.op & OT_ASSIGNMENT) && parser->had_eol)
        || parser->token.type == TT_EOF)
    {

        parser->unget = 1;

        *ret = node;
        node = KOS_NULL;
    }
    else {
        int num_assignees = 1;

        TRY(new_node(parser, &lhs, NT_LEFT_HAND_SIDE));

        if (parser->token.sep == ST_COMMA)
            TRY(check_multi_assgn_lhs(parser, node));
        else if (node_type == NT_VOID_LITERAL) {
            parser->error_str = str_err_expected_semicolon;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        ast_push(lhs, node);
        node = KOS_NULL;

        while (parser->token.sep == ST_COMMA) {

            ++num_assignees;

            TRY(member_expr(parser, &node));

            TRY(check_multi_assgn_lhs(parser, node));

            ast_push(lhs, node);
            node = KOS_NULL;

            TRY(next_token(parser));
        }

        if (!(parser->token.op & OT_ASSIGNMENT)) {
            if (num_assignees > 1)
                parser->error_str = str_err_expected_multi_assignment;
            else
                parser->error_str = str_err_expected_semicolon;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        if (parser->token.op != OT_SET && num_assignees > 1) {
            parser->error_str = str_err_expected_multi_assignment;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        if (parser->token.op != OT_SET && node_type == NT_SLICE) {
            parser->error_str = str_err_unsupported_slice_assign;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(new_node(parser, ret, num_assignees > 1 ? NT_MULTI_ASSIGNMENT : NT_ASSIGNMENT));

        ast_push(*ret, lhs);
        lhs = KOS_NULL;

        TRY(right_hand_side_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;
    }

cleanup:
    return error;
}

static int expr(KOS_PARSER *parser, int allow_in, int allow_var, KOS_AST_NODE **ret)
{
    int error;

    if (allow_var) {
        error = next_token(parser);

        if (!error) {
            if (parser->token.keyword == KW_VAR || parser->token.keyword == KW_CONST)
                error = expr_var_const(parser, allow_in, 1, 0, ret);
            else {
                parser->unget = 1;
                error = expr_no_var(parser, ret);
            }
        }
    }
    else
        error = expr_no_var(parser, ret);

    return error;
}

static int expr_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = expr(parser, 0, 1, ret);

    if (!error)
        error = assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int compound_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(assume_separator(parser, ST_CURLY_OPEN));

    TRY(new_node(parser, ret, NT_SCOPE));

    TRY(next_token(parser));

    while (parser->token.sep != ST_CURLY_CLOSE) {

        if (parser->token.type == TT_EOF) {
            parser->error_str = str_err_expected_curly_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        parser->unget = 1;

        TRY(next_statement(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        TRY(next_token(parser));
    }

cleanup:
    return error;
}

static int function_stmt(KOS_PARSER *parser, int is_public, KOS_AST_NODE **ret)
{
    int              error        = KOS_SUCCESS;
    KOS_AST_NODE    *const_node;
    KOS_AST_NODE    *fun_node     = KOS_NULL;
    KOS_TOKEN        fun_kw_token = parser->token;
    KOS_KEYWORD_TYPE fun_keyword  = (KOS_KEYWORD_TYPE)fun_kw_token.keyword;

    TRY(next_token(parser));

    if (parser->token.type == TT_IDENTIFIER) {

        KOS_TOKEN     fun_name_token = parser->token;
        KOS_AST_NODE *ident_node     = KOS_NULL;

        /* To simplify operator selection in the compiler */
        fun_kw_token.op = OT_SET;

        TRY(new_node(parser, ret, NT_ASSIGNMENT));
        (*ret)->token = fun_kw_token;

        TRY(push_node(parser, *ret, NT_CONST, &const_node));
        const_node->token = fun_kw_token;

        TRY(push_node(parser, const_node, NT_IDENTIFIER, &ident_node));

        if (is_public)
            TRY(push_node(parser, ident_node, NT_EXPORT, KOS_NULL));

        if (fun_keyword == KW_CLASS)
            TRY(class_literal(parser, &fun_node));
        else
            TRY(function_literal(parser, fun_keyword, &fun_node));

        TRY(set_function_name(parser, fun_node, &fun_name_token, 1));

        ast_push(*ret, fun_node);
        fun_node = KOS_NULL;
    }
    else if (is_public) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
    }
    else {

        kos_lexer_unget_token(&parser->lexer, &fun_kw_token);
        parser->unget = 0;
        TRY(expr_stmt(parser, ret));
    }

cleanup:
    return error;
}

static int do_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    return compound_stmt(parser, ret);
}

static int if_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_IF));

    TRY(right_hand_side_expr(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(compound_stmt(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.keyword == KW_ELSE) {

        TRY(next_token(parser));

        if (parser->token.keyword == KW_IF)
            TRY(if_stmt(parser, &node));

        else {

            parser->unget = 1;

            TRY(compound_stmt(parser, &node));
        }

        ast_push(*ret, node);
        node = KOS_NULL;
    }
    else
        parser->unget = 1;

cleanup:
    return error;
}

static int try_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_TRY_CATCH));

    TRY(compound_stmt(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.keyword == KW_CATCH) {

        int           has_paren;
        KOS_AST_NODE *catch_node;
        KOS_AST_NODE *var_node;

        TRY(push_node(parser, *ret, NT_CATCH, &catch_node));

        TRY(fetch_optional_paren(parser, &has_paren));

        TRY(next_token(parser));

        if (parser->token.keyword != KW_VAR && parser->token.keyword != KW_CONST) {
            parser->error_str = str_err_expected_var_or_const;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(push_node(parser, catch_node,
                       parser->token.keyword == KW_VAR ? NT_VAR : NT_CONST,
                       &var_node));

        TRY(next_token(parser));

        if (parser->token.type != TT_IDENTIFIER) {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(push_node(parser, var_node, NT_IDENTIFIER, KOS_NULL));

        if (has_paren)
            TRY(assume_separator(parser, ST_PAREN_CLOSE));

        TRY(compound_stmt(parser, &node));

        ast_push(catch_node, node);
        node = KOS_NULL;

        TRY(next_token(parser));
    }
    else {
        parser->error_str = str_err_expected_catch;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    parser->unget = 1;

cleanup:
    return error;
}

static int defer_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *try_node     = KOS_NULL;
    KOS_AST_NODE *finally_node = KOS_NULL;

    /* defer is implemented as try..finally */
    TRY(new_node(parser, ret, NT_TRY_DEFER));

    TRY(push_node(parser, *ret, NT_SCOPE, &try_node));

    TRY(compound_stmt(parser, &finally_node));

    TRY(next_token(parser));

    if (parser->token.type    == TT_EOF         ||
        parser->token.sep     == ST_CURLY_CLOSE ||
        parser->token.keyword == KW_CASE        ||
        parser->token.keyword == KW_DEFAULT)

        *ret = finally_node;

    else {

        ast_push(*ret, finally_node);

        do {

            KOS_AST_NODE *node = KOS_NULL;

            parser->unget = 1;

            TRY(next_statement(parser, &node));

            ast_push(try_node, node);

            TRY(next_token(parser));
        }
        while (parser->token.type    != TT_EOF         &&
               parser->token.sep     != ST_CURLY_CLOSE &&
               parser->token.keyword != KW_CASE        &&
               parser->token.keyword != KW_DEFAULT);
    }

    parser->unget = 1;

cleanup:
    return error;
}

static int gen_fake_const(KOS_PARSER   *parser,
                          KOS_AST_NODE *parent_node)
{
    int            error;
    KOS_AST_NODE  *node    = KOS_NULL;
    char          *name    = KOS_NULL;
    unsigned       name_len;
    const unsigned max_len = 32;

    TRY(push_node(parser, parent_node, NT_CONST, &node));

    TRY(push_node(parser, node, NT_IDENTIFIER, &node));

    name = (char *)KOS_mempool_alloc(parser->ast_buf, max_len);

    if ( ! name) {
        error = KOS_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    name_len = (unsigned)snprintf(name, max_len, "%u:%u",
                                  parser->token.line, parser->token.column);

    node->token.begin   = name;
    node->token.length  = (uint16_t)(name_len >= max_len ? max_len - 1 : name_len);
    node->token.type    = TT_IDENTIFIER;
    node->token.keyword = KW_NONE;
    node->token.op      = OT_NONE;
    node->token.sep     = ST_NONE;

cleanup:
    return error;
}

static int gen_acquire(KOS_PARSER   *parser,
                       KOS_AST_NODE *parent_node,
                       KOS_AST_NODE *const_node)
{
    int error;

    static const char str_acquire[] = "acquire";

    KOS_AST_NODE *if_node = KOS_NULL;
    KOS_AST_NODE *node    = KOS_NULL;
    KOS_AST_NODE *id_node = KOS_NULL;

    assert(const_node);
    assert(const_node->type == NT_CONST);
    assert(const_node->children);

    const_node = const_node->children;
    assert(const_node->type == NT_IDENTIFIER);
    assert( ! const_node->next);

    TRY(push_node(parser, parent_node, NT_IF, &if_node));

    if_node->token = const_node->token;

    TRY(push_node(parser, if_node, NT_OPERATOR, &node));

    node->token         = const_node->token;
    node->token.keyword = KW_PROPERTYOF;
    node->token.op      = OT_NONE;
    node->token.sep     = ST_NONE;
    node->token.type    = TT_IDENTIFIER;

    TRY(push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_acquire;
    id_node->token.length = sizeof(str_acquire) - 1;

    TRY(push_node(parser, if_node, NT_SCOPE, &node));

    TRY(push_node(parser, node, NT_INVOCATION, &node));

    TRY(push_node(parser, node, NT_REFINEMENT, &node));

    TRY(push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_acquire;
    id_node->token.length = sizeof(str_acquire) - 1;

cleanup:
    return error;
}

static int gen_release(KOS_PARSER   *parser,
                       KOS_AST_NODE *parent_node,
                       KOS_AST_NODE *const_node)
{
    int error;

    static const char str_release[] = "release";

    KOS_AST_NODE *node    = KOS_NULL;
    KOS_AST_NODE *id_node = KOS_NULL;

    assert(const_node);
    assert(const_node->type == NT_CONST);
    assert(const_node->children);

    const_node = const_node->children;
    assert(const_node->type == NT_IDENTIFIER);
    assert( ! const_node->next);

    TRY(push_node(parser, parent_node, NT_SCOPE, &node));

    TRY(push_node(parser, node, NT_INVOCATION, &node));

    TRY(push_node(parser, node, NT_REFINEMENT, &node));

    TRY(push_node(parser, node, NT_IDENTIFIER, &id_node));

    id_node->token = const_node->token;

    TRY(push_node(parser, node, NT_STRING_LITERAL, &id_node));

    id_node->token        = const_node->token;
    id_node->token.begin  = str_release;
    id_node->token.length = sizeof(str_release) - 1;

cleanup:
    return error;
}

static int with_stmt_continued(KOS_PARSER   *parser,
                               int           has_paren,
                               KOS_AST_NODE *parent_node)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node     = KOS_NULL;
    KOS_AST_NODE *try_node = KOS_NULL;

    if (parser->token.keyword == KW_CONST)
        TRY(expr_var_const(parser, 0, 0, 0, &node));

    else {

        KOS_AST_NODE *aux_node = KOS_NULL;

        TRY(new_node(parser, &node, NT_ASSIGNMENT));

        node->token.op = OT_SET; /* Select correct operator */

        TRY(gen_fake_const(parser, node));

        parser->unget = 1;
        TRY(right_hand_side_expr(parser, &aux_node));

        ast_push(node, aux_node);
    }

    ast_push(parent_node, node);

    TRY(gen_acquire(parser, parent_node, node->children));

    TRY(next_token(parser));

    TRY(push_node(parser, parent_node, NT_TRY_DEFER, &try_node));

    if (parser->token.sep == ST_COMMA) {

        KOS_AST_NODE *scope_node = KOS_NULL;

        TRY(increase_ast_depth(parser));

        TRY(next_token(parser));

        if (parser->token.keyword == KW_VAR        ||
            parser->token.sep     == ST_COMMA      ||
            parser->token.sep     == ST_CURLY_OPEN ||
            parser->token.sep     == ST_PAREN_CLOSE) {

            parser->error_str = str_err_expected_const_or_expr;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        TRY(push_node(parser, try_node, NT_SCOPE, &scope_node));

        TRY(with_stmt_continued(parser, has_paren, scope_node));

        --parser->ast_depth;
    }
    else {

        KOS_AST_NODE *scope_node = KOS_NULL;

        parser->unget = 1;

        if (has_paren)
            TRY(assume_separator(parser, ST_PAREN_CLOSE));

        TRY(compound_stmt(parser, &scope_node));

        ast_push(try_node, scope_node);
    }

    TRY(gen_release(parser, try_node, node->children));

cleanup:
    return error;
}

static int with_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int has_paren;

    TRY(new_node(parser, ret, NT_SCOPE));

    TRY(fetch_optional_paren(parser, &has_paren));

    TRY(next_token(parser));

    if (parser->token.keyword == KW_VAR ||
        (has_paren && parser->token.sep == ST_PAREN_CLOSE)) {

        parser->error_str = str_err_expected_const_or_expr;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    TRY(with_stmt_continued(parser, has_paren, *ret));

cleanup:
    return error;
}

static int switch_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error       = KOS_SUCCESS;
    int has_default = 0;

    KOS_AST_NODE *node = KOS_NULL;

    KOS_AST_NODE *saved_fallthrough = parser->state.last_fallthrough;

    parser->state.last_fallthrough = KOS_NULL;

    TRY(new_node(parser, ret, NT_SWITCH));

    TRY(right_hand_side_expr(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(assume_separator(parser, ST_CURLY_OPEN));

    ++parser->state.allow_break;
    ++parser->state.allow_fallthrough;

    TRY(next_token(parser));

    while (parser->token.sep != ST_CURLY_CLOSE) {

        KOS_AST_NODE *case_node  = KOS_NULL;
        KOS_AST_NODE *scope_node = KOS_NULL;
        int           num_stmts  = 0;

        if (parser->token.type == TT_EOF) {
            parser->error_str = str_err_expected_curly_close;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        if (parser->token.keyword == KW_DEFAULT) {
            if (has_default) {
                parser->error_str = str_err_duplicate_default;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }

            has_default = 1;

            TRY(push_node(parser, *ret, NT_DEFAULT, &case_node));

            TRY(assume_separator(parser, ST_COLON));

            TRY(push_node(parser, case_node, NT_EMPTY, KOS_NULL));
        }
        else {

            TRY(push_node(parser, *ret, NT_CASE, &case_node));

            if (parser->token.keyword != KW_CASE) {
                if (has_default)
                    parser->error_str = str_err_expected_case;
                else
                    parser->error_str = str_err_expected_case_or_default;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }

            for (;;) {

                TRY(right_hand_side_expr(parser, &node));

                ast_push(case_node, node);
                node = KOS_NULL;

                TRY(next_token(parser));

                if (parser->token.sep != ST_COMMA) {
                    parser->unget = 1;
                    break;
                }

                TRY(push_node(parser, case_node, NT_FALLTHROUGH, KOS_NULL));

                case_node = KOS_NULL;

                TRY(push_node(parser, *ret, NT_CASE, &case_node));
            }

            TRY(assume_separator(parser, ST_COLON));
        }

        parser->state.last_fallthrough = KOS_NULL;

        TRY(push_node(parser, case_node, NT_SCOPE, &scope_node));

        TRY(next_token(parser));

        while (parser->token.keyword != KW_CASE        &&
               parser->token.keyword != KW_DEFAULT     &&
               parser->token.sep     != ST_CURLY_CLOSE &&
               parser->token.type    != TT_EOF) {

            KOS_NODE_TYPE node_type;

            parser->unget = 1;

            TRY(next_statement(parser, &node));
            node_type = (KOS_NODE_TYPE)node->type;

            /* Note: Create empty scope if there is only a break in it */
            if (node_type != NT_BREAK || num_stmts) {
                if (node_type == NT_FALLTHROUGH)
                    ast_push(case_node, node);
                else
                    ast_push(scope_node, node);
            }
            node = KOS_NULL;

            ++num_stmts;

            TRY(next_token(parser));

            if (node_type == NT_BREAK || node_type == NT_FALLTHROUGH)
                break;
        }

        if (num_stmts == 0) {
            parser->error_str = str_err_expected_case_statements;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }

    --parser->state.allow_break;
    --parser->state.allow_fallthrough;

    if (parser->state.last_fallthrough) {
        parser->token     = parser->state.last_fallthrough->token;
        parser->error_str = str_err_fallthrough_in_last_case;
        error             = KOS_ERROR_PARSE_FAILED;
    }

cleanup:
    parser->state.last_fallthrough = saved_fallthrough;

    return error;
}

static int loop_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_WHILE));

    TRY(push_node(parser, *ret, NT_EMPTY, KOS_NULL));

    ++parser->state.allow_continue;
    ++parser->state.allow_break;

    TRY(compound_stmt(parser, &node));

    --parser->state.allow_continue;
    --parser->state.allow_break;

    ast_push(*ret, node);
    node = KOS_NULL;

cleanup:
    return error;
}

static int repeat_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_REPEAT));

    ++parser->state.allow_continue;
    ++parser->state.allow_break;

    TRY(compound_stmt(parser, &node));

    --parser->state.allow_continue;
    --parser->state.allow_break;

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(next_token(parser));

    if (parser->token.keyword != KW_WHILE) {
        parser->error_str = str_err_expected_while;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    assert(parser->state.unary_depth == 0);

    TRY(right_hand_side_expr(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    TRY(assume_separator(parser, ST_SEMICOLON));

cleanup:
    return error;
}

static int while_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_WHILE));

    TRY(right_hand_side_expr(parser, &node));

    ast_push(*ret, node);
    node = KOS_NULL;

    ++parser->state.allow_continue;
    ++parser->state.allow_break;

    TRY(compound_stmt(parser, &node));

    --parser->state.allow_continue;
    --parser->state.allow_break;

    ast_push(*ret, node);
    node = KOS_NULL;

cleanup:
    return error;
}

static int for_expr_list(KOS_PARSER        *parser,
                         KOS_SEPARATOR_TYPE end_sep,
                         KOS_AST_NODE     **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, NT_EXPRESSION_LIST));

    /* TODO allow variable without var or const */
    TRY(expr(parser, 1, 1, &node));

    if (node->type == NT_IN) {
        *ret = node;
        node = KOS_NULL;
    }
    else {
        parser->error_str = str_err_expected_for_in;
        error = KOS_ERROR_PARSE_FAILED;
    }

cleanup:
    return error;
}

static int for_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;
    int has_paren;

    KOS_AST_NODE *node       = KOS_NULL;
    KOS_AST_NODE *scope_node = KOS_NULL;
    KOS_AST_NODE *for_node   = KOS_NULL;

    TRY(new_node(parser, &for_node, NT_FOR_IN));
    *ret = for_node;

    TRY(new_node(parser, &scope_node, NT_SCOPE));

    TRY(fetch_optional_paren(parser, &has_paren));

    TRY(for_expr_list(parser, ST_SEMICOLON, &node));

    assert(node->type == NT_IN);

    ast_push(for_node, node);

    node = KOS_NULL;

    if (has_paren)
        TRY(assume_separator(parser, ST_PAREN_CLOSE));

    ++parser->state.allow_continue;
    ++parser->state.allow_break;

    TRY(compound_stmt(parser, &node));

    --parser->state.allow_continue;
    --parser->state.allow_break;

    ast_push(for_node, node);
    node = KOS_NULL;

cleanup:
    return error;
}

static int continue_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    if (!parser->state.allow_continue) {
        parser->error_str = str_err_unexpected_continue;
        error = KOS_ERROR_PARSE_FAILED;
    }

    if (!error)
        error = new_node(parser, ret, NT_CONTINUE);

    if (!error)
        error = assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int break_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    if (!parser->state.allow_break) {
        parser->error_str = str_err_unexpected_break;
        error = KOS_ERROR_PARSE_FAILED;
    }

    if (!error)
        error = new_node(parser, ret, NT_BREAK);

    if (!error)
        error = assume_separator(parser, ST_SEMICOLON);

    return error;
}

static int fallthrough_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    if (!parser->state.allow_fallthrough) {
        parser->error_str = str_err_unexpected_fallthrough;
        error = KOS_ERROR_PARSE_FAILED;
    }

    if (!error)
        error = new_node(parser, ret, NT_FALLTHROUGH);

    if (!error) {
        parser->state.last_fallthrough = *ret;
        error = assume_separator(parser, ST_SEMICOLON);
    }

    return error;
}

static int import_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    TRY(new_node(parser, ret, NT_IMPORT));

    TRY(next_token(parser));

    if (parser->token.type != TT_IDENTIFIER) {
        parser->error_str = str_err_expected_identifier;
        error = KOS_ERROR_PARSE_FAILED;
        goto cleanup;
    }

    TRY(push_node(parser, *ret, NT_IDENTIFIER, KOS_NULL));

    TRY(next_token(parser));

    if (parser->token.op == OT_DOT) {

        TRY(next_token(parser));

        if (parser->token.op == OT_MUL || parser->token.type == TT_IDENTIFIER || parser->token.type == TT_KEYWORD)
            TRY(push_node(parser, *ret, NT_IDENTIFIER, KOS_NULL));
        else {
            parser->error_str = str_err_expected_identifier;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }
    }
    else if (parser->token.sep == ST_COLON) {

        do {

            TRY(next_token(parser));

            if (parser->token.type != TT_IDENTIFIER && parser->token.type != TT_KEYWORD) {
                parser->error_str = str_err_expected_identifier;
                error = KOS_ERROR_PARSE_FAILED;
                goto cleanup;
            }

            TRY(push_node(parser, *ret, NT_IDENTIFIER, KOS_NULL));

            TRY(next_token(parser));

        } while (parser->token.sep == ST_COMMA);

        parser->unget = 1;
    }
    else
        parser->unget = 1;

    TRY(assume_separator(parser, ST_SEMICOLON));

cleanup:
    return error;
}

static int end_of_return(const KOS_PARSER *parser)
{
    const KOS_TOKEN *token = &parser->token;

    if (token->sep  == ST_SEMICOLON   ||
        token->sep  == ST_CURLY_CLOSE ||
        token->type == TT_EOF)

        return 1;

    return 0;
}

static int return_throw_assert_stmt(KOS_PARSER    *parser,
                                    KOS_NODE_TYPE  type,
                                    KOS_AST_NODE **ret)
{
    int error = KOS_SUCCESS;

    KOS_AST_NODE *node = KOS_NULL;

    TRY(new_node(parser, ret, type));

    TRY(next_token(parser));

    if (type == NT_RETURN && end_of_return(parser)) {

        if (parser->state.in_constructor) {
            TRY(push_node(parser, *ret, NT_THIS_LITERAL, &node));
            node->token = (*ret)->token;
        }

        if (parser->token.sep != ST_SEMICOLON)
            parser->unget = 1;
    }
    else {

        if (parser->state.in_constructor && type == NT_RETURN && parser->token.keyword != KW_THIS) {
            parser->error_str = str_err_expected_this;
            error = KOS_ERROR_PARSE_FAILED;
            goto cleanup;
        }

        parser->unget = 1;

        TRY(right_hand_side_expr(parser, &node));

        ast_push(*ret, node);
        node = KOS_NULL;

        if (type == NT_ASSERT) {

            TRY(next_token(parser));

            TRY(push_node(parser, *ret, NT_LANDMARK, KOS_NULL));

            parser->unget = 1;
        }

        TRY(assume_separator(parser, ST_SEMICOLON));
    }

cleanup:
    return error;
}

static int public_stmt(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = next_token(parser);

    if ( ! error) {

        switch (parser->token.keyword) {

            case KW_VAR:
                /* fall through */
            case KW_CONST:
                error = expr_var_const(parser, 0, 0, 1, ret);
                break;

            case KW_FUN:
                /* fall through */
            case KW_CLASS:
                error = function_stmt(parser, 1, ret);
                break;

            default:
                parser->error_str = str_err_invalid_public;
                error = KOS_ERROR_PARSE_FAILED;
                break;
        }
    }

    return error;
}

static int next_statement(KOS_PARSER *parser, KOS_AST_NODE **ret)
{
    int error = next_token(parser);

    if ( ! error)
        error = increase_ast_depth(parser);

    if ( ! error) {
        const KOS_TOKEN *token = &parser->token;

        assert(parser->state.unary_depth == 0);

        switch (token->keyword) {
            case KW_FUN:
                /* fall through */
            case KW_CLASS:
                error = function_stmt(parser, 0, ret);
                break;
            case KW_DO:
                error = do_stmt(parser, ret);
                break;
            case KW_IF:
                error = if_stmt(parser, ret);
                break;
            case KW_TRY:
                error = try_stmt(parser, ret);
                break;
            case KW_DEFER:
                error = defer_stmt(parser, ret);
                break;
            case KW_WITH:
                error = with_stmt(parser, ret);
                break;
            case KW_SWITCH:
                error = switch_stmt(parser, ret);
                break;
            case KW_LOOP:
                error = loop_stmt(parser, ret);
                break;
            case KW_REPEAT:
                error = repeat_stmt(parser, ret);
                break;
            case KW_WHILE:
                error = while_stmt(parser, ret);
                break;
            case KW_FOR:
                error = for_stmt(parser, ret);
                break;
            case KW_CONTINUE:
                error = continue_stmt(parser, ret);
                break;
            case KW_BREAK:
                error = break_stmt(parser, ret);
                break;
            case KW_FALLTHROUGH:
                error = fallthrough_stmt(parser, ret);
                break;
            case KW_RETURN:
                error = return_throw_assert_stmt(parser, NT_RETURN, ret);
                break;
            case KW_THROW:
                error = return_throw_assert_stmt(parser, NT_THROW, ret);
                break;
            case KW_ASSERT:
                error = return_throw_assert_stmt(parser, NT_ASSERT, ret);
                break;
            case KW_IMPORT:
                parser->error_str = str_err_unexpected_import;
                error = KOS_ERROR_PARSE_FAILED;
                break;
            case KW_PUBLIC:
                if (parser->ast_depth == 1)
                    error = public_stmt(parser, ret);
                else {
                    parser->error_str = str_err_unexpected_public;
                    error = KOS_ERROR_PARSE_FAILED;
                }
                break;
            case KW_NONE:
                if (token->sep == ST_SEMICOLON) {
                    error = new_node(parser, ret, NT_EMPTY);
                    break;
                }
                else if (token->type == TT_EOF) {
                    *ret = KOS_NULL;
                    break;
                }
                /* fall through */
            default:
                parser->unget = 1;
                error = expr_stmt(parser, ret);
                break;
        }
    }

    if ( ! error)
        --parser->ast_depth;

    return error;
}

static int handle_imports(KOS_PARSER *parser, KOS_AST_NODE *root)
{
    int error = next_token(parser);

    while (!error) {

        const KOS_TOKEN *token = &parser->token;

        if (token->keyword == KW_IMPORT) {

            KOS_AST_NODE *node = KOS_NULL;

            error = import_stmt(parser, &node);

            if (!error)
                ast_push(root, node);
            else
                break;
        }
        else if (token->type == TT_EOF)
            break;
        else if (token->sep != ST_SEMICOLON) {
            parser->unget = 1;
            break;
        }

        error = next_token(parser);
    }

    return error;
}

void kos_parser_init(KOS_PARSER           *parser,
                     struct KOS_MEMPOOL_S *mempool,
                     uint16_t              file_id,
                     const char           *begin,
                     const char           *end)
{
    kos_lexer_init(&parser->lexer, file_id, begin, end);

    parser->ast_buf                 = mempool;
    parser->error_str               = KOS_NULL;
    parser->unget                   = 0;
    parser->had_eol                 = 0;
    parser->ast_depth               = 0;
    parser->state.last_fallthrough  = KOS_NULL;
    parser->state.unary_depth       = 0;
    parser->state.allow_continue    = 0;
    parser->state.allow_break       = 0;
    parser->state.allow_fallthrough = 0;
    parser->state.in_constructor    = 0;
    parser->state.in_derived_class  = 0;
    parser->state.in_class_member   = 0;

    parser->token.length            = 0;
    parser->token.file_id           = parser->lexer.pos.file_id;
    parser->token.column            = parser->lexer.pos.column;
    parser->token.line              = parser->lexer.pos.line;
    parser->token.type              = TT_EOF;
    parser->token.keyword           = KW_NONE;
    parser->token.op                = OT_NONE;
    parser->token.sep               = ST_NONE;
}

int kos_parser_parse(KOS_PARSER    *parser,
                     KOS_AST_NODE **ret)
{
    PROF_ZONE(PARSER)

    KOS_AST_NODE *root  = KOS_NULL;
    KOS_AST_NODE *node  = KOS_NULL;
    int           error = KOS_SUCCESS;

    TRY(new_node(parser, &root, NT_SCOPE));

    TRY(handle_imports(parser, root));

    TRY(next_statement(parser, &node));

    while (node) {

        ast_push(root, node);
        node = KOS_NULL;

        TRY(next_statement(parser, &node));
    }

cleanup:
    if (!error)
        *ret = root;
    else if (error == KOS_ERROR_SCANNING_FAILED)
        parser->error_str = parser->lexer.error_str;

    return error;
}

int kos_parser_import_base(KOS_PARSER            *parser,
                           struct KOS_AST_NODE_S *ast)
{
    KOS_AST_NODE *import_node;
    KOS_AST_NODE *ident_node;
    KOS_TOKEN     token;
    int           error = KOS_SUCCESS;

    assert(ast);
    assert(ast->type == NT_SCOPE);
    assert( ! ast->next);

    memset(&token, 0, sizeof(token));

    TRY(new_node(parser, &import_node, NT_IMPORT));

    TRY(push_node(parser, import_node, NT_IDENTIFIER, &ident_node));
    token.begin       = "base";
    token.length      = 4;
    token.type        = TT_IDENTIFIER;
    ident_node->token = token;

    TRY(push_node(parser, import_node, NT_IDENTIFIER, &ident_node));
    token.begin       = "*";
    token.length      = 1;
    token.type        = TT_OPERATOR;
    token.op          = OT_MUL;
    ident_node->token = token;

    import_node->next = ast->children;
    ast->children     = import_node;

cleanup:
    return error;
}

void kos_parser_destroy(KOS_PARSER *parser)
{
    parser->ast_buf = KOS_NULL;
}

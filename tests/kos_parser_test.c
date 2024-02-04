/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_system.h"
#include "../core/kos_ast.h"
#include "../core/kos_misc.h"
#include "../core/kos_parser.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _MSC_VER
#   pragma warning( disable : 4996 ) /* strerror: This function or variable may be unsafe */
#endif

static const char *const node_types[] = {
    "empty",
    "import",
    "scope",
    "if",
    "try-catch",
    "try-defer",
    "switch",
    "repeat",
    "while",
    "for_in",
    "continue",
    "break",
    "return",
    "throw",
    "assert",
    "refinement",
    "opt_refinement",
    "slice",
    "invocation",
    "var",
    "const",
    "export",
    "operator",
    "yield",
    "async",
    "assignment",
    "multi_assignment",
    "interpolated_string",
    "left_hand_side",
    "name",
    "name_const",
    "parameters",
    "ellipsis",
    "expand",
    "property",
    "named_arguments",
    "in",
    "catch",
    "default",
    "case",
    "fallthrough",
    "landmark",
    "placeholder",
    "identifier",
    "number",
    "string",
    "this",
    "super-ctor",
    "super-proto",
    "line",
    "bool",
    "void",
    "function",
    "constructor",
    "class",
    "array",
    "object"
};

static const char *const separators[] = {
    "none",
    "paren_open",
    "paren_close",
    ",",
    ":",
    ";",
    "[",
    "]",
    "{",
    "}",
    "$"
};

struct OPERATOR_MAP {
    KOS_OPERATOR_TYPE op;
    const char       *name;
};

const struct OPERATOR_MAP operators[] = {
    { OT_ADD,        "+"      },
    { OT_SUB,        "-"      },
    { OT_MUL,        "*"      },
    { OT_DIV,        "/"      },
    { OT_MOD,        "%"      },
    { OT_NOT,        "~"      },
    { OT_LOGNOT,     "!"      },
    { OT_AND,        "&"      },
    { OT_OR,         "|"      },
    { OT_XOR,        "^"      },
    { OT_SHL,        "<<"     },
    { OT_SHR,        ">>"     },
    { OT_SHRU,       ">>>"    },
    { OT_LOGAND,     "&&"     },
    { OT_LOGOR,      "||"     },
    { OT_LOGTRI,     "?:"     },
    { OT_DOT,        "."      },
    { OT_MORE,       "..."    },
    { OT_ARROW,      "->"     },
    { OT_LAMBDA,     "=>"     },
    { OT_EQ,         "=="     },
    { OT_NE,         "!="     },
    { OT_GE,         ">="     },
    { OT_GT,         ">"      },
    { OT_LE,         "<="     },
    { OT_LT,         "<"      },
    { OT_SET,        "="      },
    { OT_SETADD,     "+="     },
    { OT_SETSUB,     "-="     },
    { OT_SETMUL,     "*="     },
    { OT_SETDIV,     "/="     },
    { OT_SETMOD,     "%="     },
    { OT_SETAND,     "&="     },
    { OT_SETOR,      "|="     },
    { OT_SETXOR,     "^="     },
    { OT_SETSHL,     "<<="    },
    { OT_SETSHR,     ">>="    },
    { OT_SETSHRU,    ">>>="   },
    { OT_NONE,       "none"   }
};

static void append_str_len(char **out, char *end, const char *str, unsigned len)
{
    const unsigned max_copy = (unsigned)(end - *out);
    const unsigned copy_len = len > max_copy ? max_copy : len;

    if (copy_len) {
        memcpy(*out, str, copy_len);
        *out += copy_len;
    }
}

static void append_str(char **out, char *end, const char *str)
{
    append_str_len(out, end, str, (unsigned)strlen(str));
}

static void append_int(char **out, char *end, int value)
{
    char buf[16];
    const int len = snprintf(buf, sizeof(buf), "%d", value);
    append_str_len(out, end, buf, (unsigned)len);
}

#define WALK_BUF_SIZE 512

static int compare_output(const char  *actual,
                          const char **expected,
                          const char  *expected_end)
{
    const char *exp   = *expected;
    int         error = KOS_SUCCESS;

    for (;;) {

        for ( ; *actual && (unsigned char)*actual <= 0x20; ++actual) { }

        for ( ; exp < expected_end && (unsigned char)*exp <= 0x20; ++exp) { }

        if (!*actual)
            break;

        if (exp >= expected_end)
            break;

        if (*actual != *exp) {
            printf("AST does not match expected output!\n");
            printf("'%c' != '%c'\n", *actual, *exp);
            error = KOS_ERROR_INTERNAL;
            break;
        }
        ++actual;
        ++exp;
    }

    *expected = exp;

    return error;
}

static int walk_tree(const KOS_AST_NODE *node,
                     int                 level,
                     int                 print,
                     const char        **compare,
                     const char         *compare_end)
{
    char             buf[WALK_BUF_SIZE];
    const KOS_TOKEN *token    = &node->token;
    char            *out      = buf;
    char            *end      = buf + WALK_BUF_SIZE - 1;
    int              error    = KOS_SUCCESS;
    int              one_line = node->children ? 0 : 1;
    const char      *indent   = "                                                                                                                                                                                                        ";
    int              i;
    int              indent_shift = 4;

    assert(level < 128);

    append_str_len(&out, end, indent, (unsigned)(level * indent_shift));
    append_str_len(&out, end, "(", 1);
    append_str(&out, end, node_types[node->type]);
    append_str_len(&out, end, " ", 1);
    append_int(&out, end, (int)token->line);
    append_str_len(&out, end, " ", 1);
    append_int(&out, end, (int)token->column);

    if (token->type == TT_OPERATOR) {
        append_str_len(&out, end, " ", 1);
        for (i=0; operators[i].op != (KOS_OPERATOR_TYPE)token->op &&
                  operators[i].op != OT_NONE; ++i);
        append_str(&out, end, operators[i].name);
    }
    else if (node->type == NT_OPERATOR) {
        append_str_len(&out, end, " ", 1);
        append_str_len(&out, end, token->begin, token->length);
    }
    else if (token->type == TT_SEPARATOR) {
        append_str_len(&out, end, " ", 1);
        append_str(&out, end, separators[token->sep]);
    }
    else if (token->type == TT_NUMERIC     || token->type == TT_IDENTIFIER ||
             token->type == TT_STRING_OPEN || token->type == TT_STRING     ||
             token->type == TT_KEYWORD) {
        append_str_len(&out, end, " ", 1);
        append_str_len(&out, end, token->begin, token->length);
    }
    else if (node->type == NT_BOOL_LITERAL) {
        append_str_len(&out, end, " ", 1);
        append_str_len(&out, end, token->begin, token->length);
    }

    if (one_line)
        append_str_len(&out, end, ")", 1);

    *out = 0;

    if (print)
        printf("%s\n", buf);

    if (compare)
        error = compare_output(buf, compare, compare_end);

    if (!error) {

        for (node = node->children; node; node = node->next) {
            error = walk_tree(node, level + 1, print, compare, compare_end);
            if (error)
                break;
        }

        if (!error && !one_line) {
            if (print)
                printf("%.*s)\n", level * indent_shift, indent);

            if (compare)
                error = compare_output(")", compare, compare_end);
        }
    }

    return error;
}

static void scan_until_eol(const char **buf, const char *end)
{
    const char *ptr = *buf;

    for ( ; ptr < end && *ptr != '\r' && *ptr != '\n'; ++ptr) { }

    if (ptr + 1 < end && *ptr == '\r' && ptr[1] == '\n')
        ++ptr;

    if (ptr < end)
        ++ptr;

    *buf = ptr;
}

static void skip_spaces(const char **buf, const char *end)
{
    const char *ptr = *buf;

    while (ptr < end && (unsigned char)*ptr <= 0x20)
        ++ptr;

    *buf = ptr;
}

static int scan_int(const char **buf, const char *end, int *value)
{
    int         error = KOS_SUCCESS;
    const char *ptr   = *buf;
    const char *value_str;

    skip_spaces(&ptr, end);

    value_str = ptr;

    for ( ; ptr < end && *ptr >= '0' && *ptr <= '9'; ++ptr) { }

    if (value_str == ptr) {
        printf("Invalid input - expected integer!\n");
        error =  KOS_ERROR_INTERNAL;
    }
    else {
        int64_t value64 = 0;
        error = kos_parse_int(value_str, ptr, &value64);

        if ( ! error) {
            if (value64 < 0 || value64 > 1024) {
                printf("Invalid input - expected integer!\n");
                error = KOS_ERROR_INTERNAL;
            }
            else
                *value = (int)value64;
        }
    }

    *buf = ptr;

    return error;
}

int main(int argc, char *argv[])
{
    KOS_PARSER           parser;
    struct KOS_FILEBUF_S file_buf;
    KOS_MEMPOOL          allocator;
    const char          *file_end;
    const char          *buf;
    int                  error;
    int                  test  = 1;
    int                  print = 0;
    const char          *usage = "Usage: kos_parser_test [-verbose] [-notest] <testfile>\n";

    KOS_filebuf_init(&file_buf);

    KOS_mempool_init(&allocator);

    if (argc < 2 || argc > 4) {
        printf("%s", usage);
        return 1;
    }

    {
        int iarg;

        for (iarg=1; iarg < argc-1; iarg++) {
            if (!strcmp(argv[iarg], "-verbose"))
                print = 1;
            else if (!strcmp(argv[iarg], "-notest"))
                test = 0;
            else {
                printf("Invalid option - %s\n%s", argv[iarg], usage);
                return 1;
            }
        }

        error = KOS_load_file(argv[iarg], &file_buf);

        switch (error) {

            case KOS_SUCCESS:
                /* fall through */
            default:
                assert( ! error);
                break;

            case KOS_ERROR_ERRNO:
                printf("Failed to open file %s: %s\n", argv[iarg], strerror(errno));
                return 1;
        }
    }

    file_end = file_buf.buffer + file_buf.size;
    buf      = file_buf.buffer;

    while (buf < file_end) {

        const int   invalid_file = 1024; /* Not a valid error code */
        int         expected_error;
        int         line   = 0;
        int         column = 0;
        const char *end;

        for (end = buf; end < file_end; ++end) {
            if (*end == '@' &&
                (end == file_buf.buffer || *(end-1) == '\r' || *(end-1) == '\n'))
                break;
        }

        kos_parser_init(&parser, &allocator, 0, buf, end);

        ++end;

        if (end >= file_end)
            expected_error = invalid_file;
        else {
            error = scan_int(&end, file_end, &expected_error);
            if (error)
                break;

            if (expected_error) {

                error = scan_int(&end, file_end, &line);
                if (error)
                    break;

                error = scan_int(&end, file_end, &column);
                if (error)
                    break;
            }
        }

        scan_until_eol(&end, file_end);

        if (!error) {

            KOS_AST_NODE *ast;

            const int test_error = kos_parser_parse(&parser, &ast);

            if (test_error != expected_error && test) {
                printf("Invalid error code returned by parser: %d, but expected %d\n",
                       test_error, expected_error);
                if (test_error)
                    printf("%u:%u: \"%s\"\n", parser.token.line, parser.token.column, parser.error_str);
                error = test_error ? test_error : expected_error;
                break;
            }

            if (!test_error)
                error = walk_tree(ast, 0, print, test ? &end : 0, file_end);

            else if (test) {
                const KOS_FILE_POS pos = get_token_pos(&parser.token);
                if ((unsigned)line   != pos.line ||
                    (unsigned)column != pos.column) {

                    printf("Invalid error location: %u:%u, but expected %d:%d\n",
                           pos.line, pos.column, line, column);
                    error = KOS_ERROR_INTERNAL;
                }
            }

            kos_parser_destroy(&parser);
        }

        if (error)
            break;

        if (!test) {
            for ( ; end < file_end; ++end) {
                if (*end == '@' && (*(end-1) == '\r' || *(end-1) == '\n'))
                    break;
            }
        }

        skip_spaces(&end, file_end);

        if (end < file_end && *end != '@') {
            printf("AST does not match expected output!\n");
            error = KOS_ERROR_INTERNAL;
            break;
        }

        scan_until_eol(&end, file_end);

        buf = end;
    }

    KOS_mempool_destroy(&allocator);

    KOS_unload_file(&file_buf);

    if (error) {

        printf("ERROR %d\n", error);

        return 1;
    }

    return 0;
}

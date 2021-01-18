/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_getline.h"
#include "../core/kos_heap.h"
#include "../core/kos_misc.h"
#include "../core/kos_parser.h"
#include "../core/kos_perf.h"
#include "../core/kos_system.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TO_STR_INTERNAL(x) #x
#define TO_STR(x) TO_STR_INTERNAL(x)
#define KOS_VERSION_STRING "Kos " TO_STR(KOS_VERSION_MAJOR) "." TO_STR(KOS_VERSION_MINOR)

static const char str_cmdline[]     = "<commandline>";
static const char str_import_base[] = "import base.*";
static const char str_stdin[]       = "<stdin>";

static int is_option(const char *arg,
                     const char *short_opt,
                     const char *long_opt);

static void print_usage(void);

static int run_interactive(KOS_CONTEXT ctx, KOS_VECTOR *buf);

int main(int argc, char *argv[])
{
    PROF_ZONE(MODULE)

    int          error       = KOS_SUCCESS;
    int          inst_ok     = 0;
    int          is_script   = 0;
    int          i_module    = 0;
    int          i_first_arg = 0;
    int          interactive = -1;
    uint32_t     mem_size    = 0;
    uint32_t     flags       = 0;
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    KOS_VECTOR   buf;

    KOS_vector_init(&buf);

    setlocale(LC_ALL, "");

    if (argc == 2 && is_option(argv[1], "h", "help")) {
        print_usage();
        goto cleanup;
    }

#ifdef _WIN32
    if (argc == 2 && is_option(argv[1], "?", KOS_NULL)) {
        print_usage();
        goto cleanup;
    }
#endif

    if (argc == 2 && is_option(argv[1], KOS_NULL, "version")) {
        printf(KOS_VERSION_STRING "\n");
        goto cleanup;
    }

    if (argc > 1) {
        i_first_arg = 1;

        while (i_first_arg < argc) {

            const char *const arg = argv[i_first_arg];

            if (strcmp(arg, "-") == 0) {
                i_first_arg = (argc - i_first_arg) > 1 ? i_first_arg + 1 : 0;
                break;
            }

            if (is_option(arg, "c", "command")) {
                if ((argc - i_first_arg) == 1) {
                    fprintf(stderr, "Argument expected for the -c option\n");
                    error = KOS_ERROR_NOT_FOUND;
                    goto cleanup;
                }
                is_script   = 1;
                i_module    = i_first_arg + 1;
                i_first_arg = (argc - i_first_arg) > 2 ? i_first_arg + 2 : 0;
                break;
            }

            if (is_option(arg, "m", "memsize")) {
                const char *ms_begin;
                int64_t     value;

                if ((argc - i_first_arg) == 1) {
                    fprintf(stderr, "Argument expected for the -m option\n");
                    error = KOS_ERROR_NOT_FOUND;
                    goto cleanup;
                }
                ms_begin = argv[i_first_arg + 1];
                error = kos_parse_int(ms_begin, ms_begin + strlen(ms_begin), &value);
                if (error || (value < 2) || (value > 4096)) {
                    fprintf(stderr, "Invalid value passed with the -m option\n");
                    goto cleanup;
                }
                /* <<20 because it is in MB, however we divide it into two halves
                 * for the heap and for malloc area, so only <<19. */
                mem_size = (uint32_t)value << 19;
                i_first_arg += 2;
                continue;
            }

            if (is_option(arg, "v", "verbose")) {
                flags |= KOS_INST_VERBOSE;
                ++i_first_arg;
            }
            else if (is_option(arg, "vv", KOS_NULL)) {
                flags |= KOS_INST_VERBOSE | KOS_INST_DEBUG;
                ++i_first_arg;
            }
            else {
                i_module    = i_first_arg;
                i_first_arg = (argc - i_first_arg) > 1 ? i_first_arg + 1 : 0;
                break;
            }
        }
    }

    /* KOSDISASM=1 turns on disassembly */
    if (!kos_get_env("KOSDISASM", &buf) &&
            buf.size == 2 && buf.buffer[0] == '1' && buf.buffer[1] == 0)
        flags |= KOS_INST_DISASM;

    /* KOSINTERACTIVE=1 forces interactive prompt       */
    /* KOSINTERACTIVE=0 forces treating stdin as a file */
    if (!kos_get_env("KOSINTERACTIVE", &buf) &&
            buf.size == 2 && buf.buffer[1] == 0) {

        if (buf.buffer[0] == '0')
            interactive = 0;
        else if (buf.buffer[0] == '1')
            interactive = 1;
    }

    error = KOS_instance_init(&inst, flags, &ctx);

    if (error) {
        fprintf(stderr, "Failed to initialize interpreter\n");
        goto cleanup;
    }

    if (mem_size) {
        inst.heap.max_heap_size   = mem_size;
        inst.heap.max_malloc_size = mem_size;
    }

    inst_ok = 1;

    /* Use executable path from OS to find modules */
    /* KOSNODEFAULTPATH=1 disables default paths */
    if (kos_get_env("KOSNODEFAULTPATH", &buf) || buf.size != 2 ||
                buf.buffer[0] != '1' || buf.buffer[1] != 0)
        error = KOS_instance_add_default_path(ctx, KOS_NULL);

    /* Fallback: use argv[0] to find modules */
    if (error)
        error = KOS_instance_add_default_path(ctx, argv[0]);

    if (error) {
        fprintf(stderr, "Failed to setup module search paths\n");
        goto cleanup;
    }

    if (i_first_arg) {

        char *saved    = argv[i_first_arg - 1];
        char  empty[1] = { '\0' };

        argv[i_first_arg - 1] = i_module ? argv[i_module] : &empty[0];

        error = KOS_instance_set_args(ctx,
                                      1 + argc - i_first_arg,
                                      (const char **)&argv[i_first_arg - 1]);

        argv[i_first_arg - 1] = saved;
    }
    else if (i_module)
        error = KOS_instance_set_args(ctx, 1, (const char**)&argv[i_module]);
    else {
        const char *empty_ptr[] = { "" };
        error = KOS_instance_set_args(ctx, 1, empty_ptr);
    }

    if (error) {
        fprintf(stderr, "Failed to setup command line arguments\n");
        goto cleanup;
    }

    error = KOS_modules_init(ctx);

    if (error) {
        fprintf(stderr, "Failed to initialize modules\n");
        goto cleanup;
    }

    if (i_module) {

        const unsigned mod_name_len = (unsigned)strlen(argv[i_module]);

        /* Load script from command line */
        if (is_script) {
            const KOS_OBJ_ID module_obj = KOS_load_module_from_memory(ctx,
                                                                      str_cmdline,
                                                                      sizeof(str_cmdline) - 1,
                                                                      str_import_base,
                                                                      sizeof(str_import_base) - 1);

            if (IS_BAD_PTR(module_obj))
                error = KOS_ERROR_EXCEPTION;
            else
                error = IS_BAD_PTR(KOS_run_module(ctx, module_obj)) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;

            if ( ! error) {
                KOS_OBJ_ID ret = KOS_repl(ctx, str_cmdline, argv[i_module], mod_name_len);
                if (IS_BAD_PTR(ret))
                    error = KOS_ERROR_EXCEPTION;
            }
        }
        /* Load script from a file */
        else {
            const KOS_OBJ_ID module_obj = KOS_load_module(ctx, argv[i_module], mod_name_len);

            if (IS_BAD_PTR(module_obj))
                error = KOS_ERROR_EXCEPTION;
            else
                error = IS_BAD_PTR(KOS_run_module(ctx, module_obj)) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
        }
    }
    else {

        const KOS_OBJ_ID module_obj = KOS_load_module_from_memory(ctx,
                                                                  str_stdin,
                                                                  sizeof(str_stdin) - 1,
                                                                  str_import_base,
                                                                  sizeof(str_import_base) - 1);

        if (IS_BAD_PTR(module_obj))
            error = KOS_ERROR_EXCEPTION;
        else
            error = IS_BAD_PTR(KOS_run_module(ctx, module_obj)) ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;

        if ( ! error) {

            if (interactive < 0)
                interactive = kos_is_stdin_interactive();

            /* Load subsequent pieces of script from interactive prompt */
            if (interactive)
                error = run_interactive(ctx, &buf);

            /* Load script from stdin */
            else {
                KOS_OBJ_ID ret = KOS_repl_stdin(ctx, str_stdin);
                if (IS_BAD_PTR(ret))
                    error = KOS_ERROR_EXCEPTION;
            }
        }
    }

    if (error == KOS_ERROR_EXCEPTION)
        KOS_print_exception(ctx, KOS_STDERR);
    else if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Out of memory\n");
    }

cleanup:
    if (inst_ok)
        KOS_instance_destroy(&inst);

    KOS_vector_destroy(&buf);

    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int is_option(const char *arg,
                     const char *short_opt,
                     const char *long_opt)
{
#ifdef _WIN32
    if (arg[0] != '-' && arg[0] != '/')
#else
    if (arg[0] != '-')
#endif
        return 0;

    if (long_opt) {

        if (arg[1] == '-')
            return ! strcmp(arg + 2, long_opt);

        if ( ! strcmp(arg + 1, long_opt))
            return 1;
    }

    if (short_opt)
        return ! strcmp(arg + 1, short_opt);

    return 0;
}

static void print_usage(void)
{
    printf("Usage: kos [option...] [-c cmd | file] [arg...]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -m M, --memsize M   Total size of heaps in MB\n");
    printf("  -v, --verbose       Enable verbose output\n");
    printf("  -vv                 Enable more verbose output\n");
    printf("\n");
}

static int is_input_complete(KOS_VECTOR *buf,
                             KOS_VECTOR *tmp,
                             int        *out_error)
{
    PROF_ZONE(MODULE)

    KOS_PARSER             parser;
    struct KOS_MEMPOOL_S   mempool;
    int                    error;
    struct KOS_AST_NODE_S *out;

    KOS_mempool_init(&mempool);

    kos_parser_init(&parser, &mempool, 0, buf->buffer, buf->buffer + buf->size);

    error = kos_parser_parse(&parser, &out);

    kos_parser_destroy(&parser);

    KOS_mempool_destroy(&mempool);

    return error != KOS_ERROR_PARSE_FAILED || parser.token.type != TT_EOF;
}

static int enforce_eol(KOS_VECTOR *buf)
{
    const char c = buf->size ? buf->buffer[buf->size - 1] : 0;

    if (c != '\r' && c != '\n') {

        const int error = KOS_vector_resize(buf, buf->size + 1);
        if (error)
            return error;

        buf->buffer[buf->size - 1] = '\n';
    }

    return KOS_SUCCESS;
}

static int run_interactive(KOS_CONTEXT ctx, KOS_VECTOR *buf)
{
    PROF_ZONE(MODULE)

    int         error;
    int         genline_init = 0;
    int         stored_errno = 0;
    KOS_GETLINE state;
    KOS_LOCAL   print_args;
    KOS_VECTOR  tmp_buf;

    KOS_vector_init(&tmp_buf);

    printf(KOS_VERSION_STRING " interactive interpreter\n");

    KOS_init_local_with(ctx, &print_args, KOS_new_array(ctx, 1));
    TRY_OBJID(print_args.o);

    error = kos_getline_init(&state);
    if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Failed to initialize command line editor\n");
        goto cleanup;
    }
    genline_init = 1;

    buf->size = 0;

    for (;;) {
        KOS_OBJ_ID ret;

        KOS_suspend_context(ctx);

        error = kos_getline(&state, PROMPT_FIRST_LINE, buf);

        if (error == KOS_ERROR_ERRNO)
            stored_errno = errno;

        KOS_resume_context(ctx);

        if (error) {
            if (error == KOS_ERROR_INTERRUPTED)
                continue;

            assert(error == KOS_SUCCESS_RETURN ||
                   error == KOS_ERROR_OUT_OF_MEMORY ||
                   error == KOS_ERROR_ERRNO);
            break;
        }

        assert(buf->size == 0 || buf->buffer[buf->size - 1] != 0);

        while ( ! is_input_complete(buf, &tmp_buf, &error)) {

            tmp_buf.size = 0;

            KOS_suspend_context(ctx);

            error = kos_getline(&state, PROMPT_SUBSEQUENT_LINE, &tmp_buf);

            if (error == KOS_ERROR_ERRNO)
                stored_errno = errno;

            KOS_resume_context(ctx);

            if (error) {
                assert(error == KOS_ERROR_OUT_OF_MEMORY ||
                       error == KOS_ERROR_INTERRUPTED   ||
                       error == KOS_ERROR_ERRNO         ||
                       error == KOS_SUCCESS_RETURN);
                break;
            }

            assert(tmp_buf.size == 0 || tmp_buf.buffer[tmp_buf.size - 1] != 0);

            error = enforce_eol(buf);
            if (error)
                break;

            error = KOS_vector_concat(buf, &tmp_buf);
            if (error)
                break;
        }
        if (error) {
            if (error == KOS_ERROR_INTERRUPTED)
                buf->size = 0;
            else
                break;
        }

        if ( ! buf->size)
            continue;

        ret = KOS_repl(ctx, str_stdin, buf->buffer, (unsigned)buf->size);
        buf->size = 0;

        if (IS_BAD_PTR(ret)) {
            KOS_print_exception(ctx, KOS_STDERR);
            KOS_clear_exception(ctx);
            continue;
        }

        if ( ! IS_SMALL_INT(ret) && GET_OBJ_TYPE(ret) == OBJ_VOID)
            continue;

        TRY(KOS_array_write(ctx, print_args.o, 0, ret));

        TRY(KOS_print_to_cstr_vec(ctx,
                                  print_args.o,
                                  KOS_QUOTE_STRINGS,
                                  buf,
                                  "",
                                  0));

        if (buf->size) {
            buf->buffer[buf->size - 1] = '\n';
            fwrite(buf->buffer, 1, buf->size, stdout);
        }
        else
            printf("\n");

        buf->size = 0;
    }

    if (error == KOS_ERROR_ERRNO) {
        KOS_raise_errno_value(ctx, KOS_NULL, stored_errno);
        error = KOS_ERROR_EXCEPTION;
    }

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

cleanup:
    if (genline_init)
        kos_getline_destroy(&state);

    KOS_vector_destroy(&tmp_buf);

    return error;
}

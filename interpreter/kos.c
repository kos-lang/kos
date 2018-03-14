/*
 * Copyright (c) 2014-2018 Chris Dragan
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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_system.h"
#include "../core/kos_getline.h"
#include "../core/kos_memory.h"
#include "../core/kos_object_alloc.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TO_STR_INTERNAL(x) #x
#define TO_STR(x) TO_STR_INTERNAL(x)
#define KOS_VERSION_STRING "Kos " TO_STR(KOS_VERSION_MAJOR) "." TO_STR(KOS_VERSION_MINOR)

static const char str_cmdline[]     = "<commandline>";
static const char str_import_lang[] = "import lang.*";
static const char str_stdin[]       = "<stdin>";

static int _is_option(const char *arg,
                      const char *short_opt,
                      const char *long_opt);

static void _print_usage(void);

static int _run_interactive(KOS_FRAME frame, struct _KOS_VECTOR *buf);

int main(int argc, char *argv[])
{
    int                error       = KOS_SUCCESS;
    int                ctx_ok      = 0;
    int                is_script   = 0;
    int                i_module    = 0;
    int                i_first_arg = 0;
    int                interactive = -1;
    uint32_t           flags       = 0;
    KOS_CONTEXT        ctx;
    KOS_FRAME          frame;
    struct _KOS_VECTOR buf;

    _KOS_vector_init(&buf);

    setlocale(LC_ALL, "");

    if (argc == 2 && _is_option(argv[1], "h", "help")) {
        _print_usage();
        goto _error;
    }

#ifdef _WIN32
    if (argc == 2 && _is_option(argv[1], "?", 0)) {
        _print_usage();
        goto _error;
    }
#endif

    if (argc == 2 && _is_option(argv[1], 0, "version")) {
        printf(KOS_VERSION_STRING "\n");
        goto _error;
    }

    if (argc > 1) {
        i_first_arg = 1;

        while (i_first_arg < argc) {

            const char *const arg = argv[i_first_arg];

            if (strcmp(arg, "-") == 0) {
                i_first_arg = (argc - i_first_arg) > 1 ? i_first_arg + 1 : 0;
                break;
            }

            if (_is_option(arg, "c", "command")) {
                if ((argc - i_first_arg) == 1) {
                    fprintf(stderr, "Argument expected for the -c option\n");
                    error = KOS_ERROR_NOT_FOUND;
                    goto _error;
                }
                is_script   = 1;
                i_module    = i_first_arg + 1;
                i_first_arg = (argc - i_first_arg) > 2 ? i_first_arg + 2 : 0;
                break;
            }

            if (_is_option(arg, "v", "verbose")) {
                flags |= KOS_CTX_VERBOSE;
                ++i_first_arg;
            }
            else {
                i_module    = i_first_arg;
                i_first_arg = (argc - i_first_arg) > 1 ? i_first_arg + 1 : 0;
                break;
            }
        }
    }

    error = KOS_context_init(&ctx, &frame);

    if (error) {
        fprintf(stderr, "Failed to initialize interpreter\n");
        goto _error;
    }

    ctx_ok = 1;

    {
        /* KOSDISASM=1 turns on disassembly */
        if (!_KOS_get_env("KOSDISASM", &buf) &&
                buf.size == 2 && buf.buffer[0] == '1' && buf.buffer[1] == 0)
            flags |= KOS_CTX_DISASM;

        /* KOSINTERACTIVE=1 forces interactive prompt       */
        /* KOSINTERACTIVE=0 forces treating stdin as a file */
        if (!_KOS_get_env("KOSINTERACTIVE", &buf) &&
                buf.size == 2 && buf.buffer[1] == 0) {

            if (buf.buffer[0] == '0')
                interactive = 0;
            else if (buf.buffer[0] == '1')
                interactive = 1;
        }
    }

    ctx.flags = flags;

    error = KOS_context_add_default_path(frame, argv[0]);

    if (error) {
        fprintf(stderr, "Failed to setup module search paths\n");
        goto _error;
    }

    if (i_first_arg) {

        char *saved   = argv[i_first_arg - 1];
        char  empty[] = "";

        argv[i_first_arg - 1] = i_module ? argv[i_module] : &empty[0];

        error = KOS_context_set_args(frame,
                                     1 + argc - i_first_arg,
                                     (const char **)&argv[i_first_arg - 1]);

        argv[i_first_arg - 1] = saved;
    }
    else if (i_module)
        error = KOS_context_set_args(frame, 1, (const char**)&argv[i_module]);
    else {
        const char *empty_ptr[] = { "" };
        error = KOS_context_set_args(frame, 1, empty_ptr);
    }

    if (error) {
        fprintf(stderr, "Failed to setup command line arguments\n");
        goto _error;
    }

    error = KOS_modules_init(&ctx);

    if (error) {
        fprintf(stderr, "Failed to initialize modules\n");
        goto _error;
    }

    if (i_module) {

        /* Load script from command line */
        if (is_script) {
            error = KOS_load_module_from_memory(frame, str_cmdline, str_import_lang, sizeof(str_import_lang));

            if ( ! error) {
                KOS_OBJ_ID ret = KOS_repl(frame, str_cmdline, argv[i_module], (unsigned)strlen(argv[i_module]));
                if (IS_BAD_PTR(ret))
                    error = KOS_ERROR_EXCEPTION;
            }
        }
        /* Load script from a file */
        else
            error = KOS_load_module(frame, argv[i_module]);
    }
    else {

        error = KOS_load_module_from_memory(frame, str_stdin, str_import_lang, sizeof(str_import_lang));

        if ( ! error) {

            if (interactive < 0)
                interactive = _KOS_is_stdin_interactive();

            /* Load subsequent pieces of script from interactive prompt */
            if (interactive)
                error = _run_interactive(frame, &buf);

            /* Load script from stdin */
            else {
                KOS_OBJ_ID ret = KOS_repl_stdin(frame, str_stdin);
                if (IS_BAD_PTR(ret))
                    error = KOS_ERROR_EXCEPTION;
            }
        }
    }

    if (error == KOS_ERROR_EXCEPTION)
        KOS_print_exception(frame);
    else if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Out of memory\n");
    }

_error:
    if (ctx_ok)
        KOS_context_destroy(&ctx);

    _KOS_vector_destroy(&buf);

    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int _is_option(const char *arg,
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

static void _print_usage(void)
{
    printf("Usage: kos [option...] [-c cmd | file] [arg...]\n");
}

static int _run_interactive(KOS_FRAME frame, struct _KOS_VECTOR *buf)
{
    int                 error;
    struct _KOS_GETLINE state;
    KOS_OBJ_ID          print_args;
    int                 genline_init = 0;

    printf(KOS_VERSION_STRING " interactive interpreter\n");

    print_args = KOS_new_array(frame, 1);
    TRY_OBJID(print_args);

    error = _KOS_getline_init(&state);
    if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Failed to initialize command line editor\n");
        goto _error;
    }
    genline_init = 1;

    buf->size = 0;

    for (;;) {
        KOS_OBJ_ID ret;

        error = _KOS_getline(&state, PROMPT_FIRST_LINE, buf);
        if (error) {
            assert(error == KOS_SUCCESS_RETURN || error == KOS_ERROR_OUT_OF_MEMORY);
            break;
        }

        if ( ! buf->size)
            continue;

        /* TODO parse check if more lines need to be read */

        ret = KOS_repl(frame, str_stdin, buf->buffer, (unsigned)buf->size);
        buf->size = 0;

        if (IS_BAD_PTR(ret)) {
            KOS_print_exception(frame);
            KOS_clear_exception(frame);
            continue;
        }

        if ( ! IS_SMALL_INT(ret) && GET_OBJ_TYPE(ret) == OBJ_VOID)
            continue;

        TRY(KOS_array_write(frame, print_args, 0, ret));

        TRY(KOS_print_to_cstr_vec(frame,
                                  print_args,
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

    if (error == KOS_SUCCESS_RETURN)
        error = KOS_SUCCESS;

_error:
    if (genline_init)
        _KOS_getline_destroy(&state);

    return error;
}

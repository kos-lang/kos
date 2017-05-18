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

#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_utils.h"
#include "../core/kos_file.h"
#include "../core/kos_memory.h"
#include "../core/kos_try.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _is_option(const char *arg,
                      const char *short_opt,
                      const char *long_opt);

static void _print_usage(void);

static void _print_version(void);

static int _add_module_search_paths(KOS_FRAME frame, const char *kos_exe);

int main(int argc, char *argv[])
{
    int         error       = KOS_SUCCESS;
    int         ctx_ok      = 0;
    int         is_script   = 0;
    int         i_module    = 0;
    int         i_first_arg = 0;
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;

    static const char str_cmdline[]     = "<commandline>";
    static const char str_import_lang[] = "import lang.*";

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
        _print_version();
        goto _error;
    }

    if (argc < 2) {
        /* TODO implement interactive shell */
        fprintf(stderr, "Arguments missing\n");
        printf("\n");
        _print_usage();
        TRY(KOS_ERROR_EXCEPTION);
    }

    if (argc > 2 && _is_option(argv[1], "c", "command")) {
        is_script = 1;
        i_module  = 2;
        if (argc > 3)
            i_first_arg = 3;
    }
    else {
        i_module = 1;
        if (argc > 2)
            i_first_arg = 2;
    }

    error = KOS_context_init(&ctx, &frame);

    if (error) {
        fprintf(stderr, "Failed to initialize interpreter\n");
        TRY(error);
    }

    ctx_ok = 1;

    /* KOSDISASM=1 turns on disassembly */
    {
        struct _KOS_VECTOR cstr;

        _KOS_vector_init(&cstr);

        if (!_KOS_get_env("KOSDISASM", &cstr) &&
                cstr.size == 2 && cstr.buffer[0] == '1' && cstr.buffer[1] == 0)
            ctx.flags |= KOS_CTX_DEBUG;

        _KOS_vector_destroy(&cstr);
    }

    error = _add_module_search_paths(frame, argv[0]);

    if (error) {
        fprintf(stderr, "Failed to setup module search paths\n");
        TRY(error);
    }

    if (i_first_arg) {

        error = KOS_context_set_args(frame, argc - i_first_arg, &argv[i_first_arg]);

        if (error) {
            fprintf(stderr, "Failed to setup command line arguments\n");
            TRY(error);
        }
    }

    error = KOS_modules_init(&ctx);

    if (error) {
        fprintf(stderr, "Failed to initialize modules\n");
        TRY(error);
    }

    if (is_script) {
        error = KOS_load_module_from_memory(frame, str_cmdline, str_import_lang, sizeof(str_import_lang));

        if ( ! error)
            error = KOS_repl(frame, str_cmdline, argv[i_module], (unsigned)strlen(argv[i_module]));
    }
    else
        error = KOS_load_module(frame, argv[i_module]);

    if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        KOS_print_exception(frame);
    }

_error:
    if (ctx_ok)
        KOS_context_destroy(&ctx);

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

static void _print_version(void)
{
    printf("Kos 0.1\n");
}

#ifndef CONFIG_MODULE_PATH
#define CONFIG_MODULE_PATH "modules"
#endif

static int _add_module_search_paths(KOS_FRAME frame, const char *kos_exe)
{
    struct _KOS_VECTOR cstr;
    int                error;
    static const char  rel_module_path[] = CONFIG_MODULE_PATH;
    const char        *kos_exe_end       = kos_exe + strlen(kos_exe) - 1;
    size_t             kos_exe_len;

    _KOS_vector_init(&cstr);

    while (kos_exe_end >= kos_exe && *kos_exe_end != KOS_PATH_SEPARATOR)
        --kos_exe_end;
    ++kos_exe_end;

    kos_exe_len = (size_t)(kos_exe_end - kos_exe);

    TRY(_KOS_vector_resize(&cstr, kos_exe_len + sizeof(rel_module_path)));
    memcpy(cstr.buffer, kos_exe, kos_exe_len);
    memcpy(cstr.buffer + kos_exe_len, rel_module_path, sizeof(rel_module_path));

    TRY(KOS_context_add_path(frame, cstr.buffer));

_error:
    _KOS_vector_destroy(&cstr);

    return error;
}

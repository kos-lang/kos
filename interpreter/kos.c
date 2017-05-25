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
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_file.h"
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

static int _add_module_search_paths(KOS_FRAME frame, const char *kos_exe);

static int _run_interactive(KOS_FRAME frame, struct _KOS_VECTOR *buf);

int main(int argc, char *argv[])
{
    int                error       = KOS_SUCCESS;
    int                ctx_ok      = 0;
    int                is_script   = 0;
    int                i_module    = 0;
    int                i_first_arg = 0;
    int                interactive = -1;
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

    if (argc > 1 && strcmp(argv[1], "-")) {

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
    }
    else if (argc > 1)
        i_first_arg = (argc > 2) ? 2 : 0;

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
            ctx.flags |= KOS_CTX_DEBUG;

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

    error = _add_module_search_paths(frame, argv[0]);

    if (error) {
        fprintf(stderr, "Failed to setup module search paths\n");
        goto _error;
    }

    if (i_first_arg) {

        error = KOS_context_set_args(frame, argc - i_first_arg, &argv[i_first_arg]);

        if (error) {
            fprintf(stderr, "Failed to setup command line arguments\n");
            goto _error;
        }
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

static int _run_interactive(KOS_FRAME frame, struct _KOS_VECTOR *buf)
{
    int                 error;
    struct _KOS_GETLINE state;
    KOS_OBJ_ID          print_args;

    printf(KOS_VERSION_STRING " interactive interpreter\n");

    {
        const enum _KOS_AREA_TYPE alloc_mode = _KOS_alloc_get_mode(frame);
        _KOS_alloc_set_mode(frame, KOS_AREA_FIXED);

        print_args = KOS_new_array(frame, 1);

        _KOS_alloc_set_mode(frame, alloc_mode);

        TRY_OBJID(print_args);
    }

    error = _KOS_getline_init(&state);
    if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Failed to initialize command line editor\n");
        goto _error;
    }

    buf->size = 0;

    for (;;) {
        error = _KOS_getline(&state, PROMPT_FIRST_LINE, buf);
        if (error) {
            assert(error == KOS_SUCCESS_RETURN || error == KOS_ERROR_OUT_OF_MEMORY);
            break;
        }

        if ( ! buf->size)
            continue;

        /* TODO parse check if more lines need to be read */

        KOS_OBJ_ID ret = KOS_repl(frame, str_stdin, buf->buffer, (unsigned)buf->size);
        buf->size = 0;

        if (IS_BAD_PTR(ret)) {
            KOS_print_exception(frame);
            KOS_clear_exception(frame);
            continue;
        }

        if (ret == KOS_VOID)
            continue;

        TRY(KOS_array_write(frame, print_args, 0, ret));

        TRY(KOS_print_to_cstr_vec(frame,
                                  print_args,
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
    _KOS_getline_destroy(&state);

    return error;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_getline.h"
#include "../inc/kos_error.h"
#include "kos_config.h"
#include "kos_system.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_READLINE
#   include <readline/readline.h>
#   include <readline/history.h>
#   include <stdlib.h>
#endif

static const char str_prompt_first_line[]      = "> ";
static const char str_prompt_subsequent_line[] = "_ ";

#ifdef _WIN32

#pragma warning( disable : 4611 ) /* interaction between '_setjmp' and C++ object destruction is non-portable */

static jmp_buf jmpbuf;

static void ctrlc_signal_handler(int sig)
{
    if (sig == SIGINT)
        longjmp(jmpbuf, sig);
}

typedef void (*signal_handler)(int);

#define set_jump() setjmp(jmpbuf)

#define install_ctrlc_signal(handler, old_action) do {                        \
    *(old_action) = signal(SIGINT, (handler)); /* TODO handle return value */ \
} while (0)

#define restore_ctrlc_signal(old_action) signal(SIGINT, *(old_action))

#else

static sigjmp_buf jmpbuf;

static void ctrlc_signal_handler(int sig)
{
    if (sig == SIGINT)
        siglongjmp(jmpbuf, sig);
}

typedef struct sigaction signal_handler;

#define set_jump() sigsetjmp(jmpbuf, 1)

#define install_ctrlc_signal(handler, old_action) do {                   \
    signal_handler sa;                                                   \
    sa.sa_handler = (handler);                                           \
    sa.sa_flags   = 0;                                                   \
    sigemptyset(&sa.sa_mask);                                            \
    sigaction(SIGINT, &sa, (old_action)); /* TODO handle return value */ \
} while (0)

#define restore_ctrlc_signal(old_action) sigaction(SIGINT, (old_action), NULL)

#endif /* !_WIN32 */

#ifdef CONFIG_READLINE

int kos_getline_init(KOS_GETLINE *state)
{
    rl_readline_name = "kos";
    rl_initialize();
    rl_catch_signals = 0;
    return KOS_SUCCESS;
}

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf)
{
    int            error;
    signal_handler old_signal;
    static char*   line       = 0;
    const char*    str_prompt = prompt == PROMPT_FIRST_LINE ? str_prompt_first_line
                                                            : str_prompt_subsequent_line;

    line = 0;

    if ( ! set_jump()) {
        install_ctrlc_signal(ctrlc_signal_handler, &old_signal);

        line = readline(str_prompt);

        restore_ctrlc_signal(&old_signal);
    }
    else {
        printf("\n");
        restore_ctrlc_signal(&old_signal);
        rl_cleanup_after_signal();
        return KOS_ERROR_INTERRUPTED;
    }

    if (line) {
        const size_t num_read = strlen(line);

        if (num_read)
            add_history(line);

        error = kos_vector_resize(buf, num_read);

        if (error)
            fprintf(stderr, "Out of memory\n");
        else
            memcpy(buf->buffer, line, num_read);

        free(line);
    }
    else
        error = KOS_SUCCESS_RETURN;

    return error;
}

#elif defined(CONFIG_EDITLINE)

static char *prompt_first_line(EditLine *e)
{
    return (char *)str_prompt_first_line;
}

static char *prompt_subsequent_line(EditLine *e)
{
    return (char *)str_prompt_subsequent_line;
}

static int stdin_eof = 0;

static int get_cfn(EditLine *e, char *c)
{
    int read_c = fgetc(stdin);

    if (read_c == 4) {
        stdin_eof = 1;
        read_c    = 10;
    }
    else if (read_c == EOF) {

        if (feof(stdin)) {
            stdin_eof = 1;
            read_c    = 10; /* Pretend EOL */
        }
        else
            return 0;
    }

    *c = (char)read_c;
    return 1;
}

int kos_getline_init(KOS_GETLINE *state)
{
    state->e = el_init("Kos", stdin, stdout, stderr);

    if ( ! state->e)
        return KOS_ERROR_OUT_OF_MEMORY;

    state->h = history_init();

    if ( ! state->h) {
        el_end(state->e);
        return KOS_ERROR_OUT_OF_MEMORY;
    }

    history(state->h, &state->ev, H_SETSIZE, 1000);

    el_set(state->e, EL_HIST, history, state->h);

    el_set(state->e, EL_EDITOR, "emacs");

    el_set(state->e, EL_GETCFN, get_cfn);

    el_source(state->e, 0);

    return KOS_SUCCESS;
}

void kos_getline_destroy(KOS_GETLINE *state)
{
    history_end(state->h);
    el_end(state->e);
}

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf)
{
    int            count = 0;
    const char    *line;
    signal_handler old_signal;

    if (stdin_eof)
        return KOS_SUCCESS_RETURN;

    el_set(state->e, EL_PROMPT, prompt == PROMPT_FIRST_LINE ? prompt_first_line
                                                            : prompt_subsequent_line);

    if ( ! set_jump()) {
        install_ctrlc_signal(ctrlc_signal_handler, &old_signal);

        line = el_gets(state->e, &count);

        restore_ctrlc_signal(&old_signal);
    }
    else {
        printf("\n");
        restore_ctrlc_signal(&old_signal);
        return KOS_ERROR_INTERRUPTED;
    }

    if (count > 0) {

        const size_t old_size = buf->size;
        const int    error    = kos_vector_resize(buf, old_size + (size_t)count);

        if (error) {
            fprintf(stderr, "Out of memory\n");
            return error;
        }

        memcpy(buf->buffer, line, count);

        history(state->h, &state->ev, H_ENTER, line);
    }
    else if (count < 0) {
        el_reset(state->e);
        return KOS_SUCCESS;
    }

    return KOS_SUCCESS;
}

#else

int kos_getline_init(KOS_GETLINE *state)
{
    state->interactive = kos_is_stdin_interactive();
    return KOS_SUCCESS;
}

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf)
{
    int error = KOS_SUCCESS;

    if (state->interactive)
        printf("%s", prompt == PROMPT_FIRST_LINE ? str_prompt_first_line
                                                 : str_prompt_subsequent_line);

    for (;;) {
        const size_t   old_size  = buf->size;
        const size_t   increment = KOS_BUF_ALLOC_SIZE;
        size_t         num_read;
        signal_handler old_signal;
        char*          ret_buf;

        if (kos_vector_resize(buf, old_size + increment)) {
            fprintf(stderr, "Out of memory\n");
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
        }

        if ( ! set_jump()) {
            install_ctrlc_signal(ctrlc_signal_handler, &old_signal);

            ret_buf = fgets(buf->buffer + old_size, (int)increment, stdin);

            restore_ctrlc_signal(&old_signal);
        }
        else {
            printf("\n");
            restore_ctrlc_signal(&old_signal);
            ret_buf = buf->buffer + old_size;
            *ret_buf = 0;
            buf->size = old_size;
            return KOS_ERROR_INTERRUPTED;
        }

        if ( ! ret_buf) {

            buf->size = old_size;

            if (feof(stdin))
                RAISE_ERROR(KOS_SUCCESS_RETURN);

            fprintf(stderr, "Failed reading from stdin\n");
            RAISE_ERROR(KOS_ERROR_CANNOT_READ_FILE);
        }

        num_read = strlen(buf->buffer + old_size);

        buf->size = old_size + num_read;

        if (num_read + 1 < increment)
            break;
    }

cleanup:
    return error;
}

#endif

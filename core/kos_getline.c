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

#include "kos_getline.h"
#include "../inc/kos_error.h"
#include "kos_config.h"
#include "kos_system.h"
#include "kos_memory.h"
#include "kos_try.h"
#include <stdio.h>
#include <string.h>

static const char str_prompt_first_line[]      = "> ";
static const char str_prompt_subsequent_line[] = "_ ";

#ifdef CONFIG_READLINE

#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>

int kos_getline_init(struct _KOS_GETLINE *state)
{
    rl_initialize();
    return KOS_SUCCESS;
}

int kos_getline(struct _KOS_GETLINE *state,
                enum _KOS_PROMPT     prompt,
                KOS_VECTOR          *buf)
{
    int         error;
    const char* str_prompt = prompt == PROMPT_FIRST_LINE ? str_prompt_first_line
                                                         : str_prompt_subsequent_line;

    char *line = readline(str_prompt);

    /* TODO hook SIGINT to return empty line */

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

static char *_prompt_first_line(EditLine *e)
{
    return (char *)str_prompt_first_line;
}

static char *_prompt_subsequent_line(EditLine *e)
{
    return (char *)str_prompt_subsequent_line;
}

static int stdin_eof = 0;

static int _get_cfn(EditLine *e, char *c)
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

int kos_getline_init(struct _KOS_GETLINE *state)
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

    el_set(state->e, EL_GETCFN, _get_cfn);

    el_source(state->e, 0);

    return KOS_SUCCESS;
}

void kos_getline_destroy(struct _KOS_GETLINE *state)
{
    history_end(state->h);
    el_end(state->e);
}

int kos_getline(struct _KOS_GETLINE *state,
                enum _KOS_PROMPT     prompt,
                KOS_VECTOR          *buf)
{
    int         count = 0;
    const char *line;

    if (stdin_eof)
        return KOS_SUCCESS_RETURN;

    el_set(state->e, EL_PROMPT, prompt == PROMPT_FIRST_LINE ? _prompt_first_line
                                                            : _prompt_subsequent_line);

    /* TODO hook SIGINT to return empty line */

    line = el_gets(state->e, &count);

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

int kos_getline_init(struct _KOS_GETLINE *state)
{
    state->interactive = kos_is_stdin_interactive();
    return KOS_SUCCESS;
}

int kos_getline(struct _KOS_GETLINE *state,
                enum _KOS_PROMPT     prompt,
                KOS_VECTOR          *buf)
{
    int error = KOS_SUCCESS;

    if (state->interactive)
        printf("%s", prompt == PROMPT_FIRST_LINE ? str_prompt_first_line
                                                 : str_prompt_subsequent_line);

    /* TODO hook SIGINT to return empty line */

    for (;;) {
        const size_t old_size  = buf->size;
        const size_t increment = _KOS_BUF_ALLOC_SIZE;
        size_t       num_read;

        if (kos_vector_resize(buf, old_size + increment)) {
            fprintf(stderr, "Out of memory\n");
            RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
        }

        if ( ! fgets(buf->buffer + old_size, (int)increment, stdin)) {

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

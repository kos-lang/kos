/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_getline.h"
#include "../inc/kos_error.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_system.h"
#include "kos_memory.h"
#include "kos_try.h"
#include "kos_utf8.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_EXPERIMENTAL_GETLINE
#   include <errno.h>
#   include <termios.h>
#   include <sys/ioctl.h>
#elif defined(CONFIG_READLINE)
#   include <readline/readline.h>
#   include <readline/history.h>
#   include <stdlib.h>
#endif

#ifndef CONFIG_EXPERIMENTAL_GETLINE
static const char str_prompt_first_line[]      = "> ";
static const char str_prompt_subsequent_line[] = "_ ";
#endif

#define install_ctrlc_signal(handler, old_action) \
    install_signal(SIGINT, (handler), (old_action))

#define restore_ctrlc_signal(old_action) restore_signal(SIGINT, (old_action))

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

static int install_signal(int sig, signal_handler handler, signal_handler *old_action)
{
    *old_action = signal(sig, handler);

    return ((*old_action == SIGERR) || kos_seq_fail()) ? KOS_ERROR_ERRNO : KOS_SUCCESS;
}

static void restore_signal(int sig, signal_handler *old_action)
{
    (void)signal(sig, *old_action);
}

#else

#ifndef CONFIG_EXPERIMENTAL_GETLINE

static sigjmp_buf jmpbuf;

static void ctrlc_signal_handler(int sig)
{
    if (sig == SIGINT)
        siglongjmp(jmpbuf, sig);
}

#define set_jump() sigsetjmp(jmpbuf, 1)

#endif

typedef struct sigaction signal_handler;

static int install_signal(int sig, void (*handler)(int), signal_handler *old_action)
{
    signal_handler sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);

    return ((sigaction(sig, &sa, old_action) != 0) || kos_seq_fail()) ? KOS_ERROR_ERRNO : KOS_SUCCESS;
}

static void restore_signal(int sig, signal_handler *old_action)
{
    (void)sigaction(sig, old_action, NULL);
}

#endif /* !_WIN32 */

#ifdef CONFIG_EXPERIMENTAL_GETLINE

static KOS_ATOMIC(uint32_t) window_dimensions_changed;

static void sig_winch(int sig)
{
    assert(sig == SIGWINCH);

    KOS_atomic_write_relaxed_u32(window_dimensions_changed, 1);
}

static int check_error(FILE *file)
{
    return ferror(file) ? KOS_ERROR_ERRNO : KOS_SUCCESS_RETURN;
}

static int send_char(char c)
{
    return (putchar(c) != EOF) ? KOS_SUCCESS : check_error(stdout);
}

static int send_escape(unsigned param, char code)
{
    return fprintf(stdout, "\x1B[%u%c", param, code) > 3 ? KOS_SUCCESS : check_error(stdout);
}

static int move_cursor_right(unsigned offset)
{
    return send_escape(offset, 'C');
}

static int move_cursor_left(unsigned offset)
{
    return send_escape(offset, 'D');
}

static int get_cursor_pos_via_esc(unsigned *pos)
{
    int       c;
    unsigned  i     = 0;
    unsigned  rows  = 0;
    unsigned  cols  = 0;
    int       error = send_escape(6, 'n');
    char      buf[16];

    if (error)
        return error;

    do {
        c = getchar();

        if (c == EOF)
            return check_error(stdin);

    } while (c != 0x1B);

    do {
        c = getchar();

        if (c == EOF)
            return check_error(stdin);

        buf[i++] = (char)c;

    } while (((c < 'A') || (c > 'Z')) && (i < sizeof(buf) - 1));

    buf[i] = 0;

    if (sscanf(buf, "[%u;%uR", &rows, &cols) != 2)
        return KOS_ERROR_ERRNO;

    *pos = cols;

    return KOS_SUCCESS;
}

static unsigned get_num_columns()
{
    unsigned       orig_pos;
    unsigned       rightmost_pos;
    struct winsize ws;
    int            error;

    error = ioctl(fileno(stdout), TIOCGWINSZ, &ws);

    if ((error != -1) && (ws.ws_col != 0) && ! kos_seq_fail())
        return ws.ws_col;

    if ( ! get_cursor_pos_via_esc(&orig_pos) &&
         ! move_cursor_right(999) &&
         ! get_cursor_pos_via_esc(&rightmost_pos)) {

        if (rightmost_pos > orig_pos)
            (void)move_cursor_left(rightmost_pos - orig_pos);

        return rightmost_pos;
    }

    return 80;
}

typedef struct termios TERM_INFO;

static int init_terminal(TERM_INFO *old_info)
{
    struct termios new_attrs;

    if (tcgetattr(fileno(stdin), &new_attrs))
        return KOS_ERROR_ERRNO;

    *old_info = new_attrs;

    new_attrs.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    new_attrs.c_oflag &= ~OPOST;
    new_attrs.c_cflag |= CS8;
    new_attrs.c_lflag &= ~(ECHOKE | ECHOE | ECHO | ECHONL | ECHOPRT | ECHOCTL | ICANON | IEXTEN | ISIG);
    new_attrs.c_cc[VMIN]  = 1;
    new_attrs.c_cc[VTIME] = 0;

    return (tcsetattr(fileno(stdin), TCSAFLUSH, &new_attrs) == 0) ? KOS_SUCCESS : KOS_ERROR_ERRNO;
}

static void restore_terminal(TERM_INFO *old_info)
{
    (void)tcsetattr(fileno(stdin), TCSANOW, old_info);
}

int kos_getline_init(KOS_GETLINE *state)
{
    /* TODO history */
    return KOS_SUCCESS;
}

void kos_getline_destroy(KOS_GETLINE *state)
{
    /* TODO history */
}

struct TERM_POS {
    /* Logical, in visible characters */
    unsigned logical;
    /* Physical, in bytes - a logical UTF-8 character can use multiple bytes */
    unsigned physical;
};

struct TERM_EDIT {
    /* Line of text being edited, actual bytes */
    KOS_VECTOR     *line;
    /* Prompt string (ASCII) */
    const char     *prompt;
    /* Number of logical characters in line (for UTF-8 this is less than bytes in line) */
    unsigned        line_size;
    /* Number of visible (logical) columns, i.e. terminal width */
    unsigned        num_columns;
    /* Number of logical characters in prompt (same as number of bytes) */
    unsigned        prompt_size;
    /* Maximum index of a visible column */
    unsigned        last_visible_column;
    /* First character drawn (0-based) */
    struct TERM_POS scroll_pos;
    /* First char shown (0-based, from the beginning of actual line) */
    struct TERM_POS cursor_pos;
    /* Indicates whether the terminal is interactive */
    int             interactive;
};

enum KEY_CODE_E {
    KEY_CTRL_A    = 1,
    KEY_CTRL_B    = 2,
    KEY_CTRL_C    = 3,
    KEY_CTRL_D    = 4,
    KEY_CTRL_E    = 5,
    KEY_CTRL_F    = 6,
    KEY_BELL      = 7,
    KEY_CTRL_H    = 8,
    KEY_TAB       = 9,
    KEY_LF        = 10,
    KEY_CTRL_K    = 11,
    KEY_CTRL_L    = 12,
    KEY_ENTER     = 13,
    KEY_CTRL_N    = 14,
    KEY_CTRL_O    = 15,
    KEY_CTRL_P    = 16,
    KEY_CTRL_Q    = 17,
    KEY_CTRL_R    = 18,
    KEY_CTRL_S    = 19,
    KEY_CTRL_T    = 20,
    KEY_CTRL_U    = 21,
    KEY_CTRL_V    = 22,
    KEY_CTRL_W    = 23,
    KEY_CTRL_X    = 24,
    KEY_CTRL_Y    = 25,
    KEY_CTRL_Z    = 26,
    KEY_ESC       = 27,
    KEY_BACKSPACE = 127
};

static int is_utf8_tail(char c)
{
    return ! (((unsigned char)c ^ 0x80U) & 0x3FU);
}

static int clear_and_redraw(struct TERM_EDIT *edit)
{
    const char *const begin        = edit->line->buffer + edit->scroll_pos.physical;
    const char *const end          = edit->line->buffer + edit->line->size;
    const char       *cur          = begin;
    unsigned          cursor_delta = 0;
    unsigned          num_left     = edit->num_columns - edit->prompt_size + 1;
    unsigned          num_to_write;

    assert(edit->cursor_pos.logical  <= edit->line_size);
    assert(edit->cursor_pos.physical <= edit->line->size);
    assert(edit->cursor_pos.logical  >= edit->scroll_pos.logical);
    assert(edit->cursor_pos.physical >= edit->scroll_pos.physical);

    edit->last_visible_column = edit->scroll_pos.logical + num_left - 1;

    if (fwrite(edit->prompt, 1, edit->prompt_size, stdout) != edit->prompt_size)
        return check_error(stdout);

    assert(cur <= end);

    for (; num_left && (cur < end); --num_left) {
        ++cursor_delta;
        ++cur;
        while ((cur < end) && is_utf8_tail(*cur))
            ++cur;
    }

    num_to_write = cur - begin;

    if (num_to_write) {
        const unsigned cursor_rel = edit->cursor_pos.logical - edit->scroll_pos.logical;

        assert(cursor_rel <= cursor_delta);

        cursor_delta -= cursor_rel;

        if (fwrite(begin, 1, num_to_write, stdout) != num_to_write)
            return check_error(stdout);

        if ( ! edit->interactive)
            return KOS_SUCCESS;

        if (num_left) {
            const int error = send_escape(0, 'K');
            if (error)
                return error;
        }

        if (cursor_delta)
            return move_cursor_left(cursor_delta);

        return KOS_SUCCESS;
    }

    return edit->interactive ? send_escape(0, 'K') : KOS_SUCCESS;
}

static int is_utf8_finished(const char *buf, int pos)
{
    unsigned num = 0;
    unsigned code_len;
    char     c;

    do {
        c = buf[pos];
        --pos;
        ++num;
    } while (is_utf8_tail(c) && (pos >= 0));

    code_len = kos_utf8_len[(unsigned char)c >> 3];

    return num >= code_len;
}

static void decrement_pos(struct TERM_EDIT *edit, struct TERM_POS *pos)
{
    unsigned i = pos->physical;

    do --i;
    while (i && is_utf8_tail(edit->line->buffer[i]));

    pos->physical = i;
    --pos->logical;
}

static void increment_pos(struct TERM_EDIT *edit, struct TERM_POS *pos)
{
    unsigned i = pos->physical;

    do ++i;
    while ((i < edit->line->size) && is_utf8_tail(edit->line->buffer[i]));

    pos->physical = i;
    ++pos->logical;
}

static void decrement_cursor_pos(struct TERM_EDIT *edit)
{
    decrement_pos(edit, &edit->cursor_pos);
}

static void increment_cursor_pos(struct TERM_EDIT *edit)
{
    increment_pos(edit, &edit->cursor_pos);
}

static void decrement_scroll_pos(struct TERM_EDIT *edit)
{
    decrement_pos(edit, &edit->scroll_pos);

    edit->last_visible_column = edit->scroll_pos.logical + edit->num_columns - edit->prompt_size;
}

static void increment_scroll_pos(struct TERM_EDIT *edit)
{
    increment_pos(edit, &edit->scroll_pos);

    edit->last_visible_column = edit->scroll_pos.logical + edit->num_columns - edit->prompt_size;
}

static int esc_left(struct TERM_EDIT *edit)
{
    if ( ! edit->cursor_pos.physical) {
        assert( ! edit->cursor_pos.logical);
        return send_char(KEY_BELL);
    }

    decrement_cursor_pos(edit);

    if (edit->cursor_pos.logical < edit->scroll_pos.logical) {
        decrement_scroll_pos(edit);
        return clear_and_redraw(edit);
    }

    return move_cursor_left(1);
}

static int esc_right(struct TERM_EDIT *edit)
{
    if (edit->cursor_pos.logical >= edit->line_size) {
        assert(edit->cursor_pos.logical == edit->line_size);
        return send_char(KEY_BELL);
    }

    increment_cursor_pos(edit);

    if (edit->cursor_pos.logical > edit->last_visible_column) {
        increment_scroll_pos(edit);
        return clear_and_redraw(edit);
    }

    return move_cursor_right(1);
}

static int esc_home(struct TERM_EDIT *edit)
{
    if ( ! edit->cursor_pos.logical) {
        assert( ! edit->cursor_pos.logical);
        return KOS_SUCCESS;
    }

    edit->cursor_pos.logical  = 0;
    edit->cursor_pos.physical = 0;
    edit->scroll_pos.logical  = 0;
    edit->scroll_pos.physical = 0;

    return clear_and_redraw(edit);
}

static int esc_end(struct TERM_EDIT *edit)
{
    if (edit->cursor_pos.logical == edit->line_size) {
        assert(edit->cursor_pos.physical == edit->line->size);
        return KOS_SUCCESS;
    }

    edit->cursor_pos.logical  = edit->line_size;
    edit->cursor_pos.physical = edit->line->size;

    if (edit->cursor_pos.logical > edit->last_visible_column) {
        const unsigned scroll_target = edit->line_size - edit->num_columns + edit->prompt_size;

        edit->last_visible_column = edit->line_size;
        edit->scroll_pos          = edit->cursor_pos;

        while (edit->scroll_pos.logical > scroll_target)
            decrement_pos(edit, &edit->scroll_pos);
    }

    return clear_and_redraw(edit);
}

static int esc_up(struct TERM_EDIT *edit)
{
    /* TODO */
    return send_char(KEY_BELL);
}

static int esc_down(struct TERM_EDIT *edit)
{
    /* TODO */
    return send_char(KEY_BELL);
}

static int backspace(struct TERM_EDIT *edit)
{
    struct TERM_POS old_pos;
    unsigned        num_del_bytes;

    if ( ! edit->cursor_pos.physical) {
        assert( ! edit->cursor_pos.logical);
        return send_char(KEY_BELL);
    }

    old_pos = edit->cursor_pos;

    decrement_cursor_pos(edit);

    num_del_bytes = old_pos.physical - edit->cursor_pos.physical;
    memmove(&edit->line->buffer[edit->cursor_pos.physical],
            &edit->line->buffer[edit->cursor_pos.physical + num_del_bytes],
            edit->line->size - old_pos.physical);
    edit->line->size -= num_del_bytes;
    --edit->line_size;

    if (edit->cursor_pos.logical < edit->scroll_pos.logical)
        decrement_cursor_pos(edit);

    return clear_and_redraw(edit);
}

static int esc_delete(struct TERM_EDIT *edit)
{
    if (edit->cursor_pos.physical == edit->line->size)
        return send_char(KEY_BELL);

    increment_cursor_pos(edit);

    return backspace(edit);
}

static int insert_char(struct TERM_EDIT *edit, char c)
{
    const size_t   init_size  = edit->line->size;
    const unsigned insert_pos = edit->cursor_pos.physical;
    const unsigned tail_size  = init_size - insert_pos;
    int            error      = kos_vector_resize(edit->line, init_size + 1);

    if (error)
        return error;

    assert(insert_pos <= init_size);

    if (tail_size)
        memmove(&edit->line->buffer[insert_pos + 1],
                &edit->line->buffer[insert_pos],
                tail_size);

    edit->line->buffer[insert_pos] = c;

    edit->cursor_pos.physical = insert_pos + 1;
    if ( ! is_utf8_finished(edit->line->buffer, (int)insert_pos))
        return KOS_SUCCESS;

    ++edit->cursor_pos.logical;
    ++edit->line_size;

    if (edit->cursor_pos.logical > edit->last_visible_column) {
        increment_scroll_pos(edit);
        return clear_and_redraw(edit);
    }

    if (tail_size || is_utf8_tail(c))
        return clear_and_redraw(edit);

    return send_char(c);
}

static int tab_complete(struct TERM_EDIT *edit)
{
    /* TODO */
    return send_char(KEY_BELL);
}

static int dispatch_esc(struct TERM_EDIT *edit)
{
    int c = getchar();

    switch (c) {

        case EOF:
            return check_error(stdin);

        case 'O': {

            c = getchar();

            switch (c) {
                case EOF: return check_error(stdin);
                case 'F': return esc_end(edit);
                case 'H': return esc_home(edit);
                default:  break;
            }

            break;
        }

        case '[': {

            c = getchar();

            switch (c) {

                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9': {
                    int code            = c - '0';
                    int after_semicolon = 0;

                    do {
                        c = getchar();

                        switch (c) {

                            case EOF:
                                return check_error(stdin);

                            case '~':
                                break;

                            case ';':
                                after_semicolon = 1;
                                break;

                            case '0': case '1': case '2': case '3': case '4':
                            case '5': case '6': case '7': case '8': case '9': {
                                if ( ! after_semicolon)
                                    code = (code * 10) + (c - '0');
                                break;
                            }

                            default:
                                return send_char(KEY_BELL);
                        }
                    } while (c != '~');

                    switch (code) {
                        case 1:  return esc_home(edit);
                        case 3:  return esc_delete(edit);
                        case 4:  return esc_end(edit);
                        case 7:  return esc_home(edit);
                        case 8:  return esc_end(edit);
                        default: break;
                    }
                    break;
                }

                case EOF: return check_error(stdin);
                case 'A': return esc_up(edit);
                case 'B': return esc_down(edit);
                case 'C': return esc_right(edit);
                case 'D': return esc_left(edit);
                case 'F': return esc_end(edit);
                case 'H': return esc_home(edit);
                default:  break;
            }
            break;
        }

        /* TODO Alt-key */

        default:
            insert_char(edit, (char)c); /* TODO debug only - remove this */
            break;
    }

    return send_char(KEY_BELL);
}

static int dispatch_key(struct TERM_EDIT *edit, int *key)
{
    switch (*key) {

        case KEY_CTRL_C:
            edit->line->size = 0;
            /* fall-through */
        case KEY_ENTER:
            /* fall-through */
        case KEY_LF:
            *key = KEY_ENTER;
            return (fwrite("\r\n", 1, 2, stdout) == 2) ? KOS_SUCCESS : check_error(stdout);

        case EOF:           return check_error(stdin);
        case KEY_ESC:       return dispatch_esc(edit);
        case KEY_BACKSPACE: return backspace(edit);
        case KEY_CTRL_A:    return esc_home(edit);
        case KEY_CTRL_B:    return esc_left(edit);
        case KEY_CTRL_D:    return edit->line->size ? esc_delete(edit) : check_error(stdin);
        case KEY_CTRL_E:    return esc_end(edit);
        case KEY_CTRL_F:    return esc_right(edit);
        case KEY_BELL:      return send_char(KEY_BELL);
        case KEY_CTRL_H:    return backspace(edit);
        case KEY_TAB:       return tab_complete(edit);
        case KEY_CTRL_N:    return esc_down(edit);
        case KEY_CTRL_P:    return esc_up(edit);
        default:            return insert_char(edit, (char)*key);
    }
}

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf)
{
    int              error       = KOS_SUCCESS;
    int              key;
    signal_handler   old_sig_winch;
    TERM_INFO        old_term_info;
    struct TERM_EDIT edit;

    memset(&edit, 0, sizeof(edit));
    edit.line = buf;

    if (prompt == PROMPT_FIRST_LINE) {
        static const char str_prompt[] = "\r> ";
        edit.prompt      = str_prompt;
        edit.prompt_size = sizeof(str_prompt) - 1;
    }
    else {
        static const char str_prompt[] = "\r_ ";
        edit.prompt      = str_prompt;
        edit.prompt_size = sizeof(str_prompt) - 1;
    }

    if (kos_is_stdin_interactive()) {

        edit.interactive = 1;

        error = init_terminal(&old_term_info);
        if (error)
            return error;

        error = install_signal(SIGWINCH, sig_winch, &old_sig_winch);
        if (error) {
            restore_terminal(&old_term_info);
            return error;
        }
    }

    KOS_atomic_write_relaxed_u32(window_dimensions_changed, 1);

    do {

        if (KOS_atomic_swap_u32(window_dimensions_changed, 0)) {
            edit.num_columns = edit.interactive ? get_num_columns() : ~0U;

            error = clear_and_redraw(&edit);
        }

        if ( ! error) {

            key = getchar();

            error = dispatch_key(&edit, &key);
        }

        /* EINTR is typically caused by SIGWINCH */
        if ((error == KOS_ERROR_ERRNO) && (errno == EINTR)) {
            clearerr(stdin);
            clearerr(stdout);
            error = KOS_SUCCESS;
        }

    } while ( ! error && (key != KEY_ENTER));

    if (edit.interactive) {
        restore_signal(SIGWINCH, &old_sig_winch);
        restore_terminal(&old_term_info);
    }

    return error;
}

#elif defined(CONFIG_READLINE)

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
    if (state->interactive)
        printf("%s", prompt == PROMPT_FIRST_LINE ? str_prompt_first_line
                                                 : str_prompt_subsequent_line);

    for (;;) {
        const size_t   old_size  = buf->size;
        const size_t   increment = 4096U;
        size_t         num_read;
        signal_handler old_signal;
        char*          ret_buf;

        if (kos_vector_resize(buf, old_size + increment)) {
            fprintf(stderr, "Out of memory\n");
            return KOS_ERROR_OUT_OF_MEMORY;
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
                return KOS_SUCCESS_RETURN;

            fprintf(stderr, "Failed reading from stdin\n");
            return KOS_ERROR_CANNOT_READ_FILE;
        }

        num_read = strlen(buf->buffer + old_size);

        buf->size = old_size + num_read;

        if (num_read + 1 < increment)
            break;
    }

    return KOS_SUCCESS;
}

#endif

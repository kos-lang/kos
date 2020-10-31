/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_getline.h"
#include "../inc/kos_error.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_system.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_utf8.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#   include <errno.h>
#   include <sys/ioctl.h>
#   include <termios.h>
#   include <unistd.h>
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

    return ((*old_action == SIG_ERR) || kos_seq_fail()) ? KOS_ERROR_ERRNO : KOS_SUCCESS;
}

static void restore_signal(int sig, signal_handler *old_action)
{
    (void)signal(sig, *old_action);
}

#else

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

#endif

#ifdef _WIN32

static const char str_prompt_first_line[]      = "> ";
static const char str_prompt_subsequent_line[] = "_ ";

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

#else

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

    c = getchar();

    if (c == EOF)
        return check_error(stdin);

    if (c != 0x1B) {
        ungetc(c, stdin);
        return KOS_ERROR_ERRNO;
    }

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
    static int     esc_cursor_failed = 0;
    struct winsize ws;
    int            error;
    unsigned       cols = 80; /* If all attempts fail, default to 80 columns */

    /* First, try to get terminal width via ioctl */
    error = ioctl(fileno(stdout), TIOCGWINSZ, &ws);

    if ((error != -1) && (ws.ws_col != 0) && ! kos_seq_fail())
        cols = ws.ws_col;
    else {

        /* Second, fall back to reading via escape code, but attempt this only once */
        if ( ! esc_cursor_failed) {
            unsigned orig_pos;
            unsigned rightmost_pos;

            if ( ! get_cursor_pos_via_esc(&orig_pos) &&
                 ! move_cursor_right(999) &&
                 ! get_cursor_pos_via_esc(&rightmost_pos)) {

                if (rightmost_pos > orig_pos) {
                    if (orig_pos > 1)
                        (void)move_cursor_left(rightmost_pos - orig_pos);
                    else
                        putchar('\r');
                }

                cols = rightmost_pos;
            }
            else
                esc_cursor_failed = 1;
        }

        /* Third, see if we can read it from environment */
        if (esc_cursor_failed) {
            const char *const env_cols = getenv("COLUMNS");

            if (env_cols && env_cols[0]) {

                const char *const end   = env_cols + strlen(env_cols);
                int64_t           value = 0;

                if ((kos_parse_int(env_cols, end, &value) == KOS_SUCCESS) &&
                    (value > 0) && (value < 0x7FFFFFFF))
                    cols = (unsigned)value;
            }
        }
    }

    return cols;
}

typedef struct termios TERM_INFO;

static TERM_INFO old_term_info;

static signal_handler old_sig_winch;

static int init_terminal(TERM_INFO *old_info)
{
    int error;

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

    if (tcsetattr(fileno(stdin), TCSAFLUSH, &new_attrs) != 0)
        return KOS_ERROR_ERRNO;

    error = install_signal(SIGWINCH, sig_winch, &old_sig_winch);
    if (error)
        (void)tcsetattr(fileno(stdin), TCSANOW, old_info);

    return error;
}

static void restore_terminal(TERM_INFO *old_info)
{
    restore_signal(SIGWINCH, &old_sig_winch);

    (void)tcsetattr(fileno(stdin), TCSANOW, old_info);
}

typedef struct KOS_GETLINE_HISTORY_NODE_S HIST_NODE;

struct TERM_POS {
    /* Logical, in visible characters */
    unsigned logical;
    /* Physical, in bytes - a logical UTF-8 character can use multiple bytes */
    unsigned physical;
};

struct TERM_EDIT {
    /* Line of text being edited, actual bytes */
    KOS_VECTOR          *line;
    /* Prompt string (ASCII) */
    const char          *prompt;
    /* Number of logical characters in line (for UTF-8 this is less than bytes in line) */
    unsigned             line_size;
    /* Number of visible (logical) columns, i.e. terminal width */
    unsigned             num_columns;
    /* Number of logical characters in prompt (same as number of bytes) */
    unsigned             prompt_size;
    /* Maximum index of a visible column */
    unsigned             last_visible_column;
    /* First character drawn (0-based) */
    struct TERM_POS      scroll_pos;
    /* First char shown (0-based, from the beginning of actual line) */
    struct TERM_POS      cursor_pos;
    /* Indicates whether the terminal is interactive */
    int                  interactive;
    /* Allocator for temporary history nodes */
    struct KOS_MEMPOOL_S temp_allocator;
    /* Currently selected history node */
    HIST_NODE           *cur_hist_node;
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
    return ! (((unsigned char)c ^ 0x80U) & 0xC0U);
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

    num_to_write = (unsigned)(cur - begin);

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

static int move_cursor_to(struct TERM_EDIT *edit, struct TERM_POS pos)
{
    if (pos.logical == edit->cursor_pos.logical)
        return KOS_SUCCESS;

    if ((pos.logical >= edit->scroll_pos.logical) && (pos.logical <= edit->last_visible_column)) {

        int error;

        if (pos.logical > edit->cursor_pos.logical)
            error = move_cursor_right(pos.logical - edit->cursor_pos.logical);
        else
            error = move_cursor_left(edit->cursor_pos.logical - pos.logical);

        edit->cursor_pos = pos;

        return error;
    }
    else {

        edit->cursor_pos = pos;

        if (pos.logical > edit->scroll_pos.logical) {

            const unsigned scroll_target = pos.logical - edit->num_columns + edit->prompt_size;

            while (pos.logical > scroll_target)
                decrement_pos(edit, &pos);
        }

        edit->scroll_pos = pos;

        return clear_and_redraw(edit);
    }
}

static int action_left(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = edit->cursor_pos;

    if ( ! pos.physical) {
        assert( ! pos.logical);
        return send_char(KEY_BELL);
    }

    decrement_pos(edit, &pos);

    return move_cursor_to(edit, pos);
}

static int action_right(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = edit->cursor_pos;

    if (pos.logical >= edit->line_size) {
        assert(pos.logical == edit->line_size);
        return send_char(KEY_BELL);
    }

    increment_pos(edit, &pos);

    return move_cursor_to(edit, pos);
}

enum TRANSITION_E {
    NO_TRANSITION,
    WORD_BEGIN,
    WORD_END
};

static enum TRANSITION_E get_char_type(struct TERM_EDIT *edit, unsigned physical)
{
    char c;

    assert(physical < edit->line->size);

    c = edit->line->buffer[physical];

    return (((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')) ||
            ((c >= '0') && (c <= '9')) ||
            (c == '_'))
           ? WORD_BEGIN : WORD_END;
}

static int is_transition(struct TERM_EDIT *edit, unsigned pos1, unsigned pos2)
{
    enum TRANSITION_E first;
    enum TRANSITION_E second;

    assert(pos1 < pos2);

    first  = get_char_type(edit, pos1);
    second = (pos2 < edit->line->size) ? get_char_type(edit, pos2) : WORD_END;

    return (first == second) ? NO_TRANSITION : second;
}

static struct TERM_POS find_word_begin(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = edit->cursor_pos;

    if (pos.logical) {
        struct TERM_POS prev = pos;

        decrement_pos(edit, &prev);

        do {
            pos = prev;
            if ( ! pos.logical)
                break;

            decrement_pos(edit, &prev);

        } while (is_transition(edit, prev.physical, pos.physical) != WORD_BEGIN);
    }

    return pos;
}

static struct TERM_POS find_word_end(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = edit->cursor_pos;

    if (pos.logical < edit->line_size) {

        struct TERM_POS prev;

        do {
            prev = pos;

            increment_pos(edit, &pos);

        } while ((pos.logical < edit->line_size) &&
                 (is_transition(edit, prev.physical, pos.physical) != WORD_END));
    }

    return pos;
}

static int action_word_begin(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = find_word_begin(edit);

    return move_cursor_to(edit, pos);
}

static int action_word_end(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = find_word_end(edit);

    return move_cursor_to(edit, pos);
}

static int action_home(struct TERM_EDIT *edit)
{
    struct TERM_POS pos = { 0, 0 };

    return move_cursor_to(edit, pos);
}

static int action_end(struct TERM_EDIT *edit)
{
    struct TERM_POS pos;

    pos.logical  = edit->line_size;
    pos.physical = (unsigned)edit->line->size;

    return move_cursor_to(edit, pos);
}

struct KOS_GETLINE_HISTORY_NODE_S {
    /* Uni-directional list of nodes which are persistent across kos_getline() invocations */
    HIST_NODE *persistent_prev;

    /* Bi-directional list of nodes used during editing of a single command */
    HIST_NODE *next;
    HIST_NODE *prev;

    uint16_t capacity;   /* Capacity of buffer, in bytes  */
    uint16_t size;       /* Command length, in bytes      */
    uint16_t line_size;  /* Command length, in characters */
    uint8_t  persistent; /* 1 if the node is persistent   */
    char     buffer[1];  /* The command (bytes)           */
};

int kos_getline_init(KOS_GETLINE *state)
{
    kos_mempool_init(&state->allocator);

    state->head = 0;

    return KOS_SUCCESS;
}

void kos_getline_destroy(KOS_GETLINE *state)
{
    kos_mempool_destroy(&state->allocator);

    state->head = 0;
}

static HIST_NODE *alloc_history_node(struct KOS_MEMPOOL_S *allocator,
                                     const char           *str,
                                     size_t                size,
                                     size_t                num_chars)
{
    const size_t capacity   = KOS_align_up(size, (size_t)16);
    const size_t alloc_size = sizeof(HIST_NODE) - 1 + capacity;

    HIST_NODE *node = kos_mempool_alloc(allocator, alloc_size);

    if (node) {

        if (size)
            memcpy(node->buffer, str, size);

        node->capacity        = (uint16_t)capacity;
        node->size            = (uint16_t)size;
        node->line_size       = (uint16_t)num_chars;
        node->persistent      = 0;
        node->persistent_prev = 0;
        node->next            = 0;
        node->prev            = 0;
    }

    return node;
}

static int add_to_persistent_history(KOS_GETLINE *state,
                                     const char  *str,
                                     size_t       size,
                                     size_t       num_chars)
{
    HIST_NODE *node = alloc_history_node(&state->allocator, str, size, num_chars);

    if (node) {
        node->persistent      = 1;
        node->persistent_prev = state->head;
        state->head           = node;

        return KOS_SUCCESS;
    }
    else
        return KOS_ERROR_OUT_OF_MEMORY;
}

static int init_history(struct TERM_EDIT *edit, HIST_NODE *node)
{
    HIST_NODE *next_node = alloc_history_node(&edit->temp_allocator, 0, 0, 0);

    if ( ! next_node)
        return KOS_ERROR_OUT_OF_MEMORY;

    edit->cur_hist_node = next_node;

    while (node) {
        next_node->prev = node;
        node->next      = next_node;
        next_node       = node;
        node            = node->persistent_prev;
    }

    next_node->prev = 0;

    return KOS_SUCCESS;
}

static int save_to_temp_history(struct TERM_EDIT *edit)
{
    HIST_NODE *node = edit->cur_hist_node;

    assert(node);

    if ((edit->line->size == node->size) &&
        ( ! node->size || ! memcmp(edit->line->buffer, &node->buffer, node->size)))
        return KOS_SUCCESS;

    if ((edit->line->size > node->capacity) || node->persistent) {
        HIST_NODE *new_node = alloc_history_node(&edit->temp_allocator,
                                                 edit->line->buffer,
                                                 edit->line->size,
                                                 edit->line_size);

        if ( ! new_node)
            return KOS_ERROR_OUT_OF_MEMORY;

        new_node->next      = node->next;
        new_node->prev      = node->prev;
        edit->cur_hist_node = new_node;
        node                = new_node;
        if (new_node->next)
            new_node->next->prev = new_node;
        if (new_node->prev)
            new_node->prev->next = new_node;
    }

    node->size      = edit->line->size;
    node->line_size = edit->line_size;

    memcpy(&node->buffer[0], edit->line->buffer, edit->line->size);

    return KOS_SUCCESS;
}

static int restore_from_temp_history(struct TERM_EDIT *edit)
{
    HIST_NODE *node = edit->cur_hist_node;
    int        error;

    assert(node);

    error = kos_vector_resize(edit->line, node->size);
    if (error)
        return error;

    memcpy(edit->line->buffer, &node->buffer[0], node->size);

    edit->line_size = node->line_size;

    edit->cursor_pos.logical  = 0;
    edit->cursor_pos.physical = 0;
    edit->scroll_pos.logical  = 0;
    edit->scroll_pos.physical = 0;

    return clear_and_redraw(edit);
}

static int action_up(struct TERM_EDIT *edit)
{
    int error;

    assert(edit->cur_hist_node);

    if ( ! edit->cur_hist_node->prev)
        return send_char(KEY_BELL);

    error = save_to_temp_history(edit);
    if (error)
        return error;

    edit->cur_hist_node = edit->cur_hist_node->prev;

    return restore_from_temp_history(edit);
}

static int action_down(struct TERM_EDIT *edit)
{
    int error;

    assert(edit->cur_hist_node);

    if ( ! edit->cur_hist_node->next)
        return send_char(KEY_BELL);

    error = save_to_temp_history(edit);
    if (error)
        return error;

    edit->cur_hist_node = edit->cur_hist_node->next;

    return restore_from_temp_history(edit);
}

static int action_reverse_search(struct TERM_EDIT *edit)
{
    /* TODO */
    return send_char(KEY_BELL);
}

static int delete_range(struct TERM_EDIT *edit, struct TERM_POS begin, struct TERM_POS end)
{
    const unsigned phys_delta = end.physical - begin.physical;
    const unsigned log_delta  = end.logical  - begin.logical;

    assert(begin.logical <= end.logical);

    if ( ! log_delta)
        return move_cursor_to(edit, begin);

    if (end.logical < edit->line_size)
        memmove(&edit->line->buffer[begin.physical],
                &edit->line->buffer[end.physical],
                edit->line->size - end.physical);

    edit->line->size -= phys_delta;
    edit->line_size  -= log_delta;

    if ((begin.logical >= edit->scroll_pos.logical) && (begin.logical <= edit->last_visible_column)) {

        edit->cursor_pos = begin;

        return clear_and_redraw(edit);
    }
    else
        return move_cursor_to(edit, begin);
}

static int action_backspace(struct TERM_EDIT *edit)
{
    struct TERM_POS begin;
    struct TERM_POS end;

    if ( ! edit->cursor_pos.physical) {
        assert( ! edit->cursor_pos.logical);
        return send_char(KEY_BELL);
    }

    begin = edit->cursor_pos;
    end   = begin;
    decrement_pos(edit, &begin);

    return delete_range(edit, begin, end);
}

static int action_delete(struct TERM_EDIT *edit)
{
    struct TERM_POS begin;
    struct TERM_POS end;

    if (edit->cursor_pos.physical == edit->line->size)
        return send_char(KEY_BELL);

    begin = edit->cursor_pos;
    end   = begin;
    increment_pos(edit, &end);

    return delete_range(edit, begin, end);
}

static int action_delete_to_word_begin(struct TERM_EDIT *edit)
{
    struct TERM_POS begin;
    struct TERM_POS end = edit->cursor_pos;

    if ( ! end.logical)
        return KOS_SUCCESS;

    begin = find_word_begin(edit);

    return delete_range(edit, begin, end);
}

static int action_delete_to_word_end(struct TERM_EDIT *edit)
{
    struct TERM_POS begin = edit->cursor_pos;
    struct TERM_POS end;

    if (begin.logical == edit->line_size)
        return KOS_SUCCESS;

    end = find_word_end(edit);

    return delete_range(edit, begin, end);
}

static int action_capitalize_to_word_end(struct TERM_EDIT *edit)
{
    /* TODO capitalize up to the end of next word */
    return send_char(KEY_BELL);
}

static int action_lowercase_to_word_end(struct TERM_EDIT *edit)
{
    /* TODO lowercase up to the end of next word */
    return send_char(KEY_BELL);
}

static int action_uppercase_to_word_end(struct TERM_EDIT *edit)
{
    /* TODO uppercase up to the end of next word */
    return send_char(KEY_BELL);
}

static int action_clear_after_cursor(struct TERM_EDIT *edit)
{
    if (edit->cursor_pos.physical == edit->line->size)
        return KOS_SUCCESS;

    edit->line->size = edit->cursor_pos.physical;
    edit->line_size  = edit->cursor_pos.logical;

    return send_escape(0, 'K');
}

static int action_clear_line(struct TERM_EDIT *edit)
{
    if ( ! edit->line_size)
        return KOS_SUCCESS;

    edit->line->size          = 0;
    edit->line_size           = 0;
    edit->cursor_pos.physical = 0;
    edit->cursor_pos.logical  = 0;
    edit->scroll_pos.physical = 0;
    edit->scroll_pos.logical  = 0;

    return clear_and_redraw(edit);
}

static int action_clear_screen(struct TERM_EDIT *edit)
{
    static const char clear_escape[] = "\x1B[H\x1B[2J";

    if (fwrite(clear_escape, 1, sizeof(clear_escape) - 1, stdout) != sizeof(clear_escape) - 1)
        return check_error(stdout);

    return clear_and_redraw(edit);
}

static int action_swap_chars(struct TERM_EDIT *edit)
{
    /* TODO swap char at cursor with previous char, then move cursor right */
    return send_char(KEY_BELL);
}

static int insert_char(struct TERM_EDIT *edit, char c)
{
    const size_t   init_size  = edit->line->size;
    const unsigned insert_pos = edit->cursor_pos.physical;
    const unsigned tail_size  = (unsigned)(init_size - insert_pos);
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
        increment_pos(edit, &edit->scroll_pos);
        return clear_and_redraw(edit);
    }

    if (tail_size || is_utf8_tail(c))
        return clear_and_redraw(edit);

    return send_char(c);
}

static int action_stop_process(struct TERM_EDIT *edit)
{
    int error;

    if (edit->interactive)
        restore_terminal(&old_term_info);

    error = kill(getpid(), SIGTSTP);

    if (error) {
        error = send_char(KEY_BELL);
        if (error)
            return error;
    }

    if (edit->interactive) {
        error = init_terminal(&old_term_info);
        if (error)
            return error;
    }

    return clear_and_redraw(edit);
}

static int action_tab_complete(struct TERM_EDIT *edit)
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
                case 'A': return action_up(edit);
                case 'B': return action_down(edit);
                case 'C': return action_right(edit);
                case 'D': return action_left(edit);
                case 'F': return action_end(edit);
                case 'H': return action_home(edit);
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
                        case 1:  return action_home(edit);
                        case 3:  return action_delete(edit);
                        case 4:  return action_end(edit);
                        case 7:  return action_home(edit);
                        case 8:  return action_end(edit);
                        default: break;
                    }
                    break;
                }

                case EOF: return check_error(stdin);
                case 'A': return action_up(edit);
                case 'B': return action_down(edit);
                case 'C': return action_right(edit);
                case 'D': return action_left(edit);
                case 'F': return action_end(edit);
                case 'H': return action_home(edit);
                default:  break;
            }
            break;
        }

        case 'b':           return action_word_begin(edit);
        case 'c':           return action_capitalize_to_word_end(edit);
        case 'd':           return action_delete_to_word_end(edit);
        case 'f':           return action_word_end(edit);
        case 'l':           return action_lowercase_to_word_end(edit);
        case 'u':           return action_uppercase_to_word_end(edit);
        case KEY_CTRL_H:    return action_delete_to_word_begin(edit);
        case KEY_BACKSPACE: return action_delete_to_word_begin(edit);

        default:
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
        case KEY_BACKSPACE: return action_backspace(edit);
        case KEY_CTRL_A:    return action_home(edit);
        case KEY_CTRL_B:    return action_left(edit);
        case KEY_CTRL_D:    return edit->line->size ? action_delete(edit) : check_error(stdin);
        case KEY_CTRL_E:    return action_end(edit);
        case KEY_CTRL_F:    return action_right(edit);
        case KEY_BELL:      return send_char(KEY_BELL);
        case KEY_CTRL_H:    return action_backspace(edit);
        case KEY_TAB:       return action_tab_complete(edit);
        case KEY_CTRL_K:    return action_clear_after_cursor(edit);
        case KEY_CTRL_L:    return action_clear_screen(edit);
        case KEY_CTRL_N:    return action_down(edit);
        case KEY_CTRL_P:    return action_up(edit);
        case KEY_CTRL_R:    return action_reverse_search(edit);
        case KEY_CTRL_T:    return action_swap_chars(edit);
        case KEY_CTRL_U:    return action_clear_line(edit);
        case KEY_CTRL_W:    return action_delete_to_word_begin(edit);
        case KEY_CTRL_Z:    return action_stop_process(edit);
        default:            return (*key < 0x20) ? send_char(KEY_BELL) : insert_char(edit, (char)*key);
    }
}

int kos_getline(KOS_GETLINE      *state,
                enum KOS_PROMPT_E prompt,
                KOS_VECTOR       *buf)
{
    int              error = KOS_SUCCESS;
    int              key   = 0;
    struct TERM_EDIT edit;

    memset(&edit, 0, sizeof(edit));
    edit.line = buf;
    kos_mempool_init(&edit.temp_allocator);
    error = init_history(&edit, state->head);
    if (error) {
        kos_mempool_destroy(&edit.temp_allocator);
        return error;
    }

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

    if (kos_is_stdin_interactive() && getenv("TERM")) {

        edit.interactive = 1;

        error = init_terminal(&old_term_info);
        if (error) {
            kos_mempool_destroy(&edit.temp_allocator);
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

    if (edit.interactive)
        restore_terminal(&old_term_info);

    if ( ! error && buf->size)
        error = add_to_persistent_history(state, buf->buffer, buf->size, edit.line_size);

    kos_mempool_destroy(&edit.temp_allocator);

    return error;
}

#endif

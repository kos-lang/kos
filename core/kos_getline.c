/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "kos_getline.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_system.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_utf8_internal.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'sscanf': This function may be unsafe */
#else
#   include <errno.h>
#   include <sys/ioctl.h>
#   include <termios.h>
#   include <unistd.h>
#endif

static int check_error(FILE *file)
{
    return ferror(file) ? KOS_ERROR_ERRNO : KOS_SUCCESS_RETURN;
}

#ifdef _WIN32

static int win_console_interactive = 0;

static int console_write(const char *data,
                         size_t      size)
{
    if (win_console_interactive) {

        DWORD num_written = 0;
        if ( ! WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),
                            data,
                            (DWORD)size,
                            &num_written,
                            KOS_NULL) || (num_written != size))
            return KOS_SUCCESS_RETURN;
    }
    else {
        if (fwrite(data, 1, size, stdout) != size)
            return check_error(stdout);
    }

    return KOS_SUCCESS;
}

static int console_read(void)
{
    uint8_t c        = 0;
    DWORD   num_read = 0;

    if ( ! win_console_interactive)
        return getchar();

    if ( ! ReadConsole(GetStdHandle(STD_INPUT_HANDLE),
                       &c,
                       1,
                       &num_read,
                       KOS_NULL))
        return EOF;

    return c;
}

#define is_term_set() 1

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
    (void)sigaction(sig, old_action, KOS_NULL);
}

static int console_write(const char *data,
                         size_t      size)
{
    if (fwrite(data, 1, size, stdout) != size)
        return check_error(stdout);

    return KOS_SUCCESS;
}

#define console_read getchar

#define is_term_set() getenv("TERM")

#endif

static int send_char(char c)
{
    return console_write(&c, 1);
}

static int send_escape(unsigned param, char code)
{
    char      esc[16];
    const int len   = snprintf(esc, sizeof(esc), "\x1B[%u%c", param, code);
    int       error = KOS_SUCCESS_RETURN;

    if ((size_t)len < sizeof(esc))
        error = console_write(esc, (size_t)len);

    return error;
}

static int move_cursor_right(unsigned offset)
{
    return send_escape(offset, 'C');
}

static int move_cursor_left(unsigned offset)
{
    return send_escape(offset, 'D');
}

#ifdef _WIN32
static unsigned get_num_columns(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.dwSize.X;

    /* Default to 80 columns */
    return 80;
}

typedef struct {
    DWORD input_mode;
    DWORD output_mode;
} TERM_INFO;

static TERM_INFO old_term_info;

static BOOL WINAPI ctrl_c_handler(DWORD ctrl_type)
{
    return ctrl_type == CTRL_C_EVENT;
}

static int init_terminal(TERM_INFO *old_info)
{
    const HANDLE h_input  = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE h_output = GetStdHandle(STD_OUTPUT_HANDLE);

    /* TODO report errors properly */

    if ((h_input == INVALID_HANDLE_VALUE) || (h_output == INVALID_HANDLE_VALUE))
        return KOS_ERROR_ERRNO;

    if ( ! GetConsoleMode(h_input, &old_info->input_mode))
        return KOS_ERROR_ERRNO;

    if ( ! GetConsoleMode(h_output, &old_info->output_mode))
        return KOS_ERROR_ERRNO;

    if ( ! SetConsoleMode(h_input, ENABLE_WINDOW_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT))
        return KOS_ERROR_ERRNO;

    if ( ! SetConsoleMode(h_output, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        SetConsoleMode(h_input, old_info->input_mode);
        return KOS_ERROR_ERRNO;
    }

    SetConsoleCtrlHandler(ctrl_c_handler, TRUE);

    return KOS_SUCCESS;
}

static void restore_terminal(TERM_INFO *old_info)
{
    SetConsoleCtrlHandler(ctrl_c_handler, FALSE);

    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  old_info->input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), old_info->output_mode);
}

#define notify_window_dimensions_changed() do { } while (0)

#define window_dimensions_changed() 1U

#else

static int receive_cursor_pos(unsigned *pos)
{
    int       c;
    unsigned  i     = 0;
    unsigned  rows  = 0;
    unsigned  cols  = 0;
    char      buf[16];

    c = console_read();

    if (c == EOF)
        return check_error(stdin);

    if (c != 0x1B) {
        ungetc(c, stdin);
        return KOS_ERROR_ERRNO;
    }

    do {
        c = console_read();

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

static unsigned get_num_columns(void)
{
    static int     esc_cursor_failed = 0;
    struct winsize ws;
    int            error;
    unsigned       cols;

    /* First, try to get terminal width via ioctl */
    error = ioctl(fileno(stdout), TIOCGWINSZ, &ws);

    cols = ws.ws_col;

    if ((error == -1) || (ws.ws_col == 0) || kos_seq_fail()) {

        /* Default to 80 columns */
        cols = 80;

        /* Second, fall back to reading via escape code, but attempt this only once */
        if ( ! esc_cursor_failed) {

            /* Move faaar to the right and query cursor position */
            static const char esc_get_width[] = "\x1B[9999C\x1B[6n";
            unsigned          rightmost_pos   = cols;

            if ((write(STDOUT_FILENO, esc_get_width, sizeof(esc_get_width) - 1) == sizeof(esc_get_width) - 1) &&
                    ! receive_cursor_pos(&rightmost_pos))
                cols = rightmost_pos;
            else
                esc_cursor_failed = 1; /* Don't try it again if the terminal does not support this */
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

static KOS_ATOMIC(uint32_t) window_dimensions_changed_flag;

static void notify_window_dimensions_changed()
{
    KOS_atomic_write_relaxed_u32(window_dimensions_changed_flag, 1);
}

static uint32_t window_dimensions_changed()
{
    return KOS_atomic_swap_u32(window_dimensions_changed_flag, 0);
}

static void sig_winch(int sig)
{
    assert(sig == SIGWINCH);

    notify_window_dimensions_changed();
}

static int init_terminal(TERM_INFO *old_info)
{
    int error = KOS_ERROR_ERRNO;

    struct termios new_attrs;

    if ( ! tcgetattr(fileno(stdin), &new_attrs)) {

        *old_info = new_attrs;

        new_attrs.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        new_attrs.c_oflag &= ~OPOST;
        new_attrs.c_cflag |= CS8;
        new_attrs.c_lflag &= ~(ECHOKE | ECHOE | ECHO | ECHONL | ECHOPRT | ECHOCTL | ICANON | IEXTEN | ISIG);
        new_attrs.c_cc[VMIN]  = 1;
        new_attrs.c_cc[VTIME] = 0;

        if (tcsetattr(fileno(stdin), TCSAFLUSH, &new_attrs) == 0) {

            error = install_signal(SIGWINCH, sig_winch, &old_sig_winch);
            if (error)
                (void)tcsetattr(fileno(stdin), TCSANOW, old_info);
        }
    }

    return error;
}

static void restore_terminal(TERM_INFO *old_info)
{
    restore_signal(SIGWINCH, &old_sig_winch);

    (void)tcsetattr(fileno(stdin), TCSANOW, old_info);
}

#endif

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

static int clear_and_redraw(struct TERM_EDIT *edit)
{
    const char *const begin        = edit->line->buffer + edit->scroll_pos.physical;
    const char *const end          = edit->line->buffer + edit->line->size;
    const char       *cur          = begin;
    unsigned          cursor_delta = 0;
    unsigned          num_left     = edit->num_columns - edit->prompt_size + 1;
    unsigned          num_to_write;
    int               error;

    assert(edit->cursor_pos.logical  <= edit->line_size);
    assert(edit->cursor_pos.physical <= edit->line->size);
    assert(edit->cursor_pos.logical  >= edit->scroll_pos.logical);
    assert(edit->cursor_pos.physical >= edit->scroll_pos.physical);

    edit->last_visible_column = edit->scroll_pos.logical + num_left - 1;

    /* Move cursor after terminal resize */
    while (edit->cursor_pos.logical > edit->last_visible_column)
        decrement_pos(edit, &edit->cursor_pos);

    error = console_write(edit->prompt, edit->prompt_size);
    if (error)
        return error;

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

        error = console_write(begin, num_to_write);
        if (error)
            return error;

        if ( ! edit->interactive)
            return KOS_SUCCESS;

        if (num_left) {
            error = send_escape(0, 'K');
            if (error)
                return error;
        }

        if (cursor_delta)
            return move_cursor_left(cursor_delta);

        return KOS_SUCCESS;
    }

    return edit->interactive ? send_escape(0, 'K') : KOS_SUCCESS;
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
    KOS_mempool_init(&state->allocator);

    state->head = KOS_NULL;

    return KOS_SUCCESS;
}

void kos_getline_destroy(KOS_GETLINE *state)
{
    KOS_mempool_destroy(&state->allocator);

    state->head = KOS_NULL;
}

static HIST_NODE *alloc_history_node(struct KOS_MEMPOOL_S *allocator,
                                     const char           *str,
                                     size_t                size,
                                     size_t                num_chars)
{
    const size_t capacity   = KOS_align_up(size, (size_t)16);
    const size_t alloc_size = sizeof(HIST_NODE) - 1 + capacity;

    HIST_NODE *node = (HIST_NODE *)KOS_mempool_alloc(allocator, alloc_size);

    if (node) {

        if (size)
            memcpy(node->buffer, str, size);

        node->capacity        = (uint16_t)capacity;
        node->size            = (uint16_t)size;
        node->line_size       = (uint16_t)num_chars;
        node->persistent      = 0;
        node->persistent_prev = KOS_NULL;
        node->next            = KOS_NULL;
        node->prev            = KOS_NULL;
    }

    return node;
}

static int add_to_persistent_history(KOS_GETLINE *state,
                                     const char  *str,
                                     size_t       size,
                                     size_t       num_chars)
{
    int        error = KOS_ERROR_OUT_OF_MEMORY;
    HIST_NODE *node  = alloc_history_node(&state->allocator, str, size, num_chars);

    if (node) {
        node->persistent      = 1;
        node->persistent_prev = state->head;
        state->head           = node;

        error = KOS_SUCCESS;
    }

    return error;
}

static int init_history(struct TERM_EDIT *edit, HIST_NODE *node)
{
    int        error     = KOS_ERROR_OUT_OF_MEMORY;
    HIST_NODE *next_node = alloc_history_node(&edit->temp_allocator, KOS_NULL, 0, 0);

    if (next_node) {

        edit->cur_hist_node = next_node;

        while (node) {
            next_node->prev = node;
            node->next      = next_node;
            next_node       = node;
            node            = node->persistent_prev;
        }

        next_node->prev = KOS_NULL;

        error = KOS_SUCCESS;
    }

    return error;
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

    node->size      = (uint16_t)edit->line->size;
    node->line_size = (uint16_t)edit->line_size;

    memcpy(&node->buffer[0], edit->line->buffer, edit->line->size);

    return KOS_SUCCESS;
}

static int restore_from_temp_history(struct TERM_EDIT *edit)
{
    HIST_NODE *node = edit->cur_hist_node;
    int        error;

    assert(node);

    error = KOS_vector_resize(edit->line, node->size);

    if ( ! error) {

        memcpy(edit->line->buffer, &node->buffer[0], node->size);

        edit->line_size = node->line_size;

        edit->cursor_pos.logical  = node->line_size;
        edit->cursor_pos.physical = (unsigned)edit->line->size;
        edit->scroll_pos.logical  = 0;
        edit->scroll_pos.physical = 0;

        if (node->line_size + edit->prompt_size > edit->num_columns) {

            struct TERM_POS pos = edit->cursor_pos;

            const unsigned scroll_target = pos.logical - edit->num_columns + edit->prompt_size;

            while (pos.logical > scroll_target)
                decrement_pos(edit, &pos);

            edit->scroll_pos = pos;
        }

        error = clear_and_redraw(edit);
    }

    return error;
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
    int               error;

    error = console_write(clear_escape, sizeof(clear_escape) - 1);

    if ( ! error)
        error = clear_and_redraw(edit);

    return error;
}

static int insert_char(struct TERM_EDIT *edit, char c)
{
    const size_t   init_size  = edit->line->size;
    const unsigned insert_pos = edit->cursor_pos.physical;
    const unsigned tail_size  = (unsigned)(init_size - insert_pos);
    int            error      = KOS_vector_resize(edit->line, init_size + 1);

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
#ifdef _WIN32
    return KOS_SUCCESS;
#else
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
#endif
}

static int action_tab_complete(struct TERM_EDIT *edit)
{
    /* TODO */
    return send_char(KEY_BELL);
}

static int dispatch_esc(struct TERM_EDIT *edit)
{
    int c = console_read();

    switch (c) {

        case EOF:
            return check_error(stdin);

        case 'O': {

            c = console_read();

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

            c = console_read();

            switch (c) {

                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9': {
                    int code            = c - '0';
                    int after_semicolon = 0;

                    do {
                        c = console_read();

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

        /* Unsupported cases:
         * - Alt-c  - capitalize to word end
         * - Alt-l  - lowercase to word end
         * - Alt-u  - uppercase to word end
         */

        case 'b':           return action_word_begin(edit);
        case 'd':           return action_delete_to_word_end(edit);
        case 'f':           return action_word_end(edit);
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
            return console_write("\r\n", 2);

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
        /* Unsupported: Ctrl-T - swap chars */
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

    KOS_mempool_init(&edit.temp_allocator);
    error = init_history(&edit, state->head);

    if ( ! error && kos_is_stdin_interactive() && is_term_set()) {

        edit.interactive = 1;

        error = init_terminal(&old_term_info);
    }

#ifdef _WIN32
    win_console_interactive = edit.interactive;
#endif

    notify_window_dimensions_changed();

    while ( ! error && (key != KEY_ENTER)) {

        if (window_dimensions_changed()) {

            const unsigned min_width = edit.prompt_size + 2;
            const unsigned max_width = 9999U;

            edit.num_columns = edit.interactive ? get_num_columns() : ~0U;

            edit.num_columns = (edit.interactive && (edit.num_columns < min_width)) ? min_width : edit.num_columns;
            edit.num_columns = (edit.interactive && (edit.num_columns > max_width)) ? max_width : edit.num_columns;

            error = clear_and_redraw(&edit);
        }

        if ( ! error) {

            key = console_read();

            error = dispatch_key(&edit, &key);
        }

        /* EINTR is typically caused by SIGWINCH */
        if ((error == KOS_ERROR_ERRNO) && (errno == EINTR)) {
            clearerr(stdin);
            clearerr(stdout);
            error = KOS_SUCCESS;
        }
    }

    if (edit.interactive)
        restore_terminal(&old_term_info);

    if ( ! error && buf->size)
        error = add_to_persistent_history(state, buf->buffer, buf->size, edit.line_size);

    KOS_mempool_destroy(&edit.temp_allocator);

    return error;
}

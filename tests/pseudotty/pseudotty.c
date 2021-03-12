/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int console_width = 20; /* Number of columns in the simulated console */
static int cursor_pos    = 1;  /* Simulated cursor position                  */
static int enable_esc_6n = 1;  /* Enable support for "get cursor pos" escape */
static int saw_eol       = 0;  /* EOL detection for detecting prompts        */
static int prompts_seen  = 0;  /* Count prompts received from client         */
static int verbose       = 0;  /* Enables debug output                       */

extern char **environ;

static int is_input_pending(int fd, int timeout_ms)
{
    struct pollfd pfd;

    pfd.fd      = fd;
    pfd.events  = POLLIN | POLLPRI;
    pfd.revents = 0;

    return poll(&pfd, 1, timeout_ms);
}

static int read_byte(int fd)
{
    char          c;
    const ssize_t num_read = read(fd, &c, 1);

    return (num_read > 0) ? (int)(unsigned char)c : EOF;
}

static void output_byte(int byte)
{
    if (byte == 0x0D) {
        putchar('\\');
        putchar('r');
    }
    else if (byte == 0x0A)
        putchar('\n');
    else if (byte == 0x1B) {
        putchar('\\');
        putchar('e');
    }
    else if (byte == 7) {
        putchar('\\');
        putchar('a');
    }
    else if ((byte < 0x20) || (byte >= 0x7F))
        printf("\\x%02x", byte);
    else
        putchar((char)byte);
}

static void update_cursor(int byte)
{
    if (byte == 0x0D)
        cursor_pos = 1;
    else if (((byte ^ 0x80) & ~0xC0) && (byte >= 0x20))
        ++cursor_pos;
}

enum COLOR {
    RED     = 31,
    GREEN   = 32,
    YELLOW  = 33,
    BLUE    = 34,
    MAGENTA = 35,
    CYAN    = 36
};

static void print_color(int color, const char *text)
{
    printf("\033[1;%dm%s\033[0m", color, text);
    fflush(stdout);
}

/* Crude prompt detection from Kos */
static int is_prompt(char c)
{
    return (c == '>') || (c == '_');
}

enum ESC_TYPE {
    ESC_NONE,
    ESC_OTHER,
    ESC_6N
};

/* Handles incoming escape sequences from client */
static size_t handle_escape(int tty_fd, char *buf, size_t max_size, enum ESC_TYPE *esc_found)
{
    size_t buf_size = 1;

    buf[0] = '\x1B';

    *esc_found = ESC_NONE;

    do {
        const int byte = read_byte(tty_fd);

        if (byte == EOF)
            break;

        buf[buf_size++] = (char)byte;

        if ((buf_size == 2) && (buf[1] != '['))
            break;

        if (isalpha(byte)) {

            switch ((char)byte) {
                case 'n': {
                    static const char get_cursor_pos[] = "\x1B[6n";

                    if ((buf_size == sizeof(get_cursor_pos) - 1) &&
                        (memcmp(buf, get_cursor_pos, sizeof(get_cursor_pos) - 1) == 0)) {

                        if (enable_esc_6n) {
                            char      esc_buf[20];
                            const int len = snprintf(esc_buf, sizeof(esc_buf), "\x1B[1;%dR", cursor_pos);

                            if ((len > 5) && ((unsigned)len < sizeof(esc_buf)))
                                if (write(tty_fd, esc_buf, len) == len)
                                    *esc_found = ESC_6N;
                        }
                        else
                            *esc_found = ESC_6N;
                    }

                    break;
                }

                case 'C': {
                    unsigned delta = 0;

                    buf[buf_size] = 0;
                    if (sscanf(buf, "\x1B[%uC", &delta) == 1) {
                        if ((delta > (unsigned)console_width) || ((cursor_pos + (int)delta) > console_width))
                            cursor_pos = console_width;
                        else
                            cursor_pos += delta;

                        *esc_found = ESC_OTHER;
                    }
                    break;
                }

                case 'D': {
                    unsigned delta = 0;

                    buf[buf_size] = 0;
                    if (sscanf(buf, "\x1B[%uD", &delta) == 1) {
                        if (delta >= (unsigned)cursor_pos)
                            cursor_pos = 1;
                        else
                            cursor_pos -= delta;

                        *esc_found = ESC_OTHER;
                    }
                    break;
                }

                case 'H':
                case 'J':
                case 'K':
                    *esc_found = ESC_OTHER;
                    break;

                default:
                    break;
            }

            break;
        }

    } while (buf_size + 1 < max_size);

    return buf_size;
}

enum RECEIVE_STATUS {
    RECEIVED_NOTHING,
    RECEIVE_ERROR,
    RECEIVED_SOMETHING,
    RECEIVED_PROMPT
};

/* Receives input from tty_fd and writes it out to stdout.
 * Translates non-printable characters (except \n) to escape sequences.
 */
static enum RECEIVE_STATUS receive_one_batch_of_input(int tty_fd, int timeout_ms)
{
    int                 pending;
    enum RECEIVE_STATUS status = RECEIVED_NOTHING;

    pending = is_input_pending(tty_fd, timeout_ms);

    while (pending == 1) {
        const int byte = read_byte(tty_fd);

        if (byte == EOF)
            break;

        if (byte == 0x1B) {
            char          buf[16];
            enum ESC_TYPE esc_found;
            size_t        i;
            const size_t  buf_size = handle_escape(tty_fd, buf, sizeof(buf), &esc_found);

            assert(buf_size > 0);

            for (i = 0; i < buf_size; i++)
                output_byte((int)(unsigned char)buf[i]);

            /* If we received ESC[6n, the client may be waiting for response. */
            if (esc_found == ESC_6N)
                status = RECEIVED_PROMPT;

            if ( ! esc_found && ! status)
                status = RECEIVED_SOMETHING;
        }
        else {
            if ((char)byte == '\n')
                saw_eol = 1;
            else if (saw_eol && is_prompt((char)byte)) {
                saw_eol = 0;
                ++prompts_seen;
                status = RECEIVED_PROMPT;
            }

            if ( ! status)
                status = RECEIVED_SOMETHING;

            output_byte(byte);
            update_cursor(byte);
        }

        pending = is_input_pending(tty_fd, 0);
    }

    if (verbose && status)
        print_color(GREEN, status == RECEIVED_PROMPT ? "P" : "S");

    fflush(stdout);

    return (pending < 0) ? RECEIVE_ERROR : status;
}

typedef struct {
    pid_t child_pid;
    int   running;
    int   stopped;
    int   status;
} CHILD_INFO;

static int check_child_status(CHILD_INFO *child_info, int options)
{
    int   status    = 0;
    pid_t ret_child = 0;

    if ( ! child_info->running)
        return child_info->status;

    ret_child = waitpid(child_info->child_pid, &status, options);
    if ( ! ret_child)
        return 0;

    assert((ret_child == -1) || (ret_child == child_info->child_pid));

    if (ret_child == -1) {
        perror("wait error");
        child_info->running = 0;
    }
    else if (WIFEXITED(status)) {
        child_info->status  = WEXITSTATUS(status);
        child_info->running = 0;
    }
    else if (WIFSTOPPED(status)) {
        if (verbose)
            print_color(BLUE, "STOP");

        child_info->stopped = 1;
        kill(child_info->child_pid, SIGCONT);
        return 0;
    }
    else if (WIFSIGNALED(status)) {
        fprintf(stderr, "Child exited due to signal %d\n", WTERMSIG(status));
        child_info->running = 0;
    }
    else {
        fprintf(stderr, "Unexpected child exit\n");
        child_info->running = 0;
    }

    return child_info->status;
}

static uint64_t get_time_ms(void)
{
    uint64_t       time_ms = 0;
    struct timeval tv;

    if ( ! gettimeofday(&tv, NULL)) {
        time_ms =  (int64_t)tv.tv_sec * 1000;
        time_ms += (int64_t)tv.tv_usec / 1000;
    }

    return time_ms;
}

enum INPUT_STATUS {
    NO_INPUT,
    SOME_INPUT,
    INPUT_PROMPT,
    INPUT_ERROR
};

static enum INPUT_STATUS receive_input(int tty_fd, CHILD_INFO *child_info, int timeout_ms)
{
    enum RECEIVE_STATUS saved_status     = RECEIVED_NOTHING;
    uint64_t            start_time_ms    = get_time_ms();
    uint64_t            prev_time_ms     = start_time_ms;
    int                 cur_wait_time_ms = 0;

    do {

        const int cur_timeout_ms = (cur_wait_time_ms < timeout_ms) ? (timeout_ms - cur_wait_time_ms) : 0;

        enum RECEIVE_STATUS status = receive_one_batch_of_input(tty_fd, cur_timeout_ms);

        prev_time_ms     = get_time_ms();
        cur_wait_time_ms = (int)(prev_time_ms - start_time_ms);

        switch (status) {

            case RECEIVE_ERROR:
                return INPUT_ERROR;

            case RECEIVED_NOTHING:
                if (cur_wait_time_ms > timeout_ms)
                    return (saved_status == RECEIVED_SOMETHING) ? SOME_INPUT : NO_INPUT;
                break;

            default:
                if (status == RECEIVED_PROMPT)
                    return INPUT_PROMPT;
                saved_status = status;
                break;
        }

    } while ( ! check_child_status(child_info, WUNTRACED | WNOHANG));

    return INPUT_ERROR;
}

static enum INPUT_STATUS receive_pending_input(int tty_fd, CHILD_INFO *child_info)
{
    return receive_input(tty_fd, child_info, 1);
}

static unsigned char from_hex(char c)
{
    if ((c >= '0') && (c <= '9'))
        return (unsigned char)(c - '0');
    else if ((c >= 'A') && (c <= 'F'))
        return (unsigned char)(c - 'A' + 10);
    else if ((c >= 'a') && (c <= 'f'))
        return (unsigned char)(c - 'a' + 10);
    else
        return 0;
}

enum SEND_RESULT {
    SEND_OK,
    SEND_END_OF_SCRIPT,
    SEND_ERROR,
    SEND_SUSPEND,
    SEND_WAIT_FOR_CURSOR
};

/* Extracts a single command from script read from stdin and sends it to the tty tty_fd. */
static enum SEND_RESULT send_one_line_from_script(int tty_fd, CHILD_INFO *child_info)
{
    static char   script_buf[64 * 1024];
    static size_t size;
    char         *comment;
    size_t        to_read      = sizeof(script_buf) - size;
    size_t        comment_size = 0;
    size_t        cmd_size;
    size_t        line_size;
    int           eol          = 0;

    if (to_read) {
        const ssize_t num_read = read(STDIN_FILENO, &script_buf[size], to_read);

        if (num_read < 0)
            perror("script read error");

        if (num_read > 0)
            size += num_read;
    }

    if ( ! size)
        return SEND_END_OF_SCRIPT;

    /* Find end of line */
    {
        char *const eol_ptr = (char *)memchr(script_buf, '\n', size);

        cmd_size  = eol_ptr ? (size_t)(eol_ptr - &script_buf[0]) : size;
        line_size = cmd_size + (eol_ptr ? 1 : 0);
    }

    /* Find comment */
    {
        comment = (char *)memchr(script_buf, '#', cmd_size);

        if (comment) {
            comment_size = cmd_size - (comment - &script_buf[0]) - 1;
            cmd_size     = comment - &script_buf[0];
            ++comment;

            /* Remove trailing spaces */
            while (cmd_size && (script_buf[cmd_size - 1] == ' '))
                --cmd_size;
        }
    }

    /* Send command to the child */
    if (cmd_size) {

        ssize_t num_written;
        char   *end = &script_buf[cmd_size];
        char   *ptr = &script_buf[0];

        if (verbose) {
            char size_str[16];
            snprintf(size_str, sizeof(size_str), "S%u", (unsigned)cmd_size);
            print_color(YELLOW, size_str);
        }

        /* Convert escaped characters */
        while (ptr < end) {
            char c         = 0;
            int  to_delete = 0;

            ptr = (char *)memchr(ptr, '\\', end - ptr);
            if ( ! ptr)
                break;

            if (ptr + 1 == end)
                break;

            switch (ptr[1]) {
                case 'r':  c = '\r';   to_delete = 1; eol = 1; break;
                case 'n':  c = '\n';   to_delete = 1; eol = 1; break;
                case 'e':  c = '\x1B'; to_delete = 1; break;
                case 'a':  c = '\a';   to_delete = 1; break;
                case '\\': c = '\\';   to_delete = 1; break;

                case 'x':
                    if (ptr + 3 < end) {
                        c = (char)((from_hex(ptr[2]) << 4) | from_hex(ptr[3]));
                        to_delete = 3;

                        /* Ctrl-C works like Enter */
                        if (c == 3)
                            eol = 1;
                    }
                    break;

                default:
                    break;
            }

            if (to_delete) {
                ptr[0] = c;
                memmove(&ptr[1], &ptr[1 + to_delete], (end - ptr) - 1 - to_delete);
                end      -= to_delete;
                cmd_size -= to_delete;
            }
            else
                ++ptr;
        }

        /* Ctrl-Z */
        if ((cmd_size == 1) && (*script_buf == 0x1A)) {
            if (receive_pending_input(tty_fd, child_info) == INPUT_ERROR)
                return SEND_ERROR;
        }

        num_written = write(tty_fd, script_buf, cmd_size);

        if (num_written != (ssize_t)cmd_size) {
            if (num_written < 0)
                perror("send error");
            return SEND_ERROR;
        }

        /* Ctrl-Z -- wait for child to become suspended */
        if ((cmd_size == 1) && (*script_buf == 0x1A)) {

            const uint64_t start_time_ms = get_time_ms();

            child_info->stopped = 0;

            do {

                struct timespec ts;

                if (check_child_status(child_info, WUNTRACED | WNOHANG))
                    return SEND_ERROR;

                if (get_time_ms() - start_time_ms > 5000) {
                    if (verbose)
                        print_color(RED, "TIMEOUT");
                    break;
                }

                ts.tv_sec  = 0;
                ts.tv_nsec = 1000000;
                nanosleep(&ts, NULL);

            } while ( ! child_info->stopped);
        }
    }

    /* Handle special commands for pseudotty placed in comments */
    if (comment && (comment_size > 1) && (*comment != ' ')) {

        static const char cmt_disable_cursor_pos[] = "disable_cursor_pos";  /* Disable responses to \e[6n */
        static const char cmt_resize[]             = "resize";              /* Send SIGWINCH */

        if ((comment_size >= sizeof(cmt_disable_cursor_pos) - 1) &&
            (strncmp(cmt_disable_cursor_pos, comment, sizeof(cmt_disable_cursor_pos) - 1) == 0)) {

            printf("[[disable_cursor_pos]]");
            enable_esc_6n = 0;
        }
        else if ((comment_size >= sizeof(cmt_resize) - 1) &&
                 (strncmp(cmt_resize, comment, sizeof(cmt_resize) - 1) == 0)) {

            unsigned new_width = 0;

            memmove(comment, comment + 6, comment_size - 6);
            comment[comment_size - 6] = 0;

            if (sscanf(comment, "%u", &new_width) == 1) {

                printf("[[resize %u]]", new_width);

                console_width = (int)new_width;
                if (cursor_pos > console_width)
                    cursor_pos = console_width;

                if (receive_pending_input(tty_fd, child_info) == INPUT_ERROR)
                    return SEND_ERROR;

                if (kill(child_info->child_pid, SIGWINCH) != 0) {
                    perror("kill(SIGWINCH) error");
                    size = 0;
                    return SEND_ERROR;
                }
            }
        }
    }

    /* Remove used line */
    if (line_size < size)
        memmove(&script_buf[0], &script_buf[line_size], size - line_size);
    size -= line_size;

    if (eol)
        return SEND_WAIT_FOR_CURSOR;

    return size ? SEND_OK : SEND_END_OF_SCRIPT;
}

int main(int argc, char *argv[])
{
    int              master_fd     = -1;
    int              prog_arg      = 1;
    char            *term_tty_name = NULL;
    enum SEND_RESULT send_result   = SEND_WAIT_FOR_CURSOR;
    CHILD_INFO       child_info;

    child_info.running = 0;
    child_info.stopped = 0;
    child_info.status  = EXIT_FAILURE;

    if ((argc >= 2) && (strcmp(argv[1], "--verbose") == 0)) {
        verbose  = 1;
        prog_arg = 2;
    }

    if (argc < prog_arg + 1) {
        fprintf(stderr, "Missing arguments\n");
        printf("Usage: pseudotty [--verbose] <PROGRAM> [<ARGS>...]\n");
        return EXIT_FAILURE;
    }

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd == -1) {
        perror("posix_openpt error");
        goto cleanup;
    }

    if (grantpt(master_fd) != 0) {
        perror("grantpt error");
        goto cleanup;
    }

    if (unlockpt(master_fd) != 0) {
        perror("unlockpt error");
        goto cleanup;
    }

    term_tty_name = ptsname(master_fd);
    if ( ! term_tty_name) {
        perror("ptsname error");
        goto cleanup;
    }
    term_tty_name = strdup(term_tty_name);
    if ( ! term_tty_name) {
        perror("strdup error");
        goto cleanup;
    }

    child_info.running = 0;
    child_info.child_pid = fork();
    if (child_info.child_pid == -1) {
        perror("fork error");
        goto cleanup;
    }

    /* In the child process run the target program specified by args */
    if (child_info.child_pid == 0) {
        int          input_fd;
        int          output_fd;
        size_t       srci;
        size_t       dsti;
        static char *env[128];
        static char  term[] = "TERM=test";
        static char  cols[] = "COLUMNS=20";

        /* Close fd of the master pseudo tty in child process */
        close(master_fd);

        /* Open the slave tty as stdin/stdout */
        input_fd = open(term_tty_name, O_RDONLY);
        if (input_fd == -1) {
            perror("slave open error");
            goto cleanup;
        }
        output_fd = open(term_tty_name, O_WRONLY);
        if (output_fd == -1) {
            perror("slave open error");
            goto cleanup;
        }
        free(term_tty_name);
        term_tty_name = NULL;
        if ((dup2(input_fd, STDIN_FILENO) == -1) ||
            (dup2(output_fd, STDOUT_FILENO) == -1) ||
            (dup2(output_fd, STDERR_FILENO) == -1)) {
            perror("dup2 error");
            goto cleanup;
        }
        close(input_fd);
        close(output_fd);

        /* Copy environment and override TERM and COLUMNS variables */
        for (srci = 0, dsti = 0; environ[srci] && (dsti < (sizeof(env) / sizeof(env[0])) - 3); ++srci) {
            static const char term_eq[] = "TERM=";
            static const char cols_eq[] = "COLUMNS=";

            if ((strncmp(environ[srci], term_eq, sizeof(term_eq) - 1) == 0) ||
                (strncmp(environ[srci], cols_eq, sizeof(cols_eq) - 1) == 0))
                continue;

            env[dsti++] = environ[srci];
        }
        assert(dsti + 3 <= sizeof(env) / sizeof(env[0]));
        env[dsti++] = term;
        env[dsti++] = cols;
        env[dsti]   = NULL;

        execve(argv[prog_arg], &argv[prog_arg], env);

        perror("execv error");
        goto cleanup;
    }

    child_info.running = 1;

    /* Execute the script and receive output from child */
    do {
        enum INPUT_STATUS status;

        if (send_result == SEND_WAIT_FOR_CURSOR) {
            status = receive_input(master_fd, &child_info, 10000);

            if (status != INPUT_PROMPT) {
                fprintf(stderr, "Expected to receive cursor query from the child\n");
                break;
            }

            send_result = (status == INPUT_ERROR) ? SEND_ERROR : SEND_OK;
        }

        if ((send_result != SEND_END_OF_SCRIPT) && (send_result != SEND_ERROR)) {
            status = receive_input(master_fd, &child_info, 0);

            if (status != INPUT_ERROR)
                send_result = send_one_line_from_script(master_fd, &child_info);
        }
        else
            status = receive_input(master_fd, &child_info, 1);

        if (status == INPUT_ERROR)
            break;
    } while (child_info.running);

    close(master_fd);
    master_fd = -1;

    check_child_status(&child_info, 0);

cleanup:
    if (master_fd != -1)
        close(master_fd);

    if (term_tty_name)
        free(term_tty_name);

    return child_info.status;
}

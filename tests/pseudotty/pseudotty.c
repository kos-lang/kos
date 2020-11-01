/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int console_width = 20; /* Number of columns in the simulated console */
static int cursor_pos    = 1;  /* Simulated cursor position                  */
static int enable_esc_6n = 1;  /* Enable support for "get cursor pos" escape */
static int saw_prompt    = 0;  /* Prompt has been detected from client       */

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

    return num_read ? (int)(unsigned char)c : EOF;
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
    else if ((byte < 0x20) || (byte == 0x7F))
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

/* Crude prompt detection from kos */
static void detect_prompt(char c)
{
    if ((c == '>') || (c == '_'))
        saw_prompt = 1;
}

/* Handles incoming escape sequences from client */
static size_t handle_escape(int tty_fd, char *buf, size_t max_size, int *handled)
{
    size_t buf_size = 1;

    buf[0] = '\x1B';

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

                    if (enable_esc_6n &&
                        (buf_size == sizeof(get_cursor_pos) - 1) &&
                        (memcmp(buf, get_cursor_pos, sizeof(get_cursor_pos) - 1) == 0)) {

                        char      esc_buf[20];
                        const int len = snprintf(esc_buf, sizeof(esc_buf), "\x1B[1;%dR", cursor_pos);

                        if ((len > 5) && ((unsigned)len < sizeof(esc_buf)))
                            if (write(tty_fd, esc_buf, len) == len)
                                *handled = 1;
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

                        *handled = 1;
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

                        *handled = 1;
                    }
                    break;
                }

                case 'H':
                case 'J':
                case 'K':
                    *handled = 1;
                    break;

                default:
                    break;
            }

            break;
        }

    } while (buf_size + 1 < max_size);

    return buf_size;
}

/* Receives input from tty_fd and writes it out to stdout.
 * Translates non-printable characters (except \n) to escape sequences.
 * Returns number of characters received (zero or more).
 * On failure returns -1.
 */
static int receive_input(int tty_fd)
{
    int total_read = 0;
    int pending;

    pending = is_input_pending(tty_fd, 1);

    if (pending < 0)
        return -1;

    if (pending == 0)
        return 0;

    do {
        const int byte = read_byte(tty_fd);

        if (byte == EOF)
            break;

        if (byte == 0x1B) {
            char         buf[16];
            int          handled  = 0;
            const size_t buf_size = handle_escape(tty_fd, buf, sizeof(buf), &handled);

            if (buf_size) {
                size_t i;

                for (i = 0; i < buf_size; i++)
                    output_byte((int)(unsigned char)buf[i]);

                if ( ! handled)
                    total_read += buf_size;
            }
            else {
                ++total_read;
                output_byte(byte);
            }
        }
        else {
            detect_prompt((char)byte);

            ++total_read;
            output_byte(byte);
            update_cursor(byte);
        }

        pending = is_input_pending(tty_fd, 1);
    } while (pending == 1);

    return total_read;
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

/* Extracts a single command from script read from stdin and sends it to the tty tty_fd.
 * Returns 0 on success and non-zero on failure or end of script. */
static int run_one_command_from_script(int tty_fd, pid_t child_pid)
{
    static char   script_buf[64 * 1024];
    static size_t size;
    char         *comment;
    size_t        to_read      = sizeof(script_buf) - size;
    size_t        comment_size = 0;
    size_t        cmd_size;
    size_t        line_size;

    if (to_read) {
        const ssize_t num_read = read(STDIN_FILENO, &script_buf[size], to_read);

        if (num_read < 0)
            perror("script read error");

        if (num_read > 0)
            size += num_read;
    }

    if ( ! size)
        return 1;

    /* Find end of line */
    {
        char *const eol = (char *)memchr(script_buf, '\n', size);

        cmd_size  = eol ? (size_t)(eol - &script_buf[0]) : size;
        line_size = cmd_size + (eol ? 1 : 0);
    }

    /* Find comment */
    {
        comment = (char *)memchr(script_buf, '@', cmd_size);

        if (comment) {
            comment_size = cmd_size - (comment - &script_buf[0]) - 1;
            cmd_size     = comment - &script_buf[0];
            ++comment;
        }
    }

    /* Send command to the child */
    if (cmd_size) {

        char *end = &script_buf[cmd_size];
        char *ptr = &script_buf[0];

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
                case 'r':  c = '\r';   to_delete = 1; break;
                case 'n':  c = '\n';   to_delete = 1; break;
                case 'e':  c = '\x1B'; to_delete = 1; break;
                case 'a':  c = '\a';   to_delete = 1; break;
                case '\\': c = '\\';   to_delete = 1; break;

                case 'x':
                    if (ptr + 3 < end) {
                        c = (char)((from_hex(ptr[2]) << 4) | from_hex(ptr[3]));
                        to_delete = 3;
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

        if (write(tty_fd, script_buf, cmd_size) != (ssize_t)cmd_size)
            return 1;
    }
    /* Handle special commands for pseudotty placed in comments */
    else if (comment) {
        if (strncmp("disable_cursor_pos", comment, comment_size) == 0) {
            printf("[[disable_cursor_pos]]");
            enable_esc_6n = 0;
        }
        else if (strncmp("enable_cursor_pos", comment, comment_size) == 0) {
            printf("[[enable_cursor_pos]]");
            enable_esc_6n = 1;
        }
        else if ((strncmp("resize", comment, comment_size) == 0) && (comment_size > 6)) {
            unsigned new_width = 0;

            memmove(comment, comment + 6, comment_size - 6);
            comment[comment_size - 6] = 0;

            if (sscanf(comment, "%u", &new_width) == 1) {

                printf("[[resize %u]]", new_width);

                console_width = (int)new_width;
                if (cursor_pos > console_width)
                    cursor_pos = console_width;

                kill(SIGWINCH, child_pid);
            }
        }
    }

    /* Remove used line */
    if (line_size < size)
        memmove(&script_buf[0], &script_buf[line_size], size - line_size);
    size -= line_size;

    return 0;
}

int main(int argc, char *argv[])
{
    int   retval         = EXIT_FAILURE;
    int   master_fd      = -1;
    char *term_tty_name  = NULL;
    pid_t child_pid;

    if (argc < 2) {
        fprintf(stderr, "Missing arguments\n");
        printf("Usage: pseudotty <PROGRAM> [<ARGS>...]\n");
        return EXIT_FAILURE;
    }

    master_fd = posix_openpt(O_RDWR);
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

    child_pid = fork();
    if (child_pid == -1) {
        perror("fork error");
        goto cleanup;
    }

    /* In the child process run the target program specified by args */
    if (child_pid == 0) {
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

        execve(argv[1], &argv[1], env);

        perror("execv error");
        goto cleanup;
    }

    /* Execute the script and receive output from child */
    for (;;) {

        const int num_received = receive_input(master_fd);
        if (num_received < 0)
            break;

        if (kill(child_pid, 0) != 0)
            break;

        if (saw_prompt && ! num_received && run_one_command_from_script(master_fd, child_pid))
            break;
    }

    close(master_fd);
    master_fd = -1;

    {
        int status = 0;
        if (wait(&status) == -1) {
            perror("wait error");
            goto cleanup;
        }

        if (WIFEXITED(status))
            retval = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            fprintf(stderr, "Child exited due to signal %d\n", WTERMSIG(status));
        else
            fprintf(stderr, "Unexpected child exit\n");
    }

cleanup:
    if (master_fd != -1)
        close(master_fd);

    if (term_tty_name)
        free(term_tty_name);

    return retval;
}

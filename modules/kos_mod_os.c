/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_debug.h"
#include "../core/kos_try.h"
#include "kos_mod_io.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* 'getenv': This function may be unsafe */
#else
#   include <errno.h>
#   include <fcntl.h>
#   include <signal.h>
#   include <sys/wait.h>
#   include <unistd.h>
#endif

#if defined(__ANDROID__)
#   define KOS_SYSNAME "Android"
#elif defined(__APPLE__)
#   include <TargetConditionals.h>
#   if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || (defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR)
#       define KOS_SYSNAME "iOS"
#   else
#       define KOS_SYSNAME "macOS"
#   endif
#elif defined(__FreeBSD__)
#   define KOS_SYSNAME "FreeBSD"
#elif defined(__HAIKU__)
#   define KOS_SYSNAME "Haiku"
#elif defined(__linux__)
#   define KOS_SYSNAME "Linux"
#elif defined(__NetBSD__)
#   define KOS_SYSNAME "NetBSD"
#elif defined(__OpenBSD__)
#   define KOS_SYSNAME "OpenBSD"
#elif defined(__QNX__)
#   define KOS_SYSNAME "QNX"
#elif defined(_WIN32)
#   define KOS_SYSNAME "Windows"
#else
#   define KOS_SYSNAME "Unknown"
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_args,               "args");
KOS_DECLARE_STATIC_CONST_STRING(str_cwd,                "cwd");
KOS_DECLARE_STATIC_CONST_STRING(str_default_value,      "default_value");
KOS_DECLARE_STATIC_CONST_STRING(str_env,                "env");
KOS_DECLARE_STATIC_CONST_STRING(str_eq,                 "=");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_string, "invalid string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_spawned,    "object is not a spawned process");
KOS_DECLARE_STATIC_CONST_STRING(str_err_use_spawn,      "use os.spawn() to launch processes");
KOS_DECLARE_STATIC_CONST_STRING(str_inherit_env,        "inherit_env");
KOS_DECLARE_STATIC_CONST_STRING(str_key,                "key");
KOS_DECLARE_STATIC_CONST_STRING(str_program,            "program");
KOS_DECLARE_STATIC_CONST_STRING(str_signal,             "signal");
KOS_DECLARE_STATIC_CONST_STRING(str_status,             "status");
KOS_DECLARE_STATIC_CONST_STRING(str_stderr,             "stderr");
KOS_DECLARE_STATIC_CONST_STRING(str_stdin,              "stdin");
KOS_DECLARE_STATIC_CONST_STRING(str_stdout,             "stdout");
KOS_DECLARE_STATIC_CONST_STRING(str_stopped,            "stopped");

struct KOS_WAIT_S {
#ifdef _WIN32
    HANDLE h_process;
    DWORD  pid;
#else
    pid_t pid;
#endif
};

static const char *get_type_name(KOS_TYPE type)
{
    static const char *const type_names[] = {
        "integer",
        "integer",
        "float",
        "void",
        "boolean",
        "string",
        "object",
        "array",
        "buffer",
        "function",
        "class"
    };

    assert(type <= OBJ_LAST_TYPE);

    return type_names[(int)type >> 1];
}

static int check_arg_type(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  obj_id,
                          const char *name,
                          KOS_TYPE    expected_type)
{
    const KOS_TYPE actual_type = GET_OBJ_TYPE(obj_id);

    if (actual_type != expected_type) {
        const char *const actual_str   = get_type_name(actual_type);
        const char *const expected_str = get_type_name(expected_type);

        KOS_raise_printf(ctx, "argument '%s' is %s, but expected %s\n",
                         name, actual_str, expected_str);
        return KOS_ERROR_EXCEPTION;
    }

    return KOS_SUCCESS;
}

static int get_string(KOS_CONTEXT           ctx,
                      KOS_OBJ_ID            obj_id,
                      struct KOS_MEMPOOL_S *alloc,
                      char                **out_cstr)
{
    unsigned str_len = 0;
    char    *buf;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);

    if (KOS_get_string_length(obj_id) > 0) {
        str_len = KOS_string_to_utf8(obj_id, KOS_NULL, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception(ctx, KOS_CONST_ID(str_err_invalid_string));
            return KOS_ERROR_EXCEPTION;
        }
    }

    buf = (char *)KOS_mempool_alloc(alloc, str_len + 1);
    if ( ! buf) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    if (str_len)
        KOS_string_to_utf8(obj_id, buf, str_len);

    buf[str_len] = 0;

    *out_cstr = buf;

    return KOS_SUCCESS;
}

static int get_args_array(KOS_CONTEXT           ctx,
                          KOS_OBJ_ID            obj_id,
                          struct KOS_MEMPOOL_S *alloc,
                          char               ***out_array)
{
    int            error     = KOS_SUCCESS;
    const uint32_t num_elems = KOS_get_array_size(obj_id);
    uint32_t       i;
    char         **array;

    array = (char **)KOS_mempool_alloc(alloc, (num_elems + 2) * sizeof(void *));
    if ( ! array) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    array[0] = KOS_NULL;

    for (i = 0; i < num_elems; i++) {
        char      *elem_cstr;
        KOS_OBJ_ID str_id;
        KOS_TYPE   type;

        str_id = KOS_array_read(ctx, obj_id, i);
        TRY_OBJID(str_id);

        type = GET_OBJ_TYPE(str_id);
        if (type != OBJ_STRING) {
            KOS_raise_printf(ctx,
                             "element %u in 'args' array passed to os.spawn() is %s, but expected string",
                             i, get_type_name(type));
            return KOS_ERROR_EXCEPTION;
        }

        TRY(get_string(ctx, str_id, alloc, &elem_cstr));

        array[i + 1] = elem_cstr;
    }

    array[num_elems + 1] = KOS_NULL;

    *out_array = array;

cleanup:
    return error;
}

#ifdef _WIN32
static char **get_cur_env(void)
{
    static char *fake_env = KOS_NULL;

    return &fake_env;
}

#else

extern char **environ;

static char **get_cur_env(void)
{
    return environ;
}
#endif

static int get_env_array(KOS_CONTEXT           ctx,
                         KOS_OBJ_ID            obj_id,
                         int                   inherit_env,
                         struct KOS_MEMPOOL_S *alloc,
                         char               ***out_array)
{
    KOS_LOCAL obj;
    KOS_LOCAL in_obj;
    KOS_LOCAL name;
    int       error       = KOS_SUCCESS;
    unsigned  est_num_env = 0;
    char    **array;
    char    **out_ptr;

    KOS_init_locals(ctx, 3, &obj, &in_obj, &name);

    /* If inheriting environment, join vars from environment with overrides from the call */
    if (inherit_env) {

        char **env = get_cur_env();

        in_obj.o = obj_id;

        obj.o = KOS_new_object(ctx);
        TRY_OBJID(obj.o);

        for ( ; *env; ++env) {
            KOS_OBJ_ID     value;
            char          *name_str = *env;
            char          *val_str  = strchr(name_str, '=');
            const unsigned name_len = val_str ? (unsigned)(val_str - name_str) : 0U;
            const unsigned val_len  = val_str ? (unsigned)strlen(val_str + 1) : 0U;

            if (val_str) {
                ++val_str;

                name.o = KOS_new_string(ctx, name_str, name_len);
                TRY_OBJID(name.o);

                value  = KOS_new_string(ctx, val_str, val_len);
                TRY_OBJID(value);

                TRY(KOS_set_property(ctx, obj.o, name.o, value));

                ++est_num_env;
            }
        }

        in_obj.o = KOS_new_iterator(ctx, in_obj.o, KOS_SHALLOW);
        TRY_OBJID(in_obj.o);

        while ( ! KOS_iterator_next(ctx, in_obj.o)) {
            const KOS_OBJ_ID value_obj = KOS_get_walk_value(in_obj.o);
            const KOS_TYPE   type      = GET_OBJ_TYPE(value_obj);

            assert( ! IS_BAD_PTR(KOS_get_walk_key(in_obj.o)));
            assert(GET_OBJ_TYPE(KOS_get_walk_key(in_obj.o)) == OBJ_STRING);

            if (type != OBJ_STRING) {

                char *buf;

                error = get_string(ctx, KOS_get_walk_key(in_obj.o), alloc, &buf);
                if ( ! error) {
                    KOS_raise_printf(ctx,
                                     "invalid type of environment variable '%s' passed to os.spawn(),"
                                     " it is %s, but expected string",
                                     buf,
                                     get_type_name(type));
                    error = KOS_ERROR_EXCEPTION;
                }
                goto cleanup;
            }

            TRY(KOS_set_property(ctx, obj.o, KOS_get_walk_key(in_obj.o), value_obj));

            ++est_num_env;
        }
        assert( ! KOS_is_exception_pending(ctx));
    }
    /* If not inheriting, just use the values passed to the call */
    else {
        obj.o = obj_id;

        in_obj.o = KOS_new_iterator(ctx, obj.o, KOS_SHALLOW);
        TRY_OBJID(in_obj.o);

        while ( ! KOS_iterator_next(ctx, in_obj.o))
            ++est_num_env;
    }

    /* Now convert the joined values to an array of strings */
    array = (char **)KOS_mempool_alloc(alloc, (est_num_env + 1) * sizeof(void *));
    if ( ! array) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    in_obj.o = KOS_new_iterator(ctx, obj.o, KOS_SHALLOW);
    TRY_OBJID(in_obj.o);

    out_ptr = array;

    while ( ! KOS_iterator_next(ctx, in_obj.o) && (est_num_env > 0)) {

        char          *buf;
        unsigned       key_len;
        unsigned       val_len;
        unsigned       buf_size;
        int            eq_pos   = 0;
        const KOS_TYPE val_type = GET_OBJ_TYPE(KOS_get_walk_value(in_obj.o));

        assert( ! IS_BAD_PTR(KOS_get_walk_key(in_obj.o)));
        assert(GET_OBJ_TYPE(KOS_get_walk_key(in_obj.o)) == OBJ_STRING);
        assert( ! IS_BAD_PTR(KOS_get_walk_value(in_obj.o)));

        TRY(KOS_string_find(ctx, KOS_get_walk_key(in_obj.o), KOS_CONST_ID(str_eq), KOS_FIND_FORWARD, &eq_pos));

        if (eq_pos != -1) {
            error = get_string(ctx, KOS_get_walk_key(in_obj.o), alloc, &buf);
            if ( ! error) {
                KOS_raise_printf(ctx, "invalid environment variable '%s' passed to os.spawn()", buf);
                error = KOS_ERROR_EXCEPTION;
            }
            goto cleanup;
        }

        if (val_type != OBJ_STRING) {
            error = get_string(ctx, KOS_get_walk_key(in_obj.o), alloc, &buf);
            if ( ! error) {
                KOS_raise_printf(ctx,
                                 "invalid type of environment variable '%s' passed to os.spawn(),"
                                 " it is %s, but expected string",
                                 buf,
                                 get_type_name(val_type));
                error = KOS_ERROR_EXCEPTION;
            }
            goto cleanup;
        }

        key_len  = KOS_string_to_utf8(KOS_get_walk_key(in_obj.o), KOS_NULL, 0);
        val_len  = KOS_string_to_utf8(KOS_get_walk_value(in_obj.o), KOS_NULL, 0);
        buf_size = key_len + val_len + 2;

        buf = (char *)KOS_mempool_alloc(alloc, buf_size);
        if ( ! buf) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            return KOS_ERROR_EXCEPTION;
        }

        KOS_string_to_utf8(KOS_get_walk_key(in_obj.o), buf, key_len);
        buf[key_len] = '=';
        KOS_string_to_utf8(KOS_get_walk_value(in_obj.o), buf + key_len + 1, val_len);
        buf[buf_size - 1] = 0;

        *(out_ptr++) = buf;

        --est_num_env;
    }

    *out_ptr   = KOS_NULL;
    *out_array = array;

cleanup:
    KOS_destroy_top_locals(ctx, &obj, &name);
    return error;
}

#ifdef _WIN32

static void release_pid(struct KOS_WAIT_S *wait_info)
{
}

#else

struct PID_ARRAY {
    uint32_t            capacity;
    KOS_ATOMIC(int32_t) num_pids;
    KOS_ATOMIC(void *)  pids[1];
};
typedef struct PID_ARRAY *PID_ARRAY_PTR;

static KOS_ATOMIC(PID_ARRAY_PTR) zombie_pids[4];
static KOS_ATOMIC(int32_t)       num_os_modules;
static struct PID_ARRAY          dummy_pids; /* Placeholder for signal handler */

union PID_TO_PTR {
    void *ptr;
    pid_t pid;
};

static pid_t to_pid(void *ptr)
{
    union PID_TO_PTR conv;

    conv.ptr = ptr;

    return conv.pid;
}

static pid_t check_pid(pid_t pid)
{
    int status;

    return waitpid(pid, &status, WNOHANG);
}

static void destroy_zombies(int sig)
{
    size_t array_idx;

    assert(sig == SIGCHLD);

    for (array_idx = 0; array_idx < sizeof(zombie_pids) / sizeof(zombie_pids[0]); array_idx++) {

        const PID_ARRAY_PTR pids = (PID_ARRAY_PTR)KOS_atomic_swap_ptr(zombie_pids[array_idx], &dummy_pids);

        if (pids) {

            uint32_t idx;

            for (idx = 0; idx < pids->capacity; idx++) {

                const pid_t pid = to_pid(KOS_atomic_swap_ptr(pids->pids[idx], (void *)KOS_NULL));

                if (pid > 0) {
                    KOS_atomic_add_i32(pids->num_pids, -1);

                    check_pid(pid);
                }
            }
        }

        if (pids != &dummy_pids)
            KOS_atomic_write_relaxed_ptr(zombie_pids[array_idx], pids);
    }
}

static KOS_ATOMIC(uint32_t) sig_child_installed;
static struct sigaction     old_sig_child;

static void handle_sig_child()
{
    if (KOS_atomic_cas_strong_u32(sig_child_installed, 0U, 1U)) {

        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = destroy_zombies;
        sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
        sigemptyset(&sa.sa_mask);

        if (sigaction(SIGCHLD, &sa, &old_sig_child))
            KOS_atomic_write_relaxed_u32(sig_child_installed, 0U);
    }
}

static int reserve_pid_slot(uint32_t array_idx, pid_t pid)
{
    const uint32_t new_capacity = array_idx ? 1024U : 32U;
    const size_t   new_size     = sizeof(struct PID_ARRAY) + (new_capacity - 1U) * sizeof(void *);
    void          *alloc_ptr    = KOS_malloc(new_size);
    int            error        = KOS_ERROR_OUT_OF_MEMORY;

    if (alloc_ptr) {

        const PID_ARRAY_PTR new_array = (PID_ARRAY_PTR)alloc_ptr;

        memset(alloc_ptr, 0, new_size);

        new_array->capacity = new_capacity;

        if (KOS_atomic_cas_strong_ptr(zombie_pids[array_idx], (PID_ARRAY_PTR)KOS_NULL, new_array))
            error = KOS_SUCCESS;
        else
            KOS_free(alloc_ptr);
    }

    return error;
}

static int append_to_pid_slot(PID_ARRAY_PTR pids, pid_t pid)
{
    union PID_TO_PTR conv;
    uint32_t         idx;
    int              error = KOS_ERROR_OUT_OF_MEMORY;

    if (KOS_atomic_read_relaxed_u32(*(KOS_ATOMIC(uint32_t) *)&pids->num_pids) == pids->capacity)
        return error;

    assert(sizeof(conv.ptr) >= sizeof(conv.pid));

    memset(&conv, 0, sizeof(conv));
    conv.pid = pid;

    for (idx = 0; idx < pids->capacity; idx++) {

        if (KOS_atomic_cas_strong_ptr(pids->pids[idx], (void *)KOS_NULL, conv.ptr)) {
            KOS_atomic_add_i32(pids->num_pids, 1);
            error = KOS_SUCCESS;
            break;
        }
    }

    return error;
}

/* If the wait object is being destroyed, try to wait on the child process to finish.
 * The waitpid() function clears the state of the child that has finished.  If the parent
 * process does not call waitpid(), a finished child will remain in a zombie state.
 * If the child is still running, we put the pid of the child on the zombie_pids list,
 * so that its state can get cleaned up later. */
static void release_pid(struct KOS_WAIT_S *wait_info)
{
    const pid_t ret_pid = check_pid(wait_info->pid);

    if (ret_pid == 0) {

        uint32_t array_idx;

        for (array_idx = 0; array_idx < sizeof(zombie_pids) / sizeof(zombie_pids[0]); array_idx++) {

            const PID_ARRAY_PTR pids = (PID_ARRAY_PTR)KOS_atomic_read_relaxed_ptr(zombie_pids[array_idx]);

            if (pids != &dummy_pids) {
                if ( ! pids) {
                    if ( ! reserve_pid_slot(array_idx, wait_info->pid))
                        break;
                }
                else {
                    if ( ! append_to_pid_slot(pids, wait_info->pid))
                        break;
                }
            }
        }

        handle_sig_child();
    }
}

static void cleanup_wait_list(void)
{
    /* If there are multiple instances, multiple os modules can be loaded.
     * Perform the cleanup only after the last os module is unloaded. */
    if (KOS_atomic_add_i32(num_os_modules, -1) == 1) {

        size_t array_idx;

        for (array_idx = 0; array_idx < sizeof(zombie_pids) / sizeof(zombie_pids[0]); array_idx++) {

            const PID_ARRAY_PTR pids = (PID_ARRAY_PTR)KOS_atomic_swap_ptr(zombie_pids[array_idx], &dummy_pids);

            if (pids != &dummy_pids) {
                KOS_free(pids);

                KOS_atomic_write_relaxed_ptr(zombie_pids[array_idx], (PID_ARRAY_PTR)KOS_NULL);
            }
        }

        if (KOS_atomic_read_relaxed_u32(sig_child_installed)) {
            sigaction(SIGCHLD, &old_sig_child, KOS_NULL);
            KOS_atomic_write_relaxed_u32(sig_child_installed, 0U);
        }
    }
}

#endif

static void wait_finalize(KOS_CONTEXT ctx,
                          void       *priv)
{
    if (priv) {
        release_pid((struct KOS_WAIT_S *)priv);

        KOS_free((struct KOS_WAIT_S *)priv);
    }
}

static KOS_OBJ_ID get_wait_proto(KOS_CONTEXT ctx)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID obj_id = KOS_get_module(ctx);

    TRY_OBJID(obj_id);
    assert(GET_OBJ_TYPE(obj_id) == OBJ_MODULE);

    obj_id = KOS_atomic_read_relaxed_obj(OBJPTR(MODULE, obj_id)->priv);
    if (IS_BAD_PTR(obj_id))
        obj_id = KOS_VOID;
    else {
        obj_id = KOS_array_read(ctx, obj_id, 0);
        TRY_OBJID(obj_id);
    }

cleanup:
    return error ? KOS_BADPTR : obj_id;
}

static KOS_OBJ_ID create_wait_object(KOS_CONTEXT ctx)
{
    KOS_OBJ_ID         obj_id;
    struct KOS_WAIT_S *wait_info;
    int                error = KOS_SUCCESS;

    obj_id = get_wait_proto(ctx);
    TRY_OBJID(obj_id);

    obj_id = KOS_new_object_with_prototype(ctx, obj_id);
    TRY_OBJID(obj_id);

    wait_info = (struct KOS_WAIT_S *)KOS_malloc(sizeof(struct KOS_WAIT_S));
    if ( ! wait_info) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    memset(wait_info, 0, sizeof(*wait_info));

    KOS_object_set_private_ptr(obj_id, wait_info);

    OBJPTR(OBJECT, obj_id)->finalize = wait_finalize;

cleanup:
    return error ? KOS_BADPTR : obj_id;
}

static int get_wait_info(KOS_CONTEXT         ctx,
                         KOS_OBJ_ID          obj_id,
                         struct KOS_WAIT_S **out_wait_info)
{
    KOS_LOCAL          obj;
    KOS_OBJ_ID         proto_id;
    struct KOS_WAIT_S *wait_info;
    int                error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &obj, obj_id);

    proto_id = get_wait_proto(ctx);
    TRY_OBJID(proto_id);

    if ( ! KOS_has_prototype(ctx, obj.o, proto_id))
        RAISE_EXCEPTION_STR(str_err_not_spawned);

    wait_info = (struct KOS_WAIT_S *)KOS_object_get_private_ptr(obj.o);

    if ( ! wait_info)
        RAISE_EXCEPTION_STR(str_err_not_spawned);

    *out_wait_info = wait_info;

cleanup:
    KOS_destroy_top_local(ctx, &obj);

    return error;
}

#ifndef _WIN32
static void send_errno_and_exit(int fd)
{
    const int     err_value = errno;
    const ssize_t num_writ  = write(fd, &err_value, sizeof(err_value));

    exit((num_writ == sizeof(err_value)) ? 1 : 2);
}

static void redirect_io(FILE *src_file,
                        int   target_fd,
                        int   status_fd)
{
    int src_fd;

    if ( ! src_file)
        return;

    src_fd = fileno(src_file);

    if (dup2(src_fd, target_fd) == -1)
        send_errno_and_exit(status_fd);

    if (fcntl(target_fd, F_SETFD, 0) == -1)
        send_errno_and_exit(status_fd);
}
#endif

/* @item os spawn()
 *
 *     spawn(program, args = [], env = {}, cwd = "", inherit_env = true,
 *           stdin = void, stdout = void, stderr = void)
 *
 * Spawns a new process.
 *
 * The arguments describe how the process will be spawned:
 *  * program        - Path to the program to start, or name of the program on PATH.
 *  * args           - (Optional) Array of arguments for the program.  If not specified,
 *                     an empty list of arguments is passed to the spawned program.
 *  * env            - (Optional) Object containing envionment variables for the spawned program.
 *                     The object is walked in a shallow manner to extract the environment.
 *                     If `inherit_env` is `true`, these are added on top of the current process's
 *                     environment.
 *  * cwd            - (Optional) Directory to start the program in.
 *  * inherit_env    - (Optional) If `true` the current process's environment is passed to
 *                     the spawned program together with environment variables from `env`.
 *                     Otherwise only environment variables from `env` are passed (if any).
 *                     Defaults to `true`.
 *  * stdin          - (Optional) File object or pipe open for reading or a string or buffer
 *                     which is fed into the spawned program on stdin.
 *  * stdout         - (Optional) File object or pipe open for writing.
 *  * stderr         - (Optional) File object or pipe open for writing.
 *
 * Returns a `process` object which can be used to obtain information about the spawned child process.
 */
static KOS_OBJ_ID spawn(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL            process;
    KOS_LOCAL            args;
    KOS_LOCAL            desc;
    struct KOS_MEMPOOL_S alloc;
    KOS_OBJ_ID           value_obj;
    KOS_OBJ_ID           inherit_env;
    KOS_OBJ_ID           file_obj;
    struct KOS_WAIT_S   *wait_info;
    char                *program_cstr      = KOS_NULL;
    char                *cwd               = KOS_NULL;
    char               **args_array        = KOS_NULL;
    char               **env_array         = KOS_NULL;
    FILE                *stdin_file        = KOS_NULL;
    FILE                *stdout_file       = KOS_NULL;
    FILE                *stderr_file       = KOS_NULL;
#ifndef _WIN32
    int                  exec_status_fd[2] = { -1, -1 };
#endif
    int                  error             = KOS_SUCCESS;

    assert(KOS_get_array_size(args_obj) >= 8);

    KOS_mempool_init(&alloc);
    KOS_init_local(     ctx, &process);
    KOS_init_local_with(ctx, &args, args_obj);
    KOS_init_local_with(ctx, &desc, KOS_array_read(ctx, args_obj, 0));

    /* Create return object which can be used to manage the child process */
    process.o = create_wait_object(ctx);
    TRY_OBJID(process.o);

    wait_info = (struct KOS_WAIT_S *)KOS_object_get_private_ptr(process.o);

    /* Get 'program' */
    value_obj = KOS_array_read(ctx, args.o, 0);
    TRY_OBJID(value_obj);
    TRY(check_arg_type(ctx, value_obj, "program", OBJ_STRING));

    TRY(get_string(ctx, value_obj, &alloc, &program_cstr));

    /* Get 'cwd' */
    value_obj = KOS_array_read(ctx, args.o, 3);
    TRY_OBJID(value_obj);
    TRY(check_arg_type(ctx, value_obj, "cwd", OBJ_STRING));

    TRY(get_string(ctx, value_obj, &alloc, &cwd));

    /* Get 'args' */
    value_obj = KOS_array_read(ctx, args.o, 1);
    TRY_OBJID(value_obj);
    TRY(check_arg_type(ctx, value_obj, "args", OBJ_ARRAY));

    TRY(get_args_array(ctx, value_obj, &alloc, &args_array));
    args_array[0] = program_cstr;

    /* Get 'inherit_env' */
    inherit_env = KOS_array_read(ctx, args.o, 4);
    TRY_OBJID(inherit_env);
    TRY(check_arg_type(ctx, inherit_env, "inherit_env", OBJ_BOOLEAN));

    /* Get 'env' */
    value_obj = KOS_array_read(ctx, args.o, 2);
    TRY_OBJID(value_obj);
    if (value_obj != KOS_VOID)
        TRY(check_arg_type(ctx, value_obj, "env", OBJ_OBJECT));

    TRY(get_env_array(ctx, value_obj, KOS_get_bool(inherit_env), &alloc, &env_array));

    /* Get 'stdin' */
    file_obj = KOS_array_read(ctx, args.o, 5);
    TRY_OBJID(file_obj);
    if (file_obj != KOS_VOID) {
        stdin_file = KOS_os_get_file(ctx, file_obj);
        if ( ! stdin_file)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    /* Get 'stdout' */
    file_obj = KOS_array_read(ctx, args.o, 6);
    TRY_OBJID(file_obj);
    if (file_obj != KOS_VOID) {
        stdout_file = KOS_os_get_file(ctx, file_obj);
        if ( ! stdout_file)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

    /* Get 'stderr' */
    file_obj = KOS_array_read(ctx, args.o, 7);
    TRY_OBJID(file_obj);
    if (file_obj != KOS_VOID) {
        stderr_file = KOS_os_get_file(ctx, file_obj);
        if ( ! stderr_file)
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }

#ifdef _WIN32
    KOS_raise_printf(ctx, "spawn not supported on Windows yet");
    RAISE_ERROR(KOS_ERROR_EXCEPTION);
#else
    {
        pid_t child_pid;
        int   err_value = 0;

        KOS_suspend_context(ctx);

        /* Create pipe for sending failure status of the child process */
        if ((pipe(exec_status_fd) != 0) || kos_seq_fail()) {
            err_value = errno;
            KOS_resume_context(ctx);
            KOS_raise_errno_value(ctx, "pipe creation failed", err_value);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        (void)fcntl(exec_status_fd[1], F_SETFD, FD_CLOEXEC);
        (void)fcntl(exec_status_fd[0], F_SETFD, FD_CLOEXEC);

        /* Create the child process */
        child_pid = fork();

        /* Handle failure of child process creation */
        if (child_pid == -1) {
            err_value = errno;
            KOS_resume_context(ctx);
            KOS_raise_errno_value(ctx, "fork failed", err_value);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        /* Inside child process, execute the program requested */
        if (child_pid == 0) {
            /* If we are here, it means the fork() has succeeded and we are in the child
             * process.  All fds should have been closed, except for those which did not have
             * the FD_CLOEXEC flag set.  Only this (current) thread has survived the fork()
             * and although all memory was copied, we cannot reliably resume the interpreter
             * or invoke any of its functions. */

            /* Explicitly close the read end of the status pipe */
            close(exec_status_fd[0]);

            /* Use redirected I/O */
            redirect_io(stdin_file,  STDIN_FILENO,  exec_status_fd[1]);
            redirect_io(stdout_file, STDOUT_FILENO, exec_status_fd[1]);
            redirect_io(stderr_file, STDERR_FILENO, exec_status_fd[1]);

            /* Execute the program in the child process */
            execve(program_cstr, args_array, env_array);

            /* If execve failed, send the error back to the Kos process */
            send_errno_and_exit(exec_status_fd[1]);
        }

        /* Close the write end of all pipes */
        close(exec_status_fd[1]);
        exec_status_fd[1] = -1;

        /* Check if there was any error in the child process */
        /* If the pipe read fails, it means that execve() was successful */
        if ((size_t)read(exec_status_fd[0], &err_value, sizeof(err_value)) != sizeof(err_value))
            err_value = 0;

        close(exec_status_fd[0]);
        exec_status_fd[0] = -1;

        if ( ! err_value)
            wait_info->pid = child_pid;

        KOS_resume_context(ctx);

        if (err_value) {
            KOS_raise_errno_value(ctx, "exec failed", err_value);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }
#endif

cleanup:
    process.o = KOS_destroy_top_locals(ctx, &desc, &process);
    KOS_mempool_destroy(&alloc);

    return error ? KOS_BADPTR : process.o;
}

/* @item os process()
 *
 *     process()
 *
 * Process class.
 *
 * This class cannot be directly instantiated.  The objects of this class are
 * returned from `os.spawn()`.
 *
 * Calling this class directly throws an exception.
 */
static KOS_OBJ_ID process_ctor(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    KOS_raise_exception(ctx, KOS_CONST_ID(str_err_use_spawn));
    return KOS_BADPTR;
}

/* @item os process.prototype.wait()
 *
 *     process.prototype.wait()
 *
 * Member of the process object returned by [os.spawn()](#osspawn).
 *
 * Waits for the process to finish.
 *
 * If the wait succeeded, returns a status object, containing the following properties:
 *
 *  * status    Exit code of the process.  If the process exited with a signal or stopped,
 *              it is 128 plus signal number.
 *  * signal    If the process exited with a signal or stopped, contains then number of
 *              the signal, otherwise contains `void`.
 *  * stopped   If the process was stopped by a signal, contains `true`, otherwise if the
 *              process exited (with or without a signal) contains `false`.
 *
 * If the wait failed, e.g. if it was already called and the process was not stopped,
 * this function throws an exception.
 *
 * This function will return in three following situations:
 *
 *  # The process exits normally, in which case the `status` property of the returned object
 *    contains the exit code.
 *  # The process exits via a signal (e.g. crashes), in which case the `status` property is
 *    128 + the number of the signal and the `signal` property is the signal number.
 *  # The process is stopped, in which case the `stopped` property is set to `true`.  In this
 *    case the `wait()` function can be called again to wait for the process to finish.
 */
static KOS_OBJ_ID wait_for_child(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  this_obj,
                                 KOS_OBJ_ID  args_obj)
{
    struct KOS_WAIT_S *wait_info;
    KOS_LOCAL          ret;
    int                error = KOS_SUCCESS;

    KOS_init_local(ctx, &ret);

    TRY(get_wait_info(ctx, this_obj, &wait_info));

    ret.o = KOS_new_object(ctx);
    TRY_OBJID(ret.o);

#ifndef _WIN32
    {
        pid_t ret_pid;
        int   status;
        int   stored_errno = 0;

        KOS_suspend_context(ctx);

        ret_pid = waitpid(wait_info->pid, &status, 0);

        if (ret_pid == -1)
            stored_errno = errno;

        KOS_resume_context(ctx);

        if (ret_pid == -1) {
            KOS_raise_errno_value(ctx, "wait failed", stored_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        assert(ret_pid == wait_info->pid);

        if (WIFEXITED(status)) {
            const uint8_t exit_code = (uint8_t)WEXITSTATUS(status);

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_status), TO_SMALL_INT(exit_code)));

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_signal), KOS_VOID));

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_stopped), KOS_FALSE));
        }
        else {
            int        sign;
            KOS_OBJ_ID stopped;
            KOS_OBJ_ID value;

            if (WIFSIGNALED(status)) {
                sign    = (int)WTERMSIG(status);
                stopped = KOS_FALSE;
            }
            else {
                assert(WIFSTOPPED(status));
                sign    = (int)WSTOPSIG(status);
                stopped = KOS_TRUE;
            }

            value = KOS_new_int(ctx, (int64_t)sign);
            TRY_OBJID(value);

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_signal), value));

            /* Exit code is 128 + signal */
            value = KOS_new_int(ctx, 128 + (int64_t)sign);
            TRY_OBJID(value);

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_status), value));

            TRY(KOS_set_property(ctx, ret.o, KOS_CONST_ID(str_stopped), stopped));
        }
    }
#endif

cleanup:
    ret.o = KOS_destroy_top_local(ctx, &ret);

    return error ? KOS_BADPTR : ret.o;
}

/* @item os process.prototype.pid
 *
 *     process.prototype.pid()
 *
 * Member of the process object returned by [os.spawn()](#osspawn).
 *
 * The pid of the spawned process.
 */
static KOS_OBJ_ID get_pid(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    KOS_OBJ_ID         pid   = KOS_VOID;
    struct KOS_WAIT_S *wait_info;
    int                error = KOS_SUCCESS;

    TRY(get_wait_info(ctx, this_obj, &wait_info));

    pid = KOS_new_int(ctx, (int64_t)wait_info->pid);
    TRY_OBJID(pid);

cleanup:
    return error ? KOS_BADPTR : pid;
}

/* @item os getenv()
 *
 *     getenv(key, default_value = void)
 *
 * Returns contents of an environment variable.
 *
 * If the environment variable does not exist, returns the `default_value` value.
 *
 * Example:
 *
 *      > getenv("PATH")
 *      "/usr/bin:/bin:/usr/sbin:/sbin"
 */
const KOS_ARG_DESC getenv_args[3] = {
    { KOS_CONST_ID(str_key),           KOS_BADPTR },
    { KOS_CONST_ID(str_default_value), KOS_VOID   },
    { KOS_BADPTR,                      KOS_BADPTR }
};
static KOS_OBJ_ID kos_getenv(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  this_obj,
                             KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR  cstr;
    KOS_OBJ_ID  obj = KOS_BADPTR;
    const char *env_var;
    int         error;

    assert(KOS_get_array_size(args_obj) >= 2);

    KOS_vector_init(&cstr);

    obj = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(obj);

    TRY(KOS_string_to_cstr_vec(ctx, obj, &cstr));

    env_var = getenv(cstr.buffer);

    if (env_var) {

        const size_t len = strlen(env_var);

        obj = KOS_new_string(ctx, env_var, len);
        TRY_OBJID(obj);
    }
    else {
        obj = KOS_array_read(ctx, args_obj, 1);
        TRY_OBJID(obj);
    }

cleanup:
    KOS_vector_destroy(&cstr);

    return error ? KOS_BADPTR : obj;
}

KOS_INIT_MODULE(os)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL priv;
    KOS_LOCAL wait_proto;
    KOS_LOCAL wait_func;

    const KOS_ARG_DESC spawn_args[11] = {
        { KOS_CONST_ID(str_program),     KOS_BADPTR      },
        { KOS_CONST_ID(str_args),        KOS_EMPTY_ARRAY },
        { KOS_CONST_ID(str_env),         KOS_VOID        },
        { KOS_CONST_ID(str_cwd),         KOS_STR_EMPTY   },
        { KOS_CONST_ID(str_inherit_env), KOS_TRUE        },
        { KOS_CONST_ID(str_stdin),       KOS_VOID        },
        { KOS_CONST_ID(str_stdout),      KOS_VOID        },
        { KOS_CONST_ID(str_stderr),      KOS_VOID        },
        { KOS_BADPTR,                    KOS_BADPTR      }
    };

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_locals(ctx, 3, &wait_func, &priv, &wait_proto);

#ifndef _WIN32
    OBJPTR(MODULE, module_obj)->finalize = cleanup_wait_list;
#endif

    priv.o = KOS_new_array(ctx, 1);
    TRY_OBJID(priv.o);

    KOS_atomic_write_relaxed_ptr(OBJPTR(MODULE, module.o)->priv, priv.o);

    TRY_ADD_FUNCTION(       ctx, module.o,               "spawn",   spawn,          spawn_args);
    TRY_ADD_FUNCTION(       ctx, module.o,               "getenv",  kos_getenv,     getenv_args);

    TRY_ADD_CONSTRUCTOR(    ctx, module.o,               "process", process_ctor,   KOS_NULL, &wait_proto.o);
    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, wait_proto.o, "wait",    wait_for_child, KOS_NULL);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, wait_proto.o, "pid",     get_pid,        0);

    /* @item os sysname
     *
     *     sysname
     *
     * Constant string representing Operating System's name where Kos is running.
     *
     * Example:
     *
     *     > sysname
     *     "Linux"
     */
    TRY_ADD_STRING_CONSTANT(ctx, module.o, "sysname", KOS_SYSNAME);

    TRY(KOS_array_write(ctx, priv.o, 0, wait_proto.o));

#ifndef _WIN32
    KOS_atomic_add_i32(num_os_modules, 1);
#endif

cleanup:
    KOS_destroy_top_locals(ctx, &wait_func, &module);

    return error;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2021 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_const_strings.h"
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
#include <string.h>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#else
#   include <errno.h>
#   include <fcntl.h>
#   include <stdlib.h>
#   include <sys/wait.h>
#   include <unistd.h>
#endif

KOS_DECLARE_STATIC_CONST_STRING(str_args,               "args");
KOS_DECLARE_STATIC_CONST_STRING(str_cwd,                "cwd");
KOS_DECLARE_STATIC_CONST_STRING(str_env,                "env");
KOS_DECLARE_STATIC_CONST_STRING(str_eq,                 "=");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_string, "invalid string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_spawned,    "object is not a spawned process");
KOS_DECLARE_STATIC_CONST_STRING(str_inherit_env,        "inherit_env");
KOS_DECLARE_STATIC_CONST_STRING(str_program,            "program");
KOS_DECLARE_STATIC_CONST_STRING(str_signal,             "signal");
KOS_DECLARE_STATIC_CONST_STRING(str_status,             "status");
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

static KOS_OBJ_ID get_opt_property(KOS_CONTEXT      ctx,
                                   KOS_OBJ_ID       obj_id,
                                   KOS_OBJ_ID       prop_id,
                                   enum KOS_DEPTH_E shallow,
                                   KOS_TYPE         expect,
                                   KOS_OBJ_ID       default_id,
                                   const char      *where)
{
    KOS_OBJ_ID value_id = KOS_get_property_with_depth(ctx, obj_id, prop_id, shallow);

    assert(expect <= OBJ_LAST_TYPE);
    assert(expect > OBJ_SMALL_INTEGER);

    if (IS_BAD_PTR(value_id)) {
        KOS_clear_exception(ctx);

        if ( ! IS_BAD_PTR(default_id))
            value_id = default_id;
        else {

            KOS_VECTOR prop_cstr;

            KOS_vector_init(&prop_cstr);

            if (KOS_string_to_cstr_vec(ctx, prop_id, &prop_cstr) == KOS_SUCCESS)
                KOS_raise_printf(ctx, "missing '%s' property in %s", prop_cstr.buffer, where);

            KOS_vector_destroy(&prop_cstr);
        }
    }
    else {
        const KOS_TYPE actual_type = GET_OBJ_TYPE(value_id);

        if (actual_type != expect) {

            const char *type_name = KOS_NULL;

            if (expect == OBJ_FLOAT) {
                if (actual_type > expect)
                    type_name = "number";
            }
            else
                type_name = get_type_name(expect);

            if (type_name) {

                KOS_VECTOR prop_cstr;

                KOS_vector_init(&prop_cstr);

                if (KOS_string_to_cstr_vec(ctx, prop_id, &prop_cstr) == KOS_SUCCESS)
                    KOS_raise_printf(ctx, "'%s' property in %s is a %s, but expected %s",
                                     prop_cstr.buffer, where, get_type_name(actual_type),
                                     type_name);

                KOS_vector_destroy(&prop_cstr);

                value_id = KOS_BADPTR;
            }
        }
    }

    return value_id;
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
        int            eq_pos   = -1;
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

static void wait_finalize(KOS_CONTEXT ctx,
                          void       *priv)
{
    if (priv)
        KOS_free((struct KOS_WAIT_S *)priv);
}

static KOS_OBJ_ID get_wait_proto(KOS_CONTEXT ctx)
{
    int        error  = KOS_SUCCESS;
    KOS_OBJ_ID obj_id = KOS_get_module(ctx);

    TRY_OBJID(obj_id);
    assert(GET_OBJ_TYPE(obj_id) == OBJ_MODULE);

    obj_id = KOS_atomic_read_relaxed_obj(OBJPTR(MODULE, obj_id)->priv);
    if (obj_id != KOS_BADPTR) {
        obj_id = KOS_array_read(ctx, obj_id, 0);
        TRY_OBJID(obj_id);
    }
    else
        obj_id = KOS_VOID;

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

/* @item os os.spawn()
 *
 *     os.spawn(spawn_desc)
 *
 * Spawns a new process described by `spawn_desc`.
 *
 * `spawn_desc` is an object containing the following properties:
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
 *  * capture_stdout - (Optional) If `true`, stdout is captured into a string.
 *  * capture_stderr - (Optional) If `true`, stderr is captured into a string.
 *  * stdin          - (Optional) File object open for reading or a string or buffer
 *                     which is fed into the spawned program on stdin.
 *  * stdout         - (Optional) File object open for writing.
 *  * stderr         - (Optional) File object open for writing.
 *
 * Returns a `process` object which can be used to obtain information about the spawned child process.
 * The process object contains the following fields:
 *
 *  * [pid](#processpid)     The pid of the spawned child process.
 *  * [wait()](#processwait) The wait function, which can be used to wait for the process to finish.
 */
static KOS_OBJ_ID spawn(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int                  error = KOS_SUCCESS;
    KOS_LOCAL            process;
    KOS_LOCAL            desc;
    struct KOS_MEMPOOL_S alloc;
    KOS_OBJ_ID           value_obj;
    KOS_OBJ_ID           inherit_env;
    struct KOS_WAIT_S   *wait_info;
    char                *program_cstr = KOS_NULL;
    char                *cwd          = KOS_NULL;
    char               **args_array   = KOS_NULL;
    char               **env_array    = KOS_NULL;

    static const char arg_desc[] = "object passed to os.spawn()";

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_mempool_init(&alloc);
    KOS_init_local(ctx, &process);
    KOS_init_local_with(ctx, &desc, KOS_array_read(ctx, args_obj, 0));

    /* Create return object which can be used to manage the child process */
    process.o = create_wait_object(ctx);
    TRY_OBJID(process.o);

    wait_info = (struct KOS_WAIT_S *)KOS_object_get_private_ptr(process.o);

    /* Get 'program' */
    value_obj = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_program), KOS_DEEP, OBJ_STRING,
                                 KOS_BADPTR, arg_desc);
    TRY_OBJID(value_obj);

    TRY(get_string(ctx, value_obj, &alloc, &program_cstr));

    /* Get 'cwd' */
    value_obj = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_cwd), KOS_DEEP, OBJ_STRING,
                                 KOS_STR_EMPTY, arg_desc);
    TRY_OBJID(value_obj);

    TRY(get_string(ctx, value_obj, &alloc, &cwd));

    /* Get 'args' */
    value_obj = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_args), KOS_DEEP, OBJ_ARRAY,
                                 KOS_EMPTY_ARRAY, arg_desc);
    TRY_OBJID(value_obj);

    TRY(get_args_array(ctx, value_obj, &alloc, &args_array));
    args_array[0] = program_cstr;

    /* Get 'inherit_env' */
    inherit_env = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_inherit_env), KOS_DEEP, OBJ_BOOLEAN,
                                   KOS_TRUE, arg_desc);
    TRY_OBJID(inherit_env);
    assert((inherit_env == KOS_TRUE) || (inherit_env == KOS_FALSE));

    /* Get 'env' */
    value_obj = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_env), KOS_DEEP, OBJ_OBJECT,
                                 KOS_VOID, arg_desc);
    TRY_OBJID(value_obj);

    TRY(get_env_array(ctx, value_obj, KOS_get_bool(inherit_env), &alloc, &env_array));

#ifdef _WIN32
    KOS_raise_printf(ctx, "spawn not supported on Windows yet");
    RAISE_ERROR(KOS_ERROR_EXCEPTION);
#else
    {
        pid_t child_pid;
        int   exec_status_fd[2];
        int   err_value = 0;

        KOS_suspend_context(ctx);

        if ((pipe(exec_status_fd) != 0) || kos_seq_fail()) {
            err_value = errno;
            KOS_resume_context(ctx);
            KOS_raise_errno_value(ctx, "pipe creation failed", err_value);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        fcntl(exec_status_fd[1], F_SETFD, FD_CLOEXEC);
        fcntl(exec_status_fd[0], F_SETFD, FD_CLOEXEC);

        child_pid = fork();

        if (child_pid == -1) {
            err_value = errno;
            KOS_resume_context(ctx);
            KOS_raise_errno_value(ctx, "fork failed", err_value);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if (child_pid == 0) {
            int exit_code;

            close(exec_status_fd[0]);

            execve(program_cstr, args_array, env_array);

            err_value = errno;
            exit_code = (write(exec_status_fd[1], &err_value, sizeof(err_value)) == sizeof(err_value)) ? 1 : 2;
            exit(exit_code);
        }

        close(exec_status_fd[1]);

        if ((size_t)read(exec_status_fd[0], &err_value, sizeof(err_value)) != sizeof(err_value))
            err_value = 0;

        close(exec_status_fd[0]);

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

/* @item os process.wait()
 *
 *     process.wait()
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
    for (;;) {
        pid_t ret_pid;
        int   status;
        int   stored_errno = 0;

        KOS_suspend_context(ctx);

        ret_pid = wait(&status);

        if (ret_pid == -1)
            stored_errno = errno;

        KOS_resume_context(ctx);

        if (ret_pid == -1) {
            KOS_raise_errno_value(ctx, "wait failed", stored_errno);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        if (ret_pid != wait_info->pid)
            continue;

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

        break;
    }
#endif

cleanup:
    ret.o = KOS_destroy_top_local(ctx, &ret);

    return error ? KOS_BADPTR : ret.o;
}

/* @item os process.pid
 *
 *     process.pid()
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

KOS_INIT_MODULE(os)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL priv;
    KOS_LOCAL wait_proto;
    KOS_LOCAL wait_func;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_locals(ctx, 3, &wait_func, &priv, &wait_proto);

    priv.o = KOS_new_array(ctx, 1);
    TRY_OBJID(priv.o);

    KOS_atomic_write_relaxed_ptr(OBJPTR(MODULE, module.o)->priv, priv.o);

    wait_proto.o = KOS_new_object(ctx);
    TRY_OBJID(wait_proto.o);

    TRY(KOS_array_write(ctx, priv.o, 0, wait_proto.o));

    TRY_ADD_FUNCTION(       ctx, module.o,               "spawn", spawn,          1);

    TRY_ADD_MEMBER_FUNCTION(ctx, module.o, wait_proto.o, "wait",  wait_for_child, 0);
    TRY_ADD_MEMBER_PROPERTY(ctx, module.o, wait_proto.o, "pid",   get_pid,        0);

cleanup:
    KOS_destroy_top_locals(ctx, &wait_func, &module);

    return error;
}

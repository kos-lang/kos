/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../core/kos_try.h"

KOS_DECLARE_STATIC_CONST_STRING(str_program, "program");

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
                KOS_raise_printf(ctx, "missing \"%s\" property in %s", prop_cstr.buffer, where);

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
                    KOS_raise_printf(ctx, "\"%s\" property in %s is a %s but expected %s",
                                     prop_cstr.buffer, where, get_type_name(actual_type),
                                     type_name);

                KOS_vector_destroy(&prop_cstr);
            }
        }
    }

    return value_id;
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
 *  * env            - (Optional) Envionment variables.  If `inherit_env` is `true`, these
 *                     are added on top of the current process's environment.
 *  * cwd            - (Optional) Directory to start the program in.
 *  * inherit_env    - (Optional) If `true` the current process's environment is passed to
 *                     the spawned program together with environment variables from `env`.
 *                     Otherwise only environment variables from `env` are passed (if any).
 *  * capture_stdout - (Optional) If `true`, stdout is captured into a string.
 *  * capture_stderr - (Optional) If `true`, stderr is captured into a string.
 *  * stdin          - (Optional) File object open for reading or a string or buffer
 *                     which is fed into the spawned program on stdin.
 *  * stdout         - (Optional) File object open for writing.
 *  * stderr         - (Optional) File object open for writing.
 */
static KOS_OBJ_ID spawn(KOS_CONTEXT ctx,
                        KOS_OBJ_ID  this_obj,
                        KOS_OBJ_ID  args_obj)
{
    int        error = KOS_SUCCESS;
    KOS_LOCAL  desc;
    KOS_VECTOR program_cstr;
    KOS_OBJ_ID value_obj;

    static const char arg_desc[] = "object passed to os.spawn()";

    assert(KOS_get_array_size(args_obj) > 0);

    KOS_vector_init(&program_cstr);
    KOS_init_local_with(ctx, &desc, KOS_array_read(ctx, args_obj, 0));

    value_obj = get_opt_property(ctx, desc.o, KOS_CONST_ID(str_program), KOS_DEEP, OBJ_STRING,
                                 KOS_BADPTR, arg_desc);
    TRY_OBJID(value_obj);

#ifdef _WIN32
    KOS_raise_printf(ctx, "spawn not supported on Windows yet");
    goto cleanup;
#endif

cleanup:
    KOS_destroy_top_local(ctx, &desc);
    KOS_vector_destroy(&program_cstr);

    return error ? KOS_BADPTR : KOS_VOID;
}

KOS_INIT_MODULE(os)(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;

    KOS_init_local_with(ctx, &module, module_obj);

    TRY_ADD_FUNCTION(ctx, module.o, "spawn", spawn, 1);

cleanup:
    KOS_destroy_top_local(ctx, &module);

    return error;
}

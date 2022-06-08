/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2022 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_malloc.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "../inc/kos_version.h"
#include "../core/kos_heap.h"
#include "../core/kos_lexer.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_try.h"
#include <string.h>

KOS_DECLARE_STATIC_CONST_STRING(str_base,                     "base");
KOS_DECLARE_STATIC_CONST_STRING(str_column,                   "column");
KOS_DECLARE_STATIC_CONST_STRING(str_err_bad_ignore_errors,    "`ignore_errors` argument is not a boolean");
KOS_DECLARE_STATIC_CONST_STRING(str_err_gen_line_not_string,  "data from generator fed to lexer is not a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_arg,          "invalid argument");
KOS_DECLARE_STATIC_CONST_STRING(str_err_invalid_string,       "invalid string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_name_not_string,      "'name' argument is not a string");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_paren,            "previous token was not ')'");
KOS_DECLARE_STATIC_CONST_STRING(str_err_script_not_buffer,    "'script' argument object is not a buffer");
KOS_DECLARE_STATIC_CONST_STRING(str_err_script_not_generator, "'script' argument object is a function but not a generator");
KOS_DECLARE_STATIC_CONST_STRING(str_keyword,                  "keyword");
KOS_DECLARE_STATIC_CONST_STRING(str_line,                     "line");
KOS_DECLARE_STATIC_CONST_STRING(str_lines,                    "lines");
KOS_DECLARE_STATIC_CONST_STRING(str_name,                     "name");
KOS_DECLARE_STATIC_CONST_STRING(str_op,                       "op");
KOS_DECLARE_STATIC_CONST_STRING(str_script,                   "script");
KOS_DECLARE_STATIC_CONST_STRING(str_sep,                      "sep");
KOS_DECLARE_STATIC_CONST_STRING(str_token,                    "token");
KOS_DECLARE_STATIC_CONST_STRING(str_type,                     "type");
KOS_DECLARE_STATIC_CONST_STRING(str_version,                  "version");

typedef struct KOS_LEXER_OBJ_S {
    KOS_LEXER lexer;
    KOS_TOKEN token;
    char     *own_buf;
    uint32_t  own_buf_size;
    uint8_t   ignore_errors;
    uint8_t   from_generator;
    char      buf[1];
} KOS_LEXER_OBJ;

static void finalize(KOS_CONTEXT ctx,
                     void       *priv)
{
    if (priv) {
        KOS_LEXER_OBJ *const lexer = (KOS_LEXER_OBJ *)priv;

        if (lexer->own_buf)
            KOS_free(lexer->own_buf);

        KOS_free(priv);
    }
}

KOS_DECLARE_PRIVATE_CLASS(lexer_priv_class);

static const KOS_CONVERT raw_lexer_args[2] = {
    KOS_DEFINE_MANDATORY_ARG(str_script),
    KOS_DEFINE_TAIL_ARG()
};

/* @item kos raw_lexer()
 *
 *     raw_lexer(script, ignore_errors = false)
 *
 * Raw Kos lexer generator.
 *
 * `script` is a string, buffer or generator containing Kos script to parse.
 *
 * If `script` is a generator, it must produce strings.  Each of these strings will be
 * subsequently parsed for consecutive tokens.  The occurrence of EOL characters is used
 * to signify ends of lines.  Tokens cannot span across subsequent strings.
 *
 * `ignore_errors` is a boolean specifying whether any errors that occur should be ignored
 * or not.  If `ignore_errors` is `true`, any invalid characters will be returned as whitespace.
 * If `ignore_errors` is `false`, which is the default, lexing error will trigger an exception.
 *
 * See `lexer` for documentation of the generator's output.
 *
 * The instantiated generator takes a single optional argument, which is an integer 0 or 1
 * (defaults to 0 if no argument is given).  When set to 1, this optional argument signifies
 * that the lexer should parse a string continuation for an interpolated string, i.e. the part
 * of the interpolated string starting with a closed parenthesis.  1 can only be specified
 * after a closing parenthesis was encountered, otherwise the lexer throws an exception.
 *
 * The drawback of using the raw lexer is that string continuations must be handled manually.
 * The benefit of using the raw lexer is that it can be used for parsing non-Kos scripts,
 * including C source code.
 */
static KOS_OBJ_ID raw_lexer(KOS_CONTEXT ctx,
                            KOS_OBJ_ID  regs_obj,
                            KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL           regs;
    KOS_LOCAL           args;
    KOS_LOCAL           lexer;
    KOS_LOCAL           init;
    KOS_LOCAL           value;
    KOS_LOCAL           token;
    KOS_LEXER_OBJ      *kos_lexer;
    KOS_NEXT_TOKEN_MODE next_token = NT_ANY;
    int                 error      = KOS_SUCCESS;

    assert(GET_OBJ_TYPE(regs_obj) == OBJ_ARRAY);

    KOS_init_locals(ctx, &regs, &args, &lexer, &init, &value, &token, kos_end_locals);

    regs.o = regs_obj;
    args.o = args_obj;

    lexer.o = KOS_array_read(ctx, regs.o, 0);
    assert( ! IS_BAD_PTR(lexer.o));
    TRY_OBJID(lexer.o);

    /* TODO improve passing args for the first time to built-in generators */

    /* Instantiate the lexer on first invocation */
    if ( ! KOS_object_get_private(lexer.o, &lexer_priv_class)) {

        uint32_t buf_size;

        init.o = lexer.o;

        lexer.o = KOS_new_object_with_private(ctx, KOS_VOID, &lexer_priv_class, finalize);
        TRY_OBJID(lexer.o);

        switch (GET_OBJ_TYPE(init.o)) {

            case OBJ_STRING:
                buf_size = KOS_string_to_utf8(init.o, KOS_NULL, 0);
                if (buf_size == ~0U)
                    RAISE_EXCEPTION_STR(str_err_invalid_string);
                break;

            case OBJ_BUFFER:
                buf_size = KOS_get_buffer_size(init.o);
                break;

            case OBJ_FUNCTION: {
                KOS_FUNCTION_STATE state;

                if ( ! KOS_is_generator(init.o, &state))
                    RAISE_EXCEPTION_STR(str_err_script_not_generator);

                buf_size = 0;
                break;
            }

            default:
                RAISE_EXCEPTION_STR(str_err_script_not_buffer);
        }

        kos_lexer = (KOS_LEXER_OBJ *)KOS_malloc(sizeof(KOS_LEXER_OBJ) + buf_size - 1);
        if ( ! kos_lexer) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        KOS_object_set_private_ptr(lexer.o, kos_lexer);

        switch (GET_OBJ_TYPE(init.o)) {

            case OBJ_BUFFER:
                memcpy(&kos_lexer->buf[0], KOS_buffer_data_const(init.o), buf_size);
                kos_lexer->from_generator = 0;
                break;

            case OBJ_STRING:
                KOS_string_to_utf8(init.o, &kos_lexer->buf[0], buf_size);
                kos_lexer->from_generator = 0;
                break;

            default:
                assert(GET_OBJ_TYPE(init.o) == OBJ_FUNCTION);
                kos_lexer->from_generator = 1;
                TRY(KOS_set_property(ctx, lexer.o, KOS_CONST_ID(str_lines), init.o));
                break;
        }

        kos_lexer_init(&kos_lexer->lexer, 0, &kos_lexer->buf[0], &kos_lexer->buf[buf_size]);

        kos_lexer->ignore_errors = 0;
        kos_lexer->own_buf       = KOS_NULL;
        kos_lexer->own_buf_size  = 0;

        if (KOS_get_array_size(regs.o) > 1) {
            KOS_OBJ_ID ignore_errors = KOS_array_read(ctx, regs.o, 1);

            TRY_OBJID(ignore_errors);

            if (GET_OBJ_TYPE(ignore_errors) != OBJ_BOOLEAN)
                RAISE_EXCEPTION_STR(str_err_bad_ignore_errors);

            kos_lexer->ignore_errors = (ignore_errors == KOS_TRUE) ? 1 : 0;
        }

        TRY(KOS_array_resize(ctx, regs.o, 2));

        TRY(KOS_array_write(ctx, regs.o, 0, lexer.o));
    }
    else {
        assert(GET_OBJ_TYPE(lexer.o) == OBJ_OBJECT);

        kos_lexer = (KOS_LEXER_OBJ *)KOS_object_get_private(lexer.o, &lexer_priv_class);

        if (KOS_get_array_size(args.o) > 0) {

            int64_t    i_value;
            KOS_OBJ_ID arg = KOS_array_read(ctx, args.o, 0);

            TRY_OBJID(arg);

            TRY(KOS_get_integer(ctx, arg, &i_value));

            if (i_value != 0 && i_value != 1)
                RAISE_EXCEPTION_STR(str_err_invalid_arg);

            if (i_value) {
                next_token = NT_CONTINUE_STRING;

                if (kos_lexer->token.sep != ST_PAREN_CLOSE)
                    RAISE_EXCEPTION_STR(str_err_not_paren);

                kos_lexer_unget_token(&kos_lexer->lexer, &kos_lexer->token);
            }
        }
    }

    assert( ! kos_lexer->lexer.error_str);

    init.o = KOS_BADPTR;

    for (;;) {
        char    *own_buf  = kos_lexer->own_buf;
        uint32_t buf_size = 0;

        error = kos_lexer_next_token(&kos_lexer->lexer, next_token, &kos_lexer->token);

        if (error || (kos_lexer->token.type != TT_EOF) || ! kos_lexer->from_generator)
            break;

        if (IS_BAD_PTR(init.o)) {
            init.o = KOS_get_property(ctx, lexer.o, KOS_CONST_ID(str_lines));
            TRY_OBJID(init.o);
        }

        value.o = KOS_call_generator(ctx, init.o, KOS_VOID, KOS_EMPTY_ARRAY);
        if (IS_BAD_PTR(value.o)) { /* end of iterator */
            if (KOS_is_exception_pending(ctx))
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            break;
        }

        if (GET_OBJ_TYPE(value.o) != OBJ_STRING)
            RAISE_EXCEPTION_STR(str_err_gen_line_not_string);

        buf_size = KOS_string_to_utf8(value.o, KOS_NULL, 0);

        if (buf_size > kos_lexer->own_buf_size) {
            own_buf = (char *)KOS_realloc(own_buf, buf_size);

            if ( ! own_buf) {
                KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }

            kos_lexer->own_buf      = own_buf;
            kos_lexer->own_buf_size = buf_size;
        }

        KOS_string_to_utf8(value.o, own_buf, buf_size);
        kos_lexer_update(&kos_lexer->lexer, own_buf, own_buf + buf_size);
    }

    if (error) {
        if (kos_lexer->ignore_errors) {

            KOS_TOKEN *cur_token = &kos_lexer->token;

            kos_lexer->lexer.error_str = KOS_NULL;

            cur_token->type    = TT_WHITESPACE;
            cur_token->keyword = KW_NONE;
            cur_token->op      = OT_NONE;
            cur_token->sep     = ST_NONE;

            kos_lexer->lexer.pos.column += cur_token->length;
        }
        else {
            KOS_raise_printf(ctx,
                             "parse error %u:%u: %s",
                             kos_lexer->lexer.pos.line,
                             kos_lexer->lexer.pos.column,
                             kos_lexer->lexer.error_str);
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    if (kos_lexer->token.type != TT_EOF) {

        KOS_TOKEN *const cur_token = &kos_lexer->token;

        token.o = KOS_new_object(ctx);
        TRY_OBJID(token.o);

        value.o = KOS_new_string(ctx, cur_token->begin, cur_token->length);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_token), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->line);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_line), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->column);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_column), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->type);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_type), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->keyword);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_keyword), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->op);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_op), value.o));

        value.o = KOS_new_int(ctx, (int64_t)cur_token->sep);
        TRY_OBJID(value.o);
        TRY(KOS_set_property(ctx, token.o, KOS_CONST_ID(str_sep), value.o));

        if (kos_lexer->token.type == TT_STRING || kos_lexer->token.type == TT_STRING_OPEN) {
            /* TODO parse string, raw/non-raw */
        }
    }

cleanup:
    token.o = KOS_destroy_top_locals(ctx, &regs, &token);

    return error ? KOS_BADPTR : token.o;
}

KOS_DECLARE_STATIC_CONST_STRING(str_num_objs_evacuated,     "num_objs_evacuated");
KOS_DECLARE_STATIC_CONST_STRING(str_num_objs_freed,         "num_objs_freed");
KOS_DECLARE_STATIC_CONST_STRING(str_num_objs_finalized,     "num_objs_finalized");
KOS_DECLARE_STATIC_CONST_STRING(str_num_pages_kept,         "num_pages_kept");
KOS_DECLARE_STATIC_CONST_STRING(str_num_pages_dropped,      "num_pages_dropped");
KOS_DECLARE_STATIC_CONST_STRING(str_num_pages_freed,        "num_pages_freed");
KOS_DECLARE_STATIC_CONST_STRING(str_size_evacuated,         "size_evacuated");
KOS_DECLARE_STATIC_CONST_STRING(str_size_freed,             "size_freed");
KOS_DECLARE_STATIC_CONST_STRING(str_size_kept,              "size_kept");
KOS_DECLARE_STATIC_CONST_STRING(str_initial_heap_size,      "initial_heap_size");
KOS_DECLARE_STATIC_CONST_STRING(str_initial_used_heap_size, "initial_used_heap_size");
KOS_DECLARE_STATIC_CONST_STRING(str_initial_malloc_size,    "initial_malloc_size");
KOS_DECLARE_STATIC_CONST_STRING(str_heap_size,              "heap_size");
KOS_DECLARE_STATIC_CONST_STRING(str_used_heap_size,         "used_heap_size");
KOS_DECLARE_STATIC_CONST_STRING(str_malloc_size,            "malloc_size");
KOS_DECLARE_STATIC_CONST_STRING(str_time_stop_us,           "time_stop_us");
KOS_DECLARE_STATIC_CONST_STRING(str_time_mark_us,           "time_mark_us");
KOS_DECLARE_STATIC_CONST_STRING(str_time_evac_us,           "time_evac_us");
KOS_DECLARE_STATIC_CONST_STRING(str_time_update_us,         "time_update_us");
KOS_DECLARE_STATIC_CONST_STRING(str_time_finish_us,         "time_finish_us");
KOS_DECLARE_STATIC_CONST_STRING(str_time_total_us,          "time_total_us");

static const KOS_CONVERT conv_gc_stats[22] = {
    { KOS_CONST_ID(str_num_objs_evacuated),     KOS_BADPTR, offsetof(KOS_GC_STATS, num_objs_evacuated),     0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_num_objs_freed),         KOS_BADPTR, offsetof(KOS_GC_STATS, num_objs_freed),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_num_objs_finalized),     KOS_BADPTR, offsetof(KOS_GC_STATS, num_objs_finalized),     0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_num_pages_kept),         KOS_BADPTR, offsetof(KOS_GC_STATS, num_pages_kept),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_num_pages_dropped),      KOS_BADPTR, offsetof(KOS_GC_STATS, num_pages_dropped),      0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_num_pages_freed),        KOS_BADPTR, offsetof(KOS_GC_STATS, num_pages_freed),        0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_size_evacuated),         KOS_BADPTR, offsetof(KOS_GC_STATS, size_evacuated),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_size_freed),             KOS_BADPTR, offsetof(KOS_GC_STATS, size_freed),             0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_size_kept),              KOS_BADPTR, offsetof(KOS_GC_STATS, size_kept),              0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_initial_heap_size),      KOS_BADPTR, offsetof(KOS_GC_STATS, initial_heap_size),      0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_initial_used_heap_size), KOS_BADPTR, offsetof(KOS_GC_STATS, initial_used_heap_size), 0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_initial_malloc_size),    KOS_BADPTR, offsetof(KOS_GC_STATS, initial_malloc_size),    0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_heap_size),              KOS_BADPTR, offsetof(KOS_GC_STATS, heap_size),              0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_used_heap_size),         KOS_BADPTR, offsetof(KOS_GC_STATS, used_heap_size),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_malloc_size),            KOS_BADPTR, offsetof(KOS_GC_STATS, malloc_size),            0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_stop_us),           KOS_BADPTR, offsetof(KOS_GC_STATS, time_stop_us),           0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_mark_us),           KOS_BADPTR, offsetof(KOS_GC_STATS, time_mark_us),           0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_evac_us),           KOS_BADPTR, offsetof(KOS_GC_STATS, time_evac_us),           0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_update_us),         KOS_BADPTR, offsetof(KOS_GC_STATS, time_update_us),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_finish_us),         KOS_BADPTR, offsetof(KOS_GC_STATS, time_finish_us),         0, KOS_NATIVE_UINT32 },
    { KOS_CONST_ID(str_time_total_us),          KOS_BADPTR, offsetof(KOS_GC_STATS, time_total_us),          0, KOS_NATIVE_UINT32 },
    KOS_DEFINE_TAIL_ARG()
};

/* @item kos collect_garbage()
 *
 *     collect_garbage()
 *
 * Runs the garbage collector.
 *
 * Returns an object containing statistics from the garbage collection cycle.
 *
 * Throws an exception if there was an error, for example if the heap
 * ran out of memory.
 */
static KOS_OBJ_ID collect_garbage(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  this_obj,
                                  KOS_OBJ_ID  args_obj)
{
    KOS_LOCAL    out;
    KOS_GC_STATS stats = KOS_GC_STATS_INIT(~0U);
    int          error;

    KOS_init_local(ctx, &out);

    TRY(KOS_collect_garbage(ctx, &stats));

    out.o = KOS_new_object(ctx);
    TRY_OBJID(out.o);

    TRY(KOS_set_properties_from_native(ctx, out.o, conv_gc_stats, &stats));

cleanup:
    out.o = KOS_destroy_top_local(ctx, &out);

    return error ? KOS_BADPTR : out.o;
}

static const KOS_CONVERT execute_args[4] = {
    KOS_DEFINE_MANDATORY_ARG(str_script),
    KOS_DEFINE_OPTIONAL_ARG( str_name, KOS_VOID),
    KOS_DEFINE_OPTIONAL_ARG( str_base, KOS_TRUE),
    KOS_DEFINE_TAIL_ARG()
};

/* @item kos execute()
 *
 *     execute(script, name = "", base = true)
 *
 * Executes a Kos script in a new temporary module.
 *
 * `script` is either a buffer or a string containing the script to execute.
 *
 * `name` is optional module name, used in messages.
 *
 * `base` is a boolean indicating whether contents of the base module should
 * be imported into the script, it defaults to `true`.
 *
 * This function pauses and waits until the script finishes executing.
 *
 * Returns the result of the last statement in `script`.
 */
static KOS_OBJ_ID execute(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  this_obj,
                          KOS_OBJ_ID  args_obj)
{
    KOS_VECTOR   name_cstr;
    KOS_VECTOR   data_cstr;
    KOS_LOCAL    name;
    KOS_OBJ_ID   arg_id;
    KOS_OBJ_ID   ret        = KOS_VOID;
    const char  *data       = KOS_NULL;
    unsigned     data_size  = 0;
    unsigned     flags      = KOS_RUN_TEMPORARY;
    int          error      = KOS_SUCCESS;

    KOS_vector_init(&name_cstr);
    KOS_vector_init(&data_cstr);

    KOS_init_local(ctx, &name);

    assert(KOS_get_array_size(args_obj) > 1);

    arg_id = KOS_array_read(ctx, args_obj, 2);
    if (arg_id == KOS_TRUE)
        flags |= KOS_IMPORT_BASE;

    name.o = KOS_array_read(ctx, args_obj, 1);
    TRY_OBJID(name.o);

    if ((name.o != KOS_VOID) && (GET_OBJ_TYPE(name.o) != OBJ_STRING))
        RAISE_EXCEPTION_STR(str_err_name_not_string);

    arg_id = KOS_array_read(ctx, args_obj, 0);
    TRY_OBJID(arg_id);

    if (GET_OBJ_TYPE(arg_id) == OBJ_STRING) {
        TRY(KOS_string_to_cstr_vec(ctx, arg_id, &data_cstr));
        data_size = (unsigned)data_cstr.size - 1;
    }
    else if (GET_OBJ_TYPE(arg_id) == OBJ_BUFFER) {
        data_size = KOS_get_buffer_size(arg_id);

        if (data_size) {
            if (KOS_vector_resize(&data_cstr, data_size)) {
                KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                goto cleanup;
            }

            memcpy(data_cstr.buffer, KOS_buffer_data_const(arg_id), data_size);
        }
    }
    else
        RAISE_EXCEPTION_STR(str_err_script_not_buffer);

    data = data_cstr.buffer;

    if (name.o == KOS_VOID) {
        if (KOS_vector_resize(&name_cstr, 1)) {
            KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
            goto cleanup;
        }
        name_cstr.buffer[0] = 0;
    }
    else {
        TRY(KOS_string_to_cstr_vec(ctx, name.o, &name_cstr));
        name.o = KOS_VOID;
    }

    ret = KOS_repl(ctx, name_cstr.buffer, flags, KOS_NULL, data, data_size);

cleanup:
    KOS_destroy_top_local(ctx, &name);

    KOS_vector_destroy(&name_cstr);
    KOS_vector_destroy(&data_cstr);

    return error ? KOS_BADPTR : ret;
}

/* @item kos search_paths()
 *
 *     search_paths()
 *
 * Returns array containing module search paths used for finding modules to import.
 */
static KOS_OBJ_ID search_paths(KOS_CONTEXT ctx,
                               KOS_OBJ_ID  this_obj,
                               KOS_OBJ_ID  args_obj)
{
    return KOS_array_slice(ctx, ctx->inst->modules.search_paths, 0, 0x7FFFFFFFU);
}

/* @item kos version
 *
 *     version
 *
 * Kos version number.
 *
 * This is a 3-element array which contains major, minor and revision numbers.
 *
 * Example:
 *
 *     > kos.version
 *     [1, 0, 0]
 */

int kos_module_kos_init(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj)
{
    int       error = KOS_SUCCESS;
    KOS_LOCAL module;
    KOS_LOCAL version;

    KOS_init_local_with(ctx, &module, module_obj);
    KOS_init_local(     ctx, &version);

    TRY_ADD_FUNCTION(        ctx, module.o, "collect_garbage",      collect_garbage, KOS_NULL);
    TRY_ADD_FUNCTION(        ctx, module.o, "execute",              execute,         execute_args);
    TRY_ADD_FUNCTION(        ctx, module.o, "search_paths",         search_paths,    KOS_NULL);
    TRY_ADD_GENERATOR(       ctx, module.o, "raw_lexer",            raw_lexer,       raw_lexer_args);

    version.o = KOS_new_array(ctx, 3);
    TRY_OBJID(version.o);

    TRY(KOS_array_write(ctx, version.o, 0, TO_SMALL_INT(KOS_VERSION_MAJOR)));
    TRY(KOS_array_write(ctx, version.o, 1, TO_SMALL_INT(KOS_VERSION_MINOR)));
    TRY(KOS_array_write(ctx, version.o, 2, TO_SMALL_INT(KOS_VERSION_REVISION)));
    TRY(KOS_lock_object(ctx, version.o));

    TRY(KOS_module_add_global(ctx, module.o, KOS_CONST_ID(str_version), version.o, KOS_NULL));

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_whitespace",     TT_WHITESPACE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_eol",            TT_EOL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_comment",        TT_COMMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_eof",            TT_EOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_identifier",     TT_IDENTIFIER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_keyword",        TT_KEYWORD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_numeric",        TT_NUMERIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_string",         TT_STRING);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_string_open",    TT_STRING_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_operator",       TT_OPERATOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "token_separator",      TT_SEPARATOR);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_none",         KW_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_line",         KW_LINE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_assert",       KW_ASSERT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_async",        KW_ASYNC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_break",        KW_BREAK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_case",         KW_CASE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_catch",        KW_CATCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_class",        KW_CLASS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_const",        KW_CONST);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_constructor",  KW_CONSTRUCTOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_continue",     KW_CONTINUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_default",      KW_DEFAULT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_defer",        KW_DEFER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_delete",       KW_DELETE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_do",           KW_DO);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_else",         KW_ELSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_extends",      KW_EXTENDS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_fallthrough",  KW_FALLTHROUGH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_false",        KW_FALSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_for",          KW_FOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_fun",          KW_FUN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_get",          KW_GET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_if",           KW_IF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_import",       KW_IMPORT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_in",           KW_IN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_instanceof",   KW_INSTANCEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_loop",         KW_LOOP);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_repeat",       KW_REPEAT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_return",       KW_RETURN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_set",          KW_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_static",       KW_STATIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_super",        KW_SUPER);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_switch",       KW_SWITCH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_this",         KW_THIS);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_throw",        KW_THROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_true",         KW_TRUE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_try",          KW_TRY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_typeof",       KW_TYPEOF);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_var",          KW_VAR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_void",         KW_VOID);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_while",        KW_WHILE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_with",         KW_WITH);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "keyword_yield",        KW_YIELD);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_none",              OT_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mask",              OT_MASK);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_arithmetic",        OT_ARITHMETIC);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_unary",             OT_UNARY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_multiplicative",    OT_MULTIPLICATIVE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_bitwise",           OT_BITWISE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_comparison",        OT_COMPARISON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_assignment",        OT_ASSIGNMENT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_add",               OT_ADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_sub",               OT_SUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mul",               OT_MUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_div",               OT_DIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_mod",               OT_MOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_not",               OT_NOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lognot",            OT_LOGNOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_and",               OT_AND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_or",                OT_OR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_xor",               OT_XOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shl",               OT_SHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shr",               OT_SHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_shru",              OT_SHRU);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logand",            OT_LOGAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logor",             OT_LOGOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_logtri",            OT_LOGTRI);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_dot",               OT_DOT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_more",              OT_MORE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_arrow",             OT_ARROW);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lambda",            OT_LAMBDA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_eq",                OT_EQ);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_ne",                OT_NE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_ge",                OT_GE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_gt",                OT_GT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_le",                OT_LE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_lt",                OT_LT);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_set",               OT_SET);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setadd",            OT_SETADD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setsub",            OT_SETSUB);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setmul",            OT_SETMUL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setdiv",            OT_SETDIV);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setmod",            OT_SETMOD);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setand",            OT_SETAND);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setor",             OT_SETOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setxor",            OT_SETXOR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshl",            OT_SETSHL);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshr",            OT_SETSHR);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "op_setshru",           OT_SETSHRU);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_none",             ST_NONE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_paren_open",       ST_PAREN_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_paren_close",      ST_PAREN_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_comma",            ST_COMMA);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_colon",            ST_COLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_semicolon",        ST_SEMICOLON);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_square_open",      ST_SQUARE_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_square_close",     ST_SQUARE_CLOSE);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_curly_open",       ST_CURLY_OPEN);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "sep_curly_close",      ST_CURLY_CLOSE);

    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "any_token",            NT_ANY);
    TRY_ADD_INTEGER_CONSTANT(ctx, module.o, "continue_string",      NT_CONTINUE_STRING);

cleanup:
    KOS_destroy_top_locals(ctx, &version, &module);

    return error;
}

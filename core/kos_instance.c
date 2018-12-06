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

#include "../inc/kos_instance.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_threads.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system.h"
#include "kos_try.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char str_init[]                    = "init";
static const char str_err_not_array[]           = "object is not an array";
static const char str_err_thread_registered[]   = "thread already registered";
static const char str_format_exception[]        = "Exception: ";
static const char str_format_hash[]             = "  #";
static const char str_format_line[]             = ":";
static const char str_format_function[]         = " in '";
static const char str_format_module[]           = "' in ";
static const char str_format_offset[]           = "  ";
static const char str_format_question_marks[]   = "???";

DECLARE_CONST_OBJECT(KOS_void)  = KOS_CONST_OBJECT_INIT(OBJ_VOID,    0);
DECLARE_CONST_OBJECT(KOS_false) = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 0);
DECLARE_CONST_OBJECT(KOS_true)  = KOS_CONST_OBJECT_INIT(OBJ_BOOLEAN, 1);

#ifdef CONFIG_PERF
struct KOS_PERF_S _kos_perf = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 },
    0, 0,
    0, 0, 0, 0
};
#endif

#ifdef CONFIG_SEQFAIL
static int                  _kos_seq_init      = 0;
static KOS_ATOMIC(uint32_t) _kos_seq;
static uint32_t             _kos_seq_threshold = ~0U;

int kos_seq_fail(void)
{
    if ( ! _kos_seq_init) {

        KOS_VECTOR cstr;
        int64_t    value = -1;

        kos_vector_init(&cstr);

        if (kos_get_env("KOSSEQFAIL", &cstr) == KOS_SUCCESS
                && cstr.size > 0
                && kos_parse_int(cstr.buffer, cstr.buffer + cstr.size - 1, &value) == KOS_SUCCESS)
            _kos_seq_threshold = (uint32_t)value;

        kos_vector_destroy(&cstr);

        KOS_atomic_write_u32(_kos_seq, 0);

        _kos_seq_init = 1;
    }

    if ((uint32_t)KOS_atomic_add_i32(_kos_seq, 1) >= _kos_seq_threshold)
        return KOS_ERROR_INTERNAL;

    return KOS_SUCCESS;
}
#endif

static int _push_local_refs_object(KOS_CONTEXT ctx)
{
    int             error      = KOS_ERROR_EXCEPTION;
    KOS_LOCAL_REFS *local_refs = (KOS_LOCAL_REFS *)
        kos_alloc_object(ctx, OBJ_LOCAL_REFS, (uint32_t)sizeof(KOS_LOCAL_REFS));

    if (local_refs) {
        local_refs->header.num_tracked = 0;
        local_refs->header.prev_scope  = KOS_LOOK_FURTHER;
        local_refs->next               = ctx->local_refs;
        ctx->local_refs                = OBJID(LOCAL_REFS, local_refs);

        error = KOS_SUCCESS;
    }

    return error;
}

static int _register_thread(KOS_INSTANCE *inst,
                            KOS_CONTEXT   ctx)
{
    int    error = KOS_SUCCESS;
    size_t i;

    assert( ! kos_tls_get(inst->threads.thread_key));

    ctx->inst             = inst;
    ctx->exception        = KOS_BADPTR;
    ctx->retval           = KOS_BADPTR;
    ctx->stack            = KOS_BADPTR;
    ctx->local_refs       = KOS_BADPTR;
    ctx->regs_idx         = 0;
    ctx->stack_depth      = 0;
    ctx->tmp_ref_count    = 0;
    ctx->helper_ref_count = 0;

    for (i = 0; i < sizeof(ctx->tmp_refs) / sizeof(ctx->tmp_refs[0]); ++i)
        ctx->tmp_refs[i] = 0;

    if (kos_tls_get(inst->threads.thread_key))
        RAISE_EXCEPTION(str_err_thread_registered);

    kos_tls_set(inst->threads.thread_key, ctx);

    TRY(_push_local_refs_object(ctx));

cleanup:
    if (error)
        kos_heap_release_thread_page(ctx);

    return error;
}

static void _unregister_thread(KOS_INSTANCE *inst,
                               KOS_CONTEXT   ctx)
{
    kos_heap_release_thread_page(ctx);

    kos_tls_set(inst->threads.thread_key, 0);

    kos_lock_mutex(&inst->threads.mutex);

    assert(ctx != &inst->threads.main_thread);

    if (ctx->prev)
        ctx->prev->next = ctx->next;

    if (ctx->next)
        ctx->next->prev = ctx->prev;

    kos_unlock_mutex(&inst->threads.mutex);
}

int KOS_instance_register_thread(KOS_INSTANCE *inst,
                                 KOS_CONTEXT   ctx)
{
    int error;

    kos_lock_mutex(&inst->threads.mutex);

    ctx->prev                      = &inst->threads.main_thread;
    ctx->next                      = inst->threads.main_thread.next;
    inst->threads.main_thread.next = ctx;
    if (ctx->next)
        ctx->next->prev            = ctx;

    kos_unlock_mutex(&inst->threads.mutex);

    ctx->cur_page = 0;

    error = _register_thread(inst, ctx);

    if (error)
        _unregister_thread(inst, ctx);

    return error;
}

void KOS_instance_unregister_thread(KOS_INSTANCE *inst,
                                    KOS_CONTEXT   ctx)
{
    assert((KOS_CONTEXT)kos_tls_get(inst->threads.thread_key) == ctx);

    _unregister_thread(inst, ctx);
}

static int _add_multiple_paths(KOS_CONTEXT ctx, KOS_VECTOR *cpaths)
{
    int   error = KOS_SUCCESS;
    char *buf   = cpaths->buffer;

    while ( ! error) {
        char *end = strchr(buf, KOS_PATH_LIST_SEPARATOR);

        if (end)
            *end = '\0';

        error = KOS_instance_add_path(ctx, buf);

        if (end)
            buf = end + 1;
        else
            break;
    }

    return error;
}

static int _init_search_paths(KOS_CONTEXT ctx)
{
#ifdef CONFIG_DISABLE_KOSPATH
    return KOS_SUCCESS;
#else
    int        error = KOS_SUCCESS;
    KOS_VECTOR cpaths;

    kos_vector_init(&cpaths);

    if (kos_get_env("KOSPATH", &cpaths) == KOS_SUCCESS)
        error = _add_multiple_paths(ctx, &cpaths);

    kos_vector_destroy(&cpaths);

    return error;
#endif
}

static KOS_OBJ_ID _alloc_empty_string(KOS_CONTEXT ctx)
{
    KOS_STRING *str = (KOS_STRING *)kos_alloc_object(ctx,
                                                     OBJ_STRING,
                                                     sizeof(KOS_STRING));

    if (str) {
        str->header.flags  = (uint8_t)KOS_STRING_ELEM_8 | (uint8_t)KOS_STRING_LOCAL;
        str->header.length = 0;
        str->header.hash   = 0;
    }

    return OBJID(STRING, str);
}

struct KOS_INIT_STRING_S {
    enum KOS_STR_E str_id;
    const char    *text;
};

static int _init_common_strings(KOS_CONTEXT   ctx,
                                KOS_INSTANCE *inst)
{
    int    error = KOS_SUCCESS;
    size_t i;

    static const struct KOS_INIT_STRING_S init[] = {

        /* Init this one first before anything else */
        { KOS_STR_OUT_OF_MEMORY, "out of memory" },

        { KOS_STR_ARGS,       "args"      },
        { KOS_STR_ARRAY,      "array"     },
        { KOS_STR_BACKTRACE,  "backtrace" },
        { KOS_STR_BOOLEAN,    "boolean"   },
        { KOS_STR_BUFFER,     "buffer"    },
        { KOS_STR_CLASS,      "class"     },
        { KOS_STR_FALSE,      "false"     },
        { KOS_STR_FILE,       "file"      },
        { KOS_STR_FLOAT,      "float"     },
        { KOS_STR_FUNCTION,   "function"  },
        { KOS_STR_GLOBAL,     "global"    },
        { KOS_STR_INTEGER,    "integer"   },
        { KOS_STR_LINE,       "line"      },
        { KOS_STR_MODULE,     "module"    },
        { KOS_STR_OBJECT,     "object"    },
        { KOS_STR_OFFSET,     "offset"    },
        { KOS_STR_PROTOTYPE,  "prototype" },
        { KOS_STR_QUOTE_MARK, "\""        },
        { KOS_STR_RESULT,     "result"    },
        { KOS_STR_SLICE,      "slice"     },
        { KOS_STR_STRING,     "string"    },
        { KOS_STR_THIS,       "this"      },
        { KOS_STR_TRUE,       "true"      },
        { KOS_STR_VALUE,      "value"     },
        { KOS_STR_VOID,       "void"      },
        { KOS_STR_XBUILTINX,  "<builtin>" }
    };

    inst->common_strings[KOS_STR_EMPTY] = _alloc_empty_string(ctx);
    TRY_OBJID(inst->common_strings[KOS_STR_EMPTY]);

    for (i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        const KOS_OBJ_ID str_id = KOS_new_const_ascii_cstring(ctx, init[i].text);
        TRY_OBJID(str_id);

        inst->common_strings[init[i].str_id] = str_id;
    }

cleanup:
    return error;
}

static void _clear_instance(KOS_INSTANCE *inst)
{
    int i;

    for (i = 0; i < KOS_STR_NUM; ++i)
        inst->common_strings[i] = KOS_BADPTR;

    /* Set to an innocuous value in case initial allocation fails */
    inst->common_strings[KOS_STR_OUT_OF_MEMORY] = KOS_VOID;

    /* Disable GC during early init */
    inst->flags = KOS_INST_MANUAL_GC;

    inst->args                            = KOS_BADPTR;
    inst->prototypes.object_proto         = KOS_BADPTR;
    inst->prototypes.number_proto         = KOS_BADPTR;
    inst->prototypes.integer_proto        = KOS_BADPTR;
    inst->prototypes.float_proto          = KOS_BADPTR;
    inst->prototypes.string_proto         = KOS_BADPTR;
    inst->prototypes.boolean_proto        = KOS_BADPTR;
    inst->prototypes.array_proto          = KOS_BADPTR;
    inst->prototypes.buffer_proto         = KOS_BADPTR;
    inst->prototypes.function_proto       = KOS_BADPTR;
    inst->prototypes.class_proto          = KOS_BADPTR;
    inst->prototypes.generator_proto      = KOS_BADPTR;
    inst->prototypes.exception_proto      = KOS_BADPTR;
    inst->prototypes.generator_end_proto  = KOS_BADPTR;
    inst->prototypes.thread_proto         = KOS_BADPTR;
    inst->modules.search_paths            = KOS_BADPTR;
    inst->modules.module_names            = KOS_BADPTR;
    inst->modules.modules                 = KOS_BADPTR;
    inst->modules.init_module             = KOS_BADPTR;
    inst->modules.module_inits            = KOS_BADPTR;
    inst->modules.load_chain              = 0;
    inst->threads.main_thread.next        = 0;
    inst->threads.main_thread.prev        = 0;
    inst->threads.main_thread.inst        = inst;
    inst->threads.main_thread.cur_page    = 0;
    inst->threads.main_thread.exception   = KOS_BADPTR;
    inst->threads.main_thread.retval      = KOS_BADPTR;
    inst->threads.main_thread.stack       = KOS_BADPTR;
    inst->threads.main_thread.stack_depth = 0;
}

int KOS_instance_init(KOS_INSTANCE *inst,
                      uint32_t      flags,
                      KOS_CONTEXT  *out_ctx)
{
    int         error;
    int         heap_ok   = 0;
    int         thread_ok = 0;
    KOS_MODULE *init_module;
    KOS_CONTEXT ctx;

    assert(!IS_HEAP_OBJECT(KOS_VOID));
    assert(!IS_HEAP_OBJECT(KOS_FALSE));
    assert(!IS_HEAP_OBJECT(KOS_TRUE));

    _clear_instance(inst);

    TRY(kos_tls_create(&inst->threads.thread_key));
    error = kos_create_mutex(&inst->threads.mutex);
    if (error) {
        kos_tls_destroy(inst->threads.thread_key);
        goto cleanup;
    }
    thread_ok = 1;

    TRY(kos_heap_init(inst));
    heap_ok = 1;

    init_module = (KOS_MODULE *)kos_heap_early_alloc(inst,
                                                     &inst->threads.main_thread,
                                                     OBJ_MODULE,
                                                     (uint32_t)sizeof(KOS_MODULE));
    if ( ! init_module)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);

    init_module->flags          = 0;
    init_module->name           = KOS_BADPTR;
    init_module->path           = KOS_BADPTR;
    init_module->inst           = inst;
    init_module->constants      = KOS_BADPTR;
    init_module->global_names   = KOS_BADPTR;
    init_module->globals        = KOS_BADPTR;
    init_module->module_names   = KOS_BADPTR;
    init_module->bytecode       = 0;
    init_module->line_addrs     = 0;
    init_module->func_addrs     = 0;
    init_module->num_line_addrs = 0;
    init_module->num_func_addrs = 0;
    init_module->bytecode_size  = 0;

    inst->modules.init_module = OBJID(MODULE, init_module);

    TRY(_register_thread(inst, &inst->threads.main_thread));

    ctx = &inst->threads.main_thread;

    TRY(_init_common_strings(ctx, inst));

    TRY_OBJID(inst->prototypes.object_proto        = KOS_new_object_with_prototype(ctx, KOS_VOID));
    TRY_OBJID(inst->prototypes.number_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.integer_proto       = KOS_new_object_with_prototype(ctx, inst->prototypes.number_proto));
    TRY_OBJID(inst->prototypes.float_proto         = KOS_new_object_with_prototype(ctx, inst->prototypes.number_proto));
    TRY_OBJID(inst->prototypes.string_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.boolean_proto       = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.array_proto         = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.buffer_proto        = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.function_proto      = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.class_proto         = KOS_new_object_with_prototype(ctx, inst->prototypes.function_proto));
    TRY_OBJID(inst->prototypes.generator_proto     = KOS_new_object_with_prototype(ctx, inst->prototypes.function_proto));
    TRY_OBJID(inst->prototypes.exception_proto     = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.generator_end_proto = KOS_new_object(ctx));
    TRY_OBJID(inst->prototypes.thread_proto        = KOS_new_object(ctx));

    TRY_OBJID(init_module->name          = KOS_new_const_ascii_string(ctx, str_init, sizeof(str_init) - 1));
    TRY_OBJID(init_module->globals       = KOS_new_array(ctx, 0));
    TRY_OBJID(init_module->global_names  = KOS_new_object(ctx));
    TRY_OBJID(init_module->module_names  = KOS_new_object(ctx));
    TRY_OBJID(inst->modules.module_names = KOS_new_object(ctx));
    TRY_OBJID(inst->modules.modules      = KOS_new_array(ctx, 0));
    TRY_OBJID(inst->modules.search_paths = KOS_new_array(ctx, 0));
    TRY_OBJID(inst->modules.module_inits = KOS_new_object(ctx));

    TRY_OBJID(inst->args = KOS_new_array(ctx, 0));

    TRY(_init_search_paths(ctx));

    *out_ctx = ctx;

#ifdef CONFIG_MAD_GC /* TODO always enable automatic GC */
    /* Enable automatic GC */
    inst->flags = 0;
#endif

    /* Set user flags */
    inst->flags |= flags;

cleanup:
    if (error) {
        if (heap_ok)
            kos_heap_destroy(inst);

        if (thread_ok) {
            kos_tls_destroy(inst->threads.thread_key);
            kos_destroy_mutex(&inst->threads.mutex);
        }
    }

    inst->threads.main_thread.retval = KOS_BADPTR;

    return error;
}

void KOS_instance_destroy(KOS_INSTANCE *inst)
{
    uint32_t    i;
    uint32_t    num_modules = KOS_get_array_size(inst->modules.modules);
    KOS_CONTEXT ctx         = &inst->threads.main_thread;

    for (i = 0; i < num_modules; i++) {
        KOS_OBJ_ID module_obj = KOS_array_read(ctx, inst->modules.modules, (int)i);
        assert(!IS_BAD_PTR(module_obj));
        if (IS_BAD_PTR(module_obj))
            KOS_clear_exception(ctx);
        else if (GET_OBJ_TYPE(module_obj) == OBJ_MODULE) {
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_BYTECODE))
                kos_free((void *)OBJPTR(MODULE, module_obj)->bytecode);
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_LINE_ADDRS))
                kos_free((void *)OBJPTR(MODULE, module_obj)->line_addrs);
            if ((OBJPTR(MODULE, module_obj)->flags & KOS_MODULE_OWN_FUNC_ADDRS))
                kos_free((void *)OBJPTR(MODULE, module_obj)->func_addrs);
        }
        else {
            /* failed e.g. during compilation */
            assert(GET_OBJ_TYPE(module_obj) == OBJ_VOID);
        }
    }

    kos_heap_destroy(inst);

    kos_tls_destroy(inst->threads.thread_key);

    kos_destroy_mutex(&inst->threads.mutex);

    _clear_instance(inst);

#ifdef CONFIG_PERF
#   define PERF_RATIO(a) do {                                             \
        const uint32_t va = KOS_atomic_read_u32(_kos_perf.a##_success);   \
        const uint32_t vb = KOS_atomic_read_u32(_kos_perf.a##_fail);      \
        uint32_t       total = va + vb;                                   \
        if (total == 0) total = 1;                                        \
        fprintf(stderr, "    " #a "\t%u / %u (%u%%)\n",                   \
                va, total, va * 100 / total);                             \
    } while (0)
#   define PERF_VALUE(a) do {                                             \
        const uint32_t va = KOS_atomic_read_u32(_kos_perf.a);             \
        fprintf(stderr, "    " #a "\t%u\n", va);                          \
    } while (0)
    printf("Performance stats:\n");
    PERF_RATIO(object_get);
    PERF_RATIO(object_set);
    PERF_RATIO(object_delete);
    PERF_RATIO(object_resize);
    PERF_RATIO(object_salvage);
    PERF_VALUE(object_collision[0]);
    PERF_VALUE(object_collision[1]);
    PERF_VALUE(object_collision[2]);
    PERF_VALUE(object_collision[3]);
    PERF_RATIO(array_salvage);
    PERF_VALUE(alloc_object);
    PERF_VALUE(alloc_huge_object);
    PERF_VALUE(alloc_new_page);
    PERF_VALUE(alloc_free_page);
#endif
}

int KOS_instance_add_path(KOS_CONTEXT ctx, const char *module_search_path)
{
    int           error;
    uint32_t      len;
    KOS_OBJ_ID    path_str;
    KOS_INSTANCE *inst = ctx->inst;

    path_str = KOS_new_cstring(ctx, module_search_path);
    TRY_OBJID(path_str);

    len = KOS_get_array_size(inst->modules.search_paths);
    TRY(KOS_array_resize(ctx, inst->modules.search_paths, len+1));
    TRY(KOS_array_write(ctx, inst->modules.search_paths, (int)len, path_str));

cleanup:
    return error;
}

#ifndef CONFIG_MODULE_PATH
#   ifdef _WIN32
#       define CONFIG_MODULE_PATH "modules"
#   else
#       define CONFIG_MODULE_PATH "../share/kos/modules"
#   endif
#endif

int KOS_instance_add_default_path(KOS_CONTEXT ctx, const char *argv0)
{
    int               error      = KOS_ERROR_NOT_FOUND;
    KOS_VECTOR        cstr;
    KOS_VECTOR        cpath;
    size_t            pos;
    static const char rel_path[] = CONFIG_MODULE_PATH;

    kos_vector_init(&cstr);
    kos_vector_init(&cpath);

    if (argv0) {

        size_t len = strlen(argv0);

        if ( ! len)
            goto cleanup;

        /* Absolute or relative path */
        if (strchr(argv0, KOS_PATH_SEPARATOR)) {

            if ( ! kos_does_file_exist(argv0))
                RAISE_ERROR(KOS_ERROR_NOT_FOUND);

            len += 1;
            TRY(kos_vector_resize(&cstr, len));

            memcpy(cstr.buffer, argv0, len);
        }
        /* Just executable name, scan PATH */
        else {

            char *buf;

            TRY(kos_get_env("PATH", &cpath));

            buf = cpath.buffer;

            TRY(kos_vector_reserve(&cstr, cpath.size + len + 1));

            cstr.size = 0;

            while ((size_t)(buf - cpath.buffer + 1) < cpath.size) {

                char  *end = strchr(buf, KOS_PATH_LIST_SEPARATOR);
                size_t base_len;

                if ( ! end)
                    end = cpath.buffer + cpath.size - 1;

                base_len = end - buf;

                TRY(kos_vector_resize(&cstr, base_len + 1 + len + 1));

                memcpy(cstr.buffer, buf, base_len);
                cstr.buffer[base_len] = KOS_PATH_SEPARATOR;
                memcpy(&cstr.buffer[base_len + 1], argv0, len);
                cstr.buffer[base_len + 1 + len] = 0;

                if (kos_does_file_exist(cstr.buffer))
                    break;

                cstr.size = 0;

                buf = end + 1;
            }

            if (cstr.size == 0)
                RAISE_ERROR(KOS_ERROR_NOT_FOUND);
        }
    }
    else {
        if (kos_seq_fail())
            RAISE_ERROR(KOS_ERROR_NOT_FOUND);

        TRY(kos_executable_path(&cstr));
    }

    TRY(kos_get_absolute_path(&cstr));

    assert(cstr.size > 0);

    for (pos = cstr.size - 1; pos > 0 && cstr.buffer[pos] != KOS_PATH_SEPARATOR; --pos);

    if ( ! pos)
        RAISE_ERROR(KOS_ERROR_NOT_FOUND);

    TRY(kos_vector_resize(&cstr, pos + 1 + sizeof(rel_path)));

    memcpy(&cstr.buffer[pos + 1], rel_path, sizeof(rel_path));

    TRY(KOS_instance_add_path(ctx, cstr.buffer));

cleanup:
    kos_vector_destroy(&cpath);
    kos_vector_destroy(&cstr);

    return error;
}

int KOS_instance_set_args(KOS_CONTEXT  ctx,
                          int          argc,
                          const char **argv)
{
    int           error;
    int           i;
    KOS_INSTANCE *inst = ctx->inst;

    assert(argc >= 0);

    if (argc <= 0)
        return KOS_SUCCESS;

    TRY(KOS_array_resize(ctx, inst->args, (uint32_t)argc));

    for (i = 0; i < argc; i++) {
        KOS_OBJ_ID arg_str = KOS_new_cstring(ctx, argv[i]);
        TRY_OBJID(arg_str);

        TRY(KOS_array_write(ctx, inst->args, i, arg_str));
    }

cleanup:
    return error;
}

int KOS_instance_register_builtin(KOS_CONTEXT      ctx,
                                  const char      *module,
                                  KOS_BUILTIN_INIT init)
{
    int                       error = KOS_SUCCESS;
    struct KOS_MODULE_INIT_S *mod_init;
    KOS_INSTANCE      *const  inst  = ctx->inst;
    KOS_OBJ_ID                module_name;

    module_name = KOS_new_cstring(ctx, module);
    TRY_OBJID(module_name);

    kos_track_refs(ctx, 1, &module_name);

    mod_init = (struct KOS_MODULE_INIT_S *)kos_alloc_object(ctx,
                                                            OBJ_OPAQUE,
                                                            sizeof(struct KOS_MODULE_INIT_S));

    kos_untrack_refs(ctx, 1);

    if ( ! mod_init)
        RAISE_ERROR(KOS_ERROR_EXCEPTION);

    mod_init->init = init;

    error = KOS_set_property(ctx,
                             inst->modules.module_inits,
                             module_name,
                             OBJID(OPAQUE, (KOS_OPAQUE *)mod_init));

cleanup:
    return error;
}

KOS_OBJ_ID KOS_get_string(KOS_CONTEXT    ctx,
                          enum KOS_STR_E str_id)
{
    assert((int)str_id >= 0 && str_id < KOS_STR_NUM);

    return ctx->inst->common_strings[str_id];
}

#ifndef NDEBUG
void KOS_instance_validate(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *inst = ctx->inst;
    KOS_CONTEXT   thread_ctx;

    assert(inst);

    thread_ctx = (KOS_CONTEXT)kos_tls_get(inst->threads.thread_key);

    assert(thread_ctx);
    assert(thread_ctx == ctx);
}
#endif

void KOS_raise_exception(KOS_CONTEXT ctx,
                         KOS_OBJ_ID  exception_obj)
{
    /* Nested exceptions are not allowed. */
    /* This can only happen if there is a bug and an exception has been ignored. */
    assert(IS_BAD_PTR(ctx->exception));

    assert(GET_OBJ_TYPE(exception_obj) <= OBJ_LAST_TYPE ||
           GET_OBJ_TYPE(exception_obj) == OBJ_DYNAMIC_PROP);

    if (IS_BAD_PTR(ctx->exception))
        ctx->exception = exception_obj;
}

void KOS_raise_exception_cstring(KOS_CONTEXT ctx,
                                 const char *cstr)
{
    const KOS_OBJ_ID str = KOS_new_const_ascii_cstring(ctx, cstr);

    if ( ! IS_BAD_PTR(str))
        KOS_raise_exception(ctx, str);

    assert( ! IS_BAD_PTR(ctx->exception));
}

KOS_OBJ_ID KOS_format_exception(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  exception)
{
    int        error;
    unsigned   i;
    unsigned   depth;
    KOS_OBJ_ID value;
    KOS_OBJ_ID backtrace;
    KOS_OBJ_ID array = KOS_BADPTR;
    KOS_OBJ_ID str;
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    value = KOS_get_property(ctx, exception, KOS_get_string(ctx, KOS_STR_VALUE));
    TRY_OBJID(value);

    backtrace = KOS_get_property(ctx, exception, KOS_get_string(ctx, KOS_STR_BACKTRACE));
    TRY_OBJID(backtrace);

    if (GET_OBJ_TYPE(backtrace) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    depth = KOS_get_array_size(backtrace);
    array = KOS_new_array(ctx, 1 + depth);
    TRY_OBJID(array);

    if (kos_vector_reserve(&cstr, 80) != KOS_SUCCESS) {
        KOS_raise_exception(ctx, KOS_get_string(ctx, KOS_STR_OUT_OF_MEMORY));
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    TRY(kos_append_cstr(ctx, &cstr, str_format_exception, sizeof(str_format_exception) - 1));
    TRY(KOS_object_to_string_or_cstr_vec(ctx, value, KOS_DONT_QUOTE, 0, &cstr));

    str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
    TRY_OBJID(str);

    TRY(KOS_array_write(ctx, array, 0, str));

    for (i = 0; i < depth; i++) {

        char cbuf[16];

        const KOS_OBJ_ID frame_desc = KOS_array_read(ctx, backtrace, (int)i);
        TRY_OBJID(frame_desc);

        cstr.size = 0;

        TRY(kos_append_cstr(ctx, &cstr, str_format_hash,
                            sizeof(str_format_hash) - 1));

        snprintf(cbuf, sizeof(cbuf), "%u", i);
        TRY(kos_append_cstr(ctx, &cstr, cbuf, strlen(cbuf)));

        TRY(kos_append_cstr(ctx, &cstr, str_format_offset,
                            sizeof(str_format_offset) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_OFFSET));
        TRY_OBJID(str);
        if (IS_SMALL_INT(str)) {
            snprintf(cbuf, sizeof(cbuf), "0x%X", (unsigned)GET_SMALL_INT(str));
            TRY(kos_append_cstr(ctx, &cstr, cbuf, strlen(cbuf)));
        }
        else
            TRY(kos_append_cstr(ctx, &cstr, str_format_question_marks,
                                sizeof(str_format_question_marks) - 1));

        TRY(kos_append_cstr(ctx, &cstr, str_format_function,
                            sizeof(str_format_function) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_FUNCTION));
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_module,
                            sizeof(str_format_module) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_FILE));
        TRY_OBJID(str);
        str = KOS_get_file_name(ctx, str);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_line,
                            sizeof(str_format_line) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_LINE));
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
        TRY_OBJID(str);

        TRY(KOS_array_write(ctx, array, 1+(int)i, str));
    }

cleanup:
    kos_vector_destroy(&cstr);

    return error ? KOS_BADPTR : array;
}

void KOS_raise_generator_end(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    const KOS_OBJ_ID exception =
            KOS_new_object_with_prototype(ctx, inst->prototypes.generator_end_proto);

    if ( ! IS_BAD_PTR(exception))
        KOS_raise_exception(ctx, exception);
}

static int have_room_for_locals(KOS_CONTEXT ctx, int num_entries)
{
    const KOS_OBJ_ID local_refs = ctx->local_refs;
    unsigned         num_tracked;

    assert( ! IS_BAD_PTR(local_refs));
    assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

    num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;

    return (num_tracked + (unsigned)num_entries) <=
           (sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs) /
            sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs[0]));
}

static int reserve_locals(KOS_CONTEXT ctx, int num_entries)
{
    const KOS_OBJ_ID local_refs = ctx->local_refs;
    unsigned         num_tracked;

    assert( ! IS_BAD_PTR(local_refs));
    assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

    num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;

    if ((num_tracked + (unsigned)num_entries) <=
        (sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs) /
         sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs[0])))
        return KOS_SUCCESS;

    return _push_local_refs_object(ctx);
}

int KOS_push_local_scope(KOS_CONTEXT ctx, KOS_OBJ_ID *prev_scope)
{
    KOS_OBJ_ID local_refs  = ctx->local_refs;
    KOS_OBJ_ID prev_locals;
    int        error;

    *prev_scope = KOS_BADPTR;

    if (IS_BAD_PTR(local_refs)) {

        prev_locals = TO_SMALL_INT(KOS_LOOK_FURTHER);

        error = _push_local_refs_object(ctx);

        if (error)
            return error;

        local_refs = ctx->local_refs;
    }
    else
        prev_locals = TO_SMALL_INT(OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope);

    error = reserve_locals(ctx, 1);

    if ( ! error) {

        local_refs = ctx->local_refs;

        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        OBJPTR(LOCAL_REFS, local_refs)->refs[
            OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked++]
                = prev_scope;

        *prev_scope = prev_locals;

        OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope =
            OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked - 1;
    }

    return error;
}

void KOS_pop_local_scope(KOS_CONTEXT ctx, KOS_OBJ_ID *prev_scope)
{
    KOS_OBJ_ID prev_scope_idx;
    KOS_OBJ_ID local_refs  = ctx->local_refs;
    uint8_t    num_tracked = 0;

    if (IS_BAD_PTR(*prev_scope))
        return;

    assert( ! IS_BAD_PTR(local_refs));

    for (;;) {

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope;

        if (num_tracked != KOS_LOOK_FURTHER)
            break;

        local_refs = OBJPTR(LOCAL_REFS, local_refs)->next;

        assert( ! IS_BAD_PTR(local_refs));
    }

    assert(prev_scope == OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked]);

    prev_scope_idx = *(OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked]);

    assert(IS_SMALL_INT(prev_scope_idx));

    if (num_tracked) {
        OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked = num_tracked;
        OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope  = (uint8_t)GET_SMALL_INT(prev_scope_idx);
    }
    else {
        const KOS_OBJ_ID next = OBJPTR(LOCAL_REFS, local_refs)->next;

        if (IS_BAD_PTR(next))
            OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked = 0;
        else {
            local_refs = next;

            assert(OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope ==
                   (uint8_t)GET_SMALL_INT(prev_scope_idx));
        }
        OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope = KOS_LOOK_FURTHER;
    }

    ctx->local_refs = local_refs;
}

int KOS_push_locals(KOS_CONTEXT ctx, int* push_status, int num_entries, ...)
{
    int     error = KOS_SUCCESS;
    va_list args;

    assert(num_entries > 0);
    assert(num_entries <= KOS_MAX_LOCALS);
    assert(*push_status == 0);

    va_start(args, num_entries);

    if (have_room_for_locals(ctx, num_entries)) {
        const KOS_OBJ_ID local_refs = ctx->local_refs;
        uint8_t          num_tracked;
        KOS_OBJ_ID     **ptr;
        KOS_OBJ_ID     **end;

        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;
        ptr         = &OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked];
        end         = ptr + num_entries;

        do
            *(ptr++) = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
        while (ptr < end);

        OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked = num_tracked + num_entries;
    }
    else {
        KOS_OBJ_ID **helper_refs = &ctx->helper_refs[0];
        KOS_OBJ_ID **end         = helper_refs + num_entries;

        assert(ctx->helper_ref_count == 0);

        do
            *(helper_refs++) = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
        while (helper_refs < end);

        ctx->helper_ref_count = num_entries;

        error = reserve_locals(ctx, num_entries);

        if ( ! error) {
            const KOS_OBJ_ID local_refs = ctx->local_refs;
            uint8_t          num_tracked;
            KOS_OBJ_ID     **ptr;

            assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

            helper_refs = &ctx->helper_refs[0];
            num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;
            ptr         = &OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked];
            end         = ptr + num_entries;

            do
                *(ptr++) = *(helper_refs++);
            while (ptr < end);

            OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked = num_tracked + num_entries;
        }

        ctx->helper_ref_count = 0;
    }

    va_end(args);

    if ( ! error)
        *push_status = num_entries;

    return error;
}

void KOS_pop_locals(KOS_CONTEXT ctx, int push_status)
{
    if (push_status) {
        KOS_OBJ_ID local_refs = ctx->local_refs;
        unsigned   num_tracked;

        assert( ! IS_BAD_PTR(local_refs));
        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;

        if ( ! num_tracked) {

            local_refs = OBJPTR(LOCAL_REFS, local_refs)->next;

            assert( ! IS_BAD_PTR(local_refs));

            ctx->local_refs = local_refs;

            num_tracked = OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked;

            assert(num_tracked > 0);

            assert(OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope == KOS_LOOK_FURTHER ||
                   OBJPTR(LOCAL_REFS, local_refs)->header.prev_scope + 1 < num_tracked);
        }

        assert(num_tracked >= (unsigned)push_status);

        OBJPTR(LOCAL_REFS, local_refs)->header.num_tracked = num_tracked - push_status;
    }
}

void kos_set_return_value(KOS_CONTEXT ctx, KOS_OBJ_ID obj_id)
{
    KOS_instance_validate(ctx);

    ctx->retval = obj_id;
}

void kos_track_refs(KOS_CONTEXT ctx, int num_entries, ...)
{
    va_list  args;
    uint32_t i;
    uint32_t end;

    assert(num_entries > 0);
    assert((size_t)(ctx->tmp_ref_count + num_entries) <=
           sizeof(ctx->tmp_refs) / sizeof(ctx->tmp_refs[0]));

    i   = ctx->tmp_ref_count;
    end = i + num_entries;

    va_start(args, num_entries);

    do {
        ctx->tmp_refs[i] = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
        ++i;
    } while (i < end);

    ctx->tmp_ref_count = end;

    va_end(args);
}

void kos_untrack_refs(KOS_CONTEXT ctx, int num_entries)
{
    assert(num_entries > 0 && (unsigned)num_entries <= ctx->tmp_ref_count);

    ctx->tmp_ref_count -= num_entries;
}

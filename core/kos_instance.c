/*
 * Copyright (c) 2014-2020 Chris Dragan
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
#include "../inc/kos_atomic.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utils.h"
#include "kos_config.h"
#include "kos_const_strings.h"
#include "kos_debug.h"
#include "kos_heap.h"
#include "kos_malloc.h"
#include "kos_math.h"
#include "kos_memory.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_perf.h"
#include "kos_system.h"
#include "kos_threads_internal.h"
#include "kos_try.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char str_err_not_array[]           = "object is not an array";
static const char str_err_thread_registered[]   = "thread already registered";
static const char str_format_exception[]        = "Exception: ";
static const char str_format_hash[]             = "  #";
static const char str_format_line[]             = ":";
static const char str_format_function[]         = " in '";
static const char str_format_module[]           = "' in ";
static const char str_format_offset[]           = "  ";
static const char str_format_question_marks[]   = "???";

DECLARE_CONST_OBJECT(KOS_void,  OBJ_VOID,    0);
DECLARE_CONST_OBJECT(KOS_false, OBJ_BOOLEAN, 0);
DECLARE_CONST_OBJECT(KOS_true,  OBJ_BOOLEAN, 1);

#ifdef CONFIG_PERF
struct KOS_PERF_S kos_perf = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 },
    0, 0,
    0, 0, 0, 0, 0,
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 }
};
#endif

#ifdef CONFIG_SEQFAIL
static int                  kos_seq_init      = 0;
static KOS_ATOMIC(uint32_t) kos_seq;
static uint32_t             kos_seq_threshold = ~0U;

int kos_seq_fail(void)
{
    if ( ! kos_seq_init) {

        KOS_VECTOR cstr;
        int64_t    value = -1;

        kos_vector_init(&cstr);

        if (kos_get_env("KOSSEQFAIL", &cstr) == KOS_SUCCESS
                && cstr.size > 0
                && kos_parse_int(cstr.buffer, cstr.buffer + cstr.size - 1, &value) == KOS_SUCCESS)
            kos_seq_threshold = (uint32_t)value;

        kos_vector_destroy(&cstr);

        KOS_atomic_write_relaxed_u32(kos_seq, 0);

        kos_seq_init = 1;
    }

    if ((uint32_t)KOS_atomic_add_i32(kos_seq, 1) >= kos_seq_threshold)
        return KOS_ERROR_INTERNAL;

    return KOS_SUCCESS;
}
#endif

static int push_local_refs_object(KOS_CONTEXT ctx)
{
    int             error      = KOS_ERROR_EXCEPTION;
    KOS_LOCAL_REFS *local_refs = (KOS_LOCAL_REFS *)
        kos_alloc_object(ctx, KOS_ALLOC_MOVABLE, OBJ_LOCAL_REFS, (uint32_t)sizeof(KOS_LOCAL_REFS));

    if (local_refs) {
        local_refs->num_tracked = 0;
        local_refs->prev_scope  = KOS_LOOK_FURTHER;
        local_refs->next        = ctx->local_refs;
        ctx->local_refs         = OBJID(LOCAL_REFS, local_refs);

        error = KOS_SUCCESS;
    }

    return error;
}

static void init_context(KOS_CONTEXT ctx, KOS_INSTANCE *inst)
{
    size_t i;

    ctx->next             = 0;
    ctx->prev             = 0;
    ctx->gc_state         = GC_SUSPENDED;
    ctx->inst             = inst;
    ctx->cur_page         = 0;
    ctx->thread_obj       = KOS_BADPTR;
    ctx->exception        = KOS_BADPTR;
    ctx->retval           = KOS_BADPTR;
    ctx->stack            = KOS_BADPTR;
    ctx->regs_idx         = 0;
    ctx->stack_depth      = 0;
    ctx->tmp_ref_count    = 0;
    ctx->helper_ref_count = 0;
    ctx->local_list       = 0;
    ctx->local_refs       = KOS_BADPTR;

    for (i = 0; i < sizeof(ctx->tmp_refs) / sizeof(ctx->tmp_refs[0]); i++)
        ctx->tmp_refs[i] = 0;

    for (i = 0; i < sizeof(ctx->helper_refs) / sizeof(ctx->helper_refs[0]); i++)
        ctx->helper_refs[i] = 0;
}

static int register_thread(KOS_INSTANCE *inst,
                           KOS_CONTEXT   ctx)
{
    int error = KOS_SUCCESS;

    if (kos_tls_get(inst->threads.thread_key)) {
        TRY(KOS_resume_context(ctx));
        RAISE_EXCEPTION(str_err_thread_registered);
    }

    assert( ! kos_tls_get(inst->threads.thread_key));

    kos_tls_set(inst->threads.thread_key, ctx);

    TRY(KOS_resume_context(ctx));

    TRY(push_local_refs_object(ctx));

cleanup:
    if (error)
        kos_heap_release_thread_page(ctx);

    return error;
}

static void unregister_thread(KOS_INSTANCE *inst,
                              KOS_CONTEXT   ctx)
{
    assert(ctx != &inst->threads.main_thread);

    while ( ! IS_BAD_PTR(ctx->local_refs)) {
        const KOS_OBJ_ID next = OBJPTR(LOCAL_REFS, ctx->local_refs)->next;

        OBJPTR(LOCAL_REFS, ctx->local_refs)->num_tracked = 0;
        ctx->local_refs = next;
    }

    KOS_suspend_context(ctx);

    kos_tls_set(inst->threads.thread_key, 0);

    kos_lock_mutex(&inst->threads.ctx_mutex);

    if (ctx->prev)
        ctx->prev->next = ctx->next;

    if (ctx->next)
        ctx->next->prev = ctx->prev;

    kos_unlock_mutex(&inst->threads.ctx_mutex);
}

int KOS_instance_register_thread(KOS_INSTANCE *inst,
                                 KOS_CONTEXT   ctx)
{
    int error;

    init_context(ctx, inst);

#if defined(CONFIG_THREADS) && (CONFIG_THREADS == 0)
    {
        KOS_DECLARE_STATIC_CONST_STRING(str_no_threads, "Kos was compiled without supports for threads");

        KOS_raise_exception(ctx, KOS_CONST_ID(str_no_threads));
    }
    error = KOS_ERROR_EXCEPTION;
#else

    kos_lock_mutex(&inst->threads.ctx_mutex);

    ctx->prev                      = &inst->threads.main_thread;
    ctx->next                      = inst->threads.main_thread.next;
    inst->threads.main_thread.next = ctx;
    if (ctx->next)
        ctx->next->prev            = ctx;

    kos_unlock_mutex(&inst->threads.ctx_mutex);

    error = register_thread(inst, ctx);

    if (error)
        unregister_thread(inst, ctx);
#endif

    return error;
}

void KOS_instance_unregister_thread(KOS_INSTANCE *inst,
                                    KOS_CONTEXT   ctx)
{
    assert((KOS_CONTEXT)kos_tls_get(inst->threads.thread_key) == ctx);

    unregister_thread(inst, ctx);
}

static int add_multiple_paths(KOS_CONTEXT ctx, KOS_VECTOR *cpaths)
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

static int init_search_paths(KOS_CONTEXT ctx)
{
#ifdef CONFIG_DISABLE_KOSPATH
    return KOS_SUCCESS;
#else
    int        error = KOS_SUCCESS;
    KOS_VECTOR cpaths;

    kos_vector_init(&cpaths);

    if (kos_get_env("KOSPATH", &cpaths) == KOS_SUCCESS)
        error = add_multiple_paths(ctx, &cpaths);

    kos_vector_destroy(&cpaths);

    return error;
#endif
}

static void setup_init_module(KOS_MODULE *init_module)
{
    KOS_DECLARE_STATIC_CONST_STRING(str_init, "init");

    assert(kos_get_object_type(init_module->header) == OBJ_MODULE);

    init_module->flags          = 0;
    init_module->name           = KOS_CONST_ID(str_init);
    init_module->path           = KOS_STR_EMPTY;
    init_module->inst           = 0;
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
}

struct KOS_CONST_MODULE_S {
    struct KOS_CONST_OBJECT_ALIGNMENT_S align;
    KOS_MODULE                          object;
};

static KOS_OBJ_ID get_init_module(void)
{
    KOS_DECLARE_ALIGNED(32, static struct KOS_CONST_MODULE_S) init_module;

    kos_set_object_type_size(init_module.object.header, OBJ_MODULE, 0);

    setup_init_module(&init_module.object);

    assert( ! kos_is_heap_object(KOS_CONST_ID(init_module)));

    return KOS_CONST_ID(init_module);
}

static void clear_instance(KOS_INSTANCE *inst)
{
    /* Disable GC during early init */
    inst->flags = KOS_INST_MANUAL_GC;

    inst->args                           = KOS_BADPTR;
    inst->prototypes.object_proto        = KOS_BADPTR;
    inst->prototypes.number_proto        = KOS_BADPTR;
    inst->prototypes.integer_proto       = KOS_BADPTR;
    inst->prototypes.float_proto         = KOS_BADPTR;
    inst->prototypes.string_proto        = KOS_BADPTR;
    inst->prototypes.boolean_proto       = KOS_BADPTR;
    inst->prototypes.array_proto         = KOS_BADPTR;
    inst->prototypes.buffer_proto        = KOS_BADPTR;
    inst->prototypes.function_proto      = KOS_BADPTR;
    inst->prototypes.class_proto         = KOS_BADPTR;
    inst->prototypes.generator_proto     = KOS_BADPTR;
    inst->prototypes.exception_proto     = KOS_BADPTR;
    inst->prototypes.generator_end_proto = KOS_BADPTR;
    inst->prototypes.thread_proto        = KOS_BADPTR;
    inst->modules.search_paths           = KOS_BADPTR;
    inst->modules.module_names           = KOS_BADPTR;
    inst->modules.modules                = KOS_BADPTR;
    inst->modules.init_module            = get_init_module();
    inst->modules.module_inits           = KOS_BADPTR;
    inst->modules.load_chain             = 0;
    inst->threads.threads                = 0;
    inst->threads.can_create             = 0;
    inst->threads.num_threads            = 0;
    inst->threads.max_threads            = KOS_MAX_THREADS;

    init_context(&inst->threads.main_thread, inst);
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

    assert(!kos_is_heap_object(KOS_VOID));
    assert(!kos_is_heap_object(KOS_FALSE));
    assert(!kos_is_heap_object(KOS_TRUE));
    assert(!kos_is_heap_object(KOS_STR_EMPTY));
    assert(KOS_get_string_length(KOS_STR_EMPTY) == 0);
    assert(!kos_is_heap_object(KOS_STR_OUT_OF_MEMORY));
    assert(KOS_get_string_length(KOS_STR_OUT_OF_MEMORY) == 13);

    clear_instance(inst);

    TRY(kos_tls_create(&inst->threads.thread_key));
    error = kos_create_mutex(&inst->threads.ctx_mutex);
    if (error) {
        kos_tls_destroy(inst->threads.thread_key);
        goto cleanup;
    }
    error = kos_create_mutex(&inst->threads.new_mutex);
    if (error) {
        kos_destroy_mutex(&inst->threads.ctx_mutex);
        kos_tls_destroy(inst->threads.thread_key);
        goto cleanup;
    }
    thread_ok = 1;

    TRY(kos_heap_init(inst));
    heap_ok = 1;

    inst->threads.threads = (KOS_ATOMIC(KOS_THREAD *) *)
        kos_malloc(sizeof(KOS_THREAD *) * inst->threads.max_threads);
    if ( ! inst->threads.threads)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    memset((void *)inst->threads.threads, 0, sizeof(KOS_THREAD *) * inst->threads.max_threads);

    TRY(register_thread(inst, &inst->threads.main_thread));

    ctx = &inst->threads.main_thread;

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
    TRY_OBJID(inst->modules.module_names           = KOS_new_object(ctx));
    TRY_OBJID(inst->modules.modules                = KOS_new_array(ctx, 0));
    TRY_OBJID(inst->modules.search_paths           = KOS_new_array(ctx, 0));
    TRY_OBJID(inst->modules.module_inits           = KOS_new_object(ctx));
    TRY_OBJID(inst->args                           = KOS_new_array(ctx, 0));

    init_module = (KOS_MODULE *)kos_alloc_object(ctx, KOS_ALLOC_IMMOVABLE, OBJ_MODULE, sizeof(KOS_MODULE));
    if ( ! init_module)
        RAISE_ERROR(KOS_ERROR_OUT_OF_MEMORY);
    setup_init_module(init_module);

    init_module->inst                    = inst;
    TRY_OBJID(init_module->globals       = KOS_new_array(ctx, 0));
    TRY_OBJID(init_module->global_names  = KOS_new_object(ctx));
    TRY_OBJID(init_module->module_names  = KOS_new_object(ctx));

    inst->modules.init_module = OBJID(MODULE, init_module);

    TRY(init_search_paths(ctx));

    *out_ctx = ctx;

    /* Set user flags.
     * Also, enable automatic GC unless disabled by user */
    inst->flags = flags;

    /* Enable creation of new threads */
    inst->threads.can_create = 1U;

cleanup:
    if (error) {
        if (heap_ok)
            kos_heap_destroy(inst);

        if (thread_ok) {
            kos_tls_destroy(inst->threads.thread_key);
            kos_destroy_mutex(&inst->threads.new_mutex);
            kos_destroy_mutex(&inst->threads.ctx_mutex);
        }

        if (inst->threads.threads)
            kos_free((void *)inst->threads.threads);
    }

    inst->threads.main_thread.retval = KOS_BADPTR;

    return error;
}

void KOS_instance_destroy(KOS_INSTANCE *inst)
{
    int         error;
    uint32_t    i;
    uint32_t    num_modules = KOS_get_array_size(inst->modules.modules);
    KOS_CONTEXT ctx         = &inst->threads.main_thread;

    KOS_instance_validate(ctx);

    error = kos_join_finished_threads(ctx, KOS_JOIN_ALL);

    if (error == KOS_ERROR_EXCEPTION)
        KOS_print_exception(ctx, KOS_STDERR);
    else if (error) {
        assert(error == KOS_ERROR_OUT_OF_MEMORY);
        fprintf(stderr, "Out of memory\n");
    }

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

    kos_destroy_mutex(&inst->threads.new_mutex);
    kos_destroy_mutex(&inst->threads.ctx_mutex);

    kos_free((void *)inst->threads.threads);

    clear_instance(inst);

#ifdef CONFIG_PERF
#   define PERF_RATIO(a) do {                                                  \
        const uint32_t va = KOS_atomic_read_relaxed_u32(kos_perf.a##_success); \
        const uint32_t vb = KOS_atomic_read_relaxed_u32(kos_perf.a##_fail);    \
        uint32_t       total = va + vb;                                        \
        if (total == 0) total = 1;                                             \
        fprintf(stderr, "    " #a "\t%u / %u (%u%%)\n",                        \
                va, total, va * 100 / total);                                  \
    } while (0)
#   define PERF_VALUE_NAME(name, a) do {                                       \
        const uint32_t va = KOS_atomic_read_relaxed_u32(kos_perf.a);           \
        fprintf(stderr, "    " #name "\t%u\n", va);                            \
    } while (0)
#   define PERF_VALUE(a) PERF_VALUE_NAME(a, a)
    fprintf(stderr, "Performance stats:\n");
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
    PERF_VALUE(gc_cycles);
    PERF_VALUE_NAME(alloc_object_32,        alloc_object_size[0]);
    PERF_VALUE_NAME(alloc_object_64_128,    alloc_object_size[1]);
    PERF_VALUE_NAME(alloc_object_256_512,   alloc_object_size[2]);
    PERF_VALUE_NAME(alloc_object_1024_plus, alloc_object_size[3]);
    PERF_VALUE_NAME(evac_object_32,         evac_object_size[0]);
    PERF_VALUE_NAME(evac_object_64_128,     evac_object_size[1]);
    PERF_VALUE_NAME(evac_object_256_512,    evac_object_size[2]);
    PERF_VALUE_NAME(evac_object_1024_plus,  evac_object_size[3]);
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

    kos_track_refs(ctx, 1, &path_str);
    TRY(KOS_array_resize(ctx, inst->modules.search_paths, len+1));
    kos_untrack_refs(ctx, 1);

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
                                                            KOS_ALLOC_MOVABLE,
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

    assert(GET_OBJ_TYPE_GC_SAFE(exception_obj) <= OBJ_LAST_TYPE ||
           GET_OBJ_TYPE_GC_SAFE(exception_obj) == OBJ_DYNAMIC_PROP);

#ifdef CONFIG_MAD_GC
    if ( ! kos_gc_active(ctx)) {
        kos_track_refs(ctx, 1, &exception_obj);
        (void)kos_trigger_mad_gc(ctx);
        kos_untrack_refs(ctx, 1);
    }
#endif

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
    int        pushed = 0;
    unsigned   i;
    unsigned   depth;
    KOS_OBJ_ID value;
    KOS_OBJ_ID backtrace;
    KOS_OBJ_ID array      = KOS_BADPTR;
    KOS_OBJ_ID frame_desc = KOS_BADPTR;
    KOS_OBJ_ID str;
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    value = KOS_get_property(ctx, exception, KOS_STR_VALUE);
    TRY_OBJID(value);

    backtrace = KOS_get_property(ctx, exception, KOS_STR_BACKTRACE);
    TRY_OBJID(backtrace);

    if (GET_OBJ_TYPE(backtrace) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    TRY(KOS_push_locals(ctx, &pushed, 4, &value, &backtrace, &array, &frame_desc));

    depth = KOS_get_array_size(backtrace);
    array = KOS_new_array(ctx, 1 + depth);
    TRY_OBJID(array);

    if (kos_vector_reserve(&cstr, 80) != KOS_SUCCESS) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    TRY(kos_append_cstr(ctx, &cstr, str_format_exception, sizeof(str_format_exception) - 1));
    TRY(KOS_object_to_string_or_cstr_vec(ctx, value, KOS_DONT_QUOTE, 0, &cstr));

    str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
    TRY_OBJID(str);

    TRY(KOS_array_write(ctx, array, 0, str));

    for (i = 0; i < depth; i++) {

        char     cbuf[16];
        unsigned len;

        frame_desc = KOS_array_read(ctx, backtrace, (int)i);
        TRY_OBJID(frame_desc);

        cstr.size = 0;

        TRY(kos_append_cstr(ctx, &cstr, str_format_hash,
                            sizeof(str_format_hash) - 1));

        len = (unsigned)snprintf(cbuf, sizeof(cbuf), "%u", i);
        TRY(kos_append_cstr(ctx, &cstr, cbuf, KOS_min(len, (unsigned)(sizeof(cbuf) - 1))));

        TRY(kos_append_cstr(ctx, &cstr, str_format_offset,
                            sizeof(str_format_offset) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_STR_OFFSET);
        TRY_OBJID(str);
        if (IS_SMALL_INT(str)) {
            len = (unsigned)snprintf(cbuf, sizeof(cbuf), "0x%X",
                                     (unsigned)GET_SMALL_INT(str));
            TRY(kos_append_cstr(ctx, &cstr, cbuf, KOS_min(len, (unsigned)(sizeof(cbuf) - 1))));
        }
        else
            TRY(kos_append_cstr(ctx, &cstr, str_format_question_marks,
                                sizeof(str_format_question_marks) - 1));

        TRY(kos_append_cstr(ctx, &cstr, str_format_function,
                            sizeof(str_format_function) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_STR_FUNCTION);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_module,
                            sizeof(str_format_module) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_STR_FILE);
        TRY_OBJID(str);
        str = KOS_get_file_name(ctx, str);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_line,
                            sizeof(str_format_line) - 1));

        str = KOS_get_property(ctx, frame_desc, KOS_STR_LINE);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
        TRY_OBJID(str);

        TRY(KOS_array_write(ctx, array, 1+(int)i, str));
    }

cleanup:
    kos_vector_destroy(&cstr);

    KOS_pop_locals(ctx, pushed);

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

    assert( ! kos_gc_active(ctx));
    assert( ! IS_BAD_PTR(local_refs));
    assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

    num_tracked = OBJPTR(LOCAL_REFS, local_refs)->num_tracked;

    return (num_tracked + (unsigned)num_entries) <=
           (sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs) /
            sizeof(OBJPTR(LOCAL_REFS, local_refs)->refs[0]));
}

static int reserve_locals(KOS_CONTEXT ctx, int num_entries)
{
    if (have_room_for_locals(ctx, num_entries))
        return KOS_SUCCESS;

    return push_local_refs_object(ctx);
}

int KOS_push_local_scope(KOS_CONTEXT ctx, KOS_OBJ_ID *prev_scope)
{
    KOS_OBJ_ID local_refs  = ctx->local_refs;
    KOS_OBJ_ID prev_locals;
    int        error;

    *prev_scope = KOS_BADPTR;

    if (IS_BAD_PTR(local_refs)) {

        prev_locals = TO_SMALL_INT(KOS_LOOK_FURTHER);

        error = push_local_refs_object(ctx);

        if (error)
            return error;
    }
    else
        prev_locals = TO_SMALL_INT(OBJPTR(LOCAL_REFS, local_refs)->prev_scope);

    error = reserve_locals(ctx, 1);

    if ( ! error) {

        local_refs = ctx->local_refs;

        assert( ! kos_gc_active(ctx));
        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        OBJPTR(LOCAL_REFS, local_refs)->refs[
            OBJPTR(LOCAL_REFS, local_refs)->num_tracked++]
                = prev_scope;

        *prev_scope = prev_locals;

        OBJPTR(LOCAL_REFS, local_refs)->prev_scope =
            OBJPTR(LOCAL_REFS, local_refs)->num_tracked - 1;
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

    assert( ! kos_gc_active(ctx));
    assert( ! IS_BAD_PTR(local_refs));

    for (;;) {

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->prev_scope;

        if (num_tracked != KOS_LOOK_FURTHER)
            break;

        local_refs = OBJPTR(LOCAL_REFS, local_refs)->next;

        assert( ! IS_BAD_PTR(local_refs));
    }

    assert(prev_scope == OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked]);

    prev_scope_idx = *(OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked]);

    assert(IS_SMALL_INT(prev_scope_idx));

    OBJPTR(LOCAL_REFS, local_refs)->num_tracked = num_tracked;
    OBJPTR(LOCAL_REFS, local_refs)->prev_scope  = (uint8_t)GET_SMALL_INT(prev_scope_idx);

    assert(num_tracked ||
           ! IS_BAD_PTR(OBJPTR(LOCAL_REFS, local_refs)->next) ||
           GET_SMALL_INT(prev_scope_idx) == KOS_LOOK_FURTHER);

    assert( ! kos_gc_active(ctx));
    ctx->local_refs = local_refs;
}

#define IS_VALID(obj_id) (IS_BAD_PTR(obj_id) || GET_OBJ_TYPE(obj_id) <= OBJ_LAST)

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

        assert( ! kos_gc_active(ctx));
        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->num_tracked;
        ptr         = &OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked];
        end         = ptr + num_entries;

        do {
            KOS_OBJ_ID *const obj_ptr = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
            assert(IS_VALID(*obj_ptr));
            *(ptr++) = obj_ptr;
        } while (ptr < end);

        OBJPTR(LOCAL_REFS, local_refs)->num_tracked = (uint8_t)(num_tracked + num_entries);
    }
    else {
        KOS_OBJ_ID **helper_refs = &ctx->helper_refs[0];
        KOS_OBJ_ID **end         = helper_refs + num_entries;

        assert( ! kos_gc_active(ctx));
        assert(ctx->helper_ref_count == 0);

        do {
            KOS_OBJ_ID *const obj_ptr = (KOS_OBJ_ID *)va_arg(args, KOS_OBJ_ID *);
            assert(IS_VALID(*obj_ptr));
            *(helper_refs++) = obj_ptr;
        } while (helper_refs < end);

        ctx->helper_ref_count = num_entries;

        error = reserve_locals(ctx, num_entries);

        if ( ! error) {
            const KOS_OBJ_ID local_refs = ctx->local_refs;
            uint8_t          num_tracked;
            KOS_OBJ_ID     **ptr;

            assert( ! kos_gc_active(ctx));
            assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

            helper_refs = &ctx->helper_refs[0];
            num_tracked = OBJPTR(LOCAL_REFS, local_refs)->num_tracked;
            ptr         = &OBJPTR(LOCAL_REFS, local_refs)->refs[num_tracked];
            end         = ptr + num_entries;

            do
                *(ptr++) = *(helper_refs++);
            while (ptr < end);

            OBJPTR(LOCAL_REFS, local_refs)->num_tracked = (uint8_t)(num_tracked + num_entries);
        }

        ctx->helper_ref_count = 0;
    }

    va_end(args);

    if ( ! error) {
        *push_status = num_entries;
        error = kos_trigger_mad_gc(ctx);
    }

    return error;
}

void KOS_pop_locals(KOS_CONTEXT ctx, int push_status)
{
    if (push_status) {
        KOS_OBJ_ID local_refs = ctx->local_refs;
        unsigned   num_tracked;

        assert( ! kos_gc_active(ctx));
        assert( ! IS_BAD_PTR(local_refs));
        assert(GET_OBJ_TYPE(local_refs) == OBJ_LOCAL_REFS);

        num_tracked = OBJPTR(LOCAL_REFS, local_refs)->num_tracked;

        if ( ! num_tracked) {

            local_refs = OBJPTR(LOCAL_REFS, local_refs)->next;

            assert( ! IS_BAD_PTR(local_refs));

            ctx->local_refs = local_refs;

            num_tracked = OBJPTR(LOCAL_REFS, local_refs)->num_tracked;

            assert(num_tracked > 0);

            assert(OBJPTR(LOCAL_REFS, local_refs)->prev_scope == KOS_LOOK_FURTHER ||
                   OBJPTR(LOCAL_REFS, local_refs)->prev_scope + 1U < num_tracked);
        }

        assert(num_tracked >= (unsigned)push_status);

        OBJPTR(LOCAL_REFS, local_refs)->num_tracked = (uint8_t)(num_tracked - push_status);
    }
}

void kos_track_refs(KOS_CONTEXT ctx, int num_entries, ...)
{
    va_list  args;
    uint32_t i;
    uint32_t end;

#ifdef CONFIG_MAD_GC
    int run_mad_gc = 1;
    if (num_entries == TRACK_ONE_REF) {
        run_mad_gc  = 0;
        num_entries = 1;
    }
#endif

    assert(num_entries > 0);
    assert( ! kos_gc_active(ctx));
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

#ifdef CONFIG_MAD_GC
    if (run_mad_gc)
        if (kos_trigger_mad_gc(ctx))
            /* Ignore stray exception from another thread or GC failure */
            KOS_clear_exception(ctx);
#endif
}

void kos_untrack_refs(KOS_CONTEXT ctx, int num_entries)
{
    assert( ! kos_gc_active(ctx));
    assert(num_entries > 0 && (unsigned)num_entries <= ctx->tmp_ref_count);

    ctx->tmp_ref_count -= num_entries;
}

void KOS_init_local(KOS_CONTEXT ctx, KOS_LOCAL *local)
{
    KOS_LOCAL *next = ctx->local_list;

    local->next     = next;
    local->o        = KOS_BADPTR;
    ctx->local_list = local;
}

void KOS_init_locals(KOS_CONTEXT ctx, int num_locals, ...)
{
    va_list     args;
    KOS_LOCAL  *head = 0;
    KOS_LOCAL **prev = &head;

    assert(num_locals);

    va_start(args, num_locals);

    for ( ; num_locals; --num_locals) {
        KOS_LOCAL *local = (KOS_LOCAL *)va_arg(args, KOS_LOCAL *);

        *prev = local;
        prev  = &local->next;

        local->o = KOS_BADPTR;
    }

    *prev           = ctx->local_list;
    ctx->local_list = head;

    va_end(args);
}

void KOS_destroy_local(KOS_CONTEXT ctx, KOS_LOCAL *local)
{
    KOS_LOCAL **prev_next;

    if ( ! ctx)
        return;

    prev_next = &ctx->local_list;

    for (;;) {
        KOS_LOCAL *next_ptr = *prev_next;

        assert(next_ptr);

        if (next_ptr == local)
            break;

        prev_next = &next_ptr->next;
    }

    *prev_next = local->next;

#ifndef NDEBUG
    local->next = 0;
    local->o    = KOS_BADPTR;
#endif
}

void KOS_destroy_locals(KOS_CONTEXT ctx, int num_locals, KOS_LOCAL *local)
{
    KOS_LOCAL  *next;
    KOS_LOCAL **prev_next = &ctx->local_list;

    assert(num_locals);

    for (;;) {
        KOS_LOCAL *next_ptr = *prev_next;

        assert(next_ptr);

        if (next_ptr == local)
            break;

        prev_next = &next_ptr->next;
    }

    for (;;) {
        --num_locals;
        next = local->next;

#ifndef NDEBUG
        local->next = 0;
        local->o    = KOS_BADPTR;
#endif

        if ( ! num_locals)
            break;

        local = next;
    }

    *prev_next = next;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
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

struct KOS_LIB_LIST_S {
    uint32_t       num_libs;
    uint32_t       capacity;
    KOS_SHARED_LIB libs[1];
};

#ifdef CONFIG_PERF
struct KOS_PERF_S kos_perf = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 },
    0, 0,
    0, 0, 0, 0, 0, 0, 0,
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
    0
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

static void init_context(KOS_CONTEXT ctx, KOS_INSTANCE *inst)
{
    ctx->next        = 0;
    ctx->prev        = 0;
    ctx->gc_state    = GC_SUSPENDED;
    ctx->inst        = inst;
    ctx->cur_page    = 0;
    ctx->thread_obj  = KOS_BADPTR;
    ctx->exception   = KOS_BADPTR;
    ctx->retval      = KOS_BADPTR;
    ctx->stack       = KOS_BADPTR;
    ctx->regs_idx    = 0;
    ctx->stack_depth = 0;
    ctx->local_list  = 0;
    ctx->ulocal_list = 0;
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

cleanup:
    if (error)
        kos_heap_release_thread_page(ctx);

    return error;
}

static void unregister_thread(KOS_INSTANCE *inst,
                              KOS_CONTEXT   ctx)
{
    assert(ctx != &inst->threads.main_thread);

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
    inst->modules.libs                   = 0;
    inst->modules.load_chain             = 0;
    inst->threads.threads                = 0;
    inst->threads.can_create             = 0;
    inst->threads.num_threads            = 0;
    inst->threads.max_threads            = KOS_MAX_THREADS;

    init_context(&inst->threads.main_thread, inst);
}

#ifdef CONFIG_FUZZ
KOS_ATOMIC(uint32_t) kos_fuzz_instructions;
#endif

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

#ifdef CONFIG_FUZZ
    KOS_atomic_write_relaxed_u32(kos_fuzz_instructions, 0U);
#endif

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

    if (inst->modules.libs) {
        uint32_t      idx;
        KOS_LIB_LIST *libs = inst->modules.libs;

        for (idx = libs->num_libs; idx > 0; ) {
            const KOS_SHARED_LIB lib = libs->libs[--idx];

            assert(lib);
            kos_unload_library(lib);
        }

        kos_free((void *)libs);
    }

    kos_tls_destroy(inst->threads.thread_key);

    kos_destroy_mutex(&inst->threads.new_mutex);
    kos_destroy_mutex(&inst->threads.ctx_mutex);

    kos_free((void *)inst->threads.threads);

    clear_instance(inst);

#ifdef CONFIG_PERF
#   define PERF_RATIO(a) do {                                                      \
        const uint64_t va = KOS_atomic_read_relaxed_u64(kos_perf.a##_success);     \
        const uint64_t vb = KOS_atomic_read_relaxed_u64(kos_perf.a##_fail);        \
        uint64_t       total = va + vb;                                            \
        if (total == 0) total = 1;                                                 \
        fprintf(stderr, "    " #a "\t%" PRIu64 " / %" PRIu64 " (%" PRIu64 "%%)\n", \
                va, total, va * 100 / total);                                      \
    } while (0)
#   define PERF_VALUE_NAME(name, a) do {                             \
        const uint64_t va = KOS_atomic_read_relaxed_u64(kos_perf.a); \
        fprintf(stderr, "    " #name "\t%" PRIu64 " \n", va);        \
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
    PERF_VALUE_NAME(alloc_object_32,        alloc_object_size[0]);
    PERF_VALUE_NAME(alloc_object_64_128,    alloc_object_size[1]);
    PERF_VALUE_NAME(alloc_object_160_256,   alloc_object_size[2]);
    PERF_VALUE_NAME(alloc_object_288_512,   alloc_object_size[3]);
    PERF_VALUE(alloc_huge_object);
    PERF_VALUE(non_full_seek);
    PERF_VALUE(non_full_seek_max);
    PERF_VALUE(alloc_new_page);
    PERF_VALUE(alloc_free_page);
    PERF_VALUE(gc_cycles);
    PERF_VALUE_NAME(evac_object_32,         evac_object_size[0]);
    PERF_VALUE_NAME(evac_object_64_128,     evac_object_size[1]);
    PERF_VALUE_NAME(evac_object_160_256,    evac_object_size[2]);
    PERF_VALUE_NAME(evac_object_288_512,    evac_object_size[3]);
    PERF_VALUE(instructions);
#endif
}

int KOS_instance_add_path(KOS_CONTEXT ctx, const char *module_search_path)
{
    int           error;
    KOS_OBJ_ID    path_str;
    KOS_INSTANCE *inst = ctx->inst;

    path_str = KOS_new_cstring(ctx, module_search_path);
    TRY_OBJID(path_str);

    TRY(KOS_array_push(ctx, inst->modules.search_paths, path_str, 0));

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

static int save_module_lib(KOS_CONTEXT ctx, KOS_SHARED_LIB lib)
{
    KOS_LIB_LIST *libs;

    if ( ! lib)
        return KOS_SUCCESS;

    libs = ctx->inst->modules.libs;

    if ( ! libs) {
        libs = (KOS_LIB_LIST *)kos_malloc(sizeof(KOS_LIB_LIST) + 7 * sizeof(KOS_SHARED_LIB));

        if ( ! libs)
            return KOS_ERROR_OUT_OF_MEMORY;

        libs->num_libs = 0;
        libs->capacity = 8;

        ctx->inst->modules.libs = libs;
    }

    if (libs->num_libs >= libs->capacity) {
        const uint32_t new_capacity = libs->capacity + 8;
        KOS_LIB_LIST  *new_libs     = (KOS_LIB_LIST *)kos_realloc(libs,
                sizeof(KOS_LIB_LIST) + (new_capacity - 1) * sizeof(KOS_SHARED_LIB));

        if ( ! new_libs)
            return KOS_ERROR_OUT_OF_MEMORY;

        libs                    = new_libs;
        libs->capacity          = new_capacity;
        ctx->inst->modules.libs = libs;
    }

    libs->libs[libs->num_libs++] = lib;

    return KOS_SUCCESS;
}

KOS_OBJ_ID kos_register_module_init(KOS_CONTEXT      ctx,
                                    KOS_OBJ_ID       module_name_obj,
                                    KOS_SHARED_LIB   lib,
                                    KOS_BUILTIN_INIT init)
{
    struct KOS_MODULE_INIT_S *mod_init_ptr;
    KOS_INSTANCE       *const inst  = ctx->inst;
    KOS_LOCAL                 module_name;
    KOS_LOCAL                 mod_init;

    KOS_init_local_with(ctx, &module_name, module_name_obj);
    KOS_init_local(     ctx, &mod_init);

    mod_init_ptr = (struct KOS_MODULE_INIT_S *)kos_alloc_object(ctx,
                                                                KOS_ALLOC_MOVABLE,
                                                                OBJ_OPAQUE,
                                                                sizeof(struct KOS_MODULE_INIT_S));

    if ( ! mod_init_ptr)
        goto cleanup;

    mod_init_ptr->lib  = lib;
    mod_init_ptr->init = init;

    mod_init.o = OBJID(OPAQUE, (KOS_OPAQUE *)mod_init_ptr);

    if (KOS_set_property(ctx,
                         inst->modules.module_inits,
                         module_name.o,
                         mod_init.o) == KOS_SUCCESS) {

        if (save_module_lib(ctx, lib) != KOS_SUCCESS) {
            assert( ! KOS_is_exception_pending(ctx));

            mod_init_ptr = (struct KOS_MODULE_INIT_S *)OBJPTR(OPAQUE, mod_init.o);
            mod_init_ptr->lib  = 0;
            mod_init_ptr->init = 0;

            mod_init.o = KOS_BADPTR;
        }
    }
    else
        mod_init.o = KOS_BADPTR;

cleanup:
    if (IS_BAD_PTR(mod_init.o))
        kos_unload_library(lib);

    return KOS_destroy_top_locals(ctx, &mod_init, &module_name);
}

int KOS_instance_register_builtin(KOS_CONTEXT      ctx,
                                  const char      *module,
                                  KOS_BUILTIN_INIT init)
{
    KOS_OBJ_ID module_name = KOS_new_cstring(ctx, module);

    if (IS_BAD_PTR(module_name))
        return KOS_ERROR_EXCEPTION;

    return IS_BAD_PTR(kos_register_module_init(ctx, module_name, 0, init))
           ? KOS_ERROR_EXCEPTION : KOS_SUCCESS;
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
        KOS_LOCAL saved_exception;

        KOS_init_local_with(ctx, &saved_exception, exception_obj);
        (void)kos_trigger_mad_gc(ctx);
        exception_obj = KOS_destroy_top_local(ctx, &saved_exception);
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
    unsigned   i;
    unsigned   depth;
    KOS_LOCAL  value;
    KOS_LOCAL  backtrace;
    KOS_LOCAL  frame_desc;
    KOS_LOCAL  array;
    KOS_OBJ_ID str;
    KOS_VECTOR cstr;

    kos_vector_init(&cstr);

    KOS_init_locals(ctx, 4, &value, &backtrace, &frame_desc, &array);

    value.o = KOS_get_property(ctx, exception, KOS_STR_VALUE);
    TRY_OBJID(value.o);

    backtrace.o = KOS_get_property(ctx, exception, KOS_STR_BACKTRACE);
    TRY_OBJID(backtrace.o);

    if (GET_OBJ_TYPE(backtrace.o) != OBJ_ARRAY)
        RAISE_EXCEPTION(str_err_not_array);

    depth   = KOS_get_array_size(backtrace.o);
    array.o = KOS_new_array(ctx, 1 + depth);
    TRY_OBJID(array.o);

    if (kos_vector_reserve(&cstr, 80) != KOS_SUCCESS) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    TRY(kos_append_cstr(ctx, &cstr, str_format_exception, sizeof(str_format_exception) - 1));
    TRY(KOS_object_to_string_or_cstr_vec(ctx, value.o, KOS_DONT_QUOTE, 0, &cstr));

    str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
    TRY_OBJID(str);

    TRY(KOS_array_write(ctx, array.o, 0, str));

    for (i = 0; i < depth; i++) {

        char     cbuf[16];
        unsigned len;

        frame_desc.o = KOS_array_read(ctx, backtrace.o, (int)i);
        TRY_OBJID(frame_desc.o);

        cstr.size = 0;

        TRY(kos_append_cstr(ctx, &cstr, str_format_hash,
                            sizeof(str_format_hash) - 1));

        len = (unsigned)snprintf(cbuf, sizeof(cbuf), "%u", i);
        TRY(kos_append_cstr(ctx, &cstr, cbuf, KOS_min(len, (unsigned)(sizeof(cbuf) - 1))));

        TRY(kos_append_cstr(ctx, &cstr, str_format_offset,
                            sizeof(str_format_offset) - 1));

        str = KOS_get_property(ctx, frame_desc.o, KOS_STR_OFFSET);
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

        str = KOS_get_property(ctx, frame_desc.o, KOS_STR_FUNCTION);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_module,
                            sizeof(str_format_module) - 1));

        str = KOS_get_property(ctx, frame_desc.o, KOS_STR_FILE);
        TRY_OBJID(str);
        str = KOS_get_file_name(ctx, str);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        TRY(kos_append_cstr(ctx, &cstr, str_format_line,
                            sizeof(str_format_line) - 1));

        str = KOS_get_property(ctx, frame_desc.o, KOS_STR_LINE);
        TRY_OBJID(str);
        TRY(KOS_object_to_string_or_cstr_vec(ctx, str, KOS_DONT_QUOTE, 0, &cstr));

        str = KOS_new_string(ctx, cstr.buffer, (unsigned)(cstr.size - 1));
        TRY_OBJID(str);

        TRY(KOS_array_write(ctx, array.o, 1+(int)i, str));
    }

cleanup:
    array.o = KOS_destroy_top_locals(ctx, &value, &array);

    kos_vector_destroy(&cstr);

    return error ? KOS_BADPTR : array.o;
}

void KOS_raise_generator_end(KOS_CONTEXT ctx)
{
    KOS_INSTANCE *const inst = ctx->inst;

    const KOS_OBJ_ID exception =
            KOS_new_object_with_prototype(ctx, inst->prototypes.generator_end_proto);

    if ( ! IS_BAD_PTR(exception))
        KOS_raise_exception(ctx, exception);
}

#ifdef NDEBUG
#define check_local_list(ctx, local) ((void)0)
#else
static void check_local_list(KOS_LOCAL *list, KOS_LOCAL *local)
{
    while (list) {
        assert(list != local);
        list = list->next;
    }
}
#endif

void KOS_init_local_with(KOS_CONTEXT ctx, KOS_LOCAL *local, KOS_OBJ_ID obj_id)
{
    KOS_LOCAL *next = ctx->local_list;

    check_local_list(next, local);

    local->next     = next;
    local->o        = obj_id;
    ctx->local_list = local;
}

void KOS_init_ulocal(KOS_CONTEXT ctx, KOS_ULOCAL *local)
{
    KOS_ULOCAL *next = ctx->ulocal_list;

    check_local_list((KOS_LOCAL *)next, (KOS_LOCAL *)local);

    local->next      = next;
    local->prev      = 0;
    local->o         = KOS_BADPTR;
    ctx->ulocal_list = local;
    if (next)
        next->prev   = local;
}

void KOS_init_locals(KOS_CONTEXT ctx, int num_locals, ...)
{
    va_list     args;
    KOS_LOCAL  *head     = 0;
    KOS_LOCAL **tail_ptr = &head;

    assert(num_locals);

    va_start(args, num_locals);

    for ( ; num_locals; --num_locals) {
        KOS_LOCAL *local = (KOS_LOCAL *)va_arg(args, KOS_LOCAL *);

        check_local_list(head, local);
        check_local_list(ctx->local_list, local);

        *tail_ptr = local;
        tail_ptr  = &local->next;

        local->o = KOS_BADPTR;
#ifndef NDEBUG
        local->next = 0;
#endif
    }

    *tail_ptr       = ctx->local_list;
    ctx->local_list = head;

    va_end(args);
}

KOS_OBJ_ID KOS_destroy_ulocal(KOS_CONTEXT ctx, KOS_ULOCAL *local)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    if (ctx) {
        KOS_ULOCAL *prev = local->prev;
        KOS_ULOCAL *next = local->next;

        KOS_ULOCAL **prev_hookup = prev ? &prev->next : &ctx->ulocal_list;

        assert(ctx->ulocal_list);
        if (prev) {
            assert(ctx->ulocal_list != local);
        }
        else {
            assert(ctx->ulocal_list == local);
        }

        *prev_hookup = next;
        if (next)
            next->prev = prev;

        ret = local->o;

#ifndef NDEBUG
        local->next = 0;
        local->prev = 0;
        local->o    = KOS_BADPTR;
#endif
    }

    return ret;
}

KOS_OBJ_ID KOS_destroy_top_local(KOS_CONTEXT ctx, KOS_LOCAL *local)
{
    KOS_OBJ_ID ret = KOS_BADPTR;

    assert(ctx->local_list == local);

    ctx->local_list = local->next;

    ret = local->o;

#ifndef NDEBUG
    local->next = 0;
    local->o    = KOS_BADPTR;
#endif

    return ret;
}

KOS_OBJ_ID KOS_destroy_top_locals(KOS_CONTEXT ctx, KOS_LOCAL *first, KOS_LOCAL *last)
{
    KOS_LOCAL **hookup = &ctx->local_list;
    KOS_OBJ_ID  ret    = last->o;

    assert(ctx->local_list == first);

    *hookup = last->next;

#ifndef NDEBUG
    for (;;) {
        KOS_LOCAL *next = first->next;

        first->next = 0;
        first->o    = KOS_BADPTR;

        if (first == last)
            break;

        first = next;
    }
#endif

    return ret;
}

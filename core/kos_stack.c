/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_bytecode.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "kos_config.h"
#include "kos_heap.h"
#include "kos_math.h"
#include "kos_object_internal.h"
#include "kos_try.h"

KOS_DECLARE_STATIC_CONST_STRING(str_err_not_callable,   "object is not callable");
KOS_DECLARE_STATIC_CONST_STRING(str_err_stack_overflow, "stack overflow");
KOS_DECLARE_STATIC_CONST_STRING(str_module,             "module");
KOS_DECLARE_STATIC_CONST_STRING(str_xbuiltinx,          "<builtin>");

static int push_new_stack(KOS_CONTEXT ctx);

static int unchain_reentrant_frame(KOS_CONTEXT ctx)
{
    KOS_OBJ_ID old_stack = ctx->stack;

    if (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->flags) & KOS_REENTRANT_STACK) {

        KOS_OBJ_ID gen_stack = old_stack;
        uint32_t   idx;

        assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->size) > 0);

        old_stack = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, old_stack)->buf[0]);

        assert( ! IS_BAD_PTR(old_stack));

        idx = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->size);

        if (idx == OBJPTR(STACK, old_stack)->capacity) {

            int error;

            ctx->stack = old_stack;

            assert( ! kos_is_heap_object(gen_stack));

            error = push_new_stack(ctx);

            if (error) {
                ctx->stack = gen_stack;
                return error;
            }

            old_stack = ctx->stack;
            idx       = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->size);

            KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, gen_stack)->buf[0], old_stack);
        }

        assert(GET_OBJ_TYPE(gen_stack) == OBJ_STACK);

        assert(old_stack == OBJPTR(STACK, gen_stack)->buf[0]);
        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, old_stack)->buf[idx], gen_stack);
        KOS_atomic_write_relaxed_u32(OBJPTR(STACK, old_stack)->size,     idx + 1);

        ctx->stack = old_stack;
    }

    return KOS_SUCCESS;
}

static int chain_stack_frame(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  stack)
{
    const int error = unchain_reentrant_frame(ctx);

    if ( ! error) {

        const KOS_OBJ_ID old_stack = ctx->stack;

        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[0], old_stack);

        ctx->stack = stack;

        ctx->stack_depth += KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    }

    return error;
}

static int init_stack(KOS_CONTEXT ctx,
                      KOS_STACK  *stack)
{
    int error = stack ? KOS_SUCCESS : KOS_ERROR_EXCEPTION;

    if (stack) {
        uint8_t *const begin = (uint8_t *)stack;
        uint8_t *const end   = begin + kos_get_object_size(stack->header);
        uint8_t *const buf   = (uint8_t *)&stack->buf[0];

        stack->capacity  = (uint32_t)(end - buf) / sizeof(KOS_OBJ_ID);
        stack->yield_reg = 0xFFU;
        KOS_atomic_write_relaxed_u32(stack->size, 1);

        if ( ! IS_BAD_PTR(ctx->stack))
            error = chain_stack_frame(ctx, OBJID(STACK, stack));
        else {
            ctx->stack = OBJID(STACK, stack);
            KOS_atomic_write_relaxed_ptr(stack->buf[0], KOS_BADPTR);
        }
    }

    return error;
}

static int push_new_stack(KOS_CONTEXT ctx)
{
    KOS_STACK *const new_stack = (KOS_STACK *)
        kos_alloc_object(ctx, KOS_ALLOC_IMMOVABLE, OBJ_STACK, KOS_STACK_OBJ_SIZE);

    if (new_stack)
        KOS_atomic_write_relaxed_u32(new_stack->flags, KOS_NORMAL_STACK);

    return init_stack(ctx, new_stack);
}

static int push_new_reentrant_stack(KOS_CONTEXT ctx,
                                    unsigned    room)
{
    int              error;
    /* KOS_STACK already has one element in the stack buffer, we use this
     * as a pointer to previous/parent stack object.  Additional space is
     * allocated to accommodate requested number of entries.
     */
    const size_t     alloc_size = sizeof(KOS_STACK) + sizeof(KOS_OBJ_ID) * room;
    KOS_STACK *const new_stack  = (KOS_STACK *)kos_alloc_object(ctx,
                                                                KOS_ALLOC_IMMOVABLE,
                                                                OBJ_STACK,
                                                                (uint32_t)alloc_size);
    if (new_stack)
        KOS_atomic_write_relaxed_u32(new_stack->flags, KOS_REENTRANT_STACK);

    assert( ! IS_BAD_PTR(ctx->stack));

    error = init_stack(ctx, new_stack);

    if ( ! error) {
        assert(new_stack->capacity >= room + 1);
    }

    return error;
}

int kos_stack_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  func_obj,
                   uint32_t    num_regs,
                   uint8_t     ret_reg,
                   uint8_t     instr)
{
    KOS_LOCAL        func;
    KOS_STACK       *stack;
    KOS_STACK       *new_stack;
    KOS_STACK_FRAME *stack_frame;
    const int64_t    catch_init = (int64_t)KOS_NO_CATCH << 8;
    int              error      = KOS_SUCCESS;
    uint32_t         stack_size;
    uint32_t         base_idx;
    uint32_t         state;
    unsigned         room;
    int              is_native;
    const KOS_TYPE   type        = GET_OBJ_TYPE(func_obj);

    KOS_init_local_with(ctx, &func, func_obj);

    if (type != OBJ_FUNCTION && type != OBJ_CLASS)
        RAISE_EXCEPTION_STR(str_err_not_callable);

    is_native = KOS_is_native_function(func.o);

    stack      = IS_BAD_PTR(ctx->stack) ? KOS_NULL : OBJPTR(STACK, ctx->stack);
    new_stack  = stack;
    stack_size = stack ? KOS_atomic_read_relaxed_u32(stack->size) : 0;
    base_idx   = stack_size;
    state      = KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state);

    assert((state > KOS_GEN_INIT) || (instr > INSTR_NEXT));

    if (is_native) {
        assert(OBJPTR(FUNCTION, func.o)->opts.num_regs == 0);

        if (state == KOS_GEN_INIT) {
            /* Reserve two additional registers:
             * - One for value passed to yield operator
             * - One for generator state
             */
            num_regs += 2U;
        }
    }
    else {
        num_regs = OBJPTR(FUNCTION, func.o)->opts.num_regs;

        if (ctx->stack_depth + num_regs > KOS_MAX_STACK_DEPTH - KOS_STACK_EXTRA)
            RAISE_EXCEPTION_STR(str_err_stack_overflow);
    }

    room = num_regs + KOS_STACK_EXTRA;

    /* Prepare stack for accommodating new stack frame */

    /* Case 1: Not a generator, no closure */
    if (state < KOS_GEN_INIT && ! (OBJPTR(FUNCTION, func.o)->opts.closure_size)) {

        if ( ! stack || stack_size + room > stack->capacity) {

            KOS_STACK *cur_stack = stack;

            if (stack) {

                TRY(unchain_reentrant_frame(ctx));

                assert( ! IS_BAD_PTR(ctx->stack));
                assert(GET_OBJ_TYPE(ctx->stack) == OBJ_STACK);

                cur_stack = OBJPTR(STACK, ctx->stack);
            }

            if ( ! stack || KOS_atomic_read_relaxed_u32(cur_stack->size) + room > cur_stack->capacity) {
                if ( ! stack || ! is_native)
                    TRY(push_new_stack(ctx));

                if (is_native)
                    TRY(push_new_reentrant_stack(ctx, room));
            }

            new_stack = OBJPTR(STACK, ctx->stack);
            base_idx  = KOS_atomic_read_relaxed_u32(new_stack->size);

            assert(base_idx + room <= new_stack->capacity);
        }
    }
    /* Case 2: Instantiated generator - reuse (push) its stack object */
    else if (state > KOS_GEN_INIT) {

        const KOS_OBJ_ID gen_stack = OBJPTR(FUNCTION, func.o)->generator_stack_frame;
#ifndef NDEBUG
        uint32_t         size;
#endif

        assert( ! IS_BAD_PTR(gen_stack));
        assert(GET_OBJ_TYPE(gen_stack) == OBJ_STACK);
#ifndef NDEBUG
        size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, gen_stack)->size);
#endif
        assert(size > KOS_STACK_EXTRA);

        if (IS_BAD_PTR(ctx->stack))
            TRY(push_new_stack(ctx));

        TRY(chain_stack_frame(ctx, gen_stack));

        assert(IS_SMALL_INT(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, gen_stack)->buf[size - 1])));
        assert(is_native || (uint32_t)GET_SMALL_INT(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, gen_stack)->buf[size - 1])) == num_regs);

        stack_frame = (KOS_STACK_FRAME *)&OBJPTR(STACK, gen_stack)->buf[1];
        stack_frame->call_opcode = instr;
        stack_frame->ret_reg     = ret_reg;

        /* Plus 1, because the first entry is a pointer to previous stack object.
         * Minus 1, because the number of registers is stored after the registers.
         */
        ctx->regs_idx = KOS_STACK_EXTRA;

        goto cleanup;
    }
    /* Case 3: New generator or closure, create new reentrant stack object */
    else {

        if (IS_BAD_PTR(ctx->stack))
            TRY(push_new_stack(ctx));

        TRY(push_new_reentrant_stack(ctx, room));

        OBJPTR(FUNCTION, func.o)->generator_stack_frame = ctx->stack;

        new_stack = OBJPTR(STACK, ctx->stack);
        base_idx  = KOS_atomic_read_relaxed_u32(new_stack->size);
    }

    assert(room == num_regs + KOS_STACK_EXTRA);
    assert(base_idx + room <= new_stack->capacity);

    /* Initialize new stack frame */
    stack_frame = (KOS_STACK_FRAME *)&new_stack->buf[base_idx];
    KOS_atomic_write_relaxed_u32(new_stack->size,         base_idx + room);
    KOS_atomic_write_relaxed_ptr(stack_frame->func_obj,   func.o);
    KOS_atomic_write_relaxed_ptr(stack_frame->catch_info, TO_SMALL_INT((int64_t)catch_init));
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx + room - 1],
                                                          TO_SMALL_INT(num_regs));
    stack_frame->instr_offs  = 0;
    stack_frame->flags       = 0;
    stack_frame->call_opcode = instr;
    stack_frame->ret_reg     = ret_reg;
    stack_frame->zero        = 0;

    ctx->regs_idx = base_idx + KOS_STACK_EXTRA - 1;

    /* Clear registers */
    {
        unsigned idx = 0;

        for ( ; idx < num_regs; idx++)
            KOS_atomic_write_relaxed_ptr(stack_frame->regs[idx], KOS_BADPTR);
    }

    /* TODO add discount for native function arguments in reentrant stack object */
    ctx->stack_depth += room;

cleanup:
    KOS_destroy_top_local(ctx, &func);

    return error;
}

void kos_stack_pop(KOS_CONTEXT ctx)
{
    KOS_STACK *stack;
    uint32_t   size;

    assert( ! IS_BAD_PTR(ctx->stack));
    stack = OBJPTR(STACK, ctx->stack);

    size = KOS_atomic_read_relaxed_u32(stack->size);
    assert(size);

    assert((size == 1 && IS_BAD_PTR(stack->buf[0])) ||
           IS_SMALL_INT(KOS_atomic_read_relaxed_obj(stack->buf[size - 1])));

    if (size > 1) {
        if ( ! (KOS_atomic_read_relaxed_u32(stack->flags) & KOS_REENTRANT_STACK)) {

            const uint32_t num_regs_u = size - ctx->regs_idx - 1U;
            const uint32_t delta      = num_regs_u + KOS_STACK_EXTRA;

            assert(ctx->regs_idx < size);

            assert((int)num_regs_u == GET_SMALL_INT(KOS_atomic_read_relaxed_obj(stack->buf[size - 1])));

            size             -= delta;
            ctx->stack_depth -= delta;

            KOS_atomic_write_relaxed_u32(stack->size, size);
        }
        else {

            const KOS_OBJ_ID new_stack_obj = stack->buf[0];

            assert(size == 1U + KOS_STACK_EXTRA +
                           (uint64_t)GET_SMALL_INT(KOS_atomic_read_relaxed_obj(stack->buf[size - 1])));
            assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);

            ctx->stack_depth -= size;

            stack      = OBJPTR(STACK, new_stack_obj);
            size       = KOS_atomic_read_relaxed_u32(stack->size);
            ctx->stack = new_stack_obj;
        }
    }

    /* If we ran out of stack, go to the previous stack object in the chain */
    while (size == 1) {

        const KOS_OBJ_ID new_stack_obj = KOS_atomic_read_relaxed_obj(stack->buf[0]);

        if (IS_BAD_PTR(new_stack_obj)) {
            size = 0;
            assert(ctx->stack_depth == 0);
            break;
        }

        assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);

        /* TODO save last stack object to reduce unnecessary reallocation */

        stack      = OBJPTR(STACK, new_stack_obj);
        size       = KOS_atomic_read_relaxed_u32(stack->size);
        ctx->stack = new_stack_obj;

        --ctx->stack_depth;
    }

    /* Push previous reentrant frame (generator or closure) on the stack */
    if (size) {

        const KOS_OBJ_ID new_stack_obj = KOS_atomic_read_relaxed_obj(stack->buf[size - 1]);

        if (IS_SMALL_INT(new_stack_obj)) {

            const uint8_t num_regs = (uint8_t)GET_SMALL_INT(new_stack_obj);

            assert(size > KOS_STACK_EXTRA);

            assert(num_regs < (int64_t)size);

            ctx->regs_idx = size - 1U - num_regs;
        }
        else {

            KOS_STACK *new_stack;

            assert( ! (KOS_atomic_read_relaxed_u32(stack->flags) & KOS_REENTRANT_STACK));

            --size;
            KOS_atomic_write_relaxed_u32(stack->size, size);

            assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);
            new_stack = OBJPTR(STACK, new_stack_obj);

            assert(KOS_atomic_read_relaxed_u32(new_stack->flags) & KOS_REENTRANT_STACK);
            size = KOS_atomic_read_relaxed_u32(new_stack->size);
            assert(size > KOS_STACK_EXTRA);

            KOS_atomic_write_relaxed_ptr(new_stack->buf[0], OBJID(STACK, stack));

            ctx->stack    = new_stack_obj;
            /* Plus 1, because the first entry is a pointer to previous stack object.
             * Minus 1, because the number of registers is stored after the registers.
             */
            ctx->regs_idx = KOS_STACK_EXTRA;
        }
    }
    else {
        ctx->regs_idx = 0U;
        ctx->stack    = KOS_BADPTR;
    }
}

typedef int (*KOS_WALK_STACK)(KOS_OBJ_ID stack,
                              uint32_t   frame_idx,
                              uint32_t   frame_size,
                              void      *cookie);

static int walk_stack(KOS_CONTEXT ctx, KOS_WALK_STACK walk, void *cookie)
{
    int        error     = KOS_SUCCESS;
    uint32_t   size;
    uint32_t   prev_size = ~0U;
    KOS_OBJ_ID stack_obj = ctx->stack;

    assert( ! IS_BAD_PTR(stack_obj));
    assert(GET_OBJ_TYPE(stack_obj) == OBJ_STACK);
    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack_obj)->size);

    while (size) {

        const int reentrant = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack_obj)->flags) & KOS_REENTRANT_STACK;

        assert( ! kos_is_heap_object(ctx->stack));

        if (size == 1) {

            const KOS_OBJ_ID prev = stack_obj;

            stack_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack_obj)->buf[0]);
            if (IS_BAD_PTR(stack_obj))
                --size;
            else {
                assert( ! IS_BAD_PTR(stack_obj));
                assert(GET_OBJ_TYPE(stack_obj) == OBJ_STACK);
                assert( ! (KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack_obj)->flags) & KOS_REENTRANT_STACK));

                size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack_obj)->size);

                if (reentrant && prev != ctx->stack) {
                    assert(size > 0);
                    assert(prev_size != ~0U);
                    assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack_obj)->buf[prev_size - 1]) == prev);

                    size      = prev_size - 1;
                    prev_size = ~0U;
                }
            }
        }
        else {

            KOS_OBJ_ID num_regs_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack_obj)->buf[size - 1]);

            if (IS_SMALL_INT(num_regs_obj)) {
                int64_t    num_regs;
                uint32_t   frame_size;

                assert(size > KOS_STACK_EXTRA);

                num_regs = GET_SMALL_INT(num_regs_obj);

                assert(num_regs < (int64_t)size);
                assert(num_regs + KOS_STACK_EXTRA <= (int64_t)size);

                frame_size = (uint32_t)num_regs + KOS_STACK_EXTRA;

                assert( ! reentrant || size == frame_size + 1);

                error = walk(stack_obj,
                             size - frame_size,
                             frame_size,
                             cookie);
                if (error)
                    break;

                size -= frame_size;
            }
            else {

                assert(GET_OBJ_TYPE(num_regs_obj) == OBJ_STACK);
                assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, num_regs_obj)->size) > 0);
                assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, num_regs_obj)->buf[0]) == stack_obj);
                assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, num_regs_obj)->flags) & KOS_REENTRANT_STACK);
                assert( ! reentrant);

                prev_size = size;
                stack_obj = num_regs_obj;
                size      = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack_obj)->size);
            }
        }
    }

    return error;
}

static int get_depth(KOS_OBJ_ID stack,
                     uint32_t   frame_idx,
                     uint32_t   frame_size,
                     void      *cookie)
{
    *(unsigned *)cookie += 1;

    return KOS_SUCCESS;
}

typedef struct KOS_DUMP_CONTEXT_S {
    KOS_CONTEXT ctx;
    uint32_t    idx;
    KOS_LOCAL   backtrace;
} KOS_DUMP_CONTEXT;

static uint32_t get_instr_offs(KOS_STACK_FRAME *stack_frame)
{
    return KOS_atomic_read_relaxed_u32(stack_frame->instr_offs) >> 1;
}

static int dump_stack(KOS_OBJ_ID stack,
                      uint32_t   frame_idx,
                      uint32_t   frame_size,
                      void      *cookie)
{
    KOS_DUMP_CONTEXT *dump_ctx    = (KOS_DUMP_CONTEXT *)cookie;
    KOS_CONTEXT       ctx         = dump_ctx->ctx;
    KOS_STACK_FRAME  *stack_frame = (KOS_STACK_FRAME *)&OBJPTR(STACK, stack)->buf[frame_idx];
    KOS_OBJ_ID        func        = KOS_atomic_read_relaxed_obj(stack_frame->func_obj);
    KOS_OBJ_ID        offs_id;
    intptr_t          instr_offs;
    unsigned          line;
    int               error       = KOS_SUCCESS;
    KOS_LOCAL         module;
    KOS_LOCAL         func_name;
    KOS_LOCAL         module_name;
    KOS_LOCAL         module_path;
    KOS_LOCAL         frame_desc;

    if (KOS_is_native_function(func)) {
        line       = 0;
        instr_offs = (intptr_t)OBJPTR(FUNCTION, func)->handler.handler;
    }
    else {
        instr_offs = get_instr_offs(stack_frame);
        line       = KOS_function_addr_to_line(func, (uint32_t)instr_offs);
    }

    KOS_init_local_with(ctx, &module,      OBJPTR(FUNCTION, func)->module);
    KOS_init_local_with(ctx, &func_name,   OBJPTR(FUNCTION, func)->name);
    KOS_init_local_with(ctx, &module_name, KOS_CONST_ID(str_xbuiltinx));
    KOS_init_local_with(ctx, &module_path, KOS_CONST_ID(str_xbuiltinx));
    KOS_init_local_with(ctx, &frame_desc,  KOS_new_object(ctx));
    TRY_OBJID(frame_desc.o);

    assert(dump_ctx->idx < KOS_get_array_size(dump_ctx->backtrace.o));
    TRY(KOS_array_write(ctx, dump_ctx->backtrace.o, (int)dump_ctx->idx, frame_desc.o));

    if ( ! IS_BAD_PTR(module.o)) {
        module_name.o = OBJPTR(MODULE, module.o)->name;
        module_path.o = OBJPTR(MODULE, module.o)->path;
    }

    TRY(KOS_set_property(ctx, frame_desc.o, KOS_CONST_ID(str_module), module_name.o));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_FILE,             module_path.o));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_LINE,             TO_SMALL_INT((int)line)));
    offs_id = KOS_new_int(ctx, (int64_t)instr_offs);
    TRY_OBJID(offs_id);
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_OFFSET,           offs_id));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_FUNCTION,         func_name.o));

    ++dump_ctx->idx;

cleanup:
    KOS_destroy_top_locals(ctx, &frame_desc, &module);

    return error;
}

void kos_wrap_exception(KOS_CONTEXT ctx)
{
    int                 error        = KOS_SUCCESS;
    unsigned            depth;
    KOS_INSTANCE *const inst         = ctx->inst;
    int                 partial_wrap = 0;
    KOS_DUMP_CONTEXT    dump_ctx;
    KOS_LOCAL           exception;
    KOS_LOCAL           backtrace;
    KOS_LOCAL           thrown_object;

    assert(!IS_BAD_PTR(ctx->exception));

    if (GET_OBJ_TYPE(ctx->exception) == OBJ_OBJECT) {

        const KOS_OBJ_ID proto = KOS_get_prototype(ctx, ctx->exception);

        if (proto == inst->prototypes.exception_proto)
            /* Exception already wrapped */
            return;
    }

    KOS_init_locals(ctx, &exception, &backtrace, &thrown_object, &dump_ctx.backtrace, kos_end_locals);

    thrown_object.o = ctx->exception;

    KOS_clear_exception(ctx);

    exception.o = KOS_new_object_with_prototype(ctx, inst->prototypes.exception_proto);
    TRY_OBJID(exception.o);

    TRY(KOS_set_property(ctx, exception.o, KOS_STR_VALUE, thrown_object.o));

    partial_wrap = 1;

    depth = 0;
    TRY(walk_stack(ctx, get_depth, &depth));

    backtrace.o = KOS_new_array(ctx, depth);
    TRY_OBJID(backtrace.o);

    TRY(KOS_set_property(ctx, exception.o, KOS_STR_BACKTRACE, backtrace.o));

    dump_ctx.ctx         = ctx;
    dump_ctx.idx         = 0;
    dump_ctx.backtrace.o = backtrace.o;

    TRY(walk_stack(ctx, dump_stack, &dump_ctx));

cleanup:
    ctx->exception = partial_wrap ? exception.o : thrown_object.o;

    KOS_destroy_top_locals(ctx, &exception, &dump_ctx.backtrace);
}

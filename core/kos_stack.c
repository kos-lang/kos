/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "kos_config.h"
#include "kos_heap.h"
#include "kos_object_internal.h"
#include "kos_try.h"

static const char str_err_not_callable[]   = "object is not callable";
static const char str_err_stack_overflow[] = "stack overflow";

static int _push_new_stack(KOS_CONTEXT ctx);

static int _unchain_reentrant_frame(KOS_CONTEXT ctx)
{
    KOS_OBJ_ID old_stack = ctx->stack;

    if (OBJPTR(STACK, old_stack)->flags & KOS_REENTRANT_STACK) {

        KOS_OBJ_ID gen_stack = old_stack;
        uint32_t   idx;

        assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->size) > 0);

        old_stack = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, old_stack)->buf[0]);

        assert( ! IS_BAD_PTR(old_stack));

        idx = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, old_stack)->size);

        if (idx == OBJPTR(STACK, old_stack)->capacity) {

            int error;

            ctx->stack = old_stack;

            kos_track_refs(ctx, 1, &gen_stack);

            error = _push_new_stack(ctx);

            kos_untrack_refs(ctx, 1);

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

static int _chain_stack_frame(KOS_CONTEXT ctx,
                              KOS_OBJ_ID  stack)
{
    const int error = _unchain_reentrant_frame(ctx);

    if ( ! error) {

        const KOS_OBJ_ID old_stack = ctx->stack;

        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, stack)->buf[0], old_stack);

        ctx->stack = stack;

        ctx->stack_depth += KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
    }

    return error;
}

static int _init_stack(KOS_CONTEXT ctx,
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
            error = _chain_stack_frame(ctx, OBJID(STACK, stack));
        else {
            ctx->stack = OBJID(STACK, stack);
            KOS_atomic_write_relaxed_ptr(stack->buf[0], KOS_BADPTR);
        }
    }

    return error;
}

static int _push_new_stack(KOS_CONTEXT ctx)
{
    KOS_STACK *const new_stack = (KOS_STACK *)kos_alloc_object_page(ctx,
                                                                    OBJ_STACK);
    if (new_stack)
        new_stack->flags = KOS_NORMAL_STACK;

    return _init_stack(ctx, new_stack);
}

static int _push_new_reentrant_stack(KOS_CONTEXT ctx,
                                     unsigned    room)
{
    const size_t     alloc_size = sizeof(KOS_STACK) + sizeof(KOS_OBJ_ID) * room;
    KOS_STACK *const new_stack  = (KOS_STACK *)kos_alloc_object(ctx,
                                                                OBJ_STACK,
                                                                (uint32_t)alloc_size);
    if (new_stack)
        new_stack->flags = KOS_REENTRANT_STACK;

    assert( ! IS_BAD_PTR(ctx->stack));

    return _init_stack(ctx, new_stack);
}

int kos_stack_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  func_obj)
{
    int            error      = KOS_SUCCESS;
    uint32_t       stack_size;
    uint32_t       base_idx;
    const int64_t  catch_init = (int64_t)KOS_NO_CATCH << 8;
    unsigned       num_regs;
    unsigned       room;
    const KOS_TYPE type       = GET_OBJ_TYPE(func_obj);
    KOS_STACK     *stack;
    KOS_STACK     *new_stack;

    kos_track_refs(ctx, 1, &func_obj);

    if (type != OBJ_FUNCTION && type != OBJ_CLASS)
        RAISE_EXCEPTION(str_err_not_callable);

    stack      = IS_BAD_PTR(ctx->stack) ? 0 : OBJPTR(STACK, ctx->stack);
    new_stack  = stack;
    stack_size = stack ? KOS_atomic_read_relaxed_u32(stack->size) : 0;
    base_idx   = stack_size;

    assert( ! OBJPTR(FUNCTION, func_obj)->handler ||
           OBJPTR(FUNCTION, func_obj)->num_regs == 0);
    num_regs = OBJPTR(FUNCTION, func_obj)->handler
               ? 1 : OBJPTR(FUNCTION, func_obj)->num_regs;
    room = num_regs + KOS_STACK_EXTRA;

    if (ctx->stack_depth + room > KOS_MAX_STACK_DEPTH)
        RAISE_EXCEPTION(str_err_stack_overflow);

    /* Prepare stack for accommodating new stack frame */
    if (OBJPTR(FUNCTION, func_obj)->state < KOS_GEN_INIT &&
        ! (OBJPTR(FUNCTION, func_obj)->flags & KOS_FUN_CLOSURE)) {

        if ( ! stack || stack_size + room > stack->capacity) {

            KOS_STACK *cur_stack = stack;

            if (stack) {

                TRY(_unchain_reentrant_frame(ctx));

                assert( ! IS_BAD_PTR(ctx->stack));
                assert(GET_OBJ_TYPE(ctx->stack) == OBJ_STACK);

                cur_stack = OBJPTR(STACK, ctx->stack);
            }

            if ( ! stack || KOS_atomic_read_relaxed_u32(cur_stack->size) + room > cur_stack->capacity)
                TRY(_push_new_stack(ctx));

            new_stack = OBJPTR(STACK, ctx->stack);
            base_idx  = KOS_atomic_read_relaxed_u32(new_stack->size);

            assert(base_idx + room <= new_stack->capacity);
        }
    }
    else if (OBJPTR(FUNCTION, func_obj)->state > KOS_GEN_INIT) {

        const KOS_OBJ_ID gen_stack = OBJPTR(FUNCTION, func_obj)->generator_stack_frame;

        assert( ! IS_BAD_PTR(gen_stack));
        assert(GET_OBJ_TYPE(gen_stack) == OBJ_STACK);
        assert(KOS_atomic_read_relaxed_u32(OBJPTR(STACK, gen_stack)->size) > KOS_STACK_EXTRA);
        assert(stack);

        TRY(_chain_stack_frame(ctx, gen_stack));

        ctx->regs_idx = 4U;

        goto cleanup;
    }
    else {

        assert(type == OBJ_FUNCTION ||
               IS_BAD_PTR(OBJPTR(FUNCTION, func_obj)->generator_stack_frame));

        if (IS_BAD_PTR(ctx->stack))
            TRY(_push_new_stack(ctx));

        TRY(_push_new_reentrant_stack(ctx, room));

        OBJPTR(FUNCTION, func_obj)->generator_stack_frame = ctx->stack;

        new_stack = OBJPTR(STACK, ctx->stack);
        base_idx  = KOS_atomic_read_relaxed_u32(new_stack->size);
    }

    /* Initialize new stack frame */
    KOS_atomic_write_relaxed_u32(new_stack->size,              base_idx + room);
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx],     func_obj);
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx + 1], TO_SMALL_INT((int64_t)catch_init));
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx + 2], TO_SMALL_INT((int64_t)OBJPTR(FUNCTION, func_obj)->instr_offs));
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx + 3 + num_regs],
                                                       TO_SMALL_INT((int64_t)num_regs));
    ctx->regs_idx = base_idx + 3;

    /* Clear registers */
    {
        unsigned       idx = base_idx + 3;
        const unsigned end = idx + num_regs;

        for ( ; idx < end; idx++)
            KOS_atomic_write_relaxed_ptr(new_stack->buf[idx], KOS_BADPTR);
    }

    ctx->stack_depth += room;

cleanup:
    kos_untrack_refs(ctx, 1);

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
        if ( ! (stack->flags & KOS_REENTRANT_STACK)) {

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

        stack      = OBJPTR(STACK, new_stack_obj);
        size       = KOS_atomic_read_relaxed_u32(stack->size);
        ctx->stack = new_stack_obj;

        --ctx->stack_depth;
    }

    /* Push previous reentrant frame (generator or closure) on the stack */
    if (size) {

        const KOS_OBJ_ID new_stack_obj = KOS_atomic_read_relaxed_obj(stack->buf[size - 1]);

        if (IS_SMALL_INT(new_stack_obj)) {
            assert(size > KOS_STACK_EXTRA);

            assert(GET_SMALL_INT(new_stack_obj) > 0);
            assert(GET_SMALL_INT(new_stack_obj) < (int64_t)size);

            ctx->regs_idx = size - 1U - (unsigned)GET_SMALL_INT(new_stack_obj);
        }
        else {

            KOS_STACK *new_stack;

            assert( ! (stack->flags & KOS_REENTRANT_STACK));

            --size;
            KOS_atomic_write_relaxed_u32(stack->size, size);

            assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);
            new_stack = OBJPTR(STACK, new_stack_obj);

            assert(new_stack->flags & KOS_REENTRANT_STACK);
            size = KOS_atomic_read_relaxed_u32(new_stack->size);
            assert(size > KOS_STACK_EXTRA);

            KOS_atomic_write_relaxed_ptr(new_stack->buf[0], OBJID(STACK, stack));

            ctx->stack    = new_stack_obj;
            ctx->regs_idx = 4U;
        }
    }
}

typedef int (*KOS_WALK_STACK)(KOS_OBJ_ID stack,
                              uint32_t   frame_idx,
                              uint32_t   frame_size,
                              void      *cookie);

static int _walk_stack(KOS_CONTEXT ctx, KOS_WALK_STACK walk, void *cookie)
{
    int        error;
    KOS_OBJ_ID stack     = ctx->stack;
    uint32_t   size;
    uint32_t   prev_size = ~0U;
    int        pushed    = 0;

    error = KOS_push_locals(ctx, &pushed, 1, &stack);
    if (error)
        return error;

    assert( ! IS_BAD_PTR(stack));
    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);

    while (size) {

        const int reentrant = OBJPTR(STACK, stack)->flags & KOS_REENTRANT_STACK;

        if (size == 1) {

            const KOS_OBJ_ID prev = stack;

            stack = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[0]);
            if (IS_BAD_PTR(stack))
                --size;
            else {
                assert( ! IS_BAD_PTR(stack));
                assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
                assert( ! (OBJPTR(STACK, stack)->flags & KOS_REENTRANT_STACK));

                size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);

                if (reentrant && prev != ctx->stack) {
                    assert(size > 0);
                    assert(prev_size != ~0U);
                    assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[prev_size - 1]) == prev);

                    size      = prev_size - 1;
                    prev_size = ~0U;
                }
            }
        }
        else {

            KOS_OBJ_ID num_regs_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack)->buf[size - 1]);

            if (IS_SMALL_INT(num_regs_obj)) {
                int64_t    num_regs;
                uint32_t   frame_size;

                assert(size > KOS_STACK_EXTRA);
                assert(IS_SMALL_INT(num_regs_obj));

                num_regs = GET_SMALL_INT(num_regs_obj);

                assert(num_regs > 0);
                assert(num_regs < (int64_t)size);
                assert(num_regs + KOS_STACK_EXTRA <= (int64_t)size);

                frame_size = (uint32_t)num_regs + KOS_STACK_EXTRA;

                assert( ! reentrant || size == frame_size + 1);

                error = walk(stack,
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
                assert(KOS_atomic_read_relaxed_obj(OBJPTR(STACK, num_regs_obj)->buf[0]) == stack);
                assert(OBJPTR(STACK, num_regs_obj)->flags & KOS_REENTRANT_STACK);
                assert( ! reentrant);

                prev_size = size;
                stack     = num_regs_obj;
                size      = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, stack)->size);
            }
        }
    }

    KOS_pop_locals(ctx, pushed);

    return error;
}

static int _get_depth(KOS_OBJ_ID stack,
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
    KOS_OBJ_ID  backtrace;
} KOS_DUMP_CONTEXT;

static uint32_t _get_instr_offs(KOS_ATOMIC(KOS_OBJ_ID) *stack_frame)
{
    KOS_OBJ_ID offs_obj = KOS_atomic_read_relaxed_obj(stack_frame[2]);
    int64_t    offs;

    assert(IS_SMALL_INT(offs_obj));
    offs = GET_SMALL_INT(offs_obj);

    return (uint32_t)offs;
}

static int _dump_stack(KOS_OBJ_ID stack,
                       uint32_t   frame_idx,
                       uint32_t   frame_size,
                       void      *cookie)
{
    KOS_DUMP_CONTEXT       *dump_ctx    = (KOS_DUMP_CONTEXT *)cookie;
    KOS_CONTEXT             ctx         = dump_ctx->ctx;
    KOS_ATOMIC(KOS_OBJ_ID) *stack_frame = &OBJPTR(STACK, stack)->buf[frame_idx];
    KOS_FUNCTION           *func        = OBJPTR(FUNCTION, KOS_atomic_read_relaxed_obj(*stack_frame));
    KOS_OBJ_ID              module      = func->module;
    const uint32_t          instr_offs  = _get_instr_offs(stack_frame);
    const unsigned          line        = KOS_module_addr_to_line(IS_BAD_PTR(module) ? 0 : OBJPTR(MODULE, module), instr_offs);
    KOS_OBJ_ID              func_name;
    KOS_OBJ_ID              module_name = KOS_get_string(ctx, KOS_STR_XBUILTINX);
    KOS_OBJ_ID              module_path = KOS_get_string(ctx, KOS_STR_XBUILTINX);
    int                     error       = KOS_SUCCESS;
    KOS_OBJ_ID              frame_desc  = KOS_BADPTR;
    int                     pushed      = 0;

    func_name = KOS_module_addr_to_func_name(ctx, IS_BAD_PTR(module) ? 0 : OBJPTR(MODULE, module), instr_offs);
    if (IS_BAD_PTR(func_name)) {
        if (KOS_is_exception_pending(ctx))
            goto cleanup;

        /* TODO add builtin function name */
        func_name = KOS_get_string(ctx, KOS_STR_XBUILTINX);
    }

    TRY(KOS_push_locals(ctx, &pushed, 5, &module, &func_name, &module_name, &module_path, &frame_desc));

    frame_desc = KOS_new_object(ctx);
    TRY_OBJID(frame_desc);

    assert(dump_ctx->idx < KOS_get_array_size(dump_ctx->backtrace));
    TRY(KOS_array_write(ctx, dump_ctx->backtrace, (int)dump_ctx->idx, frame_desc));

    /* TODO use builtin function pointer for offset */

    if ( ! IS_BAD_PTR(module)) {
        module_name = OBJPTR(MODULE, module)->name;
        module_path = OBJPTR(MODULE, module)->path;
    }

    TRY(KOS_set_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_MODULE),   module_name));
    TRY(KOS_set_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_FILE),     module_path));
    TRY(KOS_set_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_LINE),     TO_SMALL_INT((int)line)));
    TRY(KOS_set_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_OFFSET),   TO_SMALL_INT((int)instr_offs)));
    TRY(KOS_set_property(ctx, frame_desc, KOS_get_string(ctx, KOS_STR_FUNCTION), func_name));

    ++dump_ctx->idx;

cleanup:
    KOS_pop_locals(ctx, pushed);

    return error;
}

void kos_wrap_exception(KOS_CONTEXT ctx)
{
    int                 error         = KOS_SUCCESS;
    unsigned            depth;
    KOS_OBJ_ID          exception     = KOS_BADPTR;
    KOS_OBJ_ID          backtrace     = KOS_BADPTR;
    KOS_OBJ_ID          thrown_object = ctx->exception;
    KOS_INSTANCE *const inst          = ctx->inst;
    int                 partial_wrap  = 0;
    KOS_DUMP_CONTEXT    dump_ctx;
    KOS_OBJ_ID          prev_locals   = KOS_BADPTR;
    int                 pushed        = 0;

    assert(!IS_BAD_PTR(thrown_object));

    if (GET_OBJ_TYPE(thrown_object) == OBJ_OBJECT) {

        const KOS_OBJ_ID proto = KOS_get_prototype(ctx, thrown_object);

        if (proto == inst->prototypes.exception_proto)
            /* Exception already wrapped */
            return;
    }

    KOS_clear_exception(ctx);

    TRY(KOS_push_local_scope(ctx, &prev_locals));

    dump_ctx.backtrace = KOS_BADPTR;
    TRY(KOS_push_locals(ctx, &pushed, 4,
                        &exception, &backtrace, &thrown_object, &dump_ctx.backtrace));

    exception = KOS_new_object_with_prototype(ctx, inst->prototypes.exception_proto);
    TRY_OBJID(exception);

    TRY(KOS_set_property(ctx, exception, KOS_get_string(ctx, KOS_STR_VALUE), thrown_object));

    partial_wrap = 1;

    depth = 0;
    TRY(_walk_stack(ctx, _get_depth, &depth));

    backtrace = KOS_new_array(ctx, depth);
    TRY_OBJID(backtrace);

    TRY(KOS_set_property(ctx, exception, KOS_get_string(ctx, KOS_STR_BACKTRACE), backtrace));

    dump_ctx.ctx       = ctx;
    dump_ctx.idx       = 0;
    dump_ctx.backtrace = backtrace;

    TRY(_walk_stack(ctx, _dump_stack, &dump_ctx));

    ctx->exception = exception;

cleanup:
    KOS_pop_local_scope(ctx, &prev_locals);

    if (error)
        ctx->exception = partial_wrap ? exception : thrown_object;
}

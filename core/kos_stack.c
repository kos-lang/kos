/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "kos_config.h"
#include "kos_const_strings.h"
#include "kos_heap.h"
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
        new_stack->flags = KOS_NORMAL_STACK;

    return init_stack(ctx, new_stack);
}

static int push_new_reentrant_stack(KOS_CONTEXT ctx,
                                    unsigned    room)
{
    int              error;
    const size_t     alloc_size = sizeof(KOS_STACK) + sizeof(KOS_OBJ_ID) * room;
    KOS_STACK *const new_stack  = (KOS_STACK *)kos_alloc_object(ctx,
                                                                KOS_ALLOC_IMMOVABLE,
                                                                OBJ_STACK,
                                                                (uint32_t)alloc_size);
    if (new_stack)
        new_stack->flags = KOS_REENTRANT_STACK;

    assert( ! IS_BAD_PTR(ctx->stack));

    error = init_stack(ctx, new_stack);

    if ( ! error)
        new_stack->capacity = room;

    return error;
}

int kos_stack_push(KOS_CONTEXT ctx,
                   KOS_OBJ_ID  func_obj,
                   KOS_OBJ_ID  this_obj,
                   uint8_t     ret_reg,
                   uint8_t     gen_reg)
{
    int              error      = KOS_SUCCESS;
    uint32_t         stack_size;
    uint32_t         base_idx;
    uint32_t         state;
    const int64_t    catch_init = (int64_t)KOS_NO_CATCH << 8;
    int64_t          reg_init;
    unsigned         num_regs;
    unsigned         room;
    const KOS_TYPE   type       = GET_OBJ_TYPE(func_obj);
    KOS_STACK       *stack;
    KOS_STACK       *new_stack;
    KOS_STACK_FRAME *stack_frame;
    KOS_LOCAL        func;
    KOS_LOCAL        this_;

    KOS_init_local_with(ctx, &func, func_obj);
    KOS_init_local_with(ctx, &this_, this_obj);

    if (type != OBJ_FUNCTION && type != OBJ_CLASS)
        RAISE_EXCEPTION_STR(str_err_not_callable);

    stack      = IS_BAD_PTR(ctx->stack) ? 0 : OBJPTR(STACK, ctx->stack);
    new_stack  = stack;
    stack_size = stack ? KOS_atomic_read_relaxed_u32(stack->size) : 0;
    base_idx   = stack_size;
    state      = KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, func.o)->state);

    assert( ! OBJPTR(FUNCTION, func.o)->handler ||
           OBJPTR(FUNCTION, func.o)->opts.num_regs == 0);

    assert((state > KOS_GEN_INIT) || (gen_reg == KOS_NO_REG));
    assert(ret_reg <= KOS_NO_REG);

    num_regs = OBJPTR(FUNCTION, func.o)->handler
               ? 1 : OBJPTR(FUNCTION, func.o)->opts.num_regs;
    if ( ! IS_BAD_PTR(this_.o) && (state == KOS_CTOR) && ! OBJPTR(FUNCTION, func.o)->handler) {
        assert(GET_OBJ_TYPE(func.o) == OBJ_CLASS);
        ++num_regs;
    }
    else
        this_.o = KOS_BADPTR;
    room = num_regs + KOS_STACK_EXTRA;

    reg_init = ((int64_t)gen_reg << 16) | ((int64_t)ret_reg << 8) | (int64_t)num_regs;

    if (ctx->stack_depth + room > KOS_MAX_STACK_DEPTH)
        RAISE_EXCEPTION_STR(str_err_stack_overflow);

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

            if ( ! stack || KOS_atomic_read_relaxed_u32(cur_stack->size) + room > cur_stack->capacity)
                TRY(push_new_stack(ctx));

            new_stack = OBJPTR(STACK, ctx->stack);
            base_idx  = KOS_atomic_read_relaxed_u32(new_stack->size);

            assert(base_idx + room <= new_stack->capacity);
        }
    }
    /* Case 2: Instantiated generator - reuse (push) its stack object */
    else if (state > KOS_GEN_INIT) {

        const KOS_OBJ_ID gen_stack = OBJPTR(FUNCTION, func.o)->generator_stack_frame;
        uint32_t         size;
        KOS_OBJ_ID       old_regs;

        assert( ! IS_BAD_PTR(gen_stack));
        assert(GET_OBJ_TYPE(gen_stack) == OBJ_STACK);
        size = KOS_atomic_read_relaxed_u32(OBJPTR(STACK, gen_stack)->size);
        assert(size > KOS_STACK_EXTRA);

        if (IS_BAD_PTR(ctx->stack))
            TRY(push_new_stack(ctx));

        TRY(chain_stack_frame(ctx, gen_stack));

        old_regs = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, gen_stack)->buf[size - 1]);
        assert(IS_SMALL_INT(old_regs));
        assert((uint32_t)(GET_SMALL_INT(old_regs) & 0xFFU) == num_regs);

        KOS_atomic_write_relaxed_ptr(OBJPTR(STACK, gen_stack)->buf[size - 1], TO_SMALL_INT(reg_init));

        ctx->regs_idx = 4U;

        goto cleanup;
    }
    /* Case 3: New generator or closure, create new reentrant stack object */
    else {

        if (IS_BAD_PTR(ctx->stack))
            TRY(push_new_stack(ctx));

        /* +1 because item at index 0 is used to point to previous stack frame,
         * we still need to have 'room' left */
        TRY(push_new_reentrant_stack(ctx, room + 1));

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
    KOS_atomic_write_relaxed_ptr(stack_frame->instr_offs, TO_SMALL_INT((int64_t)OBJPTR(FUNCTION, func.o)->instr_offs));
    KOS_atomic_write_relaxed_ptr(new_stack->buf[base_idx + 3 + num_regs],
                                                          TO_SMALL_INT(reg_init));
    ctx->regs_idx = base_idx + 3;

    /* Clear registers */
    {
        unsigned idx = 0;

        for ( ; idx < num_regs; idx++)
            KOS_atomic_write_relaxed_ptr(stack_frame->regs[idx], KOS_BADPTR);
    }

    if ( ! IS_BAD_PTR(this_.o))
        KOS_atomic_write_relaxed_ptr(stack_frame->regs[num_regs - 1], this_.o);

    ctx->stack_depth += room;

cleanup:
    KOS_destroy_top_locals(ctx, &this_, &func);

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

            assert((int)num_regs_u == (GET_SMALL_INT(KOS_atomic_read_relaxed_obj(stack->buf[size - 1])) & 0xFF));

            size             -= delta;
            ctx->stack_depth -= delta;

            KOS_atomic_write_relaxed_u32(stack->size, size);
        }
        else {

            const KOS_OBJ_ID new_stack_obj = stack->buf[0];

            assert(size == 1U + KOS_STACK_EXTRA +
                           ((uint64_t)GET_SMALL_INT(KOS_atomic_read_relaxed_obj(stack->buf[size - 1])) & 0xFFU));
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

            const uint8_t num_regs = (uint8_t)GET_SMALL_INT(new_stack_obj);

            assert(size > KOS_STACK_EXTRA);

            assert(num_regs > 0);
            assert(num_regs < (int64_t)size);

            ctx->regs_idx = size - 1U - num_regs;
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

        const int reentrant = OBJPTR(STACK, stack_obj)->flags & KOS_REENTRANT_STACK;

        assert( ! kos_is_heap_object(ctx->stack));

        if (size == 1) {

            const KOS_OBJ_ID prev = stack_obj;

            stack_obj = KOS_atomic_read_relaxed_obj(OBJPTR(STACK, stack_obj)->buf[0]);
            if (IS_BAD_PTR(stack_obj))
                --size;
            else {
                assert( ! IS_BAD_PTR(stack_obj));
                assert(GET_OBJ_TYPE(stack_obj) == OBJ_STACK);
                assert( ! (OBJPTR(STACK, stack_obj)->flags & KOS_REENTRANT_STACK));

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

                num_regs = GET_SMALL_INT(num_regs_obj) & 0xFF;

                assert(num_regs > 0);
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
                assert(OBJPTR(STACK, num_regs_obj)->flags & KOS_REENTRANT_STACK);
                assert( ! reentrant);

                prev_size = size;
                stack_obj   = num_regs_obj;
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
    KOS_OBJ_ID offs_obj = KOS_atomic_read_relaxed_obj(stack_frame->instr_offs);
    int64_t    offs;

    assert(IS_SMALL_INT(offs_obj));
    offs = GET_SMALL_INT(offs_obj);

    return (uint32_t)offs;
}

static int dump_stack(KOS_OBJ_ID stack,
                      uint32_t   frame_idx,
                      uint32_t   frame_size,
                      void      *cookie)
{
    KOS_DUMP_CONTEXT       *dump_ctx    = (KOS_DUMP_CONTEXT *)cookie;
    KOS_CONTEXT             ctx         = dump_ctx->ctx;
    KOS_STACK_FRAME        *stack_frame = (KOS_STACK_FRAME *)&OBJPTR(STACK, stack)->buf[frame_idx];
    KOS_FUNCTION           *func        = OBJPTR(FUNCTION, KOS_atomic_read_relaxed_obj(stack_frame->func_obj));
    const uint32_t          instr_offs  = get_instr_offs(stack_frame);
    const unsigned          line        = KOS_module_addr_to_line(IS_BAD_PTR(func->module)
                                                                      ? 0 : OBJPTR(MODULE, func->module),
                                                                  instr_offs);
    int                     error       = KOS_SUCCESS;
    KOS_LOCAL               module;
    KOS_LOCAL               func_name;
    KOS_LOCAL               module_name;
    KOS_LOCAL               module_path;
    KOS_LOCAL               frame_desc;

    KOS_init_local_with(ctx, &module,      func->module);
    KOS_init_local_with(ctx, &func_name,   func->name);
    KOS_init_local_with(ctx, &module_name, KOS_CONST_ID(str_xbuiltinx));
    KOS_init_local_with(ctx, &module_path, KOS_CONST_ID(str_xbuiltinx));
    KOS_init_local_with(ctx, &frame_desc,  KOS_new_object(ctx));
    TRY_OBJID(frame_desc.o);

    assert(dump_ctx->idx < KOS_get_array_size(dump_ctx->backtrace.o));
    TRY(KOS_array_write(ctx, dump_ctx->backtrace.o, (int)dump_ctx->idx, frame_desc.o));

    /* TODO use builtin function pointer for offset */

    if ( ! IS_BAD_PTR(module.o)) {
        module_name.o = OBJPTR(MODULE, module.o)->name;
        module_path.o = OBJPTR(MODULE, module.o)->path;
    }

    TRY(KOS_set_property(ctx, frame_desc.o, KOS_CONST_ID(str_module), module_name.o));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_FILE,             module_path.o));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_LINE,             TO_SMALL_INT((int)line)));
    TRY(KOS_set_property(ctx, frame_desc.o, KOS_STR_OFFSET,           TO_SMALL_INT((int)instr_offs)));
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

    KOS_init_locals(ctx, 4, &exception, &backtrace, &thrown_object, &dump_ctx.backtrace);

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

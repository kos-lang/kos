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

static const char str_backtrace[]          = "backtrace";
static const char str_builtin[]            = "<builtin>";
static const char str_err_not_callable[]   = "object is not callable";
static const char str_err_stack_overflow[] = "stack overflow";
static const char str_file[]               = "file";
static const char str_function[]           = "function";
static const char str_line[]               = "line";
static const char str_module[]             = "module";
static const char str_offset[]             = "offset";
static const char str_value[]              = "value";

static int _push_new_stack(KOS_YARN yarn);

static int _unchain_reentrant_frame(KOS_YARN yarn)
{
    KOS_OBJ_ID old_stack = yarn->stack;

    if (OBJPTR(STACK, old_stack)->header.flags & KOS_REENTRANT_STACK) {

        KOS_OBJ_ID gen_stack = old_stack;
        uint32_t   idx;

        assert(KOS_atomic_read_u32(OBJPTR(STACK, old_stack)->size) > 0);

        old_stack = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, old_stack)->buf[0]);

        assert( ! IS_BAD_PTR(old_stack));

        idx = KOS_atomic_read_u32(OBJPTR(STACK, old_stack)->size);

        if (idx == OBJPTR(STACK, old_stack)->capacity) {

            int error;

            yarn->stack = old_stack;

            error = _push_new_stack(yarn);

            if (error) {
                yarn->stack = gen_stack;
                return error;
            }

            old_stack = yarn->stack;
            idx       = KOS_atomic_read_u32(OBJPTR(STACK, old_stack)->size);
        }

        KOS_atomic_write_ptr(OBJPTR(STACK, old_stack)->buf[idx], gen_stack);
        KOS_atomic_write_u32(OBJPTR(STACK, old_stack)->size,     idx + 1);

        yarn->stack = old_stack;
    }

    return KOS_SUCCESS;
}

static int _chain_stack_frame(KOS_YARN   yarn,
                              KOS_OBJ_ID stack)
{
    const int error = _unchain_reentrant_frame(yarn);

    if ( ! error) {

        const KOS_OBJ_ID old_stack = yarn->stack;

        KOS_atomic_write_ptr(OBJPTR(STACK, stack)->buf[0], old_stack);

        yarn->stack = stack;

        yarn->stack_depth += KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);
    }

    return error;
}

static int _init_stack(KOS_YARN   yarn,
                       KOS_STACK *stack)
{
    int error = stack ? KOS_SUCCESS : KOS_ERROR_EXCEPTION;

    if (stack) {
        uint8_t *const begin = (uint8_t *)stack;
        uint8_t *const end   = begin + GET_SMALL_INT(stack->header.alloc_size);
        uint8_t *const buf   = (uint8_t *)&stack->buf[0];

        stack->capacity         = (uint32_t)(end - buf) / sizeof(KOS_OBJ_ID);
        stack->header.yield_reg = 0xFFU;
        KOS_atomic_write_u32(stack->size, 1);

        if ( ! IS_BAD_PTR(yarn->stack))
            error = _chain_stack_frame(yarn, OBJID(STACK, stack));
        else {
            yarn->stack = OBJID(STACK, stack);
            KOS_atomic_write_ptr(stack->buf[0], KOS_BADPTR);
        }
    }

    return error;
}

static int _push_new_stack(KOS_YARN yarn)
{
    KOS_STACK *const new_stack = (KOS_STACK *)_KOS_alloc_object_page(yarn,
                                                                     KOS_ALLOC_DEFAULT,
                                                                     OBJ_STACK);
    if (new_stack)
        new_stack->header.flags = KOS_NORMAL_STACK;

    return _init_stack(yarn, new_stack);
}

static int _push_new_reentrant_stack(KOS_YARN yarn,
                                     unsigned room)
{
    const size_t     alloc_size = sizeof(KOS_STACK) + sizeof(KOS_OBJ_ID) * room;
    KOS_STACK *const new_stack  = (KOS_STACK *)_KOS_alloc_object(yarn,
                                                                 KOS_ALLOC_DEFAULT,
                                                                 OBJ_STACK,
                                                                 (uint32_t)alloc_size);
    if (new_stack)
        new_stack->header.flags = KOS_REENTRANT_STACK;

    assert( ! IS_BAD_PTR(yarn->stack));

    return _init_stack(yarn, new_stack);
}

int _KOS_stack_push(KOS_YARN   yarn,
                    KOS_OBJ_ID func_obj)
{
    int              error        = KOS_SUCCESS;
    KOS_STACK *const stack        = IS_BAD_PTR(yarn->stack) ? 0 : OBJPTR(STACK, yarn->stack);
    const uint32_t   stack_size   = stack ? KOS_atomic_read_u32(stack->size) : 0;
    KOS_STACK       *new_stack    = stack;
    uint32_t         base_idx     = stack_size;
    const int64_t    catch_init   = (int64_t)KOS_NO_CATCH << 8;
    KOS_FUNCTION    *func;
    unsigned         num_regs;
    unsigned         room;

    const enum KOS_OBJECT_TYPE type = GET_OBJ_TYPE(func_obj);

    switch (type) {

        case OBJ_FUNCTION:
            func = OBJPTR(FUNCTION, func_obj);
            break;

        case OBJ_CLASS:
            func = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
            break;

        default:
            RAISE_EXCEPTION(str_err_not_callable);
    }

    assert( ! func->handler || func->header.num_regs == 0);
    num_regs = func->handler ? 1 : func->header.num_regs;
    room = num_regs + KOS_STACK_EXTRA;

    /* Prepare stack for accommodating new stack frame */
    if (func->state < KOS_GEN_INIT && ! (func->header.flags & KOS_FUN_CLOSURE)) {

        if ( ! stack || stack_size + room > stack->capacity) {

            KOS_STACK *cur_stack = stack;

            if (stack) {

                TRY(_unchain_reentrant_frame(yarn));

                assert( ! IS_BAD_PTR(yarn->stack));
                assert(GET_OBJ_TYPE(yarn->stack) == OBJ_STACK);

                cur_stack = OBJPTR(STACK, yarn->stack);
            }

            if ( ! stack || KOS_atomic_read_u32(cur_stack->size) + room > cur_stack->capacity)
                TRY(_push_new_stack(yarn));

            new_stack = OBJPTR(STACK, yarn->stack);
            base_idx  = KOS_atomic_read_u32(new_stack->size);

            assert(base_idx + room <= new_stack->capacity);
        }
    }
    else if (func->state > KOS_GEN_INIT) {

        const KOS_OBJ_ID gen_stack = func->generator_stack_frame;

        assert( ! IS_BAD_PTR(gen_stack));
        assert(GET_OBJ_TYPE(gen_stack) == OBJ_STACK);
        assert(KOS_atomic_read_u32(OBJPTR(STACK, gen_stack)->size) > KOS_STACK_EXTRA);
        assert(stack);

        TRY(_chain_stack_frame(yarn, gen_stack));

        yarn->regs_idx = 4U;

        return KOS_SUCCESS;
    }
    else {

        assert(type == OBJ_FUNCTION || IS_BAD_PTR(func->generator_stack_frame));

        if (IS_BAD_PTR(yarn->stack))
            TRY(_push_new_stack(yarn));

        TRY(_push_new_reentrant_stack(yarn, room));

        func->generator_stack_frame = yarn->stack;

        new_stack = OBJPTR(STACK, yarn->stack);
        base_idx  = KOS_atomic_read_u32(new_stack->size);
    }

    /* Initialize new stack frame */
    KOS_atomic_write_u32(new_stack->size,              base_idx + room);
    KOS_atomic_write_ptr(new_stack->buf[base_idx],     func_obj);
    KOS_atomic_write_ptr(new_stack->buf[base_idx + 1], TO_SMALL_INT((int64_t)catch_init));
    KOS_atomic_write_ptr(new_stack->buf[base_idx + 2], TO_SMALL_INT((int64_t)func->instr_offs));
    KOS_atomic_write_ptr(new_stack->buf[base_idx + 3 + num_regs],
                                                       TO_SMALL_INT((int64_t)num_regs));
    yarn->regs_idx = base_idx + 3;

    /* Clear registers builds */
    {
        unsigned       idx = base_idx + 3;
        const unsigned end = idx + num_regs;

        for ( ; idx < end; idx++)
            KOS_atomic_write_ptr(new_stack->buf[idx], KOS_BADPTR);
    }

    yarn->stack_depth += room;

    if (yarn->stack_depth > _KOS_MAX_STACK_DEPTH) {
        _KOS_stack_pop(yarn);
        RAISE_EXCEPTION(str_err_stack_overflow);
    }

_error:
    return error;
}

void _KOS_stack_pop(KOS_YARN yarn)
{
    KOS_STACK *stack;
    uint32_t   size;

    assert( ! IS_BAD_PTR(yarn->stack));
    stack = OBJPTR(STACK, yarn->stack);

    size = KOS_atomic_read_u32(stack->size);
    assert(size);

    assert((size == 1 && IS_BAD_PTR(stack->buf[0])) ||
           IS_SMALL_INT((KOS_OBJ_ID)KOS_atomic_read_ptr(stack->buf[size - 1])));

    if (size > 1) {
        if ( ! (stack->header.flags & KOS_REENTRANT_STACK)) {

            const uint32_t num_regs_u = size - yarn->regs_idx - 1U;
            const uint32_t delta      = num_regs_u + KOS_STACK_EXTRA;

            assert(yarn->regs_idx < size);

            assert((int)num_regs_u == GET_SMALL_INT((KOS_OBJ_ID)KOS_atomic_read_ptr(stack->buf[size - 1])));

            size              -= delta;
            yarn->stack_depth -= delta;

            KOS_atomic_write_u32(stack->size, size);
        }
        else {

            const KOS_OBJ_ID new_stack_obj = stack->buf[0];

            assert(size == 1U + KOS_STACK_EXTRA +
                           (uint64_t)GET_SMALL_INT((KOS_OBJ_ID)KOS_atomic_read_ptr(stack->buf[size - 1])));
            assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);

            yarn->stack_depth -= size;

            stack       = OBJPTR(STACK, new_stack_obj);
            size        = KOS_atomic_read_u32(stack->size);
            yarn->stack = new_stack_obj;
        }
    }

    /* If we ran out of stack, go to the previous stack object in the chain */
    while (size == 1) {

        const KOS_OBJ_ID new_stack_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(stack->buf[0]);

        if (IS_BAD_PTR(new_stack_obj)) {
            size = 0;
            assert(yarn->stack_depth == 0);
            break;
        }

        assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);

        stack       = OBJPTR(STACK, new_stack_obj);
        size        = KOS_atomic_read_u32(stack->size);
        yarn->stack = new_stack_obj;

        --yarn->stack_depth;
    }

    /* Push previous reentrant frame (generator or closure) on the stack */
    if (size) {

        const KOS_OBJ_ID new_stack_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(stack->buf[size - 1]);

        if (IS_SMALL_INT(new_stack_obj)) {
            assert(size > KOS_STACK_EXTRA);

            assert(GET_SMALL_INT(new_stack_obj) > 0);
            assert(GET_SMALL_INT(new_stack_obj) < (int64_t)size);

            yarn->regs_idx = size - 1U - (unsigned)GET_SMALL_INT(new_stack_obj);
        }
        else {

            KOS_STACK *new_stack;

            assert( ! (stack->header.flags & KOS_REENTRANT_STACK));

            --size;
            KOS_atomic_write_u32(stack->size, size);

            assert(GET_OBJ_TYPE(new_stack_obj) == OBJ_STACK);
            new_stack = OBJPTR(STACK, new_stack_obj);

            assert(new_stack->header.flags & KOS_REENTRANT_STACK);
            size = KOS_atomic_read_u32(new_stack->size);
            assert(size > KOS_STACK_EXTRA);

            KOS_atomic_write_ptr(new_stack->buf[0], OBJID(STACK, stack));

            yarn->stack    = new_stack_obj;
            yarn->regs_idx = 4U;
        }
    }
}

typedef int (*KOS_WALK_STACK)(KOS_OBJ_ID stack,
                              uint32_t   frame_idx,
                              uint32_t   frame_size,
                              void      *cookie);

static int _walk_stack(KOS_YARN yarn, KOS_WALK_STACK walk, void *cookie)
{
    int        error     = KOS_SUCCESS;
    KOS_OBJ_ID stack     = yarn->stack;
    uint32_t   size;
    uint32_t   prev_size = ~0U;

    assert( ! IS_BAD_PTR(stack));
    assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
    size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);

    while (size) {

        const int reentrant = OBJPTR(STACK, stack)->header.flags & KOS_REENTRANT_STACK;

        if (size == 1) {

            const KOS_OBJ_ID prev = stack;

            stack = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[0]);
            if (IS_BAD_PTR(stack))
                --size;
            else {
                assert( ! IS_BAD_PTR(stack));
                assert(GET_OBJ_TYPE(stack) == OBJ_STACK);
                assert( ! (OBJPTR(STACK, stack)->header.flags & KOS_REENTRANT_STACK));

                size = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);

                if (reentrant && prev != yarn->stack) {
                    assert(size > 0);
                    assert(prev_size != ~0U);
                    assert((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[prev_size - 1]) == prev);

                    size      = prev_size - 1;
                    prev_size = ~0U;
                }
            }
        }
        else {

            KOS_OBJ_ID num_regs_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack)->buf[size - 1]);

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
                assert(KOS_atomic_read_u32(OBJPTR(STACK, num_regs_obj)->size) > 0);
                assert((KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, num_regs_obj)->buf[0]) == stack);
                assert(OBJPTR(STACK, num_regs_obj)->header.flags & KOS_REENTRANT_STACK);
                assert( ! reentrant);

                prev_size = size;
                stack     = num_regs_obj;
                size      = KOS_atomic_read_u32(OBJPTR(STACK, stack)->size);
            }
        }
    }

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

typedef struct _DUMP_CONTEXT {
    KOS_YARN   yarn;
    uint32_t   idx;
    KOS_OBJ_ID backtrace;
} _KOS_DUMP_CONTEXT;

static KOS_FUNCTION *_get_func(KOS_ATOMIC(KOS_OBJ_ID) *stack_frame)
{
    KOS_OBJ_ID    func_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(*stack_frame);
    KOS_FUNCTION *func;

    if (GET_OBJ_TYPE(func_obj) == OBJ_FUNCTION)
        func = OBJPTR(FUNCTION, func_obj);
    else {
        assert(GET_OBJ_TYPE(func_obj) == OBJ_CLASS);
        func = (KOS_FUNCTION *)OBJPTR(CLASS, func_obj);
    }

    return func;
}

static uint32_t _get_instr_offs(KOS_ATOMIC(KOS_OBJ_ID) *stack_frame)
{
    KOS_OBJ_ID offs_obj = (KOS_OBJ_ID)KOS_atomic_read_ptr(stack_frame[2]);
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
    _KOS_DUMP_CONTEXT      *dump_ctx    = (_KOS_DUMP_CONTEXT *)cookie;
    KOS_YARN                yarn       = dump_ctx->yarn;
    KOS_ATOMIC(KOS_OBJ_ID) *stack_frame = &OBJPTR(STACK, stack)->buf[frame_idx];
    KOS_FUNCTION           *func        = _get_func(stack_frame);
    KOS_MODULE             *module      = IS_BAD_PTR(func->module) ? 0 : OBJPTR(MODULE, func->module);
    const uint32_t          instr_offs  = _get_instr_offs(stack_frame);
    const unsigned          line        = KOS_module_addr_to_line(module, instr_offs);
    KOS_OBJ_ID              func_name   = KOS_module_addr_to_func_name(module, instr_offs);
    KOS_OBJ_ID              module_name = KOS_instance_get_cstring(yarn, str_builtin);
    KOS_OBJ_ID              module_path = KOS_instance_get_cstring(yarn, str_builtin);
    int                     error       = KOS_SUCCESS;

    KOS_OBJ_ID frame_desc = KOS_new_object(yarn);
    TRY_OBJID(frame_desc);

    if (IS_BAD_PTR(func_name))
        func_name = KOS_instance_get_cstring(yarn, str_builtin); /* TODO add builtin function name */

    assert(dump_ctx->idx < KOS_get_array_size(dump_ctx->backtrace));
    TRY(KOS_array_write(yarn, dump_ctx->backtrace, (int)dump_ctx->idx, frame_desc));

    /* TODO use builtin function pointer for offset */

    if (module) {
        module_name = module->name;
        module_path = module->path;
    }

    TRY(KOS_set_property(yarn, frame_desc, KOS_instance_get_cstring(yarn, str_module),   module_name));
    TRY(KOS_set_property(yarn, frame_desc, KOS_instance_get_cstring(yarn, str_file),     module_path));
    TRY(KOS_set_property(yarn, frame_desc, KOS_instance_get_cstring(yarn, str_line),     TO_SMALL_INT((int)line)));
    TRY(KOS_set_property(yarn, frame_desc, KOS_instance_get_cstring(yarn, str_offset),   TO_SMALL_INT((int)instr_offs)));
    TRY(KOS_set_property(yarn, frame_desc, KOS_instance_get_cstring(yarn, str_function), func_name));

    ++dump_ctx->idx;

_error:
    return error;
}

void _KOS_wrap_exception(KOS_YARN yarn)
{
    int                 error         = KOS_SUCCESS;
    unsigned            depth;
    KOS_OBJ_ID          exception;
    KOS_OBJ_ID          backtrace;
    KOS_OBJ_ID          thrown_object = yarn->exception;
    KOS_INSTANCE *const inst          = yarn->inst;
    int                 partial_wrap  = 0;
    _KOS_DUMP_CONTEXT   dump_ctx;

    assert(!IS_BAD_PTR(thrown_object));

    if (GET_OBJ_TYPE(thrown_object) == OBJ_OBJECT) {

        const KOS_OBJ_ID proto = KOS_get_prototype(yarn, thrown_object);

        if (proto == inst->prototypes.exception_proto)
            /* Exception already wrapped */
            return;
    }

    KOS_clear_exception(yarn);

    exception = KOS_new_object_with_prototype(yarn, inst->prototypes.exception_proto);
    TRY_OBJID(exception);

    TRY(KOS_set_property(yarn, exception, KOS_instance_get_cstring(yarn, str_value), thrown_object));

    partial_wrap = 1;

    depth = 0;
    TRY(_walk_stack(yarn, _get_depth, &depth));

    backtrace = KOS_new_array(yarn, depth);
    TRY_OBJID(backtrace);

    TRY(KOS_array_resize(yarn, backtrace, depth));

    TRY(KOS_set_property(yarn, exception, KOS_instance_get_cstring(yarn, str_backtrace), backtrace));

    dump_ctx.yarn      = yarn;
    dump_ctx.idx       = 0;
    dump_ctx.backtrace = backtrace;

    TRY(_walk_stack(yarn, _dump_stack, &dump_ctx));

    yarn->exception = exception;

_error:
    if (error)
        yarn->exception = partial_wrap ? exception : thrown_object;
}

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
#include "../inc/kos_bytecode.h"
#include "../inc/kos_context.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../core/kos_config.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_vm.h"
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning( disable : 4310 ) /* cast truncates constant value */
#endif

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(frame)); KOS_clear_exception(frame); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(frame))

#define IMMPART(val,shift) ((uint8_t)((uint32_t)(val) >> shift))
#define IMM32(val) IMMPART(val, 0), IMMPART(val, 8), IMMPART(val, 16), IMMPART(val, 24)

static const char str_value[] = "value";

static KOS_OBJ_ID _run_code(KOS_CONTEXT   *ctx,
                            KOS_FRAME      frame,
                            const uint8_t *bytecode,
                            unsigned       bytecode_size,
                            unsigned       num_regs,
                            KOS_OBJ_ID    *constants,
                            unsigned       num_constants)
{
    KOS_OBJ_ID  ret    = KOS_BADPTR;
    int         error  = KOS_SUCCESS;
    KOS_MODULE *module = OBJPTR(MODULE, ctx->modules.init_module);

    memset(module, 0, sizeof(*module));

    module->header.type       = OBJ_MODULE;
    module->context           = ctx;
    module->constants_storage = num_constants ? KOS_new_array(frame, num_constants) : KOS_BADPTR;
    module->constants         = 0;
    module->bytecode          = bytecode;
    module->bytecode_size     = bytecode_size;
    module->instr_offs        = 0;
    module->num_regs          = (uint16_t)num_regs;

    if (num_constants) {

        unsigned i;

        if (IS_BAD_PTR(module->constants_storage))
            error = KOS_ERROR_EXCEPTION;

        for (i = 0; ! error && i < num_constants; ++i)
            error = KOS_array_write(frame, module->constants_storage, i, constants[i]);

        if ( ! error)
            module->constants = _KOS_get_array_buffer(OBJPTR(ARRAY, module->constants_storage));
    }

    if ( ! error) {
        error = _KOS_vm_run_module(module, &ret);

        if (error) {
            KOS_raise_exception(frame, ret);
            ret = KOS_BADPTR;
        }
    }

    return ret;
}

enum _CREATE_FUNC {
    CREATE_FUNC,
    CREATE_GEN,
    CREATE_CLASS
};

static KOS_OBJ_ID _create_func_obj(KOS_FRAME         frame,
                                   enum _CREATE_FUNC create,
                                   uint32_t          offset,
                                   uint8_t           num_regs,
                                   uint8_t           args_reg,
                                   uint8_t           num_args,
                                   uint8_t           flags)
{
    KOS_OBJ_ID    obj_id;
    KOS_FUNCTION *func;

    if (create == CREATE_CLASS) {
        obj_id = KOS_new_class(frame, KOS_VOID);
        if (IS_BAD_PTR(obj_id))
            return KOS_BADPTR;
        func   = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
    }
    else {
        obj_id = KOS_new_function(frame);
        if (IS_BAD_PTR(obj_id))
            return KOS_BADPTR;
        func   = OBJPTR(FUNCTION, obj_id);
    }

    func->header.flags    = flags;
    func->header.num_args = num_args;
    func->header.num_regs = num_regs;
    func->args_reg        = args_reg;
    func->instr_offs      = offset;
    func->module          = frame->ctx->modules.init_module;

    if (create == CREATE_GEN)
        func->state = KOS_GEN_INIT;

    return obj_id;
}

static KOS_OBJ_ID _create_func(KOS_FRAME frame,
                               uint32_t  offset,
                               uint8_t   num_regs,
                               uint8_t   args_reg,
                               uint8_t   num_args,
                               uint8_t   flags)
{
    return _create_func_obj(frame, CREATE_FUNC, offset, num_regs, args_reg, num_args, flags);
}

static KOS_OBJ_ID _create_gen(KOS_FRAME frame,
                              uint32_t  offset,
                              uint8_t   num_regs,
                              uint8_t   args_reg,
                              uint8_t   num_args,
                              uint8_t   flags)
{
    return _create_func_obj(frame, CREATE_GEN, offset, num_regs, args_reg, num_args, flags);
}

static KOS_OBJ_ID _create_class(KOS_FRAME frame,
                                uint32_t  offset,
                                uint8_t   num_regs,
                                uint8_t   args_reg,
                                uint8_t   num_args,
                                uint8_t   flags)
{
    return _create_func_obj(frame, CREATE_CLASS, offset, num_regs, args_reg, num_args, flags);
}

static KOS_OBJ_ID _read_stack_reg(KOS_OBJ_ID stack_obj_id,
                                  int        idx)
{
    uint32_t   size;
    KOS_OBJ_ID ret;

    assert(GET_OBJ_TYPE(stack_obj_id) == OBJ_STACK);

    size = KOS_atomic_read_u32(OBJPTR(STACK, stack_obj_id)->size);

    assert(OBJPTR(STACK, stack_obj_id)->header.flags & KOS_REENTRANT_STACK);

    assert(idx >= 0);
    if (idx < 0)
        return KOS_BADPTR;

    assert((uint32_t)idx + KOS_STACK_EXTRA < size);
    if ((uint32_t)idx + KOS_STACK_EXTRA >= size)
        return KOS_BADPTR;

    ret = (KOS_OBJ_ID)KOS_atomic_read_ptr(OBJPTR(STACK, stack_obj_id)->buf[idx + KOS_STACK_EXTRA]);
    return ret;
}

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* SET, GET.PROP */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    2, (uint8_t)(int8_t)-6,
            INSTR_SET,          0, 1, 2,
            INSTR_LOAD_INT8,    2, 0,
            INSTR_GET_PROP,     3, 0, IMM32(0),
            INSTR_RETURN,       0, 3
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, &str_prop, 1) == TO_SMALL_INT(-6));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP, GET */
    {
        static const char prop2[] = "prop2";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop2);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-7,
            INSTR_SET_PROP,     0, IMM32(0)/*"prop2"*/, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop2"*/
            INSTR_GET,          1, 0, 1,
            INSTR_RETURN,       0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == TO_SMALL_INT(-7));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET, GET.ELEM */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY, 0, IMM32(5),
            INSTR_LOAD_INT8,  1, 3,
            INSTR_LOAD_INT8,  2, 10,
            INSTR_SET,        0, 1, 2,
            INSTR_GET_ELEM,   1, 0, IMM32(-2),
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0, 0) == TO_SMALL_INT(10));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM, GET */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY, 0, IMM32(3),
            INSTR_LOAD_INT8,  1, (uint8_t)(int8_t)-8,
            INSTR_SET_ELEM,   0, IMM32(2), 1,
            INSTR_LOAD_INT8,  1, (uint8_t)(int8_t)-1,
            INSTR_GET,        2, 0, 1,
            INSTR_RETURN,     0, 2
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0, 0) == TO_SMALL_INT(-8));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET,          0, 0, 1,
            INSTR_RETURN,       0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid index type for object */
    {
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,  0,
            INSTR_LOAD_TRUE, 1,
            INSTR_LOAD_INT8, 2, (uint8_t)(int8_t)-6,
            INSTR_SET,       0, 1, 2,
            INSTR_RETURN,    0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid index type for object */
    {
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT8,  1, 0,
            INSTR_SET,        0, 1, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET_PROP,     0, IMM32(0)/*"prop1"*/, 1,
            INSTR_RETURN,       0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET_ELEM,     0, IMM32(0), 1,
            INSTR_RETURN,       0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - index out of range */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY,   0, IMM32(1),
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,     0, IMM32(1), 1,
            INSTR_RETURN,       0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid index type for array */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,     0, IMM32(0), 0,
            INSTR_RETURN,       0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP, HAS.PROP */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop5);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT8,  1, (uint8_t)(int8_t)-9,
            INSTR_SET_PROP,   0, IMM32(0)/*"prop5"*/, 1,
            INSTR_HAS_PROP,   2, 0, IMM32(0),/*"prop5"*/
            INSTR_RETURN,     0, 2
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &str_prop, 1) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* PUSH */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop5);
        KOS_OBJ_ID        ret;
        KOS_OBJ_ID        val;
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8,  0, 0,
            INSTR_LOAD_ARRAY8,  1, 1,
            INSTR_LOAD_INT8,    2, 10,
            INSTR_SET_ELEM,     1, IMM32(0), 2,
            INSTR_PUSH,         1, 0,
            INSTR_PUSH,         1, 1,
            INSTR_LOAD_CONST,   2, IMM32(0)/*"prop5"*/,
            INSTR_PUSH,         1, 2,
            INSTR_RETURN,       0, 1
        };

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &str_prop, 1);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 4);
        TEST(KOS_array_read(frame, ret, 0) == TO_SMALL_INT(10));

        val = KOS_array_read(frame, ret, 1);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_ARRAY);
        TEST(KOS_get_array_size(val) == 0);

        TEST(KOS_array_read(frame, ret, 2) == ret);
        TEST(KOS_array_read(frame, ret, 3) == str_prop);
    }

    /************************************************************************/
    /* PUSH.EX */
    {
        static const char prop5[]  = "01";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop5);
        KOS_OBJ_ID        ret;
        KOS_OBJ_ID        val;
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8,  0, 0,
            INSTR_LOAD_ARRAY8,  1, 1,
            INSTR_LOAD_INT8,    2, 10,
            INSTR_SET_ELEM,     1, IMM32(0), 2,
            INSTR_PUSH_EX,      1, 0,
            INSTR_PUSH_EX,      1, 1,
            INSTR_LOAD_CONST,   2, IMM32(0)/*"01"*/,
            INSTR_PUSH_EX,      1, 2,
            INSTR_RETURN,       0, 1
        };

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &str_prop, 1);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 4);
        TEST(KOS_array_read(frame, ret, 0) == TO_SMALL_INT(10));
        TEST(KOS_array_read(frame, ret, 1) == TO_SMALL_INT(10));

        val = KOS_array_read(frame, ret, 2);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_STRING);
        TEST(KOS_get_string_length(val) == 1);
        TEST(KOS_string_get_char_code(frame, val, 0) == 0x30);

        val = KOS_array_read(frame, ret, 3);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_STRING);
        TEST(KOS_get_string_length(val) == 1);
        TEST(KOS_string_get_char_code(frame, val, 0) == 0x31);
    }

    /************************************************************************/
    /* DEL.PROP */
    {
        static const char prop6[]  = "prop6";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop6);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT8,  1, (uint8_t)(int8_t)-10,
            INSTR_SET_PROP,   0, IMM32(0),/*"prop6"*/ 1,
            INSTR_DEL_PROP,   0, IMM32(0),/*"prop6"*/
            INSTR_HAS_PROP,   1, 0, IMM32(0),/*"prop6"*/
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL.PROP - delete non-existent property */
    {
        static const char prop6[]  = "prop6";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop6);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ, 0,
            INSTR_DEL_PROP, 0, IMM32(0),/*"prop6"*/
            INSTR_HAS_PROP, 0, 0, IMM32(0),/*"prop6"*/
            INSTR_RETURN,   0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-10,
            INSTR_SET_PROP,     0, IMM32(0)/*"prop7"*/, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop7*/
            INSTR_DEL,          0, 1,
            INSTR_HAS_PROP,     1, 0, IMM32(0),/*"prop7"*/
            INSTR_RETURN,       0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL - delete non-existent property */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop7*/
            INSTR_DEL,          0, 1,
            INSTR_HAS_PROP,     1, 0, IMM32(0),/*"prop7"*/
            INSTR_RETURN,       0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP */
    {
        const uint8_t code[] = {
            INSTR_LOAD_TRUE,  0,
            INSTR_JUMP,       IMM32(2),
            INSTR_LOAD_FALSE, 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP */
    {
        const uint8_t code[] = {
            INSTR_LOAD_INT8,  0, 1,
            INSTR_JUMP,       IMM32(10),
            INSTR_LOAD_INT8,  1, 2,
            INSTR_ADD,        0, 0, 1,
            INSTR_RETURN,     0, 0,
            INSTR_LOAD_INT8,  1, 3,
            INSTR_ADD,        0, 0, 1,
            INSTR_JUMP,       IMM32(-22),
            INSTR_LOAD_VOID,  0,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0);
        TEST_NO_EXCEPTION();

        TEST(!IS_BAD_PTR(ret));
        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 6);
    }

    /************************************************************************/
    /* JUMP.COND */
    {
        const uint8_t code[] = {
            INSTR_LOAD_TRUE,  0,
            INSTR_JUMP_COND,  IMM32(2), 0,
            INSTR_LOAD_FALSE, 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP.NOT.COND */
    {
        const uint8_t code[] = {
            INSTR_LOAD_TRUE,     0,
            INSTR_JUMP_NOT_COND, IMM32(2), 0,
            INSTR_LOAD_FALSE,    0,
            INSTR_RETURN,        0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP.NOT.COND */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FALSE,    0,
            INSTR_JUMP_NOT_COND, IMM32(2), 0,
            INSTR_LOAD_TRUE,     0,
            INSTR_RETURN,        0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(3),

            INSTR_RETURN,      0, 0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY8, 1, 1,
            INSTR_LOAD_INT8,   2, 42,
            INSTR_SET_ELEM,    1, IMM32(0), 2,
            INSTR_LOAD_VOID,   2,
            INSTR_CALL,        0, 0, 2, 1,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 2, 0, 1, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST,  0, IMM32(0),
            INSTR_LOAD_ARRAY8, 1, 1,
            INSTR_LOAD_INT8,   2, 100,
            INSTR_SET_ELEM,    1, IMM32(0), 2,
            INSTR_LOAD_VOID,   2,
            INSTR_CALL,        0, 0, 2, 1,
            INSTR_LOAD_INT8,   1, 2,
            INSTR_ADD,         0, 0, 1,
            INSTR_RETURN,      0, 0,

            INSTR_MUL,         0, 0, 0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 36, 2, 0, 1, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 10002);
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(3),

            INSTR_RETURN,     0, 0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 121,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 1, 0, 0, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 121);
    }

    /************************************************************************/
    /* CALL.N */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(3),

            INSTR_RETURN,     0, 0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 42,
            INSTR_LOAD_VOID,  2,
            INSTR_CALL_N,     0, 0, 2, 1, 1,
            INSTR_RETURN,     0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 2, 0, 1, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* CALL.N - zero args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(6),

            INSTR_LOAD_INT8,  0, 43,
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_VOID,  1,
            INSTR_CALL_N,     0, 0, 1, 255, 0,
            INSTR_RETURN,     0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 1, 0, 0, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 43);
    }

    /************************************************************************/
    /* CALL.FUN */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(3),

            INSTR_RETURN,     0, 0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 42,
            INSTR_CALL_FUN,   0, 0, 1, 1,
            INSTR_RETURN,     0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 2, 0, 1, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* CALL.FUN - zero args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(6),

            INSTR_LOAD_INT8,  0, 44,
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_CALL_FUN,   0, 0, 255, 0,
            INSTR_RETURN,     0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 1, 0, 0, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 44);
    }

    /************************************************************************/
    /* CALL.FUN - too many args */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 42,
            INSTR_CALL_FUN,    0, 0, 1, 1,
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_INT8,   0, 43,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 14, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == TO_SMALL_INT(43));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL.N - reuse function body twice */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,                     /* this function adds 130 */
            INSTR_LOAD_CONST8, 1, 1,                     /* this function adds 100 */
            INSTR_LOAD_INT8,   2, 5,
            INSTR_CALL_N,      2, 0, 2, 255, 0,          /* effectively add 130 */
            INSTR_CALL_N,      2, 1, 2, 255, 0,          /* effectively add 100 */
            INSTR_RETURN,      0, 2,

            INSTR_LOAD_INT8,   1, 30,
            INSTR_ADD,         0, 0, 1,                  /* add 30 to this */
            INSTR_LOAD_INT8,   1, 100,
            INSTR_ADD,         0, 0, 1,                  /* add 100 to this */
            INSTR_RETURN,      0, 0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = _create_func(frame, 24, 2, 0, 0, 0);
        constants[1] = _create_func(frame, 31, 2, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, constants, 2) == TO_SMALL_INT(235));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not a function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,   0,
            INSTR_LOAD_ARRAY8, 1, 0,
            INSTR_CALL,        0, 0, 0, 1,
            INSTR_RETURN,      0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - args not an array */
    {
        static const char str[]    = "str";
        KOS_OBJ_ID        constants[2];
        const uint8_t     code[] = {
            INSTR_JUMP,         IMM32(3),

            INSTR_RETURN,       0, 0,

            INSTR_LOAD_CONST8,  0, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),
            INSTR_LOAD_VOID,    2,
            INSTR_CALL,         0, 0, 2, 1,
            INSTR_RETURN,       0, 0
        };

        constants[0] = KOS_context_get_cstring(frame, str);
        constants[1] = _create_func(frame, 5, 2, 0, 1, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, constants, 2) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not enough args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(3),

            INSTR_RETURN,      0, 0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY8, 2, 0,
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 2, 0, 10, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.FUN - not enough args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(3),

            INSTR_RETURN,      0, 0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 255, 0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 5, 2, 0, 1, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL constructor */
    {
        static const char str[]  = "own property";
        KOS_OBJ_ID        constants[4];
        KOS_OBJ_ID        ret;
        const uint8_t     code[] = {
            INSTR_JUMP,        IMM32(10),

            INSTR_SET_PROP,    1, IMM32(0), 0,
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_FUN8,   0, 2,
            INSTR_LOAD_CONST,  1, IMM32(1),
            INSTR_CALL_FUN,    0, 0, 1, 1,
            INSTR_RETURN,      0, 0
        };

        constants[0] = KOS_context_get_cstring(frame, str);
        constants[1] = TO_SMALL_INT(0xC0DEU);
        constants[2] = _create_class(frame, 5, 2, 0, 1, 0);
        constants[3] = KOS_new_object(frame); /* prototype */

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, constants, 4);
        TEST_NO_EXCEPTION();

        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_OBJECT);
        TEST(KOS_get_property(frame, ret, KOS_context_get_cstring(frame, str)) == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL constructor */
    {
        static const char str[]    = "own property";
        KOS_OBJ_ID        ret;
        const uint8_t     code[]   = {
            INSTR_JUMP,        IMM32(12),

            INSTR_SET_PROP,    1, IMM32(0), 0,
            INSTR_LOAD_VOID,   0,                /* return value is ignored */
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_FUN8,   0, 2,
            INSTR_LOAD_ARRAY8, 1, 1,             /* create arguments array */
            INSTR_LOAD_CONST,  2, IMM32(1),
            INSTR_SET_ELEM,    1, IMM32(0), 2,   /* set argument */
            INSTR_CALL,        0, 0, 1, 1,
            INSTR_RETURN,      0, 0
        };

        KOS_OBJ_ID constants[4];
        constants[0] = KOS_context_get_cstring(frame, str);
        constants[1] = TO_SMALL_INT(0xC0DEU);
        constants[2] = _create_class(frame, 5, 2, 0, 1, 0);
        constants[3] = KOS_new_object(frame); /* prototype */

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, constants, 4);
        TEST_NO_EXCEPTION();

        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_OBJECT);
        TEST(KOS_get_property(frame, ret, KOS_context_get_cstring(frame, str)) == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(3),

            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(0),
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_CALL,       1, 0, 1, 1,

            INSTR_INSTANCEOF, 0, 1, 0,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = _create_class(frame, 5, 1, 0, 0, 0);
        constants[1] = KOS_new_object(frame); /* prototype */

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, constants, 2) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    /* The same function addresses - the same default prototypes */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(3),

            INSTR_RETURN,        0, 0,

            INSTR_LOAD_FUN8,     2, 0,
            INSTR_LOAD_FUN8,     3, 0,
            INSTR_CALL_N,        4, 2, 2, 0, 0,
            INSTR_CALL_N,        5, 3, 3, 0, 0,

            INSTR_LOAD_FALSE,    0,
            INSTR_INSTANCEOF,    1, 4, 2,
            INSTR_JUMP_NOT_COND, IMM32(32), 1,  /* if ! (4 instanceof 2) { return false; } */
            INSTR_INSTANCEOF,    1, 5, 2,
            INSTR_JUMP_NOT_COND, IMM32(22), 1,  /* if ! (5 instanceof 2) { return false; } */
            INSTR_INSTANCEOF,    1, 4, 3,
            INSTR_JUMP_NOT_COND, IMM32(12), 1,  /* if ! (4 instanceof 3) { return false; } */
            INSTR_INSTANCEOF,    1, 5, 3,
            INSTR_JUMP_NOT_COND, IMM32(2), 1,   /* if ! (5 instanceof 3) { return false; } */

            INSTR_LOAD_TRUE,     0,             /* If everything went OK, return true.     */

            INSTR_RETURN,        0, 0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = _create_class(frame, 5, 1, 0, 0, 0);
        constants[1] = KOS_new_object(frame); /* prototype */

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, constants, 2) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    /* Different function addresses - different default prototypes */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(6),

            INSTR_RETURN,        0, 0,
            INSTR_RETURN,        0, 0,

            INSTR_LOAD_FUN8,     2, 0,
            INSTR_LOAD_FUN8,     3, 2,
            INSTR_CALL_N,        4, 2, 2, 0, 0,
            INSTR_CALL_N,        5, 3, 3, 0, 0,

            INSTR_LOAD_FALSE,    0,
            INSTR_INSTANCEOF,    1, 4, 2,
            INSTR_JUMP_NOT_COND, IMM32(32), 1,  /* if ! (4 instanceof 2) { return false; } */
            INSTR_INSTANCEOF,    1, 5, 3,
            INSTR_JUMP_NOT_COND, IMM32(22), 1,  /* if ! (5 instanceof 3) { return false; } */
            INSTR_INSTANCEOF,    1, 4, 3,
            INSTR_JUMP_COND,     IMM32(12), 1,  /* if 4 instanceof 3 { return false; }     */
            INSTR_INSTANCEOF,    1, 5, 2,
            INSTR_JUMP_COND,     IMM32(2), 1,   /* if 5 instanceof 2 { return false; }     */

            INSTR_LOAD_TRUE,     0,             /* If everything went OK, return true.     */

            INSTR_RETURN,        0, 0
        };

        KOS_OBJ_ID constants[4];
        constants[0] = _create_class(frame, 5, 1, 0, 0, 0);
        constants[1] = KOS_new_object(frame); /* prototype */
        constants[2] = _create_class(frame, 8, 1, 0, 0, 0);
        constants[3] = KOS_new_object(frame); /* prototype */

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, constants, 4) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL - instantiate generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_gen(frame, 3, 1, 0, 0, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(!IS_BAD_PTR(ret));
        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_FUNCTION);
        TEST(OBJPTR(FUNCTION, ret)->state == KOS_GEN_READY);
        TEST(OBJPTR(FUNCTION, ret)->generator_stack_frame != 0);
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL.N/FUN */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 1,
            INSTR_LOAD_CONST8, 1, 0,              /* generator yields 'this' */
            INSTR_CALL_N,      0, 0, 1, 255, 0,   /* instantiate generator   */
            INSTR_CALL_FUN,    0, 0, 2, 0,        /* invoke generator        */
            INSTR_RETURN,      0, 0,

            INSTR_YIELD,       0,                 /* generator yields 'this' */
            INSTR_RETURN,      0, 0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = TO_SMALL_INT(0xCAFEU);
        constants[1] = _create_gen(frame, 20, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, constants, 2) == TO_SMALL_INT(0xCAFEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), YIELD */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 100, 0,
            INSTR_CALL_FUN,    0, 0, 100, 0,
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_YIELD,       0
        };
        KOS_OBJ_ID func = _create_gen(frame, 16, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, &func, 1) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL - ensure that YIELD resets the register to 'void' */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 0,          /* generator will yield 'this' first */
            INSTR_CALL_N,      0, 0, 1, 0, 0, /* instantiate generator */

            INSTR_CALL_FUN,    1, 0, 0, 0,    /* yields 0 ('this') */
            INSTR_CALL_FUN,    1, 0, 0, 0,    /* yields 'void', because args are empty */
            INSTR_RETURN,      0, 1,

            INSTR_YIELD,       0,
            INSTR_JUMP,        IMM32(-7)
        };
        KOS_OBJ_ID func = _create_gen(frame, 25, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, &func, 1) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), YIELD, CALL.GEN */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_LOAD_ARRAY8,   2, 2,
            INSTR_LOAD_INT8,     1, 3,
            INSTR_SET_ELEM,      2, IMM32(0), 1,    /* begin (3) */
            INSTR_LOAD_INT8,     1, 6,
            INSTR_SET_ELEM,      2, IMM32(1), 1,    /* end (6) */
            INSTR_LOAD_VOID,     1,
            INSTR_CALL,          0, 0, 1, 2,        /* instantiate generator */

            INSTR_LOAD_ARRAY8,   2, 0,
            INSTR_CALL,          3, 0, 1, 2,        /* yields 3 */
            INSTR_CALL,          4, 0, 1, 2,        /* yields 4 */
            INSTR_ADD,           3, 3, 4,
            INSTR_CALL_GEN,      4, 0, 1,           /* yields 5 */
            INSTR_ADD,           3, 3, 4,
            INSTR_JUMP_NOT_COND, IMM32(3), 1,
            INSTR_LOAD_INT8,     3, 0,
            INSTR_CALL_GEN,      4, 0, 1,           /* no more */
            INSTR_JUMP_COND,     IMM32(3), 1,
            INSTR_LOAD_INT8,     3, 0,
            INSTR_RETURN,        0, 3,

            INSTR_JUMP,          IMM32(12),
            INSTR_MOVE,          2, 0,
            INSTR_YIELD,         2,
            INSTR_LOAD_INT8,     2, 1,
            INSTR_ADD,           0, 0, 2,
            INSTR_CMP_LT,        2, 0, 1,
            INSTR_JUMP_COND,     IMM32(-22), 2,
            INSTR_LOAD_VOID,     2,
            INSTR_RETURN,        0, 2
        };
        KOS_OBJ_ID func = _create_gen(frame, 83, 3, 0, 2, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 5, &func, 1) == TO_SMALL_INT(3+4+5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL - not enough args */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 0,
            INSTR_LOAD_ARRAY8, 2, 1,
            INSTR_CALL,        0, 0, 1, 2, /* instantiate generator */
            INSTR_RETURN,      0, 1,

            INSTR_YIELD,       1
        };
        KOS_OBJ_ID func = _create_gen(frame, 17, 3, 0, 2, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL - args not an array */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 0,
            INSTR_LOAD_VOID,   2,
            INSTR_CALL,        0, 0, 1, 2, /* instantiate generator */
            INSTR_RETURN,      0, 1,

            INSTR_YIELD,       1
        };
        KOS_OBJ_ID func = _create_gen(frame, 16, 2, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), CALL - one arg passed to generator in "READY" state */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 120,
            INSTR_CALL_N,      0, 0, 1, 0, 1, /* instantiate generator */
            INSTR_CALL_FUN,    0, 0, 0, 0,    /* invoke generator */
            INSTR_RETURN,      0, 0,

            INSTR_YIELD,       1
        };
        KOS_OBJ_ID func = _create_gen(frame, 20, 2, 0, 1, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == TO_SMALL_INT(120));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* YIELD - pass data to generator through yield */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 100,    /* bind initial value, 'this', used by the generator */
            INSTR_LOAD_ARRAY8, 2, 0,
            INSTR_CALL,        0, 0, 1, 2,       /* instantiate generator */

            INSTR_CALL,        3, 0, 1, 2,       /* yields 101 */
            INSTR_LOAD_ARRAY8, 2, 1,
            INSTR_ADD,         4, 3, 3,          /* 202 */
            INSTR_LOAD_INT8,   3, 64,
            INSTR_SET_ELEM,    2, IMM32(0), 3,
            INSTR_CALL,        3, 0, 1, 2,       /* yields 65 */
            INSTR_ADD,         4, 4, 3,          /* 267 */
            INSTR_LOAD_INT8,   3, 16,
            INSTR_SET_ELEM,    2, IMM32(0), 3,
            INSTR_CALL,        3, 0, 1, 2,       /* yields 17 */
            INSTR_SUB,         4, 4, 3,          /* 250 */
            INSTR_RETURN,      0, 4,

            INSTR_LOAD_INT8,   1, 1,
            INSTR_ADD,         0, 0, 1,    /* use 'this' as the initial value */
            INSTR_YIELD,       0,
            INSTR_JUMP,        IMM32(-11)
        };
        KOS_OBJ_ID func = _create_gen(frame, 67, 2, 0, 0, 0);

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 5, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(!IS_BAD_PTR(ret));
        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 250);
    }

    /************************************************************************/
    /* CALL - call beyond the end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, (uint8_t)(unsigned)-7,
            INSTR_CALL_N,      0, 0, 1, 10, 0, /* instantiate generator */

            INSTR_CALL_FUN,    1, 0, 11, 0,
            INSTR_CALL_FUN,    1, 0, 12, 0,
            INSTR_RETURN,      0, 1,

            INSTR_YIELD,       0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_gen(frame, 25, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - call beyond the end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 0, 0, /* instantiate generator */

            INSTR_CALL_GEN,    2, 0, 1,    /* returns 'true' in register 1 */
            INSTR_CALL_GEN,    2, 0, 2,    /* raise exception */
            INSTR_RETURN,      0, 1,

            INSTR_RETURN,      0, 0,
            INSTR_JUMP,        IMM32(-8)
        };
        KOS_OBJ_ID func = _create_gen(frame, 22, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* YIELD - yield not supported in a regular function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 13,
            INSTR_CALL_N,      0, 0, 1, 10, 0, /* invoke function */
            INSTR_RETURN,      0, 0,

            INSTR_YIELD,       0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 15, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - put both return value and status in the same register */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY8, 1, 0,
            INSTR_CALL,        0, 0, 1, 1, /* instantiate generator */

            INSTR_CALL_GEN,    0, 0, 0,    /* invoke generator */
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_INT8,   0, 0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_gen(frame, 18, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - put both return value and status in the same register */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY,  1, IMM32(0),
            INSTR_CALL,        0, 0, 1, 1, /* instantiate generator */

            INSTR_CALL_GEN,    0, 0, 0,    /* invoke generator */
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_INT8,   0, 0,
            INSTR_YIELD,       0,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_gen(frame, 21, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* TAIL.CALL */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY8, 1, 0,
            INSTR_TAIL_CALL,   0, 0, 0, 1,

            /* unreachable */
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 16, 1, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, &func, 1) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* TAIL.CALL.N */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 3,
            INSTR_LOAD_INT8,   2, 20,
            INSTR_LOAD_INT8,   3, 100,
            INSTR_TAIL_CALL_N, 0, 0, 1, 2, 2,

            /* unreachable */
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0, 0,

            INSTR_ADD,         0, 0, 0,        /* arg 0 - 100 */
            INSTR_ADD,         0, 0, 1,        /* arg 1 - 20  */
            INSTR_ADD,         0, 0, 2,        /* this  - 3   */
            INSTR_RETURN,      0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 23, 3, 0, 2, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, &func, 1) == TO_SMALL_INT(143));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* TAIL.CALL.FUN */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_LOAD_INT8,     1, 20,
            INSTR_LOAD_INT8,     2, 100,
            INSTR_TAIL_CALL_FUN, 0, 0, 1, 2,

            /* unreachable */
            INSTR_LOAD_VOID,     0,
            INSTR_RETURN,        0, 0,

            INSTR_ADD,           0, 0, 0,        /* arg 0 - 100 */
            INSTR_ADD,           0, 0, 1,        /* arg 1 - 20  */
            INSTR_RETURN,        0, 0
        };
        KOS_OBJ_ID func = _create_func(frame, 19, 3, 0, 2, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1) == TO_SMALL_INT(140));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - nothing is thrown */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(6),
            INSTR_LOAD_INT8,  0, 0,
            INSTR_RETURN,     0, 0,
            INSTR_LOAD_INT8,  0, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == TO_SMALL_INT(0));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - throw a number */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(8),
            INSTR_LOAD_INT8,  0, 0,
            INSTR_LOAD_INT8,  1, 1,
            INSTR_THROW,      1,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID obj = _run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_property(frame, obj, KOS_context_get_cstring(frame, str_value)) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - catch when invalid instruction operands cause exception */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(9),
            INSTR_LOAD_VOID,  0,
            INSTR_SET,        0, 0, 0, /* throws */
            INSTR_RETURN,     0, 0,
            INSTR_LOAD_TRUE,  0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - unset catch */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(7),
            INSTR_CANCEL,
            INSTR_LOAD_FALSE, 0,
            INSTR_LOAD_TRUE,  1,
            INSTR_THROW,      1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - catch exception from another function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY8, 2, 0,
            INSTR_CATCH,       1, IMM32(5),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 1,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_LOAD_FALSE,  1,
            INSTR_THROW,       0,
            INSTR_RETURN,      0, 1
        };
        KOS_OBJ_ID func = _create_func(frame, 22, 2, 0, 0, 0);

        KOS_OBJ_ID obj = _run_code(&ctx, frame, &code[0], sizeof(code), 3, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_property(frame, obj, KOS_context_get_cstring(frame, str_value)) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - several levels of catch */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 1,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(8),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP,    0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_RETURN,      0, 0,

            INSTR_LOAD_CONST8, 0, 2,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(8),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP,    0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_THROW,       0,
            INSTR_RETURN,      0, 1,

            INSTR_LOAD_CONST8, 0, 3,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(8),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0, 1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP,    0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_THROW,       0,
            INSTR_RETURN,      0, 1,

            INSTR_LOAD_INT8,   0, 1,
            INSTR_THROW,       0
        };

        KOS_OBJ_ID constants[4];
        constants[0] = KOS_context_get_cstring(frame, str_value);
        constants[1] = _create_func(frame,  42, 3, 0, 0, 0);
        constants[2] = _create_func(frame,  86, 3, 0, 0, 0);
        constants[3] = _create_func(frame, 130, 2, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, constants, 4) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_BIND_SELF,   0, 0,
            INSTR_LOAD_VOID,   1,
            INSTR_CALL_N,      0, 0, 1, 21, 0,       /* returns the last function */
            INSTR_CALL_N,      0, 0, 1, 23, 0,       /* sets register 1 to 41     */
            INSTR_RETURN,      0, 1,

            /* reg 1 is register array of the main function */
            INSTR_LOAD_CONST8, 2, 1,
            INSTR_BIND_SELF,   2, 0,                 /* bind own registers    */
            INSTR_BIND,        2, 1, 1,              /* bind main's registers */
            INSTR_LOAD_INT8,   0, 37,
            INSTR_RETURN,      1, 2,                 /* leave one reg (reg 0) */

            /* reg 1 is register array of the above function */
            /* reg 2 is register array of the main function  */
            INSTR_GET_ELEM,    0, 1, IMM32(0),
            INSTR_LOAD_INT8,   1, 4,
            INSTR_ADD,         0, 0, 1,
            INSTR_SET_ELEM,    2, IMM32(1), 0,
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0, 0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = _create_func(frame, 23, 3, 0, 0, KOS_FUN_CLOSURE);
        constants[1] = _create_func(frame, 39, 3, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, constants, 2) == TO_SMALL_INT(41));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF */
    {
        const uint8_t code[]   = {
            INSTR_LOAD_CONST8, 0, 1,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_LOAD_ARRAY,  3, IMM32(1),
            INSTR_BIND_SELF,   0, 0,
            INSTR_BIND,        0, 1, 3,
            INSTR_CALL,        0, 0, 1, 2,           /* instantiate generator */

            INSTR_LOAD_INT8,   4, (uint8_t)(int8_t)-100,
            INSTR_SET_ELEM,    3, IMM32(0), 4,
            INSTR_LOAD_CONST,  4, IMM32(0),

            INSTR_CALL,        0, 0, 1, 2,           /* add 3[0] to 4 */
            INSTR_RETURN,      0, 4,

            /* reg 1 is register array of the main function        */
            /* reg 2 is array from register 3 in the main function */
            INSTR_GET_ELEM,    0, 2, IMM32(0),
            INSTR_GET_ELEM,    3, 1, IMM32(4),
            INSTR_ADD,         0, 0, 3,
            INSTR_SET_ELEM,    1, IMM32(4), 0,
            INSTR_LOAD_INT8,   0, 0,
            INSTR_YIELD,       0
        };

        KOS_OBJ_ID constants[2];
        constants[0] = TO_SMALL_INT(-200);
        constants[1] = _create_gen(frame, 53, 4, 0, 0, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 5, constants, 2) == TO_SMALL_INT(-300));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND - cannot bind to void (non-function) */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,  0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_BIND,       0, 0, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* BIND.SELF - cannot bind to void (non-function) */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,  0,
            INSTR_BIND_SELF,  0, 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF - independent variables */
    {
        const uint8_t code[] = {
            INSTR_LOAD_INT8,   4, 3,                 /* Read by level 1 */
            INSTR_LOAD_CONST,  5, IMM32(0),          /* Read by level 2 */
            INSTR_LOAD_ARRAY,  1, IMM32(2),
            INSTR_LOAD_INT8,   0, 9,
            INSTR_SET_ELEM,    1, IMM32(0), 0,
            INSTR_LOAD_CONST,  0, IMM32(1),
            INSTR_SET_ELEM,    1, IMM32(1), 0,
            INSTR_LOAD_INT8,   0, 4,
            INSTR_LOAD_CONST,  2, IMM32(2),
            INSTR_LOAD_FUN8,   3, 4,                 /* Overwritten by this function with level 2 */
            INSTR_BIND_SELF,   3, 0,
            INSTR_BIND,        3, 1, 2,
            INSTR_CALL,        0, 3, 0, 1,           /* Returns 0x10 */
            INSTR_LOAD_INT8,   1, 2,
            INSTR_SHL,         0, 0, 1,              /* 0x40 */
            INSTR_LOAD_ARRAY,  1, IMM32(3),
            INSTR_SET_ELEM,    1, IMM32(2), 0,       /* 0x100 */
            INSTR_LOAD_INT8,   0, 5,
            INSTR_CALL,        2, 3, 0, 1,
            INSTR_RETURN,      0, 2,

            /* Level 1: this outer function starts with:
             * 0 - independent var
             * 1 - arg 0 (bound but not preserved)
             * 2 - arg 1 (bound but not preserved)
             * 3 - this
             * 4 - global regs
             * 5 - global integer */
            INSTR_GET_ELEM,    6, 4, IMM32(-2),      /* 3 */
            INSTR_LOAD_CONST,  0, IMM32(3),
            INSTR_ADD,         3, 3, 1,              /* 4+9 = 13 */
            INSTR_ADD,         3, 3, 6,              /* 13+3 = 0x10 */
            INSTR_LOAD_FUN8,   6, 5,
            INSTR_BIND,        6, 0, 4,
            INSTR_BIND,        6, 1, 5,
            INSTR_BIND_SELF,   6, 2,
            INSTR_BIND,        6, 3, 2,
            INSTR_BIND,        6, 4, 3,
            INSTR_SET_ELEM,    4, IMM32(-3), 6,
            INSTR_RETURN,      1, 3,

            /* Level 2: this inner function starts with:
             * 98  - arg 0
             * 99  - arg 1
             * 100 - arg 2
             * 101 - this
             * 102 - global regs
             * 103 - global integer
             * 104 - level 1 regs
             * 105 - level 1 arg 0
             * 106 - level 1 integer */
            INSTR_MOVE,        0, 100,               /* 0x40 */
            INSTR_GET_ELEM,    2, 102, IMM32(-1),    /* 0x40000 */
            INSTR_GET_ELEM,    4, 104, IMM32(-1),    /* 0x1000 */
            INSTR_OR,          0, 0, 101,            /* 0x40 | 0x5 */
            INSTR_OR,          0, 0, 2,              /* | 0x40000 */
            INSTR_OR,          0, 0, 103,            /* | 0x20000 */
            INSTR_OR,          0, 0, 4,              /* | 0x1000 */
            INSTR_OR,          0, 0, 105,            /* | 0x8000 */
            INSTR_OR,          0, 0, 106,            /* | 0x10 */
            INSTR_RETURN,      0, 0
        };

        KOS_OBJ_ID constants[6] = {
            TO_SMALL_INT(0x40000),
            TO_SMALL_INT(0x8000),
            TO_SMALL_INT(0x20000),
            TO_SMALL_INT(0x1000),
            TO_SMALL_INT(0),
            TO_SMALL_INT(0)
        };
        constants[4] = _create_func(frame,  93,   7,  1, 2, KOS_FUN_CLOSURE);
        constants[5] = _create_func(frame, 146, 107, 98, 3, 0);

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, constants, 6) == TO_SMALL_INT(0x69055));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND.DEFAULTS - all default values */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8,   0, 3,
            INSTR_LOAD_INT8,     1, 10,
            INSTR_SET_ELEM,      0, IMM32(0), 1,
            INSTR_LOAD_INT8,     1, 11,
            INSTR_SET_ELEM,      0, IMM32(1), 1,
            INSTR_LOAD_INT8,     1, 12,
            INSTR_SET_ELEM,      0, IMM32(2), 1,

            INSTR_LOAD_FUN8,     1, 0,
            INSTR_BIND_DEFAULTS, 1, 0,
            INSTR_TAIL_CALL_FUN, 0, 1, 255, 0,

            INSTR_LOAD_FUN8,     3, 1,
            INSTR_BIND_SELF,     3, 0,
            INSTR_TAIL_CALL_FUN, 3, 3, 255, 0,

            INSTR_RETURN,        0, 1
        };
        KOS_OBJ_ID ret;

        KOS_OBJ_ID constants[2];
        constants[0] = _create_func(frame, 44, 4, 0, 0, KOS_FUN_CLOSURE);
        constants[1] = _create_func(frame, 55, 2, 0, 0, KOS_FUN_CLOSURE);

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, constants, 2);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_STACK);
        TEST(KOS_atomic_read_u32(OBJPTR(STACK, ret)->size) == 1U + KOS_STACK_EXTRA + 3U);
        TEST(KOS_atomic_read_ptr(OBJPTR(STACK, ret)->buf[KOS_STACK_EXTRA + 0]) == TO_SMALL_INT(10));
        TEST(KOS_atomic_read_ptr(OBJPTR(STACK, ret)->buf[KOS_STACK_EXTRA + 1]) == TO_SMALL_INT(11));
        TEST(KOS_atomic_read_ptr(OBJPTR(STACK, ret)->buf[KOS_STACK_EXTRA + 2]) == TO_SMALL_INT(12));
    }

    /************************************************************************/
    /* BIND.DEFAULTS - some default values */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8,   0, 3,
            INSTR_LOAD_INT8,     1, 20,
            INSTR_SET_ELEM,      0, IMM32(0), 1,
            INSTR_LOAD_INT8,     1, 21,
            INSTR_SET_ELEM,      0, IMM32(1), 1,
            INSTR_LOAD_INT8,     1, 22,
            INSTR_SET_ELEM,      0, IMM32(2), 1,

            INSTR_LOAD_FUN8,     2, 0,
            INSTR_BIND_DEFAULTS, 2, 0,
            INSTR_LOAD_INT8,     0, 5,
            INSTR_LOAD_INT8,     1, 6,
            INSTR_TAIL_CALL_FUN, 0, 2, 0, 2,

            INSTR_LOAD_TRUE,     0,
            INSTR_LOAD_FUN8,     5, 1,
            INSTR_BIND_SELF,     5, 0,
            INSTR_TAIL_CALL_FUN, 5, 5, 255, 0,

            INSTR_RETURN,        0, 1
        };
        KOS_OBJ_ID ret;

        KOS_OBJ_ID constants[2];
        constants[0] = _create_func(frame, 50, 6, 1, 1, KOS_FUN_CLOSURE);
        constants[1] = _create_func(frame, 63, 2, 0, 0, 0);

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, constants, 2);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_STACK);
        TEST(KOS_atomic_read_u32(OBJPTR(STACK, ret)->size) == 1U + KOS_STACK_EXTRA + 5U);
        TEST(_read_stack_reg(ret, 0) == KOS_TRUE);
        TEST(_read_stack_reg(ret, 1) == TO_SMALL_INT(5));
        TEST(_read_stack_reg(ret, 2) == TO_SMALL_INT(6));
        TEST(_read_stack_reg(ret, 3) == TO_SMALL_INT(21));
        TEST(_read_stack_reg(ret, 4) == TO_SMALL_INT(22));
    }

    /************************************************************************/
    /* BIND.DEFAULTS - lots of default values and ellipsis, few input args */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_JUMP,          IMM32(31),

            /* 0 - begin
             * 1 - end */
            INSTR_LOAD_ARRAY8,   2, 0,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_JUMP,          IMM32(7),
            INSTR_PUSH,          2, 0,
            INSTR_ADD,           0, 0, 3,
            INSTR_CMP_LT,        4, 0, 1,
            INSTR_JUMP_COND,     IMM32(-17), 4,
            INSTR_RETURN,        0, 2,

            INSTR_LOAD_INT8,     3, 64,
            INSTR_LOAD_INT8,     4, 96,
            INSTR_CALL_FUN,      1, 0, 3, 2,
            INSTR_LOAD_FUN8,     2, 1,
            INSTR_BIND_DEFAULTS, 2, 1,
            INSTR_LOAD_INT8,     3, 7,
            INSTR_LOAD_INT8,     4, 25,
            INSTR_CALL_FUN,      0, 0, 3, 2,
            INSTR_TAIL_CALL,     0, 2, 1, 0,

            INSTR_LOAD_VOID,     0,
            INSTR_LOAD_VOID,     1,
            INSTR_LOAD_VOID,     2,
            INSTR_LOAD_VOID,     3,
            INSTR_LOAD_VOID,     4,
            INSTR_LOAD_FUN8,     _KOS_MAX_ARGS_IN_REGS + 5 + 2, 2,
            INSTR_BIND_SELF,     _KOS_MAX_ARGS_IN_REGS + 5 + 2, 0,
            INSTR_TAIL_CALL_FUN, _KOS_MAX_ARGS_IN_REGS + 5 + 2, _KOS_MAX_ARGS_IN_REGS + 5 + 2, 255, 0,

            INSTR_RETURN,        0, 1
        };

        int i;

        KOS_OBJ_ID obj;
        KOS_OBJ_ID ret;

        KOS_OBJ_ID constants[3];
        constants[0] = _create_func(frame,  8, 5, 0, 2, 0);
        constants[1] = _create_func(frame, 72, _KOS_MAX_ARGS_IN_REGS + 5 + 3, 5, 16, KOS_FUN_ELLIPSIS | KOS_FUN_CLOSURE);
        constants[2] = _create_func(frame, 93, 2, 0, 0, 0);

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 5, constants, 3);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_STACK);
        TEST(KOS_atomic_read_u32(OBJPTR(STACK, ret)->size) == 1U + KOS_STACK_EXTRA +
                                                              _KOS_MAX_ARGS_IN_REGS + 5 + 2);
        for (i = 0; i < 5; i++)
            TEST(_read_stack_reg(ret, i) == KOS_VOID);
        for (i = 5; i < 23; i++)
            TEST(_read_stack_reg(ret, i) == TO_SMALL_INT(i + 2));
        for (i = 23; i < (int)_KOS_MAX_ARGS_IN_REGS + 5 - 1; i++)
            TEST(_read_stack_reg(ret, i) == TO_SMALL_INT(i - 23 + 66));
        /* Rest of args */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS + 5 - 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 48 - _KOS_MAX_ARGS_IN_REGS + 1);
        for (i = 0; i < 48 - (int)_KOS_MAX_ARGS_IN_REGS + 1; i++)
            TEST(KOS_array_read(frame, obj, i) == TO_SMALL_INT(i + (int)_KOS_MAX_ARGS_IN_REGS - 1 - 16 + 64));
        /* Ellipsis */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS + 5);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 0);
        /* this */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS + 5 + 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 32);
        for (i = 0; i < 32; i++)
            TEST(KOS_array_read(frame, obj, i) == TO_SMALL_INT(i + 64));
    }

    /************************************************************************/
    /* BIND.DEFAULTS - lots of args, a few default values and ellipsis */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_JUMP,          IMM32(31),

            /* 0 - begin
             * 1 - end */
            INSTR_LOAD_ARRAY8,   2, 0,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_JUMP,          IMM32(7),
            INSTR_PUSH,          2, 0,
            INSTR_ADD,           0, 0, 3,
            INSTR_CMP_LT,        4, 0, 1,
            INSTR_JUMP_COND,     IMM32(-17), 4,
            INSTR_RETURN,        0, 2,

            INSTR_LOAD_INT8,     3, 100,
            INSTR_LOAD_INT8,     4, 105,
            INSTR_CALL_FUN,      1, 0, 3, 2,
            INSTR_LOAD_FUN8,     2, 1,
            INSTR_BIND_DEFAULTS, 2, 1,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_LOAD_INT8,     4, _KOS_MAX_ARGS_IN_REGS + 10,
            INSTR_CALL_FUN,      0, 0, 3, 2,
            INSTR_TAIL_CALL,     0, 2, 1, 0,

            INSTR_LOAD_FUN8,     _KOS_MAX_ARGS_IN_REGS + 2, 2,
            INSTR_BIND_SELF,     _KOS_MAX_ARGS_IN_REGS + 2, 0,
            INSTR_TAIL_CALL_FUN, _KOS_MAX_ARGS_IN_REGS + 2, _KOS_MAX_ARGS_IN_REGS + 2, 255, 0,

            INSTR_RETURN,        0, 1
        };

        int i;

        KOS_OBJ_ID obj;
        KOS_OBJ_ID ret;

        KOS_OBJ_ID constants[3];
        constants[0] = _create_func(frame,  8, 5, 0, 2, 0);
        constants[1] = _create_func(frame, 72, _KOS_MAX_ARGS_IN_REGS + 3, 0, _KOS_MAX_ARGS_IN_REGS, KOS_FUN_ELLIPSIS | KOS_FUN_CLOSURE);
        constants[2] = _create_func(frame, 83, 2, 0, 0, 0);

        ret = _run_code(&ctx, frame, &code[0], sizeof(code), 5, constants, 3);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_STACK);
        TEST(KOS_atomic_read_u32(OBJPTR(STACK, ret)->size) == 1U + KOS_STACK_EXTRA +
                                                              _KOS_MAX_ARGS_IN_REGS + 2);
        for (i = 0; i < (int)_KOS_MAX_ARGS_IN_REGS - 1; i++)
            TEST(_read_stack_reg(ret, i) == TO_SMALL_INT(i + 1));
        /* Rest of args */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS - 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 6);
        for (i = 0; i < 6; i++)
            TEST(KOS_array_read(frame, obj, i) == TO_SMALL_INT(i + (int)_KOS_MAX_ARGS_IN_REGS));
        /* Ellipsis */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 4);
        for (i = 0; i < 4; i++)
            TEST(KOS_array_read(frame, obj, i) == TO_SMALL_INT(i + (int)_KOS_MAX_ARGS_IN_REGS + 6));
        /* this */
        obj = _read_stack_reg(ret, _KOS_MAX_ARGS_IN_REGS + 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 5);
        for (i = 0; i < 5; i++)
            TEST(KOS_array_read(frame, obj, i) == TO_SMALL_INT(i + 100));
    }

    KOS_context_destroy(&ctx);

    return 0;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2020 Chris Dragan
 */

#include "../inc/kos_array.h"
#include "../inc/kos_bytecode.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_entity.h"
#include "../inc/kos_error.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../core/kos_config.h"
#include "../core/kos_const_strings.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_vm.h"
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning( disable : 4310 ) /* cast truncates constant value */
#endif

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define TEST_EXCEPTION() do { TEST(KOS_is_exception_pending(ctx)); KOS_clear_exception(ctx); } while (0)
#define TEST_NO_EXCEPTION() TEST( ! KOS_is_exception_pending(ctx))

#define IMMPART(val,shift) ((uint8_t)((uint32_t)(val) >> shift))
#define IMM32(val) IMMPART(val, 0), IMMPART(val, 8), IMMPART(val, 16), IMMPART(val, 24)

enum CREATE_FUNC_E {
    CREATE_FUNC,
    CREATE_GEN,
    CREATE_CLASS
};

static KOS_FUNCTION_OPTS create_func_opts(uint8_t num_regs, uint8_t num_args)
{
    KOS_FUNCTION_OPTS opts;

    memset(&opts, 0, sizeof(opts));

    opts.num_regs     = num_regs;
    opts.min_args     = num_args;
    opts.rest_reg     = KOS_NO_REG;
    opts.ellipsis_reg = KOS_NO_REG;
    opts.this_reg     = KOS_NO_REG;
    opts.bind_reg     = KOS_NO_REG;

    if ( ! num_args)
        opts.args_reg = KOS_NO_REG;

    return opts;
}

static KOS_OBJ_ID create_func_obj(KOS_CONTEXT              ctx,
                                  enum CREATE_FUNC_E       create,
                                  uint32_t                 offset,
                                  const KOS_FUNCTION_OPTS *opts)
{
    KOS_OBJ_ID    obj_id;
    KOS_FUNCTION *func;

    if (create == CREATE_CLASS) {
        obj_id = KOS_new_class(ctx, KOS_VOID);
        if (IS_BAD_PTR(obj_id))
            return KOS_BADPTR;
        func   = (KOS_FUNCTION *)OBJPTR(CLASS, obj_id);
    }
    else {
        obj_id = KOS_new_function(ctx);
        if (IS_BAD_PTR(obj_id))
            return KOS_BADPTR;
        func   = OBJPTR(FUNCTION, obj_id);
    }

    func->opts       = *opts;
    func->instr_offs = offset;
    func->module     = ctx->inst->modules.init_module;

    if (create == CREATE_CLASS && func->opts.this_reg == KOS_NO_REG) {
        if (func->opts.rest_reg != KOS_NO_REG)
            func->opts.this_reg = func->opts.rest_reg + 1;
        else if (func->opts.args_reg == KOS_NO_REG)
            func->opts.this_reg = 0;
        else
            func->opts.this_reg = func->opts.args_reg + func->opts.min_args;
    }

    if (create == CREATE_GEN)
        func->state = KOS_GEN_INIT;

    return obj_id;
}

static KOS_OBJ_ID create_func(KOS_CONTEXT              ctx,
                              uint32_t                 offset,
                              const KOS_FUNCTION_OPTS *opts)
{
    return create_func_obj(ctx, CREATE_FUNC, offset, opts);
}

static KOS_OBJ_ID create_gen(KOS_CONTEXT              ctx,
                             uint32_t                 offset,
                             const KOS_FUNCTION_OPTS *opts)
{
    return create_func_obj(ctx, CREATE_GEN, offset, opts);
}

static KOS_OBJ_ID create_class(KOS_CONTEXT              ctx,
                               uint32_t                 offset,
                               const KOS_FUNCTION_OPTS *opts)
{
    return create_func_obj(ctx, CREATE_CLASS, offset, opts);
}

static KOS_OBJ_ID run_code(KOS_INSTANCE  *inst,
                           KOS_CONTEXT    ctx,
                           const uint8_t *bytecode,
                           unsigned       bytecode_size,
                           unsigned       num_regs,
                           unsigned       closure_size,
                           KOS_OBJ_ID    *constants,
                           unsigned       num_constants)
{
    KOS_OBJ_ID  ret    = KOS_BADPTR;
    int         error  = KOS_SUCCESS;
    KOS_MODULE *module = OBJPTR(MODULE, inst->modules.init_module);

    memset(((uint8_t *)module) + sizeof(KOS_OBJ_HEADER), 0, sizeof(*module) - sizeof(KOS_OBJ_HEADER));

    kos_set_object_type(module->header, OBJ_MODULE);

    module->inst          = inst;
    module->constants     = KOS_new_array(ctx, num_constants + 1);
    module->bytecode      = bytecode;
    module->bytecode_size = bytecode_size;
    module->main_idx      = num_constants;

    if (IS_BAD_PTR(module->constants))
        error = KOS_ERROR_EXCEPTION;

    if (num_constants && ! error) {

        unsigned i;

        for (i = 0; ! error && i < num_constants; ++i)
            error = KOS_array_write(ctx, module->constants, i, constants[i]);
    }

    if ( ! error) {
        KOS_FUNCTION_OPTS opts = create_func_opts((uint8_t)num_regs, 0);
        KOS_OBJ_ID        func_obj;

        opts.closure_size = (uint8_t)closure_size;

        func_obj = create_func_obj(ctx, CREATE_FUNC, 0, &opts);

        if (IS_BAD_PTR(func_obj))
            error = KOS_ERROR_EXCEPTION;
        else
            error = KOS_array_write(ctx, module->constants, num_constants, func_obj);
    }

    if ( ! error) {
        ret = kos_vm_run_module(ctx, OBJID(MODULE, module));

        if (IS_BAD_PTR(ret)) {
            assert(KOS_is_exception_pending(ctx));
            error = KOS_ERROR_EXCEPTION;
        }
    }

    return ret;
}

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    /* SET, GET.PROP8 */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    2, (uint8_t)(int8_t)-6,
            INSTR_SET,          0, 1, 2,
            INSTR_LOAD_INT8,    2, 0,
            INSTR_GET_PROP8,    3, 0, 0,
            INSTR_RETURN,       3
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 4, 0, &str_prop, 1) == TO_SMALL_INT(-6));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP8, GET */
    {
        static const char prop2[] = "prop2";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop2);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-7,
            INSTR_SET_PROP8,    0, 0/*"prop2"*/, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop2"*/
            INSTR_GET,          1, 0, 1,
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == TO_SMALL_INT(-7));
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
            INSTR_RETURN,     1
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, 0, 0) == TO_SMALL_INT(10));
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
            INSTR_RETURN,     2
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, 0, 0) == TO_SMALL_INT(-8));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET,          0, 0, 1,
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_BADPTR);
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
            INSTR_RETURN,    0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid index type for object */
    {
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT8,  1, 0,
            INSTR_SET,        0, 1, 1,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP8 - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET_PROP8,    0, 0/*"prop1"*/, 1,
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-6,
            INSTR_SET_ELEM,     0, IMM32(0), 1,
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - index out of range */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY,   0, IMM32(1),
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,     0, IMM32(1), 1,
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid index type for array */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_CONST,   0, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,     0, IMM32(0), 0,
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, &str_prop, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP8, HAS.SH.PROP8 */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop5);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-9,
            INSTR_SET_PROP8,    0, 0/*"prop5"*/, 1,
            INSTR_HAS_SH_PROP8, 2, 0, 0/*"prop5"*/,
            INSTR_RETURN,       2
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &str_prop, 1) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP8, HAS.DP.PROP8 */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop5);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-9,
            INSTR_SET_PROP8,    0, 0/*"prop5"*/, 1,
            INSTR_HAS_DP_PROP8, 2, 0, 0,/*"prop5"*/
            INSTR_RETURN,       2
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &str_prop, 1) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* PUSH */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop5);
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
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &str_prop, 1);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 4);
        TEST(KOS_array_read(ctx, ret, 0) == TO_SMALL_INT(10));

        val = KOS_array_read(ctx, ret, 1);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_ARRAY);
        TEST(KOS_get_array_size(val) == 0);

        TEST(KOS_array_read(ctx, ret, 2) == ret);
        TEST(KOS_array_read(ctx, ret, 3) == str_prop);
    }

    /************************************************************************/
    /* PUSH.EX */
    {
        static const char prop5[]  = "01";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop5);
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
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &str_prop, 1);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();

        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 4);
        TEST(KOS_array_read(ctx, ret, 0) == TO_SMALL_INT(10));
        TEST(KOS_array_read(ctx, ret, 1) == TO_SMALL_INT(10));

        val = KOS_array_read(ctx, ret, 2);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_STRING);
        TEST(KOS_get_string_length(val) == 1);
        TEST(KOS_string_get_char_code(ctx, val, 0) == 0x30);

        val = KOS_array_read(ctx, ret, 3);
        TEST( ! IS_BAD_PTR(val));
        TEST(GET_OBJ_TYPE(val) == OBJ_STRING);
        TEST(KOS_get_string_length(val) == 1);
        TEST(KOS_string_get_char_code(ctx, val, 0) == 0x31);
    }

    /************************************************************************/
    /* DEL.PROP8 */
    {
        static const char prop6[]  = "prop6";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop6);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-10,
            INSTR_SET_PROP8,    0, 0,/*"prop6"*/ 1,
            INSTR_DEL_PROP8,    0, 0,/*"prop6"*/
            INSTR_HAS_DP_PROP8, 1, 0, 0,/*"prop6"*/
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL.PROP8 - delete non-existent property */
    {
        static const char prop6[]  = "prop6";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop6);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_DEL_PROP8,    0, 0,/*"prop6"*/
            INSTR_HAS_DP_PROP8, 0, 0, 0,/*"prop6"*/
            INSTR_RETURN,       0
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_INT8,    1, (uint8_t)(int8_t)-10,
            INSTR_SET_PROP8,    0, 0/*"prop7"*/, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop7*/
            INSTR_DEL,          0, 1,
            INSTR_HAS_DP_PROP8, 1, 0, 0,/*"prop7"*/
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL - delete non-existent property */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_new_const_ascii_cstring(ctx, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,     0,
            INSTR_LOAD_CONST,   1, IMM32(0),/*"prop7*/
            INSTR_DEL,          0, 1,
            INSTR_HAS_DP_PROP8, 1, 0, 0,/*"prop7"*/
            INSTR_RETURN,       1
        };

        TEST(!IS_BAD_PTR(str_prop));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &str_prop, 1) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP */
    {
        const uint8_t code[] = {
            INSTR_LOAD_TRUE,  0,
            INSTR_JUMP,       IMM32(2),
            INSTR_LOAD_FALSE, 0,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP */
    {
        const uint8_t code[] = {
            INSTR_LOAD_INT8,  0, 1,
            INSTR_JUMP,       IMM32(9),
            INSTR_LOAD_INT8,  1, 2,
            INSTR_ADD,        0, 0, 1,
            INSTR_RETURN,     0,
            INSTR_LOAD_INT8,  1, 3,
            INSTR_ADD,        0, 0, 1,
            INSTR_JUMP,       IMM32(-21),
            INSTR_LOAD_VOID,  0,
            INSTR_RETURN,     0
        };

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0);
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
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP.NOT.COND */
    {
        const uint8_t code[] = {
            INSTR_LOAD_TRUE,     0,
            INSTR_JUMP_NOT_COND, IMM32(2), 0,
            INSTR_LOAD_FALSE,    0,
            INSTR_RETURN,        0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP.NOT.COND */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FALSE,    0,
            INSTR_JUMP_NOT_COND, IMM32(2), 0,
            INSTR_LOAD_TRUE,     0,
            INSTR_RETURN,        0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(2),

            INSTR_RETURN,      0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY8, 1, 1,
            INSTR_LOAD_INT8,   2, 42,
            INSTR_SET_ELEM,    1, IMM32(0), 2,
            INSTR_LOAD_VOID,   2,
            INSTR_CALL,        0, 0, 2, 1,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
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
            INSTR_RETURN,      0,

            INSTR_MUL,         0, 0, 0,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func = create_func(ctx, 35, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 10002);
    }

    /************************************************************************/
    /* LOAD.CONST (function), CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(2),

            INSTR_RETURN,     0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 121,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func;
        KOS_OBJ_ID        ret;

        opts.this_reg = 0;
        func = create_func(ctx, 5, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 121);
    }

    /************************************************************************/
    /* CALL.N */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(2),

            INSTR_RETURN,     0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 42,
            INSTR_LOAD_VOID,  2,
            INSTR_CALL_N,     0, 0, 2, 1, 1,
            INSTR_RETURN,     0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* CALL.N - zero args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(5),

            INSTR_LOAD_INT8,  0, 43,
            INSTR_RETURN,     0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_VOID,  1,
            INSTR_CALL_N,     0, 0, 1, 255, 0,
            INSTR_RETURN,     0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 43);
    }

    /************************************************************************/
    /* CALL.FUN */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(2),

            INSTR_RETURN,     0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_LOAD_INT8,  1, 42,
            INSTR_CALL_FUN,   0, 0, 1, 1,
            INSTR_RETURN,     0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* CALL.FUN - zero args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(5),

            INSTR_LOAD_INT8,  0, 44,
            INSTR_RETURN,     0,

            INSTR_LOAD_CONST, 0, IMM32(0),
            INSTR_CALL_FUN,   0, 0, 255, 0,
            INSTR_RETURN,     0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1);
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
            INSTR_RETURN,      0,

            INSTR_LOAD_INT8,   0, 43,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_func(ctx, 13, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == TO_SMALL_INT(43));
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
            INSTR_RETURN,      2,

            INSTR_LOAD_INT8,   0, 30,
            INSTR_ADD,         0, 0, 1,                  /* add 30 to this */
            INSTR_LOAD_INT8,   1, 100,
            INSTR_ADD,         0, 0, 1,                  /* add 100 to this */
            INSTR_RETURN,      0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(2, 0);
        opts.this_reg = 1;
        constants[0] = create_func(ctx, 23, &opts);

        opts = create_func_opts(2, 0);
        opts.this_reg = 0;
        constants[1] = create_func(ctx, 30, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, constants, 2) == TO_SMALL_INT(235));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not a function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,   0,
            INSTR_LOAD_ARRAY8, 1, 0,
            INSTR_CALL,        0, 0, 0, 1,
            INSTR_RETURN,      0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - args not an array */
    {
        static const char str[]    = "str";
        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID        constants[2];
        const uint8_t     code[] = {
            INSTR_JUMP,         IMM32(2),

            INSTR_RETURN,       0,

            INSTR_LOAD_CONST8,  0, 1,
            INSTR_LOAD_CONST,   1, IMM32(0),
            INSTR_LOAD_VOID,    2,
            INSTR_CALL,         0, 0, 2, 1,
            INSTR_RETURN,       0
        };

        constants[0] = KOS_new_const_ascii_cstring(ctx, str);

        opts = create_func_opts(2, 1);
        constants[1] = create_func(ctx, 5, &opts);

        TEST(!IS_BAD_PTR(constants[0]));
        TEST(!IS_BAD_PTR(constants[1]));

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, constants, 2) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not enough args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(2),

            INSTR_RETURN,      0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY8, 2, 0,
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 10);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.FUN - not enough args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,        IMM32(2),

            INSTR_RETURN,      0,

            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 255, 0,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func = create_func(ctx, 5, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL constructor - return this */
    {
        static const char str[]  = "own property";
        KOS_OBJ_ID        str_prop;
        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID        constants[4];
        KOS_OBJ_ID        ret;
        const uint8_t     code[] = {
            INSTR_JUMP,        IMM32(6),

            INSTR_SET_PROP8,   1, 0, 0,
            INSTR_RETURN,      1,

            INSTR_LOAD_FUN8,   0, 2,
            INSTR_LOAD_CONST,  1, IMM32(1),
            INSTR_CALL_FUN,    0, 0, 1, 1,
            INSTR_RETURN,      0
        };

        constants[0] = KOS_new_const_ascii_cstring(ctx, str);

        constants[1] = TO_SMALL_INT(0xC0DEU);

        opts = create_func_opts(2, 1);
        constants[2] = create_class(ctx, 5, &opts);

        constants[3] = KOS_new_object(ctx); /* prototype */

        str_prop     = KOS_new_const_ascii_cstring(ctx, str);

        TEST(!IS_BAD_PTR(constants[0]));
        TEST(!IS_BAD_PTR(constants[1]));
        TEST(!IS_BAD_PTR(constants[2]));
        TEST(!IS_BAD_PTR(constants[3]));
        TEST(!IS_BAD_PTR(str_prop));

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, constants, 4);
        TEST_NO_EXCEPTION();

        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_OBJECT);
        TEST(KOS_get_property(ctx, ret, str_prop) == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL constructor - return another object instead of this */
#ifndef CONFIG_DEEP_STACK
    {
        static const char str[]  = "own property";
        KOS_OBJ_ID        str_prop;
        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID        constants[4];
        KOS_OBJ_ID        ret;
        const uint8_t     code[] = {
            INSTR_JUMP,        IMM32(6),

            INSTR_SET_PROP8,   1, 0, 0,
            INSTR_RETURN,      0,

            INSTR_LOAD_FUN8,   0, 2,
            INSTR_LOAD_CONST,  1, IMM32(1),
            INSTR_CALL_FUN,    0, 0, 1, 1,
            INSTR_RETURN,      0
        };

        constants[0] = KOS_new_const_ascii_cstring(ctx, str);

        constants[1] = TO_SMALL_INT(0xC0DEU);

        opts = create_func_opts(2, 1);
        constants[2] = create_class(ctx, 5, &opts);

        constants[3] = KOS_new_object(ctx); /* prototype */

        str_prop     = KOS_new_const_ascii_cstring(ctx, str);

        TEST(!IS_BAD_PTR(constants[0]));
        TEST(!IS_BAD_PTR(constants[1]));
        TEST(!IS_BAD_PTR(constants[2]));
        TEST(!IS_BAD_PTR(constants[3]));
        TEST(!IS_BAD_PTR(str_prop));

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, constants, 4);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(ret == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }
#endif

    /************************************************************************/
    /* CALL constructor */
    {
        static const char str[]    = "own property";
        KOS_OBJ_ID        str_prop;
        KOS_OBJ_ID        ret;
        const uint8_t     code[]   = {
            INSTR_JUMP,        IMM32(8),

            INSTR_SET_PROP8,   1, 0, 0,
            INSTR_LOAD_VOID,   0,                /* return value is ignored */
            INSTR_RETURN,      1,

            INSTR_LOAD_FUN8,   0, 2,
            INSTR_LOAD_ARRAY8, 1, 1,             /* create arguments array */
            INSTR_LOAD_CONST,  2, IMM32(1),
            INSTR_SET_ELEM,    1, IMM32(0), 2,   /* set argument */
            INSTR_CALL,        0, 0, 1, 1,
            INSTR_RETURN,      0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[4];

        constants[0] = KOS_new_const_ascii_cstring(ctx, str);

        constants[1] = TO_SMALL_INT(0xC0DEU);

        opts = create_func_opts(2, 1);
        constants[2] = create_class(ctx, 5, &opts);

        constants[3] = KOS_new_object(ctx); /* prototype */

        str_prop     = KOS_new_const_ascii_cstring(ctx, str);

        TEST(!IS_BAD_PTR(constants[0]));
        TEST(!IS_BAD_PTR(constants[1]));
        TEST(!IS_BAD_PTR(constants[2]));
        TEST(!IS_BAD_PTR(constants[3]));
        TEST(!IS_BAD_PTR(str_prop));

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, constants, 4);
        TEST_NO_EXCEPTION();

        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_OBJECT);
        TEST(KOS_get_property(ctx, ret, str_prop) == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(2),

            INSTR_RETURN,     0,

            INSTR_LOAD_FUN,   0, IMM32(0),
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_CALL,       1, 0, 1, 1,

            INSTR_INSTANCEOF, 0, 1, 0,
            INSTR_RETURN,     0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(1, 0);
        constants[0] = create_class(ctx, 5, &opts);

        constants[1] = KOS_new_object(ctx); /* prototype */

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, constants, 2) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    /* The same function addresses - the same default prototypes */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(2),

            INSTR_RETURN,        0,

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

            INSTR_RETURN,        0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(1, 0);
        constants[0] = create_class(ctx, 5, &opts);

        constants[1] = KOS_new_object(ctx); /* prototype */

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 6, 0, constants, 2) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    /* Different function addresses - different default prototypes */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(4),

            INSTR_RETURN,        0,
            INSTR_RETURN,        0,

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

            INSTR_RETURN,        0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[4];

        opts = create_func_opts(1, 0);
        constants[0] = create_class(ctx, 5, &opts);

        constants[1] = KOS_new_object(ctx); /* prototype */

        opts = create_func_opts(1, 0);
        constants[2] = create_class(ctx, 7, &opts);

        constants[3] = KOS_new_object(ctx); /* prototype */

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 6, 0, constants, 4) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* GET.PROTO */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN8,  0, 0,
            INSTR_GET_PROTO,  0, 0,
            INSTR_RETURN,     0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(1, 0);
        constants[0] = create_class(ctx, 0, &opts);

        constants[1] = TO_SMALL_INT(42); /* prototype */

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, constants, 2) == TO_SMALL_INT(42));
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
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_gen(ctx, 3, &opts);

        KOS_OBJ_ID ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
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
            INSTR_RETURN,      0,

            INSTR_YIELD,       0,                 /* generator yields 'this' */
            INSTR_RETURN,      0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        constants[0] = TO_SMALL_INT(0xCAFEU);

        opts = create_func_opts(1, 0);
        opts.this_reg = 0;
        constants[1] = create_gen(ctx, 19, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, constants, 2) == TO_SMALL_INT(0xCAFEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), YIELD */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 100, 0,
            INSTR_CALL_FUN,    0, 0, 100, 0,
            INSTR_RETURN,      0,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_YIELD,       0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_gen(ctx, 15, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, &func, 1) == TO_SMALL_INT(42));
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
            INSTR_RETURN,      1,

            INSTR_YIELD,       0,
            INSTR_JUMP,        IMM32(-7)
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func;

        opts.this_reg = 0;
        func = create_gen(ctx, 24, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 4, 0, &func, 1) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.ITER, NEXT */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8,   0, 3,
            INSTR_LOAD_INT8,     1, 3,
            INSTR_SET_ELEM,      0, IMM32(0), 1,
            INSTR_LOAD_INT8,     1, 4,
            INSTR_SET_ELEM,      0, IMM32(1), 1,
            INSTR_LOAD_INT8,     1, 5,
            INSTR_SET_ELEM,      0, IMM32(2), 1,

            INSTR_LOAD_ITER,     0, 0,              /* convert to iterator   */

            INSTR_LOAD_VOID,     1,
            INSTR_NEXT,          1, 0,              /* yields 3 */
            INSTR_NEXT,          2, 0,              /* yields 4 */
            INSTR_ADD,           1, 1, 2,
            INSTR_NEXT,          2, 0,              /* yields 5 */
            INSTR_ADD,           1, 1, 2,

            INSTR_NEXT_JUMP,     2, 0, IMM32(2),    /* end of generator */
            INSTR_RETURN,        1,
            INSTR_THROW,         0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, 0, 0) == TO_SMALL_INT(3 + 4 + 5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.CONST (generator), YIELD, NEXT */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(19),

            INSTR_LOAD_INT8,     0, 3,
            INSTR_YIELD,         0,
            INSTR_LOAD_INT8,     0, 4,
            INSTR_YIELD,         0,
            INSTR_LOAD_INT8,     0, 5,
            INSTR_YIELD,         0,
            INSTR_LOAD_VOID,     0,
            INSTR_RETURN,        0,

            INSTR_LOAD_CONST8,   0, 0,
            INSTR_CALL_FUN,      0, 0, 255, 0,      /* instantiate generator */
            INSTR_LOAD_ITER,     0, 0,              /* convert to iterator   */

            INSTR_LOAD_VOID,     1,
            INSTR_NEXT,          1, 0,              /* yields 3 */
            INSTR_NEXT,          2, 0,              /* yields 4 */
            INSTR_ADD,           1, 1, 2,
            INSTR_NEXT,          2, 0,              /* yields 5 */
            INSTR_ADD,           1, 1, 2,

            INSTR_NEXT_JUMP,     2, 0, IMM32(2),    /* end of generator, skips the load */
            INSTR_RETURN,        1,
            INSTR_THROW,         2
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_gen(ctx, 5, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1) == TO_SMALL_INT(3 + 4 + 5));
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
            INSTR_RETURN,      1,

            INSTR_YIELD,       1
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(3, 2);
        KOS_OBJ_ID        func = create_gen(ctx, 16, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1) == KOS_BADPTR);
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
            INSTR_RETURN,      1,

            INSTR_YIELD,       1
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 0);
        KOS_OBJ_ID        func = create_gen(ctx, 15, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1) == KOS_BADPTR);
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
            INSTR_RETURN,      0,

            INSTR_YIELD,       1
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 1);
        KOS_OBJ_ID        func;

        opts.this_reg = 1;
        func = create_gen(ctx, 19, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == TO_SMALL_INT(120));
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
            INSTR_RETURN,      4,

            INSTR_LOAD_INT8,   1, 1,
            INSTR_ADD,         0, 0, 1,    /* use 'this' as the initial value */
            INSTR_YIELD,       0,
            INSTR_JUMP,        IMM32(-11)
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 0);
        KOS_OBJ_ID        func;
        KOS_OBJ_ID        ret;

        opts.this_reg = 0;
        func = create_gen(ctx, 66, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 5, 0, &func, 1);
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
            INSTR_RETURN,      1,

            INSTR_YIELD,       0,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func;

        opts.this_reg = 0;
        func = create_gen(ctx, 24, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* NEXT.JUMP - detect end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 0, 0, /* instantiate generator */
            INSTR_LOAD_ITER,   0, 0,       /* convert to iterator   */

            INSTR_NEXT_JUMP,   1, 0, IMM32(10), /* generator ends, does not jump */
            INSTR_NEXT_JUMP,   1, 0, IMM32(3),  /* generator ended already */
            INSTR_LOAD_INT8,   1, 42,
            INSTR_RETURN,      1,

            INSTR_RETURN,      0,
            INSTR_JUMP,        IMM32(-7)
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func;

        opts.this_reg = 0;
        func = create_gen(ctx, 30, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* NEXT - call beyond the end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_CALL_FUN,    0, 0, 0, 0, /* instantiate generator */
            INSTR_LOAD_ITER,   0, 0,       /* convert to iterator   */

            INSTR_NEXT,        1, 0,
            INSTR_RETURN,      1,

            INSTR_RETURN,      0,
            INSTR_JUMP,        IMM32(-7)
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func;

        opts.this_reg = 0;
        func = create_gen(ctx, 16, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* YIELD - yield not supported in a regular function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_INT8,   1, 13,
            INSTR_CALL_N,      0, 0, 1, 10, 0, /* invoke function */
            INSTR_RETURN,      0,

            INSTR_YIELD,       0,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_func(ctx, 14, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* NEXT - set output register to VOID on generator end */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY8, 0, 0,       /* load empty array */
            INSTR_LOAD_ITER,   0, 0,       /* convert to generator */

            INSTR_LOAD_TRUE,   1,
            INSTR_NEXT_JUMP,   1, 0, IMM32(2),
            INSTR_RETURN,      1,
            INSTR_LOAD_FALSE,  1
        };
        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* TAIL.CALL */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 0,
            INSTR_LOAD_ARRAY8, 1, 0,
            INSTR_TAIL_CALL,   0, 0, 1,

            /* unreachable */
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(1, 0);
        KOS_OBJ_ID        func = create_func(ctx, 14, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, &func, 1) == TO_SMALL_INT(42));
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
            INSTR_TAIL_CALL_N, 0, 1, 2, 2,

            /* unreachable */
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0,

            INSTR_ADD,         0, 0, 0,        /* arg 0 - 100 */
            INSTR_ADD,         0, 0, 1,        /* arg 1 - 20  */
            INSTR_ADD,         0, 0, 2,        /* this  - 3   */
            INSTR_RETURN,      0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(3, 2);
        KOS_OBJ_ID        func;

        opts.this_reg = 2;
        func = create_func(ctx, 21, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 4, 0, &func, 1) == TO_SMALL_INT(143));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* TAIL.CALL.FUN */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_LOAD_INT8,     1, 20,
            INSTR_LOAD_INT8,     2, 100,
            INSTR_TAIL_CALL_FUN, 0, 1, 2,

            /* unreachable */
            INSTR_LOAD_VOID,     0,
            INSTR_RETURN,        0,

            INSTR_ADD,           0, 0, 0,        /* arg 0 - 100 */
            INSTR_ADD,           0, 0, 1,        /* arg 1 - 20  */
            INSTR_RETURN,        0
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(3, 2);
        KOS_OBJ_ID        func = create_func(ctx, 17, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1) == TO_SMALL_INT(140));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - nothing is thrown */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(5),
            INSTR_LOAD_INT8,  0, 0,
            INSTR_RETURN,     0,
            INSTR_LOAD_INT8,  0, 1,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == TO_SMALL_INT(0));
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
            INSTR_RETURN,     0
        };

        KOS_OBJ_ID obj = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_property(ctx, obj, KOS_STR_VALUE) == TO_SMALL_INT(1));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - catch when invalid instruction operands cause exception */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(8),
            INSTR_LOAD_VOID,  0,
            INSTR_SET,        0, 0, 0, /* throws */
            INSTR_RETURN,     0,
            INSTR_LOAD_TRUE,  0,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 1, 0, 0, 0) == KOS_TRUE);
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
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0) == KOS_BADPTR);
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
            INSTR_RETURN,      1,

            INSTR_LOAD_INT8,   0, 42,
            INSTR_LOAD_FALSE,  1,
            INSTR_THROW,       0,
            INSTR_RETURN,      1
        };
        KOS_FUNCTION_OPTS opts = create_func_opts(2, 0);
        KOS_OBJ_ID        func = create_func(ctx, 21, &opts);

        KOS_OBJ_ID obj = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, &func, 1);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_property(ctx, obj, KOS_STR_VALUE) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - several levels of catch */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8, 0, 1,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(7),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP8,   0, 0, 0/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_RETURN,      0,

            INSTR_LOAD_CONST8, 0, 2,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(7),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP8,   0, 0, 0/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_THROW,       0,
            INSTR_RETURN,      1,

            INSTR_LOAD_CONST8, 0, 3,
            INSTR_LOAD_VOID,   1,
            INSTR_LOAD_ARRAY,  2, IMM32(0),
            INSTR_CATCH,       0, IMM32(7),
            INSTR_CALL,        0, 0, 1, 2,
            INSTR_RETURN,      1,
            INSTR_LOAD_INT8,   2, 1,
            INSTR_GET_PROP8,   0, 0, 0/* "value" */,
            INSTR_ADD,         0, 0, 2,
            INSTR_THROW,       0,
            INSTR_RETURN,      1,

            INSTR_LOAD_INT8,   0, 1,
            INSTR_THROW,       0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[4];

        constants[0] = KOS_STR_VALUE;

        opts = create_func_opts(3, 0);
        constants[1] = create_func(ctx,  37, &opts);

        opts = create_func_opts(3, 0);
        constants[2] = create_func(ctx,  76, &opts);

        opts = create_func_opts(2, 0);
        constants[3] = create_func(ctx, 115, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, constants, 4) == TO_SMALL_INT(4));
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
            INSTR_RETURN,      1,

            /* reg 1 is register array of the main function */
            INSTR_LOAD_CONST8, 2, 1,
            INSTR_BIND_SELF,   2, 0,                 /* bind own registers    */
            INSTR_BIND,        2, 1, 1,              /* bind main's registers */
            INSTR_LOAD_INT8,   0, 37,
            INSTR_RETURN,      2,                    /* leave one reg (reg 0) */

            /* reg 1 is register array of the above function */
            /* reg 2 is register array of the main function  */
            INSTR_GET_ELEM,    0, 1, IMM32(0),
            INSTR_LOAD_INT8,   1, 4,
            INSTR_ADD,         0, 0, 1,
            INSTR_SET_ELEM,    2, IMM32(1), 0,
            INSTR_LOAD_VOID,   0,
            INSTR_RETURN,      0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(3, 0);
        opts.closure_size = 1;
        opts.bind_reg     = 1;
        opts.num_binds    = 1;
        constants[0] = create_func(ctx, 22, &opts);

        opts = create_func_opts(3, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 2;
        constants[1] = create_func(ctx, 37, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 1, constants, 2) == TO_SMALL_INT(41));
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
            INSTR_RETURN,      4,

            /* reg 1 is register array of the main function        */
            /* reg 2 is array from register 3 in the main function */
            INSTR_GET_ELEM,    0, 2, IMM32(0),
            INSTR_GET_ELEM,    3, 1, IMM32(4),
            INSTR_ADD,         0, 0, 3,
            INSTR_SET_ELEM,    1, IMM32(4), 0,
            INSTR_LOAD_INT8,   0, 0,
            INSTR_YIELD,       0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        constants[0] = TO_SMALL_INT(-200);

        opts = create_func_opts(4, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 2;
        constants[1] = create_gen(ctx, 52, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 5, 1, constants, 2) == TO_SMALL_INT(-300));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND - cannot bind to void (non-function) */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,  0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_BIND,       0, 0, 1,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, 0, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* BIND.SELF - cannot bind to void (non-function) */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,  0,
            INSTR_BIND_SELF,  0, 0,
            INSTR_RETURN,     0
        };

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 2, 1, 0, 0) == KOS_BADPTR);
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
            INSTR_RETURN,      2,

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
            INSTR_RETURN,      3,

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
            INSTR_RETURN,      0
        };

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[6] = {
            TO_SMALL_INT(0x40000),
            TO_SMALL_INT(0x8000),
            TO_SMALL_INT(0x20000),
            TO_SMALL_INT(0x1000),
            TO_SMALL_INT(0),
            TO_SMALL_INT(0)
        };

        opts = create_func_opts(7, 2);
        opts.args_reg     = 1;
        opts.this_reg     = 3;
        opts.closure_size = 1;
        opts.bind_reg     = 4;
        opts.num_binds    = 2;
        constants[4] = create_func(ctx,  92, &opts);

        opts = create_func_opts(107, 3);
        opts.args_reg  = 98;
        opts.this_reg  = 101;
        opts.bind_reg  = 102;
        opts.num_binds = 5;
        constants[5] = create_func(ctx, 144, &opts);

        TEST(run_code(&inst, ctx, &code[0], sizeof(code), 6, 6, constants, 6) == TO_SMALL_INT(0x69055));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND.DEFAULTS - all default values */
    {
#define STACK_MOVE(idx) \
            INSTR_GET_ELEM,      2, 1, IMM32(idx), \
            INSTR_PUSH,          0, 2

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
            INSTR_TAIL_CALL_FUN, 1, 255, 0,

            INSTR_LOAD_FUN8,     3, 1,
            INSTR_BIND_SELF,     3, 0,
            INSTR_TAIL_CALL_FUN, 3, 255, 0,

            INSTR_LOAD_ARRAY8,   0, 0,
            STACK_MOVE(0),
            STACK_MOVE(1),
            STACK_MOVE(2),
            INSTR_RETURN,        0
        };
        KOS_OBJ_ID ret;

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(4, 0);
        opts.args_reg     = 0;
        opts.num_def_args = 3;
        opts.closure_size = 3;
        constants[0] = create_func(ctx, 43, &opts);

        opts = create_func_opts(3, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 1;
        constants[1] = create_func(ctx, 53, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 2, 0, constants, 2);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 3);
        TEST(KOS_array_read(ctx, ret, 0) == TO_SMALL_INT(10));
        TEST(KOS_array_read(ctx, ret, 1) == TO_SMALL_INT(11));
        TEST(KOS_array_read(ctx, ret, 2) == TO_SMALL_INT(12));
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
            INSTR_TAIL_CALL_FUN, 2, 0, 2,

            INSTR_LOAD_TRUE,     0,
            INSTR_LOAD_FUN8,     5, 1,
            INSTR_BIND_SELF,     5, 0,
            INSTR_TAIL_CALL_FUN, 5, 255, 0,

            INSTR_LOAD_ARRAY8,   0, 0,
            STACK_MOVE(0),
            STACK_MOVE(1),
            STACK_MOVE(2),
            STACK_MOVE(3),
            STACK_MOVE(4),
            INSTR_RETURN,        0
        };
        KOS_OBJ_ID ret;

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[2];

        opts = create_func_opts(6, 1);
        opts.args_reg     = 1;
        opts.num_def_args = 3;
        opts.closure_size = 5;
        constants[0] = create_func(ctx, 49, &opts);

        opts = create_func_opts(3, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 1;
        constants[1] = create_func(ctx, 61, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 3, 0, constants, 2);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == 5);
        TEST(KOS_array_read(ctx, ret, 0) == KOS_TRUE);
        TEST(KOS_array_read(ctx, ret, 1) == TO_SMALL_INT(5));
        TEST(KOS_array_read(ctx, ret, 2) == TO_SMALL_INT(6));
        TEST(KOS_array_read(ctx, ret, 3) == TO_SMALL_INT(21));
        TEST(KOS_array_read(ctx, ret, 4) == TO_SMALL_INT(22));
    }

    /************************************************************************/
    /* BIND.DEFAULTS - lots of default values and ellipsis, few input args */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_JUMP,          IMM32(30),

            /* 0 - begin
             * 1 - end */
            INSTR_LOAD_ARRAY8,   2, 0,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_JUMP,          IMM32(7),
            INSTR_PUSH,          2, 0,
            INSTR_ADD,           0, 0, 3,
            INSTR_CMP_LT,        4, 0, 1,
            INSTR_JUMP_COND,     IMM32(-17), 4,
            INSTR_RETURN,        2,

            INSTR_LOAD_INT8,     3, 64,
            INSTR_LOAD_INT8,     4, 96,
            INSTR_CALL_FUN,      1, 0, 3, 2,
            INSTR_LOAD_FUN8,     2, 1,
            INSTR_BIND_DEFAULTS, 2, 1,
            INSTR_LOAD_INT8,     3, 7,
            INSTR_LOAD_INT8,     4, 25,
            INSTR_CALL_FUN,      0, 0, 3, 2,
            INSTR_TAIL_CALL,     2, 1, 0,

            INSTR_LOAD_VOID,     0,
            INSTR_LOAD_VOID,     1,
            INSTR_LOAD_VOID,     2,
            INSTR_LOAD_VOID,     3,
            INSTR_LOAD_VOID,     4,
            INSTR_LOAD_FUN8,     KOS_MAX_ARGS_IN_REGS + 5 + 2, 2,
            INSTR_BIND_SELF,     KOS_MAX_ARGS_IN_REGS + 5 + 2, 0,
            INSTR_TAIL_CALL_FUN, KOS_MAX_ARGS_IN_REGS + 5 + 2, 255, 0,

            INSTR_LOAD_ARRAY8,   0, 0,
            STACK_MOVE( 0), STACK_MOVE( 1), STACK_MOVE( 2), STACK_MOVE( 3), STACK_MOVE( 4),
            STACK_MOVE( 5), STACK_MOVE( 6), STACK_MOVE( 7), STACK_MOVE( 8), STACK_MOVE( 9),
            STACK_MOVE(10), STACK_MOVE(11), STACK_MOVE(12), STACK_MOVE(13), STACK_MOVE(14),
            STACK_MOVE(15), STACK_MOVE(16), STACK_MOVE(17), STACK_MOVE(18), STACK_MOVE(19),
            STACK_MOVE(20), STACK_MOVE(21), STACK_MOVE(22), STACK_MOVE(23), STACK_MOVE(24),
            STACK_MOVE(25), STACK_MOVE(26), STACK_MOVE(27), STACK_MOVE(28), STACK_MOVE(29),
            STACK_MOVE(30), STACK_MOVE(31), STACK_MOVE(32), STACK_MOVE(33), STACK_MOVE(34),
            STACK_MOVE(35), STACK_MOVE(36), STACK_MOVE(37), STACK_MOVE(38),
            INSTR_RETURN,        0
        };

        int i;

        KOS_OBJ_ID obj;
        KOS_OBJ_ID ret;

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[3];

        opts = create_func_opts(5, 2);
        constants[0] = create_func(ctx,  8, &opts);

        opts = create_func_opts(KOS_MAX_ARGS_IN_REGS + 5 + 3, 16);
        opts.args_reg     = 5;
        opts.num_def_args = KOS_MAX_ARGS_IN_REGS;
        opts.rest_reg     = KOS_MAX_ARGS_IN_REGS + 4;
        opts.ellipsis_reg = KOS_MAX_ARGS_IN_REGS + 5;
        opts.this_reg     = KOS_MAX_ARGS_IN_REGS + 5 + 1;
        opts.closure_size = KOS_MAX_ARGS_IN_REGS + 5 + 2;
        constants[1] = create_func(ctx, 70, &opts);

        opts = create_func_opts(3, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 1;
        constants[2] = create_func(ctx, 90, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 5, 0, constants, 3);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == KOS_MAX_ARGS_IN_REGS + 5 + 2);
        for (i = 0; i < 5; i++)
            TEST(KOS_array_read(ctx, ret, i) == KOS_VOID);
        for (i = 5; i < 23; i++)
            TEST(KOS_array_read(ctx, ret, i) == TO_SMALL_INT(i + 2));
        for (i = 23; i < (int)KOS_MAX_ARGS_IN_REGS + 5 - 1; i++)
            TEST(KOS_array_read(ctx, ret, i) == TO_SMALL_INT(i - 23 + 66));
        /* Rest of args */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS + 5 - 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 48 - KOS_MAX_ARGS_IN_REGS + 1);
        for (i = 0; i < 48 - (int)KOS_MAX_ARGS_IN_REGS + 1; i++)
            TEST(KOS_array_read(ctx, obj, i) == TO_SMALL_INT(i + (int)KOS_MAX_ARGS_IN_REGS - 1 - 16 + 64));
        /* Ellipsis */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS + 5);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 0);
        /* this */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS + 5 + 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 32);
        for (i = 0; i < 32; i++)
            TEST(KOS_array_read(ctx, obj, i) == TO_SMALL_INT(i + 64));
    }

    /************************************************************************/
    /* BIND.DEFAULTS - lots of args, a few default values and ellipsis */
    {
        const uint8_t code[] = {
            INSTR_LOAD_CONST8,   0, 0,
            INSTR_JUMP,          IMM32(30),

            /* 0 - begin
             * 1 - end */
            INSTR_LOAD_ARRAY8,   2, 0,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_JUMP,          IMM32(7),
            INSTR_PUSH,          2, 0,
            INSTR_ADD,           0, 0, 3,
            INSTR_CMP_LT,        4, 0, 1,
            INSTR_JUMP_COND,     IMM32(-17), 4,
            INSTR_RETURN,        2,

            INSTR_LOAD_INT8,     3, 100,
            INSTR_LOAD_INT8,     4, 105,
            INSTR_CALL_FUN,      1, 0, 3, 2,
            INSTR_LOAD_FUN8,     2, 1,
            INSTR_BIND_DEFAULTS, 2, 1,
            INSTR_LOAD_INT8,     3, 1,
            INSTR_LOAD_INT8,     4, KOS_MAX_ARGS_IN_REGS + 10,
            INSTR_CALL_FUN,      0, 0, 3, 2,
            INSTR_TAIL_CALL,     2, 1, 0,

            INSTR_LOAD_FUN8,     KOS_MAX_ARGS_IN_REGS + 2, 2,
            INSTR_BIND_SELF,     KOS_MAX_ARGS_IN_REGS + 2, 0,
            INSTR_TAIL_CALL_FUN, KOS_MAX_ARGS_IN_REGS + 2, 255, 0,

            INSTR_LOAD_ARRAY8,   0, 0,
            STACK_MOVE( 0), STACK_MOVE( 1), STACK_MOVE( 2), STACK_MOVE( 3), STACK_MOVE( 4),
            STACK_MOVE( 5), STACK_MOVE( 6), STACK_MOVE( 7), STACK_MOVE( 8), STACK_MOVE( 9),
            STACK_MOVE(10), STACK_MOVE(11), STACK_MOVE(12), STACK_MOVE(13), STACK_MOVE(14),
            STACK_MOVE(15), STACK_MOVE(16), STACK_MOVE(17), STACK_MOVE(18), STACK_MOVE(19),
            STACK_MOVE(20), STACK_MOVE(21), STACK_MOVE(22), STACK_MOVE(23), STACK_MOVE(24),
            STACK_MOVE(25), STACK_MOVE(26), STACK_MOVE(27), STACK_MOVE(28), STACK_MOVE(29),
            STACK_MOVE(30), STACK_MOVE(31), STACK_MOVE(32), STACK_MOVE(33),
            INSTR_RETURN,        0
        };

        int i;

        KOS_OBJ_ID obj;
        KOS_OBJ_ID ret;

        KOS_FUNCTION_OPTS opts;
        KOS_OBJ_ID constants[3];

        opts = create_func_opts(5, 2);
        constants[0] = create_func(ctx,  8, &opts);

        opts = create_func_opts(KOS_MAX_ARGS_IN_REGS + 3, KOS_MAX_ARGS_IN_REGS);
        opts.num_def_args = 5;
        opts.rest_reg     = KOS_MAX_ARGS_IN_REGS - 1;
        opts.ellipsis_reg = KOS_MAX_ARGS_IN_REGS;
        opts.this_reg     = KOS_MAX_ARGS_IN_REGS + 1;
        opts.closure_size = KOS_MAX_ARGS_IN_REGS + 2;
        constants[1] = create_func(ctx, 70, &opts);

        opts = create_func_opts(3, 0);
        opts.bind_reg  = 1;
        opts.num_binds = 1;
        constants[2] = create_func(ctx, 80, &opts);

        ret = run_code(&inst, ctx, &code[0], sizeof(code), 5, 0, constants, 3);
        TEST( ! IS_BAD_PTR(ret));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(ret) == OBJ_ARRAY);
        TEST(KOS_get_array_size(ret) == KOS_MAX_ARGS_IN_REGS + 2);
        for (i = 0; i < (int)KOS_MAX_ARGS_IN_REGS - 1; i++)
            TEST(KOS_array_read(ctx, ret, i) == TO_SMALL_INT(i + 1));
        /* Rest of args */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS - 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 6);
        for (i = 0; i < 6; i++)
            TEST(KOS_array_read(ctx, obj, i) == TO_SMALL_INT(i + (int)KOS_MAX_ARGS_IN_REGS));
        /* Ellipsis */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 4);
        for (i = 0; i < 4; i++)
            TEST(KOS_array_read(ctx, obj, i) == TO_SMALL_INT(i + (int)KOS_MAX_ARGS_IN_REGS + 6));
        /* this */
        obj = KOS_array_read(ctx, ret, KOS_MAX_ARGS_IN_REGS + 1);
        TEST( ! IS_BAD_PTR(obj));
        TEST_NO_EXCEPTION();
        TEST(GET_OBJ_TYPE(obj) == OBJ_ARRAY);
        TEST(KOS_get_array_size(obj) == 5);
        for (i = 0; i < 5; i++)
            TEST(KOS_array_read(ctx, obj, i) == TO_SMALL_INT(i + 100));
    }

    KOS_instance_destroy(&inst);

    return 0;
}

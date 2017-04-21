/*
 * Copyright (c) 2014-2017 Chris Dragan
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

static KOS_OBJ_ID _run_code(KOS_CONTEXT     *ctx,
                            KOS_STACK_FRAME *frame,
                            const uint8_t   *bytecode,
                            unsigned         bytecode_size,
                            unsigned         num_regs,
                            KOS_OBJ_ID       string)
{
    KOS_OBJ_ID ret;
    int        error = KOS_SUCCESS;

    struct _KOS_MODULE module;

    memset(&module, 0, sizeof(module));

    module.type          = OBJ_MODULE;
    module.context       = ctx;
    module.strings       = IS_BAD_PTR(string) ? KOS_BADPTR : KOS_new_array(frame, 1);
    module.bytecode      = bytecode;
    module.bytecode_size = bytecode_size;
    module.instr_offs    = 0;
    module.num_regs      = num_regs;

    if ( ! IS_BAD_PTR(string)) {

        if (IS_BAD_PTR(module.strings))
            error = KOS_ERROR_EXCEPTION;

        if ( ! error)
            error = KOS_array_write(frame, module.strings, 0, string);
    }

    if ( ! error)
        error = _KOS_vm_run_module(&module, &ret);

    if (error) {
        KOS_raise_exception(frame, ret);
        ret = KOS_BADPTR;
    }

    return ret;
}

int main(void)
{
    KOS_CONTEXT      ctx;
    KOS_STACK_FRAME *frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /************************************************************************/
    /* SET, GET.PROP */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_STR,   1, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT32, 2, IMM32(-6),
            INSTR_SET,        0, 1, 2,
            INSTR_LOAD_INT32, 2, IMM32(0),
            INSTR_GET_PROP,   3, 0, IMM32(0),
            INSTR_RETURN,     0, 3
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, str_prop) == TO_SMALL_INT(-6));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP, GET */
    {
        static const char prop2[] = "prop2";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop2);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT32, 1, IMM32(-7),
            INSTR_SET_PROP,   0, IMM32(0)/*"prop2"*/, 1,
            INSTR_LOAD_STR,   1, IMM32(0),/*"prop2"*/
            INSTR_GET,        1, 0, 1,
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == TO_SMALL_INT(-7));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET, GET.ELEM */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY, 0, IMM32(5),
            INSTR_LOAD_INT32, 1, IMM32(3),
            INSTR_LOAD_INT32, 2, IMM32(10),
            INSTR_SET,        0, 1, 2,
            INSTR_GET_ELEM,   1, 0, IMM32(-2),
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == TO_SMALL_INT(10));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM, GET */
    {
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY, 0, IMM32(3),
            INSTR_LOAD_INT32, 1, IMM32(-8),
            INSTR_SET_ELEM,   0, IMM32(2), 1,
            INSTR_LOAD_INT32, 1, IMM32(-1),
            INSTR_GET,        2, 0, 1,
            INSTR_RETURN,     0, 2
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == TO_SMALL_INT(-8));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_STR,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT32, 1, IMM32(-6),
            INSTR_SET,        0, 0, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid index type for object */
    {
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,  0,
            INSTR_LOAD_TRUE, 1,
            INSTR_LOAD_INT32,2, IMM32(-6),
            INSTR_SET,       0, 1, 2,
            INSTR_RETURN,    0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET - invalid index type for object */
    {
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT32, 1, IMM32(0),
            INSTR_SET,        0, 1, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_STR,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT32, 1, IMM32(-6),
            INSTR_SET_PROP,   0, IMM32(0)/*"prop1"*/, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid object type */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_STR,   0, IMM32(0),/*"prop1"*/
            INSTR_LOAD_INT32, 1, IMM32(-6),
            INSTR_SET_ELEM,   0, IMM32(0), 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - index out of range */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_ARRAY, 0, IMM32(1),
            INSTR_LOAD_STR,   1, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,   0, IMM32(1), 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.ELEM - invalid index type for array */
    {
        static const char prop1[]  = "prop1";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop1);
        const uint8_t code[] = {
            INSTR_LOAD_STR,   0, IMM32(0),/*"prop1"*/
            INSTR_SET_ELEM,   0, IMM32(0), 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* SET.PROP, HAS.PROP */
    {
        static const char prop5[]  = "prop5";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop5);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT32, 1, IMM32(-9),
            INSTR_SET_PROP,   0, IMM32(0)/*"prop5"*/, 1,
            INSTR_HAS_PROP,   2, 0, IMM32(0),/*"prop5"*/
            INSTR_RETURN,     0, 2
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, str_prop) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL.PROP */
    {
        static const char prop6[]  = "prop6";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop6);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT32, 1, IMM32(-10),
            INSTR_SET_PROP,   0, IMM32(0),/*"prop6"*/ 1,
            INSTR_DEL_PROP,   0, IMM32(0),/*"prop6"*/
            INSTR_HAS_PROP,   1, 0, IMM32(0),/*"prop6"*/
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_FALSE);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, str_prop) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ,   0,
            INSTR_LOAD_INT32, 1, IMM32(-10),
            INSTR_SET_PROP,   0, IMM32(0)/*"prop7"*/, 1,
            INSTR_LOAD_STR,   1, IMM32(0),/*"prop7*/
            INSTR_DEL,        0, 1,
            INSTR_HAS_PROP,   1, 0, IMM32(0),/*"prop7"*/
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* DEL - delete non-existent property */
    {
        static const char prop7[]  = "prop7";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, prop7);
        const uint8_t code[] = {
            INSTR_LOAD_OBJ, 0,
            INSTR_LOAD_STR, 1, IMM32(0),/*"prop7*/
            INSTR_DEL,      0, 1,
            INSTR_HAS_PROP, 1, 0, IMM32(0),/*"prop7"*/
            INSTR_RETURN,   0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, str_prop) == KOS_FALSE);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* JUMP */
    {
        const uint8_t code[] = {
            INSTR_LOAD_INT32, 0, IMM32(1),
            INSTR_JUMP,       IMM32(13),
            INSTR_LOAD_INT32, 1, IMM32(2),
            INSTR_ADD,        0, 0, 1,
            INSTR_RETURN,     0, 0,
            INSTR_LOAD_INT32, 1, IMM32(3),
            INSTR_ADD,        0, 0, 1,
            INSTR_JUMP,       IMM32(-28),
            INSTR_LOAD_VOID,  0,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 2, 0);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == KOS_TRUE);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == KOS_FALSE);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.FUN, CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(10),

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(-19), 1, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(1),
            INSTR_LOAD_INT32, 2, IMM32(42),
            INSTR_SET_ELEM,   1, IMM32(0), 2,
            INSTR_LOAD_VOID,  2,
            INSTR_CALL,       0, 0, 2, 1,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, 0);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 42);
    }

    /************************************************************************/
    /* LOAD.FUN, CALL */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(39), 1, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(1),
            INSTR_LOAD_INT32, 2, IMM32(100),
            INSTR_SET_ELEM,   1, IMM32(0), 2,
            INSTR_LOAD_VOID,  2,
            INSTR_CALL,       0, 0, 2, 1,
            INSTR_LOAD_INT32, 1, IMM32(2),
            INSTR_ADD,        0, 0, 1,
            INSTR_RETURN,     0, 0,

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_MUL,        0, 0, 0,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, 0);
        TEST_NO_EXCEPTION();

        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 10002);
    }

    /************************************************************************/
    /* LOAD.FUN, CALL */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(3),

            INSTR_RETURN,     0, 1,

            INSTR_LOAD_FUN,   0, IMM32(-12), 0, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.FUN, CALL - reuse function body twice */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(34), 0, 3, 0,    /* this function adds 110 */
            INSTR_LOAD_FUN,   1, IMM32(35), 0, 3, 0,    /* this function adds 100 */
            INSTR_LOAD_INT32, 2, IMM32(1),
            INSTR_LOAD_ARRAY, 3, IMM32(0),
            INSTR_CALL,       2, 0, 2, 3,               /* effectively add 110 */
            INSTR_CALL,       2, 1, 2, 3,               /* effectively add 100 */
            INSTR_RETURN,     0, 2,

            INSTR_LOAD_INT32, 2, IMM32(10),
            INSTR_ADD,        1, 1, 2,                  /* add 10 to this */
            INSTR_LOAD_INT32, 2, IMM32(100),
            INSTR_ADD,        1, 1, 2,                  /* add 100 to this */
            INSTR_RETURN,     0, 1,
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, 0) == TO_SMALL_INT(211));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not a function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_VOID,  0,
            INSTR_LOAD_ARRAY, 1, IMM32(1),
            INSTR_LOAD_INT32, 2, IMM32(100),
            INSTR_SET_ELEM,   1, IMM32(0), 2,
            INSTR_LOAD_VOID,  2,
            INSTR_CALL,       0, 0, 2, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - args not an array */
    {
        static const char str[]    = "str";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, str);
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(10),

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(-19), 1, 2, 0,
            INSTR_LOAD_STR,   1, IMM32(0),
            INSTR_LOAD_VOID,  2,
            INSTR_CALL,       0, 0, 2, 1,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, str_prop) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL - not enough args */
    {
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(10),

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(-19), 10, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* NEW */
    {
        static const char str[]    = "own property";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, str);
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(17),

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_SET_PROP,   1, IMM32(0), 0,
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(-26), 1, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(1),      /* create arguments array */
            INSTR_LOAD_INT32, 2, IMM32(0xC0DEU),
            INSTR_SET_ELEM,   1, IMM32(0), 2,   /* set argument */
            INSTR_NEW,        0, 0, 1,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, str_prop);
        TEST_NO_EXCEPTION();

        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_OBJECT);
        TEST(KOS_get_property(frame, ret, KOS_context_get_cstring(frame, str)) == TO_SMALL_INT(0xC0DEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* NEW */
    {
        static const char str[]    = "own property";
        KOS_OBJ_ID        str_prop = KOS_context_get_cstring(frame, str);
        const uint8_t code[] = {
            INSTR_JUMP,       IMM32(23),

            INSTR_GET_ELEM,   0, 0, IMM32(0),
            INSTR_SET_PROP,   1, IMM32(0), 0,
            INSTR_LOAD_INT32, 0, IMM32(0),      /* return value is ignored */
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(-32), 1, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(1),      /* create arguments array */
            INSTR_LOAD_INT32, 2, IMM32(0xC0DEU),
            INSTR_SET_ELEM,   1, IMM32(0), 2,   /* set argument */
            INSTR_NEW,        0, 0, 1,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, str_prop);
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

            INSTR_LOAD_FUN,   0, IMM32(-12), 0, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_NEW,        1, 0, 1,

            INSTR_INSTANCEOF, 0, 1, 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* INSTANCEOF */
    /* The same function addresses - the same default prototypes */
    {
        const uint8_t code[] = {
            INSTR_JUMP,          IMM32(3),

            INSTR_RETURN,        0, 0,

            INSTR_LOAD_ARRAY,    1, IMM32(0),
            INSTR_LOAD_FUN,      2, IMM32(-18), 0, 2, 0,
            INSTR_LOAD_FUN,      3, IMM32(-27), 0, 2, 0,
            INSTR_NEW,           4, 2, 1,
            INSTR_NEW,           5, 3, 1,

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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, 0) == KOS_TRUE);
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

            INSTR_LOAD_ARRAY,    1, IMM32(0),
            INSTR_LOAD_FUN,      2, IMM32(-21), 0, 2, 0,
            INSTR_LOAD_FUN,      3, IMM32(-27), 0, 2, 0,
            INSTR_NEW,           4, 2, 1,
            INSTR_NEW,           5, 3, 1,

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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, CALL - instantiate generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(0), 0, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 3, 0);
        TEST_NO_EXCEPTION();

        TEST(!IS_BAD_PTR(ret));
        TEST(!IS_SMALL_INT(ret));
        TEST(GET_OBJ_TYPE(ret) == OBJ_FUNCTION);
        TEST(OBJPTR(FUNCTION, ret)->generator_state == KOS_GEN_READY);
        TEST(OBJPTR(FUNCTION, ret)->generator_stack_frame != 0);
    }

    /************************************************************************/
    /* LOAD.GEN, CALL */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(25), 0, 2, 0,
            INSTR_LOAD_INT32, 1, IMM32(0xCAFEU), /* generator yields 'this' */
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,        /* instantiate generator   */
            INSTR_CALL,       0, 0, 1, 2,        /* invoke generator        */
            INSTR_RETURN,     0, 0,

            INSTR_YIELD,      1,                 /* generator yields 'this' */
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == TO_SMALL_INT(0xCAFEU));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, YIELD */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(19), 0, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_CALL,       0, 0, 1, 1,
            INSTR_CALL,       0, 0, 1, 1,
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_INT32, 0, IMM32(42),
            INSTR_YIELD,      0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, CALL - ensure that YIELD resets the register to 'void' */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(30), 0, 2, 0,
            INSTR_LOAD_INT32, 1, IMM32(0),/* generator will yield 'this' first */
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2, /* instantiate generator */

            INSTR_CALL,       3, 0, 1, 2, /* yields 0 ('this') */
            INSTR_CALL,       3, 0, 1, 2, /* yields 'void', because args are empty */
            INSTR_RETURN,     0, 3,

            INSTR_YIELD,      1,
            INSTR_JUMP,       IMM32(-7)
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, YIELD, CALL.GEN */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,      0, IMM32(80), 2, 3, 0,
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

            INSTR_GET_ELEM,      1, 0, IMM32(1),    /* arg 1 - end   */
            INSTR_GET_ELEM,      0, 0, IMM32(0),    /* arg 0 - begin */
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 5, 0) == TO_SMALL_INT(3+4+5));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, CALL - not enough args */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(20), 2, 2, 0,
            INSTR_LOAD_INT32, 1, IMM32(0),
            INSTR_LOAD_ARRAY, 2, IMM32(1),
            INSTR_CALL,       0, 0, 1, 2, /* instantiate generator */
            INSTR_RETURN,     0, 1,

            INSTR_YIELD,      1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, CALL - args not an array */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(16), 0, 2, 0,
            INSTR_LOAD_INT32, 1, IMM32(0),
            INSTR_LOAD_VOID,  2,
            INSTR_CALL,       0, 0, 1, 2, /* instantiate generator */
            INSTR_RETURN,     0, 1,

            INSTR_YIELD,      1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* LOAD.GEN, CALL - one arg passed to generator in "READY" state */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(21), 1, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(1),
            INSTR_CALL,       0, 0, 1, 2, /* instantiate generator */
            INSTR_CALL,       0, 0, 1, 2, /* invoke generator */
            INSTR_RETURN,     0, 0,

            INSTR_YIELD,      1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == KOS_VOID);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* YIELD - pass data to generator through yield */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(79), 0, 2, 0,
            INSTR_LOAD_INT32, 1, IMM32(100),    /* bind initial value, 'this', used by the generator */
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,       /* instantiate generator */

            INSTR_CALL,       3, 0, 1, 2,       /* yields 101 */
            INSTR_LOAD_ARRAY, 2, IMM32(1),
            INSTR_ADD,        4, 3, 3,          /* 202 */
            INSTR_LOAD_INT32, 3, IMM32(64),
            INSTR_SET_ELEM,   2, IMM32(0), 3,
            INSTR_CALL,       3, 0, 1, 2,       /* yields 65 */
            INSTR_ADD,        4, 4, 3,          /* 267 */
            INSTR_LOAD_INT32, 3, IMM32(16),
            INSTR_SET_ELEM,   2, IMM32(0), 3,
            INSTR_CALL,       3, 0, 1, 2,       /* yields 17 */
            INSTR_SUB,        4, 4, 3,          /* 250 */
            INSTR_RETURN,     0, 4,

            INSTR_LOAD_INT32, 0, IMM32(1),
            INSTR_ADD,        1, 1, 0,    /* use 'this' as the initial value */
            INSTR_YIELD,      1,
            INSTR_JUMP,       IMM32(-11)
        };

        KOS_OBJ_ID ret = _run_code(&ctx, frame, &code[0], sizeof(code), 5, 0);
        TEST_NO_EXCEPTION();

        TEST(!IS_BAD_PTR(ret));
        TEST(IS_SMALL_INT(ret));
        TEST(GET_SMALL_INT(ret) == 250);
    }

    /************************************************************************/
    /* CALL - call beyond the end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(26), 0, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2, /* instantiate generator */

            INSTR_CALL,       3, 0, 1, 2,
            INSTR_CALL,       3, 0, 1, 2,
            INSTR_RETURN,     0, 3,

            INSTR_YIELD,      1,
            INSTR_RETURN,     0, 1
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - call beyond the end of generator */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(19), 0, 2, 0,
            INSTR_LOAD_ARRAY8,1, 0,
            INSTR_CALL,       0, 0, 1, 1, /* instantiate generator */

            INSTR_CALL_GEN,   3, 0, 2,    /* returns 'true' in register 2 */
            INSTR_CALL_GEN,   3, 0, 3,    /* raise exception */
            INSTR_RETURN,     0, 2,

            INSTR_RETURN,     0, 1,
            INSTR_JUMP,       IMM32(-8)
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 4, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* YIELD - yield not supported in a regular function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(14), 0, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_CALL,       0, 0, 1, 1, /* invoke function */
            INSTR_RETURN,     0, 0,

            INSTR_YIELD,      0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - put both return value and status in the same register */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(15), 0, 2, 0,
            INSTR_LOAD_ARRAY8,1, 0,
            INSTR_CALL,       0, 0, 1, 1, /* instantiate generator */

            INSTR_CALL_GEN,   0, 0, 0,    /* invoke generator */
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_INT8,  0, 0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_TRUE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CALL.GEN - put both return value and status in the same register */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(18), 0, 2, 0,
            INSTR_LOAD_ARRAY, 1, IMM32(0),
            INSTR_CALL,       0, 0, 1, 1, /* instantiate generator */

            INSTR_CALL_GEN,   0, 0, 0,    /* invoke generator */
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_INT8,  0, 0,
            INSTR_YIELD,      0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_FALSE);
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - nothing is thrown */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(6),
            INSTR_LOAD_INT32, 0, IMM32(0),
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == TO_SMALL_INT(0));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - throw a number */
    {
        const uint8_t code[] = {
            INSTR_CATCH,      0, IMM32(14),
            INSTR_LOAD_INT32, 0, IMM32(0),
            INSTR_LOAD_INT32, 1, IMM32(1),
            INSTR_THROW,      1,
            INSTR_RETURN,     0, 0
        };

        KOS_OBJ_ID obj = _run_code(&ctx, frame, &code[0], sizeof(code), 2, 0);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 1, 0) == KOS_TRUE);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - catch exception from another function */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(22), 0, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CATCH,      1, IMM32(5),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 1,

            INSTR_LOAD_INT32, 0, IMM32(42),
            INSTR_LOAD_FALSE, 1,
            INSTR_THROW,      0,
            INSTR_RETURN,     0, 1
        };

        KOS_OBJ_ID obj = _run_code(&ctx, frame, &code[0], sizeof(code), 3, 0);
        TEST_NO_EXCEPTION();

        TEST(KOS_get_property(frame, obj, KOS_context_get_cstring(frame, str_value)) == TO_SMALL_INT(42));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* CATCH - several levels of catch */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(42), 0, 3, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CATCH,      0, IMM32(8),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 1,
            INSTR_LOAD_INT32, 2, IMM32(1),
            INSTR_GET_PROP,   0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,        0, 0, 2,
            INSTR_RETURN,     0, 0,

            INSTR_LOAD_FUN,   0, IMM32(44), 0, 3, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CATCH,      0, IMM32(8),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 1,
            INSTR_LOAD_INT32, 2, IMM32(1),
            INSTR_GET_PROP,   0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,        0, 0, 2,
            INSTR_THROW,      0,
            INSTR_RETURN,     0, 1,

            INSTR_LOAD_FUN,   0, IMM32(44), 0, 2, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CATCH,      0, IMM32(8),
            INSTR_CALL,       0, 0, 1, 2,
            INSTR_RETURN,     0, 1,
            INSTR_LOAD_INT32, 2, IMM32(1),
            INSTR_GET_PROP,   0, 0, IMM32(0)/* "value" */,
            INSTR_ADD,        0, 0, 2,
            INSTR_THROW,      0,
            INSTR_RETURN,     0, 1,

            INSTR_LOAD_INT32, 0, IMM32(1),
            INSTR_THROW,      0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, KOS_context_get_cstring(frame, str_value)) == TO_SMALL_INT(4));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF */
    {
        const uint8_t code[] = {
            INSTR_LOAD_FUN,   0, IMM32(24), 0, 3, 0,
            INSTR_BIND_SELF,  0, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_CALL,       0, 0, 1, 2,           /* returns the last function */
            INSTR_CALL,       0, 0, 1, 2,           /* sets register 1 to 41     */
            INSTR_RETURN,     0, 1,

            /* reg 2 is register array of the main function */
            INSTR_LOAD_FUN,   1, IMM32(16), 0, 4, 0,
            INSTR_BIND_SELF,  1, 0,                 /* bind own registers    */
            INSTR_BIND,       1, 1, 2,              /* bind main's registers */
            INSTR_LOAD_INT32, 0, IMM32(37),
            INSTR_RETURN,     1, 1,                 /* leave one reg (reg 0) */

            /* reg 2 is register array of the above function */
            /* reg 3 is register array of the main function  */
            INSTR_GET_ELEM,   0, 2, IMM32(0),
            INSTR_LOAD_INT32, 1, IMM32(4),
            INSTR_ADD,        0, 0, 1,
            INSTR_SET_ELEM,   3, IMM32(1), 0,
            INSTR_LOAD_VOID,  0,
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 3, 0) == TO_SMALL_INT(41));
        TEST_NO_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF */
    {
        const uint8_t code[] = {
            INSTR_LOAD_GEN,   0, IMM32(53), 0, 4, 0,
            INSTR_LOAD_VOID,  1,
            INSTR_LOAD_ARRAY, 2, IMM32(0),
            INSTR_LOAD_ARRAY, 3, IMM32(1),
            INSTR_BIND_SELF,  0, 0,
            INSTR_BIND,       0, 1, 3,
            INSTR_CALL,       0, 0, 1, 2,           /* instantiate generator */

            INSTR_LOAD_INT32, 4, IMM32(-100),
            INSTR_SET_ELEM,   3, IMM32(0), 4,
            INSTR_LOAD_INT32, 4, IMM32(-200),

            INSTR_CALL,       0, 0, 1, 2,           /* add 3[0] to 4 */
            INSTR_RETURN,     0, 4,

            /* reg 2 is register array of the main function        */
            /* reg 3 is array from register 3 in the main function */
            INSTR_GET_ELEM,   0, 3, IMM32(0),
            INSTR_GET_ELEM,   1, 2, IMM32(4),
            INSTR_ADD,        0, 0, 1,
            INSTR_SET_ELEM,   2, IMM32(4), 0,
            INSTR_LOAD_INT32, 0, IMM32(0),
            INSTR_YIELD,      0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 5, 0) == TO_SMALL_INT(-300));
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_BADPTR);
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

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 2, 0) == KOS_BADPTR);
        TEST_EXCEPTION();
    }

    /************************************************************************/
    /* BIND, BIND.SELF - independent variables */
    {
        const uint8_t code[] = {
            INSTR_LOAD_INT32, 4, IMM32(3),          /* Read by level 1 */
            INSTR_LOAD_INT32, 5, IMM32(0x40000),    /* Read by level 2 */
            INSTR_LOAD_ARRAY, 1, IMM32(2),
            INSTR_LOAD_INT32, 0, IMM32(9),
            INSTR_SET_ELEM,   1, IMM32(0), 0,
            INSTR_LOAD_INT32, 0, IMM32(0x8000),
            INSTR_SET_ELEM,   1, IMM32(1), 0,
            INSTR_LOAD_INT32, 0, IMM32(4),
            INSTR_LOAD_INT32, 2, IMM32(0x20000),
            INSTR_LOAD_FUN,   3, IMM32(49), 2, 7, 1, /* Overwritten by this function with level 2 */
            INSTR_BIND_SELF,  3, 0,
            INSTR_BIND,       3, 1, 2,
            INSTR_CALL,       0, 3, 0, 1,           /* Returns 0x10 */
            INSTR_LOAD_INT32, 1, IMM32(2),
            INSTR_SHL,        0, 0, 1,              /* 0x40 */
            INSTR_LOAD_ARRAY, 1, IMM32(3),
            INSTR_SET_ELEM,   1, IMM32(2), 0,       /* 0x100 */
            INSTR_LOAD_INT32, 0, IMM32(5),
            INSTR_CALL,       2, 3, 0, 1,
            INSTR_RETURN,     0, 2,

            /* Level 1: this outer function starts with:
             * 0 - independent var
             * 1 - args (bound but not preserved)
             * 2 - this
             * 3 - global regs
             * 4 - global integer */
            INSTR_LOAD_INT32, 0, IMM32(0x1000),
            INSTR_GET_ELEM,   5, 1, IMM32(-2),      /* 9 */
            INSTR_ADD,        2, 2, 5,              /* 4+9 = 13 */
            INSTR_GET_ELEM,   5, 3, IMM32(-2),      /* 3 */
            INSTR_ADD,        2, 2, 5,              /* 13+3 = 0x10 */
            INSTR_LOAD_FUN,   6, IMM32(29), 3, 107, 100,
            INSTR_BIND,       6, 0, 3,
            INSTR_BIND,       6, 1, 4,
            INSTR_BIND_SELF,  6, 2,
            INSTR_BIND,       6, 3, 1,
            INSTR_BIND,       6, 4, 2,
            INSTR_SET_ELEM,   3, IMM32(-3), 6,
            INSTR_RETURN,     1, 2,

            /* Level 2: this inner function starts with:
             * 100 - args array
             * 101 - this
             * 102 - global regs
             * 103 - global integer
             * 104 - level 1 regs
             * 105 - level 1 args
             * 106 - level 1 integer */
            INSTR_GET_ELEM,   0, 100, IMM32(-1),    /* 0x40 */
            INSTR_GET_ELEM,   2, 102, IMM32(-1),    /* 0x40000 */
            INSTR_GET_ELEM,   4, 104, IMM32(-1),    /* 0x1000 */
            INSTR_GET_ELEM,   5, 105, IMM32(-1),    /* 0x8000 */
            INSTR_OR,         0, 0, 101,            /* 0x40 | 0x5 */
            INSTR_OR,         0, 0, 2,              /* | 0x40000 */
            INSTR_OR,         0, 0, 103,            /* | 0x20000 */
            INSTR_OR,         0, 0, 4,              /* | 0x1000 */
            INSTR_OR,         0, 0, 5,              /* | 0x8000 */
            INSTR_OR,         0, 0, 106,            /* | 0x10 */
            INSTR_RETURN,     0, 0
        };

        TEST(_run_code(&ctx, frame, &code[0], sizeof(code), 6, 0) == TO_SMALL_INT(0x69055));
        TEST_NO_EXCEPTION();
    }

    KOS_context_destroy(&ctx);

    return 0;
}

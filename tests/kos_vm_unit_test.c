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
#include "../inc/kos_object_base.h"
#include "../inc/kos_string.h"
#include "../core/kos_object_internal.h"
#include "../core/kos_memory.h"
#include "../core/kos_misc.h"
#include "../core/kos_vm.h"
#include <stdio.h>
#include <string.h>

#define TEST(test) do { if (!(test)) { printf("Failed: line %d: %s\n", __LINE__, #test); return 1; } } while (0)

enum _VALUE_TYPE {
    V_NONE,     /* in     - no more instruction arguments      */
    V_EXCEPT,   /* out    - instruction generates an exception */
    V_OK,       /* out    - no result, no exception            */
    V_IMM8,     /* in     - immediate 8-bit integer            */
    V_IMM,      /* in     - immediate 32-bit integer           */
    V_VOID,     /* in/out - void                               */
    V_FALSE,    /* in/out - boolean - false                    */
    V_TRUE,     /* in/out - boolean - true                     */
    V_INTEGER,  /* out    - small int or integer, low, high    */
    V_INT32,    /* in     - 32-bit integer, low                */
    V_INT64,    /* in     - 64-bit integer, low, high          */
    V_FLOAT,    /* in     - float, low, high                   */
    V_STR0,     /* in/out - string 0, str(optional)            */
    V_STR1,     /* in/out - string 1, str(optional)            */
    V_STR2,     /* in/out - string 2, str(optional)            */
    V_ARRAY,    /* in/out - array, low(size)                   */
    V_OBJECT    /* in/out - object                             */
};

struct _INSTR_VALUE {
    enum _VALUE_TYPE value;
    uint32_t         low;
    uint32_t         high;
    const char      *str;
};

#define MAX_ARGS 3

struct _INSTR_DEF {
    KOS_BYTECODE_INSTR  instr;
    struct _INSTR_VALUE out;
    struct _INSTR_VALUE in[MAX_ARGS];
};

#define TEST_INSTR \
{ \
    struct _INSTR_DEF instr = {

#define END \
    }; \
    if (_test_instr(&ctx, instr.instr, __LINE__, &instr.out, &instr.in[0]) != KOS_SUCCESS) \
        return 1; \
}

static int _test_instr(KOS_CONTEXT         *ctx,
                       KOS_BYTECODE_INSTR   instr,
                       int                  line,
                       struct _INSTR_VALUE *ret_val,
                       struct _INSTR_VALUE *args)
{
    uint8_t            code[64]        = { 0 };
    uint32_t           parms[MAX_ARGS] = { 0 };
    struct _KOS_MODULE module;
    KOS_FRAME          frame           = &ctx->main_thread.frame;
    const char        *cstrings[]      = { "aaa", "bbb", "ccc" };
    KOS_OBJ_ID         strings;
    uint8_t            regs            = 0;
    unsigned           words           = 0;
    int                error           = KOS_SUCCESS;
    int                i;
    KOS_OBJ_ID         ret;

    strings = KOS_new_array(frame, 3);
    if (IS_BAD_PTR(strings)) {
        printf("Failed: Unable to allocate strings!\n");
        return KOS_ERROR_EXCEPTION;
    }

    for (i = 0; i < 3; i++) {
        KOS_OBJ_ID str = KOS_new_cstring(frame, cstrings[i]);
        if (IS_BAD_PTR(str)) {
            printf("Failed: Unable to allocate strings!\n");
            return KOS_ERROR_EXCEPTION;
        }
        error = KOS_array_write(frame, strings, i, str);
        if (error) {
            printf("Failed: Unable to allocate strings!\n");
            return error;
        }
    }

    for (i=0; i < MAX_ARGS; i++) {

        if (args[i].value == V_NONE)
            break;

        assert(words + MAX_ARGS < sizeof(code)/sizeof(code[0]));

        switch (args[i].value) {

            case V_IMM:
                /* fall through */
            case V_IMM8:
                parms[i] = args[i].low;
                break;

            case V_VOID:
                code[words++] = INSTR_LOAD_VOID;
                code[words++] = regs;
                parms[i]      = regs++;
                break;

            case V_FALSE:
                code[words++] = INSTR_LOAD_FALSE;
                code[words++] = regs;
                parms[i]      = regs++;
                break;

            case V_TRUE:
                code[words++] = INSTR_LOAD_TRUE;
                code[words++] = regs;
                parms[i]      = regs++;
                break;

            case V_INT32:
                code[words++] = INSTR_LOAD_INT32;
                code[words++] = regs;
                code[words++] = (uint8_t)args[i].low;
                code[words++] = (uint8_t)(args[i].low >> 8);
                code[words++] = (uint8_t)(args[i].low >> 16);
                code[words++] = (uint8_t)(args[i].low >> 24);
                parms[i]      = regs++;
                break;

            case V_INT64:
                code[words++] = INSTR_LOAD_INT64;
                code[words++] = regs;
                code[words++] = (uint8_t)args[i].low;
                code[words++] = (uint8_t)(args[i].low >> 8);
                code[words++] = (uint8_t)(args[i].low >> 16);
                code[words++] = (uint8_t)(args[i].low >> 24);
                code[words++] = (uint8_t)args[i].high;
                code[words++] = (uint8_t)(args[i].high >> 8);
                code[words++] = (uint8_t)(args[i].high >> 16);
                code[words++] = (uint8_t)(args[i].high >> 24);
                parms[i]      = regs++;
                break;

            case V_FLOAT:
                code[words++] = INSTR_LOAD_FLOAT;
                code[words++] = regs;
                code[words++] = (uint8_t)args[i].low;
                code[words++] = (uint8_t)(args[i].low >> 8);
                code[words++] = (uint8_t)(args[i].low >> 16);
                code[words++] = (uint8_t)(args[i].low >> 24);
                code[words++] = (uint8_t)args[i].high;
                code[words++] = (uint8_t)(args[i].high >> 8);
                code[words++] = (uint8_t)(args[i].high >> 16);
                code[words++] = (uint8_t)(args[i].high >> 24);
                parms[i]      = regs++;
                break;

            case V_STR0:
                code[words++] = INSTR_LOAD_STR;
                code[words++] = regs;
                code[words++] = 0;
                code[words++] = 0;
                code[words++] = 0;
                code[words++] = 0;
                parms[i]      = regs++;
                if (args[i].str) {
                    KOS_OBJ_ID str = KOS_new_cstring(frame, args[i].str);
                    if (IS_BAD_PTR(str)) {
                        printf("Failed: Unable to allocate strings!\n");
                        return KOS_ERROR_EXCEPTION;
                    }
                    error = KOS_array_write(frame, strings, 0, str);
                    if (error) {
                        printf("Failed: Unable to allocate strings!\n");
                        return error;
                    }
                }
                break;

            case V_STR1:
                code[words++] = INSTR_LOAD_STR;
                code[words++] = regs;
                code[words++] = 1;
                code[words++] = 0;
                code[words++] = 0;
                code[words++] = 0;
                parms[i]      = regs++;
                if (args[i].str) {
                    KOS_OBJ_ID str = KOS_new_cstring(frame, args[i].str);
                    if (IS_BAD_PTR(str)) {
                        printf("Failed: Unable to allocate strings!\n");
                        return KOS_ERROR_EXCEPTION;
                    }
                    error = KOS_array_write(frame, strings, 1, str);
                    if (error) {
                        printf("Failed: Unable to allocate strings!\n");
                        return error;
                    }
                }
                break;

            case V_STR2:
                code[words++] = INSTR_LOAD_STR;
                code[words++] = regs;
                code[words++] = 2;
                code[words++] = 0;
                code[words++] = 0;
                code[words++] = 0;
                parms[i]      = regs++;
                if (args[i].str) {
                    KOS_OBJ_ID str = KOS_new_cstring(frame, args[i].str);
                    if (IS_BAD_PTR(str)) {
                        printf("Failed: Unable to allocate strings!\n");
                        return KOS_ERROR_EXCEPTION;
                    }
                    error = KOS_array_write(frame, strings, 2, str);
                    if (error) {
                        printf("Failed: Unable to allocate strings!\n");
                        return error;
                    }
                }
                break;

            case V_ARRAY:
                code[words++] = INSTR_LOAD_ARRAY;
                code[words++] = regs;
                code[words++] = (uint8_t)args[i].low;
                code[words++] = (uint8_t)(args[i].low >> 8);
                code[words++] = (uint8_t)(args[i].low >> 16);
                code[words++] = (uint8_t)(args[i].low >> 24);
                parms[i]      = regs++;
                break;

            case V_OBJECT:
                code[words++] = INSTR_LOAD_OBJ;
                code[words++] = regs;
                parms[i]      = regs++;
                break;

            default:
                assert(!"invalid instruction argument!");
                error = KOS_ERROR_EXCEPTION;
                break;
        }
    }

    assert(words + MAX_ARGS + 3 < (int)(sizeof(code)/sizeof(code[0])));

    if (regs == 0)
        regs = 1;

    if (instr == INSTR_JUMP_COND ||
        instr == INSTR_JUMP_NOT_COND) {

        ++regs;
        code[words++] = INSTR_LOAD_TRUE;
        code[words++] = regs - 1U;
        code[words++] = (uint8_t)instr;
        code[words++] = 2; /* delta */
        code[words++] = 0;
        code[words++] = 0;
        code[words++] = 0;
        code[words++] = 0; /* test reg */
        code[words++] = INSTR_LOAD_FALSE;
        code[words++] = regs - 1U;
    }
    else {

        code[words++] = (uint8_t)instr;

        if (instr != INSTR_SET      &&
            instr != INSTR_SET_ELEM &&
            instr != INSTR_SET_PROP &&
            instr != INSTR_PUSH     &&
            instr != INSTR_PUSH_EX  &&
            instr != INSTR_DEL      &&
            instr != INSTR_DEL_PROP)

            code[words++] = regs - 1U;

        for (i=0; i < MAX_ARGS; i++) {
            if (args[i].value == V_NONE)
                break;
            if (args[i].value == V_IMM) {
                code[words++] = (uint8_t)args[i].low;
                code[words++] = (uint8_t)(args[i].low >> 8);
                code[words++] = (uint8_t)(args[i].low >> 16);
                code[words++] = (uint8_t)(args[i].low >> 24);
            }
            else
                code[words++] = (uint8_t)parms[i];
        }
    }

    code[words++] = INSTR_RETURN;
    code[words++] = 0;
    code[words++] = regs - 1U;

    memset(&module, 0, sizeof(module));

    module.header.type   = OBJ_MODULE;
    module.context       = ctx;
    module.strings       = strings;
    module.bytecode      = &code[0];
    module.bytecode_size = words;
    module.instr_offs    = 0;
    module.num_regs      = regs;

    error = _KOS_vm_run_module(&module, &ret);

    if (ret_val->value == V_EXCEPT) {
        if (error != KOS_ERROR_EXCEPTION) {
            printf("Failed: line %d: expected exception\n", line);
            error = KOS_ERROR_EXCEPTION;
        }
        else
            error = KOS_SUCCESS;
    }
    else if (error) {
        assert(error == KOS_ERROR_EXCEPTION);
        printf("Failed: line %d: unexpected exception\n", line);
    }
    else switch (ret_val->value) {

        case V_OK:
            break;

        case V_VOID:
            if (IS_BAD_PTR(ret)   ||
                IS_SMALL_INT(ret) ||
                ret != KOS_VOID) {

                printf("Failed: line %d: expected void\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            break;

        case V_FALSE:
            if (IS_BAD_PTR(ret)                       ||
                IS_SMALL_INT(ret)                     ||
                (ret != KOS_FALSE && ret != KOS_TRUE) ||
                KOS_get_bool(ret) != 0) {

                printf("Failed: line %d: expected false\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            break;

        case V_TRUE:
            if (IS_BAD_PTR(ret)                       ||
                IS_SMALL_INT(ret)                     ||
                (ret != KOS_FALSE && ret != KOS_TRUE) ||
                KOS_get_bool(ret) == 0) {

                printf("Failed: line %d: expected true\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            break;

        case V_INTEGER:
            if (IS_BAD_PTR(ret) || (!IS_SMALL_INT(ret) && GET_OBJ_TYPE(ret) != OBJ_INTEGER)) {
                printf("Failed: line %d: expected integer\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            else {
                int64_t value;
                if (IS_SMALL_INT(ret))
                    value = GET_SMALL_INT(ret);
                else
                    value = OBJPTR(INTEGER, ret)->value;
                if ((uint32_t)(value >> 32) != ret_val->high ||
                    (uint32_t)value         != ret_val->low) {

                    printf("Failed: line %d: expected integer 0x%08X:%08X, but got 0x%08X:%08X\n",
                           line, (unsigned)ret_val->high, (unsigned)ret_val->low,
                           (unsigned)(uint32_t)(value >> 32), (unsigned)(uint32_t)value);
                    error = KOS_ERROR_EXCEPTION;
                }
            }
            break;

        case V_FLOAT:
            if (IS_BAD_PTR(ret)   ||
                IS_SMALL_INT(ret) ||
                GET_OBJ_TYPE(ret) != OBJ_FLOAT) {

                printf("Failed: line %d: expected float\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            else {
                uint64_t value = _KOS_double_to_uint64_t(OBJPTR(FLOAT, ret)->value);
                if ((uint32_t)(value >> 32) != ret_val->high ||
                    (uint32_t)value         != ret_val->low) {

                    printf("Failed: line %d: expected float 0x%08X:%08X, but got 0x%08X:%08X\n",
                           line, (unsigned)ret_val->high, (unsigned)ret_val->low,
                           (unsigned)(uint32_t)(value >> 32), (unsigned)(uint32_t)value);
                    error = KOS_ERROR_EXCEPTION;
                }
            }
            break;

        case V_STR0:
        case V_STR1:
        case V_STR2:
            if (IS_BAD_PTR(ret) ||
                GET_OBJ_TYPE(ret) != OBJ_STRING) {

                printf("Failed: line %d: expected string\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            else {
                KOS_OBJ_ID expected;
                int        idx;
                if (ret_val->value == V_STR0)
                    idx = 0;
                else if (ret_val->value == V_STR1)
                    idx = 1;
                else
                    idx = 2;
                if (ret_val->str)
                    expected = KOS_new_cstring(frame, ret_val->str);
                else
                    expected = KOS_array_read(frame, strings, idx);
                if (IS_BAD_PTR(expected)) {
                    printf("Failed: Unable to allocate strings\n");
                    error = KOS_ERROR_EXCEPTION;
                }
                if ( ! error && KOS_string_compare(ret, expected)) {
                    struct _KOS_VECTOR cstr;

                    _KOS_vector_init(&cstr);
                    KOS_clear_exception(frame);

                    error = KOS_string_to_cstr_vec(frame, expected, &cstr);
                    if (error)
                        printf("Failed: line %d: expected string ?\n", line);
                    else
                        printf("Failed: line %d: expected string \"%s\"\n", line, cstr.buffer);

                    _KOS_vector_destroy(&cstr);
                    error = KOS_ERROR_EXCEPTION;
                }
            }
            break;

        case V_ARRAY:
            if (IS_BAD_PTR(ret)   ||
                IS_SMALL_INT(ret) ||
                GET_OBJ_TYPE(ret) != OBJ_ARRAY) {

                printf("Failed: line %d: expected array\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            else {
                if (KOS_get_array_size(ret) != ret_val->low) {
                    printf("Failed: line %d: expected array of size %u, but got size %u\n",
                           line, (uint32_t)ret_val->low, (uint32_t)KOS_get_array_size(ret));
                    error = KOS_ERROR_EXCEPTION;
                }
            }
            break;

        case V_OBJECT:
            if (IS_BAD_PTR(ret)   ||
                IS_SMALL_INT(ret) ||
                GET_OBJ_TYPE(ret) != OBJ_OBJECT) {

                printf("Failed: line %d: expected object\n", line);
                error = KOS_ERROR_EXCEPTION;
            }
            break;

        default:
            assert(!"invalid instruction return value!");
            error = KOS_ERROR_EXCEPTION;
            break;
    }

    return error;
}

int main(void)
{
    KOS_CONTEXT ctx;
    KOS_FRAME   frame;

    TEST(KOS_context_init(&ctx, &frame) == KOS_SUCCESS);

    /*========================================================================*/
    /* LOAD.VOID */
    TEST_INSTR INSTR_LOAD_VOID,  { V_VOID  } END

    /*========================================================================*/
    /* LOAD.FALSE */
    TEST_INSTR INSTR_LOAD_FALSE, { V_FALSE } END

    /*========================================================================*/
    /* LOAD.TRUE */
    TEST_INSTR INSTR_LOAD_TRUE,  { V_TRUE  } END

    /*========================================================================*/
    /* LOAD.STR */
    TEST_INSTR INSTR_LOAD_STR,   { V_STR0                              }, { { V_IMM,   0                        } }                                        END

    /*========================================================================*/
    /* LOAD.INT8 */
    TEST_INSTR INSTR_LOAD_INT8,  { V_INTEGER, 0                        }, { { V_IMM8,  0                        } }                                        END
    TEST_INSTR INSTR_LOAD_INT8,  { V_INTEGER, 0x7FU                    }, { { V_IMM8,  0x7F                     } }                                        END
    TEST_INSTR INSTR_LOAD_INT8,  { V_INTEGER, 0xFFFFFF80U, ~0U         }, { { V_IMM8,  0x80                     } }                                        END

    /*========================================================================*/
    /* LOAD.INT32 */
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, 0                        }, { { V_IMM,   0                        } }                                        END
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, 0x1FFFFFFFU              }, { { V_IMM,   0x1FFFFFFFU              } }                                        END
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, 0x7FFFFFFFU              }, { { V_IMM,   0x7FFFFFFFU              } }                                        END
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, 0x80000000U, ~0U         }, { { V_IMM,   0x80000000U              } }                                        END
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, 0x80000000U, ~0U         }, { { V_IMM,   0x80000000U              } }                                        END
    TEST_INSTR INSTR_LOAD_INT32, { V_INTEGER, ~0U,         ~0U         }, { { V_IMM,   ~0U                      } }                                        END

    /*========================================================================*/
    /* LOAD.INT64 */
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, 0                        }, { { V_INT64, 0                        } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, 0x7FFFFFFFU              }, { { V_INT64, 0x7FFFFFFFU              } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, 0x80000000U,             }, { { V_INT64, 0x80000000U              } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, ~0U,         0x7FFFFFFFU }, { { V_INT64, ~0U,         0x7FFFFFFFU } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, 0,           0x80000000U }, { { V_INT64, 0,           0x80000000U } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, 0,           0x40000000U }, { { V_INT64, 0,           0x40000000U } }                                        END
    TEST_INSTR INSTR_MOVE,       { V_INTEGER, ~0U,         ~0U         }, { { V_INT64, ~0U,         ~0U         } }                                        END

    /*========================================================================*/
    /* LOAD.FLOAT */
    TEST_INSTR INSTR_MOVE,       { V_FLOAT,   0,           0x3FF00000U }, { { V_FLOAT, 0,           0x3FF00000U } }                                        END

    /*========================================================================*/
    /* LOAD.ARRAY8 */
    TEST_INSTR INSTR_LOAD_ARRAY8,{ V_ARRAY,   0                        }, { { V_IMM8,  0                        } }                                        END
    TEST_INSTR INSTR_LOAD_ARRAY8,{ V_ARRAY,   255                      }, { { V_IMM8,  255                      } }                                        END

    /*========================================================================*/
    /* LOAD.ARRAY */
    TEST_INSTR INSTR_LOAD_ARRAY, { V_ARRAY,   0                        }, { { V_IMM,   0                        } }                                        END
    TEST_INSTR INSTR_LOAD_ARRAY, { V_ARRAY,   10                       }, { { V_IMM,   10                       } }                                        END

    /*========================================================================*/
    /* LOAD.OBJ */
    TEST_INSTR INSTR_LOAD_OBJ,   { V_OBJECT                            }                                                                                   END

    /*========================================================================*/
    /* THROW */
    TEST_INSTR INSTR_THROW,      { V_EXCEPT                            }, { { V_INT32, 0                        }                                        } END

    /*========================================================================*/
    /* GET */
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_VOID                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_INT32, 0,                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0x3FF00000U }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_STR1                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_STR0                            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_VOID                            } } END
    /* string */
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "b"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "a"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "d"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "b"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, (uint32_t)-3             } } END
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "a"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, (uint32_t)-2             } } END
    TEST_INSTR INSTR_GET,        { V_STR1,    0, 0,        "d"         }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, (uint32_t)-4             } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_STR0,  0, 0,        "bad"       }, { V_INT32, 3                        } } END
    /* array */
    TEST_INSTR INSTR_GET,        { V_VOID                              }, { { V_ARRAY, 10                       }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_GET,        { V_VOID                              }, { { V_ARRAY, 10                       }, { V_INT32, 9                        } } END
    TEST_INSTR INSTR_GET,        { V_VOID                              }, { { V_ARRAY, 10                       }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_GET,        { V_VOID                              }, { { V_ARRAY, 10                       }, { V_INT32, (uint32_t)-10            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_INT32, (uint32_t)-11            } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_INT32, 10                       } } END
    /* wrong types */
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_ARRAY, 5                        } } END
    TEST_INSTR INSTR_GET,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* GET.ELEM */
    /* string */
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "b"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "a"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   1                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "d"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   2                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "b"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   (uint32_t)-3             } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "a"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   (uint32_t)-2             } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_STR1,    0, 0,        "d"         }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   (uint32_t)-1             } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   (uint32_t)-4             } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_STR0,  0, 0,        "bad"       }, { V_IMM,   3                        } } END
    /* array */
    TEST_INSTR INSTR_GET_ELEM,   { V_VOID                              }, { { V_ARRAY, 10                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_VOID                              }, { { V_ARRAY, 10                       }, { V_IMM,   9                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_VOID                              }, { { V_ARRAY, 10                       }, { V_IMM,   (uint32_t)-1             } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_VOID                              }, { { V_ARRAY, 10                       }, { V_IMM,   (uint32_t)-10            } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_IMM,   (uint32_t)-11            } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_IMM,   10                       } } END
    /* wrong types */
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_VOID                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_FALSE                           }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_INT32, 0,                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_ELEM,   { V_EXCEPT                            }, { { V_FLOAT, 0,           0x3FF00000U }, { V_IMM,   0                        } } END

    /*========================================================================*/
    /* GET.RANGE */
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "def"       }, { { V_STR0,  0, 0,        "abcdefgh"  }, { V_INT32, 3   }, { V_INT32, 6      } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "abc"       }, { { V_STR0,  0, 0,        "abcdefgh"  }, { V_VOID       }, { V_INT32, (uint32_t)-5 } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "gh"        }, { { V_STR0,  0, 0,        "abcdefgh"  }, { V_INT32, (uint32_t)-2  }, { V_VOID } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "xyz"       }, { { V_STR0,  0, 0,        "xyz"       }, { V_INT32, (uint32_t)-99 }, { V_INT32, 99 } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        ""          }, { { V_STR0,  0, 0,        "xyz"       }, { V_INT32, 99  }, { V_INT32, (uint32_t)-99 } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "rs"        }, { { V_STR0,  0, 0,        "pqrstuv"   }, { V_FLOAT, ~0U, 0x40021111U }, { V_FLOAT, ~0U, 0xC0044444U } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_STR1,    0, 0,        "mnop"      }, { { V_STR0,  0, 0,        "mnop"      }, { V_VOID       }, { V_VOID          } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_EXCEPT                            }, { { V_STR0,  0, 0,        "abc"       }, { V_FALSE      }, { V_VOID          } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_EXCEPT                            }, { { V_STR0,  0, 0,        "abc"       }, { V_VOID       }, { V_FALSE         } } END
    TEST_INSTR INSTR_GET_RANGE,  { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID       }, { V_VOID          } } END

    /*========================================================================*/
    /* GET.PROP */
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_VOID                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_FALSE                           }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_INT32, 0,                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_FLOAT, 0,           0x3FF00000U }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_STR1                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_GET_PROP,   { V_EXCEPT                            }, { { V_OBJECT                          }, { V_IMM,   0                        } } END

    /*========================================================================*/
    /* HAS */
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_VOID                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_FALSE                           }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_INT32, 0,                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_FLOAT, 0,           0x3FF00000U }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_STR1                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_ARRAY, 10                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_STR0                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_VOID                            } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_ARRAY, 5                        } } END
    TEST_INSTR INSTR_HAS,        { V_FALSE                             }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* HAS.PROP */
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_VOID                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_FALSE                           }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_INT32, 0,                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_FLOAT, 0,           0x3FF00000U }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_STR1                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_ARRAY, 10                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_HAS_PROP,   { V_FALSE                             }, { { V_OBJECT                          }, { V_IMM,   0                        } } END

    /*========================================================================*/
    /* DEL */
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_VOID                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_FALSE                           }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_INT32, 0,                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_FLOAT, 0,           0x3FF00000U }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_STR1                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_ARRAY, 10                       }, { V_STR0                            } } END
    TEST_INSTR INSTR_DEL,        { V_OK                                }, { { V_OBJECT                          }, { V_STR0                            } } END
    /* wrong types */
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_VOID                            } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_ARRAY, 10                       }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_STR0,  10                       }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_ARRAY, 5                        } } END
    TEST_INSTR INSTR_DEL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* DEL.PROP */
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_VOID                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_FALSE                           }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_INT32, 0,                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_FLOAT, 0,           0x3FF00000U }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_STR1                            }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_ARRAY, 10                       }, { V_IMM,   0                        } } END
    TEST_INSTR INSTR_DEL_PROP,   { V_OK                                }, { { V_OBJECT                          }, { V_IMM,   0                        } } END

    /*========================================================================*/
    /* PUSH */
    TEST_INSTR INSTR_PUSH,       { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH,       { V_EXCEPT                            }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH,       { V_EXCEPT                            }, { { V_INT32, 0,                       }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH,       { V_EXCEPT                            }, { { V_STR1                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH,       { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH,       { V_OK                                }, { { V_ARRAY, 0                        }, { V_INT32, 1                        } } END

    /*========================================================================*/
    /* PUSH.EX */
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_INT32, 0,                       }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_STR1                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_OK                                }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_PUSH_EX,    { V_OK                                }, { { V_ARRAY, 0                        }, { V_STR0                            } } END

    /*========================================================================*/
    /* TYPE */
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "integer"   }, { { V_INT32, 0                        } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "integer"   }, { { V_INT32, 0x7FFFFFFFU              } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "integer"   }, { { V_INT64, ~0U,         0x7FFFFFFFU } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "float"     }, { { V_FLOAT, 0,           0xFFF00000U } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "void"      }, { { V_VOID                            } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "boolean"   }, { { V_FALSE                           } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "boolean"   }, { { V_TRUE                            } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "string"    }, { { V_STR1,  0, 0,        ""          } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "string"    }, { { V_STR1,                           } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "array"     }, { { V_ARRAY, 0                        } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "array"     }, { { V_ARRAY, 100                      } }                                        END
    TEST_INSTR INSTR_TYPE,       { V_STR0,    0, 0,        "object"    }, { { V_OBJECT                          } }                                        END

    /*========================================================================*/
    /* ADD */
    TEST_INSTR INSTR_ADD,        { V_INTEGER, 5,           0           }, { { V_INT32, 2                        }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, 0,           0           }, { { V_INT32, (uint32_t)-1             }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, ~0U,         ~0U         }, { { V_INT64, 0,           0x80000000U }, { V_INT64, ~0U,         0x7FFFFFFFU } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, ~0U,         0x7FFFFFFFU }, { { V_INT32, 1                        }, { V_INT64, 0xFFFFFFFEU, 0x7FFFFFFFU } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, ~0U,         0x7FFFFFFFU }, { { V_INT64, 0,           0x80000000U }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, 0,           0x80000000U }, { { V_INT64, ~0U,         0x7FFFFFFFU }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_ADD,        { V_INTEGER, 0x80000000U, 0           }, { { V_INT32, 0x7FFFFFFFU              }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_ADD,        { V_FLOAT,   0,           0x40000000U }, { { V_INT32, 1                        }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_ADD,        { V_FLOAT,   0,           0x40000000U }, { { V_FLOAT, 0,           0x3FF00000U }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_ADD,        { V_FLOAT,   0,           0x43E00000U }, { { V_INT64, 0,           0x40000000U }, { V_FLOAT, 0,           0x43D00000U } } END
    TEST_INSTR INSTR_ADD,        { V_FLOAT,   0,           0x40080000U }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0x3FF00000U } } END
    /* wrong types */
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    /* string */
    TEST_INSTR INSTR_ADD,        { V_STR0,    0, 0,        "abcdef"    }, { { V_STR1,  0, 0,        "abc"       }, { V_STR2,  0, 0,        "def"       } } END
    TEST_INSTR INSTR_ADD,        { V_STR0,    0, 0,        "abc"       }, { { V_STR1,  0, 0,        "abc"       }, { V_STR2,  0, 0,        ""          } } END
    TEST_INSTR INSTR_ADD,        { V_STR0,    0, 0,        "def"       }, { { V_STR1,  0, 0,        ""          }, { V_STR2,  0, 0,        "def"       } } END
    /* wrong types */
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_STR0                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_ADD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* SUB */
    TEST_INSTR INSTR_SUB,        { V_INTEGER, ~0U,         ~0U         }, { { V_INT32, 2                        }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_SUB,        { V_INTEGER, 2,           0           }, { { V_INT32, 1                        }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_SUB,        { V_INTEGER, 0,           0x80000000U }, { { V_INT32, (uint32_t)-1             }, { V_INT64, ~0U,         0x7FFFFFFFU } } END
    TEST_INSTR INSTR_SUB,        { V_INTEGER, 1,           0x80000000U }, { { V_INT64, 0,           0x80000000U }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_SUB,        { V_INTEGER, ~0U,         0x7FFFFFFFU }, { { V_INT64, 0,           0x80000000U }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_SUB,        { V_INTEGER, 0,           0x80000000U }, { { V_INT64, ~0U,         0x7FFFFFFFU }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_SUB,        { V_FLOAT,   0,           0x40000000U }, { { V_INT32, 1                        }, { V_FLOAT, 0,           0xBFF00000U } } END
    TEST_INSTR INSTR_SUB,        { V_FLOAT,   0,           0x40000000U }, { { V_FLOAT, 0,           0x3FF00000U }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_SUB,        { V_FLOAT,   0,           0x43E00000U }, { { V_INT64, 0,           0x40000000U }, { V_FLOAT, 0,           0xC3D00000U } } END
    TEST_INSTR INSTR_SUB,        { V_FLOAT,   0,           0x40080000U }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0xBFF00000U } } END
    /* wrong types */
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SUB,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* MUL */
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 42,          0           }, { { V_INT32, 6                        }, { V_INT32, 7                        } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, ~0U,         ~0U         }, { { V_INT32, 1                        }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0x80000000U, 0xC0000000U }, { { V_INT32, 0x80000000U              }, { V_INT32, 0x7FFFFFFFU              } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, ~0U,         0x7FFFFFFFU }, { { V_INT32, 1                        }, { V_INT64, ~0U,         0x7FFFFFFFU } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 1,           0x80000000U }, { { V_INT64, ~0U,         0x7FFFFFFFU }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0xFFFFFFFEU, 0x7FFFFFFFU }, { { V_INT64, ~0U,         0x3FFFFFFFU }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0xFFFFFFFDU, 0xBFFFFFFFU }, { { V_INT64, ~0U,         0x3FFFFFFFU }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0,           0           }, { { V_INT64, 0,           1           }, { V_INT64, 0,           1           } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0,           0x80000000U }, { { V_INT64, 0,           0x80000000U }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 0,           0x80000000U }, { { V_INT64, 0,           0x80000000U }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_MUL,        { V_INTEGER, 1,           0x80000000U }, { { V_INT64, ~0U,         0x7FFFFFFFU }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_MUL,        { V_FLOAT,   0,           0xBFF00000U }, { { V_INT32, 1                        }, { V_FLOAT, 0,           0xBFF00000U } } END
    TEST_INSTR INSTR_MUL,        { V_FLOAT,   0,           0xBFF00000U }, { { V_FLOAT, 0,           0x3FF00000U }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_MUL,        { V_FLOAT,   0,           0xC7B00000U }, { { V_INT64, 0,           0x40000000U }, { V_FLOAT, 0,           0xC3D00000U } } END
    TEST_INSTR INSTR_MUL,        { V_FLOAT,   0,           0xC0000000U }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0xBFF00000U } } END
    /* wrong types */
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MUL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* DIV */
    TEST_INSTR INSTR_DIV,        { V_INTEGER, 3,           0           }, { { V_INT32, 6                        }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_DIV,        { V_INTEGER, (uint32_t)-3,~0U         }, { { V_INT32, 6                        }, { V_INT32, (uint32_t)-2             } } END
    TEST_INSTR INSTR_DIV,        { V_INTEGER, 1,           0           }, { { V_INT32, 6                        }, { V_INT32, 4                        } } END
    TEST_INSTR INSTR_DIV,        { V_FLOAT,   0,           0x3FF80000U }, { { V_INT32, 6                        }, { V_FLOAT, 0,           0x40100000U } } END
    TEST_INSTR INSTR_DIV,        { V_FLOAT,   0,           0x3FE00000U }, { { V_FLOAT, 0,           0x3FF00000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_DIV,        { V_FLOAT,   0,           0x3FE00000U }, { { V_FLOAT, 0,           0x3FF00000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_DIV,        { V_INTEGER, 0,           0x20000000U }, { { V_INT64, 0,           0x40000000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_DIV,        { V_INTEGER, 1,           0           }, { { V_INT64, 0,           0x40000000U }, { V_INT64, 0,           0x40000000U } } END
    TEST_INSTR INSTR_DIV,        { V_FLOAT,   0,           0x43D00000U }, { { V_INT64, 0,           0x40000000U }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_DIV,        { V_FLOAT,   0,           0x43C00000U }, { { V_FLOAT, 0,           0x43D00000U }, { V_INT32, 2                        } } END
    /* division by zero */
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0x43D00000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0x43D00000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0,           0x01000000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0,           0x01000000U }, { V_FLOAT, 0,           0           } } END
    /* wrong types */
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_DIV,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* MOD */
    TEST_INSTR INSTR_MOD,        { V_INTEGER, 2,           0           }, { { V_INT32, 10                       }, { V_INT32, 4                        } } END
    TEST_INSTR INSTR_MOD,        { V_FLOAT,   0,           0x40000000U }, { { V_FLOAT, 0,           0x40000000U }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_MOD,        { V_FLOAT,   0,           0x40000000U }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0xC0080000U } } END
    /* division by zero */
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0x43D00000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0x43D00000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0,           0x01000000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0,           0x01000000U }, { V_FLOAT, 0,           0           } } END
    /* wrong types */
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_MOD,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* AND */
    TEST_INSTR INSTR_AND,        { V_INTEGER, 0x01446014U, 0x01446014U }, { { V_INT64, 0xABC67ABCU, 0xABC67ABCU }, { V_INT64, 0x456DE456U, 0x456DE456U } } END
    TEST_INSTR INSTR_AND,        { V_INTEGER, 0x0000BEEFU, 0           }, { { V_FLOAT, 0,           0x40EFFFE4U }, { V_FLOAT, 0xDDFDFBE7U, 0x41E81BD7U } } END
    /* wrong types */
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_AND,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* OR */
    TEST_INSTR INSTR_OR,         { V_INTEGER, 0x12345678U, 0x02ABCDEFU }, { { V_INT64, 0x12345678U, 0           }, { V_INT64, 0,           0x02ABCDEFU } } END
    TEST_INSTR INSTR_OR,         { V_INTEGER, 0xC0DEFFFFU, 0           }, { { V_FLOAT, 0,           0x40EFFFE4U }, { V_FLOAT, 0xDDFDFBE7U, 0x41E81BD7U } } END
    /* wrong types */
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_OR,         { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* XOR */
    TEST_INSTR INSTR_XOR,        { V_INTEGER, 0x12345678U, 0x02ABCDEFU }, { { V_INT64, 0x12345678U, 0           }, { V_INT64, 0,           0x02ABCDEFU } } END
    TEST_INSTR INSTR_XOR,        { V_INTEGER, 0xC0DE4110U, 0           }, { { V_FLOAT, 0,           0x40EFFFE4U }, { V_FLOAT, 0xDDFDFBE7U, 0x41E81BD7U } } END
    /* wrong types */
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_XOR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* SHL */
    TEST_INSTR INSTR_SHL,        { V_INTEGER, 0x23456780U, 0xEDCBA001U }, { { V_INT64, 0x12345678U, 0xFEDCBA00U }, { V_INT32, 4                        } } END
    TEST_INSTR INSTR_SHL,        { V_INTEGER, 0x00000400U, 0           }, { { V_FLOAT, 0x55555555U, 0x40700555U }, { V_FLOAT, 0xAAAAAAAAU, 0x4002AAAAU } } END
    TEST_INSTR INSTR_SHL,        { V_INTEGER, 4,           0           }, { { V_FLOAT, 0,           0x40100000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_INTEGER, 0,           0x80000000U }, { { V_INT32, 1                        }, { V_INT32, 63                       } } END
    /* wrong types */
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHL,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* SHR */
    TEST_INSTR INSTR_SHR,        { V_INTEGER, 0x01234567U, 0xFFEDCBA0U }, { { V_INT64, 0x12345678U, 0xFEDCBA00U }, { V_INT32, 4                        } } END
    TEST_INSTR INSTR_SHR,        { V_INTEGER, 0x00000040U, 0           }, { { V_FLOAT, 0x55555555U, 0x40700555U }, { V_FLOAT, 0xAAAAAAAAU, 0x4002AAAAU } } END
    TEST_INSTR INSTR_SHR,        { V_INTEGER, 4,           0           }, { { V_FLOAT, 0,           0x40100000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_INTEGER, 1,           0           }, { { V_FLOAT, 0,           0x43D00000U }, { V_INT32, 62                       } } END
    /* wrong types */
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHR,        { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* SHRU */
    TEST_INSTR INSTR_SHRU,       { V_INTEGER, 0x01234567U, 0x0FEDCBA0U }, { { V_INT64, 0x12345678U, 0xFEDCBA00U }, { V_INT32, 4                        } } END
    TEST_INSTR INSTR_SHRU,       { V_INTEGER, 0x00000040U, 0           }, { { V_FLOAT, 0x55555555U, 0x40700555U }, { V_FLOAT, 0xAAAAAAAAU, 0x4002AAAAU } } END
    TEST_INSTR INSTR_SHRU,       { V_INTEGER, 4,           0           }, { { V_FLOAT, 0,           0x40100000U }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_INTEGER, 1,           0           }, { { V_FLOAT, 0,           0x43D00000U }, { V_INT32, 62                       } } END
    /* wrong types */
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_STR0                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_STR0                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_STR0                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_OBJECT                          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_STR0                            }, { V_STR1                            } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_SHRU,       { V_EXCEPT                            }, { { V_OBJECT                          }, { V_OBJECT                          } } END

    /*========================================================================*/
    /* NOT */
    TEST_INSTR INSTR_NOT,        { V_INTEGER, 0xEDCBA987U, 0x012345FFU }, { { V_INT64, 0x12345678U, 0xFEDCBA00U }                                        } END
    TEST_INSTR INSTR_NOT,        { V_INTEGER, 0xFFFFFEFFU, ~0U         }, { { V_FLOAT, 0x55555555U, 0x40700555U }                                        } END
    /* wrong types */
    TEST_INSTR INSTR_NOT,        { V_EXCEPT                            }, { { V_VOID                            }                                        } END
    TEST_INSTR INSTR_NOT,        { V_EXCEPT                            }, { { V_FALSE                           }                                        } END
    TEST_INSTR INSTR_NOT,        { V_EXCEPT                            }, { { V_STR0                            }                                        } END
    TEST_INSTR INSTR_NOT,        { V_EXCEPT                            }, { { V_ARRAY, 0                        }                                        } END
    TEST_INSTR INSTR_NOT,        { V_EXCEPT                            }, { { V_OBJECT                          }                                        } END

    /*========================================================================*/
    /* CMP.EQ */
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_TRUE                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_INT32, 2                        }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_INT64, 0,           0x7FFFFFFFU }, { V_INT64, 0,           0x7FFFFFFFU } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, ~0U,         ~0U         }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_FLOAT, 0,           0x80000000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_FLOAT, 0,           0x40000000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_INT32, 2                        }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 2                        }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 1,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_STR1,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_TRUE                              }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyy"       } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_VOID                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FALSE                           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_TRUE                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 1                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_EQ,     { V_FALSE                             }, { { V_OBJECT                          }, { V_ARRAY, 0                        } } END

    /*========================================================================*/
    /* CMP.NE */
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_TRUE                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_INT32, 2                        }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_INT64, 0,           0x7FFFFFFFU }, { V_INT64, 0,           0x7FFFFFFFU } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, ~0U,         ~0U         }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_FLOAT, 0,           0x80000000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_FLOAT, 0,           0x40000000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_INT32, 2                        }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 2                        }, { V_INT32, 3                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 1,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_STR1,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_FALSE                             }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyy"       } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_VOID                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FALSE                           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_TRUE                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_STR1,  0, 0,        "0"         }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 1                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_ARRAY, 0                        }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_NE,     { V_TRUE                              }, { { V_OBJECT                          }, { V_ARRAY, 0                        } } END

    /*========================================================================*/
    /* CMP.LE */
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_TRUE                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_TRUE                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FALSE                           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT32, 2                        }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT64, 0,           0x7FFFFFFFU }, { V_INT64, 0,           0x7FFFFFFFU } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_FLOAT, ~0U,         ~0U         }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FLOAT, 0,           0x80000000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FLOAT, 0,           0x40000000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT32, 2                        }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT32, (uint32_t)-1             }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_INT32, 1                        }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_FLOAT, 1,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }, { V_STR1,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyy"       } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_STR0,  0, 0,        "xyy"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_VOID                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_VOID                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_FALSE                           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FALSE                           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FALSE                           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_TRUE                            }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_TRUE                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_TRUE                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_ARRAY, 1                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LE,     { V_FALSE                             }, { { V_OBJECT                          }, { V_TRUE                            } } END

    /*========================================================================*/
    /* CMP.LT */
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_VOID                            }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FALSE                           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_FALSE                           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_INT32, 2                        }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_INT64, 0,           0x7FFFFFFFU }, { V_INT64, 0,           0x7FFFFFFFU } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FLOAT, 0,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FLOAT, ~0U,         ~0U         }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FLOAT, 0,           0x80000000U }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FLOAT, 0,           0x40000000U }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_INT32, 2                        }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_INT32, (uint32_t)-1             }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_INT32, 1                        }, { V_INT32, (uint32_t)-1             } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FLOAT, 1,           0x40000000U }, { V_FLOAT, 0,           0x40000000U } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_STR1,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR0,  0, 0,        "xyz"       }, { V_STR1,  0, 0,        "xyy"       } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_STR0,  0, 0,        "xyy"       }, { V_STR1,  0, 0,        "xyz"       } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_VOID                            }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_VOID                            }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FALSE                           }, { V_VOID                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FALSE                           }, { V_INT32, 0                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_FALSE                           }, { V_FLOAT, 0,           0           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_FALSE                           }, { V_STR0,  0, 0,        ""          } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_FALSE                           }, { V_ARRAY, 0                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, ~0U,         ~0U         } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 1                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_INT32, 2                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_TRUE                            }, { V_FLOAT, 0,           0x3FF00000U } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_TRUE                            }, { V_STR0,  0, 0,        "0"         } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_TRUE                            }, { V_ARRAY, 1                        } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_TRUE                            }, { V_OBJECT                          } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_INT32, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_TRUE                              }, { { V_FLOAT, 0,           0           }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR0,  0, 0,        ""          }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_STR1,  0, 0,        "0"         }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_ARRAY, 1                        }, { V_FALSE                           } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_ARRAY, 0                        }, { V_TRUE                            } } END
    TEST_INSTR INSTR_CMP_LT,     { V_FALSE                             }, { { V_OBJECT                          }, { V_TRUE                            } } END

    /*========================================================================*/
    /* JUMP.COND */
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_VOID                            }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_FALSE                           }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_TRUE                            }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_INT32, 0                        }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_INT32, 1                        }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_INT64, 0,           0           }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_INT64, 0,           0x80000000U }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_FLOAT, 0,           0           }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_FALSE                             }, { { V_FLOAT, 0,           0x80000000U }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_FLOAT, 0,           0x3FF00000U }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_FLOAT, ~0U,         ~0U         }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_STR0,  0, 0,        ""          }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_STR0,  0, 0,        "0"         }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_ARRAY, 0                        }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_ARRAY, 1                        }                                        } END
    TEST_INSTR INSTR_JUMP_COND,  { V_TRUE                              }, { { V_OBJECT                          }                                        } END

    /*========================================================================*/
    /* JUMP.NOT.COND */
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_VOID                            }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_FALSE                           }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_TRUE                            }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_INT32, 0                        }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_INT32, 1                        }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_INT64, 0,           0           }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_INT64, 0,           0x80000000U }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_FLOAT, 0,           0           }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_TRUE                           }, { { V_FLOAT, 0,           0x80000000U }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_FLOAT, 0,           0x3FF00000U }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_FLOAT, ~0U,         ~0U         }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_STR0,  0, 0,        ""          }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_STR0,  0, 0,        "0"         }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_ARRAY, 0                        }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_ARRAY, 1                        }                                        } END
    TEST_INSTR INSTR_JUMP_NOT_COND, { V_FALSE                          }, { { V_OBJECT                          }                                        } END

    KOS_context_destroy(&ctx);

    return 0;
}

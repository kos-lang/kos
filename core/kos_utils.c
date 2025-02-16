/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_utils.h"
#include "../inc/kos_array.h"
#include "../inc/kos_buffer.h"
#include "../inc/kos_constants.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_error.h"
#include "../inc/kos_memory.h"
#include "../inc/kos_module.h"
#include "../inc/kos_object.h"
#include "../inc/kos_string.h"
#include "../inc/kos_utf8.h"
#include "kos_math.h"
#include "kos_misc.h"
#include "kos_object_internal.h"
#include "kos_try.h"
#include <assert.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   pragma warning( push )
#   pragma warning( disable : 4255 4668 )
#   include <windows.h>
#   pragma warning( pop )
#   pragma warning( disable : 4996 ) /* strerror: This function or variable may be unsafe */
#endif

static const char str_array_close[]        = "]";
static const char str_array_comma[]        = ", ";
static const char str_array_open[]         = "[";
static const char str_buffer_close[]       = ">";
static const char str_buffer_open[]        = "<";
static const char str_class_open[]         = "<class ";
static const char str_empty_array[]        = "[]";
static const char str_empty_buffer[]       = "<>";
static const char str_err_cannot_expand[]  = "cannot expand object";
static const char str_err_invalid_string[] = "invalid string";
static const char str_err_not_number[]     = "object is not a number";
static const char str_function_open[]      = "<function ";
static const char str_module_open[]        = "<module ";
static const char str_module_close[]       = ">";
static const char str_object_close[]       = "}";
static const char str_object_colon[]       = ": ";
static const char str_object_open[]        = "{";
static const char str_object_sep[]         = ", ";
static const char str_recursive_array[]    = "[...]";
static const char str_recursive_object[]   = "{...}";
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_array,     "object is not an array");
KOS_DECLARE_STATIC_CONST_STRING(str_err_not_generator, "function is not a generator");

static const int8_t extra_len_map[256] = {
    /* 0 .. 127 */
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
    0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* <- " */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  /* <- backslash */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,
    /* 128 .. 191 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    /* 192 .. 223 */
    3,  3,  3,  3,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    /* 224 .. 239 */
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    /* 240 .. 247 */
    8,  8,  8,  8,  8,  8,  8,  8,
    /* 148 .. 255 */
    3,  3,  3,  3,  3,  3,  3,  3
};

static const char hex_digits[] = "0123456789abcdef";

const char *KOS_get_type_name(KOS_TYPE type)
{
    static const char *const type_names[] = {
        "integer",
        "integer",
        "float",
        "void",
        "boolean",
        "string",
        "object",
        "array",
        "buffer",
        "function",
        "class",
        "module"
    };

    assert(type <= OBJ_LAST_TYPE);

    return type_names[(int)type >> 1];
}

KOS_NUMERIC KOS_get_numeric(KOS_OBJ_ID obj_id)
{
    KOS_NUMERIC numeric;

    assert( ! IS_BAD_PTR(obj_id));

    if (IS_SMALL_INT(obj_id)) {
        numeric.type = KOS_INTEGER_VALUE;
        numeric.u.i  = GET_SMALL_INT(obj_id);
    }
    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            numeric.type = KOS_INTEGER_VALUE;
            numeric.u.i  = OBJPTR(INTEGER, obj_id)->value;
            break;

        case OBJ_FLOAT:
            numeric.type = KOS_FLOAT_VALUE;
            numeric.u.d  = OBJPTR(FLOAT, obj_id)->value;
            break;

        default:
            numeric.type = KOS_NON_NUMERIC;
            numeric.u.i  = 0;
            break;
    }

    return numeric;
}

int KOS_get_numeric_arg(KOS_CONTEXT  ctx,
                        KOS_OBJ_ID   args_obj,
                        int          idx,
                        KOS_NUMERIC *numeric)
{
    KOS_OBJ_ID arg;
    int        error = KOS_SUCCESS;

    assert(GET_OBJ_TYPE(args_obj) == OBJ_ARRAY);
    assert(idx < (int)KOS_get_array_size(args_obj));

    arg = KOS_array_read(ctx, args_obj, idx);
    TRY_OBJID(arg);

    *numeric = KOS_get_numeric(arg);

    if (numeric->type == KOS_NON_NUMERIC)
        RAISE_EXCEPTION(str_err_not_number);

cleanup:
    return error;
}

int KOS_get_integer(KOS_CONTEXT ctx,
                    KOS_OBJ_ID  obj_id,
                    int64_t    *ret)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj_id));

    if (IS_SMALL_INT(obj_id))
        *ret = GET_SMALL_INT(obj_id);

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            *ret = OBJPTR(INTEGER, obj_id)->value;
            break;

        case OBJ_FLOAT: {
            const double number = OBJPTR(FLOAT, obj_id)->value;
            if (number <= -9223372036854775808.0 || number >= 9223372036854775808.0) {
                KOS_raise_printf(ctx, "number %f is out of range for conversion to integer", number);
                error = KOS_ERROR_EXCEPTION;
            }
            else
                *ret = (int64_t)floor(number);
            break;

        }

        default:
            KOS_raise_printf(ctx, "unable to convert %s to integer", KOS_get_type_name(READ_OBJ_TYPE(obj_id)));
            error = KOS_ERROR_EXCEPTION;
            break;
    }

    return error;
}

int64_t KOS_fix_index(int64_t idx, unsigned length)
{
    if (idx < 0)
        idx += length;

    if (idx < 0)
        idx = 0;
    else if (idx > (int64_t)length)
        idx = length;

    return idx;
}

int KOS_get_index_arg(KOS_CONTEXT           ctx,
                      KOS_OBJ_ID            args_obj,
                      int                   arg_idx,
                      int                   begin_pos,
                      int                   end_pos,
                      enum KOS_VOID_INDEX_E void_index,
                      int                  *found_pos)
{
    int64_t          ival;
    const KOS_OBJ_ID val_id = KOS_array_read(ctx, args_obj, arg_idx);
    int              error  = KOS_SUCCESS;

    TRY_OBJID(val_id);

    assert(end_pos >= 0);

    if (val_id == KOS_VOID) {
        switch (void_index) {
            case KOS_VOID_INDEX_IS_BEGIN:
                *found_pos = begin_pos;
                return KOS_SUCCESS;

            case KOS_VOID_INDEX_IS_END:
                *found_pos = end_pos;
                return KOS_SUCCESS;

            default:
                break;
        }
    }

    TRY(KOS_get_integer(ctx, val_id, &ival));

    if (ival < 0)
        ival += end_pos;
    if (ival < begin_pos)
        ival = begin_pos;
    else if (ival > end_pos)
        ival = end_pos;

    *found_pos = (int)ival;

cleanup:
    return error;
}

static KOS_OBJ_ID get_exception_string(KOS_CONTEXT ctx,
                                       KOS_OBJ_ID  exception)
{
    if (GET_OBJ_TYPE(exception) == OBJ_OBJECT) {

        const KOS_OBJ_ID proto = KOS_get_prototype(ctx, exception);

        if (proto == ctx->inst->prototypes.exception_proto) {

            const KOS_OBJ_ID obj_id = KOS_get_property(ctx, exception, KOS_STR_VALUE);

            if (IS_BAD_PTR(obj_id))
                KOS_clear_exception(ctx);
            else if (GET_OBJ_TYPE(obj_id) == OBJ_STRING)
                return obj_id;
        }
    }

    return KOS_BADPTR;
}

void KOS_print_exception(KOS_CONTEXT ctx, enum KOS_PRINT_WHERE_E print_where)
{
    KOS_VECTOR cstr;
    KOS_LOCAL  exception;
    KOS_LOCAL  last_exception;
    FILE      *dest = print_where == KOS_STDERR ? stderr : stdout;

#ifdef CONFIG_FUZZ
    dest = fopen("/dev/null", "r+");
#endif

    KOS_vector_init(&cstr);

    KOS_init_locals(ctx, &exception, &last_exception, kos_end_locals);

    exception.o = KOS_get_exception(ctx);
    assert(!IS_BAD_PTR(exception.o));

    KOS_clear_exception(ctx);

    if (GET_OBJ_TYPE(exception.o) == OBJ_STRING) {
        if (KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, exception.o, &cstr))
            fprintf(dest, "%s\n", cstr.buffer);
    }
    else {
        KOS_OBJ_ID formatted;

        formatted = KOS_format_exception(ctx, exception.o);

        if (IS_BAD_PTR(formatted)) {
            KOS_OBJ_ID str;

            KOS_clear_exception(ctx);

            str = KOS_object_to_string(ctx, exception.o);

            if (IS_BAD_PTR(str)) {

                last_exception.o = KOS_get_exception(ctx);

                KOS_clear_exception(ctx);

                str = get_exception_string(ctx, exception.o);

                if (IS_BAD_PTR(str))
                    KOS_raise_exception(ctx, last_exception.o);
            }

            if ( ! IS_BAD_PTR(str) && KOS_SUCCESS == KOS_string_to_cstr_vec(ctx, str, &cstr))
                fprintf(dest, "%s\n", cstr.buffer);
        }
        else {
            uint32_t i;
            uint32_t lines;

            exception.o = formatted;

            assert(GET_OBJ_TYPE(exception.o) == OBJ_ARRAY);

            lines = KOS_get_array_size(exception.o);

            for (i = 0; i < lines; i++) {
                KOS_OBJ_ID line = KOS_array_read(ctx, exception.o, (int)i);

                if (KOS_is_exception_pending(ctx) || KOS_string_to_cstr_vec(ctx, line, &cstr))
                    break;
                fprintf(dest, "%s\n", cstr.buffer);
            }
        }
    }

    KOS_destroy_top_locals(ctx, &exception, &last_exception);

    KOS_vector_destroy(&cstr);

    if (KOS_is_exception_pending(ctx)) {
        fprintf(dest, "Exception: <unable to format>\n");

        if (print_where == KOS_STDERR)
            KOS_clear_exception(ctx);
    }

#ifdef CONFIG_FUZZ
    fclose(dest);
#endif
}

KOS_OBJ_ID KOS_get_file_name(KOS_CONTEXT ctx,
                             KOS_OBJ_ID  full_path)
{
    unsigned i;
    unsigned len;

    assert(GET_OBJ_TYPE(full_path) == OBJ_STRING);

    len = KOS_get_string_length(full_path);
    for (i = len; i > 0; i--) {
        const unsigned c = KOS_string_get_char_code(ctx, full_path, (int)i - 1);
        if (c == ~0U)
            return KOS_BADPTR;
        if (c == '/' || c == '\\')
            break;
    }

    if (i == len)
        i = 0;

    return KOS_string_slice(ctx, full_path, i, len);
}

static int int_to_str(KOS_CONTEXT ctx,
                      int64_t     value,
                      KOS_OBJ_ID *str,
                      KOS_VECTOR *cstr_vec)
{
    int      error = KOS_SUCCESS;
    uint8_t  buf[64];
    char    *ptr   = (char *)buf;
    unsigned len;

    if (cstr_vec) {
        TRY(KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    len = (unsigned)snprintf(ptr, sizeof(buf), "%" PRId64, value);

    if (cstr_vec) {
        TRY(KOS_vector_resize(cstr_vec,
                              cstr_vec->size + KOS_min(len, (unsigned)(sizeof(buf) - 1)) +
                                  (cstr_vec->size ? 0 : 1)));
    }
    else if (str) {
        KOS_OBJ_ID ret = KOS_new_cstring(ctx, ptr);
        TRY_OBJID(ret);
        *str = ret;
    }

cleanup:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int float_to_str(KOS_CONTEXT ctx,
                        double      value,
                        KOS_OBJ_ID *str,
                        KOS_VECTOR *cstr_vec)
{
    int      error = KOS_SUCCESS;
    uint8_t  buf[32];
    char    *ptr   = (char *)buf;
    unsigned size;

    if (cstr_vec) {
        TRY(KOS_vector_reserve(cstr_vec, cstr_vec->size + sizeof(buf)));
        ptr = &cstr_vec->buffer[cstr_vec->size ? cstr_vec->size - 1 : 0];
    }

    size = kos_print_float(ptr, sizeof(buf), value);

    if (cstr_vec) {
        TRY(KOS_vector_resize(cstr_vec, cstr_vec->size + size +
                    (cstr_vec->size ? 0 : 1)));
    }
    else if (str) {
        KOS_OBJ_ID ret = KOS_new_string(ctx, ptr, size);
        TRY_OBJID(ret);
        *str = ret;
    }

cleanup:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

static int vector_append_str(KOS_CONTEXT   ctx,
                             KOS_VECTOR   *cstr_vec,
                             KOS_OBJ_ID    obj,
                             KOS_QUOTE_STR quote_str)
{
    unsigned str_len = 0;
    size_t   pos     = cstr_vec->size;
    int      error   = KOS_SUCCESS;

    if (pos)
        --pos;

    if (KOS_get_string_length(obj) > 0) {

        str_len = KOS_string_to_utf8(obj, KOS_NULL, 0);
        assert(str_len > 0);

        if (str_len == ~0U) {
            KOS_raise_exception_cstring(ctx, str_err_invalid_string);
            return KOS_ERROR_EXCEPTION;
        }
    }

    if ( ! str_len && ! quote_str)
        return KOS_SUCCESS;

    error = KOS_vector_resize(cstr_vec, pos + str_len + 1 + (quote_str ? 2 : 0));

    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        return KOS_ERROR_EXCEPTION;
    }

    if (quote_str) {
        cstr_vec->buffer[pos] = '"';
        ++pos;
    }

    if (str_len)
        KOS_string_to_utf8(obj, &cstr_vec->buffer[pos], str_len);

    if (quote_str) {

        unsigned i;
        unsigned extra_len = 0;

        for (i = 0; i < str_len; i++) {

            const uint8_t c = (uint8_t)cstr_vec->buffer[pos + i];

            extra_len += extra_len_map[c];
        }

        if (extra_len) {

            char    *src;
            char    *dst;
            unsigned num_utf8_cont = 0;

            error = KOS_vector_resize(cstr_vec, cstr_vec->size + extra_len);

            if (error) {
                KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                return KOS_ERROR_EXCEPTION;
            }

            src = &cstr_vec->buffer[pos + str_len];
            dst = src + extra_len;

            while (src < dst) {

                const uint8_t c = (uint8_t)*(--src);

                const int esc_len = extra_len_map[c];

                switch (esc_len) {

                    case -1:
                        ++num_utf8_cont;
                        break;

                    case 0:
                        *(--dst) = (char)c;
                        break;

                    case 1:
                        *(--dst) = (char)c;
                        *(--dst) = '\\';
                        break;

                    case 3: {

                        uint32_t code[1];
                        int      lo;
                        int      hi;

                        code[0] = c;

                        if (num_utf8_cont) {
                            assert(num_utf8_cont == 1);

                            KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code[0]);

                            num_utf8_cont = 0;
                        }
                        else
                            code[0] = c;

                        lo = (int)code[0] & 0xF;
                        hi = (int)code[0] >> 4;

                        dst   -= 4;
                        *dst   = '\\';
                        dst[1] = 'x';
                        dst[2] = hex_digits[hi];
                        dst[3] = hex_digits[lo];

                        break;
                    }

                    default: {
                        uint32_t code[1];

                        assert(num_utf8_cont < 5);

                        KOS_utf8_decode_32(src, num_utf8_cont + 1U, KOS_UTF8_NO_ESCAPE, &code[0]);

                        *(--dst) = '}';

                        for (i = (unsigned)esc_len - 3U; i > 0; i--) {
                            *(--dst) = hex_digits[code[0] & 0xFU];
                            code[0]   >>= 4;
                        }

                        dst   -= 3;
                        *dst   = '\\';
                        dst[1] = 'x';
                        dst[2] = '{';

                        num_utf8_cont = 0;
                        break;
                    }
                }
            }

            pos += extra_len;
        }
    }

    pos += str_len;

    if (quote_str) {
        cstr_vec->buffer[pos] = '"';
        ++pos;
    }

    cstr_vec->buffer[pos] = 0;

    return KOS_SUCCESS;
}

static int make_quoted_str(KOS_CONTEXT ctx,
                           KOS_OBJ_ID  obj_id,
                           KOS_OBJ_ID *str,
                           KOS_VECTOR *cstr_vec)
{
    int    error;
    size_t old_size;

    assert(cstr_vec);

    old_size = cstr_vec->size;

    error = vector_append_str(ctx, cstr_vec, obj_id, KOS_QUOTE_STRINGS);

    if ( ! error) {
        const char  *buf     = &cstr_vec->buffer[old_size ? old_size - 1 : 0];
        const size_t size    = cstr_vec->size - old_size - (old_size ? 0 : 1);
        KOS_OBJ_ID   new_str = KOS_new_string(ctx, buf, (unsigned)size);
        if (IS_BAD_PTR(new_str))
            error = KOS_ERROR_EXCEPTION;
        else
            *str = new_str;
    }

    cstr_vec->size = old_size;

    if (old_size)
        cstr_vec->buffer[old_size - 1] = 0;

    return error;
}

/* Used when coverting objects to strings to avoid infinite recursion */
typedef struct KOS_STR_REC_GUARD_S {
    struct KOS_STR_REC_GUARD_S *next;
    KOS_OBJ_ID                 *obj_id_ptr;
} KOS_STR_REC_GUARD;

static int is_to_string_recursive(KOS_STR_REC_GUARD *guard, KOS_OBJ_ID obj_id)
{
    while (guard) {

        if (*guard->obj_id_ptr == obj_id)
            return 1;

        guard = guard->next;
    }

    return 0;
}

static int object_to_string_or_cstr_vec(KOS_CONTEXT        ctx,
                                        KOS_OBJ_ID         obj_id,
                                        KOS_QUOTE_STR      quote_str,
                                        KOS_OBJ_ID        *str,
                                        KOS_VECTOR        *cstr_vec,
                                        KOS_STR_REC_GUARD *guard);

static int vector_append_array(KOS_CONTEXT        ctx,
                               KOS_VECTOR        *cstr_vec,
                               KOS_OBJ_ID         array_obj,
                               KOS_STR_REC_GUARD *guard)
{
    int               error;
    uint32_t          length;
    uint32_t          i;
    KOS_STR_REC_GUARD new_guard;
    KOS_LOCAL         array;

    KOS_init_local_with(ctx, &array, array_obj);

    assert(GET_OBJ_TYPE(array.o) == OBJ_ARRAY);

    new_guard.next       = guard;
    new_guard.obj_id_ptr = &array.o;

    length = KOS_get_array_size(array.o);

    TRY(KOS_append_cstr(ctx, cstr_vec, str_array_open, sizeof(str_array_open)-1));

    for (i = 0; i < length; ) {

        KOS_OBJ_ID val_id = KOS_array_read(ctx, array.o, i);
        TRY_OBJID(val_id);

        if (is_to_string_recursive(&new_guard, val_id)) {
            if (GET_OBJ_TYPE(val_id) == OBJ_ARRAY)
                TRY(KOS_append_cstr(ctx, cstr_vec,
                                    str_recursive_array, sizeof(str_recursive_array)-1));
            else {
                assert(GET_OBJ_TYPE(val_id) == OBJ_OBJECT);
                TRY(KOS_append_cstr(ctx, cstr_vec,
                                    str_recursive_object, sizeof(str_recursive_object)-1));
            }
        }
        else
            TRY(object_to_string_or_cstr_vec(ctx, val_id, KOS_QUOTE_STRINGS,
                                             KOS_NULL, cstr_vec, &new_guard));

        ++i;

        if (i < length)
            TRY(KOS_append_cstr(ctx, cstr_vec, str_array_comma, sizeof(str_array_comma)-1));
    }

    TRY(KOS_append_cstr(ctx, cstr_vec, str_array_close, sizeof(str_array_close)-1));

cleanup:
    KOS_destroy_top_local(ctx, &array);

    return error;
}

static KOS_OBJ_ID array_to_str(KOS_CONTEXT        ctx,
                               KOS_OBJ_ID         array_obj,
                               KOS_STR_REC_GUARD *guard)
{
    int               error;
    uint32_t          length;
    uint32_t          i;
    uint32_t          i_out;
    KOS_OBJ_ID        ret          = KOS_BADPTR;
    KOS_OBJ_ID        str;
    KOS_LOCAL         array;
    KOS_LOCAL         str_comma;
    KOS_LOCAL         aux_array;
    KOS_LOCAL         value;
    KOS_STR_REC_GUARD new_guard;

    assert(GET_OBJ_TYPE(array_obj) == OBJ_ARRAY);

    length = KOS_get_array_size(array_obj);

    if (length == 0)
        return KOS_new_const_ascii_string(ctx, str_empty_array,
                                          sizeof(str_empty_array) - 1);

    KOS_init_locals(ctx, &array, &str_comma, &aux_array, &value, kos_end_locals);

    array.o = array_obj;

    new_guard.next       = guard;
    new_guard.obj_id_ptr = &array.o;

    aux_array.o = KOS_new_array(ctx, length * 4 + 1);
    TRY_OBJID(aux_array.o);

    str = KOS_new_const_ascii_string(ctx, str_array_open,
                                     sizeof(str_array_open) - 1);
    TRY_OBJID(str);
    TRY(KOS_array_write(ctx, aux_array.o, 0, str));

    i_out = 1;

    for (i = 0; i < length; ++i) {

        value.o = KOS_array_read(ctx, array.o, i);
        TRY_OBJID(value.o);

        if (GET_OBJ_TYPE(value.o) == OBJ_STRING) {

            KOS_DECLARE_STATIC_CONST_STRING(str_quote, "\"");

            TRY(KOS_array_write(ctx, aux_array.o, i_out,     KOS_CONST_ID(str_quote)));
            TRY(KOS_array_write(ctx, aux_array.o, i_out + 1, value.o));
            TRY(KOS_array_write(ctx, aux_array.o, i_out + 2, KOS_CONST_ID(str_quote)));

            i_out += 3;
        }
        else {

            if (is_to_string_recursive(&new_guard, value.o)) {
                const KOS_TYPE type = GET_OBJ_TYPE(value.o);

                KOS_DECLARE_STATIC_CONST_STRING(str_recursive_array_obj,  "[...]");
                KOS_DECLARE_STATIC_CONST_STRING(str_recursive_object_obj, "{...}");

                assert(type == OBJ_ARRAY || type == OBJ_OBJECT);

                value.o = type == OBJ_ARRAY ? KOS_CONST_ID(str_recursive_array_obj)
                                            : KOS_CONST_ID(str_recursive_object_obj);
            }
            else
                TRY(object_to_string_or_cstr_vec(ctx, value.o, KOS_QUOTE_STRINGS,
                                                 &value.o, KOS_NULL, &new_guard));

            TRY(KOS_array_write(ctx, aux_array.o, i_out, value.o));

            ++i_out;
        }

        if (i + 1 < length) {
            if (str_comma.o == KOS_BADPTR) {
                str_comma.o = KOS_new_const_ascii_string(ctx, str_array_comma,
                                                         sizeof(str_array_comma) - 1);
                TRY_OBJID(str_comma.o);
            }
            TRY(KOS_array_write(ctx, aux_array.o, i_out, str_comma.o));
            ++i_out;
        }
    }

    str = KOS_new_const_ascii_string(ctx, str_array_close,
                                     sizeof(str_array_close) - 1);
    TRY_OBJID(str);
    TRY(KOS_array_write(ctx, aux_array.o, i_out, str));
    ++i_out;
    TRY(KOS_array_resize(ctx, aux_array.o, i_out));

    ret = KOS_string_add(ctx, aux_array.o);

cleanup:
    KOS_destroy_top_locals(ctx, &array, &value);

    return error ? KOS_BADPTR : ret;
}

static int vector_append_buffer(KOS_CONTEXT ctx,
                                KOS_VECTOR *cstr_vec,
                                KOS_OBJ_ID  obj_id)
{
    int            error;
    uint32_t       size;
    const uint8_t *src;
    const uint8_t *end;
    char          *dest;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);

    size = KOS_get_buffer_size(obj_id);

    error = KOS_vector_reserve(cstr_vec, cstr_vec->size + size * 3 + 2);
    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        RAISE_ERROR(KOS_ERROR_EXCEPTION);
    }
    assert(sizeof(str_buffer_open)  == 2);
    assert(sizeof(str_buffer_close) == 2);

    TRY(KOS_append_cstr(ctx, cstr_vec, str_buffer_open, sizeof(str_buffer_open)-1));

    dest = cstr_vec->buffer + cstr_vec->size - 1;
    src  = KOS_buffer_data_const(obj_id);
    end  = src + size;

    while (src < end) {

        const uint8_t b = *(src++);

        dest[0] = hex_digits[b >> 4];
        dest[1] = hex_digits[b & 15];
        dest[2] = ' ';

        dest += 3;
    }

    if (size) {
        *(--dest) = 0;
        cstr_vec->size += size * 3 - 1;
    }

    TRY(KOS_append_cstr(ctx, cstr_vec, str_buffer_close, sizeof(str_buffer_close)-1));

cleanup:
    return error;
}

static KOS_OBJ_ID buffer_to_str(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  obj_id)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    KOS_VECTOR cstr_vec;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);

    if (KOS_get_buffer_size(obj_id) == 0)
        return KOS_new_const_ascii_string(ctx, str_empty_buffer,
                                          sizeof(str_empty_buffer) - 1);

    KOS_vector_init(&cstr_vec);

    TRY(vector_append_buffer(ctx, &cstr_vec, obj_id));

    assert(cstr_vec.size > 1);

    ret = KOS_new_string(ctx, cstr_vec.buffer, (unsigned)cstr_vec.size - 1U);

cleanup:
    KOS_vector_destroy(&cstr_vec);

    return error ? KOS_BADPTR : ret;
}

static int vector_append_object(KOS_CONTEXT        ctx,
                                KOS_VECTOR        *cstr_vec,
                                KOS_OBJ_ID         obj_id,
                                KOS_STR_REC_GUARD *guard)
{
    int               error     = KOS_SUCCESS;
    uint32_t          num_elems = 0;
    KOS_STR_REC_GUARD new_guard;
    KOS_LOCAL         obj;
    KOS_LOCAL         walk;
    KOS_LOCAL         value;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);

    KOS_init_locals(ctx, &obj, &walk, &value, kos_end_locals);

    obj.o = obj_id;

    new_guard.next       = guard;
    new_guard.obj_id_ptr = &obj.o;

    walk.o = kos_new_object_walk(ctx, obj.o, KOS_SHALLOW);
    TRY_OBJID(walk.o);

    TRY(KOS_append_cstr(ctx, cstr_vec, str_object_open, sizeof(str_object_open)-1));

    while ( ! kos_object_walk(ctx, walk.o)) {

        assert(GET_OBJ_TYPE(KOS_get_walk_key(walk.o)) == OBJ_STRING);
        assert( ! IS_BAD_PTR(KOS_get_walk_value(walk.o)));

        if (num_elems)
            TRY(KOS_append_cstr(ctx, cstr_vec, str_object_sep, sizeof(str_object_sep)-1));

        TRY(vector_append_str(ctx, cstr_vec, KOS_get_walk_key(walk.o), KOS_QUOTE_STRINGS));

        TRY(KOS_append_cstr(ctx, cstr_vec, str_object_colon, sizeof(str_object_colon)-1));

        value.o = KOS_get_walk_value(walk.o);
        assert( ! IS_BAD_PTR(value.o));

        if (GET_OBJ_TYPE(value.o) == OBJ_DYNAMIC_PROP) {

            KOS_OBJ_ID actual = KOS_call_function(ctx,
                                                  OBJPTR(DYNAMIC_PROP, value.o)->getter,
                                                  OBJPTR(ITERATOR, walk.o)->obj,
                                                  KOS_EMPTY_ARRAY);
            if (IS_BAD_PTR(actual)) {
                assert(KOS_is_exception_pending(ctx));
                KOS_clear_exception(ctx);

                value.o = OBJPTR(DYNAMIC_PROP, value.o)->getter;
            }
            else
                value.o = actual;
        }

        if (is_to_string_recursive(&new_guard, value.o)) {
            if (GET_OBJ_TYPE(value.o) == OBJ_ARRAY)
                TRY(KOS_append_cstr(ctx, cstr_vec,
                                    str_recursive_array, sizeof(str_recursive_array)-1));
            else
                TRY(KOS_append_cstr(ctx, cstr_vec,
                                    str_recursive_object, sizeof(str_recursive_object)-1));
        }
        else
            TRY(object_to_string_or_cstr_vec(ctx, value.o, KOS_QUOTE_STRINGS,
                                             KOS_NULL, cstr_vec, &new_guard));

        ++num_elems;
    }

    TRY(KOS_append_cstr(ctx, cstr_vec, str_object_close, sizeof(str_object_close)-1));

cleanup:
    KOS_destroy_top_locals(ctx, &obj, &value);

    return error;
}

static KOS_OBJ_ID object_to_str(KOS_CONTEXT        ctx,
                                KOS_OBJ_ID         obj_id,
                                KOS_STR_REC_GUARD *guard)
{
    int        error = KOS_SUCCESS;
    KOS_OBJ_ID ret   = KOS_BADPTR;
    KOS_VECTOR cstr_vec;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_OBJECT);

    KOS_vector_init(&cstr_vec);

    TRY(vector_append_object(ctx, &cstr_vec, obj_id, guard));

    assert(cstr_vec.size > 1);

    ret = KOS_new_string(ctx, cstr_vec.buffer, (unsigned)cstr_vec.size - 1U);

cleanup:
    KOS_vector_destroy(&cstr_vec);

    return error ? KOS_BADPTR : ret;
}

static int vector_append_function(KOS_CONTEXT ctx,
                                  KOS_VECTOR *cstr_vec,
                                  KOS_OBJ_ID  obj_id)
{
    int           error = KOS_SUCCESS;
    KOS_FUNCTION *func;
    char          cstr_ptr[22];
    unsigned      len;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION ||
           GET_OBJ_TYPE(obj_id) == OBJ_CLASS);

    func = OBJPTR(FUNCTION, obj_id);

    if (GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION)
        TRY(KOS_append_cstr(ctx, cstr_vec, str_function_open, sizeof(str_function_open) - 1));
    else
        TRY(KOS_append_cstr(ctx, cstr_vec, str_class_open, sizeof(str_class_open) - 1));

    TRY(vector_append_str(ctx, cstr_vec, func->name, KOS_DONT_QUOTE));

    if (func->handler)
        len = (unsigned)snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%" PRIX64 ">",
                                 (uint64_t)(uintptr_t)func->handler);
    else
        len = (unsigned)snprintf(cstr_ptr, sizeof(cstr_ptr), " @ %u>",
                                 KOS_function_get_def_line(obj_id));

    TRY(KOS_append_cstr(ctx, cstr_vec, cstr_ptr, KOS_min(len, (unsigned)(sizeof(cstr_ptr) - 1))));

cleanup:
    return error;
}

static KOS_OBJ_ID function_to_str(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id)
{
    int           error = KOS_SUCCESS;
    KOS_OBJ_ID    ret   = KOS_BADPTR;
    KOS_LOCAL     strings[3];
    KOS_LOCAL     func;
    char          cstr_ptr[15];

    assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION ||
           GET_OBJ_TYPE(obj_id) == OBJ_CLASS);

    KOS_init_locals(ctx, &strings[1], &strings[2], &func, kos_end_locals);

    func.o = obj_id;

    if (GET_OBJ_TYPE(func.o) == OBJ_FUNCTION) {
        KOS_DECLARE_STATIC_CONST_STRING(str_open_function, "<function ");

        strings[0].o = KOS_CONST_ID(str_open_function);
    }
    else {
        KOS_DECLARE_STATIC_CONST_STRING(str_open_class, "<class ");

        strings[0].o = KOS_CONST_ID(str_open_class);
    }

    strings[1].o = OBJPTR(FUNCTION, func.o)->name;

    if (OBJPTR(FUNCTION, func.o)->handler)
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ 0x%" PRIx64 ">",
                 (uint64_t)(uintptr_t)OBJPTR(FUNCTION, func.o)->handler);
    else
        snprintf(cstr_ptr, sizeof(cstr_ptr), " @ %u>",
                 KOS_function_get_def_line(func.o));

    strings[2].o = KOS_new_cstring(ctx, cstr_ptr);
    TRY_OBJID(strings[2].o);

    ret = KOS_string_add_n(ctx, strings, sizeof(strings) / sizeof(strings[0]));

cleanup:
    KOS_destroy_top_locals(ctx, &strings[1], &func);

    return error ? KOS_BADPTR : ret;
}

static int vector_append_module(KOS_CONTEXT ctx,
                                KOS_VECTOR *cstr_vec,
                                KOS_OBJ_ID  obj_id)
{
    int error = KOS_SUCCESS;

    assert(GET_OBJ_TYPE(obj_id) == OBJ_MODULE);

    TRY(KOS_append_cstr(ctx, cstr_vec, str_module_open, sizeof(str_module_open) - 1));

    TRY(vector_append_str(ctx, cstr_vec, OBJPTR(MODULE, obj_id)->name, KOS_DONT_QUOTE));

    TRY(KOS_append_cstr(ctx, cstr_vec, str_module_close, sizeof(str_module_close) - 1));

cleanup:
    return error;
}

static KOS_OBJ_ID module_to_str(KOS_CONTEXT ctx,
                                  KOS_OBJ_ID  obj_id)
{
    KOS_OBJ_ID ret = KOS_BADPTR;
    KOS_LOCAL  strings[3];

    KOS_DECLARE_STATIC_CONST_STRING(str_open_module,  "<module ");
    KOS_DECLARE_STATIC_CONST_STRING(str_close_module, ">");

    assert(GET_OBJ_TYPE(obj_id) == OBJ_MODULE);

    KOS_init_local(ctx, &strings[1]);

    strings[0].o = KOS_CONST_ID(str_open_module);
    strings[1].o = OBJPTR(MODULE, obj_id)->name;
    strings[2].o = KOS_CONST_ID(str_close_module);

    ret = KOS_string_add_n(ctx, strings, sizeof(strings) / sizeof(strings[0]));

    KOS_destroy_top_local(ctx, &strings[1]);

    return ret;
}

static int object_to_string_or_cstr_vec(KOS_CONTEXT        ctx,
                                        KOS_OBJ_ID         obj_id,
                                        KOS_QUOTE_STR      quote_str,
                                        KOS_OBJ_ID        *str,
                                        KOS_VECTOR        *cstr_vec,
                                        KOS_STR_REC_GUARD *guard)
{
    int error = KOS_SUCCESS;

    assert( ! IS_BAD_PTR(obj_id));
    assert(str || cstr_vec);
    assert( ! str || ! cstr_vec || quote_str);

    if (IS_SMALL_INT(obj_id))
        error = int_to_str(ctx, GET_SMALL_INT(obj_id), str, cstr_vec);

    else switch (READ_OBJ_TYPE(obj_id)) {

        case OBJ_INTEGER:
            error = int_to_str(ctx, OBJPTR(INTEGER, obj_id)->value, str, cstr_vec);
            break;

        case OBJ_FLOAT:
            error = float_to_str(ctx, OBJPTR(FLOAT, obj_id)->value, str, cstr_vec);
            break;

        case OBJ_STRING:
            if ( ! str)
                error = vector_append_str(ctx, cstr_vec, obj_id, quote_str);
            else if (quote_str)
                error = make_quoted_str(ctx, obj_id, str, cstr_vec);
            else
                *str = obj_id;
            break;

        case OBJ_VOID:
            /* fall through */
        default:
            assert(READ_OBJ_TYPE(obj_id) == OBJ_VOID);
            if (cstr_vec)
                error = KOS_append_cstr(ctx, cstr_vec, "void", 4);
            else if (str)
                *str = KOS_STR_VOID;
            break;

        case OBJ_BOOLEAN:
            if (KOS_get_bool(obj_id)) {
                if (cstr_vec)
                    error = KOS_append_cstr(ctx, cstr_vec, "true", 4);
                else if (str) {
                    KOS_DECLARE_STATIC_CONST_STRING(str_true, "true");
                    *str = KOS_CONST_ID(str_true);
                }
            }
            else {
                if (cstr_vec)
                    error = KOS_append_cstr(ctx, cstr_vec, "false", 5);
                else if (str) {
                    KOS_DECLARE_STATIC_CONST_STRING(str_false, "false");
                    *str = KOS_CONST_ID(str_false);
                }
            }
            break;

        case OBJ_ARRAY:
            if (cstr_vec)
                error = vector_append_array(ctx, cstr_vec, obj_id, guard);
            else if (str)
                *str = array_to_str(ctx, obj_id, guard);
            break;

        case OBJ_BUFFER:
            if (cstr_vec)
                error = vector_append_buffer(ctx, cstr_vec, obj_id);
            else if (str)
                *str = buffer_to_str(ctx, obj_id);
            break;

        case OBJ_OBJECT:
            if (cstr_vec)
                error = vector_append_object(ctx, cstr_vec, obj_id, guard);
            else if (str)
                *str = object_to_str(ctx, obj_id, guard);
            break;

        case OBJ_FUNCTION:
            /* fall through */
        case OBJ_CLASS:
            if (cstr_vec)
                error = vector_append_function(ctx, cstr_vec, obj_id);
            else if (str)
                *str = function_to_str(ctx, obj_id);
            break;

        case OBJ_MODULE:
            if (cstr_vec)
                error = vector_append_module(ctx, cstr_vec, obj_id);
            else if (str)
                *str = module_to_str(ctx, obj_id);
            break;
    }

    if ( ! error && str && IS_BAD_PTR(*str)) {
        error = KOS_ERROR_EXCEPTION;
        assert(KOS_is_exception_pending(ctx));
    }

    return error;
}

int KOS_object_to_string_or_cstr_vec(KOS_CONTEXT   ctx,
                                     KOS_OBJ_ID    obj_id,
                                     KOS_QUOTE_STR quote_str,
                                     KOS_OBJ_ID   *str,
                                     KOS_VECTOR   *cstr_vec)
{
    return object_to_string_or_cstr_vec(ctx, obj_id, quote_str, str, cstr_vec, KOS_NULL);
}

KOS_OBJ_ID KOS_object_to_string(KOS_CONTEXT ctx,
                                KOS_OBJ_ID  obj)
{
    KOS_OBJ_ID ret   = KOS_BADPTR;
    const int  error = object_to_string_or_cstr_vec(ctx, obj, KOS_DONT_QUOTE, &ret, KOS_NULL, KOS_NULL);

    return error ? KOS_BADPTR : ret;
}

int KOS_print_to_cstr_vec(KOS_CONTEXT   ctx,
                          KOS_OBJ_ID    array,
                          KOS_QUOTE_STR quote_str,
                          KOS_VECTOR   *cstr_vec,
                          const char   *sep,
                          unsigned      sep_len)
{
    int      error = KOS_SUCCESS;
    uint32_t len;
    uint32_t i;
    uint32_t first_sep_i;

    assert(cstr_vec);
    assert(GET_OBJ_TYPE(array) == OBJ_ARRAY);

    if (GET_OBJ_TYPE(array) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);

    first_sep_i = cstr_vec->size ? 0U : 1U;

    len = KOS_get_array_size(array);

    if (len)
        TRY(KOS_vector_reserve(cstr_vec, cstr_vec->size + 128U));

    for (i = 0; i < len; i++) {
        KOS_OBJ_ID obj = KOS_array_read(ctx, array, (int)i);
        TRY_OBJID(obj);

        if (i >= first_sep_i && sep_len) {
            const size_t pos = cstr_vec->size;
            TRY(KOS_vector_resize(cstr_vec, pos + sep_len + (pos ? 0 : 1)));
            memcpy(&cstr_vec->buffer[pos - (pos ? 1 : 0)], sep, sep_len + 1);
        }

        TRY(KOS_object_to_string_or_cstr_vec(ctx, obj, quote_str, KOS_NULL, cstr_vec));
    }

cleanup:
    if (error == KOS_ERROR_OUT_OF_MEMORY) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }

    return error;
}

int KOS_append_cstr(KOS_CONTEXT ctx,
                    KOS_VECTOR *cstr_vec,
                    const char *str,
                    size_t      len)
{
    const size_t pos   = cstr_vec->size;
    int          error = KOS_vector_resize(cstr_vec, pos + len + (pos ? 0 : 1));

    if (error) {
        KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
        error = KOS_ERROR_EXCEPTION;
    }
    else
        memcpy(&cstr_vec->buffer[pos ? pos - 1 : pos], str, len + 1);

    return error;
}

int KOS_array_push_expand(KOS_CONTEXT ctx,
                          KOS_OBJ_ID  array_obj,
                          KOS_OBJ_ID  value_obj)
{
    int       error  = KOS_SUCCESS;
    uint32_t  cur_size;
    KOS_LOCAL array;
    KOS_LOCAL value;

    KOS_init_locals(ctx, &array, &value, kos_end_locals);

    array.o = array_obj;
    value.o = value_obj;

    if (GET_OBJ_TYPE(array.o) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);

    cur_size = KOS_get_array_size(array.o);

    switch (GET_OBJ_TYPE(value.o)) {

        case OBJ_ARRAY:
            TRY(KOS_array_insert(ctx, array.o, cur_size, cur_size,
                                 value.o, 0, (int32_t)KOS_get_array_size(value.o)));
            break;

        case OBJ_STRING: {
            const uint32_t len = KOS_get_string_length(value.o);
            uint32_t       i;

            TRY(KOS_array_resize(ctx, array.o, cur_size + len));

            for (i = 0; i < len; i++) {
                KOS_OBJ_ID ch = KOS_string_get_char(ctx, value.o, (int)i);
                TRY_OBJID(ch);
                TRY(KOS_array_write(ctx, array.o, (int)(cur_size + i), ch));
            }
            break;
        }

        case OBJ_BUFFER: {
            const uint32_t size = KOS_get_buffer_size(value.o);
            uint32_t       i;
            const uint8_t *buf  = KOS_NULL;

            TRY(KOS_array_resize(ctx, array.o, cur_size + size));

            if (size) {
                buf = KOS_buffer_data_const(value.o);
                assert(buf);
            }

            for (i = 0; i < size; i++) {
                const KOS_OBJ_ID byte = TO_SMALL_INT((int)buf[i]);
                TRY(KOS_array_write(ctx, array.o, (int)(cur_size + i), byte));
            }
            break;
        }

        case OBJ_FUNCTION: {
            KOS_FUNCTION_STATE state;

            if ( ! KOS_is_generator(value.o, &state))
                RAISE_EXCEPTION(str_err_cannot_expand);

            if (state != KOS_GEN_DONE) {
                for (;;) {
                    KOS_OBJ_ID ret = KOS_call_generator(ctx, value.o, KOS_VOID, KOS_EMPTY_ARRAY);
                    if (IS_BAD_PTR(ret)) { /* end of iterator */
                        if (KOS_is_exception_pending(ctx))
                            error = KOS_ERROR_EXCEPTION;
                        break;
                    }
                    TRY(KOS_array_push(ctx, array.o, ret, KOS_NULL));
                }
            }
            break;
        }

        default:
            RAISE_EXCEPTION(str_err_cannot_expand);
    }

cleanup:
    KOS_destroy_top_locals(ctx, &array, &value);

    return error;
}

static KOS_COMPARE_RESULT compare_int(int64_t a, int64_t b)
{
    return a < b ? KOS_LESS_THAN    :
           a > b ? KOS_GREATER_THAN :
                   KOS_EQUAL;
}

static double get_float(KOS_OBJ_ID obj_id)
{
    if (IS_SMALL_INT(obj_id))
        return (double)GET_SMALL_INT(obj_id);
    else if (GET_OBJ_TYPE(obj_id) == OBJ_INTEGER)
        return (double)OBJPTR(INTEGER, obj_id)->value;
    else
        return OBJPTR(FLOAT, obj_id)->value;
}

static KOS_COMPARE_RESULT compare_float(KOS_OBJ_ID a, KOS_OBJ_ID b)
{
    const double a_float = get_float(a);
    const double b_float = get_float(b);

    if (a_float != a_float || b_float != b_float)
        return KOS_INDETERMINATE;
    else
        return a_float < b_float ? KOS_LESS_THAN    :
               a_float > b_float ? KOS_GREATER_THAN :
                                   KOS_EQUAL;
}

/* This is used when comparing arrays to prevent infinite recursion */
struct KOS_COMPARE_REF_S {
    KOS_OBJ_ID                a;
    KOS_OBJ_ID                b;
    struct KOS_COMPARE_REF_S *next;
};

static KOS_COMPARE_RESULT compare(KOS_OBJ_ID                a,
                                  KOS_OBJ_ID                b,
                                  struct KOS_COMPARE_REF_S *cmp_ref);

static KOS_COMPARE_RESULT compare_array(KOS_OBJ_ID                a,
                                        KOS_OBJ_ID                b,
                                        struct KOS_COMPARE_REF_S *cmp_ref)
{
    const uint32_t a_size   = KOS_get_array_size(a);
    const uint32_t b_size   = KOS_get_array_size(b);
    const uint32_t cmp_size = KOS_min(a_size, b_size);

    KOS_ATOMIC(KOS_OBJ_ID)       *a_buf    = a_size ? kos_get_array_buffer(OBJPTR(ARRAY, a)) : KOS_NULL;
    KOS_ATOMIC(KOS_OBJ_ID)       *b_buf    = b_size ? kos_get_array_buffer(OBJPTR(ARRAY, b)) : KOS_NULL;
    KOS_ATOMIC(KOS_OBJ_ID) *const a_end    = a_size ? a_buf + cmp_size : KOS_NULL;
    KOS_COMPARE_RESULT            cmp      = KOS_EQUAL;
    struct KOS_COMPARE_REF_S      this_ref;

    this_ref.a    = a;
    this_ref.b    = b;
    this_ref.next = cmp_ref;

    /* Check recursive arrays */
    while (cmp_ref) {
        const int aa = a == cmp_ref->a;
        const int bb = b == cmp_ref->b;
        const int ab = a == cmp_ref->b;
        const int ba = b == cmp_ref->a;

        if (aa & bb)
            return KOS_EQUAL;
        else if (aa | bb | ab | ba)
            return compare_int((int64_t)(intptr_t)a, (int64_t)(intptr_t)b);

        cmp_ref = cmp_ref->next;
    }

    for ( ; a_buf < a_end; ++a_buf, ++b_buf) {

        cmp = compare(KOS_atomic_read_relaxed_obj(*a_buf),
                       KOS_atomic_read_relaxed_obj(*b_buf),
                       &this_ref);

        if (cmp)
            break;
    }

    return cmp             ? cmp              :
           a_size < b_size ? KOS_LESS_THAN    :
           a_size > b_size ? KOS_GREATER_THAN :
                             KOS_EQUAL;
}

static KOS_COMPARE_RESULT compare_buf(KOS_OBJ_ID a, KOS_OBJ_ID b)
{
    const uint32_t a_size   = KOS_get_buffer_size(a);
    const uint32_t b_size   = KOS_get_buffer_size(b);
    const uint32_t cmp_size = KOS_min(a_size, b_size);
    const int      min_cmp  = memcmp(KOS_buffer_data_const(a),
                                     KOS_buffer_data_const(b),
                                     cmp_size);

    const int cmp = cmp_size ? min_cmp : 0;

    if (cmp)
        return cmp > 0 ? KOS_GREATER_THAN : KOS_LESS_THAN;
    else
        return a_size < b_size ? KOS_LESS_THAN    :
               a_size > b_size ? KOS_GREATER_THAN :
                                 KOS_EQUAL;
}

static KOS_COMPARE_RESULT compare(KOS_OBJ_ID                a,
                                  KOS_OBJ_ID                b,
                                  struct KOS_COMPARE_REF_S *cmp_ref)
{
    const KOS_TYPE a_type = GET_OBJ_TYPE(a);
    const KOS_TYPE b_type = GET_OBJ_TYPE(b);

    if (a == b) {
        if (a_type == OBJ_FLOAT) {
            const double value = OBJPTR(FLOAT, a)->value;
            return value == value ? KOS_EQUAL : KOS_INDETERMINATE;
        }
        else
            return KOS_EQUAL;
    }
    else if (a_type == b_type || (a_type <= OBJ_FLOAT && b_type <= OBJ_FLOAT)) {

        switch (a_type) {

            default:
                assert(a_type == OBJ_SMALL_INTEGER ||
                       a_type == OBJ_INTEGER       ||
                       a_type == OBJ_FLOAT);

                if (a_type == OBJ_FLOAT || b_type == OBJ_FLOAT)
                    return compare_float(a, b);
                else if (a_type == OBJ_SMALL_INTEGER && b_type == OBJ_SMALL_INTEGER)
                    return compare_int((int64_t)(intptr_t)a, (int64_t)(intptr_t)b);
                else {
                    const int64_t a_int = a_type == OBJ_SMALL_INTEGER
                                        ? GET_SMALL_INT(a)
                                        : OBJPTR(INTEGER, a)->value;
                    const int64_t b_int = b_type == OBJ_SMALL_INTEGER
                                        ? GET_SMALL_INT(b)
                                        : OBJPTR(INTEGER, b)->value;
                    return compare_int(a_int, b_int);
                }

            case OBJ_BOOLEAN:
                return compare_int((int)KOS_get_bool(a), (int)KOS_get_bool(b));

            case OBJ_STRING: {
                const int cmp = KOS_string_compare(a, b);
                return cmp < 0 ? KOS_LESS_THAN    :
                       cmp     ? KOS_GREATER_THAN :
                                 KOS_EQUAL;
            }

            case OBJ_OBJECT:
                return compare_int((int64_t)(intptr_t)a, (int64_t)(intptr_t)b);

            case OBJ_ARRAY:
                return compare_array(a, b, cmp_ref);

            case OBJ_BUFFER:
                return compare_buf(a, b);

            case OBJ_FUNCTION:
                /* fall through */
            case OBJ_CLASS:
                /* fall through */
            case OBJ_MODULE:
                return compare_int((int64_t)(intptr_t)a, (int64_t)(intptr_t)b);
        }
    }
    else
        return (a_type < b_type) ? KOS_LESS_THAN : KOS_GREATER_THAN;
}

KOS_COMPARE_RESULT KOS_compare(KOS_OBJ_ID a,
                               KOS_OBJ_ID b)
{
    return compare(a, b, KOS_NULL);
}

int KOS_is_generator(KOS_OBJ_ID fun_obj, KOS_FUNCTION_STATE *fun_state)
{
    KOS_FUNCTION_STATE state;

    assert(GET_OBJ_TYPE(fun_obj) == OBJ_FUNCTION || GET_OBJ_TYPE(fun_obj) == OBJ_CLASS);

    state = (KOS_FUNCTION_STATE)KOS_atomic_read_relaxed_u32(OBJPTR(FUNCTION, fun_obj)->state);

    if (fun_state)
        *fun_state = state;

    return state == KOS_GEN_READY || state == KOS_GEN_ACTIVE || state == KOS_GEN_DONE;
}

/* We need to know the target size first, so we need to call vsnprintf() twice.
 * This requires a copy of va_list object.  However, neither C89 nor C++98
 * have va_copy(), so we need to pass two va_list objects. */
static KOS_OBJ_ID string_vprintf(KOS_CONTEXT ctx,
                                 const char *format,
                                 va_list     args1,
                                 va_list     args2)
{
    KOS_VECTOR buf;
    int        size;
    KOS_OBJ_ID str = KOS_BADPTR;

    KOS_vector_init(&buf);

    size = vsnprintf(buf.buffer, (size_t)buf.capacity, format, args1);

    if (size > 0) {
        if ((size_t)(size + 1) > buf.capacity) {
            const int error = KOS_vector_resize(&buf, (size_t)(size + 1));

            if (error) {
                KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                goto cleanup;
            }

            vsnprintf(buf.buffer, buf.size, format, args2);
        }

        str = KOS_new_string(ctx, buf.buffer, (unsigned)size);
    }
    else
        str = KOS_STR_EMPTY;

cleanup:
    KOS_vector_destroy(&buf);

    return str;
}

KOS_OBJ_ID KOS_string_printf(KOS_CONTEXT ctx,
                             const char *format,
                             ...)
{
    va_list    args1;
    va_list    args2;
    KOS_OBJ_ID str;

    va_start(args1, format);
    va_start(args2, format);

    str = string_vprintf(ctx, format, args1, args2);

    va_end(args2);
    va_end(args1);

    return str;
}

void KOS_raise_printf(KOS_CONTEXT ctx,
                      const char *format,
                      ...)
{
    va_list    args1;
    va_list    args2;
    KOS_OBJ_ID str;

    va_start(args1, format);
    va_start(args2, format);

    str = string_vprintf(ctx, format, args1, args2);

    va_end(args2);
    va_end(args1);

    if ( ! IS_BAD_PTR(str))
        KOS_raise_exception(ctx, str);
}

void KOS_raise_errno_value(KOS_CONTEXT ctx, const char *prefix, int error_value)
{
    const char *const error_str = strerror(error_value);

    if (prefix)
        KOS_raise_printf(ctx, "%s: %s", prefix, error_str ? error_str : "");

    else {

        const size_t len = error_str ? strlen(error_str) : 0;

        KOS_OBJ_ID error_obj = KOS_new_string(ctx, error_str, (unsigned)len);
        if ( ! IS_BAD_PTR(error_obj))
            KOS_raise_exception(ctx, error_obj);
    }
}

void KOS_raise_errno(KOS_CONTEXT ctx, const char *prefix)
{
    KOS_raise_errno_value(ctx, prefix, errno);
}

#ifdef _WIN32
void KOS_raise_last_error(KOS_CONTEXT ctx, const char *prefix, unsigned error_value)
{
    char *msg = KOS_NULL;
    DWORD msg_size;

    msg_size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                             KOS_NULL,
                             error_value,
                             LANG_USER_DEFAULT,
                             (LPTSTR)&msg,
                             1024,
                             KOS_NULL);

    while (msg_size && ((uint8_t)msg[msg_size - 1] <= 0x20U))
        --msg_size;

    if (prefix)
        KOS_raise_printf(ctx, "%s: 0x%08x - %.*s", prefix, error_value, (int)msg_size, msg);
    else
        KOS_raise_printf(ctx, "0x%08x - %.*s", error_value, (int)msg_size, msg);

    if (msg)
        LocalFree(msg);
}
#endif

KOS_OBJ_ID KOS_new_iterator_copy(KOS_CONTEXT ctx,
                                 KOS_OBJ_ID  iter_id)
{
    KOS_ITERATOR *iter;
    KOS_LOCAL     src;

    KOS_init_local_with(ctx, &src, iter_id);

    assert(GET_OBJ_TYPE(src.o) == OBJ_ITERATOR);

    iter = (KOS_ITERATOR *)kos_alloc_object(ctx,
                                            KOS_ALLOC_MOVABLE,
                                            OBJ_ITERATOR,
                                            sizeof(KOS_ITERATOR));

    if (iter) {
        KOS_atomic_write_relaxed_u32(iter->index,
                KOS_atomic_read_relaxed_u32(OBJPTR(ITERATOR, src.o)->index));
        iter->depth         = OBJPTR(ITERATOR, src.o)->depth;
        iter->type          = OBJPTR(ITERATOR, src.o)->type;
        iter->obj           = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->obj);
        iter->prop_obj      = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->prop_obj);
        iter->key_table     = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->key_table);
        iter->returned_keys = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->returned_keys);
        iter->last_key      = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->last_key);
        iter->last_value    = KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, src.o)->last_value);
    }

    KOS_destroy_top_local(ctx, &src);

    return OBJID(ITERATOR, iter);
}

KOS_OBJ_ID KOS_new_iterator(KOS_CONTEXT      ctx,
                            KOS_OBJ_ID       obj_id,
                            enum KOS_DEPTH_E depth)
{
    KOS_ITERATOR *iter;
    KOS_LOCAL     obj;
    KOS_TYPE      type;

    assert( ! IS_BAD_PTR(obj_id));

    type = GET_OBJ_TYPE(obj_id);

    if ((type == OBJ_OBJECT) || (type == OBJ_CLASS) || (depth != KOS_CONTENTS))
        return kos_new_object_walk(ctx, obj_id, depth);

    KOS_init_local_with(ctx, &obj, obj_id);

    iter = (KOS_ITERATOR *)kos_alloc_object(ctx,
                                            KOS_ALLOC_MOVABLE,
                                            OBJ_ITERATOR,
                                            sizeof(KOS_ITERATOR));

    if (iter) {
        iter->index         = 0;
        iter->depth         = (uint8_t)depth;
        iter->type          = (uint8_t)type;
        iter->obj           = obj.o;
        iter->prop_obj      = obj.o;
        iter->key_table     = KOS_BADPTR;
        iter->returned_keys = KOS_BADPTR;
        iter->last_key      = KOS_BADPTR;
        iter->last_value    = KOS_BADPTR;
    }

    KOS_destroy_top_local(ctx, &obj);

    return OBJID(ITERATOR, iter);
}

int KOS_iterator_next(KOS_CONTEXT ctx,
                      KOS_OBJ_ID  iter_id)
{
    assert( ! IS_BAD_PTR(iter_id));
    assert(GET_OBJ_TYPE(iter_id) == OBJ_ITERATOR);

    switch (OBJPTR(ITERATOR, iter_id)->type) {

        case OBJ_OBJECT:
            /* fall through */
        case OBJ_CLASS:
            return kos_object_walk(ctx, iter_id);

        case OBJ_FUNCTION: {
            KOS_FUNCTION_STATE state;
            KOS_OBJ_ID         obj_id = OBJPTR(ITERATOR, iter_id)->obj;

            assert(GET_OBJ_TYPE(obj_id) == OBJ_FUNCTION);

            if (KOS_is_generator(obj_id, &state)) {

                KOS_LOCAL obj;
                KOS_LOCAL iter;
                uint32_t  idx;

                if (state == KOS_GEN_DONE)
                    break;

                KOS_init_local_with(ctx, &iter, iter_id);
                KOS_init_local_with(ctx, &obj,  obj_id);

                obj_id = KOS_call_generator(ctx, obj.o, KOS_VOID, KOS_EMPTY_ARRAY);

                iter_id = KOS_destroy_top_locals(ctx, &obj, &iter);

                if ( ! IS_BAD_PTR(obj_id)) {
                    idx = KOS_atomic_add_u32(OBJPTR(ITERATOR, iter_id)->index, 1U);

                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key,   TO_SMALL_INT((int64_t)idx));
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value, obj_id);
                    return KOS_SUCCESS;
                }

                /* End of iterator */

                if (KOS_is_exception_pending(ctx))
                    return KOS_ERROR_EXCEPTION;

                break;
            }
            else {
                KOS_raise_exception(ctx, KOS_CONST_ID(str_err_not_generator));
                return KOS_ERROR_EXCEPTION;
            }
        }
        /* fall through */

        default:
            assert(OBJPTR(ITERATOR, iter_id)->type == GET_OBJ_TYPE(OBJPTR(ITERATOR, iter_id)->obj));

            if ( ! KOS_atomic_swap_u32(OBJPTR(ITERATOR, iter_id)->index, 1U)) {
                KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key, KOS_VOID);
                KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value,
                                             KOS_atomic_read_relaxed_obj(OBJPTR(ITERATOR, iter_id)->obj));
                return KOS_SUCCESS;
            }
            break;

        case OBJ_VOID:
            break;

        case OBJ_ARRAY: {
            KOS_LOCAL      iter;
            KOS_OBJ_ID     obj_id = OBJPTR(ITERATOR, iter_id)->obj;
            const uint32_t idx    = KOS_atomic_add_u32(OBJPTR(ITERATOR, iter_id)->index, 1U);
            uint32_t       size;

            assert(GET_OBJ_TYPE(obj_id) == OBJ_ARRAY);

            size = KOS_get_array_size(obj_id);
            if (idx < size) {

                KOS_init_local_with(ctx, &iter, iter_id);
                obj_id = KOS_array_read(ctx, obj_id, (int)idx);
                iter_id = KOS_destroy_top_local(ctx, &iter);

                if ( ! IS_BAD_PTR(obj_id)) {
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key,   TO_SMALL_INT((int64_t)idx));
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value, obj_id);
                    return KOS_SUCCESS;
                }

                assert(KOS_is_exception_pending(ctx));
                return KOS_ERROR_EXCEPTION;
            }

            KOS_atomic_write_relaxed_u32(OBJPTR(ITERATOR, iter_id)->index, size);
            break;
        }

        case OBJ_STRING: {
            KOS_LOCAL      iter;
            KOS_OBJ_ID     obj_id = OBJPTR(ITERATOR, iter_id)->obj;
            const uint32_t idx    = KOS_atomic_add_u32(OBJPTR(ITERATOR, iter_id)->index, 1U);
            uint32_t       size;

            assert(GET_OBJ_TYPE(obj_id) == OBJ_STRING);

            size = KOS_get_string_length(obj_id);
            if (idx < size) {

                KOS_init_local_with(ctx, &iter, iter_id);
                obj_id = KOS_string_get_char(ctx, obj_id, (int)idx);
                iter_id = KOS_destroy_top_local(ctx, &iter);

                if ( ! IS_BAD_PTR(obj_id)) {
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key,   TO_SMALL_INT((int64_t)idx));
                    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value, obj_id);
                    return KOS_SUCCESS;
                }

                return KOS_ERROR_EXCEPTION;
            }

            KOS_atomic_write_relaxed_u32(OBJPTR(ITERATOR, iter_id)->index, size);
            break;
        }

        case OBJ_BUFFER: {
            KOS_OBJ_ID     obj_id = OBJPTR(ITERATOR, iter_id)->obj;
            const uint32_t idx    = KOS_atomic_add_u32(OBJPTR(ITERATOR, iter_id)->index, 1U);
            uint32_t       size;
            uint8_t        elem;

            assert(GET_OBJ_TYPE(obj_id) == OBJ_BUFFER);

            size = KOS_get_buffer_size(obj_id);
            if (idx < size) {

                KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key, TO_SMALL_INT((int64_t)idx));

                elem = KOS_buffer_data_const(obj_id)[idx];
                KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value, TO_SMALL_INT(elem));

                return KOS_SUCCESS;
            }

            KOS_atomic_write_relaxed_u32(OBJPTR(ITERATOR, iter_id)->index, size);
            break;
        }
    }

    assert( ! KOS_is_exception_pending(ctx));

    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_key,   KOS_BADPTR);
    KOS_atomic_write_release_ptr(OBJPTR(ITERATOR, iter_id)->last_value, KOS_BADPTR);

    return KOS_ERROR_NOT_FOUND;
}

struct KOS_NATIVE_TYPE_S {
    KOS_TYPE type;
    unsigned size;
};

enum KOS_FORCE_ENUM_E {
    ENUM_MIN = 0,
    ENUM_MAX = 0x7FFFFFFF
};

static const struct KOS_NATIVE_TYPE_S native_to_kos[19] = {
    { OBJ_VOID,    0                             },
    { OBJ_VOID,    0                             },
    { OBJ_INTEGER, sizeof(uint8_t)               },
    { OBJ_INTEGER, sizeof(uint16_t)              },
    { OBJ_INTEGER, sizeof(uint32_t)              },
    { OBJ_INTEGER, sizeof(uint64_t)              },
    { OBJ_INTEGER, sizeof(int8_t)                },
    { OBJ_INTEGER, sizeof(int16_t)               },
    { OBJ_INTEGER, sizeof(int32_t)               },
    { OBJ_INTEGER, sizeof(int64_t)               },
    { OBJ_INTEGER, sizeof(size_t)                },
    { OBJ_INTEGER, sizeof(enum KOS_FORCE_ENUM_E) },
    { OBJ_BOOLEAN, sizeof(uint8_t)               },
    { OBJ_BOOLEAN, sizeof(uint32_t)              },
    { OBJ_FLOAT,   sizeof(float)                 },
    { OBJ_FLOAT,   sizeof(double)                },
    { OBJ_STRING,  0                             },
    { OBJ_STRING,  0                             },
    { OBJ_BUFFER,  0                             }
};

struct KOS_INT_LIMITS_S {
    int64_t min_value;
    int64_t max_value;
};

#define KOS_MIN_INT32 ((int32_t)((uint32_t)1U << 31))
#define KOS_MIN_INT64 ((int64_t)((uint64_t)1U << 63))
#define KOS_MAX_SIZE  ((int64_t)~(size_t)0U)

static const struct KOS_INT_LIMITS_S int_limits[12] = {
    { 0,             0                    },
    { 0,             0                    },
    { 0,             0xFF                 },
    { 0,             0xFFFF               },
    { 0,             (int64_t)0xFFFFFFFFU },
    { KOS_MIN_INT64, MAX_INT64            },
    { -0x80,         0x7F                 },
    { -0x8000,       0x7FFF               },
    { KOS_MIN_INT32, 0x7FFFFFFF           },
    { KOS_MIN_INT64, MAX_INT64            },
    { 0,             KOS_MAX_SIZE         },
    { 0,             0x7FFFFFFF           }
};

int KOS_extract_native_value(KOS_CONTEXT           ctx,
                             KOS_OBJ_ID            value_id,
                             const KOS_CONVERT    *convert,
                             struct KOS_MEMPOOL_S *alloc,
                             void                 *value_ptr)
{
    KOS_VECTOR                     name_cstr;
    KOS_LOCAL                      value;
    const struct KOS_NATIVE_TYPE_S conv      = native_to_kos[convert->type];
    const unsigned                 num_elems = (conv.size && convert->size) ? (convert->size / conv.size) : 1;
    unsigned                       i;
    int                            error     = KOS_SUCCESS;

    KOS_init_local_with(ctx, &value, value_id);

    KOS_vector_init(&name_cstr);

    assert((convert->type != KOS_NATIVE_INVALID) && (convert->type != KOS_NATIVE_SKIP));

    if (num_elems > 1) {
        const KOS_TYPE type = GET_OBJ_TYPE(value.o);

        if (type != OBJ_ARRAY) {
            TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

            KOS_raise_printf(ctx, "'%s' is %s but expected array",
                             name_cstr.buffer, KOS_get_type_name(type));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
        else if (KOS_get_array_size(value.o) < num_elems) {
            TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

            KOS_raise_printf(ctx, "'%s' requires %u elements, but only %u provided",
                             name_cstr.buffer, num_elems, KOS_get_array_size(value.o));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }
    }

    for (i = 0; i < num_elems; i++) {

        KOS_OBJ_ID elem_id;
        KOS_TYPE   type;

        if (num_elems > 1) {
            elem_id = KOS_array_read(ctx, value.o, i);
            TRY_OBJID(elem_id);
        }
        else
            elem_id = value.o;

        type = GET_OBJ_TYPE(elem_id);
        if (((conv.type <= OBJ_FLOAT) && (type > OBJ_FLOAT)) ||
            ((conv.type > OBJ_FLOAT) && (conv.type != type))) {

            TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

            KOS_raise_printf(ctx, "'%s' is %s but expected %s",
                             name_cstr.buffer, KOS_get_type_name(type), KOS_get_type_name(conv.type));
            RAISE_ERROR(KOS_ERROR_EXCEPTION);
        }

        switch (convert->type) {
            case KOS_NATIVE_UINT8:
                /* fall through */
            case KOS_NATIVE_UINT16:
                /* fall through */
            case KOS_NATIVE_UINT32:
                /* fall through */
            case KOS_NATIVE_UINT64:
                /* fall through */
            case KOS_NATIVE_INT8:
                /* fall through */
            case KOS_NATIVE_INT16:
                /* fall through */
            case KOS_NATIVE_INT32:
                /* fall through */
            case KOS_NATIVE_INT64:
                /* fall through */
            case KOS_NATIVE_SIZE:
                /* fall through */
            case KOS_NATIVE_ENUM: {
                int64_t int_value;

                assert(GET_OBJ_TYPE(elem_id) <= OBJ_FLOAT);

                TRY(KOS_get_integer(ctx, elem_id, &int_value));

                if (int_value < int_limits[convert->type].min_value) {

                    TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

                    KOS_raise_printf(ctx, "'%s' value %" PRId64 " exceeds minimum %" PRId64,
                                     name_cstr.buffer, int_value, int_limits[convert->type].min_value);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }
                if (int_value > int_limits[convert->type].max_value) {

                    TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

                    KOS_raise_printf(ctx, "'%s' value %" PRId64 " exceeds maximum %" PRId64,
                                     name_cstr.buffer, int_value, int_limits[convert->type].max_value);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                switch (convert->type) {
                    case KOS_NATIVE_UINT8:
                        *(uint8_t *)value_ptr = (uint8_t)int_value;
                        break;

                    case KOS_NATIVE_UINT16:
                        *(uint16_t *)value_ptr = (uint16_t)int_value;
                        break;

                    case KOS_NATIVE_UINT32:
                        *(uint32_t *)value_ptr = (uint32_t)int_value;
                        break;

                    case KOS_NATIVE_UINT64:
                        *(uint64_t *)value_ptr = (uint64_t)int_value;
                        break;

                    case KOS_NATIVE_INT8:
                        *(int8_t *)value_ptr = (int8_t)int_value;
                        break;

                    case KOS_NATIVE_INT16:
                        *(int16_t *)value_ptr = (int16_t)int_value;
                        break;

                    case KOS_NATIVE_INT32:
                        *(int32_t *)value_ptr = (int32_t)int_value;
                        break;

                    case KOS_NATIVE_INT64:
                        *(int64_t *)value_ptr = (int64_t)int_value;
                        break;

                    case KOS_NATIVE_SIZE:
                        *(size_t *)value_ptr = (size_t)int_value;
                        break;

                    default:
                        assert(convert->type == KOS_NATIVE_ENUM);
                        *(enum KOS_FORCE_ENUM_E *)value_ptr = (enum KOS_FORCE_ENUM_E)int_value;
                        break;
                }
                break;
            }

            case KOS_NATIVE_BOOL8:
                assert(GET_OBJ_TYPE(elem_id) == OBJ_BOOLEAN);
                *(uint8_t *)value_ptr = KOS_get_bool(elem_id) ? 1 : 0;
                break;

            case KOS_NATIVE_BOOL32:
                assert(GET_OBJ_TYPE(elem_id) == OBJ_BOOLEAN);
                *(uint32_t *)value_ptr = KOS_get_bool(elem_id) ? 1 : 0;
                break;

            case KOS_NATIVE_FLOAT:
                /* fall through */
            case KOS_NATIVE_DOUBLE: {
                double f_value;

                assert(GET_OBJ_TYPE(elem_id) <= OBJ_FLOAT);

                if (IS_SMALL_INT(elem_id))
                    f_value = (double)GET_SMALL_INT(elem_id);
                else switch (READ_OBJ_TYPE(elem_id)) {
                    case OBJ_INTEGER:
                        f_value = (double)OBJPTR(INTEGER, elem_id)->value;
                        break;
                    default:
                        assert(READ_OBJ_TYPE(elem_id) == OBJ_FLOAT);
                        f_value = OBJPTR(FLOAT, elem_id)->value;
                        break;
                }

                if (convert->type == KOS_NATIVE_FLOAT)
                    *(float *)value_ptr = (float)f_value;
                else
                    *(double *)value_ptr = f_value;
                break;
            }

            case KOS_NATIVE_STRING:
                assert(GET_OBJ_TYPE(elem_id) == OBJ_STRING);
                TRY(KOS_string_to_cstr_vec(ctx, elem_id, &name_cstr));

                if (name_cstr.size > convert->size) {
                    TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

                    KOS_raise_printf(ctx, "'%s' string size %u exceeds maximum size %u",
                                     name_cstr.buffer, (unsigned)name_cstr.size, convert->size);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                memcpy(value_ptr, name_cstr.buffer, name_cstr.size);
                break;

            case KOS_NATIVE_STRING_PTR: {
                char    *buf;
                unsigned str_len = 0;

                assert(GET_OBJ_TYPE(elem_id) == OBJ_STRING);

                if (KOS_get_string_length(elem_id) > 0) {
                    str_len = KOS_string_to_utf8(elem_id, KOS_NULL, 0);
                    assert(str_len > 0);

                    if (str_len == ~0U) {
                        TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

                        KOS_raise_printf(ctx, "'%s' contains invalid string", name_cstr.buffer);
                        RAISE_ERROR(KOS_ERROR_EXCEPTION);
                    }
                }

                assert(alloc);
                buf = (char *)KOS_mempool_alloc(alloc, str_len + 1);
                if ( ! buf) {
                    KOS_raise_exception(ctx, KOS_STR_OUT_OF_MEMORY);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                if (str_len)
                    KOS_string_to_utf8(elem_id, buf, str_len);

                buf[str_len] = 0;

                *(char **)value_ptr = buf;
                break;
            }

            default: {
                uint8_t *data;
                assert(convert->type == KOS_NATIVE_BUFFER);
                assert(GET_OBJ_TYPE(elem_id) == OBJ_BUFFER);

                if (KOS_get_buffer_size(elem_id) < convert->size) {
                    TRY(KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr));

                    KOS_raise_printf(ctx, "'%s' buffer size %u too short, need at least %u bytes",
                                     name_cstr.buffer, KOS_get_buffer_size(elem_id), convert->size);
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);
                }

                data  = KOS_buffer_data_volatile(ctx, elem_id);
                error = data ? KOS_SUCCESS : KOS_ERROR_EXCEPTION;
                if (data)
                    memcpy(value_ptr, data, convert->size);
                break;
            }
        }

        value_ptr = (void *)((uintptr_t)value_ptr + conv.size);
    }

cleanup:
    KOS_vector_destroy(&name_cstr);

    KOS_destroy_top_local(ctx, &value);

    return error;
}

int KOS_extract_native_from_array(KOS_CONTEXT           ctx,
                                  KOS_OBJ_ID            array_id,
                                  const char           *element_name,
                                  const KOS_CONVERT    *convert,
                                  struct KOS_MEMPOOL_S *alloc,
                                  ...)
{
    va_list   args;
    KOS_LOCAL array;
    uint32_t  size;
    uint32_t  i     = 0;
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &array, array_id);

    va_start(args, alloc);

    assert( ! IS_BAD_PTR(array_id));
    if (GET_OBJ_TYPE(array_id) != OBJ_ARRAY)
        RAISE_EXCEPTION_STR(str_err_not_array);
    size = KOS_get_array_size(array_id);

    while ( ! IS_BAD_PTR(convert->name)) {

        if (convert->type != KOS_NATIVE_SKIP) {

            KOS_OBJ_ID value_id;
            void      *value_ptr = va_arg(args, void *);

            if (i < size) {
                value_id = KOS_array_read(ctx, array.o, i);
                TRY_OBJID(value_id);
            }
            else if (IS_BAD_PTR(convert->default_value)) {
                KOS_VECTOR name_cstr;

                KOS_vector_init(&name_cstr);

                if ( ! KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr))
                    KOS_raise_printf(ctx, "missing %s %u '%s'", element_name, i, name_cstr.buffer);

                KOS_vector_destroy(&name_cstr);

                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
            else
                value_id = convert->default_value;

            TRY(KOS_extract_native_value(ctx, value_id, convert, alloc, value_ptr));
        }

        ++convert;
        ++i;
    }

cleanup:
    va_end(args);

    KOS_destroy_top_local(ctx, &array);

    return error;
}

int KOS_extract_native_from_iterable(KOS_CONTEXT           ctx,
                                     KOS_OBJ_ID            iterable_id,
                                     const KOS_CONVERT    *convert,
                                     struct KOS_MEMPOOL_S *alloc,
                                     ...)
{
    va_list   args;
    KOS_LOCAL iterable;
    uint32_t  i     = 0;
    int       done  = 0;
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &iterable, iterable_id);

    va_start(args, alloc);

    iterable.o = KOS_new_iterator(ctx, iterable.o, KOS_CONTENTS);
    TRY_OBJID(iterable.o);

    while ( ! IS_BAD_PTR(convert->name)) {

        if ( ! done) {
            error = KOS_iterator_next(ctx, iterable.o);
            if (error == KOS_ERROR_EXCEPTION)
                goto cleanup;
            else if (error == KOS_ERROR_NOT_FOUND)
                done = 1;
        }

        if (convert->type != KOS_NATIVE_SKIP) {

            KOS_OBJ_ID value_id;
            void      *value_ptr = va_arg(args, void *);

            if ( ! done)
                value_id = KOS_get_walk_value(iterable.o);
            else if (IS_BAD_PTR(convert->default_value)) {
                KOS_VECTOR name_cstr;

                KOS_vector_init(&name_cstr);

                if ( ! KOS_string_to_cstr_vec(ctx, convert->name, &name_cstr))
                    KOS_raise_printf(ctx, "missing element %u '%s'", i, name_cstr.buffer);

                KOS_vector_destroy(&name_cstr);

                RAISE_ERROR(KOS_ERROR_EXCEPTION);
            }
            else
                value_id = convert->default_value;

            TRY(KOS_extract_native_value(ctx, value_id, convert, alloc, value_ptr));
        }

        ++convert;
        ++i;
    }

cleanup:
    va_end(args);

    KOS_destroy_top_local(ctx, &iterable);

    return error;
}

int KOS_extract_native_from_object(KOS_CONTEXT           ctx,
                                   KOS_OBJ_ID            object_id,
                                   const KOS_CONVERT    *convert,
                                   struct KOS_MEMPOOL_S *alloc,
                                   ...)
{
    va_list   args;
    KOS_LOCAL object;
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &object, object_id);

    va_start(args, alloc);

    while ( ! IS_BAD_PTR(convert->name)) {

        if (convert->type != KOS_NATIVE_SKIP) {

            KOS_OBJ_ID value_id;
            void      *value_ptr = va_arg(args, void *);

            value_id = KOS_get_property(ctx, object.o, convert->name);

            if (IS_BAD_PTR(value_id)) {

                value_id = convert->default_value;

                if (IS_BAD_PTR(value_id))
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);

                KOS_clear_exception(ctx);
            }

            TRY(KOS_extract_native_value(ctx, value_id, convert, alloc, value_ptr));
        }

        ++convert;
    }

cleanup:
    va_end(args);

    KOS_destroy_top_local(ctx, &object);

    return error;
}

int KOS_extract_native_struct_from_object(KOS_CONTEXT           ctx,
                                          KOS_OBJ_ID            object_id,
                                          const KOS_CONVERT    *convert,
                                          struct KOS_MEMPOOL_S *alloc,
                                          void              *struct_ptr)
{
    KOS_LOCAL object;
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &object, object_id);

    while ( ! IS_BAD_PTR(convert->name)) {

        if (convert->type != KOS_NATIVE_SKIP) {

            KOS_OBJ_ID value_id;
            void      *value_ptr = (void *)((uintptr_t)struct_ptr + convert->offset);

            value_id = KOS_get_property(ctx, object.o, convert->name);

            if (IS_BAD_PTR(value_id)) {

                value_id = convert->default_value;

                if (IS_BAD_PTR(value_id))
                    RAISE_ERROR(KOS_ERROR_EXCEPTION);

                KOS_clear_exception(ctx);
            }

            TRY(KOS_extract_native_value(ctx, value_id, convert, alloc, value_ptr));
        }

        ++convert;
    }

cleanup:
    KOS_destroy_top_local(ctx, &object);

    return error;
}

KOS_OBJ_ID KOS_new_from_native(KOS_CONTEXT        ctx,
                               const KOS_CONVERT *convert,
                               const void        *value_ptr)
{
    KOS_VECTOR                     name_cstr;
    KOS_LOCAL                      value;
    const struct KOS_NATIVE_TYPE_S conv      = native_to_kos[convert->type];
    const unsigned                 num_elems = (conv.size && convert->size) ? (convert->size / conv.size) : 1;
    unsigned                       i;
    int                            error     = KOS_SUCCESS;

    KOS_init_local(ctx, &value);

    KOS_vector_init(&name_cstr);

    assert((convert->type != KOS_NATIVE_INVALID) && (convert->type != KOS_NATIVE_SKIP));

    if (num_elems > 1) {
        value.o = KOS_new_array(ctx, num_elems);
        TRY_OBJID(value.o);
    }

    for (i = 0; i < num_elems; i++) {

        KOS_OBJ_ID elem_id;

        switch (convert->type) {

            case KOS_NATIVE_UINT8:
                elem_id = KOS_new_int(ctx, (int64_t)*(const uint8_t *)value_ptr);
                break;

            case KOS_NATIVE_UINT16:
                elem_id = KOS_new_int(ctx, (int64_t)*(const uint16_t *)value_ptr);
                break;

            case KOS_NATIVE_UINT32:
                elem_id = KOS_new_int(ctx, (int64_t)*(const uint32_t *)value_ptr);
                break;

            case KOS_NATIVE_UINT64:
                elem_id = KOS_new_int(ctx, (int64_t)*(const uint64_t *)value_ptr);
                break;

            case KOS_NATIVE_INT8:
                elem_id = KOS_new_int(ctx, *(const int8_t *)value_ptr);
                break;

            case KOS_NATIVE_INT16:
                elem_id = KOS_new_int(ctx, *(const int16_t *)value_ptr);
                break;

            case KOS_NATIVE_INT32:
                elem_id = KOS_new_int(ctx, *(const int32_t *)value_ptr);
                break;

            case KOS_NATIVE_INT64:
                elem_id = KOS_new_int(ctx, *(const int64_t *)value_ptr);
                break;

            case KOS_NATIVE_SIZE:
                elem_id = KOS_new_int(ctx, (int64_t)*(const size_t *)value_ptr);
                break;

            case KOS_NATIVE_ENUM:
                elem_id = KOS_new_int(ctx, (int64_t)*(const enum KOS_FORCE_ENUM_E *)value_ptr);
                break;

            case KOS_NATIVE_BOOL8:
                elem_id = KOS_BOOL(*(const uint8_t *)value_ptr);
                break;

            case KOS_NATIVE_BOOL32:
                elem_id = KOS_BOOL(*(const uint32_t *)value_ptr);
                break;

            case KOS_NATIVE_FLOAT:
                elem_id = KOS_new_float(ctx, (double)*(const float *)value_ptr);
                break;

            case KOS_NATIVE_DOUBLE:
                elem_id = KOS_new_float(ctx, *(const double *)value_ptr);
                break;

            case KOS_NATIVE_STRING: {
                const void *const zero = memchr(value_ptr, 0, convert->size);
                elem_id = KOS_new_string(ctx, (const char *)value_ptr, zero ? (unsigned)((uintptr_t)zero - (uintptr_t)value_ptr) : convert->size);
                break;
            }

            case KOS_NATIVE_STRING_PTR:
                elem_id = KOS_new_cstring(ctx, *(const char *const *)value_ptr);
                break;

            default:
                assert(convert->type == KOS_NATIVE_BUFFER);
                elem_id = KOS_new_buffer(ctx, convert->size);
                if ( ! IS_BAD_PTR(elem_id)) {
                    uint8_t *const dest = KOS_buffer_data_volatile(ctx, elem_id);
                    if (dest)
                        memcpy(dest, value_ptr, convert->size);
                }
                break;
        }

        TRY_OBJID(elem_id);

        if (num_elems > 1)
            TRY(KOS_array_write(ctx, value.o, i, elem_id));
        else
            value.o = elem_id;

        value_ptr = (const void *)((uintptr_t)value_ptr + conv.size);
    }

cleanup:
    KOS_vector_destroy(&name_cstr);

    value.o = KOS_destroy_top_local(ctx, &value);

    return error ? KOS_BADPTR : value.o;
}

int KOS_set_properties_from_native(KOS_CONTEXT        ctx,
                                   KOS_OBJ_ID         object_id,
                                   const KOS_CONVERT *convert,
                                   const void        *struct_ptr)
{
    KOS_LOCAL object;
    int       error = KOS_SUCCESS;

    KOS_init_local_with(ctx, &object, object_id);

    while ( ! IS_BAD_PTR(convert->name)) {

        if (convert->type != KOS_NATIVE_SKIP) {

            const void *const value_ptr = (const void *)((uintptr_t)struct_ptr + convert->offset);
            KOS_OBJ_ID        value_id  = KOS_new_from_native(ctx, convert, value_ptr);

            TRY_OBJID(value_id);

            TRY(KOS_set_property(ctx, object.o, convert->name, value_id));
        }

        ++convert;
    }

cleanup:
    KOS_destroy_top_local(ctx, &object);

    return error;
}

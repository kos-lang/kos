/*
 * Copyright (c) 2014-2016 Chris Dragan
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

#include "../inc/kos_context.h"
#include "../inc/kos_array.h"
#include "../inc/kos_error.h"
#include "../inc/kos_modules_init.h"
#include "../inc/kos_string.h"
#include "../lang/kos_file.h"
#include "../lang/kos_memory.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    int                error = KOS_SUCCESS;
    KOS_CONTEXT        ctx;
    struct _KOS_VECTOR cstr;

    _KOS_vector_init(&cstr);

    if (argc != 2) {
        fprintf(stderr, "Usage: kos <program>\n");
        error = KOS_ERROR_EXCEPTION;
    }
    else {
        error = KOS_context_init(&ctx);

        if (error)
            fprintf(stderr, "Failed to initialize interpreter\n");
        else {
            error = KOS_modules_init(&ctx);

            if (!error) {

                /* KOSDISASM=1 turns on disassembly */
                if (!_KOS_get_env("KOSDISASM", &cstr) &&
                        cstr.size == 2 && cstr.buffer[0] == '1' && cstr.buffer[1] == 0)
                    ctx.flags |= KOS_CTX_DEBUG;

                error = KOS_context_load_module(&ctx, argv[1]);

                if (error) {
                    assert(error == KOS_ERROR_EXCEPTION);

                    /* TODO move this to a function KOS_print_exception(ctx) */
                    if (error == KOS_ERROR_EXCEPTION) {
                        KOS_OBJ_PTR exception = KOS_get_exception(&ctx);
                        assert(!IS_BAD_PTR(exception));
                        if (!IS_SMALL_INT(exception) && IS_STRING_OBJ(exception)) {
                            KOS_clear_exception(&ctx);
                            if (KOS_SUCCESS == KOS_string_to_cstr_vec(&ctx, exception, &cstr))
                                fprintf(stderr, "%s\n", cstr.buffer);
                        }
                        else {
                            KOS_OBJ_PTR formatted;

                            KOS_clear_exception(&ctx);
                            formatted = KOS_format_exception(&ctx, exception);
                            if (IS_BAD_PTR(formatted)) {
                                KOS_OBJ_PTR str;

                                KOS_clear_exception(&ctx);

                                str = KOS_object_to_string(&ctx, exception);

                                KOS_clear_exception(&ctx);

                                if (IS_BAD_PTR(str) || KOS_string_to_cstr_vec(&ctx, str, &cstr))
                                    fprintf(stderr, "Exception: <unable to format>\n");
                                else
                                    fprintf(stderr, "%s\n", cstr.buffer);
                            }
                            else {
                                uint32_t i;
                                uint32_t lines;

                                assert(!IS_SMALL_INT(formatted) && GET_OBJ_TYPE(formatted) == OBJ_ARRAY);

                                lines = KOS_get_array_size(formatted);

                                for (i = 0; i < lines; i++) {
                                    KOS_OBJ_PTR line = KOS_array_read(&ctx, formatted, (int)i);
                                    assert(!KOS_is_exception_pending(&ctx));
                                    if (KOS_SUCCESS == KOS_string_to_cstr_vec(&ctx, line, &cstr))
                                        fprintf(stderr, "%s\n", cstr.buffer);
                                }
                            }
                        }
                    }
                }
            }

            KOS_context_destroy(&ctx);
        }
    }

    _KOS_vector_destroy(&cstr);

    return (error == KOS_SUCCESS) ? 0 : 1;
}

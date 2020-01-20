/*
 * Copyright (c) 2014-2020 Chris Dragan
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

#include "../inc/kos_instance.h"
#include "../core/kos_compiler.h"
#include "../core/kos_parser.h"

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    KOS_COMP_UNIT program;
    KOS_PARSER    parser;
    KOS_AST_NODE *ast;
    const int     module_idx     = 0;
    unsigned      num_opt_passes = 0;
    int           error;

    kos_compiler_init(&program, module_idx);
    kos_parser_init(&parser,
                    &program.allocator,
                    (unsigned)module_idx,
                    (const char *)data,
                    (const char *)data + size);

    error = kos_parser_parse(&parser, &ast);

    if ( ! error) {
        KOS_INSTANCE inst;
        KOS_CONTEXT  ctx;

        error = KOS_instance_init(&inst, 0, &ctx);

        if ( ! error) {
            program.ctx = ctx;
            error = kos_compiler_compile(&program, ast, &num_opt_passes);

            KOS_instance_destroy(&inst);
        }
    }

    kos_parser_destroy(&parser);
    kos_compiler_destroy(&program);

    return 0;
}

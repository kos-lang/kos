/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014-2023 Chris Dragan
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

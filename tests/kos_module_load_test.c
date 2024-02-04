/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#include "../inc/kos_error.h"
#include "../inc/kos_instance.h"
#include "../inc/kos_module.h"
#include "kos_test_tools.h"
#include <stdio.h>

KOS_DECLARE_STATIC_CONST_STRING(str_test, "test_global");

int main(void)
{
    KOS_INSTANCE inst;
    KOS_CONTEXT  ctx;
    KOS_OBJ_ID   mod_obj;

    static const char base[] = "base.kos";

    TEST(KOS_instance_init(&inst, KOS_INST_MANUAL_GC, &ctx) == KOS_SUCCESS);

    /************************************************************************/
    mod_obj = KOS_load_module_from_memory(ctx, base, sizeof(base) - 1, 0, 0);
    TEST(IS_BAD_PTR(mod_obj));
    TEST_EXCEPTION();

    /************************************************************************/
    {
        unsigned idx = ~0U;

        mod_obj = inst.modules.init_module;

        TEST( ! IS_BAD_PTR(mod_obj));
        TEST(GET_OBJ_TYPE(mod_obj) == OBJ_MODULE);

        TEST(KOS_module_add_global(ctx, mod_obj, KOS_CONST_ID(str_test), TO_SMALL_INT(42), &idx) == KOS_SUCCESS);
        TEST_NO_EXCEPTION();
        TEST(idx == 0);

        idx = ~0U;
        TEST(KOS_module_add_global(ctx, mod_obj, KOS_CONST_ID(str_test), TO_SMALL_INT(42), &idx) != KOS_SUCCESS);
        TEST_EXCEPTION();
        TEST(idx == ~0U);
    }

    KOS_instance_destroy(&inst);

    return 0;
}

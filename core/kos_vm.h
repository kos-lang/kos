/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_VM_H_INCLUDED
#define KOS_VM_H_INCLUDED

#include "../inc/kos_entity.h"

KOS_OBJ_ID kos_vm_run_module(KOS_CONTEXT ctx, KOS_OBJ_ID module_obj);

#endif

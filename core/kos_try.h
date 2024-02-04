/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
 */

#ifndef KOS_TRY_H_INCLUDED
#define KOS_TRY_H_INCLUDED

#define TRY(code) do { if (0 != (error = (code))) goto cleanup; } while (0)

#define TRY_OBJID(obj_id) do { if (IS_BAD_PTR(obj_id)) RAISE_ERROR(KOS_ERROR_EXCEPTION); } while (0)

#define RAISE_EXCEPTION(err_obj) do { KOS_raise_exception_cstring(ctx, (err_obj)); RAISE_ERROR(KOS_ERROR_EXCEPTION); } while (0)

#define RAISE_EXCEPTION_STR(cstr_obj) do { KOS_raise_exception(ctx, KOS_CONST_ID(cstr_obj)); RAISE_ERROR(KOS_ERROR_EXCEPTION); } while (0)

#define RAISE_ERROR(code) do { error = (code); goto cleanup; } while (0)

#endif

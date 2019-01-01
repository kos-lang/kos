/*
 * Copyright (c) 2014-2019 Chris Dragan
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

#ifndef KOS_TRY_H_INCLUDED
#define KOS_TRY_H_INCLUDED

#define TRY(code) do { if (0 != (error = (code))) goto cleanup; } while (0)

#define TRY_OBJID(obj_id) do { if (IS_BAD_PTR(obj_id)) RAISE_ERROR(KOS_ERROR_EXCEPTION); } while (0)

#define RAISE_EXCEPTION(err_obj) do { KOS_raise_exception_cstring(ctx, (err_obj)); RAISE_ERROR(KOS_ERROR_EXCEPTION); } while (0)

#define RAISE_ERROR(code) do { error = (code); goto cleanup; } while (0)

#endif

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

#ifndef KOS_ERROR_H_INCLUDED
#define KOS_ERROR_H_INCLUDED

enum KOS_ERROR_E {
    KOS_SUCCESS,
    KOS_SUCCESS_RETURN,                     /* auxiliary, not an error */
    KOS_ERROR_INTERNAL,
    KOS_ERROR_EXCEPTION,
    KOS_ERROR_OUT_OF_MEMORY,
    KOS_ERROR_NOT_FOUND,
    KOS_ERROR_NO_DIRECTORY,
    KOS_ERROR_CANNOT_OPEN_FILE,
    KOS_ERROR_CANNOT_READ_FILE,
    KOS_ERROR_SCANNING_FAILED,
    KOS_ERROR_PARSE_FAILED,
    KOS_ERROR_COMPILE_FAILED,
    KOS_ERROR_INVALID_UTF8_CHARACTER,
    KOS_ERROR_INTERRUPTED,
    KOS_ERROR_SETTER,                       /* property has a setter */
    KOS_ERROR_INVALID_EXPONENT,
    KOS_ERROR_EXPONENT_OUT_OF_RANGE,
    KOS_ERROR_NUMBER_TOO_BIG,
    KOS_ERROR_INTEGER_EXPECTED,
    KOS_ERROR_INVALID_NUMBER
};

#endif

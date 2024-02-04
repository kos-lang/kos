/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2014-2024 Chris Dragan
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
    KOS_ERROR_ERRNO,                        /* read errno to get error code */
    KOS_ERROR_SETTER,                       /* property has a setter */
    KOS_ERROR_SCANNING_FAILED,
    KOS_ERROR_PARSE_FAILED,
    KOS_ERROR_COMPILE_FAILED,
    KOS_ERROR_INVALID_UTF8_CHARACTER,
    KOS_ERROR_INTERRUPTED,
    KOS_ERROR_INVALID_EXPONENT,
    KOS_ERROR_EXPONENT_OUT_OF_RANGE,
    KOS_ERROR_NUMBER_TOO_BIG,
    KOS_ERROR_INTEGER_EXPECTED,
    KOS_ERROR_INVALID_NUMBER,
    KOS_ERROR_LAST_ERROR,                   /* GetLastError (Windows only) */
    KOS_ERROR_NO_VIRTUAL_TERMINAL
};

#endif

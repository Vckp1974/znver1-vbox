/* $Id: assert-r0drv-netbsd.c 63346 2016-08-11 18:51:48Z vboxsync $ */
/** @file
 * IPRT - Assertion Workers, Ring-0 Drivers, NetBSD.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-netbsd-kernel.h"

#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/stdarg.h>

#include "internal/assert.h"


DECLHIDDEN(void) rtR0AssertNativeMsg1(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    printf("\r\n!!Assertion Failed!!\r\n"
           "Expression: %s\r\n"
           "Location  : %s(%d) %s\r\n",
           pszExpr, pszFile, uLine, pszFunction);
}


DECLHIDDEN(void) rtR0AssertNativeMsg2V(bool fInitial, const char *pszFormat, va_list va)
{
    /** @todo implement rtR0AssertNativeMsg2V. */
}


RTR0DECL(void) RTR0AssertPanicSystem(void)
{
    panic("%s%s", g_szRTAssertMsg1, g_szRTAssertMsg2);

}

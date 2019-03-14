/* $Id: semfastmutex-generic.cpp 76553 2019-01-01 01:45:53Z vboxsync $ */
/** @file
 * IPRT - Fast Mutex, Generic.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/errcore.h>
#include <iprt/critsect.h>



RTDECL(int) RTSemFastMutexCreate(PRTSEMFASTMUTEX phFastMtx)
{
    PRTCRITSECT pCritSect = (PRTCRITSECT)RTMemAlloc(sizeof(RTCRITSECT));
    if (!pCritSect)
        return VERR_NO_MEMORY;

    int rc = RTCritSectInitEx(pCritSect, RTCRITSECT_FLAGS_NO_NESTING,
                              NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
    if (RT_SUCCESS(rc))
        *phFastMtx = (RTSEMFASTMUTEX)pCritSect;
    else
        RTMemFree(pCritSect);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemFastMutexCreate);


RTDECL(int) RTSemFastMutexDestroy(RTSEMFASTMUTEX hFastMtx)
{
    if (hFastMtx == NIL_RTSEMFASTMUTEX)
        return VERR_INVALID_PARAMETER;
    PRTCRITSECT pCritSect = (PRTCRITSECT)hFastMtx;
    int rc = RTCritSectDelete(pCritSect);
    if (RT_SUCCESS(rc))
        RTMemFree(pCritSect);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemFastMutexDestroy);


RTDECL(int) RTSemFastMutexRequest(RTSEMFASTMUTEX hFastMtx)
{
    return RTCritSectEnter((PRTCRITSECT)hFastMtx);
}
RT_EXPORT_SYMBOL(RTSemFastMutexRequest);


RTDECL(int) RTSemFastMutexRelease(RTSEMFASTMUTEX hFastMtx)
{
    return RTCritSectLeave((PRTCRITSECT)hFastMtx);
}
RT_EXPORT_SYMBOL(RTSemFastMutexRelease);


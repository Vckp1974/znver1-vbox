/* $Id: Defs.h 76576 2019-01-01 06:05:25Z vboxsync $ */
/** @file
 * DHCP server - common definitions
 */

/*
 * Copyright (C) 2017-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBOX_INCLUDED_SRC_Dhcpd_Defs_h
#define VBOX_INCLUDED_SRC_Dhcpd_Defs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stdint.h>
#include <iprt/string.h>
#include <VBox/log.h>

#include <map>
#include <vector>

#if __cplusplus >= 199711
#include <memory>
using std::shared_ptr;
#else
#include <tr1/memory>
using std::tr1::shared_ptr;
#endif

typedef std::vector<uint8_t> octets_t;

typedef std::map<uint8_t, octets_t> rawopts_t;

class DhcpOption;
typedef std::map<uint8_t, std::shared_ptr<DhcpOption> > optmap_t;

inline bool operator==(const RTMAC &l, const RTMAC &r)
{
    return memcmp(&l, &r, sizeof(RTMAC)) == 0;
}

inline bool operator<(const RTMAC &l, const RTMAC &r)
{
    return memcmp(&l, &r, sizeof(RTMAC)) < 0;
}

#if 1
#define LogDHCP LogRel
#else
#define LogDHCP(args) RTPrintf args
#endif

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_Defs_h */

#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: index.py 76553 2019-01-01 01:45:53Z vboxsync $

"""
CGI - Web UI - Main (index) page.
"""

__copyright__ = \
"""
Copyright (C) 2012-2019 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL) only, as it comes in the "COPYING.CDDL" file of the
VirtualBox OSE distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.
"""
__version__ = "$Revision: 76553 $"


# Standard python imports.
import os
import sys

# Only the main script needs to modify the path.
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testmanager                        import config;
from testmanager.core.webservergluecgi  import WebServerGlueCgi;
from testmanager.webui.wuimain          import WuiMain;


def main():
    """
    Main function a la C/C++. Returns exit code.
    """

    oSrvGlue = WebServerGlueCgi(g_ksValidationKitDir, fHtmlOutput = False);
    try:
        oWui = WuiMain(oSrvGlue);
        oWui.dispatchRequest();
        oSrvGlue.flush();
    except Exception as oXcpt:
        return oSrvGlue.errorPage('Internal error: %s' % (str(oXcpt),), sys.exc_info());

    return 0;

if __name__ == '__main__':
    if config.g_kfProfileIndex:
        from testmanager.debug import cgiprofiling;
        sys.exit(cgiprofiling.profileIt(main));
    else:
        sys.exit(main());


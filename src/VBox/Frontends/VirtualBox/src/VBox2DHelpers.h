/* $Id: VBox2DHelpers.h 76581 2019-01-01 06:24:57Z vboxsync $ */
/** @file
 * VBox Qt GUI - 2D Video Acceleration helpers declarations.
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
 */

#ifndef FEQT_INCLUDED_SRC_VBox2DHelpers_h
#define FEQT_INCLUDED_SRC_VBox2DHelpers_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if defined(VBOX_GUI_USE_QGL) || defined(VBOX_WITH_VIDEOHWACCEL)

/* Qt includes: */
#include <QtGlobal>

/** 2D Video Acceleration Helpers */
namespace VBox2DHelpers
{
    /** Returns whether 2D Video Acceleration is available. */
    bool isAcceleration2DVideoAvailable();

    /** Returns 2D offscreen video memory required for 2D Video Acceleration. */
    quint64 required2DOffscreenVideoMemory();
};

#endif /* VBOX_GUI_USE_QGL || VBOX_WITH_VIDEOHWACCEL */

#endif /* !FEQT_INCLUDED_SRC_VBox2DHelpers_h */

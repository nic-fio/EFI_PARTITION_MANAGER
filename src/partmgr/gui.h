/* Copyright (c) 2026 Nicola Fiorillo
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

/* partmgr: the graphical window (decisions P18-P25). */
#ifndef PARTMGR_GUI_H
#define PARTMGR_GUI_H

#include "partmgr.h"

/* Runs the window when the firmware has a graphics screen; false, with
 * nothing changed, when it has none (the text screens are used then). */
bool pm_gui(void);

#endif

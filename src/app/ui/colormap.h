// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_UI_COLORMAP_H
#define CLAW3D_APP_UI_COLORMAP_H

/// Shared ImGui color-map sampling for scalar legends and small UI plots.

#include "imgui.h"

namespace claw_ui {

ImU32 colormap_color(float t);

} // namespace claw_ui

#endif // CLAW3D_APP_UI_COLORMAP_H

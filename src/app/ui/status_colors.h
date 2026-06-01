// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_UI_STATUS_COLORS_H
#define CLAW3D_UI_STATUS_COLORS_H

/// Contrast-safe semantic text colors for the light ImGui theme.

#include "imgui.h"

namespace claw_ui {

inline ImVec4 status_running_color() {
    return ImVec4(0.10f, 0.34f, 0.62f, 1.0f);
}

inline ImVec4 status_success_color() {
    return ImVec4(0.06f, 0.42f, 0.22f, 1.0f);
}

inline ImVec4 status_warning_color() {
    return ImVec4(0.58f, 0.35f, 0.03f, 1.0f);
}

inline ImVec4 status_error_color() {
    return ImVec4(0.68f, 0.16f, 0.13f, 1.0f);
}

inline ImVec4 status_muted_color() {
    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
}

} // namespace claw_ui

#endif // CLAW3D_UI_STATUS_COLORS_H

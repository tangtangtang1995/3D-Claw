// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ui/ui_scale.h"

#include <algorithm>


UIScale& UIScale::instance() {
    static UIScale s;
    return s;
}


void UIScale::capture_baseline() {
    baseline_ = ImGui::GetStyle();
    has_baseline_ = true;
}


void UIScale::set_scale(float s) {
    scale_ = std::min(kMax, std::max(kMin, s));
}


void UIScale::apply_for_next_frame() {
    if (!has_baseline_) return;
    ImGuiStyle& live = ImGui::GetStyle();
    live = baseline_;          // reset to captured defaults
    live.ScaleAllSizes(scale_);
    ImGui::GetIO().FontGlobalScale = scale_;
}

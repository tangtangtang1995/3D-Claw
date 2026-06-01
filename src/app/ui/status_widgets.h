// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_UI_STATUS_WIDGETS_H
#define CLAW3D_UI_STATUS_WIDGETS_H

/// Status and progress widgets used by long-running operation panels.

#include "services/core/algorithm_controller.h"
#include "ui/status_colors.h"

#include "imgui.h"

#include <string>

namespace claw_ui {

inline void render_algorithm_status_panel(const AlgorithmController& controller,
                                          AlgorithmId expected_id,
                                          const char* fallback_label) {
    const bool this_job = controller.is_running_id(expected_id);
    const bool other_job = !this_job && controller.is_running();
    if (!this_job && !other_job)
        return;

    std::string label = controller.current_label();
    if (label.empty() && fallback_label)
        label = fallback_label;

    const double time = ImGui::GetTime();
    const int dots = static_cast<int>(time * 3.0) % 4;

    const ImVec4 bg = this_job
        ? ImVec4(0.88f, 0.94f, 1.00f, 1.0f)
        : ImVec4(1.00f, 0.95f, 0.84f, 1.0f);
    const ImVec4 border = this_job
        ? ImVec4(0.36f, 0.56f, 0.78f, 1.0f)
        : ImVec4(0.74f, 0.56f, 0.24f, 1.0f);
    const ImVec4 text = this_job ? status_running_color()
                                 : status_warning_color();

    ImGui::Spacing();
    ImGui::PushID(static_cast<int>(expected_id));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::BeginChild("##algorithm_status_panel",
                      ImVec2(0.0f,
                             ImGui::GetTextLineHeightWithSpacing() * 2.7f),
                      true,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(text, "%s: %s%.*s",
                       this_job ? "Running" : "Waiting",
                       label.empty() ? "algorithm" : label.c_str(),
                       dots,
                       "....");
    ImGui::TextDisabled("%s",
                        this_job
                            ? "Please wait. This panel will update when the task finishes."
                            : "Another algorithm is running. Start controls are locked.");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

} // namespace claw_ui

#endif // CLAW3D_UI_STATUS_WIDGETS_H

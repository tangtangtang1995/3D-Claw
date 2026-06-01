// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// AI and Help menu sections for MainWindow.
// Labels, callbacks, and tooltips stay local to the menu implementation.

#include "window/main_window.h"

#include <easy3d/util/logging.h>

#include "imgui.h"


void MainWindow::render_menu_ai() {
    if (ImGui::BeginMenu("AI")) {
        if (ImGui::MenuItem("AI Chat"))
            dlg_ai_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("AI Chat (DeepSeek)");
        if (ImGui::MenuItem("3D Generation..."))
            open_dialog(dlg_3dgen_, st_3dgen_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("AI 3D generation (Tencent Hunyuan3D API). Text or image to 3D model.");
        ImGui::EndMenu();
    }
}


void MainWindow::render_menu_help() {
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About"))
            dlg_about_ = true;
        if (ImGui::MenuItem("Manual"))
            LOG(INFO) << "Manual: see README.md and BUILDING.md in the 3D Claw repository.";
        ImGui::EndMenu();
    }
}

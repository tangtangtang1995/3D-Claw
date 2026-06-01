// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Analyze menu section for MainWindow.
//
// Analyze merges the former Property and Measurement menus (nested as
// submenus) with the model-to-model distance tools.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "viewport/viewport_canvas.h"

#include "imgui.h"


void MainWindow::render_menu_analyze() {
    if (ImGui::BeginMenu("Analyze")) {
        menu_group_ai_item(this, "Ask AI: which analysis tool should I use?",
                           "Analyze",
                           "Choose property and topology reports, interactive measurements, or model-to-model distance analysis.",
                           "A current model; some tools need a SurfaceMesh or a second compatible model.",
                           "Results depend on model scale, sampling density, watertightness, and alignment quality.",
                           viewer_.current_model());
        ImGui::Separator();
        // Property and Measurement are nested as submenus of Analyze.
        render_menu_property();
        render_menu_measurement();
        ImGui::Separator();
        if (ImGui::MenuItem("Point Cloud <-> Mesh Distance..."))
            open_dialog(dlg_pc_mesh_dist_, st_pc_mesh_dist_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compute nearest distances from each point in a cloud to a mesh surface.");
        if (ImGui::MenuItem("Point Cloud <-> Point Cloud Distance..."))
            open_dialog(dlg_pc_pc_dist_, st_pc_pc_dist_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compute nearest distances between two point clouds.");
        ImGui::EndMenu();
    }
}

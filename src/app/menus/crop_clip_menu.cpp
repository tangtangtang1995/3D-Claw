// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Crop / Clip menu section for MainWindow.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "viewport/viewport_canvas.h"

#include "imgui.h"


void MainWindow::render_menu_crop_clip() {
    if (ImGui::BeginMenu("Crop / Clip")) {
        menu_group_ai_item(this, "Ask AI: which crop/clip tool should I use?",
                           "Crop / Clip",
                           "Choose box crop or plane clip.",
                           "A current SurfaceMesh or PointCloud.",
                           "Crop creates a new child model; the original is untouched.",
                           viewer_.current_model());
        ImGui::Separator();
        auto open_crop_dialog = [&](CropMode mode) {
            st_crop_ = CropState{};
            st_crop_.mode = mode;
            st_crop_.box_initialized = false;
            dlg_crop_ = true;
        };
        if (ImGui::MenuItem("Box Crop...")) {
            open_crop_dialog(CropMode::Box);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filter a SurfaceMesh / PointCloud by an interactive box.\n"
                              "Outputs a new model as a child of the source. Original is untouched.");
        if (ImGui::MenuItem("Plane Clip...")) {
            open_crop_dialog(CropMode::PlaneXY);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filter by a plane half-space (XY/YZ/XZ or custom normal).\n"
                              "Mesh: face-level filter, no cap fill yet.");
        ImGui::EndMenu();
    }
}

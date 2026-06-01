// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Measurement menu section for MainWindow.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "viewport/viewport_canvas.h"

#include "imgui.h"


void MainWindow::render_menu_measurement() {
    if (ImGui::BeginMenu("Measurement")) {
        menu_group_ai_item(this, "Ask AI: which measurement should I use?",
                           "Measurement",
                           "Choose interactive distance, polyline, angle, bounding box, surface area, or volume measurements.",
                           "A current model; surface area and volume require SurfaceMesh input.",
                           "Volume is reliable only for closed watertight meshes.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::MenuItem("Distance / Polyline / Angle...")) {
            st_measure_.reset();
            st_measure_.type = MeasureType::Distance;
            dlg_measure_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Interactive measurement: pick points on model surface.\n"
                              "Distance (2 pts), Polyline (N pts), Angle (3 pts).");
        ImGui::Separator();
        if (ImGui::MenuItem("Bounding Box...")) {
            st_measure_.reset();
            st_measure_.type = MeasureType::MtBBox;
            dlg_measure_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Display the bounding box dimensions of the current model.");
        if (ImGui::MenuItem("Surface Area...")) {
            st_measure_.reset();
            st_measure_.type = MeasureType::MtSurfaceArea;
            dlg_measure_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compute the surface area of the current SurfaceMesh.");
        if (ImGui::MenuItem("Volume...")) {
            st_measure_.reset();
            st_measure_.type = MeasureType::MtVolume;
            dlg_measure_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compute the volume of the current SurfaceMesh.\n"
                              "Reliable only for closed (watertight) meshes.");
        ImGui::EndMenu();
    }
}

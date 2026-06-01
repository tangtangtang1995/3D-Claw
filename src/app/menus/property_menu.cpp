// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Property menu for MainWindow.
//
// Contains model attribute tools, simple scalar attribute generation, and
// topology reporting entry points.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_property() {
    if (ImGui::BeginMenu("Property")) {
        menu_group_ai_item(this, "Ask AI: which property tool should I use?",
                           "Property",
                           "Explain height fields, curvature attributes, topology reports, and property manipulation.",
                           "A current point cloud or surface mesh.",
                           "Some property computations add attributes that can affect later visualization choices.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::MenuItem("Manipulate Properties..."))
            open_dialog(dlg_properties_, st_properties_);
        ImGui::SetNextItemAllowOverlap();
        bool height_clicked = ImGui::MenuItem("Compute Height Field");
        bool height_hovered = ImGui::IsItemHovered();
        bool height_ai = menu_command_ai_tip(
            "Property > Compute Height Field",
            "Create v:height_x/y/z scalar attributes from vertex coordinates for colormap visualization.",
            "A current point cloud or surface mesh.",
            "Adds attributes to the model; geometry is unchanged.",
            false, false, viewer_.current_model());
        if (height_clicked && !height_ai) {
            auto* model = viewer_.current_model();
            if (claw3d::services::compute_height_fields(model)) {
                model->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "height field computed";
            }
        }
        if (height_hovered)
            ImGui::SetTooltip("Create scalar attributes v:height_x, v:height_y, v:height_z\n"
                              "from vertex XYZ coordinates. Enables height-map colormap visualization\n"
                              "and terrain-style rendering.");
        if (ImGui::MenuItem("Compute Surface Mesh Curvatures..."))
            open_dialog(dlg_sm_curvature_, st_sm_curvature_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compute per-vertex mean curvature, Gaussian curvature, and\n"
                              "principal curvature directions. Opens a dialog for parameter selection.");
        ImGui::SetNextItemAllowOverlap();
        bool topo_clicked = ImGui::MenuItem("Report Topology Statistics");
        bool topo_hovered = ImGui::IsItemHovered();
        bool topo_ai = menu_command_ai_tip(
            "Property > Report Topology Statistics",
            "Classify connected components by topology and log face/edge/vertex/border counts.",
            "A current SurfaceMesh.",
            "Read-only analysis; results are printed to Log.",
            false, false, viewer_.current_model());
        if (topo_clicked && !topo_ai) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
            if (mesh) {
                const auto report =
                    claw3d::services::build_topology_statistics_report(mesh, 10);
                LOG(INFO) << "\n" << report;
            }
        }
        if (topo_hovered)
            ImGui::SetTooltip("Log per-component topology classification: sphere, disc, cylinder, torus.\n"
                              "Reports face/vertex/edge/border counts and watertightness for up to 10 components.");
        ImGui::EndMenu();
    }
}

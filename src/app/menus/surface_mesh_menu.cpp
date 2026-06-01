// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Surface Mesh menu frame for MainWindow.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"

#include "imgui.h"

void MainWindow::render_menu_surface_mesh() {
    if (ImGui::BeginMenu("Surface Mesh")) {
        menu_group_ai_item(this, "Ask AI: which mesh tool should I use?",
                           "Surface Mesh",
                           "Choose repair, orientation, subdivision, simplification, smoothing, remeshing, UV, geodesic, or wrapping tools.",
                           "A current SurfaceMesh unless the command explicitly accepts point clouds or triangle soups.",
                           "Many direct commands modify topology or orientation; duplicate the model first if unsure.",
                           viewer_.current_model());
        ImGui::Separator();

        render_menu_surface_mesh_topology();
        ImGui::Separator();
        render_menu_surface_mesh_repair();
        ImGui::Separator();

        if (ImGui::MenuItem("Sampling..."))
            open_dialog(dlg_sm_sampling_, st_sm_sampling_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sample points on a mesh surface (random, uniform, or curvature-sensitive).\n"
                              "Outputs a new PointCloud. Opens a dialog for parameter selection.");
        ImGui::Separator();

        render_menu_surface_mesh_processing();

#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("MCF Skeletonization..."))
            open_dialog(dlg_mcf_, st_mcf_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mean Curvature Flow skeleton extraction (CGAL).\n"
                              "Closed triangle mesh, one connected component required.");

        if (ImGui::MenuItem("ARAP Deformation..."))
            open_dialog(dlg_arap_, st_arap_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("As-Rigid-As-Possible deformation (CGAL). Pick ROI, set control\n"
                              "handles, and deform the mesh while preserving local rigidity.");
#else
        menu_cgal_required_item(
            "MCF Skeletonization...",
            "Mean Curvature Flow skeleton extraction. Requires a closed triangle mesh with one connected component.");
        menu_cgal_required_item(
            "ARAP Deformation...",
            "As-Rigid-As-Possible deformation with ROI and control handles.");
#endif

        ImGui::Separator();
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("Alpha Wrapping 3D..."))
            dlg_alpha_wrap_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shrink-wrap a watertight mesh around any triangle soup or point cloud.\n"
                              "Output is guaranteed watertight and 2-manifold. (CGAL required)");
#else
        menu_cgal_required_item(
            "Alpha Wrapping 3D...",
            "Shrink-wrap a watertight mesh around a triangle soup or point cloud.");
#endif

        ImGui::EndMenu();
    }
}

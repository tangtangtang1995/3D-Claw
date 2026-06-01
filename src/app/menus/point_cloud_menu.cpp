// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Point Cloud menu for MainWindow.
//
// Keeps point-cloud preprocessing, reconstruction entry points, and direct
// Delaunay tools separate from the main menu dispatcher.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_point_cloud() {
    if (ImGui::BeginMenu("Point Cloud")) {
        menu_group_ai_item(this, "Ask AI: which point-cloud tool should I use?",
                           "Point Cloud",
                           "Choose normal estimation/orientation, simplification, reconstruction, primitive extraction, or triangulation.",
                           "A current PointCloud model.",
                           "Some commands create dense meshes or depend on reliable normals.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::MenuItem("Down Sampling..."))
            open_dialog(dlg_pc_simplify_, st_pc_simplify_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Simplify a point cloud by subsampling (random, grid, or geometry-aware).\n"
                              "Opens a dialog to choose method and target point count.");
        if (ImGui::MenuItem("Estimate Normals..."))
            open_dialog(dlg_pc_normals_, st_pc_normals_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Estimate per-point normals from local k-nearest-neighbor neighborhoods.\n"
                              "Opens a dialog for parameter selection. Required before Poisson or RANSAC.");
        ImGui::SetNextItemAllowOverlap();
        bool reorient_clicked = ImGui::MenuItem("Reorient Normals");
        bool reorient_hovered = ImGui::IsItemHovered();
        bool reorient_ai = menu_command_ai_tip(
            "Point Cloud > Reorient Normals",
            "Propagate a consistent normal direction through the point cloud.",
            "A current PointCloud with an existing v:normal property.",
            "Changes normal directions; Poisson and primitive fitting depend on this quality.",
            false, false, viewer_.current_model());
        if (reorient_clicked && !reorient_ai) {
            auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer_.current_model());
            if (claw3d::services::reorient_point_cloud_normals(cloud, 16)) {
                cloud->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "normals reoriented";
            }
        }
        if (reorient_hovered)
            ImGui::SetTooltip("Propagate consistent normal orientation via minimum spanning tree.\n"
                              "Uses 16 nearest neighbors. Requires v:normal attribute to exist first\n"
                              "(use Estimate Normals).");
        ImGui::SetNextItemAllowOverlap();
        bool normalize_clicked = ImGui::MenuItem("Normalize Normals");
        bool normalize_hovered = ImGui::IsItemHovered();
        bool normalize_ai = menu_command_ai_tip(
            "Point Cloud > Normalize Normals",
            "Rescale all point normals to unit length.",
            "A current PointCloud with an existing v:normal property.",
            "Usually safe and idempotent; does not fix wrong normal orientation.",
            false, false, viewer_.current_model());
        if (normalize_clicked && !normalize_ai) {
            auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer_.current_model());
            if (claw3d::services::normalize_point_cloud_normals(cloud)) {
                cloud->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "normals normalized";
            }
        }
        if (normalize_hovered)
            ImGui::SetTooltip("Rescale all normal vectors to unit length.\n"
                              "Requires v:normal attribute to exist. Harmless idempotent operation.");
        ImGui::Separator();
        if (ImGui::MenuItem("Poisson Surface Reconstruction..."))
            open_dialog(dlg_poisson_, st_poisson_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reconstruct a watertight triangle mesh from an oriented "
                              "point cloud using screened Poisson surface reconstruction.");
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("RANSAC Primitive Extraction..."))
            open_dialog(dlg_ransac_, st_ransac_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Efficient RANSAC primitive detection (plane-only).\n"
                              "Requires point cloud with normals.");
        if (ImGui::MenuItem("Region Growing..."))
            open_dialog(dlg_region_growing_, st_region_growing_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Region growing plane detection from seeds.\n"
                              "Produces connected, contiguous planar regions.\n"
                              "Requires point cloud with normals.");
#else
        menu_cgal_required_item(
            "RANSAC Primitive Extraction...",
            "Efficient RANSAC primitive detection (plane-only). Requires point cloud with normals.");
        menu_cgal_required_item(
            "Region Growing...",
            "Region growing plane detection from seeds. Requires point cloud with normals.");
#endif
        ImGui::Separator();
        render_menu_point_cloud_triangulation();
        ImGui::EndMenu();
    }
}

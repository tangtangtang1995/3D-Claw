// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Point Cloud triangulation menu commands.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_point_cloud_triangulation() {
    ImGui::SetNextItemAllowOverlap();
    bool delaunay2_clicked = ImGui::MenuItem("Delaunay Triangulation 2D");
    bool delaunay2_hovered = ImGui::IsItemHovered();
    bool delaunay2_ai = menu_command_ai_tip(
        "Point Cloud > Delaunay Triangulation 2D",
        "Triangulate XY coordinates and keep original Z values, producing a terrain-like TIN.",
        "A current PointCloud that is mostly a single-valued height field over XY.",
        "Not suitable for vertical walls, overhangs, or general 3D objects.",
        false, false, viewer_.current_model());
    if (delaunay2_clicked && !delaunay2_ai) {
        auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer_.current_model());
        if (cloud) {
            auto* mesh = claw3d::services::build_delaunay_xy_mesh(cloud);
            viewer_.add_model(mesh);
            viewer_.mark_dirty();
            LOG(INFO) << "Delaunay 2D done: " << mesh->n_faces() << " faces";
        }
    }
    if (delaunay2_hovered)
        ImGui::SetTooltip("Compute 2D Delaunay triangulation on XY coordinates (Shewchuk's Triangle).\n"
                          "Preserves original Z values. Outputs a triangle SurfaceMesh.\n"
                          "Useful for terrain/TIN generation from XY point sets.");

    ImGui::SetNextItemAllowOverlap();
    bool delaunay3_clicked = ImGui::MenuItem("Delaunay Triangulation 3D");
    bool delaunay3_hovered = ImGui::IsItemHovered();
    bool delaunay3_ai = menu_command_ai_tip(
        "Point Cloud > Delaunay Triangulation 3D",
        "Build a tetrahedral volumetric mesh from point positions.",
        "A current PointCloud with enough well-distributed 3D points.",
        "Can create many tetrahedra and use significant memory on dense clouds.",
        false, false, viewer_.current_model());
    if (delaunay3_clicked && !delaunay3_ai) {
        auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer_.current_model());
        if (cloud) {
            auto* mesh = claw3d::services::build_delaunay_tetra_mesh(cloud);
            viewer_.add_model(mesh);
            viewer_.mark_dirty();
            LOG(INFO) << "Delaunay 3D done: " << mesh->n_cells() << " cells";
        }
    }
    if (delaunay3_hovered)
        ImGui::SetTooltip("Compute 3D Delaunay tetrahedralization of points (Si's TetGen).\n"
                          "Outputs a PolyMesh with tetrahedral cells. Useful for volume meshing\n"
                          "and Voronoi diagram computation.");
}

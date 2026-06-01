// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_surface_mesh_topology() {
    ImGui::SetNextItemAllowOverlap();
    bool components_clicked = ImGui::MenuItem("Extract Connected Components");
    bool components_hovered = ImGui::IsItemHovered();
    bool components_ai = menu_command_ai_tip(
        "Surface Mesh > Extract Connected Components",
        "Identify connected face components and store the component labels as mesh properties.",
        "A current SurfaceMesh.",
        "Read-only segmentation property; later extraction may split the model.",
        false, false, viewer_.current_model());
    if (components_clicked && !components_ai) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::extract_connected_components(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "connected components extracted";
        }
    }
    if (components_hovered)
        ImGui::SetTooltip("Split the mesh into separate connected components.\n"
                          "Each component is identified by a face property.");

    ImGui::SetNextItemAllowOverlap();
    bool dual_clicked = ImGui::MenuItem("Dual");
    bool dual_hovered = ImGui::IsItemHovered();
    bool dual_ai = menu_command_ai_tip(
        "Surface Mesh > Dual",
        "Convert the mesh to its dual: faces become vertices and adjacency becomes edges.",
        "A current SurfaceMesh.",
        "Destructive topology conversion; properties are cleared.",
        false, false, viewer_.current_model());
    if (dual_clicked && !dual_ai) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_dual_mesh(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "dual mesh computed";
        }
    }
    if (dual_hovered)
        ImGui::SetTooltip("Convert each face to a vertex at its centroid.\n"
                          "Adjacent faces become edges in the dual. Face degrees map to vertex valences.\n"
                          "All existing properties are cleared.");

    if (ImGui::MenuItem("Planar Partition")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::compute_planar_partition(mesh, 1.0f)) {
            auto planar_segments = mesh->get_face_property<int>("f:planar_partition");
            auto coloring = mesh->face_property<easy3d::vec3>("f:color_planar_partition");
            easy3d::Renderer::color_from_segmentation(mesh, planar_segments, coloring);
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "planar partition done";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Segment the mesh into nearly-coplanar face patches by region growing.\n"
                          "Two adjacent faces belong to the same patch if their dihedral angle < 1 degree.\n"
                          "Colors each planar patch distinctly.");

    ImGui::Separator();

    if (ImGui::MenuItem("Polygonization")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_polygonization(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "polygonization done";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Merge coplanar adjacent triangles into general polygonal faces.\n"
                          "Reduces face count while preserving geometry. Uses dihedral angle < 1 deg\n"
                          "as coplanarity criterion.");

    ImGui::SetNextItemAllowOverlap();
    bool triangulate_clicked = ImGui::MenuItem("Triangulation");
    bool triangulate_hovered = ImGui::IsItemHovered();
    bool triangulate_ai = menu_command_ai_tip(
        "Surface Mesh > Triangulation",
        "Triangulate non-triangle faces using minimum-squared-area ear clipping.",
        "A current SurfaceMesh with polygonal faces.",
        "Changes face topology; useful before algorithms that require triangles.",
        false, false, viewer_.current_model());
    if (triangulate_clicked && !triangulate_ai) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_triangulation(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "triangulation done";
        }
    }
    if (triangulate_hovered)
        ImGui::SetTooltip("Triangulate all non-triangle faces using minimum-squared-area ear clipping\n"
                          "(from Liepa 2003 hole-filling paper). Ensures every face is a triangle.");

    if (ImGui::MenuItem("Tetrahedralization")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (auto* result = claw3d::services::build_tetrahedralization(mesh)) {
            viewer_.add_model(result);
            viewer_.mark_dirty();
            LOG(INFO) << "tetrahedralization done";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fill a closed surface mesh with quality tetrahedra using Si's TetGen.\n"
                          "Outputs a new PolyMesh with volumetric cells.\n"
                          "Requires a watertight closed triangle mesh as input.");
}

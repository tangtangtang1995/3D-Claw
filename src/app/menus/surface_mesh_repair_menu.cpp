// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_surface_mesh_repair() {
    if (ImGui::MenuItem("Stitch with Reorientation")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_surface_mesh_stitching(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "stitching done";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stitch coincident open boundary edges together with automatic reorientation.\n"
                          "Handles incompatible boundary cycles by reversing component orientation as needed.");

    if (ImGui::MenuItem("Stitch without Reorientation")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_surface_mesh_stitching(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "stitching (non-CGAL fallback) done";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stitch coincident open boundary edges without reorientation.\n"
                          "Merges borders while preserving existing face orientation.\n"
                          "Use when orientations are already consistent.");

    ImGui::SetNextItemAllowOverlap();
    bool reverse_clicked = ImGui::MenuItem("Reverse Orientation");
    bool reverse_hovered = ImGui::IsItemHovered();
    bool reverse_ai = menu_command_ai_tip(
        "Surface Mesh > Reverse Orientation",
        "Flip all face winding orders and therefore all face normals.",
        "A current SurfaceMesh.",
        "Can turn an outward-facing mesh inward; affects rendering and volume sign.",
        false, false, viewer_.current_model());
    if (reverse_clicked && !reverse_ai) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::reverse_surface_mesh_orientation(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "orientation reversed";
        }
    }
    if (reverse_hovered)
        ImGui::SetTooltip("Reverse the orientation of all faces (clockwise <-> counter-clockwise).\n"
                          "Flips all face normals. No geometry is changed.");

    ImGui::Separator();

    ImGui::SetNextItemAllowOverlap();
    bool remove_iso_clicked = ImGui::MenuItem("Remove Isolated Vertices");
    bool remove_iso_hovered = ImGui::IsItemHovered();
    bool remove_iso_ai = menu_command_ai_tip(
        "Surface Mesh > Remove Isolated Vertices",
        "Delete vertices that are not referenced by any face.",
        "A current SurfaceMesh.",
        "Usually safe cleanup; vertex indices may change after garbage collection.",
        false, false, viewer_.current_model());
    if (remove_iso_clicked && !remove_iso_ai) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::remove_isolated_vertices(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "isolated vertices removed";
        }
    }
    if (remove_iso_hovered)
        ImGui::SetTooltip("Delete vertices not referenced by any face.\n"
                          "Pure cleanup operation. Does not affect the visible surface.");

    if (ImGui::MenuItem("Orient and Stitch Polygon Soup")) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
        if (claw3d::services::apply_surface_mesh_stitching(mesh)) {
            mesh->renderer()->update();
            viewer_.mark_dirty();
            LOG(INFO) << "stitching done (non-CGAL fallback)";
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stitch open boundaries with reorientation. Uses Easy3D's SurfaceMeshStitching\n"
                          "as fallback when CGAL is unavailable.");
}

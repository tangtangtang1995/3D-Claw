// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Select menu for MainWindow.
//
// This file keeps selection-mode UI, bulk selection commands, and selection
// extraction/deletion entry points out of the general menu dispatcher.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"

#include <easy3d/util/logging.h>

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <vector>

void MainWindow::render_menu_select() {
    if (ImGui::BeginMenu("Select")) {
        menu_group_ai_item(this, "Ask AI: which selection mode should I use?",
                           "Select",
                           "Choose between vertex, face, point, rectangle, extraction, and deletion workflows.",
                           "A current model and an element type that matches the active selection mode.",
                           "Delete Selection permanently removes selected elements after confirmation.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::BeginMenu("Mode")) {
            if (ImGui::MenuItem("View / Navigate", "Esc",
                    selection_mode_ == SelectionMode::View))
                selection_mode_ = SelectionMode::View;
            ImGui::Separator();
            if (ImGui::MenuItem("Pick Mesh Vertex", nullptr,
                    selection_mode_ == SelectionMode::PickSurfaceVertex))
                selection_mode_ = SelectionMode::PickSurfaceVertex;
            if (ImGui::MenuItem("Pick Mesh Face", nullptr,
                    selection_mode_ == SelectionMode::PickSurfaceFace))
                selection_mode_ = SelectionMode::PickSurfaceFace;
            if (ImGui::MenuItem("Pick Point Cloud Point", nullptr,
                    selection_mode_ == SelectionMode::PickPointCloudPoint))
                selection_mode_ = SelectionMode::PickPointCloudPoint;
            ImGui::Separator();
            if (ImGui::MenuItem("Rectangle Mesh Faces", nullptr,
                    selection_mode_ == SelectionMode::RectangleSurfaceFace))
                selection_mode_ = SelectionMode::RectangleSurfaceFace;
            if (ImGui::MenuItem("Rectangle Point Cloud Points", nullptr,
                    selection_mode_ == SelectionMode::RectanglePointCloudPoint))
                selection_mode_ = SelectionMode::RectanglePointCloudPoint;
            ImGui::Separator();
            ImGui::BeginDisabled();
            ImGui::MenuItem("Lasso Mesh Faces", nullptr, false);
            ImGui::MenuItem("Lasso Point Cloud Points", nullptr, false);
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Selection mode: left-click picks, right-click deselects.\n"
                              "Esc returns to View/Navigate at any time.");
        ImGui::Separator();
        ImGui::SetNextItemAllowOverlap();
        bool select_all_clicked = ImGui::MenuItem("Select All", "Ctrl+A");
        bool select_all_ai = menu_command_ai_tip(
            "Select > Select All",
            "Select every element of the active selection type.",
            "A current model and a non-view selection mode, such as mesh faces or point-cloud points.",
            "Large selections can make later extract/delete operations expensive.",
            false, false, viewer_.current_model());
        if (select_all_clicked && !select_all_ai) {
            auto* model = viewer_.current_model();
            if (model && selection_mode_ != SelectionMode::View) {
                auto etype = [&]() -> SelectionElementType {
                    switch (selection_mode_) {
                        case SelectionMode::PickSurfaceFace:    return SelectionElementType::SurfaceFace;
                        case SelectionMode::PickSurfaceVertex:  return SelectionElementType::SurfaceVertex;
                        case SelectionMode::PickPointCloudPoint:return SelectionElementType::PointCloudPoint;
                        default: return SelectionElementType::None;
                    }
                }();
                auto* sel = selection_manager_.get(model);
                if (sel && etype != SelectionElementType::None) {
                    if (etype == SelectionElementType::SurfaceFace)
                        std::fill(sel->surface_faces.begin(), sel->surface_faces.end(), 1);
                    else if (etype == SelectionElementType::SurfaceVertex)
                        std::fill(sel->surface_vertices.begin(), sel->surface_vertices.end(), 1);
                    else if (etype == SelectionElementType::PointCloudPoint)
                        std::fill(sel->pointcloud_points.begin(), sel->pointcloud_points.end(), 1);
                    selection_manager_.bump_revision();
                    LOG(INFO) << "select all: " << selection_manager_.selected_count(model, etype) << " elements";
                }
            }
        }
        ImGui::SetNextItemAllowOverlap();
        bool invert_sel_clicked = ImGui::MenuItem("Invert Selection");
        bool invert_sel_ai = menu_command_ai_tip(
            "Select > Invert Selection",
            "Swap selected and unselected elements for the active selection type.",
            "A current model and an existing element-selection mode.",
            "On dense models this can select a very large number of elements.",
            false, false, viewer_.current_model());
        if (invert_sel_clicked && !invert_sel_ai) {
            auto* model = viewer_.current_model();
            if (model && selection_mode_ != SelectionMode::View) {
                auto etype = [&]() -> SelectionElementType {
                    switch (selection_mode_) {
                        case SelectionMode::PickSurfaceFace:    return SelectionElementType::SurfaceFace;
                        case SelectionMode::PickSurfaceVertex:  return SelectionElementType::SurfaceVertex;
                        case SelectionMode::PickPointCloudPoint:return SelectionElementType::PointCloudPoint;
                        default: return SelectionElementType::None;
                    }
                }();
                auto* sel = selection_manager_.get(model);
                if (sel && etype != SelectionElementType::None) {
                    auto invert_vec = [](std::vector<uint8_t>& v) {
                        for (auto& x : v) x = x ? 0 : 1;
                    };
                    if (etype == SelectionElementType::SurfaceFace)
                        invert_vec(sel->surface_faces);
                    else if (etype == SelectionElementType::SurfaceVertex)
                        invert_vec(sel->surface_vertices);
                    else if (etype == SelectionElementType::PointCloudPoint)
                        invert_vec(sel->pointcloud_points);
                    selection_manager_.bump_revision();
                    LOG(INFO) << "selection inverted: " << selection_manager_.selected_count(model, etype) << " elements";
                }
            }
        }
        ImGui::SetNextItemAllowOverlap();
        bool clear_sel_clicked = ImGui::MenuItem("Clear Selection");
        bool clear_sel_ai = menu_command_ai_tip(
            "Select > Clear Selection",
            "Remove all current selected vertices, faces, or points.",
            "A current model with an active selection.",
            "This only clears selection state; it does not change geometry.",
            false, false, viewer_.current_model());
        if (clear_sel_clicked && !clear_sel_ai) {
            auto* model = viewer_.current_model();
            if (model) {
                selection_manager_.clear_all_selections(model);
                selection_manager_.bump_revision();
                LOG(INFO) << "selection cleared for current model";
            }
        }
        ImGui::Separator();
        ImGui::SetNextItemAllowOverlap();
        bool extract_sel_clicked = ImGui::MenuItem("Extract Selection...");
        bool extract_sel_hovered = ImGui::IsItemHovered();
        bool extract_sel_ai = menu_command_ai_tip(
            "Select > Extract Selection",
            "Copy selected elements into a new child model.",
            "A current model with selected faces, vertices, or points.",
            "The original model is kept; output depends on the active selection type.",
            true, false, viewer_.current_model());
        if (extract_sel_clicked && !extract_sel_ai) {
            extract_selection();
        }
        if (extract_sel_hovered)
            ImGui::SetTooltip("Copy selected faces / vertices / points into a new\n"
                              "model attached as a child of the source.");
        ImGui::SetNextItemAllowOverlap();
        bool delete_sel_clicked = ImGui::MenuItem("Delete Selection...");
        bool delete_sel_hovered = ImGui::IsItemHovered();
        bool delete_sel_ai = menu_command_ai_tip(
            "Select > Delete Selection",
            "Delete selected elements from the current model after confirmation.",
            "A current model with selected faces, vertices, or points.",
            "Destructive: permanently changes the source model after confirmation.",
            false, false, viewer_.current_model());
        if (delete_sel_clicked && !delete_sel_ai)
            dlg_confirm_delete_sel_ = true;
        if (delete_sel_hovered)
            ImGui::SetTooltip("Permanently remove selected elements from the current\n"
                              "model. A confirmation dialog will be shown.");
        ImGui::Separator();
        if (ImGui::MenuItem("Show Selection Overlay", nullptr,
                            &interaction_overlay_.selection_visible)) {
            if (!interaction_overlay_.selection_visible)
                clear_selection_overlays();
            else
                selection_revision_ = -1;
        }
        ImGui::EndMenu();
    }
}

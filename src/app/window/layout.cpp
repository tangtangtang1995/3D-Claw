// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/surface_mesh.h>

#include "ui/layout_helpers.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <mutex>


// =============================================================================
// Status Bar
// =============================================================================
void MainWindow::render_status_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 28));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 28));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##StatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking);

    auto* model = viewer_.current_model();
    if (model) {
        if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model))
            ImGui::Text("F:%d  V:%d  E:%d  |  %s", mesh->n_faces(), mesh->n_vertices(), mesh->n_edges(), model->name().c_str());
        else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model))
            ImGui::Text("V:%d  |  %s", cloud->n_vertices(), model->name().c_str());
        else if (auto* graph = dynamic_cast<easy3d::Graph*>(model))
            ImGui::Text("V:%d  E:%d  |  %s", graph->n_vertices(), graph->n_edges(), model->name().c_str());
        else if (auto* pmesh = dynamic_cast<easy3d::PolyMesh*>(model))
            ImGui::Text("F:%d  V:%d  E:%d  C:%d  |  %s", pmesh->n_faces(), pmesh->n_vertices(), pmesh->n_edges(), pmesh->n_cells(), model->name().c_str());
    } else {
        ImGui::Text("Ready");
    }

    if (selection_mode_ != SelectionMode::View) {
        ImGui::SameLine();
        ImGui::TextColored(claw_ui::status_warning_color(), "|  SEL:");
        ImGui::SameLine(0, 2);
        const char* mode_str = "???";
        switch (selection_mode_) {
            case SelectionMode::PickSurfaceVertex:       mode_str = "Pick Vertex"; break;
            case SelectionMode::PickSurfaceFace:         mode_str = "Pick Face"; break;
            case SelectionMode::PickPointCloudPoint:     mode_str = "Pick Point"; break;
            case SelectionMode::RectangleSurfaceFace:    mode_str = "Rect Faces"; break;
            case SelectionMode::RectanglePointCloudPoint: mode_str = "Rect Points"; break;
            default: break;
        }
        ImGui::Text("%s", mode_str);
    }

    claw_ui::same_line_right_if_fits_text("000 FPS");
    ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);

    ImGui::End();
    ImGui::PopStyleVar(2);
}


void MainWindow::update_status_bar() {}


void MainWindow::setup_dockspace_layout() {
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    // Left column | Viewport | AI column
    ImGuiID dock_left, dock_rest;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, &dock_left, &dock_rest);

    ImGuiID dock_ai, dock_center;
    ImGui::DockBuilderSplitNode(dock_rest, ImGuiDir_Right, 0.35f, &dock_ai, &dock_center);

    // Left column: Model List (top 60%) | Properties+Log (bottom 40%)
    ImGuiID dock_left_top, dock_left_bottom;
    ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Up, 0.60f, &dock_left_top, &dock_left_bottom);

    // Right (AI) column: Chat (top 55%) | 3D Generation (bottom 45%)
    ImGuiID dock_ai_top, dock_ai_bottom;
    ImGui::DockBuilderSplitNode(dock_ai, ImGuiDir_Up, 0.55f, &dock_ai_top, &dock_ai_bottom);

    ImGui::DockBuilderDockWindow("Model List", dock_left_top);
    ImGui::DockBuilderDockWindow("Properties", dock_left_bottom);
    ImGui::DockBuilderDockWindow("Log", dock_left_bottom);             // tab with Properties
    ImGui::DockBuilderDockWindow("Health Report", dock_left_bottom);   // tab with Properties
    ImGui::DockBuilderDockWindow("History", dock_left_bottom);         // tab with Properties
    ImGui::DockBuilderDockWindow("Viewport", dock_center);
    ImGui::DockBuilderDockWindow("AI Chat", dock_ai_top);
    ImGui::DockBuilderDockWindow("AI 3D Generation (Hunyuan)", dock_ai_bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}


void MainWindow::send(el::Level level, const std::string& msg) {
    const char* lvl = "INFO";
    if (level == el::Level::Warning) lvl = "WARNING";
    else if (level == el::Level::Error) lvl = "ERROR";
    else if (level == el::Level::Fatal) lvl = "FATAL";

    // Logger callbacks may fire from any thread (e.g. async file loader).
    // g_log_entries is also read by the UI thread in renderWidgetLog.
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_entries.push_back({lvl, msg});

    // Keep only the last 500 entries to prevent unbounded growth
    if (g_log_entries.size() > 500)
        g_log_entries.erase(g_log_entries.begin(), g_log_entries.begin() + 100);
}


// =============================================================================
// Widget Panels
// =============================================================================
void MainWindow::render_widgets() {
    if (wgt_model_list_) renderWidgetModelList(viewer(), st_model_list_, wgt_model_list_);
    if (wgt_properties_) renderWidgetProperties(viewer(), st_properties_panel_, wgt_properties_);
    if (wgt_log_)        renderWidgetLog(viewer(), st_log_, wgt_log_);
    if (wgt_health_)     renderWidgetHealthReport(viewer(), st_health_, wgt_health_);
    if (wgt_history_)    renderWidgetHistory(viewer(), st_history_, wgt_history_);
    if (dlg_settings_)   renderSettingsDialog(viewer(), dlg_settings_);
}

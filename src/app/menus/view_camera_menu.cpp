// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// View, Camera, and Window menu sections for MainWindow.
//
// View owns display toggles and nests Camera as a submenu. Window owns the
// dock-panel toggles that previously lived in the View menu.

#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "ui/walk_through.h"

#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/util/dialog.h>

#include <GLFW/glfw3.h>
#include "imgui.h"


void MainWindow::render_menu_view() {
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit Screen", "F"))       menu_view_fit_screen();
        if (ImGui::MenuItem("Snapshot..."))            open_dialog(dlg_snapshot_, st_snapshot_);
        if (ImGui::MenuItem("Display Settings..."))    dlg_settings_ = true;
        ImGui::Separator();
        ImGui::MenuItem("Show Backend Logo", nullptr, &show_backend_logo_);
        ImGui::MenuItem("Show Frame Rate", nullptr, &show_frame_rate_);
        ImGui::MenuItem("Show Axes Gizmo", nullptr, &viewer_.show_axes_gizmo_ref());
        ImGui::MenuItem("Show Selection BBox", nullptr, &viewer_.show_selection_bbox_ref());
        ImGui::Separator();
        ImGui::MenuItem("Show Primitive ID Under Mouse", nullptr, &show_primitive_id_under_mouse_);
        ImGui::MenuItem("Show Coordinates Under Mouse", nullptr, &show_coordinates_under_mouse_);
        ImGui::Separator();
        render_menu_camera();
        ImGui::EndMenu();
    }
}


void MainWindow::render_menu_window() {
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Model List", nullptr, &wgt_model_list_);
        ImGui::MenuItem("Properties", nullptr, &wgt_properties_);
        ImGui::MenuItem("Log", nullptr, &wgt_log_);
        ImGui::MenuItem("Health Report", nullptr, &wgt_health_);
        ImGui::MenuItem("History", nullptr, &wgt_history_);
        ImGui::Separator();
        ImGui::MenuItem("AI Chat", nullptr, &dlg_ai_);
        ImGui::MenuItem("AI 3D Generation", nullptr, &dlg_3dgen_);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) setup_dockspace_layout();
        ImGui::EndMenu();
    }
}


void MainWindow::render_menu_camera() {
    if (ImGui::BeginMenu("Camera")) {
        if (ImGui::MenuItem("Copy Camera"))     viewer_.copy_camera();
        if (ImGui::MenuItem("Paste Camera"))    viewer_.paste_camera();
        ImGui::Separator();
        if (ImGui::MenuItem("Save Camera State...")) {
            auto path = easy3d::dialog::save(
                "Save Camera State", "camera.view",
                {"View Files (*.view)", "*.view"});
            if (!path.empty()) viewer_.save_camera_state(path);
        }
        if (ImGui::MenuItem("Restore Camera State...")) {
            auto path = easy3d::dialog::open(
                "Restore Camera State", "",
                {"View Files (*.view)", "*.view"});
            if (!path.empty()) viewer_.restore_camera_state(path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Camera Path...")) {
            auto path = easy3d::dialog::open(
                "Import Camera Path", "",
                {"Keyframe Files (*.kf)", "*.kf"});
            if (!path.empty() && walk_through_)
                walk_through_->interpolator()->read_keyframes(path);
        }
        if (ImGui::MenuItem("Export Camera Path...")) {
            auto path = easy3d::dialog::save(
                "Export Camera Path", "path.kf",
                {"Keyframe Files (*.kf)", "*.kf"});
            if (!path.empty() && walk_through_)
                walk_through_->interpolator()->save_keyframes(path);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Animation / Walk Through..."))
            open_dialog(dlg_animation_, st_animation_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create and preview camera paths. Three modes:\n"
                              "- Free: add keyframes from current viewpoints.\n"
                              "- Walking: simulate character walking through scene.\n"
                              "- Rotate Around Axis: automatic turntable animation.");
        ImGui::EndMenu();
    }
}

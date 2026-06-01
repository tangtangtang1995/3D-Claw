// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"

#include "window/window_helpers.h"
#include "ui/ui_scale.h"

#include <utility>
#include <vector>

#include <easy3d/util/logging.h>

#include "imgui.h"

bool MainWindow::process_pending_file_upload() {
    std::vector<std::unique_ptr<easy3d::Model>> models_to_upload;
    const bool do_upload_this_frame =
        file_loader_.process_pending_upload(models_to_upload);
    if (do_upload_this_frame) {
        for (auto& holder : models_to_upload) {
            auto* m = holder.release();
            if (!m)
                continue;
            viewer_.add_model(m);
            apply_surface_mesh_texture(m);
            viewer_.fit_screen(m);
        }
        viewer_.mark_dirty();
        update_status_bar();
        sync_selection_manager_with_viewer();
    }

    return do_upload_this_frame;
}

void MainWindow::sync_selection_manager_with_viewer() {
    std::vector<easy3d::Model*> all;
    for (auto& m : viewer_.models())
        all.push_back(m.get());
    selection_manager_.sync_with_viewer(all);
}

void MainWindow::wire_viewer_interaction_state() {
    viewer_.set_selection_state(&selection_mode_, &selection_manager_);
    viewer_.set_measurement_state(&st_measure_);
    viewer_.set_crop_state(dlg_crop_ ? &st_crop_ : nullptr);
    viewer_.set_walk_through(walk_through_.get());
}

void MainWindow::handle_global_keyboard_shortcuts() {
    if (selection_mode_ != SelectionMode::View &&
        ImGui::IsKeyPressed(ImGuiKey_Escape) &&
        !ImGui::GetIO().WantTextInput) {
        selection_mode_ = SelectionMode::View;
        LOG(INFO) << "selection mode: View / Navigate";
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))
            UIScale::instance().set_scale(UIScale::instance().scale() + 0.1f);
        else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
            UIScale::instance().set_scale(UIScale::instance().scale() - 0.1f);
        else if (ImGui::IsKeyPressed(ImGuiKey_0, false))
            UIScale::instance().set_scale(1.0f);
    }
}

void MainWindow::handle_global_mouse_wheel_scale() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.MouseWheel != 0.0f && !io.WantTextInput) {
        UIScale::instance().set_scale(
            UIScale::instance().scale() + io.MouseWheel * 0.1f);
        io.MouseWheel = 0.0f;
    }
}

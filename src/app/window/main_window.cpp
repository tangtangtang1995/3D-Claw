// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "ui/walk_through.h"

// AI Chat panel rendering + markdown helpers + send_ai_request moved to
// ai_chat_panel.cpp.
// apply_surface_mesh_texture + SurfaceMeshAttributeCopier moved to
// main_window_helpers.{h,cpp} so other split TUs (selection_ops, files,
// etc.) can use them without duplicating the implementation.
// Runtime loading/uploading, welcome dialog, and status overlays moved to
// runtime.cpp.

MainWindow* MainWindow::s_instance_ = nullptr;

MainWindow::MainWindow() {
    s_instance_ = this;
    walk_through_ = std::make_unique<WalkThrough>(viewer_.camera());
}

MainWindow::~MainWindow() {
    st_3dgen_.stop_worker();
    algorithm_.join_worker();
    file_loader_.join_worker();
}

void MainWindow::render() {
    if (first_frame_) {
        setup_dockspace_layout();
        first_frame_ = false;
    }

    const bool do_upload_this_frame = process_pending_file_upload();

    record_algorithm_start_if_needed();
    process_algorithm_completion();

    sync_selection_manager_with_viewer();
    wire_viewer_interaction_state();

    render_menu_bar();
    handle_global_keyboard_shortcuts();

    viewer_.render();
    update_selection_overlays();
    if (do_upload_this_frame)
        file_loader_.finish_upload();

    render_widgets();
    render_dialogs();
    if (dlg_ai_)
        renderAIPanel();
    render_status_bar();

    render_welcome_dialog();
    handle_global_mouse_wheel_scale();
    render_runtime_status_overlays();
}

// Dialog dispatch + About/open_named_dialog moved to
// dialog_dispatch.cpp.
// send_ai_request and renderAIPanel moved to ai_chat_panel.cpp.
// Selection / measurement / crop / align-gizmo overlays moved to
// selection_overlays.cpp.
// Layout/status/logger/widget dispatch moved to layout.cpp.

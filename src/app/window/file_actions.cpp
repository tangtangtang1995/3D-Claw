// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// File menu actions for MainWindow (Open / Save / async load worker).
// Extracted out of main_window.cpp during the 2024-2025 split  - the
// monolithic file was 6700+ lines and very hard to navigate.

#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "io/surface_mesh_io.h"
#include "platform/window_events.h"
#include "services/resources/resource_paths.h"
#include "util/thread_priority.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/fileio/point_cloud_io.h>
#include <easy3d/fileio/graph_io.h>
#include <easy3d/fileio/translator.h>
#include <easy3d/util/dialog.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

#include <memory>
#include <thread>
#include <utility>
#include <vector>


void MainWindow::render_menu_file() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            menu_file_open();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            menu_file_save();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            claw3d::app::request_window_close();
        ImGui::EndMenu();
    }
}


void MainWindow::menu_file_open() {
    if (!file_loader_.can_start()) return;

    auto files = easy3d::dialog::open("Open Model",
        claw3d::resources::easy3d_data_directory() + "/",
        {"Mesh Files (*.ply *.obj *.stl *.off *.glb *.gltf)", "*.ply *.obj *.stl *.off *.glb *.gltf",
         "Point Cloud Files (*.xyz *.bin *.ply)", "*.xyz *.bin *.ply",
         "All Files (*.*)", "*"}, true);
    if (files.empty()) return;

    load_files_async(std::move(files));
}


void MainWindow::load_files_async(std::vector<std::string> filenames) {
    if (filenames.empty()) return;
    if (!file_loader_.can_start()) return;

    auto files = std::make_shared<std::vector<std::string>>(std::move(filenames));
    file_loader_.begin_loading();

    file_loader_.start_worker(std::thread([this, files]() {
        // Lower this thread's priority so the UI thread (rendering at ~30fps
        // while the loading overlay is up) is not starved on busy systems.
        claw3d::app::lower_current_thread_priority();
        // Disable Translator to avoid thread-unsafe global state
        easy3d::Translator::instance()->set_status(easy3d::Translator::DISABLED);
        std::vector<std::unique_ptr<easy3d::Model>> models;
        for (const auto& f : *files) {
            std::unique_ptr<easy3d::Model> m(
                ViewportCanvas::load_model_file(f));
            if (m) models.push_back(std::move(m));
        }
        file_loader_.finish_loading(std::move(models));
        // Wake the main thread so it picks up the result immediately,
        // even if it's currently blocked waiting for window events.
        claw3d::app::wake_event_loop();
    }));
}


void MainWindow::menu_file_save() {
    auto* model = viewer_.current_model();
    if (!model) { LOG(ERROR) << "no model exists"; return; }

    std::string default_name = model->name();
    if (easy3d::file_system::extension(default_name).empty())
        default_name += ".ply";

    auto path = easy3d::dialog::save("Save Model", default_name,
        {"PLY Files (*.ply)", "*.ply", "OBJ Files (*.obj)", "*.obj",
         "STL Files (*.stl)", "*.stl", "OFF Files (*.off)", "*.off",
         "All Files (*.*)", "*"});
    if (path.empty()) return;

    bool saved = false;
    if (dynamic_cast<easy3d::SurfaceMesh*>(model))
        saved = claw3d::io::save_surface_mesh(
            path, dynamic_cast<easy3d::SurfaceMesh*>(model));
    else if (dynamic_cast<easy3d::PointCloud*>(model))
        saved = easy3d::PointCloudIO::save(path, dynamic_cast<easy3d::PointCloud*>(model));
    else if (dynamic_cast<easy3d::Graph*>(model))
        saved = easy3d::GraphIO::save(path, dynamic_cast<easy3d::Graph*>(model));

    LOG(INFO) << (saved ? "saved: " + path : "save failed");
}

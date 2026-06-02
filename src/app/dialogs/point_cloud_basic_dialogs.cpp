// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "platform/window_events.h"
#include "services/jobs/easy3d/easy3d_point_cloud_jobs.h"
#include "ui/layout_helpers.h"
#include "ui/status_widgets.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

void renderDialogPointCloudSimplify(ViewportCanvas* viewer, PointCloudSimplifyState& s, bool& open) {
    prepare_dialog_window(420, 220);
    DIALOG_BODY("Point Cloud Simplification", open) {
        auto pq = prereq_point_cloud(viewer);
        ImGui::InputFloat("Cell Size", &s.radius, 0.0f, 0.0f, "%.4f");
        prereq_begin(pq);
        if (ImGui::Button("Query")) {
            s.found_points = true;
            LOG(INFO) << "grid simplification with cell size " << s.radius;
        }
        if (s.found_points) ImGui::Text("Points found. Click Apply to remove.");
        claw_ui::same_line_if_fits_button("Apply");
        if (ImGui::Button("Apply") && s.found_points) {
            auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer->current_model());
            const auto removed =
                claw3d::services::apply_point_cloud_grid_simplification(
                    cloud, s.radius);
            cloud->renderer()->update();
            viewer->mark_dirty();
            s.found_points = false;
            LOG(INFO) << "removed " << removed << " points";
        }
        prereq_end(pq);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}

void renderDialogPointCloudNormalEstimation(ViewportCanvas* viewer, PointCloudNormalEstimationState& s, bool& open) {
    prepare_dialog_window(420, 240);
    DIALOG_BODY("Point Cloud Normal Estimation", open) {
        auto pq = prereq_point_cloud(viewer);
        ImGui::InputInt("K Neighbors", &s.k);
        if (s.k < 3) s.k = 3;
        ImGui::Checkbox("Reorient Normals", &s.reorient);
        ImGui::Checkbox("Normalize", &s.normalize);
        auto* win = MainWindow::instance();
        const bool busy = win && win->algorithm_controller().is_running();
        if (busy)
            ImGui::BeginDisabled();
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::PointCloudNormalEstimationJobStart request;
                request.source_cloud = cloud;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(cloud)
                        : ModelHandle{};
                request.k_neighbors = s.k;
                request.reorient = s.reorient;
                request.normalize = s.normalize;
                request.source_name = cloud->name();
                request.wake_ui = []() { claw3d::app::wake_event_loop(); };
                if (!claw3d::services::start_point_cloud_normal_estimation_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start point cloud normal estimation";
                }
            }
        }
        prereq_end(pq);
        if (busy)
            ImGui::EndDisabled();
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        if (win) {
            claw_ui::render_algorithm_status_panel(
                win->algorithm_controller(),
                AlgorithmId::PointCloudNormalEstimation,
                "Point Cloud Normal Estimation");
        }
    } DIALOG_END;
}

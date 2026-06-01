// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "services/jobs/easy3d/easy3d_surface_mesh_jobs.h"
#include "ui/layout_helpers.h"
#include "ui/status_widgets.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <GLFW/glfw3.h>

namespace {

bool begin_start_guard(MainWindow* win) {
    if (win && win->algorithm_controller().is_running()) {
        ImGui::BeginDisabled();
        return true;
    }
    return false;
}

void end_start_guard(bool guard) {
    if (guard)
        ImGui::EndDisabled();
}

void render_job_status(MainWindow* win,
                       AlgorithmId id,
                       const char* fallback_label) {
    if (!win)
        return;
    claw_ui::render_algorithm_status_panel(
        win->algorithm_controller(), id, fallback_label);
}

} // namespace

void renderDialogSurfaceMeshCurvature(ViewportCanvas* viewer, SurfaceMeshCurvatureState& s, bool& open) {
    prepare_dialog_window(420, 220);
    DIALOG_BODY("Surface Mesh Curvature", open) {
        auto pq = prereq_surface_mesh(viewer);
        ImGui::InputInt("Post-smoothing Iterations", &s.smooth_iters);
        ImGui::Checkbox("Use 2-Ring Neighborhood", &s.two_ring);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (claw3d::services::apply_surface_mesh_curvature(
                    mesh, s.smooth_iters, s.two_ring)) {
                mesh->renderer()->update();
                viewer->mark_dirty();
                LOG(INFO) << "curvature computed";
            }
        }
        prereq_end(pq);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}

void renderDialogSurfaceMeshSampling(ViewportCanvas* viewer, SurfaceMeshSamplingState& s, bool& open) {
    prepare_dialog_window(440, 220);
    DIALOG_BODY("Surface Mesh Sampling", open) {
        auto pq = prereq_surface_mesh(viewer);
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
        int min_pts = mesh ? (int)mesh->n_vertices() : 0;
        ImGui::InputInt("Number of Points", &s.num_points);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Target number of sampled points.\nMust be >= %d (current vertex count).", min_pts);

        bool valid = mesh && (s.num_points >= min_pts);
        if (mesh && !valid) {
            claw_ui::same_line_if_fits_text("Must be >= 000000 (vertex count)");
            ImGui::TextColored(claw_ui::status_error_color(),
                "Must be >= %d (vertex count)", min_pts);
        }

        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            int np = valid ? s.num_points : min_pts;
            if (!valid) s.num_points = min_pts;
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshSamplingJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.target_points = np;
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_sampling_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh sampling";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshSampling,
                          "Surface Mesh Sampling");
    } DIALOG_END;
}

void renderDialogSurfaceMeshSimplification(ViewportCanvas* viewer, SurfaceMeshSimplificationState& s, bool& open) {
    prepare_dialog_window(420, 200);
    DIALOG_BODY("Surface Mesh Simplification", open) {
        auto pq = prereq_surface_mesh(viewer);
        ImGui::InputInt("Target Vertex Count", &s.target_vertices);
        if (s.target_vertices < 4) s.target_vertices = 4;
        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshSimplificationJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.target_vertices = s.target_vertices;
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_simplification_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh simplification";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshSimplification,
                          "Surface Mesh Simplification");
    } DIALOG_END;
}

void renderDialogSurfaceMeshSmoothing(ViewportCanvas* viewer, SurfaceMeshSmoothingState& s, bool& open) {
    prepare_dialog_window(420, 240);
    DIALOG_BODY("Surface Mesh Smoothing", open) {
        auto pq = prereq_surface_mesh(viewer);
        const char* schemes[] = {"Explicit Smoothing", "Implicit Smoothing"};
        ImGui::Combo("Scheme", &s.scheme, schemes, 2);
        ImGui::InputInt("Iterations", &s.iterations);
        ImGui::Checkbox("Uniform Laplace", &s.uniform);
        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshSmoothingJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.scheme = s.scheme;
                request.iterations = s.iterations;
                request.uniform_laplace = s.uniform;
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_smoothing_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh smoothing";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshSmoothing,
                          "Surface Mesh Smoothing");
    } DIALOG_END;
}

void renderDialogSurfaceMeshFairing(ViewportCanvas* viewer, SurfaceMeshFairingState& s, bool& open) {
    prepare_dialog_window(400, 180);
    DIALOG_BODY("Surface Mesh Fairing", open) {
        auto pq = prereq_surface_mesh(viewer);
        const char* criteria[] = {"Minimize Area", "Minimize Curvature"};
        ImGui::Combo("Criterion", &s.criterion, criteria, 2);
        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshFairingJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.criterion = s.criterion;
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_fairing_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh fairing";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshFairing,
                          "Surface Mesh Fairing");
    } DIALOG_END;
}

void renderDialogSurfaceMeshHoleFilling(ViewportCanvas* viewer, SurfaceMeshHoleFillingState&, bool& open) {
    prepare_dialog_window(380, 160);
    DIALOG_BODY("Surface Mesh Hole Filling", open) {
        auto pq = prereq_surface_mesh(viewer);
        ImGui::TextWrapped("Fills all boundary loops of the current mesh with triangles.");
        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshHoleFillingJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_hole_filling_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh hole filling";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshHoleFilling,
                          "Surface Mesh Hole Filling");
    } DIALOG_END;
}

void renderDialogSurfaceMeshParameterization(ViewportCanvas* viewer, SurfaceMeshParameterizationState& s, bool& open) {
    prepare_dialog_window(400, 180);
    DIALOG_BODY("Surface Mesh Parameterization", open) {
        auto pq = prereq_surface_mesh(viewer);
        const char* methods[] = {"LSCM", "Discrete Harmonic"};
        ImGui::Combo("Method", &s.method, methods, 2);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (claw3d::services::apply_surface_mesh_parameterization(
                    mesh, s.method)) {
                mesh->renderer()->update();
                viewer->mark_dirty();
                LOG(INFO) << "parameterization done";
            }
        }
        prereq_end(pq);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}

void renderDialogSurfaceMeshRemeshing(ViewportCanvas* viewer, SurfaceMeshRemeshingState& s, bool& open) {
    prepare_dialog_window(440, 260);
    DIALOG_BODY("Surface Mesh Remeshing", open) {
        auto pq = prereq_surface_mesh(viewer);
        const char* schemes[] = {"Uniform Remeshing", "Adaptive Remeshing"};
        ImGui::Combo("Scheme", &s.scheme, schemes, 2);
        ImGui::InputFloat("Edge Length", &s.edge_length, 0.0f, 0.0f, "%.4f");
        ImGui::Checkbox("Use Features", &s.use_features);
        if (s.use_features) ImGui::InputInt("Feature Angle", &s.feature_angle);
        auto* win = MainWindow::instance();
        const bool guarded = begin_start_guard(win);
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (win && !win->algorithm_controller().is_running()) {
                claw3d::services::SurfaceMeshRemeshingJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.scheme = s.scheme;
                request.feature_angle = s.feature_angle;
                request.edge_length = s.edge_length;
                request.use_features = s.use_features;
                request.source_name = mesh->name();
                request.wake_ui = []() { glfwPostEmptyEvent(); };
                if (!claw3d::services::start_surface_mesh_remeshing_job(
                        win->algorithm_controller(), request)) {
                    LOG(WARNING) << "Failed to start surface mesh remeshing";
                }
            }
        }
        prereq_end(pq);
        end_start_guard(guarded);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
        render_job_status(win, AlgorithmId::SurfaceMeshRemeshing,
                          "Surface Mesh Remeshing");
    } DIALOG_END;
}

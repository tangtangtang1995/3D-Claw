// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/cgal_smoothing_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/cgal_smoothing_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "overlays/overlay_controller.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"
#include "ui/ui_frame_gate.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static const char* SMOOTH_HELP_PROMPT =
    "I am using CGAL Polygon Mesh Processing smoothing in 3D Claw.\n\n"
    "Three modes are available:\n"
    "  Tangential Relaxation - redistribute vertices tangentially on the\n"
    "    surface. Improves triangle distribution. Shape stays almost\n"
    "    unchanged. Good default after remeshing or simplification.\n"
    "  Angle Smoothing - improve triangle angle distribution. Reduces\n"
    "    skinny / poor-quality triangles. Mesh quality focused, not\n"
    "    denoising.\n"
    "  Mean Curvature Flow - smooth visible surface noise. Can shrink the\n"
    "    mesh or round sharp features. Best for organic / scanned data.\n\n"
    "Please suggest a mode, iteration count, and constraint settings for\n"
    "my mesh.";

namespace {

const char* mode_label(int m) {
    switch (m) {
    case SMOOTH_MODE_TangentialRelaxation: return "Tangential Relaxation";
    case SMOOTH_MODE_AngleSmoothing:       return "Angle Smoothing";
    case SMOOTH_MODE_MeanCurvatureFlow:    return "Mean Curvature Flow";
    case SMOOTH_MODE_AngleArea:            return "Angle + Area (disabled)";
    default:                                return "Unknown";
    }
}

const char* mode_hint(int m) {
    switch (m) {
    case SMOOTH_MODE_TangentialRelaxation:
        return "Safe default. Improves triangle distribution while keeping the "
               "shape almost unchanged. Use after remeshing / simplification. "
               "Keep sharp features constrained for CAD-like models.";
    case SMOOTH_MODE_AngleSmoothing:
        return "Fixes bad triangle angles. Less about denoising the visible "
               "shape, more about making the mesh healthier. Use safety "
               "constraints when the mesh has thin triangles.";
    case SMOOTH_MODE_MeanCurvatureFlow:
        return "Best for visible surface denoising. Start with a small time "
               "step. Can round sharp features, so keep boundaries and sharp "
               "edges constrained when structure matters.";
    case SMOOTH_MODE_AngleArea:
        return "Disabled in this build (requires Ceres support).";
    default:
        return "";
    }
}

using claw_ai::ascii_only;

void append_parameter_block(std::ostringstream& oss, const SmoothingState& s) {
    oss << "Current Smoothing parameters:\n";
    oss << "- Mode: " << mode_label(s.mode) << "\n";
    oss << "- Iterations: " << s.iterations << "\n";
    if (s.mode == SMOOTH_MODE_MeanCurvatureFlow)
        oss << "- Time step: " << s.time_step << "\n";
    oss << "- Preserve boundary: " << (s.preserve_boundary ? "yes" : "no") << "\n";
    oss << "- Preserve sharp edges: " << (s.preserve_sharp_edges ? "yes" : "no")
        << " (sharp angle = " << s.sharp_angle_degrees << " deg)\n";
    if (s.mode == SMOOTH_MODE_TangentialRelaxation)
        oss << "- Relax constraints: " << (s.relax_constraints ? "yes" : "no") << "\n";
    if (s.mode == SMOOTH_MODE_AngleSmoothing) {
        oss << "- Safety constraints: " << (s.safety_constraints ? "yes" : "no") << "\n";
        oss << "- Project to original surface: "
            << (s.project_to_original ? "yes" : "no") << "\n";
    }
    if (s.mode == SMOOTH_MODE_MeanCurvatureFlow)
        oss << "- Rescale after smoothing: "
            << (s.rescale_after_smoothing ? "yes" : "no") << "\n";
}

std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const SmoothingState& s)
{
    std::ostringstream oss;
    oss << "Please give parameter advice for CGAL Mesh Smoothing in 3D Claw.\n\n";
    append_parameter_block(oss, s);
    oss << "\n";
    claw_ai::append_surface_mesh_metadata(
        oss, claw_ai::collect_surface_mesh_ai_stats(mesh));
    oss << "\nKeep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Recommend a mode (Tangential / Angle / MCF) for this mesh.\n";
    oss << "2. Recommend an iteration count (conservative / moderate / aggressive).\n";
    oss << "3. For MCF: recommend a starting time step.\n";
    oss << "4. Should boundary and sharp features be preserved?\n";
    oss << "5. Risks: over-smoothing, shrinkage, sharp feature loss, etc.\n";
    return ascii_only(oss.str());
}

std::string build_result_evaluation_prompt(const SmoothingState& s,
                                           const SMOOTH_ResultStats& st)
{
    std::ostringstream oss;
    oss << "Please evaluate this CGAL Mesh Smoothing run in 3D Claw.\n\n";
    SmoothingState lr = s;
    lr.mode = s.last_run_mode;
    lr.iterations = s.last_run_iterations;
    lr.time_step = s.last_run_time_step;
    lr.preserve_boundary = s.last_run_preserve_boundary;
    lr.preserve_sharp_edges = s.last_run_preserve_sharp;
    lr.sharp_angle_degrees = s.last_run_sharp_angle;
    append_parameter_block(oss, lr);
    oss << "\n";
    if (!s.last_input_metadata_prompt.empty())
        oss << s.last_input_metadata_prompt << "\n";
    oss << std::fixed << std::setprecision(6);
    oss << "Run statistics:\n";
    oss << "- Input/output counts: V=" << st.before.vertices
        << " F=" << st.before.faces << " (same after - connectivity unchanged)\n";
    oss << "- Iterations completed: " << st.iterations_done << "\n";
    oss << "- Before: minA=" << st.before.min_angle << "deg meanA="
        << st.before.mean_angle << "deg bad<10=" << st.before.bad_triangles_10deg
        << " bad<15=" << st.before.bad_triangles_15deg
        << " maxAR=" << st.before.max_aspect_ratio << "\n";
    oss << "- After : minA=" << st.after.min_angle << "deg meanA="
        << st.after.mean_angle << "deg bad<10=" << st.after.bad_triangles_10deg
        << " bad<15=" << st.after.bad_triangles_15deg
        << " maxAR=" << st.after.max_aspect_ratio << "\n";
    oss << "- Surface area ratio: " << st.surface_area_ratio
        << " | Volume ratio: " << st.volume_ratio
        << " | Closed: " << (st.closed_mesh ? "yes" : "no") << "\n";
    oss << "- Mean displacement: " << st.mean_displacement
        << " | Max displacement: " << st.max_displacement;
    if (st.before.bbox_diag > 0.0)
        oss << " (max/bbox = " << (st.max_displacement / st.before.bbox_diag * 100.0)
            << "%)";
    oss << "\n";
    oss << "- Runtime ms: total=" << st.ms_total
        << " preprocess=" << st.ms_preprocess
        << " smoothing=" << st.ms_smoothing << "\n";
    oss << "- Cancelled: " << (st.cancelled ? "yes" : "no") << "\n\n";
    oss.unsetf(std::ios::floatfield);
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Did smoothing improve the intended quality metric?\n";
    oss << "2. Is the smoothing conservative or aggressive?\n";
    oss << "3. Any evidence of over-smoothing / shrinkage / feature loss?\n";
    oss << "4. What should the user try next?\n";
    return ascii_only(oss.str());
}

} // namespace

void renderDialogCGALSmoothing(ViewportCanvas* viewer, SmoothingState& s, bool& open) {
    prepare_dialog_window(520, 480);
    DIALOG_BODY("CGAL Mesh Smoothing", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        const bool smooth_busy =
            s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::CgalSmoothing);
        if (!open && smooth_busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            if (win)
                win->algorithm_controller().request_cancel();
            claw3d::app::wake_event_loop();
        }

        render_panel_header(
            "Angle/area-based or mean-curvature smoothing that respects mesh quality (CGAL PMP).",
            "Removing scan noise without volume loss; prepping for printing.",
            [win]() { claw_ai::send_panel_ai_prompt(
                          win, SMOOTH_HELP_PROMPT,
                          "Ask AI: CGAL smoothing help"); });
        ImGui::Spacing();
        auto* viewer = win ? win->viewer() : nullptr;

        easy3d::SurfaceMesh* mesh = nullptr;
        if (viewer) {
            mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (!mesh) {
                for (auto& mp : viewer->models()) {
                    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(mp.get())) {
                        mesh = sm; break;
                    }
                }
            }
        }

        if (!mesh) {
            ImGui::TextColored(claw_ui::status_error_color(),
                "No surface mesh loaded.");
            if (ImGui::Button("Close")) open = false;
        } else {
            const int nv = (int)mesh->n_vertices();
            const int nf = (int)mesh->n_faces();
            const bool is_triangle = mesh->is_triangle_mesh();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (v=%d f=%d) triangle=%s",
                mesh->name().c_str(), nv, nf,
                is_triangle ? "yes" : "no");
            if (!is_triangle) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Smoothing requires a triangle mesh.");
            }

#ifdef CLAW3D_HAS_CGAL
            const bool busy = win && win->algorithm_controller().is_running();

            {
                const char* items[] = {
                    "Tangential Relaxation",
                    "Angle Smoothing",
                    "Mean Curvature Flow",
                    "Angle + Area (disabled)"
                };
                ImGui::Combo("Mode", &s.mode, items, IM_ARRAYSIZE(items));
            }
            ImGui::TextWrapped("%s", mode_hint(s.mode));
            ImGui::Spacing();

            ImGui::InputInt("Iterations", &s.iterations, 1, 5);
            if (s.iterations < 1) s.iterations = 1;
            if (s.iterations > 200) s.iterations = 200;

            if (s.mode == SMOOTH_MODE_MeanCurvatureFlow) {
                ImGui::InputFloat("Time Step", &s.time_step, 0.0001f, 0.001f, "%.5f");
                if (s.time_step < 1e-6f) s.time_step = 1e-6f;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Smaller -> more conservative smoothing.\n"
                        "Typical range: 0.00001 .. 0.001.");
            }

            ImGui::Checkbox("Preserve Boundary", &s.preserve_boundary);
            claw_ui::same_line_if_fits_text("Preserve Sharp Edges");
            ImGui::Checkbox("Preserve Sharp Edges", &s.preserve_sharp_edges);
            if (s.preserve_sharp_edges) {
                ImGui::SliderFloat("Sharp Angle", &s.sharp_angle_degrees,
                                   10.0f, 170.0f, "%.0f deg");
            }

            if (ImGui::CollapsingHeader("Advanced")) {
                if (s.mode == SMOOTH_MODE_TangentialRelaxation) {
                    ImGui::Checkbox("Relax Constraints", &s.relax_constraints);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Allow vertices on constrained edges to slide\n"
                            "along the polyline (1D move).");
                }
                if (s.mode == SMOOTH_MODE_AngleSmoothing) {
                    ImGui::Checkbox("Safety Constraints", &s.safety_constraints);
                    ImGui::Checkbox("Project to Original Surface", &s.project_to_original);
                }
                if (s.mode == SMOOTH_MODE_MeanCurvatureFlow) {
                    ImGui::Checkbox("Rescale After Smoothing", &s.rescale_after_smoothing);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Try to preserve volume on closed meshes by\n"
                            "rescaling after each iteration.");
                }
            }

            ImGui::Spacing();
            ImGui::Checkbox("Live Preview", &s.live_preview);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Run one CGAL iteration at a time and animate the mesh moving each step. Slightly slower than fast mode; the Angle mode rebuilds its projection tree per step.");
            if (s.live_preview) {
                claw_ui::same_line_if_fits_width(220.0f);
                const char* speed_items[] = {"Normal", "Slow"};
                ImGui::Combo("Speed", &s.preview_speed, speed_items, 2);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Normal: paint each snapshot as it arrives. Slow: at most 1 snapshot every ~240 ms so the motion is readable on small meshes.");
            }

            if (!busy) {
                ImGui::Spacing();
                if (ImGui::Button("AI Parameter Advice")) {
                    claw_ai::send_panel_ai_prompt(
                        win, build_parameter_advice_prompt(mesh, s),
                        "Ask AI: CGAL smoothing parameter advice");
                }
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            // Progress text + live snapshot polling while busy.
            constexpr int slow_preview_ms = claw3d::preview_policy::kSlowPreviewUiMs;
            if (s.runner && busy) {
                const auto cur_stats = s.runner.result_stats();
                s.runner.copy_error_if_any(s.last_error);
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Phase: smoothing ... | mode=%s iters_done=%d/%d",
                    mode_label(s.last_run_mode),
                    cur_stats.iterations_done, s.last_run_iterations);

                if (s.live_preview && win) {
                    SMOOTH_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap)) {
                        // Skip the default-constructed empty snap that the
                        // very first poll might capture before the worker
                        // publishes its first iteration.
                        const bool has_payload = !snap.vertices.empty();
                        if (has_payload) {
                            bool show_now = true;
                            if (s.preview_speed == 1) {
                                show_now = claw_ui::frame_due(
                                    ImGui::GetTime(),
                                    s.last_overlay_display_time,
                                    slow_preview_ms);
                            }
                            if (show_now) {
                                win->overlays().update_smoothing_overlay(snap);
                                s.last_snap_iter      = snap.iteration;
                                s.last_snap_total     = snap.total_iterations;
                                s.last_snap_mean_disp = snap.mean_displacement;
                                s.last_snap_max_disp  = snap.max_displacement;
                            }
                        }
                    }
                    claw3d::app::wake_event_loop();
                }

                // Always-on progress line. Lives at a fixed slot inside the
                // busy block so the Cancel button below stays put across
                // frames (previously the iter line was inside show_now and
                // disappeared most frames, causing visible jitter).
                if (s.last_snap_total > 0) {
                    ImGui::TextDisabled(
                        "Iter %d/%d | mean_disp=%.5g max_disp=%.5g",
                        s.last_snap_iter, s.last_snap_total,
                        s.last_snap_mean_disp, s.last_snap_max_disp);
                } else {
                    ImGui::TextDisabled("Waiting for first iteration...");
                }
            }

            // Run / Cancel.
            if (!busy) {
                if (ImGui::Button("Run") && is_triangle) {
                    SMOOTH_Config cfg;
                    cfg.mode = s.mode;
                    cfg.iterations = s.iterations;
                    cfg.time_step = (double)s.time_step;
                    cfg.preserve_boundary = s.preserve_boundary;
                    cfg.preserve_sharp_edges = s.preserve_sharp_edges;
                    cfg.sharp_angle_degrees = (double)s.sharp_angle_degrees;
                    cfg.relax_constraints = s.relax_constraints;
                    cfg.safety_constraints = s.safety_constraints;
                    cfg.project_to_original = s.project_to_original;
                    cfg.rescale_after_smoothing = s.rescale_after_smoothing;
                    cfg.live_preview = s.live_preview;
                    cfg.preview_speed = s.preview_speed;
                    cfg.snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;

                    s.last_stats_valid = false;
                    s.last_input_metadata_prompt =
                        claw_ai::build_surface_mesh_metadata_prompt(mesh);
                    s.last_run_mode               = cfg.mode;
                    s.last_run_iterations         = cfg.iterations;
                    s.last_run_time_step          = (float)cfg.time_step;
                    s.last_run_preserve_boundary  = cfg.preserve_boundary;
                    s.last_run_preserve_sharp     = cfg.preserve_sharp_edges;
                    s.last_run_sharp_angle        = (float)cfg.sharp_angle_degrees;
                    s.close_requested = false;
                    s.last_error.clear();
                    s.final_result_ready.store(false,
                        std::memory_order_release);
                    s.last_snap_gen = -1;
                    s.last_overlay_display_time = 0.0;
                    s.settling = false;
                    s.settle_started_at = 0.0;
                    s.last_snap_iter = 0;
                    s.last_snap_total = s.iterations;
                    s.last_snap_mean_disp = 0.0;
                    s.last_snap_max_disp = 0.0;

                    if (s.live_preview && win)
                        win->overlays().init_smoothing_overlay(mesh);

                    claw3d::services::CgalSmoothingJobStart request;
                    request.source_mesh = mesh;
                    request.source_handle =
                        (win && win->viewer())
                            ? win->viewer()->model_handle(mesh)
                            : ModelHandle{};
                    request.config = cfg;
                    request.source_name = mesh->name();
                    request.final_result_ready = &s.final_result_ready;
                    request.wake_ui = []() { claw3d::app::wake_event_loop(); };

                    if (win) {
                        s.runner = claw3d::services::start_cgal_smoothing_job(
                            win->algorithm_controller(), request);
                    } else {
                        s.runner.reset();
                    }
                    if (!s.runner) {
                        if (s.live_preview && win)
                            win->overlays().clear_smoothing_overlay();
                        s.final_result_ready.store(true,
                            std::memory_order_release);
                        s.last_error = "Failed to start smoothing job.";
                        LOG(WARNING) << s.last_error;
                    }
                }
            } else {
                if (s.runner && ImGui::Button("Cancel")) {
                    s.runner.cancel();
                    if (win)
                        win->algorithm_controller().request_cancel();
                    claw3d::app::wake_event_loop();
                }
                if (s.runner && s.runner.is_cancelled() && !s.close_requested) {
                    claw_ui::same_line_if_fits_text("Cancel requested...");
                    ImGui::TextDisabled("Cancel requested...");
                }
                if (s.close_requested) {
                    claw_ui::same_line_if_fits_text("Closing after smoothing stops...");
                    ImGui::TextDisabled("Closing after smoothing stops...");
                }
            }

            // Worker completion -> optional settle, then handoff to the controller.
            if (busy &&
                s.runner.done_and_ready(s.final_result_ready))
            {
                const bool close_after = s.close_requested;
                const bool finish_immediately =
                    !s.live_preview || close_after ||
                    s.runner.cancelled_or_failed();
                if (finish_immediately) {
                    if (s.live_preview && win)
                        win->overlays().clear_smoothing_overlay();
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.result_stats();
                    s.last_stats_valid = true;
                    mark_algorithm_done(win);
                    s.runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    if (close_after) open = false;
                    claw3d::app::wake_event_loop();
                } else if (!s.settling) {
                    // Drain a final snapshot so the overlay reflects the
                    // last iteration before settle locks the view.
                    SMOOTH_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                        win && !snap.vertices.empty())
                    {
                        win->overlays().update_smoothing_overlay(snap);
                    }
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.result_stats();
                    s.last_stats_valid = true;
                    if (win)
                        win->algorithm_controller().mark_final_preview_holding();
                    s.settling = true;
                    s.settle_started_at = ImGui::GetTime();
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding final mesh for %.1fs ...",
                        s.settle_ms / 1000.0);
                } else {
                    const double now = ImGui::GetTime();
                    const double elapsed =
                        (now - s.settle_started_at) * 1000.0;
                    if (elapsed >= s.settle_ms) {
                        if (win) win->overlays().clear_smoothing_overlay();
                        mark_algorithm_done(win);
                        s.runner.reset();
                        s.settling = false;
                        s.close_requested = false;
                        claw3d::app::wake_event_loop();
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Done. Holding final mesh %.1fs / %.1fs ...",
                            elapsed / 1000.0, s.settle_ms / 1000.0);
                    }
                }
            }

            // Post-run stats.
            if (s.last_stats_valid && !busy) {
                const auto& st = s.last_stats;
                ImGui::Spacing();
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Result: V=%d F=%d | %.0f ms (smoothing %.0f ms)",
                    st.after.vertices, st.after.faces,
                    st.ms_total, st.ms_smoothing);
                ImGui::TextDisabled(
                    "Mode: %s | iters: %d | minA: %.2f -> %.2f deg | bad<10: %d -> %d",
                    mode_label(s.last_run_mode), st.iterations_done,
                    st.before.min_angle, st.after.min_angle,
                    st.before.bad_triangles_10deg, st.after.bad_triangles_10deg);
                ImGui::TextDisabled(
                    "Displacement: mean=%.5f max=%.5f%s",
                    st.mean_displacement, st.max_displacement,
                    st.before.bbox_diag > 0
                        ? (std::string(" (max/bbox=") +
                           std::to_string(st.max_displacement / st.before.bbox_diag * 100.0) +
                           "%)").c_str()
                        : "");
                ImGui::TextDisabled(
                    "Area ratio: %.4f | Volume ratio: %.4f | Closed: %s",
                    st.surface_area_ratio, st.volume_ratio,
                    st.closed_mesh ? "yes" : "no");
                if (st.cancelled)
                    ImGui::TextColored(claw_ui::status_warning_color(), "Status: cancelled");
                if (!s.last_error.empty())
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", s.last_error.c_str());
                if (ImGui::Button("AI Evaluate Result")) {
                    claw_ai::send_panel_ai_prompt(
                        win, build_result_evaluation_prompt(s, st),
                        "Ask AI: evaluate CGAL smoothing result");
                }
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }
#else
            ImGui::TextColored(claw_ui::status_warning_color(),
                "CGAL not available (rebuild with CLAW3D_ENABLE_CGAL=ON).");
#endif
        }
    } DIALOG_END;
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/acvd_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/acvd_remeshing_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "overlays/overlay_controller.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ai/mesh_ai_stats.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"
#include "ui/ui_frame_gate.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static const char* ACVD_HELP_PROMPT =
    "I am using CGAL ACVD Remeshing in 3D Claw. Please advise on parameters.\n\n"
    "Modes:\n"
    "  Uniform - baseline isotropic remeshing, fastest.\n"
    "  Uniform + QEM Postprocess - adds QEM optimization after clustering.\n"
    "  QEM Energy - uses QEM-based energy during clustering, more accurate.\n"
    "  Adaptive Curvature - gradation factor controls density near curves.\n\n"
    "Please suggest target vertex count and mode for my mesh.";

namespace {

const char* mode_label(int m) {
    switch (m) {
    case ACVD_MODE_Uniform:               return "Uniform";
    case ACVD_MODE_UniformQemPostprocess: return "Uniform + QEM Postprocess";
    case ACVD_MODE_QemEnergy:             return "QEM Energy";
    case ACVD_MODE_AdaptiveCurvature:     return "Adaptive Curvature";
    default:                               return "Unknown";
    }
}

using claw_ai::ascii_only;

claw_ai::SurfaceMeshAIStats collect_mesh_ai_stats(easy3d::SurfaceMesh* mesh) {
    return claw_ai::collect_surface_mesh_ai_stats(mesh);
}

void append_mesh_metadata(std::ostringstream& oss,
                          const claw_ai::SurfaceMeshAIStats& m) {
    claw_ai::append_surface_mesh_metadata(oss, m);
}

std::string build_mesh_metadata_prompt(easy3d::SurfaceMesh* mesh) {
    return claw_ai::build_surface_mesh_metadata_prompt(mesh);
}

void append_parameter_block(std::ostringstream& oss, int mode,
                            int target_vertices, float gradation,
                            float ratio, int seed, bool live_preview,
                            int preview_speed)
{
    oss << "Current ACVD parameters:\n";
    oss << "- Mode: " << mode_label(mode) << "\n";
    oss << "- Target vertices: " << target_vertices << "\n";
    oss << "- Gradation factor: " << gradation << "\n";
    oss << "- Vertex count ratio: " << ratio << "\n";
    oss << "- Random seed: " << seed << "\n";
    oss << "- Live preview: " << (live_preview ? "on" : "off") << "\n";
    oss << "- Preview speed: " << (preview_speed == 1 ? "Slow" : "Normal") << "\n";
    oss << "Mode notes:\n";
    oss << "- Uniform: fastest baseline isotropic ACVD remeshing.\n";
    oss << "- Uniform + QEM Postprocess: fast sharp-feature recovery after clustering.\n";
    oss << "- QEM Energy: slower feature-aware clustering with QEM energy.\n";
    oss << "- Adaptive Curvature: uses gradation to allocate more vertices near curvature.\n";
}

std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const ACVDState& s)
{
    std::ostringstream oss;
    oss << "Please give parameter advice for CGAL ACVD Remeshing in 3D Claw.\n\n";
    append_parameter_block(oss, s.mode, s.target_vertices, s.gradation,
                           s.ratio, s.seed, s.live_preview, s.preview_speed);
    oss << "\n";
    append_mesh_metadata(oss, collect_mesh_ai_stats(mesh));
    oss << "\nKeep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Recommend a safe target vertex range for first run, moderate remesh, and aggressive remesh.\n";
    oss << "2. Say whether Uniform, QEM Postprocess, QEM Energy, or Adaptive Curvature is best for this mesh.\n";
    oss << "3. Explain when to increase gradation factor or use adaptive curvature.\n";
    oss << "4. Point out risks from open boundaries, non-triangle faces, multiple components, or non-manifold vertices.\n";
    oss << "5. Say whether live preview may be expensive for this model.\n";
    return ascii_only(oss.str());
}

std::string build_result_evaluation_prompt(const ACVDState& s,
                                           const ACVD_DebugStats& st)
{
    std::ostringstream oss;
    oss << "Please evaluate this CGAL ACVD Remeshing run in 3D Claw.\n\n";
    append_parameter_block(oss, s.last_run_mode, s.last_run_target_vertices,
                           s.last_run_gradation, s.last_run_ratio,
                           s.last_run_seed, s.last_run_live_preview,
                           s.last_run_preview_speed);
    oss << "\n";
    if (!s.last_input_metadata_prompt.empty())
        oss << s.last_input_metadata_prompt << "\n";
    oss << "Run statistics:\n";
    oss << "- Input counts: vertices=" << st.initial_vertices
        << " edges=" << st.initial_edges
        << " faces=" << st.initial_faces << "\n";
    oss << "- Working vertices after preprocess: "
        << st.working_vertices_after_preprocess << "\n";
    oss << "- Target vertices: " << st.target_vertices << "\n";
    oss << "- Output counts: vertices=" << st.final_vertices
        << " faces=" << st.final_faces << "\n";
    oss << "- Output vertex count matched target: "
        << (st.output_vertex_count_matched ? "yes" : "no") << "\n";
    oss << "- Loops: " << st.loops
        << " iterations: " << st.iterations
        << " assignment events: " << st.assignment_events << "\n";
    oss << "- Disconnected cluster repairs: " << st.disconnected_repairs
        << " non-manifold repairs: " << st.non_manifold_repairs << "\n";
    oss << "- Cancelled: " << (st.cancelled ? "yes" : "no") << "\n";
    oss << "- Runtime ms: total=" << st.ms_total
        << " preprocess=" << st.ms_preprocess
        << " clustering=" << st.ms_clustering
        << " output=" << st.ms_output << "\n\n";
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Did the result reasonably hit the requested target?\n";
    oss << "2. Is the target conservative, moderate, or aggressive for the input?\n";
    oss << "3. Should the user compare Uniform, QEM Energy, QEM Postprocess, or Adaptive next?\n";
    oss << "4. What visual artifacts should be inspected on the mesh?\n";
    oss << "5. If the mesh has open boundaries, explain why output vertices can exceed target.\n";
    return ascii_only(oss.str());
}

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

} // namespace

void renderDialogACVD(ViewportCanvas* viewer, ACVDState& s, bool& open) {
    prepare_dialog_window(520, 520);
    DIALOG_BODY("ACVD Remeshing", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        const bool acvd_busy =
            s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::AcvdRemeshing);
        if (!open && acvd_busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            if (win)
                win->algorithm_controller().request_cancel();
            claw3d::app::wake_event_loop();
        }

        render_panel_header(
            "Centroidal-Voronoi-driven isotropic remesh with explicit target vertex count.",
            "Uniform triangle distribution for simulation / FE meshing.",
            [&]() { send_ai_prompt(win, ACVD_HELP_PROMPT,
                                   "Ask AI: ACVD remeshing help"); });
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
            int nf = (int)mesh->n_faces();
            const int nv = (int)mesh->n_vertices();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (v=%d f=%d)", mesh->name().c_str(), nv, nf);

#ifdef CLAW3D_HAS_CGAL
            const bool busy = win && win->algorithm_controller().is_running();

            // Mode selector.
            {
                const char* items[] = {
                    "Uniform", "Uniform + QEM Postprocess",
                    "QEM Energy", "Adaptive Curvature"
                };
                ImGui::Combo("Mode", &s.mode, items, IM_ARRAYSIZE(items));
            }
            ImGui::InputInt("Target Vertices", &s.target_vertices, 100, 500);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Desired output vertex count. Default: max(faces/20, 10).");
            if (s.target_vertices < 10) s.target_vertices = 10;
            claw_ui::same_line_if_fits_button("Default");
            if (ImGui::SmallButton("Default"))
                s.target_vertices = std::max(nf / 20, 10);
            claw_ui::same_line_if_fits_button("5% faces");
            if (ImGui::SmallButton("5% faces"))
                s.target_vertices = std::max((int)std::lround(nf * 0.05), 10);
            claw_ui::same_line_if_fits_button("10% faces");
            if (ImGui::SmallButton("10% faces"))
                s.target_vertices = std::max((int)std::lround(nf * 0.10), 10);
            claw_ui::same_line_if_fits_button("1000");
            if (ImGui::SmallButton("1000"))
                s.target_vertices = 1000;
            claw_ui::same_line_if_fits_button("3000");
            if (ImGui::SmallButton("3000"))
                s.target_vertices = 3000;

            if (s.mode == ACVD_MODE_AdaptiveCurvature) {
                ImGui::SliderFloat("Gradation", &s.gradation, 0.1f, 4.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Higher = more vertices near high-curvature areas.");
            }

            if (ImGui::CollapsingHeader("Advanced")) {
                ImGui::SliderFloat("Vertex Count Ratio", &s.ratio, 0.01f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "CGAL subdivision guard. If the input has too few vertices "
                        "for the target, ACVD subdivides the working mesh until this "
                        "ratio condition is satisfied. Lower values reduce preprocessing "
                        "cost; higher values can make clustering denser and slower.");
                ImGui::InputInt("Random Seed", &s.seed, 1, 10);
                if (s.seed < 0) s.seed = 0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fixed seed makes the colored cells reproducible.");
                ImGui::InputDouble("Settle Hold (ms)", &s.settle_ms, 100.0, 500.0, "%.0f");
                if (s.settle_ms < 0.0) s.settle_ms = 0.0;
            }

            ImGui::Spacing();
            ImGui::Checkbox("Live Preview", &s.live_preview);
            if (s.live_preview) {
                claw_ui::same_line_if_fits_width(220.0f);
                const char* speed_items[] = {"Normal", "Slow"};
                ImGui::Combo("Speed", &s.preview_speed, speed_items,
                             IM_ARRAYSIZE(speed_items));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Normal: display each cluster snapshot as it arrives.\n"
                        "Slow: display at most 1 snapshot every ~240 ms so the\n"
                        "       cells-spreading animation is more readable.");
            }

            if (!busy) {
                ImGui::Spacing();
                if (ImGui::Button("AI Parameter Advice")) {
                    send_ai_prompt(win, build_parameter_advice_prompt(mesh, s),
                                   "Ask AI: ACVD parameter advice");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Send current mesh metadata and ACVD parameters to AI.");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            // Progress + live overlay polling.
            constexpr int slow_preview_ms = claw3d::preview_policy::kSlowPreviewUiMs;
            if (s.runner && busy) {
                auto cur_stats = s.runner.debug_stats();
                s.last_stats = cur_stats;
                s.last_stats_valid = true;
                s.runner.copy_error_if_any(s.last_error);
                int assigned = cur_stats.assignment_events;
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Phase: Clustering | Loops %d | Iters %d | "
                    "Assigned %d pts | Clusters %d",
                    cur_stats.loops, cur_stats.iterations,
                    assigned, s.target_vertices);

                // Seed overlay: poll persistent seed list each frame. The
                // worker collects positions in on_seed_created. The seed
                // count only ever grows during seeding, so re-uploading is
                // cheap.
                std::vector<ACVD_Point3d> seeds;
                s.runner.get_seed_positions(seeds);
                if (!seeds.empty() && seeds.size() != s.seed_positions.size()) {
                    s.seed_positions = seeds;
                    if (win) win->overlays().update_acvd_seed_overlay(seeds);
                }

                // Poll cluster snapshot.
                std::vector<ACVD_Point3d>  sv;
                std::vector<ACVD_Triangle> st;
                std::vector<int>           sc;
                std::vector<ACVD_Point3d>  centers;
                if (s.runner.poll_cluster_snapshot(s.last_snap_gen, sv, st, sc, centers))
                {
                    bool show_now = true;
                    if (s.preview_speed == 1) {
                        // Slow: gate by wall clock so snapshots are spaced at
                        // least slow_preview_ms apart. The worker keeps running
                        // at full speed; we just consume the queue slower.
                        const double now = ImGui::GetTime();
                        show_now = claw_ui::frame_due(
                            now, s.last_cluster_display_time,
                            slow_preview_ms);
                        if (show_now)
                            claw_ui::mark_frame_displayed(
                                now, s.last_seed_display_time);
                    }
                    if (show_now && win)
                        win->overlays().update_acvd_cluster_overlay(sv, st, sc);
                }

                // Keep the GLFW loop awake while the worker publishes live
                // snapshots. Otherwise very fast ACVD runs can finish before
                // the viewport repaints enough frames to show the spread.
                if (s.live_preview)
                    claw3d::app::wake_event_loop();
            }

            // --- Buttons ---
            if (!busy) {
                if (ImGui::Button("Run")) {
                    ACVD_Config cfg;
                    cfg.mode             = s.mode;
                    cfg.target_vertices  = s.target_vertices;
                    cfg.gradation_factor = (double)s.gradation;
                    if (s.ratio < 0.01f) s.ratio = 0.01f;
                    if (s.ratio > 1.0f) s.ratio = 1.0f;
                    cfg.vertex_count_ratio = (double)s.ratio;
                    cfg.random_seed      = (unsigned)s.seed;
                    cfg.live_preview     = s.live_preview;
                    cfg.slow_visual_playback = (s.preview_speed == 1);
                    cfg.event_interval   = 1024;   // sample per-vertex
                    if (cfg.live_preview && cfg.target_vertices >= claw3d::preview_policy::kLargeTargetVertexCount) {
                        cfg.snapshot_min_ms = claw3d::preview_policy::kHugeMeshSnapshotMinMs;
                        cfg.max_snapshot_faces = claw3d::preview_policy::kReducedMaxSnapshotFaces;
                    }

                    s.last_stats_valid = false;
                    s.last_snap_gen    = -1;
                    s.last_cluster_display_time = 0.0;
                    s.last_seed_display_time    = 0.0;
                    s.seed_positions.clear();
                    s.settling = false;
                    s.settle_started_at = 0.0;
                    s.last_input_metadata_prompt =
                        build_mesh_metadata_prompt(mesh);
                    s.last_run_mode = cfg.mode;
                    s.last_run_target_vertices = cfg.target_vertices;
                    s.last_run_gradation = (float)cfg.gradation_factor;
                    s.last_run_ratio = (float)cfg.vertex_count_ratio;
                    s.last_run_seed = (int)cfg.random_seed;
                    s.last_run_live_preview = cfg.live_preview;
                    s.last_run_preview_speed = s.preview_speed;
                    s.close_requested = false;
                    s.last_error.clear();
                    s.final_result_ready.store(false,
                        std::memory_order_release);

                    if (s.live_preview && win)
                        win->overlays().init_acvd_overlay(mesh);

                    claw3d::services::AcvdRemeshingJobStart request;
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
                        s.runner = claw3d::services::start_acvd_remeshing_job(
                            win->algorithm_controller(), request);
                    } else {
                        s.runner.reset();
                    }
                    if (!s.runner) {
                        if (s.live_preview && win)
                            win->overlays().clear_acvd_overlay();
                        s.final_result_ready.store(true,
                            std::memory_order_release);
                        s.last_error = "Failed to start ACVD remeshing job.";
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
                if (s.runner && s.runner.is_cancelled()
                    && !s.close_requested) {
                    claw_ui::same_line_if_fits_text("Cancel requested...");
                    ImGui::TextDisabled("Cancel requested...");
                }
                if (s.close_requested) {
                    claw_ui::same_line_if_fits_text("Closing after ACVD stops...");
                    ImGui::TextDisabled("Closing after ACVD stops...");
                }
            }

            // Worker completion enters a short settle phase before handoff.
            // Very fast meshes can finish before the viewport paints enough
            // frames for the cluster spread to be readable, so only the visual
            // teardown is delayed; worker stats are already captured.
            if (busy &&
                s.runner.done_and_ready(s.final_result_ready))
            {
                const bool close_after_finish = s.close_requested;
                const bool finish_immediately =
                    !s.live_preview || close_after_finish ||
                    s.runner.cancelled_or_failed();

                if (finish_immediately) {
                    if (s.live_preview && win)
                        win->overlays().clear_acvd_overlay();
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.debug_stats();
                    s.last_stats_valid = true;
                    mark_algorithm_done(win);
                    s.runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    if (close_after_finish)
                        open = false;
                    claw3d::app::wake_event_loop();
                } else
                if (!s.settling) {
                    // Pull the absolute last snapshot in case we missed one
                    // between the last poll and worker finishing, so the
                    // overlay shows the truly final cluster state.
                    std::vector<ACVD_Point3d>  sv;
                    std::vector<ACVD_Triangle> st;
                    std::vector<int>           sc;
                    std::vector<ACVD_Point3d>  centers;
                    if (s.runner.poll_cluster_snapshot(s.last_snap_gen,
                            sv, st, sc, centers))
                    {
                        if (win) win->overlays().update_acvd_cluster_overlay(sv, st, sc);
                    }
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.debug_stats();
                    s.last_stats_valid = true;
                    if (win)
                        win->algorithm_controller().mark_final_preview_holding();
                    s.settling = true;
                    s.settle_started_at = ImGui::GetTime();
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding cluster view for %.1fs ...",
                        s.settle_ms / 1000.0);
                } else {
                    const double now = ImGui::GetTime();
                    const double elapsed = (now - s.settle_started_at) * 1000.0;
                    if (elapsed >= s.settle_ms) {
                        if (win) win->overlays().clear_acvd_overlay();
                        mark_algorithm_done(win);
                        s.runner.reset();
                        s.settling = false;
                        s.close_requested = false;
                        claw3d::app::wake_event_loop();
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Done. Holding cluster view %.1fs / %.1fs ...",
                            elapsed / 1000.0, s.settle_ms / 1000.0);
                    }
                }
            }

            // Post-run stats.
            if (s.last_stats_valid && !busy) {
                const auto& st = s.last_stats;
                ImGui::Spacing();
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Result: %d v -> %d v (%d f) | %.1f ms",
                    st.initial_vertices, st.final_vertices,
                    st.final_faces, st.ms_total);
                ImGui::TextDisabled(
                    "Mode: %s | target %d | loops %d | iters %d | repairs %d/%d",
                    mode_label(st.mode), st.target_vertices,
                    st.loops, st.iterations,
                    st.disconnected_repairs, st.non_manifold_repairs);
                ImGui::TextDisabled(
                    "Matched target: %s | working vertices after preprocess: %d",
                    st.output_vertex_count_matched ? "yes" : "no",
                    st.working_vertices_after_preprocess);
                if (st.cancelled)
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Status: cancelled");
                if (!s.last_error.empty()) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", s.last_error.c_str());
                }
                if (ImGui::Button("AI Evaluate Result")) {
                    send_ai_prompt(win, build_result_evaluation_prompt(s, st),
                                   "Ask AI: evaluate ACVD result");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Send ACVD run statistics to AI for result evaluation.");
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

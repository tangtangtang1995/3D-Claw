// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/mcf_skeletonization_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/mcf_skeletonization_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "overlays/overlay_controller.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static const char* MCF_HELP_PROMPT =
    "I am using CGAL Mean Curvature Flow Skeletonization in 3D Claw.\n\n"
    "MCF iteratively contracts a closed triangle mesh inward along the mean\n"
    "curvature flow, locally remeshes the contracted shape, fixes degenerate\n"
    "triangles, and finally extracts a 1D curve skeleton from the collapsed\n"
    "surface.\n\n"
    "Input must be a closed, pure-triangle, single-connected-component mesh.\n"
    "Self-intersections, open borders, multiple components, or non-triangle\n"
    "faces should be repaired before running.\n\n"
    "Key parameters:\n"
    "  Max Iterations - upper cap; convergence usually hits before this.\n"
    "  Area Variation Factor - convergence threshold; smaller = tighter stop.\n"
    "  Quality / Speed (omega_H) - smaller is faster but lower quality.\n"
    "  Medially Centered + omega_P - attract skeleton to medial axis.\n"
    "  Min Edge Length - default 0.002 * bbox diagonal.\n"
    "  Max Triangle Angle - split triangles above this angle.\n\n"
    "Please suggest parameters appropriate for my mesh and goal.";

namespace {

std::string strip_mesh_ext(const std::string& s) {
    static const char* exts[] = {
        ".off", ".obj", ".ply", ".stl",
        ".OFF", ".OBJ", ".PLY", ".STL"
    };
    for (const char* e : exts) {
        const std::size_t L = std::strlen(e);
        if (s.size() > L && s.compare(s.size() - L, L, e) == 0)
            return s.substr(0, s.size() - L);
    }
    return s;
}

std::string build_mesh_metadata_prompt(easy3d::SurfaceMesh* mesh) {
    if (!mesh) return "(no active SurfaceMesh)";
    const auto m = claw_ai::collect_surface_mesh_ai_stats(mesh);
    std::ostringstream s;
    s << "Mesh: " << m.name << "\n"
      << "  vertices: " << m.vertices << "\n"
      << "  faces: " << m.faces << "\n"
      << "  triangle_mesh: " << (m.triangle_mesh ? "yes" : "no") << "\n";
    s << "  border_edges: " << m.boundary_edges
      << " (closed: " << (m.boundary_edges == 0 ? "yes" : "no") << ")\n";
    s << "  connected_components: ";
    if (m.connected_components >= 0)
        s << m.connected_components << "\n";
    else
        s << "unknown\n";
    s << "  bbox_diag: " << m.bbox_diag << "\n";
    s << "  default_min_edge_length (auto = 0.002*bbox_diag): "
      << (0.002 * m.bbox_diag) << "\n";
    // Surface flags the AI should know about to warn the user.
    if (!m.triangle_mesh)
        s << "  WARNING: non-triangle faces present, MCF will reject\n";
    if (m.boundary_edges > 0)
        s << "  WARNING: mesh has open borders, MCF will reject\n";
    if (m.connected_components > 1)
        s << "  WARNING: multi-component mesh, MCF will reject\n";
    return s.str();
}

std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const MCFSkelState& s) {
    std::ostringstream out;
    out << MCF_HELP_PROMPT << "\n\nCurrent input:\n"
        << build_mesh_metadata_prompt(mesh) << "\n"
        << "Current parameters:\n"
        << "  max_iterations: " << s.max_iterations << "\n"
        << "  area_variation_factor: " << s.area_variation_factor << "\n"
        << "  quality_speed_tradeoff (omega_H): " << s.quality_speed_tradeoff << "\n"
        << "  medially_centered: " << (s.medially_centered ? "yes" : "no") << "\n"
        << "  medially_centered_speed_tradeoff (omega_P): "
        << s.medially_centered_speed_tradeoff << "\n"
        << "  min_edge_length: "
        << (s.auto_min_edge_length ? std::string("auto")
                                   : std::to_string(s.min_edge_length)) << "\n"
        << "  max_triangle_angle_degrees: " << s.max_triangle_angle_degrees << "\n";
    out << "\nPlease answer in two parts:\n"
        << "1. A brief diagnosis of the input (one paragraph): is the mesh "
           "suitable for MCF, any preprocessing risks.\n"
        << "2. Recommended parameters in this exact key=value block so the "
           "user can copy them back into the panel:\n"
        << "   max_iterations=...\n"
        << "   area_variation_factor=...\n"
        << "   quality_speed_tradeoff=...\n"
        << "   medially_centered=...\n"
        << "   medially_centered_speed_tradeoff=...\n"
        << "   min_edge_length=...   (use 'auto' or a numeric value)\n"
        << "   max_triangle_angle_degrees=...\n";
    return out.str();
}

std::string build_result_evaluation_prompt(const MCFSkelState& s,
                                           const MCF_Result& r) {
    std::ostringstream out;
    out << MCF_HELP_PROMPT << "\n\n";
    out << "Run statistics:\n"
        << "  input_vertices: " << r.metrics.original_vertices << "\n"
        << "  input_faces: " << r.metrics.original_faces << "\n"
        << "  skeleton_vertices: " << r.metrics.skeleton_vertices << "\n"
        << "  skeleton_edges: " << r.metrics.skeleton_edges << "\n"
        << "  converged: " << (r.metrics.converged ? "yes" : "no") << "\n"
        << "  cancelled: " << (r.metrics.cancelled ? "yes" : "no") << "\n"
        << "  iterations_done: " << r.metrics.iteration << "\n"
        << "  area_change_ratio_final: " << r.metrics.area_change_ratio << "\n"
        << "  fixed_vertices: " << r.metrics.fixed_vertices << "\n"
        << "  elapsed_ms: " << r.metrics.elapsed_ms << "\n"
        << "\nParameters used:\n"
        << "  max_iterations: " << s.max_iterations << "\n"
        << "  area_variation_factor: " << s.area_variation_factor << "\n"
        << "  quality_speed_tradeoff: " << s.quality_speed_tradeoff << "\n"
        << "  medially_centered: " << (s.medially_centered ? "yes" : "no") << "\n"
        << "  medially_centered_speed_tradeoff: "
        << s.medially_centered_speed_tradeoff << "\n"
        << "  min_edge_length: "
        << (s.auto_min_edge_length ? std::string("auto")
                                   : std::to_string(s.min_edge_length)) << "\n"
        << "  max_triangle_angle_degrees: " << s.max_triangle_angle_degrees << "\n";
    if (r.error_code != MCF_ERR_None)
        out << "\n  error: " << r.error_message << "\n";
    out << "\nPlease answer concisely (4-6 bullets):\n"
        << "1. Is convergence healthy?\n"
        << "2. Skeleton resolution: too sparse / about right / too noisy?\n"
        << "3. Any sign the input is unsuitable?\n"
        << "4. If rerunning, recommend ONE parameter to change first.\n";
    return out.str();
}

// MCF keeps its own metadata prompt because it adds MCF-specific reject
// warnings, but the raw mesh statistics come from the shared claw_ai
// collector. ASCII filter and send-prompt route through claw_ai too.
using claw_ai::ascii_only;

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

} // namespace

void renderDialogMCFSkeletonization(ViewportCanvas* viewer,
                                    MCFSkelState& s,
                                    bool& open)
{
    prepare_dialog_window(500, 480);
    DIALOG_BODY("MCF Skeletonization", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        MainWindow* win = MainWindow::instance();
        // If the user clicks the dialog X while a run is in flight we
        // must NOT just drop the runner. Force open back true, request
        // close, and cancel; the finalize block below will catch it on
        // the next frames and finalize cleanly.
        const bool mcf_busy_now =
            s.runner && win &&
            win->algorithm_controller().is_running_id(
                AlgorithmId::MeanCurvatureFlowSkeleton);
        if (!open && mcf_busy_now) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            if (win)
                win->algorithm_controller().request_cancel();
            claw3d::app::wake_event_loop();
        }
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

        if (ImGui::SmallButton("?")) {
            send_ai_prompt(win, MCF_HELP_PROMPT,
                           "Ask AI: MCF skeletonization help");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Ask AI to explain MCF skeletonization parameters.");
        claw_ui::same_line_if_fits_text("Mean Curvature Flow Skeletonization (CGAL)");
        ImGui::TextDisabled("Mean Curvature Flow Skeletonization (CGAL)");

        ImGui::Separator();

        // Input validation banner.
        std::string reason;
        const bool input_ok =
            claw3d::services::validate_mcf_skeletonization_input(mesh, reason);
        if (!input_ok) {
            ImGui::TextColored(claw_ui::status_warning_color(),
                "Input not ready: %s", reason.c_str());
            ImGui::TextDisabled(
                "MCF needs a closed, single-component, pure triangle mesh.");
        } else {
            ImGui::TextColored(claw_ui::status_success_color(),
                "Input OK: %s (V=%zu F=%zu)",
                mesh->name().c_str(),
                (size_t)mesh->n_vertices(),
                (size_t)mesh->n_faces());
        }

        // Pre-run AI parameter advice. Available whenever there is a mesh
        // (even if the validation banner says it cannot run yet; AI can
        // explain what to fix). The "##pre" suffix keeps ImGui from
        // colliding with the post-run button further down.
        if (mesh && ImGui::SmallButton("AI Parameter Advice##pre")) {
            send_ai_prompt(win, build_parameter_advice_prompt(mesh, s),
                           "Ask AI: MCF parameter advice");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Ask AI for MCF parameters appropriate for this mesh. "
                "AI replies in a key=value block you can copy back here.");
        if (mesh && win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
            claw_ui::same_line_if_fits_text("(set API key in AI Chat)");
            ImGui::TextDisabled("(set API key in AI Chat)");
        }

        ImGui::Separator();

        // --- Parameters ---
        ImGui::Text("Algorithm parameters");

        ImGui::SliderInt("Max Iterations",
                         &s.max_iterations, 1, 1000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Upper cap on contraction iterations.\n"
                "CGAL default: 500. Convergence usually triggers earlier.\n"
                "Raise if the area is still changing visibly at the cap;\n"
                "lower for a quick preview.");

        ImGui::SliderFloat("Area Variation Factor",
                           &s.area_variation_factor, 1e-6f, 1e-2f,
                           "%.6f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Convergence threshold. Iteration stops when\n"
                "  |area(t) - area(t-1)| / area(original) < this.\n"
                "CGAL default: 0.0001.\n"
                "Smaller = tighter convergence (more iterations, finer skeleton).\n"
                "Larger = early stop (coarser skeleton).");

        ImGui::SliderFloat("Quality / Speed (omega_H)",
                           &s.quality_speed_tradeoff, 0.001f, 10.0f,
                           "%.4f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "omega_H: weight on the smoothing term in the linear system.\n"
                "CGAL default: 0.1.\n"
                "Smaller -> faster contraction but lower-quality skeleton\n"
                "  (can collapse too aggressively).\n"
                "Larger -> safer contraction, more iterations needed,\n"
                "  cleaner skeleton on thin features.");

        ImGui::Checkbox("Medially Centered", &s.medially_centered);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "If on, the meso-skeleton is attracted toward the medial\n"
                "axis (computed from Voronoi poles) during contraction.\n"
                "Default: on.\n"
                "On = cleaner, more centered skeleton; slightly slower.\n"
                "Off = pure mean-curvature contraction; may drift.");

        if (s.medially_centered) {
            ImGui::SliderFloat("Medial Smoothness (omega_P)",
                               &s.medially_centered_speed_tradeoff,
                               0.001f, 5.0f,
                               "%.4f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "omega_P: strength of the medial-axis attraction.\n"
                    "CGAL default: 0.2. Only used when Medially Centered is on.\n"
                    "Larger -> skeleton tracks the medial axis more closely\n"
                    "  (less smooth, slower convergence).\n"
                    "Smaller -> smoother skeleton, further from medial axis.");
        }

        ImGui::SliderFloat("Max Triangle Angle (deg)",
                           &s.max_triangle_angle_degrees, 60.0f, 170.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Local remeshing: triangles with any angle larger than this\n"
                "get split during contraction.\n"
                "CGAL default: 110 degrees.\n"
                "Lower -> more aggressive splits, finer meso mesh.\n"
                "Higher -> fewer splits, faster but coarser meso.");

        ImGui::Checkbox("Auto Min Edge Length (0.002 * bbox diag)",
                        &s.auto_min_edge_length);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "When on, min_edge_length is set to 0.002 * bbox diagonal,\n"
                "which is the CGAL default. Recommended.");

        if (!s.auto_min_edge_length) {
            ImGui::SliderFloat("Min Edge Length",
                               &s.min_edge_length, 1e-6f, 1e-1f,
                               "%.6f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Local remeshing: edges shorter than this get collapsed\n"
                    "during contraction.\n"
                    "Default (auto) = 0.002 * bbox diagonal.\n"
                    "Smaller -> finer meso mesh, slower, more skeleton detail.\n"
                    "Larger -> coarser meso mesh, faster, may lose features.");
        }

        ImGui::Separator();

        // --- Visual toggles (only after a successful run) ---
        if (s.last_result_valid) {
        ImGui::Separator();
        ImGui::Text("Visualization");
        const bool prev_ghost    = s.show_original_ghost;
        const bool prev_meso     = s.show_meso;
        const bool prev_skeleton = s.show_skeleton;
        const bool prev_corr     = s.show_correspondence;
        const bool prev_sdf      = s.show_sdf_heatmap;
        ImGui::Checkbox("Show Original Ghost", &s.show_original_ghost);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Toggle the source mesh visibility. After a run, the source\n"
                "stays as a gray wireframe so the skeleton stands out.");
        claw_ui::same_line_if_fits_text("Show Skeleton");
        ImGui::Checkbox("Show Skeleton", &s.show_skeleton);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Toggle visibility of <source>_mcf_skeleton (the final\n"
                "purple curve). Only active after a successful run.");
        ImGui::Checkbox("Show Meso Mesh", &s.show_meso);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Toggle the contracting meso mesh visibility while a live\n"
                "preview is running. No effect after the run completes.");
        ImGui::Checkbox("Show Correspondence", &s.show_correspondence);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Faint gray lines from each skeleton vertex to its\n"
                "corresponding input mesh vertices. Capped to keep the\n"
                "view readable. Only meaningful after a successful run\n"
                "with Collect Correspondence enabled.");
        claw_ui::same_line_if_fits_text("Show SDF Heatmap");
        ImGui::Checkbox("Show SDF Heatmap", &s.show_sdf_heatmap);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Re-color the source mesh by distance to the skeleton.\n"
                "Blue = thin (close to skeleton), red = thick (far).\n"
                "Replaces the wireframe ghost while on. Only meaningful\n"
                "after a successful run.");

        // Apply toggle changes immediately to live drawables.
        const bool ghost_changed = (prev_ghost != s.show_original_ghost);
        const bool meso_changed  = (prev_meso  != s.show_meso);
        const bool skel_changed  = (prev_skeleton != s.show_skeleton);
        const bool corr_changed  = (prev_corr != s.show_correspondence);
        const bool sdf_changed   = (prev_sdf  != s.show_sdf_heatmap);
        if (viewer && (corr_changed || sdf_changed) && win && s.last_result_valid) {
            const auto& r = s.last_result;
            if (corr_changed) {
                if (s.show_correspondence && !r.correspondence_lines.empty())
                    win->overlays().update_mcf_correspondence_overlay(r.correspondence_lines);
                else
                    win->overlays().clear_mcf_correspondence_overlay();
                viewer->mark_dirty();
            }
            if (sdf_changed && mesh) {
                if (s.show_sdf_heatmap && !r.sdf_per_input_vertex.empty())
                    win->overlays().paint_mcf_sdf_on_source(mesh, r.sdf_per_input_vertex, true);
                else
                    win->overlays().paint_mcf_sdf_on_source(mesh, r.sdf_per_input_vertex, false);
                viewer->mark_dirty();
            }
        }
        if (viewer && (ghost_changed || meso_changed || skel_changed)) {
            if (ghost_changed) {
                bool handled = false;
                if (win)
                    handled = win->overlays().set_mcf_source_ghost_visible(
                        s.show_original_ghost);
                if (!handled && mesh) {
                    mesh->renderer()->set_visible(s.show_original_ghost);
                    viewer->mark_dirty();
                }
            }
            if (meso_changed && win)
                win->overlays().set_mcf_meso_overlay_visible(s.show_meso);
            if (skel_changed && mesh) {
                const std::string skel_name = strip_mesh_ext(mesh->name())
                                              + ".mcf-skeleton";
                for (auto& mp : viewer->models()) {
                    if (auto* g = dynamic_cast<easy3d::Graph*>(mp.get())) {
                        if (g->name() == skel_name) {
                            g->renderer()->set_visible(s.show_skeleton);
                            viewer->mark_dirty();
                            break;
                        }
                    }
                }
            }
        }
        } // if (s.last_result_valid)

        ImGui::Separator();

        // --- Live preview UI ---
        ImGui::Text("Live preview");
        ImGui::Checkbox("Live Preview", &s.live_preview);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Run one MCF iteration at a time and animate the meso mesh "
                "shrinking. Slightly slower than fast mode but visually clear.");
        if (s.live_preview) {
            claw_ui::same_line_if_fits_width(180.0f);
            const char* speeds[] = {"Fast", "Normal", "Slow"};
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("##speed",
                         &s.preview_speed, speeds, IM_ARRAYSIZE(speeds));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "UI pacing only; does NOT change the numerical result.\n"
                    "Fast: ~60 ms/iteration (smooth).\n"
                    "Normal: ~180 ms/iteration (default).\n"
                    "Slow: ~480 ms/iteration (inspectable, good for teaching).");
        }

        ImGui::Separator();

        const bool busy = static_cast<bool>(s.runner);
        const bool can_run = input_ok && !busy;

        if (!busy) {
            ImGui::BeginDisabled(!can_run);
            if (ImGui::Button("Run") && can_run) {
                MCF_Config cfg;
                cfg.max_iterations                   = s.max_iterations;
                cfg.area_variation_factor            = (double)s.area_variation_factor;
                cfg.quality_speed_tradeoff           = (double)s.quality_speed_tradeoff;
                cfg.medially_centered_speed_tradeoff = (double)s.medially_centered_speed_tradeoff;
                cfg.medially_centered                = s.medially_centered;
                cfg.max_triangle_angle_degrees       = (double)s.max_triangle_angle_degrees;
                cfg.min_edge_length                  =
                    s.auto_min_edge_length ? -1.0 : (double)s.min_edge_length;
                // Data is cheap compared with skeletonization itself and
                // lets the post-run toggles work without forcing a re-run.
                cfg.collect_correspondence           = true;
                cfg.collect_sdf                      = true;
                cfg.max_correspondence_lines         = 3000;
                cfg.live_preview                     = s.live_preview;
                cfg.preview_speed                    = s.preview_speed;

                s.last_result_valid = false;
                s.result_base_name = strip_mesh_ext(mesh->name());
                s.last_input_metadata_prompt = build_mesh_metadata_prompt(mesh);
                s.close_requested  = false;
                s.last_error.clear();
                s.final_result_ready.store(false, std::memory_order_release);
                s.last_snap_gen    = -1;
                s.settling         = false;
                s.settle_started_at = 0.0;

                if (win && s.live_preview)
                    win->overlays().init_mcf_overlay(mesh);

                claw3d::services::McfSkeletonizationJobStart request;
                request.source_mesh = mesh;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(mesh)
                        : ModelHandle{};
                request.config = cfg;
                request.result_base_name = s.result_base_name;
                request.final_result_ready = &s.final_result_ready;
                request.wake_ui = []() { claw3d::app::wake_event_loop(); };

                if (win) {
                    s.runner =
                        claw3d::services::start_mcf_skeletonization_job(
                            win->algorithm_controller(), request);
                } else {
                    s.runner.reset();
                }
                if (!s.runner) {
                    if (s.live_preview)
                        win->overlays().clear_mcf_overlay(/*restore_source=*/true);
                    s.final_result_ready.store(true,
                        std::memory_order_release);
                    s.last_error = "Failed to start MCF skeletonization job.";
                    LOG(WARNING) << s.last_error;
                }
            }
            ImGui::EndDisabled();
        } else {
            // Drain the latest snapshot every frame so the overlay keeps up
            // with the worker. Only relevant in live mode.
            if (s.live_preview && s.runner && win) {
                MCF_Snapshot snap;
                if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                    !snap.vertices.empty())
                {
                    win->overlays().update_mcf_overlay(snap);
                }
            }
            if (s.runner && ImGui::Button("Cancel")) {
                s.runner.cancel();
                if (win)
                    win->algorithm_controller().request_cancel();
                claw3d::app::wake_event_loop();
            }
            claw_ui::same_line_if_fits_width(320.0f);
            if (s.runner) {
                const auto m = s.runner.result_metrics();
                ImGui::TextDisabled(
                    "iter %d/%d | meso V=%d F=%d | area change=%.2e",
                    m.iteration, m.max_iterations,
                    m.meso_vertices, m.meso_faces,
                    m.area_change_ratio);
            }
            if (s.runner && s.runner.is_cancelled() && !s.close_requested) {
                ImGui::TextDisabled("(cancel requested)");
            }
        }

        // Worker completion -> optional settle, then finalize.
        if (s.runner && busy && s.runner.is_done() &&
            s.final_result_ready.load(std::memory_order_acquire))
        {
            const bool close_after = s.close_requested;
            const bool finish_immediately =
                !s.live_preview || close_after ||
                s.runner.is_cancelled() || s.runner.has_error();
            if (finish_immediately) {
                // Cancel / error / close / fast-mode finish: fully restore
                // source so nothing left over from this run.
                if (s.live_preview && win)
                    win->overlays().clear_mcf_overlay(/*restore_source=*/true);
                if (s.runner.has_error())
                    s.last_error = s.runner.last_error();
                s.runner.get_result(s.last_result);
                s.last_result_valid = true;
                mark_algorithm_done(win);
                s.runner.reset();
                s.settling = false;
                if (close_after) open = false;
                s.close_requested = false;
                claw3d::app::wake_event_loop();
            } else if (!s.settling) {
                // Drain a final snapshot so the overlay matches the last
                // iteration before settle locks the view.
                MCF_Snapshot snap;
                if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                    win && !snap.vertices.empty())
                {
                    win->overlays().update_mcf_overlay(snap);
                }
                if (s.runner.has_error())
                    s.last_error = s.runner.last_error();
                s.runner.get_result(s.last_result);
                s.last_result_valid = true;
                if (win)
                    win->algorithm_controller().mark_final_preview_holding();
                s.settling = true;
                s.settle_started_at = ImGui::GetTime();
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Done. Holding contracted mesh for %.1fs ...",
                    s.settle_ms / 1000.0);
            } else {
                const double now = ImGui::GetTime();
                const double elapsed = (now - s.settle_started_at) * 1000.0;
                if (elapsed >= s.settle_ms) {
                    // Normal end: keep source as wireframe ghost so the
                    // freshly produced skeleton stays clearly readable.
                    if (win) win->overlays().clear_mcf_overlay(/*restore_source=*/false);
                    mark_algorithm_done(win);
                    s.runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    claw3d::app::wake_event_loop();
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding contracted mesh %.1fs / %.1fs ...",
                        elapsed / 1000.0, s.settle_ms / 1000.0);
                }
            }
        }

        // Post-run stats + AI evaluate.
        if (s.last_result_valid && !busy) {
            const auto& r = s.last_result;
            ImGui::Spacing();
            ImGui::Separator();
            if (r.error_code != MCF_ERR_None) {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "Error (%d): %s", mcf_error_code_value(r.error_code), r.error_message);
            } else {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Skeleton: V=%d E=%d  |  %.0f ms",
                    r.metrics.skeleton_vertices,
                    r.metrics.skeleton_edges,
                    r.metrics.elapsed_ms);
                ImGui::TextDisabled(
                    "Input V=%d F=%d  |  converged: %s  |  cancelled: %s",
                    r.metrics.original_vertices,
                    r.metrics.original_faces,
                    r.metrics.converged ? "yes" : "no",
                    r.metrics.cancelled ? "yes" : "no");
            }
            if (!s.last_error.empty()) {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "Runtime error: %s", s.last_error.c_str());
            }
            if (ImGui::Button("AI Evaluate Result")) {
                send_ai_prompt(win, build_result_evaluation_prompt(s, r),
                               "Ask AI: evaluate MCF skeleton result");
            }
            if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                ImGui::TextDisabled("Set API key in AI Chat");
            }
            claw_ui::same_line_if_fits_button("AI Parameter Advice");
            if (ImGui::Button("AI Parameter Advice") && mesh) {
                send_ai_prompt(win, build_parameter_advice_prompt(mesh, s),
                               "Ask AI: MCF parameter advice");
            }
        }

    } DIALOG_END;
}

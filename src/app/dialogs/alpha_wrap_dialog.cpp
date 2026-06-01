// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/alpha_wrap_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_language.h"
#include "services/jobs/cgal/alpha_wrap_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/state.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>
#include <GLFW/glfw3.h>

// =============================================================================
// AW3 AI help prompt + metadata builder
// =============================================================================

static const char* AW3_HELP_PROMPT = R"(
Explain Alpha Wrapping 3D (CGAL algorithm) in 3D Claw:

**Algorithm**: Alpha Wrapping (Portaneri et al., 2022). Shrink-wraps a watertight,
2-manifold triangle mesh around any input geometry (triangle soup, point cloud,
or mixed primitives). Guaranteed output: watertight, orientable, strictly enclosing.

**Algorithm overview**:
1. Start with a loose bounding box around the input
2. Iteratively "carve" Delaunay cells from outside-in through gates (facets)
3. When carving would expose the input, insert Steiner points on an offset surface
4. Gates smaller than alpha cannot be traversed -- cavities are sealed
5. Output = facets separating INSIDE from OUTSIDE cells

**Parameters**:
- alpha: minimum cavity/hole size to traverse. Acts as a sizing criterion.
  Small alpha (0.01-0.03): preserves fine detail, more triangles, slower
  Large alpha (0.1-0.5): coarse wrap, fast execution, fills holes aggressively
  Default 0.05 is balanced but SLOW for >10K points -- use 0.2+ for quick preview.
- offset: distance from output to input surface. Vertices placed on this surface.
  Smaller = tighter fit, larger = looser (better hole closure)
  Typical: alpha/10 to alpha/30. Default is alpha/30.

**Speed vs Quality strategy**:
- Quick preview: alpha=0.2-0.5, reduce input to 5K-10K points first
- Normal quality: alpha=0.05, up to 30K points (1-3 minutes)
- High quality: alpha=0.02, up to 50K points (5-15 minutes)
- For dense point clouds (>50K), downsample first with Grid Simplification
- For triangle soups, alpha > average edge length ensures good wrapping

**Common issues**:
- "No result after 3 minutes": alpha too small for input size -- increase alpha or downsample
- Too coarse/blobby result: decrease alpha, decrease offset
- Holes not filled: increase alpha to bridge the gap
- Thin features lost: decrease offset, may need smaller alpha
- Non-watertight output: increase alpha, or check input has no large gaps

Keep concise and practical. Suggest parameter values.
)";

static std::string build_aw3_metadata_prompt(easy3d::Model* model, float alpha, float offset) {
    if (!model) return std::string(AW3_HELP_PROMPT) + "\n" + ai_lang::directive();
    std::ostringstream meta;
    meta << "## Current Model Metadata\n\n| Property | Value |\n|----------|-------|\n";
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        int nv = sm->n_vertices(), nf = sm->n_faces();
        meta << "| Type | SurfaceMesh |\n| Vertices | " << nv << " |\n| Faces | " << nf << " |\n";
    } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(model)) {
        int np = pc->n_vertices();
        meta << "| Type | PointCloud |\n| Points | " << np << " |\n";
        if (np > 50000) meta << "| **Warning** | >50K pts! Consider downsample. |\n";
    }
    const auto& bbox = model->bounding_box();
    if (bbox.is_valid()) {
        float diag = bbox.diagonal_length();
        meta << "| BBox Diagonal | " << diag << " |\n| alpha | " << alpha
             << " |\n| offset | " << offset << " |\n";
    }
    meta << "\n";
    return meta.str() + "\n---\n\n" + AW3_HELP_PROMPT + "\n" + ai_lang::directive();
}


// =============================================================================
// 3.19 Alpha Wrapping 3D
// =============================================================================
void renderDialogAlphaWrapping(ViewportCanvas* viewer, AlphaWrappingState& s, bool& open) {
    prepare_dialog_window(560, 640);
    DIALOG_BODY("Alpha Wrapping 3D", open) {
        prereq_hint_only(prereq_mesh_or_pc(viewer));
        auto* model = viewer->current_model();
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
        auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
        bool valid = model && (mesh || cloud);

        render_panel_header(
            "Shrink-wrap a watertight, 2-manifold mesh around any input geometry.",
            "Broken / non-manifold meshes, triangle soup, dense point clouds.",
            [&]() {
                auto* win = MainWindow::instance();
                if (!win || !valid) return;
                if (!win->ai_chat()->HasApiKey()) {
                    ImGui::OpenPopup("##no_api_key_aw3");
                    return;
                }
                auto prompt = build_aw3_metadata_prompt(model, s.alpha, s.offset);
                win->send_ai_request(
                    prompt, AICtx_CurrentModel | AICtx_ActivePanel,
                    std::string(),
                    "Ask AI: Alpha Wrap parameter advice");
            });

        if (valid) {
            if (ImGui::BeginPopup("##no_api_key_aw3")) {
                ImGui::Text("Please set API Key in AI Chat panel first.");
                if (ImGui::Button("Open AI Chat")) {
                    MainWindow::instance()->show_ai_chat();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // Model info
            int n_pts = mesh ? mesh->n_vertices() : (cloud ? cloud->n_vertices() : 0);
            ImGui::TextColored(claw_ui::status_muted_color(), "Input: %d pts", n_pts);
            if (n_pts > 50000)
                ImGui::TextColored(claw_ui::status_warning_color(), ">50K pts -- consider downsample for speed");

            ImGui::InputFloat("Alpha", &s.alpha, 0.001f, 0.1f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Min cavity size to traverse (sizing criterion).\n"
                                  "SPEED: alpha=0.2-0.5 for quick preview (<30s)\n"
                                  "BALANCED: alpha=0.05-0.1 (1-3 min)\n"
                                  "QUALITY: alpha=0.01-0.03 for fine detail (5-15 min)");

            ImGui::InputFloat("Offset", &s.offset, 0.0001f, 0.01f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Distance from output vertices to input.\n"
                                  "Smaller = tighter fit. Larger = looser.\nTypical: alpha/10 to alpha/30.");

            ImGui::Spacing();
            ImGui::Text("Presets:");
            claw_ui::same_line_if_fits_button("Speed");
            if (ImGui::SmallButton("Speed"))  { s.alpha = 0.2f; s.offset = 0.005f; }
            claw_ui::same_line_if_fits_button("Balanced");
            if (ImGui::SmallButton("Balanced")) { s.alpha = 0.05f; s.offset = 0.002f; }
            claw_ui::same_line_if_fits_button("Quality");
            if (ImGui::SmallButton("Quality")) { s.alpha = 0.02f; s.offset = 0.001f; }
            ImGui::Spacing();

            auto* win = MainWindow::instance();
            bool busy = win && win->algorithm_controller().is_running();
            if (!busy) {
                if (ImGui::Button("AI Parameter Advice")) {
                    if (win && win->ai_chat() && win->ai_chat()->HasApiKey()) {
                        win->send_ai_request(
                            build_aw3_metadata_prompt(model, s.alpha, s.offset),
                            AICtx_CurrentModel | AICtx_ActivePanel,
                            std::string(),
                            "Ask AI: Alpha Wrap parameter advice");
                    } else {
                        ImGui::OpenPopup("##no_api_key_aw3");
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Ask AI to suggest Alpha and Offset for the current model.");
            }

            ImGui::Checkbox("Live Preview", &s.live_preview);
            claw_ui::same_line_if_fits_text("(?)");
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Real-time visualization during algorithm execution.\n"
                                  "Shows live Steiner point count, step, carved cells.\n"
                                  "Slightly slower than Fast mode (callback overhead).");
            if (s.live_preview) {
                bool live_vis_changed = false;
                const char* live_modes[] = {
                    "Cumulative",
                    "Fade Old",
                    "Recent Only"
                };
                live_vis_changed |= ImGui::Combo(
                    "Live Display", &s.live_display_mode, live_modes, 3);
                live_vis_changed |= ImGui::SliderInt(
                    "Recent Points", &s.live_recent_count, 50, 5000);
                live_vis_changed |= ImGui::Checkbox(
                    "Show Current Gate", &s.live_show_gate);
                if (s.live_show_gate) {
                    live_vis_changed |= ImGui::SliderInt(
                        "Gate Trail", &s.live_gate_trail_count, 1, 120);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Number of recent gates drawn as the live frontier trail.\n"
                                          "1 shows only the current gate.");
                }
                if (ImGui::Checkbox("Show Live Surface", &s.live_show_surface)) {
                    if (!s.live_show_surface && win) {
                        win->clear_aw3_live_surface_overlay();
                        s.running_surface_vertices = 0;
                        s.running_surface_faces = 0;
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Extract low-frequency intermediate surface snapshots.\n"
                                      "Takes effect when starting the next AW3 run.");
                if (s.live_show_surface) {
                    ImGui::SliderInt("Surface Interval", &s.live_surface_interval, 50, 2000);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Number of Steiner insertions between surface snapshots.");
                    bool surface_style_changed = false;
                    surface_style_changed |= ImGui::SliderFloat(
                        "Surface Opacity", &s.live_surface_opacity, 0.05f, 0.80f, "%.2f");
                    surface_style_changed |= ImGui::Checkbox(
                        "Surface Wireframe", &s.live_surface_wireframe);
                    if (surface_style_changed && win)
                        win->refresh_aw3_live_surface_style();
                }
                live_vis_changed |= ImGui::Checkbox(
                    "Clear Live Overlay After Finish", &s.live_clear_on_finish);
                if (live_vis_changed && win) {
                    std::vector<AW3_FrameEvent> no_events;
                    win->update_aw3_live_overlay(no_events, true);
                }
            }
#ifdef CLAW3D_HAS_CGAL
            auto drain_live_events = [&]() {
                auto runner = s.runner;
                if (!s.live_preview || !runner)
                    return;

                std::vector<AW3_FrameEvent> events;
                bool has_events = runner.drain_live_events(events);

                for (auto& ev : events) {
                    if (ev.type == AW3_FrameEvent::SteinerR1 ||
                        ev.type == AW3_FrameEvent::SteinerR2 ||
                        ev.type == AW3_FrameEvent::Gate ||
                        ev.type == AW3_FrameEvent::Progress ||
                        ev.type == AW3_FrameEvent::SurfaceSnapshot ||
                        ev.type == AW3_FrameEvent::Done) {
                        s.running_step.store(ev.step, std::memory_order_relaxed);
                        s.running_steiner.store(ev.num_steiner_total, std::memory_order_relaxed);
                        s.running_carved.store(ev.num_carved_total, std::memory_order_relaxed);
                        s.running_queue.store(ev.gate_queue_size, std::memory_order_relaxed);
                        if (ev.type == AW3_FrameEvent::SurfaceSnapshot) {
                            s.running_surface_vertices.store(ev.surface_vertices, std::memory_order_relaxed);
                            s.running_surface_faces.store(ev.surface_faces, std::memory_order_relaxed);
                        }
                    }
                }

                if (has_events && win)
                    win->update_aw3_live_overlay(events);

                if (s.live_show_surface && win) {
                    std::vector<AW3_Point3d> verts;
                    std::vector<AW3_Triangle> faces;
                    if (runner.drain_live_surface_snapshot(verts, faces)) {
                        s.running_surface_vertices.store((int)verts.size(), std::memory_order_relaxed);
                        s.running_surface_faces.store((int)faces.size(), std::memory_order_relaxed);
                        win->update_aw3_live_surface_overlay(verts, faces);
                    }
                }
            };

            if (busy) {
                // Drain live events each frame to keep stats and overlay current.
                drain_live_events();
                // Surface worker error to the dialog. The runner
                // is set even on the exception path (see worker catch block).
                {
                    auto err_runner = s.runner;
                    if (err_runner && err_runner.has_error()) {
                        ImGui::TextColored(claw_ui::status_error_color(),
                            "Worker error: %s", err_runner.last_error().c_str());
                        claw_ui::same_line_if_fits_button("Dismiss##aw3_err");
                        if (ImGui::SmallButton("Dismiss##aw3_err"))
                            err_runner.clear_error();
                    }
                }
                int r_step = s.running_step;
                if (s.live_preview && r_step > 0) {
                    int r_queue = s.running_queue.load(std::memory_order_relaxed);
                    int r_surface_v = s.running_surface_vertices.load(std::memory_order_relaxed);
                    int r_surface_f = s.running_surface_faces.load(std::memory_order_relaxed);
                    if (s.live_show_surface && r_surface_v > 0) {
                        if (r_queue > 0) {
                            ImGui::TextColored(claw_ui::status_success_color(),
                                "Running... Step %d | Steiner %d | Carved %d | GateQ %d | Surface %dV/%dF",
                                r_step,
                                s.running_steiner.load(std::memory_order_relaxed),
                                s.running_carved.load(std::memory_order_relaxed),
                                r_queue, r_surface_v, r_surface_f);
                        } else {
                            ImGui::TextColored(claw_ui::status_success_color(),
                                "Running... Step %d | Steiner %d | Carved %d | Surface %dV/%dF",
                                r_step,
                                s.running_steiner.load(std::memory_order_relaxed),
                                s.running_carved.load(std::memory_order_relaxed),
                                r_surface_v, r_surface_f);
                        }
                    } else if (r_queue > 0) {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Running... Step %d | Steiner %d | Carved %d | GateQ %d",
                            r_step,
                            s.running_steiner.load(std::memory_order_relaxed),
                            s.running_carved.load(std::memory_order_relaxed),
                            r_queue);
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Running... Step %d | Steiner %d | Carved %d",
                            r_step,
                            s.running_steiner.load(std::memory_order_relaxed),
                            s.running_carved.load(std::memory_order_relaxed));
                    }
                } else {
                    float elapsed = (float)ImGui::GetTime() - s.running_start_time;
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Running... %s (%.1fs)",
                        win->algorithm_controller().current_label().c_str(), elapsed);
                }
                claw_ui::same_line_if_fits_button("Cancel##aw3_cancel_run");
                if (ImGui::SmallButton("Cancel##aw3_cancel_run")) {
                    auto runner = s.runner;
                    if (runner) runner.cancel();
                }
            } else if (ImGui::Button("Run AW3")) {
                if (!win) { /* skip */ }
                else {
                    float alpha = s.alpha, offset = s.offset;
                    bool live = s.live_preview;
                    int surface_interval = (live && s.live_show_surface)
                        ? std::max(1, s.live_surface_interval) : 0;
                    s.runner.reset();
                    win->reset_aw3_process_overlay();
                    s.running_step = 0;
                    s.running_steiner = 0;
                    s.running_carved = 0;
                    s.running_queue = 0;
                    s.running_surface_vertices = 0;
                    s.running_surface_faces = 0;
                    s.running_start_time = (float)ImGui::GetTime();

                    claw3d::services::AlphaWrapJobStart request;
                    request.source_model = model;
                    request.source_handle =
                        (win && win->viewer())
                            ? win->viewer()->model_handle(model)
                            : ModelHandle{};
                    request.alpha = alpha;
                    request.offset = offset;
                    request.live_preview = live;
                    request.live_surface_interval = surface_interval;
                    request.source_name = model->name();
                    request.wake_ui = []() { glfwPostEmptyEvent(); };

                    auto runner = claw3d::services::start_alpha_wrap_job(
                        win->algorithm_controller(), request);
                    s.runner = runner;
                    if (!runner) {
                        LOG(WARNING) << "Failed to start Alpha Wrap job";
                    }
                }
            }
#else
            ImGui::TextColored(claw_ui::status_error_color(),
                "CGAL not available. Enable CLAW3D_ENABLE_CGAL in CMake.");
#endif
#ifdef CLAW3D_HAS_CGAL
            if (!busy && !s.live_clear_on_finish)
                drain_live_events();
            // Post-run error banner (also when busy=false). Same widget as the
            // running-state branch, just placed where it's visible after Run AW3
            // returns regardless of whether live preview was on.
            if (!busy) {
                auto err_runner = s.runner;
                if (err_runner && err_runner.has_error()) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Worker error: %s", err_runner.last_error().c_str());
                    claw_ui::same_line_if_fits_button("Dismiss##aw3_err_idle");
                    if (ImGui::SmallButton("Dismiss##aw3_err_idle"))
                        err_runner.clear_error();
                }
            }
#endif
            if (!busy) {
                claw_ui::same_line_if_fits_button("Cancel##aw3_close");
                if (ImGui::Button("Cancel##aw3_close")) open = false;
            }

            // AI result evaluation button (visible after AW3 completes with quality context).
            if (!busy && win && win->algorithm_controller().has_quality_context()) {
                ImGui::Spacing();
                const bool has_key = win->ai_chat() && win->ai_chat()->HasApiKey();
                if (!has_key)
                    ImGui::BeginDisabled();
                if (ImGui::Button("AI Evaluate Result")) {
                    const std::string quality_context =
                        win->algorithm_controller().take_quality_context();
                    win->send_ai_request(
                        std::string(
                            "Analyze the Alpha Wrapping 3D quality report above. "
                            "Based on the paper metrics (Hausdorff distance vs "
                            "alpha+offset bound, distance distribution, output "
                            "complexity), suggest parameter adjustments for alpha "
                            "and offset. ") + ai_lang::directive(),
                        AICtx_CurrentModel | AICtx_ActivePanel,
                        quality_context,
                        "Ask AI: evaluate Alpha Wrap result");
                }
                if (!has_key) {
                    ImGui::EndDisabled();
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
                claw_ui::same_line_if_fits_text("(?)");
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Send the quality report to AI for analysis.\n"
                                      "Requires API key configured in AI Chat panel.");
            }

        } else {
            ImGui::TextColored(claw_ui::status_warning_color(), "No model loaded or unsupported type.");
            ImGui::Text("Load a triangle mesh (SurfaceMesh) or point cloud first.");
        }
    } DIALOG_END;
}

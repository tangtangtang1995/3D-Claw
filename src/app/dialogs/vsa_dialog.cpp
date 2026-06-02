// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/vsa_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/vsa_approximation_job.h"
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
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static const char* VSA_HELP_PROMPT =
    "I am using CGAL Variational Shape Approximation (VSA) in 3D Claw.\n\n"
    "VSA partitions a triangle mesh into proxy regions (planar or point), then\n"
    "iteratively reassigns faces to minimize a fitting metric, and finally can\n"
    "extract an approximation mesh from the proxy boundaries.\n\n"
    "Metrics:\n"
    "  L21 - plane proxy, area-weighted squared normal deviation. Good default.\n"
    "  L2  - plane proxy, distance-based, sharper on flat industrial surfaces.\n\n"
    "Seeding:\n"
    "  Hierarchical - splits worst proxy until target count reached (default).\n"
    "  Incremental  - adds one proxy at a time at worst-error location.\n"
    "  Random       - picks initial seeds uniformly at random.\n\n"
    "Please suggest proxy count, metric, and seeding for my mesh.";

namespace {

const char* metric_label(int m) {
    switch (m) {
    case VSA_METRIC_L21: return "L21 (plane, normal-weighted)";
    case VSA_METRIC_L2:  return "L2 (plane, distance)";
    default:             return "Unknown";
    }
}

const char* metric_slug(int m) {
    switch (m) {
    case VSA_METRIC_L21: return "l21";
    case VSA_METRIC_L2:  return "l2";
    default:             return "unknown";
    }
}

const char* seeding_label(int s) {
    switch (s) {
    case VSA_SEED_Hierarchical: return "Hierarchical";
    case VSA_SEED_Incremental:  return "Incremental";
    case VSA_SEED_Random:       return "Random";
    default:                     return "Unknown";
    }
}

const char* seeding_slug(int s) {
    switch (s) {
    case VSA_SEED_Hierarchical: return "hier";
    case VSA_SEED_Incremental:  return "incr";
    case VSA_SEED_Random:       return "rand";
    default:                     return "unknown";
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

void append_parameter_block(std::ostringstream& oss, const VSAState& s) {
    oss << "Current VSA parameters:\n";
    oss << "- Metric: " << metric_label(s.metric) << "\n";
    oss << "- Seeding: " << seeding_label(s.seeding) << "\n";
    oss << "- Target proxies: " << s.target_proxies << "\n";
    oss << "- Iterations: " << s.iterations << "\n";
    oss << "- Relaxations per seed: " << s.relaxations << "\n";
    oss << "- Extract approximated mesh: "
        << (s.extract_mesh ? "yes" : "no") << "\n";
    oss << "- Random seed: " << s.seed << "\n";
    oss << "Metric notes:\n";
    oss << "- L21: plane proxy with normal-weighted error. Good general default,"
           " strong on organic / smooth surfaces.\n";
    oss << "- L2 : plane proxy with distance-based error. Often sharper on flat"
           " industrial / CAD surfaces.\n";
    oss << "Seeding notes:\n";
    oss << "- Hierarchical: splits the worst-error proxy until target reached.\n";
    oss << "- Incremental : adds one proxy at a time at worst-error face.\n";
    oss << "- Random      : initialize seeds at uniformly random face positions.\n";
}

std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const VSAState& s)
{
    std::ostringstream oss;
    oss << "Please give parameter advice for CGAL VSA Approximation in 3D Claw.\n\n";
    append_parameter_block(oss, s);
    oss << "\n";
    append_mesh_metadata(oss, collect_mesh_ai_stats(mesh));
    oss << "\nKeep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Recommend conservative / moderate / aggressive proxy counts.\n";
    oss << "2. Say which metric (L21 vs L2) is better for this mesh.\n";
    oss << "3. Say whether Hierarchical, Incremental, or Random seeding fits.\n";
    oss << "4. Flag risks: open boundaries, multiple components, non-manifold "
           "vertices, non-triangle faces.\n";
    oss << "5. Will extraction likely produce a manifold mesh, or should the "
           "user expect a non-manifold output?\n";
    return ascii_only(oss.str());
}

std::string build_result_evaluation_prompt(const VSAState& s,
                                           const VSA_DebugStats& st)
{
    std::ostringstream oss;
    oss << "Please evaluate this CGAL VSA Approximation run in 3D Claw.\n\n";
    // Reuse parameter block but with last_run_* values.
    VSAState lr = s;
    lr.metric          = s.last_run_metric;
    lr.seeding         = s.last_run_seeding;
    lr.target_proxies  = s.last_run_target_proxies;
    lr.iterations      = s.last_run_iterations;
    lr.relaxations     = s.last_run_relaxations;
    lr.extract_mesh    = s.last_run_extract_mesh;
    lr.seed            = s.last_run_seed;
    append_parameter_block(oss, lr);
    oss << "\n";
    if (!s.last_input_metadata_prompt.empty())
        oss << s.last_input_metadata_prompt << "\n";
    oss << "Run statistics:\n";
    oss << "- Input counts: vertices=" << st.initial_vertices
        << " faces=" << st.initial_faces
        << " components=" << st.initial_components << "\n";
    oss << "- Target proxies: " << st.target_proxies
        << " | seeded: " << st.seeds_inserted
        << " | final proxies: " << st.final_proxies << "\n";
    oss << "- Iterations completed: " << st.iterations_completed << "\n";
    oss << "- Initial fitting error: " << st.initial_error << "\n";
    oss << "- Final fitting error: " << st.final_error << "\n";
    oss << "- Extracted mesh: " << (st.extracted_mesh ? "yes" : "no")
        << " | manifold: " << (st.manifold_output ? "yes" : "no") << "\n";
    oss << "- Output mesh: vertices=" << st.final_vertices
        << " faces=" << st.final_faces << "\n";
    oss << "- Cancelled: " << (st.cancelled ? "yes" : "no") << "\n";
    oss << "- Runtime ms: total=" << st.ms_total
        << " convert_in=" << st.ms_convert_in
        << " seeding=" << st.ms_seeding
        << " iteration=" << st.ms_iteration
        << " extraction=" << st.ms_extraction
        << " convert_out=" << st.ms_convert_out << "\n\n";
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Did the fitting error drop sufficiently? Is the current proxy "
           "count too low or too high?\n";
    oss << "2. Was the metric / seeding choice reasonable for this mesh?\n";
    oss << "3. If extraction is non-manifold, should the user trust the output "
           "or rerun with different parameters?\n";
    oss << "4. What follow-up parameter changes would you try next?\n";
    oss << "5. What visual artifacts should be inspected on the extracted "
           "mesh?\n";
    return ascii_only(oss.str());
}

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

} // namespace

void renderDialogVSA(ViewportCanvas* viewer, VSAState& s, bool& open) {
    prepare_dialog_window(540, 560);
    DIALOG_BODY("CGAL VSA Approximation", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        const bool vsa_busy =
            s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::VsaApproximation);
        if (!open && vsa_busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            if (win)
                win->algorithm_controller().request_cancel();
            claw3d::app::wake_event_loop();
        }

        render_panel_header(
            "Variational shape approximation: extract proxy planes covering the mesh.",
            "Building / CAD-like models you want segmented into planar regions.",
            [win]() { send_ai_prompt(win, VSA_HELP_PROMPT,
                                     "Ask AI: VSA approximation help"); });
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
            const int nf = (int)mesh->n_faces();
            const int nv = (int)mesh->n_vertices();
            const bool is_triangle = mesh->is_triangle_mesh();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (v=%d f=%d) triangle=%s",
                mesh->name().c_str(), nv, nf,
                is_triangle ? "yes" : "no");
            if (!is_triangle) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "VSA requires a triangle mesh.");
            }

            // Auto-size target proxies the first time we see this mesh. The
            // formula matches the Default quick-button and keeps small meshes
            // from being over-segmented on the first run.
            if (s.initialized_for_faces != nf && nf > 0) {
                s.target_proxies = std::max(10, std::min(nf / 100, 200));
                s.initialized_for_faces = nf;
            }

#ifdef CLAW3D_HAS_CGAL
            const bool busy = win && win->algorithm_controller().is_running();

            {
                const char* items[] = {
                    "L21 (plane, normal)",
                    "L2 (plane, distance)"
                };
                ImGui::Combo("Metric", &s.metric, items, IM_ARRAYSIZE(items));
            }
            {
                const char* items[] = {
                    "Hierarchical", "Incremental", "Random"
                };
                ImGui::Combo("Seeding", &s.seeding, items,
                             IM_ARRAYSIZE(items));
            }

            ImGui::InputInt("Target Proxies", &s.target_proxies, 5, 25);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Number of proxy regions to fit. "
                    "Default: max(faces/100, 10).");
            if (s.target_proxies < 2)   s.target_proxies = 2;
            if (s.target_proxies > 2000) s.target_proxies = 2000;
            claw_ui::same_line_if_fits_button("Default");
            if (ImGui::SmallButton("Default"))
                s.target_proxies = std::max(10, std::min(nf / 100, 500));
            claw_ui::same_line_if_fits_button("20");
            if (ImGui::SmallButton("20"))  s.target_proxies = 20;
            claw_ui::same_line_if_fits_button("50");
            if (ImGui::SmallButton("50"))  s.target_proxies = 50;
            claw_ui::same_line_if_fits_button("100");
            if (ImGui::SmallButton("100")) s.target_proxies = 100;
            claw_ui::same_line_if_fits_button("200");
            if (ImGui::SmallButton("200")) s.target_proxies = 200;

            ImGui::InputInt("Iterations", &s.iterations, 5, 25);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Number of partition+fit passes after seeding. "
                    "Default: 30. More iterations = better fit but slower.");
            if (s.iterations < 0) s.iterations = 0;
            if (s.iterations > 500) s.iterations = 500;

            if (ImGui::CollapsingHeader("Advanced")) {
                ImGui::InputInt("Relaxations per Seed",
                                &s.relaxations, 1, 5);
                if (s.relaxations < 0)  s.relaxations = 0;
                if (s.relaxations > 50) s.relaxations = 50;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Iterations executed between proxy insertions "
                        "during seeding (Hierarchical / Incremental).");

                ImGui::InputInt("Random Seed", &s.seed, 1, 10);
                if (s.seed < 0) s.seed = 0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Random seed for Random seeding and for "
                        "tie-breaking in the other modes.");
            }

            ImGui::Spacing();
            ImGui::Checkbox("Extract Approximated Mesh", &s.extract_mesh);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "After clustering, extract a triangle mesh from the "
                    "proxy boundaries. Disable to only compute the "
                    "face-to-proxy assignment.");

            ImGui::Checkbox("Live Preview", &s.live_preview);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Show the face->proxy assignment evolve during the run.\n"
                    "Costs one proxy_map() pass per iteration; expect the\n"
                    "run to be ~2x slower than fast mode.");
            if (s.live_preview) {
                claw_ui::same_line_if_fits_width(220.0f);
                const char* speed_items[] = {"Normal", "Slow"};
                ImGui::Combo("Speed", &s.preview_speed, speed_items,
                             IM_ARRAYSIZE(speed_items));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Normal: paint every snapshot as it arrives.\n"
                        "Slow  : paint at most 1 snapshot every ~240 ms\n"
                        "        so the spreading regions are readable.");

                ImGui::Checkbox("Staged Growth", &s.staged_growth);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Start with 8 proxies, then grow in batches of\n"
                        "~target/20 with a few relaxations between batches.\n"
                        "More dramatic 'islands appear and spread' animation\n"
                        "but not numerically identical to fast mode.");
                claw_ui::same_line_if_fits_text("Keep Segmentation");
                ImGui::Checkbox("Keep Segmentation", &s.keep_segmentation);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Convert the cluster overlay into a persistent child\n"
                        "mesh under the source when the run finishes\n"
                        "(named <source>_vsa_segments_<metric>_<proxies>p).");
            }

            if (!busy) {
                ImGui::Spacing();
                if (ImGui::Button("AI Parameter Advice")) {
                    send_ai_prompt(win,
                        build_parameter_advice_prompt(mesh, s),
                        "Ask AI: VSA parameter advice");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Send current mesh metadata and VSA parameters "
                        "to AI.");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            // Progress text + live snapshot polling while busy.
            constexpr int slow_preview_ms = claw3d::preview_policy::kSlowPreviewUiMs;
            if (s.runner && busy) {
                s.runner.copy_error_if_any(s.last_error);
                const char* phase_text = "Running...";
                if (s.live_snapshot_valid) {
                    if (s.live_snapshot_phase == 0)
                        phase_text = "Seeding...";
                    else if (s.live_snapshot_phase == 1)
                        phase_text = "Iterating...";
                    else
                        phase_text = "Finishing...";
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Phase: %s | proxies=%d/%d iters=%d/%d | err=%.4g",
                        phase_text, s.live_snapshot_proxies,
                        s.live_snapshot_target, s.live_snapshot_iteration,
                        s.last_run_iterations, s.live_snapshot_error);
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Phase: %s", phase_text);
                }

                if (s.live_preview && win) {
                    VSA_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap)) {
                        s.live_snapshot_valid = true;
                        s.live_snapshot_phase = snap.phase;
                        s.live_snapshot_iteration = snap.iteration;
                        s.live_snapshot_proxies = snap.proxies;
                        s.live_snapshot_target = snap.target_proxies;
                        s.live_snapshot_error = snap.total_error;
                        bool show_now = true;
                        if (s.preview_speed == 1) {
                            show_now = claw_ui::frame_due(
                                ImGui::GetTime(),
                                s.last_overlay_display_time,
                                slow_preview_ms);
                        }
                        if (show_now) {
                            win->overlays().update_vsa_cluster_overlay(
                                snap.face_proxy_ids);
                            // Publish proxy seed positions once on the first
                            // snapshot (seeding phase). Centers may jitter as
                            // membership changes, but the initial set is the
                            // most useful "where the regions started" cue.
                            if (!s.seeds_published &&
                                !snap.proxies_info.empty())
                            {
                                std::vector<VSA_Point3d> seeds;
                                seeds.reserve(snap.proxies_info.size());
                                for (const auto& p : snap.proxies_info)
                                    seeds.push_back(p.seed_center);
                                win->overlays().update_vsa_seed_overlay(seeds);
                                s.seeds_published = true;
                            }
                        }
                    }
                    // Keep the event loop awake; small meshes can outrun the
                    // viewport's natural redraw cadence.
                    claw3d::app::wake_event_loop();
                }
            }

            // Run / Cancel buttons.
            if (!busy) {
                if (ImGui::Button("Run")) {
                    if (!is_triangle) {
                        LOG(WARNING)
                            << "VSA: input is not a triangle mesh";
                    } else {
                        VSA_Config cfg;
                        cfg.metric         = s.metric;
                        cfg.seeding        = s.seeding;
                        cfg.target_proxies = s.target_proxies;
                        cfg.iterations     = s.iterations;
                        cfg.relaxations    = s.relaxations;
                        cfg.extract_mesh   = s.extract_mesh;
                        cfg.random_seed    = (unsigned)s.seed;
                        cfg.live_preview   = s.live_preview;
                        cfg.preview_speed  = s.preview_speed;
                        cfg.staged_growth  = s.staged_growth;
                        // Auto-throttle: large meshes get a wider
                        // snapshot gate so the worker does not flood the
                        // UI thread with proxy_map dumps.
                        if (nf > claw3d::preview_policy::kHugeMeshFaceCount)      cfg.snapshot_min_ms = claw3d::preview_policy::kHugeMeshSnapshotMinMs;
                        else if (nf > claw3d::preview_policy::kLargeMeshFaceCount) cfg.snapshot_min_ms = claw3d::preview_policy::kLargeMeshSnapshotMinMs;
                        else                   cfg.snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;

                        s.last_stats_valid = false;
                        s.last_snap_gen    = -1;
                        s.live_snapshot_valid = false;
                        s.live_snapshot_phase = 0;
                        s.live_snapshot_iteration = 0;
                        s.live_snapshot_proxies = 0;
                        s.live_snapshot_target = cfg.target_proxies;
                        s.live_snapshot_error = 0.0;
                        s.last_overlay_display_time = 0.0;
                        s.settling         = false;
                        s.settle_started_at = 0.0;
                        s.seeds_published  = false;
                        s.last_input_metadata_prompt =
                            build_mesh_metadata_prompt(mesh);
                        s.last_run_metric         = cfg.metric;
                        s.last_run_seeding        = cfg.seeding;
                        s.last_run_target_proxies = cfg.target_proxies;
                        s.last_run_iterations     = cfg.iterations;
                        s.last_run_relaxations    = cfg.relaxations;
                        s.last_run_extract_mesh   = cfg.extract_mesh;
                        s.last_run_seed           = (int)cfg.random_seed;
                        s.close_requested         = false;
                        s.last_error.clear();
                        s.final_result_ready.store(false,
                            std::memory_order_release);

                        if (s.live_preview && win)
                            win->overlays().init_vsa_overlay(mesh);

                        claw3d::services::VsaApproximationJobStart request;
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
                            s.runner =
                                claw3d::services::start_vsa_approximation_job(
                                    win->algorithm_controller(), request);
                        } else {
                            s.runner.reset();
                        }
                        if (!s.runner) {
                            if (s.live_preview && win)
                                win->overlays().clear_vsa_overlay();
                            s.final_result_ready.store(true,
                                std::memory_order_release);
                            s.last_error =
                                "Failed to start VSA approximation job.";
                            LOG(WARNING) << s.last_error;
                        }
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
                    claw_ui::same_line_if_fits_text("Closing after VSA stops...");
                    ImGui::TextDisabled("Closing after VSA stops...");
                }
            }

            // Worker completion -> optional settle phase, then handoff to
            // the controller in the main loop.
            if (busy &&
                s.runner.done_and_ready(s.final_result_ready))
            {
                const bool close_after = s.close_requested;
                const bool finish_immediately =
                    !s.live_preview || close_after ||
                    s.runner.cancelled_or_failed();

                // Either tear the overlay down or promote it to a persistent
                // child mesh, depending on Keep Segmentation + run health.
                auto teardown_overlay = [&]() {
                    if (!s.live_preview || !win) return;
                    auto* src = dynamic_cast<easy3d::SurfaceMesh*>(
                        resolve_current_algorithm_source(win));
                    const bool keep_ok = s.keep_segmentation &&
                        !s.runner.cancelled_or_failed() &&
                        src;
                    if (keep_ok) {
                        char nm[200];
                        std::snprintf(nm, sizeof(nm),
                            "%s.vsa-segments-%s-%dp",
                            src ? src->name().c_str() : "vsa",
                            metric_slug(s.last_run_metric),
                            s.last_run_target_proxies);
                        win->overlays().promote_vsa_overlay_to_child(src, nm);
                    } else {
                        win->overlays().clear_vsa_overlay();
                    }
                };

                if (finish_immediately) {
                    teardown_overlay();
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.debug_stats();
                    s.last_stats_valid = true;
                    s.last_proxy_count = s.last_stats.final_proxies;
                    mark_algorithm_done(win);
                    s.runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    if (close_after) open = false;
                    claw3d::app::wake_event_loop();
                } else if (!s.settling) {
                    // Pull the absolute last snapshot in case we missed one
                    // between the last poll and the worker finishing.
                    VSA_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                        win)
                    {
                        win->overlays().update_vsa_cluster_overlay(snap.face_proxy_ids);
                    }
                    s.runner.copy_error_if_any(s.last_error);
                    s.last_stats = s.runner.debug_stats();
                    s.last_stats_valid = true;
                    s.last_proxy_count = s.last_stats.final_proxies;
                    if (win)
                        win->algorithm_controller().mark_final_preview_holding();
                    s.settling = true;
                    s.settle_started_at = ImGui::GetTime();
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding cluster view for %.1fs ...",
                        s.settle_ms / 1000.0);
                } else {
                    const double now = ImGui::GetTime();
                    const double elapsed =
                        (now - s.settle_started_at) * 1000.0;
                    if (elapsed >= s.settle_ms) {
                        teardown_overlay();
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
                    "Result: %d proxies | error %.4g -> %.4g | %.0f ms",
                    st.final_proxies, st.initial_error, st.final_error,
                    st.ms_total);
                ImGui::TextDisabled(
                    "Metric: %s | seeding: %s | iters: %d | seeds: %d",
                    metric_label(st.target_proxies > 0
                                     ? s.last_run_metric : s.metric),
                    seeding_label(s.last_run_seeding),
                    st.iterations_completed, st.seeds_inserted);
                if (st.extracted_mesh) {
                    ImGui::TextDisabled(
                        "Extracted: anchors=%d faces=%d | manifold=%s",
                        st.final_vertices, st.final_faces,
                        st.manifold_output ? "yes" : "no");
                    if (!st.manifold_output) {
                        ImGui::TextColored(claw_ui::status_warning_color(),
                            "Non-manifold extraction; source mesh kept "
                            "visible.");
                    }
                } else {
                    ImGui::TextDisabled(
                        "Extraction disabled; segmentation only.");
                }
                ImGui::TextDisabled(
                    "Timing ms: convert_in=%.0f seed=%.0f iter=%.0f "
                    "extract=%.0f convert_out=%.0f",
                    st.ms_convert_in, st.ms_seeding, st.ms_iteration,
                    st.ms_extraction, st.ms_convert_out);
                if (st.cancelled)
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Status: cancelled");
                if (!s.last_error.empty()) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", s.last_error.c_str());
                }
                if (ImGui::Button("AI Evaluate Result")) {
                    send_ai_prompt(win,
                        build_result_evaluation_prompt(s, st),
                        "Ask AI: evaluate VSA result");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Send VSA run statistics to AI for evaluation.");
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

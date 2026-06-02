// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/cgal_simplification_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/cgal_simplification_job.h"
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
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// AI prompt text is kept ASCII-only for predictable cross-platform rendering.
// ============================================================================

static const char* CGAL_SIMPL_HELP_PROMPT_ASCII =
    "I'm using CGAL Surface_mesh_simplification for mesh reduction via edge "
    "collapse. Please explain the strategies and suggest a target ratio.\n\n"
    "Strategies in this panel:\n"
    "1. Lindstrom-Turk - CGAL default, memoryless, robust, slower.\n"
    "2. Garland-Heckbert Plane - classic QEM, good shape preservation,\n"
    "   moderately fast.\n"
    "3. Edge Length + Midpoint - cheapest, mostly geometric, may damage\n"
    "   features but useful for fast LOD.\n\n"
    "Additional strategies are also available: Garland-Heckbert Triangle and\n"
    "probabilistic Plane/Triangle variants. Protection options include\n"
    "bounded normal change and polyhedral envelope.\n\n"
    "Stop modes: edge ratio / edge count / face ratio / face count.\n"
    "Edge ratio 0.5 keeps 50% of edges; ratio 0.25 reduces to 25%.\n\n"
    "Please answer:\n"
    "- Which strategy fits typical 3D-scan / CAD / sculpted meshes?\n"
    "- A reasonable starting target ratio for moderate LOD vs aggressive\n"
    "  compression.\n"
    "- Whether live preview is expensive (yes; default off for big meshes).\n"
    "- Any other tips for first-time users.";

// ============================================================================
// helpers
// ============================================================================

namespace {

const char* strategy_label(int s) {
    switch (s) {
    case SIMPL_STRAT_LindstromTurk:        return "Lindstrom-Turk";
    case SIMPL_STRAT_GarlandHeckbertPlane: return "Garland-Heckbert Plane";
    case SIMPL_STRAT_EdgeLengthMidpoint:   return "Edge Length + Midpoint";
    case SIMPL_STRAT_GarlandHeckbertTriangle:
        return "Garland-Heckbert Triangle";
    case SIMPL_STRAT_GarlandHeckbertProbabilisticPlane:
        return "Garland-Heckbert Probabilistic Plane";
    case SIMPL_STRAT_GarlandHeckbertProbabilisticTriangle:
        return "Garland-Heckbert Probabilistic Triangle";
    default:                                return "Unknown";
    }
}

const char* stop_mode_label(int m) {
    switch (m) {
    case SIMPL_STOP_EdgeRatio: return "Edge Ratio";
    case SIMPL_STOP_EdgeCount: return "Edge Count";
    case SIMPL_STOP_FaceRatio: return "Face Ratio";
    case SIMPL_STOP_FaceCount: return "Face Count";
    default:                    return "Unknown";
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

double mesh_bbox_diag_or_one(easy3d::SurfaceMesh* mesh) {
    if (!mesh)
        return 1.0;
    const auto& bbox = mesh->bounding_box();
    if (!bbox.is_valid())
        return 1.0;
    const double diag = (double)bbox.diagonal_length();
    return diag > 0.0 ? diag : 1.0;
}

double envelope_epsilon_from_state(easy3d::SurfaceMesh* mesh,
                                   const CGALSimplificationState& s)
{
    const double pct = std::max(0.0001, (double)s.envelope_relative_percent);
    return mesh_bbox_diag_or_one(mesh) * pct * 0.01;
}

void append_parameter_block(std::ostringstream& oss,
                            int strategy,
                            int stop_mode,
                            float target_ratio,
                            int target_count,
                            bool live_preview,
                            int preview_speed,
                            bool bounded_normal_change,
                            bool polyhedral_envelope,
                            double envelope_epsilon)
{
    oss << "Current parameters:\n";
    oss << "- Strategy: " << strategy_label(strategy) << "\n";
    oss << "- Stop mode: " << stop_mode_label(stop_mode) << "\n";
    if (stop_mode == SIMPL_STOP_EdgeRatio ||
        stop_mode == SIMPL_STOP_FaceRatio)
        oss << "- Target ratio to keep: " << target_ratio << "\n";
    else
        oss << "- Target count: " << target_count << "\n";
    oss << "- Live preview: " << (live_preview ? "on" : "off") << "\n";
    if (live_preview)
        oss << "- Preview speed: "
            << (preview_speed == 1 ? "Slow" : "Normal") << "\n";
    oss << "- Bounded normal change: "
        << (bounded_normal_change ? "on" : "off") << "\n";
    oss << "- Polyhedral envelope: "
        << (polyhedral_envelope ? "on" : "off");
    if (polyhedral_envelope)
        oss << " epsilon=" << envelope_epsilon;
    oss << "\n";
    oss << "Strategy comparison notes:\n";
    oss << "- Lindstrom-Turk: robust default, good baseline.\n";
    oss << "- Garland-Heckbert Plane: classic QEM-like shape preservation.\n";
    oss << "- Garland-Heckbert Triangle: triangle quadric variant, often "
           "worth comparing on scan meshes.\n";
    oss << "- Probabilistic variants: more noise-aware, compare on rough or "
           "scanned input.\n";
    oss << "- Edge Length + Midpoint: fastest geometric baseline, may be "
           "visibly rougher.\n";
}

std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const CGALSimplificationState& s)
{
    std::ostringstream oss;
    const double envelope_eps =
        s.use_polyhedral_envelope ? envelope_epsilon_from_state(mesh, s) : 0.0;
    oss << "Please give parameter advice for CGAL Surface_mesh_simplification "
           "(edge-collapse mesh reduction) in 3D Claw.\n\n";
    append_parameter_block(oss, s.strategy, s.stop_mode, s.target_ratio,
                           s.target_count, s.live_preview, s.preview_speed,
                           s.use_bounded_normal_change,
                           s.use_polyhedral_envelope, envelope_eps);
    oss << "\n";
    append_mesh_metadata(oss, collect_mesh_ai_stats(mesh));
    oss << "\nKeep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Is the selected strategy suitable for this mesh type?\n";
    oss << "2. Recommend a safe target ratio/count for first run, moderate LOD, "
           "and aggressive compression.\n";
    oss << "3. Point out any risks from boundary edges, non-triangle faces, "
           "degenerate faces, or non-manifold vertices.\n";
    oss << "4. Say whether live preview may be expensive for this model.\n";
    oss << "5. Suggest which strategy to compare next.\n";
    return ascii_only(oss.str());
}

std::string build_result_evaluation_prompt(
    const CGALSimplificationState& s,
    const SIMPL_DebugStats& st)
{
    std::ostringstream oss;
    oss << "Please evaluate this CGAL Surface_mesh_simplification run in "
           "3D Claw.\n\n";
    append_parameter_block(oss, s.last_run_strategy, s.last_run_stop_mode,
                           s.last_run_target_ratio, s.last_run_target_count,
                           s.last_run_live_preview, s.last_run_preview_speed,
                           s.last_run_bounded_normal_change,
                           s.last_run_polyhedral_envelope,
                           s.last_run_envelope_epsilon);
    oss << "\n";
    if (!s.last_input_metadata_prompt.empty())
        oss << s.last_input_metadata_prompt << "\n";
    oss << "Run statistics:\n";
    oss << "- Input counts: vertices=" << st.initial_vertices
        << " edges=" << st.initial_edges
        << " faces=" << st.initial_faces << "\n";
    oss << "- Output counts: vertices=" << st.current_vertices
        << " edges=" << st.current_edges
        << " faces=" << st.current_faces << "\n";
    oss << "- Edge reduction ratio: " << std::fixed << std::setprecision(2)
        << (100.0 * st.reduction_ratio) << "%\n";
    oss.unsetf(std::ios::floatfield);
    oss << "- Selected candidates: " << st.selected_count
        << " collapsed: " << st.collapsed_count
        << " rejected/non-collapsable: " << st.rejected_count << "\n";
    if (st.cost_count > 0) {
        oss << "- Cost stats: count=" << st.cost_count
            << " min=" << st.cost_min
            << " max=" << st.cost_max
            << " mean=" << st.cost_mean
            << " recent_mean=" << st.cost_recent_mean << "\n";
    } else {
        oss << "- Cost stats: unavailable\n";
    }
    oss << "- Stop reached: " << (st.stop_reached ? "yes" : "no")
        << " cancelled: " << (st.cancelled ? "yes" : "no") << "\n";
    oss << "- Runtime ms: total=" << st.ms_total
        << " setup=" << st.ms_setup
        << " collapse=" << st.ms_collapse
        << " convert=" << st.ms_convert << "\n\n";
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Did it reach the requested target?\n";
    oss << "2. Is the reduction conservative, moderate, or aggressive?\n";
    oss << "3. Does the strategy match the mesh and the observed rejected ratio?\n";
    oss << "4. What parameter should the user try next if the mesh is too dense "
           "or too faceted?\n";
    oss << "5. Which strategy should be compared next, and why?\n";
    return ascii_only(oss.str());
}

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

}  // namespace

// ============================================================================
// Dialog
// ============================================================================

void renderDialogCGALSimplification(ViewportCanvas* viewer,
                                    CGALSimplificationState& s,
                                    bool& open)
{
    prepare_dialog_window(520, 520);
    DIALOG_BODY("CGAL Simplification", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        render_panel_header(
            "Decimate a mesh with quality-preserving edge collapses (Lindstrom-Turk / Garland-Heckbert).",
            "Game assets, LODs, when sharp features should be preserved.",
            [win]() { send_ai_prompt(win, CGAL_SIMPL_HELP_PROMPT_ASCII,
                                     "Ask AI: CGAL simplification help"); });
        ImGui::Spacing();
        auto* viewer = win ? win->viewer() : nullptr;

        // ---- Source mesh pick: current model if it's a SurfaceMesh, else
        //      first SurfaceMesh in the scene ----
        easy3d::SurfaceMesh* mesh = nullptr;
        if (viewer) {
            mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (!mesh) {
                for (auto& mp : viewer->models()) {
                    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(mp.get())) {
                        mesh = sm;
                        break;
                    }
                }
            }
        }

        bool can_run = false;
        if (!mesh) {
            ImGui::TextColored(claw_ui::status_error_color(),
                "No surface mesh loaded. Load a mesh first.");
            if (ImGui::Button("Close")) open = false;
        } else {
            const int n_v = (int)mesh->n_vertices();
            const int n_e = (int)mesh->n_edges();
            const int n_f = (int)mesh->n_faces();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (v=%d e=%d f=%d)",
                mesh->name().c_str(), n_v, n_e, n_f);
            const bool is_tri = mesh->is_triangle_mesh();
            if (!is_tri) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Mesh is not pure triangles. Non-triangle faces are dropped.");
            }
            can_run = (n_f > 0);
        }

        if (can_run) {
            // ---- Strategy combo ----
            {
                const char* items[] = {
                    "Lindstrom-Turk",
                    "Garland-Heckbert Plane",
                    "Edge Length + Midpoint",
                    "Garland-Heckbert Triangle",
                    "Garland-Heckbert Probabilistic Plane",
                    "Garland-Heckbert Probabilistic Triangle"
                };
                int cur = s.strategy;
                if (cur < 0 || cur > 5) cur = 0;
                if (ImGui::Combo("Strategy", &cur, items, IM_ARRAYSIZE(items))) {
                    s.strategy = cur;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Lindstrom-Turk: CGAL default, robust, slower.\n"
                        "Garland-Heckbert Plane: classic QEM.\n"
                        "Edge Length + Midpoint: cheapest, geometric only.\n"
                        "Garland-Heckbert Triangle: triangle quadrics.\n"
                        "Probabilistic variants: noise-aware quadrics.");
                }
            }

            // ---- Stop mode combo ----
            {
                const char* items[] = {
                    "Edge Ratio (keep X%)",
                    "Edge Count (target)",
                    "Face Ratio (keep X%)",
                    "Face Count (target)"
                };
                int cur = s.stop_mode;
                if (cur < 0 || cur > 3) cur = 0;
                if (ImGui::Combo("Stop Mode", &cur, items, IM_ARRAYSIZE(items))) {
                    s.stop_mode = cur;
                }
            }

            // ---- Target ----
            if (s.stop_mode == SIMPL_STOP_EdgeRatio ||
                s.stop_mode == SIMPL_STOP_FaceRatio)
            {
                ImGui::SliderFloat("Target Ratio", &s.target_ratio, 0.01f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fraction of edges/faces to KEEP. 0.5 = halve. "
                                      "Smaller = more aggressive simplification.");
            } else {
                int tgt = s.target_count;
                if (tgt < 0) tgt = 0;
                if (ImGui::InputInt("Target Count", &tgt)) {
                    if (tgt < 0) tgt = 0;
                    s.target_count = tgt;
                }
            }

            ImGui::Spacing();
            ImGui::Checkbox("Live Preview", &s.live_preview);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Animate the collapse process: source ghost, "
                                  "recent collapse trail, and stepped snapshot mesh.");
            if (s.live_preview) {
                ImGui::Indent();
                const char* speed_items[] = {"Normal", "Slow"};
                ImGui::Combo("Preview Speed", &s.preview_speed,
                             speed_items, IM_ARRAYSIZE(speed_items));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Normal applies snapshots immediately. Slow buffers "
                        "snapshots and displays them at a gentler visual pace; "
                        "the simplification algorithm and final output stay "
                        "unchanged.");
                }
                ImGui::SliderInt("Snapshot Interval (ms)",
                                 &s.snapshot_min_ms, 20, 500);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Floor on wall time between snapshots. Lower = more "
                        "frames, but each snapshot walks the mid-simplification "
                        "mesh, so going too low can slow the worker on big "
                        "meshes. Default 80 ms.");
                }
                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Protection");
            ImGui::Checkbox("Bounded Normal Change",
                            &s.use_bounded_normal_change);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Reject placements that would flip adjacent face normals. "
                    "This usually preserves visual quality but may stop before "
                    "very aggressive targets.");
            }
            ImGui::Checkbox("Polyhedral Envelope", &s.use_polyhedral_envelope);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Reject placements outside a tolerance envelope around "
                    "the original mesh. More conservative and may be slower.");
            }
            if (s.use_polyhedral_envelope) {
                ImGui::Indent();
                ImGui::SliderFloat("Envelope (% BBox)",
                                   &s.envelope_relative_percent,
                                   0.01f, 5.0f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Absolute envelope distance is computed as this "
                        "percentage of the source bounding-box diagonal. "
                        "Smaller values preserve shape more tightly but reject "
                        "more collapses.");
                }
                ImGui::Unindent();
            }

#ifdef CLAW3D_HAS_CGAL
            const bool busy = win && win->algorithm_controller().is_running();
            constexpr int slow_preview_ms = claw3d::preview_policy::kSlowPreviewUiMs;

            // ---- AI parameter advice ----
            if (!busy) {
                if (ImGui::Button("AI Parameter Advice")) {
                    send_ai_prompt(win, build_parameter_advice_prompt(mesh, s),
                                   "Ask AI: CGAL simplification parameter advice");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Send current mesh metadata and simplification "
                        "parameters to AI for parameter advice.");
                }
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            // ---- Live event drain (only when live preview is active) ----
            if (s.runner && busy && s.live_preview) {
                std::vector<SIMPL_FrameEvent> events;
                if (s.runner.drain_live_events(events)) {
                    std::vector<SimplTrailEntry> trail_new;
                    trail_new.reserve(events.size());
                    for (const auto& ev : events) {
                        if (ev.type == SIMPL_FrameEvent::Collapsing) {
                            SimplTrailEntry e;
                            e.p0        = easy3d::vec3((float)ev.p0[0],
                                                       (float)ev.p0[1],
                                                       (float)ev.p0[2]);
                            e.p1        = easy3d::vec3((float)ev.p1[0],
                                                       (float)ev.p1[1],
                                                       (float)ev.p1[2]);
                            e.placement = easy3d::vec3((float)ev.placement[0],
                                                       (float)ev.placement[1],
                                                       (float)ev.placement[2]);
                            trail_new.push_back(e);
                        }
                    }
                    if (win && !trail_new.empty()) {
                        bool update_trail = true;
                        if (s.preview_speed == 1) {
                            update_trail = claw_ui::frame_due(
                                ImGui::GetTime(),
                                s.last_trail_display_time,
                                slow_preview_ms);
                        }
                        if (update_trail)
                            win->overlays().update_simpl_overlay(trail_new);
                    }
                }

                // Snapshot poll: cheap atomic check; we only acquire the
                // snapshot mutex + copy when there's a newer generation.
                std::vector<SIMPL_Point3d>  snap_v;
                std::vector<SIMPL_Triangle> snap_t;
                if (s.runner.poll_snapshot(s.last_snapshot_gen_seen,
                                           snap_v, snap_t))
                {
                    if (s.preview_speed == 1) {
                        CGALSimplificationState::SnapshotFrame frame;
                        frame.verts = std::move(snap_v);
                        frame.tris  = std::move(snap_t);
                        const std::size_t max_pending = 16;
                        if (s.pending_snapshots.size() >= max_pending)
                            s.pending_snapshots.pop_front();
                        s.pending_snapshots.push_back(std::move(frame));
                    } else {
                        s.pending_snapshots.clear();
                        if (win) win->overlays().update_simpl_snapshot_mesh(
                            snap_v, snap_t, 1.0f);
                    }
                }

                if (s.preview_speed == 1 && !s.pending_snapshots.empty()) {
                    if (claw_ui::frame_due(
                            ImGui::GetTime(),
                            s.last_snapshot_display_time,
                            slow_preview_ms))
                    {
                        auto frame = std::move(s.pending_snapshots.front());
                        s.pending_snapshots.pop_front();
                        if (win) win->overlays().update_simpl_snapshot_mesh(
                            frame.verts, frame.tris, 1.0f);
                    }
                }
            }

            // ---- Progress + stats ----
            if (s.runner) {
                // Poll latest stats every frame while busy.
                if (busy)
                    s.last_stats = s.runner.debug_stats();
                float pct = s.runner.progress() * 100.0f;
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                if (busy) {
                    // Big counter row (uses FontGlobalScale push/pop so it
                    // doesn't leak into other panels in this frame).
                    if (s.live_preview && s.last_stats.initial_faces > 0) {
                        ImGuiIO& io = ImGui::GetIO();
                        const float saved_scale = io.FontGlobalScale;
                        io.FontGlobalScale = 1.6f;
                        const auto& st = s.last_stats;
                        const double reduction =
                            (st.initial_edges > 0)
                            ? 100.0 * (1.0 - (double)st.current_edges /
                                              (double)st.initial_edges)
                            : 0.0;
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "%.1f%%", reduction);
                        io.FontGlobalScale = saved_scale;
                        claw_ui::same_line_if_fits_width(300.0f);
                        ImGui::TextDisabled(
                            "faces %d/%d  edges %d/%d  collapsed %d  rejected %d",
                            st.current_faces, st.initial_faces,
                            st.current_edges, st.initial_edges,
                            st.collapsed_count, st.rejected_count);
                        if (st.cost_count > 0) {
                            ImGui::TextDisabled(
                                "cost mean %.4g  recent %.4g  min %.4g  max %.4g",
                                st.cost_mean, st.cost_recent_mean,
                                st.cost_min, st.cost_max);
                        }
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Running... progress %.1f%% | collapsed %d | rejected %d",
                            pct, s.last_stats.collapsed_count,
                            s.last_stats.rejected_count);
                        if (s.last_stats.cost_count > 0) {
                            ImGui::TextDisabled(
                                "cost mean %.4g  recent %.4g",
                                s.last_stats.cost_mean,
                                s.last_stats.cost_recent_mean);
                        }
                    }
                }
            }

            // Always show the most recent run summary if we have one.
            if (s.last_stats_valid && !busy) {
                const auto& st = s.last_stats;
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Last run: %s reduction %.1f%% (faces %d -> %d)",
                    strategy_label(st.strategy_used),
                    100.0 * st.reduction_ratio,
                    st.initial_faces, st.current_faces);
                ImGui::TextDisabled(
                    "timing %.0f ms = setup %.0f + collapse %.0f + convert %.0f | "
                    "collapsed %d rejected %d stop_reached=%d cancelled=%d",
                    st.ms_total, st.ms_setup, st.ms_collapse, st.ms_convert,
                    st.collapsed_count, st.rejected_count,
                    st.stop_reached ? 1 : 0, st.cancelled ? 1 : 0);
                if (st.cost_count > 0) {
                    ImGui::TextDisabled(
                        "cost mean %.4g  recent %.4g  min %.4g  max %.4g",
                        st.cost_mean, st.cost_recent_mean,
                        st.cost_min, st.cost_max);
                }
            }

            // ---- Run / Cancel ----
            if (!busy) {
                if (ImGui::Button("Run")) {
                    SIMPL_Config cfg;
                    cfg.strategy        = s.strategy;
                    cfg.stop_mode       = s.stop_mode;
                    cfg.target_ratio    = (double)s.target_ratio;
                    cfg.target_count    = s.target_count;
                    cfg.live_preview    = s.live_preview;
                    cfg.snapshot_min_ms = std::max(20, s.snapshot_min_ms);
                    cfg.use_bounded_normal_change =
                        s.use_bounded_normal_change;
                    cfg.use_polyhedral_envelope =
                        s.use_polyhedral_envelope;
                    cfg.polyhedral_envelope_epsilon =
                        s.use_polyhedral_envelope
                        ? envelope_epsilon_from_state(mesh, s)
                        : 0.0;
                    // Auto-throttle the per-collapse event push so a big mesh
                    // doesn't flood the queue. ~1 event per K collapses,
                    // capped at 32 to keep the trail animation alive on small
                    // meshes too.
                    const int total_e = (int)mesh->n_edges();
                    const int target_events = 2000;
                    cfg.event_interval = std::max(1,
                        std::min(32, total_e / target_events));

                    s.last_stats_valid = false;
                    s.last_run_strategy = cfg.strategy;
                    s.last_run_stop_mode = cfg.stop_mode;
                    s.last_run_target_ratio = (float)cfg.target_ratio;
                    s.last_run_target_count = cfg.target_count;
                    s.last_run_live_preview = cfg.live_preview;
                    s.last_run_preview_speed = s.preview_speed;
                    s.last_run_bounded_normal_change =
                        cfg.use_bounded_normal_change;
                    s.last_run_polyhedral_envelope =
                        cfg.use_polyhedral_envelope;
                    s.last_run_envelope_epsilon =
                        cfg.polyhedral_envelope_epsilon;
                    s.last_run_source_name = ascii_only(mesh->name());
                    s.last_input_metadata_prompt =
                        build_mesh_metadata_prompt(mesh);
                    s.last_snapshot_gen_seen = -1;
                    s.pending_snapshots.clear();
                    s.last_snapshot_display_time = 0.0;
                    s.last_trail_display_time = 0.0;
                    s.final_result_ready.store(false,
                        std::memory_order_release);

                    // Live overlay setup: dim the source, prep an empty
                    // collapse trail. clear_simpl_overlay() runs on completion
                    // (see below) to restore opacity.
                    if (s.live_preview && win)
                        win->overlays().init_simpl_overlay(mesh);

                    claw3d::services::CgalSimplificationJobStart request;
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
                            claw3d::services::start_cgal_simplification_job(
                                win->algorithm_controller(), request);
                    } else {
                        s.runner.reset();
                    }
                    if (!s.runner) {
                        if (s.live_preview && win)
                            win->overlays().clear_simpl_overlay();
                        s.final_result_ready.store(true,
                            std::memory_order_release);
                        LOG(WARNING) << "Failed to start CGAL simplification job";
                    }
                }

                // AI Result Evaluation button (after a run)
                if (s.last_stats_valid) {
                    claw_ui::same_line_if_fits_button("AI Evaluate Result");
                    if (ImGui::Button("AI Evaluate Result")) {
                        send_ai_prompt(win,
                            build_result_evaluation_prompt(s, s.last_stats),
                            "Ask AI: evaluate CGAL simplification result");
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Send the last run parameters, mesh metadata, "
                            "timing, reduction and cost statistics to AI.");
                    }
                    if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                        claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                        ImGui::TextDisabled("Set API key in AI Chat");
                    }
                }
            } else {
                // Busy: only Cancel.
                if (s.runner) {
                    if (ImGui::Button("Cancel")) {
                        s.runner.cancel();
                        if (win)
                            win->algorithm_controller().request_cancel();
                    }
                }
            }

            // Worker is done and the main thread should pick up the result.
            if (busy &&
                s.runner.done_and_ready(s.final_result_ready))
            {
                const double now = ImGui::GetTime();
                const bool slow_playback_pending =
                    s.live_preview && s.preview_speed == 1 &&
                    !s.runner.is_cancelled() &&
                    !s.pending_snapshots.empty();
                const bool slow_playback_hold =
                    s.live_preview && s.preview_speed == 1 &&
                    !s.runner.is_cancelled() &&
                    claw_ui::frame_waiting(
                        now, s.last_snapshot_display_time, slow_preview_ms);
                if (slow_playback_pending || slow_playback_hold) {
                    if (win)
                        win->algorithm_controller().mark_preview_flushing();
                    claw3d::app::wake_event_loop();
                } else {
                    s.last_stats       = s.runner.debug_stats();
                    s.last_stats_valid = true;
                    // Tear down the live overlay (collapse trail + source ghost)
                    // before completion runs; completion then hides the source
                    // entirely and adds the simplified-mesh child.
                    if (win) win->overlays().clear_simpl_overlay();
                    mark_algorithm_done(win);
                    s.pending_snapshots.clear();
                    s.last_snapshot_display_time = 0.0;
                    s.last_trail_display_time = 0.0;
                    s.runner.reset();
                }
            }
#else
            ImGui::TextColored(claw_ui::status_warning_color(),
                "CGAL not available (rebuild with CLAW3D_ENABLE_CGAL=ON).");
#endif
        }  // can_run
    } DIALOG_END;
}

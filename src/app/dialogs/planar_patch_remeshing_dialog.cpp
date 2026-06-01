// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/planar_patch_remeshing_dialog.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "services/jobs/cgal/planar_patch_remeshing_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"
#include "ui/ui_frame_gate.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/state.h>
#include <easy3d/util/logging.h>

#include <GLFW/glfw3.h>
#include "imgui.h"

#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

const char* kPprPatchProperty = "f:patch_id";
const char* kPprOwnPatchProperty = "f:ppr_patch_id";
constexpr double kPprSlowFrameMs = 300.0;
constexpr double kPprSlowFinalHoldMs = 1000.0;

// PPR builds a mesh-metadata prompt with PPR-specific fields (reusable
// face label property), so we keep the local prompt builder, but route
// the ASCII filter + send-prompt through the shared claw_ai layer.
using claw_ai::ascii_only;

const char* ppr_mode_label(int mode) {
    switch (mode) {
    case PPR_ExactPlanar: return "Exact Planar";
    case PPR_AlmostPlanar: return "Almost Planar";
    case PPR_ExistingLabels: return "Existing Labels";
    default: return "Unknown";
    }
}

const char* ppr_mode_tip(int mode) {
    switch (mode) {
    case PPR_ExactPlanar:
        return "Exact Planar: use for CAD-like meshes whose faces are already exactly coplanar. Fastest and cleanest when the input is mathematically planar.";
    case PPR_AlmostPlanar:
        return "Almost Planar: use for scanned or noisy triangle meshes. It detects near-planar patches first, then remeshes them.";
    case PPR_ExistingLabels:
        return "Existing Labels: use after VSA or another segmentation. It reuses per-face labels such as f:patch_id or f:vsa_proxy_id.";
    default:
        return "Choose the mode that matches how the patch segmentation should be obtained.";
    }
}

bool read_existing_patch_labels(
    easy3d::SurfaceMesh* mesh,
    std::vector<int>& labels,
    std::string& source_property,
    std::string& error)
{
    if (!mesh) {
        error = "No mesh selected.";
        return false;
    }

    const char* candidates[] = {
        kPprPatchProperty,
        kPprOwnPatchProperty,
        "f:vsa_proxy_id",
        "f:region_id",
        "f:planar_partition",
        "f:chart"
    };

    for (const char* name : candidates) {
        auto prop = mesh->get_face_property<int>(name);
        if (!prop) continue;

        labels.clear();
        labels.reserve(mesh->n_faces());
        for (auto f : mesh->faces())
            labels.push_back(prop[f]);

        for (int id : labels) {
            if (id < 0) {
                error = std::string("Face label property contains unlabeled faces: ") + name;
                return false;
            }
        }
        if (labels.empty()) {
            error = std::string("Face label property is empty: ") + name;
            return false;
        }
        source_property = name;
        return true;
    }

    error = "No reusable face label property found. Expected f:patch_id, f:vsa_proxy_id, or f:region_id.";
    return false;
}

std::string build_mesh_metadata_prompt(easy3d::SurfaceMesh* mesh) {
    std::ostringstream oss;
    if (!mesh) {
        oss << "Source mesh metadata: unavailable\n";
        return oss.str();
    }

    claw_ai::append_surface_mesh_metadata(
        oss, claw_ai::collect_surface_mesh_ai_stats(mesh),
        true, true);

    std::vector<int> labels;
    std::string label_property, label_error;
    if (read_existing_patch_labels(mesh, labels, label_property, label_error)) {
        std::unordered_map<int, int> seen;
        for (int id : labels)
            seen[id] += 1;
        oss << "- Reusable face label property: " << label_property
            << " labels=" << seen.size() << "\n";
    } else {
        oss << "- Reusable face label property: none\n";
    }

    return ascii_only(oss.str());
}

void append_ppr_parameter_block(
    std::ostringstream& oss,
    int mode,
    float cos_threshold,
    float dist_ratio,
    bool postprocess,
    const std::string& label_property)
{
    oss << "Current Planar Patch Remeshing parameters:\n";
    oss << "- Mode: " << ppr_mode_label(mode) << "\n";
    oss << "- Cos Angle: " << cos_threshold << "\n";
    if (mode == PPR_AlmostPlanar || mode == PPR_ExistingLabels)
        oss << "- Distance Ratio: " << dist_ratio << "\n";
    if (mode == PPR_AlmostPlanar)
        oss << "- Postprocess Regions: " << (postprocess ? "yes" : "no") << "\n";
    if (mode == PPR_ExistingLabels)
        oss << "- Existing label property: "
            << (label_property.empty() ? "auto-detect" : label_property) << "\n";
    oss << "Mode selection notes:\n";
    oss << "- Exact Planar: for CAD-like meshes with exactly coplanar faces.\n";
    oss << "- Almost Planar: for noisy/scanned meshes where planar patches must be detected.\n";
    oss << "- Existing Labels: for reusing VSA/RG/PPR face labels from a previous algorithm.\n";
}

std::string build_parameter_advice_prompt(
    easy3d::SurfaceMesh* mesh,
    const PPRState& s,
    const std::string& label_property)
{
    std::ostringstream oss;
    oss << "Please give parameter advice for CGAL Planar Patch Remeshing in 3D Claw.\n\n";
    append_ppr_parameter_block(oss, s.mode, s.cos_threshold, s.dist_ratio,
        s.postprocess, label_property);
    oss << "\n" << build_mesh_metadata_prompt(mesh) << "\n";
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Which mode should I use and why?\n";
    oss << "2. Recommend Cos Angle and Distance Ratio values.\n";
    oss << "3. Should Postprocess Regions be enabled?\n";
    oss << "4. If Existing Labels is appropriate, which face property should be used?\n";
    oss << "5. What failure modes or visual artifacts should I inspect after remeshing?\n";
    return ascii_only(oss.str());
}

std::string build_result_evaluation_prompt(
    const PPRState& s,
    const PPR_DebugStats& st)
{
    std::ostringstream oss;
    oss << "Please evaluate this CGAL Planar Patch Remeshing result in 3D Claw.\n\n";
    append_ppr_parameter_block(oss, s.last_run_mode, s.last_run_cos_threshold,
        s.last_run_dist_ratio, s.last_run_postprocess,
        s.last_run_label_property);
    oss << "\n";
    if (!s.last_input_metadata_prompt.empty())
        oss << s.last_input_metadata_prompt << "\n";
    oss << "Run statistics:\n";
    oss << "- Input: vertices=" << st.input_vertices
        << " edges=" << st.input_edges
        << " faces=" << st.input_faces << "\n";
    oss << "- Output: vertices=" << st.output_vertices
        << " edges=" << st.output_edges
        << " faces=" << st.output_faces << "\n";
    oss << "- Patches: " << st.patches
        << " corners=" << st.corners
        << " constrained_edges=" << st.constrained_edges << "\n";
    oss << "- All patches remeshed: " << (st.all_patches_remeshed ? "yes" : "no") << "\n";
    oss << "- Cancelled: " << (st.cancelled ? "yes" : "no") << "\n";
    oss << "- Compression ratio: " << st.compression_ratio << "\n";
    oss << "- Runtime ms: total=" << st.ms_total
        << " region_growing=" << st.ms_region_growing
        << " corner_detection=" << st.ms_corner_detection
        << " remeshing=" << st.ms_remeshing << "\n\n";
    oss << "Keep the structure concise. " << ai_lang::directive() << "\n";
    oss << "1. Is this remeshing result likely good or suspicious?\n";
    oss << "2. Is the patch count reasonable for the input?\n";
    oss << "3. Should I adjust Cos Angle, Distance Ratio, or mode?\n";
    oss << "4. If some patches failed, what should I try next?\n";
    oss << "5. What should I visually inspect on the output mesh?\n";
    return ascii_only(oss.str());
}

} // namespace

static const char* PPR_HELP_PROMPT =
    "I am using CGAL Planar Patch Remeshing. There are three modes:\n\n"
    "1. Exact Planar: detects exactly coplanar faces and simplifies them.\n"
    "   Best for CAD meshes. One parameter: cosine_of_maximum_angle.\n\n"
    "2. Almost Planar: uses region growing to detect near-planar patches,\n"
    "   then detects corners and constrained edges, then remeshes each\n"
    "   patch. Parameters: cosine_of_maximum_angle, maximum_distance,\n"
    "   postprocess_regions.\n\n"
    "3. Existing Labels: reuses a face label property such as f:patch_id or\n"
    "   f:vsa_proxy_id from an earlier segmentation, then detects patch\n"
    "   boundaries/corners and remeshes those patches.\n\n"
    "Please explain these parameters and suggest values for my mesh.";

void renderDialogPlanarPatchRemeshing(ViewportCanvas* viewer, PPRState& s, bool& open) {
    if (!open) return;
    prepare_dialog_window(520, 520);
    if (ImGui::Begin("CGAL Planar Patch Remeshing", &open)) {
        claw_report_active_panel("CGAL Planar Patch Remeshing");
        prereq_hint_only(prereq_surface_mesh(viewer));
        render_panel_header(
            "Detect coplanar patches and remesh them with sharp edges + corners.",
            "Architectural / CAD meshes where planarity should be enforced.",
            []() {
                auto* win = MainWindow::instance();
                claw_ai::send_panel_ai_prompt(
                    win, PPR_HELP_PROMPT,
                    "Ask AI: Planar Patch Remeshing help");
            });

        auto* win = MainWindow::instance();
        auto* viewer = win ? win->viewer() : nullptr;

        easy3d::SurfaceMesh* mesh = nullptr;
        if (viewer) {
            mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (!mesh)
                for (auto& mp : viewer->models())
                    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(mp.get()))
                        { mesh = sm; break; }
        }

        if (!mesh) {
            ImGui::TextColored(claw_ui::status_error_color(), "No triangle mesh loaded.");
            if (ImGui::Button("Close")) open = false;
        } else {
            int nv = mesh->n_vertices(), nf = mesh->n_faces();
            ImGui::TextColored(claw_ui::status_muted_color(), "Input: %s (%dV %dF)", mesh->name().c_str(), nv, nf);

            bool is_tri = true;
            for (auto f : mesh->faces()) {
                int c = 0; for (auto v : mesh->vertices(f)) { (void)v; ++c; }
                if (c != 3) { is_tri = false; break; }
            }
            if (!is_tri) {
                ImGui::TextColored(claw_ui::status_error_color(), "Non-triangle mesh. Triangulate first.");
                if (ImGui::Button("Close")) open = false;
            } else {
                ImGui::Separator();
                const char* modes[] = {
                    "Exact Planar",
                    "Almost Planar",
                    "Existing Labels"
                };
                ImGui::Combo("Mode", &s.mode, modes, 3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", ppr_mode_tip(s.mode));
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", ppr_mode_tip(s.mode));
                ImGui::PopStyleColor();
                ImGui::SliderFloat("Cos Angle", &s.cos_threshold, 0.80f, 1.00f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cosine of max angle for planar grouping.");
                if (s.mode == PPR_AlmostPlanar ||
                    s.mode == PPR_ExistingLabels) {
                    ImGui::SliderFloat("Distance Ratio", &s.dist_ratio, 0.0001f, 0.05f, "%.4f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("max_distance = bbox_diag * distance_ratio. Used for corner and boundary validation.");
                }
                if (s.mode == PPR_AlmostPlanar) {
                    ImGui::Checkbox("Postprocess Regions", &s.postprocess);
                }
                if (s.mode == PPR_ExistingLabels) {
                    std::vector<int> labels;
                    std::string source_property, label_error;
                    if (read_existing_patch_labels(mesh, labels,
                            source_property, label_error)) {
                        ImGui::TextDisabled("Using labels: %s", source_property.c_str());
                    } else {
                        ImGui::TextColored(claw_ui::status_warning_color(),
                            "%s", label_error.c_str());
                    }
                }
                ImGui::Checkbox("Live Preview", &s.live_preview);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Show constrained edges, corner points, and patch-by-patch remeshing progress.");
                    if (s.live_preview) {
                        claw_ui::same_line_if_fits_width(220.0f);
                        const char* speed_items[] = {"Normal", "Slow"};
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::Combo("Speed", &s.preview_speed, speed_items, 2);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Normal: paint each snapshot as it arrives. Slow: replay queued snapshots about every 300 ms.");
                    }
                    ImGui::Spacing();

#ifdef CLAW3D_HAS_CGAL
                bool busy = win && win->algorithm_controller().is_running();
                std::string current_label_property;
                if (s.mode == PPR_ExistingLabels) {
                    std::vector<int> labels;
                    std::string label_error;
                    read_existing_patch_labels(mesh, labels,
                        current_label_property, label_error);
                }

                // Close-while-busy: ImGui set open=false from the X. Hold the
                // dialog alive, request cancel, wait for worker to finish.
                if (!open && busy && s.runner) {
                    open = true;
                    s.close_requested = true;
                    s.runner.cancel();
                    glfwPostEmptyEvent();
                }

                if (busy) {
                    ImGui::TextColored(claw_ui::status_success_color(), "Running...");
                    if (s.runner && s.runner.is_cancelled())
                        ImGui::TextDisabled("Cancel requested...");
                    if (s.close_requested)
                        ImGui::TextDisabled("Closing after PPR stops...");
                }

                // Live snapshot polling.
                if (s.runner && busy && s.live_preview && win &&
                    !s.runner.is_cancelled()) {
                    const double now = ImGui::GetTime();
                    const bool slow_waiting =
                        s.preview_speed == 1 &&
                        claw_ui::frame_waiting(
                            now, s.last_overlay_display_time,
                            kPprSlowFrameMs);
                    PPR_Snapshot snap;
                    if (!slow_waiting &&
                        s.runner.poll_snapshot(s.last_snap_gen, snap)) {
                        // Default-constructed snapshot (generation=0) arrives once
                        // before the worker publishes anything. Skip it; otherwise
                        // Slow mode would burn the playback window on an
                        // empty snap and silently drop the real phase 1/2 ones.
                        const bool has_payload =
                            !snap.face_patch_ids.empty() ||
                            !snap.corner_points.empty() ||
                            !snap.constrained_edge_endpoints.empty();
                        if (has_payload) {
                            claw_ui::mark_frame_displayed(
                                ImGui::GetTime(),
                                s.last_overlay_display_time);
                            s.last_displayed_phase = snap.phase;
                            if (!snap.face_patch_ids.empty())
                                win->update_ppr_patch_overlay(snap.face_patch_ids);
                            if (!snap.constrained_edge_endpoints.empty())
                                win->update_ppr_constraint_overlay(
                                    snap.constrained_edge_endpoints);
                            if (!snap.corner_points.empty())
                                win->update_ppr_corner_overlay(snap.corner_points);
                        }
                    }
                    glfwPostEmptyEvent();
                }

                if (s.runner && s.runner.is_done() &&
                    s.final_result_ready.load(std::memory_order_acquire) &&
                    busy) {
                    s.last_stats = s.runner.debug_stats();
                    s.last_stats_valid = true;

                    const bool close_after = s.close_requested;
                    const bool pending_live_snapshots =
                        s.live_preview &&
                        !close_after &&
                        !s.runner.cancelled_or_failed() &&
                        s.runner.has_pending_snapshot(s.last_snap_gen);
                    const bool finish_immediately =
                        !s.live_preview || close_after ||
                        s.runner.cancelled_or_failed() ||
                        s.settle_ms <= 0.0;

                    if (pending_live_snapshots) {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Playing live preview...");
                        glfwPostEmptyEvent();
                    } else if (finish_immediately) {
                        if (s.live_preview && win)
                            win->clear_ppr_overlay();
                        s.runner.copy_error_if_any(s.last_error);
                        mark_algorithm_done(win);
                        s.runner.reset();
                        s.settling = false;
                        s.close_requested = false;
                        if (close_after) open = false;
                        glfwPostEmptyEvent();
                    } else if (!s.settling) {
                        s.settling = true;
                        s.settle_started_at = ImGui::GetTime();
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Done. Holding overlay for %.1fs ...",
                            s.settle_ms / 1000.0);
                    } else {
                        const double now = ImGui::GetTime();
                        const double elapsed = (now - s.settle_started_at) * 1000.0;
                        if (elapsed >= s.settle_ms) {
                            if (win) win->clear_ppr_overlay();
                            mark_algorithm_done(win);
                            s.runner.reset();
                            s.settling = false;
                            s.close_requested = false;
                            glfwPostEmptyEvent();
                        } else {
                            ImGui::TextColored(claw_ui::status_success_color(),
                                "Done. Holding overlay %.1fs / %.1fs ...",
                                elapsed / 1000.0, s.settle_ms / 1000.0);
                        }
                    }
                }

                if (!busy) {
                    if (ImGui::Button("AI Parameter Advice")) {
                        claw_ai::send_panel_ai_prompt(win,
                            build_parameter_advice_prompt(
                                mesh, s, current_label_property),
                            "Ask AI: Planar Patch Remeshing parameter advice");
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Send current mesh metadata and PPR parameters to AI.");
                    if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                        claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                        ImGui::TextDisabled("Set API key in AI Chat");
                    }

                    if (ImGui::Button("Run")) {
                        std::vector<int> existing_labels;
                        std::string selected_label_property;
                        bool labels_ok = true;
                        if (s.mode == PPR_ExistingLabels) {
                            std::string label_error;
                            if (!read_existing_patch_labels(mesh, existing_labels,
                                    selected_label_property, label_error)) {
                                s.last_error = label_error;
                                s.last_stats_valid = false;
                                labels_ok = false;
                            }
                        }

                        if (labels_ok) {
                            PPR_Config cfg;
                            cfg.mode = s.mode;
                            cfg.cosine_threshold = s.cos_threshold;
                            cfg.distance_ratio = s.dist_ratio;
                            cfg.postprocess_regions =
                                (s.mode == PPR_AlmostPlanar) && s.postprocess;
                            cfg.live_preview = s.live_preview;
                            cfg.preview_speed = s.preview_speed;
                            cfg.snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;

                            s.last_stats_valid = false;
                            s.last_error.clear();
                            s.last_run_mode = s.mode;
                            s.last_run_cos_threshold = s.cos_threshold;
                            s.last_run_dist_ratio = s.dist_ratio;
                            s.last_run_postprocess = s.postprocess;
                            s.last_run_label_property = selected_label_property;
                            s.last_input_metadata_prompt = build_mesh_metadata_prompt(mesh);
                            s.last_snap_gen = -1;
                            s.last_displayed_phase = -1;
                            s.last_overlay_display_time = 0.0;
                            s.settling = false;
                            s.settle_started_at = 0.0;
                            s.final_result_ready.store(false,
                                std::memory_order_release);
                            s.settle_ms =
                                (s.live_preview && s.preview_speed == 1)
                                    ? kPprSlowFinalHoldMs
                                    : 0.0;
                            s.close_requested = false;

                            if (s.live_preview && win)
                                win->init_ppr_overlay(mesh);

                            claw3d::services::PlanarPatchRemeshingJobStart request;
                            request.source_mesh = mesh;
                            request.source_handle =
                                (win && win->viewer())
                                    ? win->viewer()->model_handle(mesh)
                                    : ModelHandle{};
                            request.config = cfg;
                            request.existing_face_patch_ids = existing_labels;
                            request.source_name = mesh->name();
                            request.final_result_ready =
                                &s.final_result_ready;
                            request.wake_ui = []() { glfwPostEmptyEvent(); };

                            if (win) {
                                s.runner = claw3d::services::
                                    start_planar_patch_remeshing_job(
                                        win->algorithm_controller(), request);
                            } else {
                                s.runner.reset();
                            }
                            if (!s.runner) {
                                if (s.live_preview && win)
                                    win->clear_ppr_overlay();
                                s.final_result_ready.store(true,
                                    std::memory_order_release);
                                s.last_error =
                                    "Failed to start Planar Patch Remeshing job.";
                                LOG(WARNING) << s.last_error;
                            }
                        }
                    }

                    if (s.last_stats_valid) {
                        claw_ui::same_line_if_fits_text("Done: 000000V 000000F 000000 patches 0000.0ms");
                        ImGui::TextColored(claw_ui::status_success_color(), "Done: %dV %dF %d patches %.1fms",
                            s.last_stats.output_vertices, s.last_stats.output_faces,
                            s.last_stats.patches, s.last_stats.ms_total);
                        if (ImGui::Button("AI Evaluate Result")) {
                            claw_ai::send_panel_ai_prompt(win,
                                build_result_evaluation_prompt(s, s.last_stats),
                                "Ask AI: evaluate Planar Patch Remeshing result");
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Ask AI to evaluate the completed PPR result.");
                        if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                            claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                            ImGui::TextDisabled("Set API key in AI Chat");
                        }
                    }
                    if (!s.last_error.empty()) {
                        ImGui::TextColored(claw_ui::status_error_color(),
                            "%s", s.last_error.c_str());
                    }
                } else {
                    if (ImGui::Button("Cancel")) {
                        if (s.runner) {
                            s.runner.cancel();
                            if (win)
                                win->clear_ppr_overlay();
                            s.settling = false;
                            s.close_requested = false;
                            s.last_overlay_display_time = 0.0;
                            if (s.runner.is_done()) {
                                s.runner.reset();
                                mark_algorithm_done(win);
                                s.last_stats_valid = false;
                            }
                            glfwPostEmptyEvent();
                        }
                    }
                }
#else
                ImGui::TextColored(claw_ui::status_warning_color(), "CGAL not available.");
#endif
            }
        }
    }
    ImGui::End();
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/region_growing_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/primitive_preview_policy.h"
#include "services/jobs/cgal/region_growing_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>
#include <GLFW/glfw3.h>

// =============================================================================
// Region Growing help prompt
// =============================================================================

static const char* REGION_GROWING_HELP_PROMPT =
    "I'm using CGAL 6.1.1's Region Growing algorithm for plane detection on "
    "a point cloud. Please help me understand the parameters:\n\n"
    "1. K Neighbors - number of nearest neighbors for the neighbor query. "
    "Higher values (16-24) give better connectivity but slower performance. "
    "Default 12.\n\n"
    "2. Max Distance - maximum distance from a point to the fitted plane. "
    "Smaller = tighter planes. Auto = bbox diagonal * 0.005.\n\n"
    "3. Max Angle - maximum angle (degrees) between a point's normal and the "
    "plane normal. Lower values enforce normal consistency. Default 25 deg.\n\n"
    "4. Min Region Size - minimum number of points to accept a region. "
    "Higher values reject small noisy regions. Auto = max(30, 0.5% of points).\n\n"
    "Algorithm overview: Region growing starts from a seed point, expands to "
    "neighbors that satisfy the plane compatibility test, refits the plane, "
    "and repeats until no more compatible neighbors are found. Unlike RANSAC "
    "which randomly samples global candidates, region growing expands locally "
    "from seeds. It produces connected, contiguous regions but is sensitive to "
    "normal quality and connectivity.\n\n"
    "Please analyze my results and suggest parameter improvements.";

static const char* REGION_GROWING_HELP_PROMPT_ASCII =
    "I'm using CGAL 6.1.1 Region Growing for plane detection on a point cloud. "
    "Please explain the key parameters and suggest practical tuning rules.\n\n"
    "1. K Neighbors - number of nearest neighbors for the neighbor query. "
    "Higher values improve scan-line connectivity but slow down detection. "
    "Current default: 20.\n\n"
    "2. Max Distance - maximum point-to-plane distance. Smaller values produce "
    "tighter planes. Auto default: bbox diagonal * 0.015.\n\n"
    "3. Max Angle - maximum angle in degrees between point normals and the "
    "region plane normal. Lower values enforce normal consistency. "
    "Current default: 30 degrees.\n\n"
    "4. Min Region Size - minimum number of points to accept a region. "
    "Higher values reject small noisy regions. Auto default: max(30, 0.01% of points).\n\n"
    "Algorithm overview: Region growing starts from a seed point, expands to "
    "neighbors that satisfy plane compatibility, refits the plane, and repeats "
    "until no more compatible neighbors are found. Compared with RANSAC, it "
    "is local and contiguous, but more sensitive to normals and connectivity.\n\n"
    "Please analyze my results and suggest parameter improvements.";

static std::string build_parameter_advice_prompt(easy3d::PointCloud* cloud,
                                                 const RegionGrowingState& s) {
    std::ostringstream prompt;
    prompt << "Please give Region Growing parameter advice for the current "
              "point cloud.\n\n";
    if (cloud) {
        prompt << "| Field | Value |\n|---|---|\n";
        prompt << "| Model | " << cloud->name() << " |\n";
        prompt << "| Points | " << cloud->n_vertices() << " |\n";
        const auto& bbox = cloud->bounding_box();
        if (bbox.is_valid())
            prompt << "| BBox diagonal | " << bbox.diagonal_length() << " |\n";
        prompt << "| Has normals | "
               << (cloud->get_vertex_property<easy3d::vec3>("v:normal")
                       ? "yes" : "no")
               << " |\n";
    }
    prompt << "| Parameter | Current value |\n|---|---:|\n";
    prompt << "| K Neighbors | " << s.k_neighbors << " |\n";
    prompt << "| Max Distance | " << s.max_distance << " |\n";
    prompt << "| Max Angle Deg | " << s.max_angle_deg << " |\n";
    prompt << "| Min Region Size | " << s.min_region_size << " |\n";
    prompt << "| Live Preview | " << (s.live_preview ? "on" : "off") << " |\n\n";
    prompt << REGION_GROWING_HELP_PROMPT_ASCII << "\n"
           << ai_lang::directive();
    return prompt.str();
}

// =============================================================================
// Region Growing Dialog
// =============================================================================

void renderDialogRegionGrowing(ViewportCanvas* viewer, RegionGrowingState& s, bool& open) {
    prepare_dialog_window(520, 560);
    DIALOG_BODY("Region Growing", open) {
        prereq_hint_only(prereq_point_cloud(viewer));
        // "?" AI help button
        claw_ui::same_line_right_if_fits_button("?");
        if (ImGui::SmallButton("?")) {
            auto* win = MainWindow::instance();
            if (win && win->ai_chat() && win->ai_chat()->HasApiKey()) {
                win->show_ai_chat();
                win->ai_chat()->SendUserMessage(REGION_GROWING_HELP_PROMPT_ASCII,
                    "Ask AI: Region Growing help");
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ask AI about Region Growing parameters");

        ImGui::Spacing();
        auto* win = MainWindow::instance();
        auto* viewer = win ? win->viewer() : nullptr;
        const bool rg_busy =
            s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::RegionGrowing);
        if (!open && rg_busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            s.current_region_pending_idx.clear();
            s.pending_cmds.clear();
            if (win)
                win->clear_rg_overlays();
            glfwPostEmptyEvent();
        }

        easy3d::PointCloud* cloud = nullptr;
        if (viewer) {
            cloud = dynamic_cast<easy3d::PointCloud*>(viewer->current_model());
            if (!cloud) {
                for (auto& mp : viewer->models()) {
                    if (auto* pc = dynamic_cast<easy3d::PointCloud*>(mp.get())) {
                        cloud = pc;
                        break;
                    }
                }
            }
        }

        bool can_run = false;
        if (!cloud) {
            ImGui::TextColored(claw_ui::status_error_color(),
                "No point cloud loaded. Load a point cloud first.");
            if (ImGui::Button("Close")) open = false;
        } else {
            auto normals = cloud->get_vertex_property<easy3d::vec3>("v:normal");
            bool has_normals = normals && cloud->n_vertices() > 0;
            int n_pts = cloud->n_vertices();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (%d pts)", cloud->name().c_str(), n_pts);
            if (!has_normals) {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "Region Growing requires point normals.\n"
                    "Run Point Cloud > Normal Estimation first.");
                if (ImGui::Button("Close")) open = false;
            } else {
                ImGui::TextColored(claw_ui::status_success_color(), "Normals: OK");
                can_run = true;
            }
        }

        if (can_run) {
        int n_pts = cloud->n_vertices();
        bool busy = win && win->algorithm_controller().is_running();
        const bool this_busy =
            s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::RegionGrowing);

        // Fill auto defaults from cloud on first frame (state sentinel = -1).
        {
            const auto& bb = cloud->bounding_box();
            float bbox_diag = bb.is_valid() ? bb.diagonal_length() : 1.0f;
            if (s.max_distance < 0)
                s.max_distance = bbox_diag * claw3d::primitive_preview_policy::kRegionGrowingPlanePatchBboxRatio;
            if (s.min_region_size < 0)
                s.min_region_size = std::max(30, (int)(n_pts * 0.0001));
        }

        ImGui::SliderInt("K Neighbors", &s.k_neighbors, 6, 64);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Number of nearest neighbors for local connectivity.");

        ImGui::InputFloat("Max Distance", &s.max_distance, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max point-to-plane distance. Default = bbox diag * 0.015.");

        ImGui::InputFloat("Max Angle (deg)", &s.max_angle_deg, 1.0f, 5.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max angle between point normal and plane normal.");

        ImGui::InputInt("Min Region Size", &s.min_region_size);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum points per region. Default = max(30, 0.01%% of points).");

        ImGui::Spacing();
        ImGui::Checkbox("Live Preview", &s.live_preview);
        claw_ui::same_line_if_fits_text("(?)");
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show accepted regions as they are fitted.");
        if (s.live_preview)
            s.live_throttle = false;

        if (!busy) {
            if (ImGui::Button("AI Parameter Advice")) {
                if (win && win->ai_chat() && win->ai_chat()->HasApiKey()) {
                    win->send_ai_request(
                        build_parameter_advice_prompt(cloud, s),
                        AICtx_CurrentModel | AICtx_ActivePanel,
                        std::string(),
                        "Ask AI: Region Growing parameter advice");
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ask AI for Region Growing parameters appropriate for this cloud.");
            if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                ImGui::TextDisabled("Set API key in AI Chat");
            }
        }

#ifdef CLAW3D_HAS_CGAL
        // Poll progress directly from runner (works even without live preview)
        if (s.runner && this_busy) {
            s.running_regions = s.runner.num_regions();
            s.running_visited = (int)(s.runner.progress() * n_pts);
        }

        // --- Progress display ---
        if (s.runner && s.runner.is_paused()) {
            float pct = std::max(0.0f, std::min(100.0f, s.runner.progress() * 100.0f));
            ImGui::TextColored(claw_ui::status_warning_color(),
                "Paused | Progress: %.1f%% | Fitted planes: %d",
                pct, s.running_regions);
        } else if (this_busy) {
            float pct = s.runner
                ? std::max(0.0f, std::min(100.0f, s.runner.progress() * 100.0f))
                : 0.0f;
            ImGui::TextColored(claw_ui::status_success_color(),
                "Running... | Progress: %.1f%% | Fitted planes: %d",
                pct, s.running_regions);
        }
        // Pick the freshest stats: live (while runner exists) or snapshot
        // (kept after runner.reset()) so the user can still inspect the
        // timing breakdown after the algorithm completes.
        const RG_DebugStats* dbg_ptr = nullptr;
        RG_DebugStats live_stats;
        if (s.runner && this_busy) {
            live_stats = s.runner.debug_stats();
            dbg_ptr = &live_stats;
        } else if (s.last_stats_valid) {
            dbg_ptr = &s.last_stats;
        }
        if (dbg_ptr) {
            const RG_DebugStats& dbg = *dbg_ptr;
            ImGui::TextDisabled(
                "RG debug | seed %d | attempts %d/%d | scan %d | covered %d | current %d pts | rejected %d",
                dbg.current_seed,
                dbg.seed_attempts,
                dbg.total_points,
                dbg.scan_progress,
                dbg.visited_points,
                dbg.current_region_size,
                dbg.rejected_regions);
            ImGui::TextDisabled(
                "frontier %d | n+ %d | n- %d | refit %d",
                dbg.frontier_items,
                dbg.neighbor_accepted,
                dbg.neighbor_rejected,
                dbg.primitive_refits);
            // Timing breakdown - answers "where did the time go".
            // ms_total covers detect(); the rest sums to ~ms_total but rounded.
            // "rejected" is the tail-noise budget. If it dominates ms_total
            if (dbg.ms_total > 0.0) {
                ImGui::TextDisabled(
                    "timing | total %.0f ms | setup %.0f | accepted %.0f | rejected %.0f",
                    dbg.ms_total,
                    dbg.ms_setup,
                    dbg.ms_accepted_regions,
                    dbg.ms_rejected_seeds);
            }
        }

        // --- Live event drain ---
        // Live overlay: animate final accepted regions. CGAL also has
        // tentative candidate-region events, but those can be rejected later
        // and would flicker if painted as final results.
        std::vector<RGColorCmd> live_cmds;
        live_cmds.swap(s.pending_cmds);

        if (s.runner) {
            std::vector<RG_FrameEvent> events;
            if (s.runner.drain_live_events(events)) {
                for (const auto& ev : events) {
                    switch (ev.type) {
                    case RG_FrameEvent::SeedSelected:
                        s.current_region_pending_idx.clear();
                        break;
                    case RG_FrameEvent::NeighborAccepted:
                        break;
                    case RG_FrameEvent::RegionAccepted: {
                        s.running_regions = ev.accepted_regions;
                        s.running_visited = ev.visited_points;
                        if (ev.region_id >= 0 && s.runner) {
                            RG_RegionResult res;
                            s.runner.get_region(ev.region_id, res);
                            auto cloud_pts = cloud->get_vertex_property<easy3d::vec3>("v:point");
                            if (cloud_pts && ev.seed_index >= 0 && ev.seed_index < n_pts) {
                                auto seed = cloud_pts[typename easy3d::PointCloud::Vertex(ev.seed_index)];
                                auto dist2 = [&](int idx) -> float {
                                    if (idx < 0 || idx >= n_pts)
                                        return std::numeric_limits<float>::max();
                                    auto p = cloud_pts[typename easy3d::PointCloud::Vertex(idx)];
                                    float dx = p.x - seed.x;
                                    float dy = p.y - seed.y;
                                    float dz = p.z - seed.z;
                                    return dx * dx + dy * dy + dz * dz;
                                };
                                std::stable_sort(res.indices.begin(), res.indices.end(),
                                    [&](int a, int b) { return dist2(a) < dist2(b); });
                            }
                            live_cmds.reserve(live_cmds.size() + res.indices.size());
                            for (int idx : res.indices)
                                live_cmds.push_back({idx, ev.region_id});
                        }
                        s.current_region_pending_idx.clear();
                        break;
                    }
                    case RG_FrameEvent::RegionRejected:
                        s.current_region_pending_idx.clear();
                        break;
                    case RG_FrameEvent::Progress:
                        s.running_visited = ev.visited_points;
                        s.running_regions = std::max(s.running_regions, ev.accepted_regions);
                        break;
                    }
                }
            }

            // Backfill bursts can be large, so drain them over a few frames.
            // Keeping the runner alive until pending_cmds is empty guarantees
            // the source cloud reaches the accepted region's final color.
            static constexpr int RG_PER_FRAME = 4000;
            if ((int)live_cmds.size() > RG_PER_FRAME) {
                s.pending_cmds.assign(
                    live_cmds.begin() + RG_PER_FRAME, live_cmds.end());
                live_cmds.resize(RG_PER_FRAME);
            }
            if (win && !live_cmds.empty())
                win->update_rg_overlay(live_cmds);

            if (s.runner.is_done() && this_busy
                && !s.final_results_ready.load(std::memory_order_acquire)) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Finalizing result meshes...");
            }

            // Once runner is done, all live color commands are flushed, and
            // the worker has prepared result meshes, hand completion to the
            // main_window handler. Heavy CH/AS construction is intentionally
            // not done here; this function runs inside the ImGui frame.
            if (s.runner.is_done() && this_busy && s.pending_cmds.empty()
                && s.final_results_ready.load(std::memory_order_acquire)) {
                s.num_regions = s.runner.num_regions();
                s.last_stats = s.runner.debug_stats();
                s.last_stats_valid = true;
                mark_algorithm_done(win);
                s.runner.reset();
                if (s.close_requested) {
                    s.close_requested = false;
                    open = false;
                }
            }

        }

        // --- Buttons ---
        if (!busy) {
            if (ImGui::Button("Detect Regions")) {
                if (win) win->clear_rg_overlays();
                int n_pts2 = cloud->n_vertices();

                // Auto defaults below should match the sentinel-fill block
                // above (the one that runs when s.max_distance / s.min_region_size
                // are still <0). Keeping the formula identical in both spots
                // avoids a maintenance trap if the default ever needs tuning.
                const auto& bb = cloud->bounding_box();
                float bbox_diag = bb.is_valid() ? bb.diagonal_length() : 1.0f;
                double max_dist = s.max_distance > 0 ? (double)s.max_distance
                    : (double)(bbox_diag * claw3d::primitive_preview_policy::kRegionGrowingPlanePatchBboxRatio);
                int min_sz = s.min_region_size > 0 ? s.min_region_size
                    : std::max(30, (int)(n_pts2 * 0.0001));

                RG_Config cfg;
                cfg.k_neighbors = s.k_neighbors;
                cfg.max_distance = max_dist;
                cfg.max_angle_deg = (double)s.max_angle_deg;
                cfg.min_region_size = min_sz;
                cfg.live_preview = s.live_preview;
                cfg.live_throttle = false;
                cfg.live_show_tentative = false;
                s.live_throttle = false;

                s.live_regions_added = 0;
                s.running_regions = 0;
                s.running_visited = 0;
                s.num_regions = 0;
                s.current_region_pending_idx.clear();
                s.pending_cmds.clear();
                s.last_stats_valid = false;
                s.close_requested = false;
                s.final_results_ready.store(false, std::memory_order_release);

                if (s.live_preview && win) win->init_rg_overlay(cloud);

                claw3d::services::RegionGrowingJobStart request;
                request.source_cloud = cloud;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(cloud)
                        : ModelHandle{};
                request.config = cfg;
                request.final_result_ready = &s.final_results_ready;
                request.wake_ui = []() { glfwPostEmptyEvent(); };

                if (win) {
                    s.runner = claw3d::services::start_region_growing_job(
                        win->algorithm_controller(), request);
                } else {
                    s.runner.reset();
                }
                if (!s.runner) {
                    if (s.live_preview && win)
                        win->clear_rg_overlays();
                    s.final_results_ready.store(true,
                        std::memory_order_release);
                    LOG(WARNING) << "Failed to start Region Growing job";
                }
            }

            if (s.num_regions > 0) {
                claw_ui::same_line_if_fits_text("Done: 000000 regions");
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Done: %d regions", s.num_regions);
            }

            // AI result evaluation button (after completion).
            if (win && win->algorithm_controller().has_quality_context_for(
                    AlgorithmId::RegionGrowing)) {
                ImGui::Spacing();
                const bool has_key = win->ai_chat() && win->ai_chat()->HasApiKey();
                if (!has_key)
                    ImGui::BeginDisabled();
                if (ImGui::Button("AI Evaluate Result")) {
                    const std::string quality_context =
                        win->algorithm_controller().take_quality_context();
                    win->send_ai_request(
                        std::string("Analyze the Region Growing result above. "
                                    "Are the detected regions reasonable? "
                                    "Any parameter suggestions? ") +
                            ai_lang::directive(),
                        AICtx_CurrentModel | AICtx_ActivePanel,
                        quality_context,
                        "Ask AI: evaluate Region Growing result");
                }
                if (!has_key) {
                    ImGui::EndDisabled();
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
                claw_ui::same_line_if_fits_text("(?)");
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Send the region growing report to AI for analysis.");
            }
        } else {
            // Busy - show pause/resume/step/cancel
            if (s.runner) {
                if (s.close_requested)
                    ImGui::TextDisabled("Closing after Region Growing stops...");
                if (s.runner.is_paused()) {
                    if (ImGui::Button("Resume")) s.runner.resume();
                    claw_ui::same_line_if_fits_button("Step");
                    if (ImGui::Button("Step")) s.runner.step();
                } else {
                    if (ImGui::Button("Pause")) s.runner.pause();
                }
                claw_ui::same_line_if_fits_button("Cancel");
                if (ImGui::Button("Cancel")) {
                    s.runner.cancel();
                    s.current_region_pending_idx.clear();
                    s.pending_cmds.clear();
                    if (win) win->clear_rg_overlays();
                }
            }
        }
#else
        ImGui::TextColored(claw_ui::status_warning_color(),
            "CGAL not available (rebuild with CLAW3D_ENABLE_CGAL=ON).");
#endif

        } // end if (can_run)
    } DIALOG_END;
}

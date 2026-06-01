// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_VSA_DIALOG_H
#define CLAW3D_VSA_DIALOG_H

/// State and render entry point for variational shape approximation controls.

#include <atomic>
#include <string>
#include <vector>

#include "services/jobs/cgal/vsa_approximation_job.h"

class ViewportCanvas;

struct VSAState {
    VSAState() = default;
    VSAState(const VSAState& other) { *this = other; }
    VSAState& operator=(const VSAState& other) {
        metric           = other.metric;
        seeding          = other.seeding;
        target_proxies   = other.target_proxies;
        iterations       = other.iterations;
        relaxations      = other.relaxations;
        extract_mesh     = other.extract_mesh;
        keep_segmentation = other.keep_segmentation;
        seed             = other.seed;
        runner           = other.runner;
        last_stats       = other.last_stats;
        last_stats_valid = other.last_stats_valid;
        initialized_for_faces = other.initialized_for_faces;
        live_preview     = other.live_preview;
        preview_speed    = other.preview_speed;
        last_snap_gen    = other.last_snap_gen;
        live_snapshot_valid = other.live_snapshot_valid;
        live_snapshot_phase = other.live_snapshot_phase;
        live_snapshot_iteration = other.live_snapshot_iteration;
        live_snapshot_proxies = other.live_snapshot_proxies;
        live_snapshot_target = other.live_snapshot_target;
        live_snapshot_error = other.live_snapshot_error;
        last_overlay_display_time = other.last_overlay_display_time;
        settling         = other.settling;
        settle_started_at = other.settle_started_at;
        settle_ms        = other.settle_ms;
        seeds_published  = other.seeds_published;
        staged_growth    = other.staged_growth;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        last_run_metric           = other.last_run_metric;
        last_run_seeding          = other.last_run_seeding;
        last_run_target_proxies   = other.last_run_target_proxies;
        last_run_iterations       = other.last_run_iterations;
        last_run_relaxations      = other.last_run_relaxations;
        last_run_extract_mesh     = other.last_run_extract_mesh;
        last_run_seed             = other.last_run_seed;
        close_requested  = other.close_requested;
        last_error       = other.last_error;
        last_proxy_count = other.last_proxy_count;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    int  metric            = VSA_METRIC_L21;
    int  seeding           = VSA_SEED_Hierarchical;
    int  target_proxies    = 100;
    int  iterations        = 30;
    int  relaxations       = 5;
    bool extract_mesh      = true;
    bool keep_segmentation = false;
    int  seed              = 1;

    // Live preview
    bool live_preview      = false;
    int  preview_speed     = 0;       // 0 = Normal, 1 = Slow (~240ms gate)
    int  last_snap_gen     = -1;
    bool live_snapshot_valid = false;
    int  live_snapshot_phase = 0;
    int  live_snapshot_iteration = 0;
    int  live_snapshot_proxies = 0;
    int  live_snapshot_target = 0;
    double live_snapshot_error = 0.0;
    double last_overlay_display_time = 0.0;
    bool   settling         = false;
    double settle_started_at = 0.0;
    double settle_ms         = 1800.0;
    bool   seeds_published   = false;
    // Expressive options
    bool   staged_growth     = false;  // live-only playback variant

    claw3d::services::VsaApproximationJobHandle runner;
    VSA_DebugStats last_stats{};
    bool   last_stats_valid = false;
    int    last_proxy_count = 0;
    // Sentinel: dialog has not auto-sized target_proxies for any mesh yet.
    int    initialized_for_faces = -1;

    std::string last_input_metadata_prompt;
    int    last_run_metric         = VSA_METRIC_L21;
    int    last_run_seeding        = VSA_SEED_Hierarchical;
    int    last_run_target_proxies = 100;
    int    last_run_iterations     = 30;
    int    last_run_relaxations    = 5;
    bool   last_run_extract_mesh   = true;
    int    last_run_seed           = 1;
    bool   close_requested = false;
    std::string last_error;

    std::atomic<bool> final_result_ready{true};
};

void renderDialogVSA(ViewportCanvas* viewer, VSAState& s, bool& open);

#endif  // CLAW3D_VSA_DIALOG_H

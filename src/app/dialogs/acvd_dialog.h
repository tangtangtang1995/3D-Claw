// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ACVD_DIALOG_H
#define CLAW3D_ACVD_DIALOG_H

/// State and render entry point for ACVD remeshing controls.

#include <atomic>
#include <string>

#include "services/jobs/cgal/acvd_remeshing_job.h"

class ViewportCanvas;

struct ACVDState {
    ACVDState() = default;
    ACVDState(const ACVDState& other) { *this = other; }
    ACVDState& operator=(const ACVDState& other) {
        mode            = other.mode;
        target_vertices = other.target_vertices;
        gradation       = other.gradation;
        ratio           = other.ratio;
        seed            = other.seed;
        live_preview    = other.live_preview;
        preview_speed   = other.preview_speed;
        runner          = other.runner;
        last_stats      = other.last_stats;
        last_stats_valid = other.last_stats_valid;
        last_snap_gen   = other.last_snap_gen;
        last_cluster_display_time = other.last_cluster_display_time;
        last_seed_display_time    = other.last_seed_display_time;
        seed_positions  = other.seed_positions;
        settling           = other.settling;
        settle_started_at  = other.settle_started_at;
        settle_ms          = other.settle_ms;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        last_run_mode = other.last_run_mode;
        last_run_target_vertices = other.last_run_target_vertices;
        last_run_gradation = other.last_run_gradation;
        last_run_ratio = other.last_run_ratio;
        last_run_seed = other.last_run_seed;
        last_run_live_preview = other.last_run_live_preview;
        last_run_preview_speed = other.last_run_preview_speed;
        close_requested = other.close_requested;
        last_error = other.last_error;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    int    mode            = ACVD_MODE_Uniform;
    int    target_vertices = 1000;
    float  gradation       = 0.8f;
    float  ratio           = 0.1f;
    int    seed            = 1;
    bool   live_preview    = false;

    // 0 = Normal (apply every snapshot immediately).
    // 1 = Slow (apply at most 1 snapshot every 240ms).
    int    preview_speed   = 0;

    claw3d::services::AcvdRemeshingJobHandle runner;
    ACVD_DebugStats last_stats{};
    bool   last_stats_valid = false;
    int    last_snap_gen    = -1;
    double last_cluster_display_time = 0.0;
    double last_seed_display_time    = 0.0;
    // Seed positions collected during live run for seed overlay.
    std::vector<ACVD_Point3d> seed_positions;
    // "Settle" phase: when the worker reports is_done(), we don't tear the
    // cluster/seed overlays down immediately. Small meshes (fandisk 800v)
    // finish in ~90 ms; clearing right away meant the cells flashed by
    // before the user could see them. We hold the final colored mesh
    // visible for `settle_ms`, then run the completion handoff.
    bool   settling = false;
    double settle_started_at = 0.0;
    double settle_ms          = 1800.0;  // user-perceivable but not annoying

    std::string last_input_metadata_prompt;
    int    last_run_mode = ACVD_MODE_Uniform;
    int    last_run_target_vertices = 1000;
    float  last_run_gradation = 0.8f;
    float  last_run_ratio = 0.1f;
    int    last_run_seed = 1;
    bool   last_run_live_preview = false;
    int    last_run_preview_speed = 0;
    bool   close_requested = false;
    std::string last_error;

    std::atomic<bool> final_result_ready{true};
};

void renderDialogACVD(ViewportCanvas* viewer, ACVDState& s, bool& open);

#endif  // CLAW3D_ACVD_DIALOG_H

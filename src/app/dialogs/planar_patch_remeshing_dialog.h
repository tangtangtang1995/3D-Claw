// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PLANAR_PATCH_REMESHING_DIALOG_H
#define CLAW3D_PLANAR_PATCH_REMESHING_DIALOG_H

/// State and render entry point for planar patch remeshing controls.

#include <atomic>
#include <string>

#include "services/jobs/cgal/planar_patch_remeshing_job.h"

class ViewportCanvas;

struct PPRState {
    PPRState() = default;
    PPRState(const PPRState& o) { *this = o; }
    PPRState& operator=(const PPRState& o) {
        mode = o.mode; cos_threshold = o.cos_threshold;
        dist_ratio = o.dist_ratio; postprocess = o.postprocess;
        runner = o.runner; running = o.running;
        last_stats = o.last_stats; last_stats_valid = o.last_stats_valid;
        last_error = o.last_error;
        last_run_mode = o.last_run_mode;
        last_run_cos_threshold = o.last_run_cos_threshold;
        last_run_dist_ratio = o.last_run_dist_ratio;
        last_run_postprocess = o.last_run_postprocess;
        last_run_label_property = o.last_run_label_property;
        last_input_metadata_prompt = o.last_input_metadata_prompt;
        live_preview = o.live_preview; preview_speed = o.preview_speed;
        last_snap_gen = o.last_snap_gen;
        last_displayed_phase = o.last_displayed_phase;
        last_overlay_display_time = o.last_overlay_display_time;
        settling = o.settling; settle_started_at = o.settle_started_at;
        settle_ms = o.settle_ms; close_requested = o.close_requested;
        final_result_ready.store(
            o.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    int    mode = PPR_AlmostPlanar;
    float  cos_threshold = 0.98f;
    float  dist_ratio = 0.002f;
    bool   postprocess = true;

    claw3d::services::PlanarPatchRemeshingJobHandle runner;
    bool   running = false;
    PPR_DebugStats last_stats{};
    bool   last_stats_valid = false;
    std::string last_error;
    int    last_run_mode = PPR_AlmostPlanar;
    float  last_run_cos_threshold = 0.98f;
    float  last_run_dist_ratio = 0.002f;
    bool   last_run_postprocess = true;
    std::string last_run_label_property;
    std::string last_input_metadata_prompt;

    // Live preview
    bool   live_preview = false;
    int    preview_speed = 0;            // 0 = Normal, 1 = Slow playback
    int    last_snap_gen = -1;
    int    last_displayed_phase = -1;
    double last_overlay_display_time = 0.0;
    bool   settling = false;
    double settle_started_at = 0.0;
    double settle_ms = 0.0;
    bool   close_requested = false;
    std::atomic<bool> final_result_ready{true};
};

void renderDialogPlanarPatchRemeshing(ViewportCanvas* viewer, PPRState& s, bool& open);

#endif // CLAW3D_PLANAR_PATCH_REMESHING_DIALOG_H

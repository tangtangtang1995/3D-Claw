// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_CGAL_SMOOTHING_DIALOG_H
#define CLAW3D_CGAL_SMOOTHING_DIALOG_H

/// State and render entry point for CGAL smoothing controls.

#include <atomic>
#include <string>
#include <vector>

#include "services/jobs/cgal/cgal_smoothing_job.h"

class ViewportCanvas;

struct SmoothingState {
    SmoothingState() = default;
    SmoothingState(const SmoothingState& other) { *this = other; }
    SmoothingState& operator=(const SmoothingState& other) {
        mode                  = other.mode;
        iterations            = other.iterations;
        time_step             = other.time_step;
        preserve_boundary     = other.preserve_boundary;
        preserve_sharp_edges  = other.preserve_sharp_edges;
        sharp_angle_degrees   = other.sharp_angle_degrees;
        relax_constraints     = other.relax_constraints;
        safety_constraints    = other.safety_constraints;
        project_to_original   = other.project_to_original;
        rescale_after_smoothing = other.rescale_after_smoothing;
        runner                = other.runner;
        last_stats            = other.last_stats;
        last_stats_valid      = other.last_stats_valid;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        last_run_mode         = other.last_run_mode;
        last_run_iterations   = other.last_run_iterations;
        last_run_time_step    = other.last_run_time_step;
        last_run_preserve_boundary  = other.last_run_preserve_boundary;
        last_run_preserve_sharp     = other.last_run_preserve_sharp;
        last_run_sharp_angle        = other.last_run_sharp_angle;
        live_preview          = other.live_preview;
        preview_speed         = other.preview_speed;
        last_snap_gen         = other.last_snap_gen;
        last_overlay_display_time = other.last_overlay_display_time;
        settling              = other.settling;
        settle_started_at     = other.settle_started_at;
        settle_ms             = other.settle_ms;
        last_snap_iter        = other.last_snap_iter;
        last_snap_total       = other.last_snap_total;
        last_snap_mean_disp   = other.last_snap_mean_disp;
        last_snap_max_disp    = other.last_snap_max_disp;
        close_requested       = other.close_requested;
        last_error            = other.last_error;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    // Primary parameters.
    int   mode                 = SMOOTH_MODE_TangentialRelaxation;
    int   iterations           = 10;
    float time_step            = 0.0001f;
    bool  preserve_boundary    = true;
    bool  preserve_sharp_edges = true;
    float sharp_angle_degrees  = 60.0f;
    // Mode-specific advanced flags.
    bool  relax_constraints      = false;  // Tangential
    bool  safety_constraints     = true;   // Angle
    bool  project_to_original    = true;   // Angle
    bool  rescale_after_smoothing = true;  // MCF (do_scale)

    // Live preview.
    bool   live_preview      = false;
    int    preview_speed     = 0;            // 0 = Normal, 1 = Slow (240ms gate)
    int    last_snap_gen     = -1;
    double last_overlay_display_time = 0.0;
    bool   settling          = false;
    double settle_started_at = 0.0;
    double settle_ms         = 1500.0;
    // Layout-stable progress cache. The busy block reads these every
    // frame so the "Iter N/M | mean_disp ..." line is always present and
    // the Cancel button below it does not jitter between frames.
    int    last_snap_iter      = 0;
    int    last_snap_total     = 0;
    double last_snap_mean_disp = 0.0;
    double last_snap_max_disp  = 0.0;

    claw3d::services::CgalSmoothingJobHandle runner;
    SMOOTH_ResultStats last_stats{};
    bool   last_stats_valid = false;

    std::string last_input_metadata_prompt;
    int    last_run_mode               = SMOOTH_MODE_TangentialRelaxation;
    int    last_run_iterations         = 10;
    float  last_run_time_step          = 0.0001f;
    bool   last_run_preserve_boundary  = true;
    bool   last_run_preserve_sharp     = true;
    float  last_run_sharp_angle        = 60.0f;

    bool   close_requested = false;
    std::string last_error;

    std::atomic<bool> final_result_ready{true};
};

void renderDialogCGALSmoothing(ViewportCanvas* viewer, SmoothingState& s, bool& open);

#endif  // CLAW3D_CGAL_SMOOTHING_DIALOG_H

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MCF_SKELETONIZATION_DIALOG_H
#define CLAW3D_MCF_SKELETONIZATION_DIALOG_H

/// State and render entry point for mean-curvature-flow skeletonization.

#include <atomic>
#include <string>
#include <vector>

#include "services/jobs/cgal/mcf_skeletonization_job.h"

class ViewportCanvas;

struct MCFSkelState {
    MCFSkelState() = default;
    MCFSkelState(const MCFSkelState& o) { *this = o; }
    MCFSkelState& operator=(const MCFSkelState& o) {
        max_iterations                   = o.max_iterations;
        area_variation_factor            = o.area_variation_factor;
        quality_speed_tradeoff           = o.quality_speed_tradeoff;
        medially_centered_speed_tradeoff = o.medially_centered_speed_tradeoff;
        min_edge_length                  = o.min_edge_length;
        max_triangle_angle_degrees       = o.max_triangle_angle_degrees;
        medially_centered                = o.medially_centered;
        auto_min_edge_length             = o.auto_min_edge_length;
        collect_correspondence           = o.collect_correspondence;
        show_original_ghost              = o.show_original_ghost;
        show_meso                        = o.show_meso;
        show_skeleton                    = o.show_skeleton;
        show_correspondence              = o.show_correspondence;
        show_sdf_heatmap                 = o.show_sdf_heatmap;
        live_preview                     = o.live_preview;
        preview_speed                    = o.preview_speed;
        runner                           = o.runner;
        last_result                      = o.last_result;
        last_result_valid                = o.last_result_valid;
        last_input_metadata_prompt       = o.last_input_metadata_prompt;
        result_base_name                 = o.result_base_name;
        close_requested                  = o.close_requested;
        settling                         = o.settling;
        settle_started_at                = o.settle_started_at;
        settle_ms                        = o.settle_ms;
        last_snap_gen                    = o.last_snap_gen;
        last_error                       = o.last_error;
        final_result_ready.store(
            o.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    // Algorithm parameters (mirror MCF_Config for ImGui binding ease).
    int   max_iterations                   = 500;
    float area_variation_factor            = 1e-4f;
    float quality_speed_tradeoff           = 0.1f;
    float medially_centered_speed_tradeoff = 0.2f;
    float min_edge_length                  = -1.0f; // < 0 = auto
    float max_triangle_angle_degrees       = 110.0f;
    bool  medially_centered                = true;
    bool  auto_min_edge_length             = true;
    bool  collect_correspondence           = false;

    // Visual toggles.
    bool  show_original_ghost = true;
    bool  show_meso           = true;  // active during live preview
    bool  show_skeleton       = true;
    bool  show_correspondence = false;
    bool  show_sdf_heatmap    = false;

    // Live-preview controls.
    bool  live_preview        = false;
    int   preview_speed       = MCF_PREVIEW_Normal;

    claw3d::services::McfSkeletonizationJobHandle runner;
    MCF_Result last_result{};
    bool  last_result_valid = false;

    std::string last_input_metadata_prompt;
    std::string result_base_name;

    // Async bookkeeping.
    bool   close_requested  = false;
    bool   settling         = false;
    double settle_started_at = 0.0;
    double settle_ms         = 1500.0;
    int    last_snap_gen     = -1;
    std::string last_error;

    std::atomic<bool> final_result_ready{true};
};

void renderDialogMCFSkeletonization(ViewportCanvas* viewer,
                                    MCFSkelState& s,
                                    bool& open);

#endif  // CLAW3D_MCF_SKELETONIZATION_DIALOG_H

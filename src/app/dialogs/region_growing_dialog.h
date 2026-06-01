// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_REGION_GROWING_DIALOG_H
#define CLAW3D_REGION_GROWING_DIALOG_H

/// State and render entry point for region-growing plane extraction.

#include <atomic>
#include <vector>

#include "services/jobs/cgal/region_growing_job.h"

class ViewportCanvas;

struct RGColorCmd { int idx; int region_id; };

struct RegionGrowingState {
    RegionGrowingState() = default;
    RegionGrowingState(const RegionGrowingState& other) { *this = other; }
    RegionGrowingState& operator=(const RegionGrowingState& other) {
        k_neighbors = other.k_neighbors;
        max_distance = other.max_distance;
        max_angle_deg = other.max_angle_deg;
        min_region_size = other.min_region_size;
        live_preview = other.live_preview;
        live_throttle = other.live_throttle;
        running_step = other.running_step;
        running_regions = other.running_regions;
        running_visited = other.running_visited;
        running_start_time = other.running_start_time;
        num_regions = other.num_regions;
        runner = other.runner;
        live_regions_added = other.live_regions_added;
        current_region_pending_idx = other.current_region_pending_idx;
        pending_cmds = other.pending_cmds;
        last_stats = other.last_stats;
        last_stats_valid = other.last_stats_valid;
        close_requested = other.close_requested;
        final_results_ready.store(
            other.final_results_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    int   k_neighbors = 20;
    float max_distance = -1;
    float max_angle_deg = 30;
    int   min_region_size = -1;
    bool  live_preview = false;
    bool  live_throttle = false;
    int   running_step = 0;
    int   running_regions = 0;
    int   running_visited = 0;
    float running_start_time = 0;
    int   num_regions = 0;

    claw3d::services::RegionGrowingJobHandle runner;
    int  live_regions_added = 0;
    std::vector<int> current_region_pending_idx;
    std::vector<RGColorCmd> pending_cmds;
    RG_DebugStats last_stats;
    bool last_stats_valid = false;
    bool close_requested = false;
    std::atomic<bool> final_results_ready{true};
};

void renderDialogRegionGrowing(ViewportCanvas* viewer, RegionGrowingState& s, bool& open);

#endif // CLAW3D_REGION_GROWING_DIALOG_H

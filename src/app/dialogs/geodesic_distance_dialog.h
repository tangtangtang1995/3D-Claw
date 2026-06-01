// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_GEODESIC_DISTANCE_DIALOG_H
#define CLAW3D_GEODESIC_DISTANCE_DIALOG_H

/// State and render entry point for surface geodesic-distance visualization.

#include <atomic>
#include <string>
#include <vector>

#include "services/jobs/easy3d/geodesic_distance_job.h"

class ViewportCanvas;

namespace easy3d { class SurfaceMesh; }

enum GEO_Mode : int {
    GEO_MODE_FrontPropagation  = 0,
    GEO_MODE_ExactShortestPath = 1,
    GEO_MODE_HeatMethod        = 2
};

enum GEO_HeatVariant : int {
    GEO_HEAT_Direct            = 0,
    GEO_HEAT_IntrinsicDelaunay = 1
};

struct GeodesicState {
    GeodesicState() = default;
    GeodesicState(const GeodesicState& o) { *this = o; }
    GeodesicState& operator=(const GeodesicState& o) {
        mode                = o.mode;
        heat_variant        = o.heat_variant;
        sources             = o.sources;
        pending_source_vid  = o.pending_source_vid;
        target_vid          = o.target_vid;
        target_valid        = o.target_valid;
        pending_target_vid  = o.pending_target_vid;
        use_virtual_edges   = o.use_virtual_edges;
        live_preview        = o.live_preview;
        preview_speed       = o.preview_speed;
        compare_exact_path  = o.compare_exact_path;
        last_result_mode    = o.last_result_mode;
        return *this;
    }

    int  mode               = GEO_MODE_FrontPropagation;
    int  heat_variant       = GEO_HEAT_Direct;
    std::vector<int> sources;            // selected source vertex ids
    int  pending_source_vid = 0;         // value in the "Add Source" input
    int  target_vid         = -1;        // -1 if no target set
    bool target_valid       = false;
    int  pending_target_vid = 0;         // value in the "Set Target" input

    // Algorithm options.
    bool use_virtual_edges  = true;      // Front mode only
    bool live_preview       = true;
    int  preview_speed      = 0;         // 0 = Normal, 1 = Slow
    // Run CGAL exact shortest path after front propagation.
    bool compare_exact_path = false;
    int  last_result_mode   = GEO_MODE_FrontPropagation;

    // Worker + snapshot bookkeeping.
    claw3d::services::GeodesicFrontJobHandle front_runner;
    GEO_FrontResultStats last_stats{};
    bool   last_stats_valid = false;
    int    last_snap_gen    = -1;
    bool   close_requested  = false;
    bool   settling         = false;
    double settle_started_at = 0.0;
    double settle_ms         = 1500.0;
    // Always-on progress cache so the Cancel button does not jitter.
    int    last_visited      = 0;
    int    last_front_size   = 0;
    float  last_max_distance = 0.0f;
    std::string last_error;
    std::atomic<bool> final_result_ready{true};

#ifdef CLAW3D_HAS_CGAL
    // Standalone Exact Shortest Path runner.
    claw3d::services::GeodesicCgalJobHandle exact_runner;
    GEO_CGAL_PathResult  exact_path_result{};
    bool   exact_result_valid = false;
    std::atomic<bool> exact_final_ready{true};
#endif

    // Vertex picking (one-shot).
    int  pick_mode  = 0;  // 0=None, 1=Source, 2=Target
    bool owns_viewport_input_lock = false;
    char pick_status[128] = {};
};

void renderDialogGeodesicDistance(ViewportCanvas* viewer,
                                  GeodesicState& s,
                                  bool& open);

#endif // CLAW3D_GEODESIC_DISTANCE_DIALOG_H

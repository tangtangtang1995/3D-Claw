// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_REGION_GROWING_RUNNER_H
#define CLAW3D_REGION_GROWING_RUNNER_H

/// CGAL region-growing plane extraction runner behind the services facade.

#include "claw3d_cgal_algo_export.h"
#include "common/region_growing_contract.h"

#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API RegionGrowingRunner {
public:
    struct Impl;

    RegionGrowingRunner();
    ~RegionGrowingRunner();

    RegionGrowingRunner(const RegionGrowingRunner&) = delete;
    RegionGrowingRunner& operator=(const RegionGrowingRunner&) = delete;

    void set_input(const std::vector<RG_Point3d>& points,
        const std::vector<RG_Vector3d>& normals);

    void run(const RG_Config& cfg);

    // Live preview
    bool drain_live_events(std::vector<RG_FrameEvent>& out_events);

    // Cancel
    void cancel();
    bool is_cancelled() const;

    // Pause / Resume / Step
    void pause();
    void resume();
    void step();
    bool is_paused() const;

    // Error
    void        set_error(const std::string& msg);
    bool        has_error() const;
    std::string last_error() const;
    void        clear_error();

    // Status
    bool  is_done() const;
    int   num_regions() const;
    int   total_points() const;
    int   current_region_size() const; // growing region's live size
    float progress() const;
    RG_DebugStats debug_stats() const;

    // Results
    void get_region(int idx, RG_RegionResult& out) const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_REGION_GROWING_RUNNER_H

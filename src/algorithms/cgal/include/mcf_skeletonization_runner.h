// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MCF_SKELETONIZATION_RUNNER_H
#define CLAW3D_MCF_SKELETONIZATION_RUNNER_H

/// CGAL mean-curvature-flow skeletonization runner behind the services facade.

#include "claw3d_cgal_algo_export.h"
#include "common/mcf_skeletonization_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API MCFSkeletonizationRunner {
public:
    struct Impl;

    MCFSkeletonizationRunner();
    ~MCFSkeletonizationRunner();

    MCFSkeletonizationRunner(const MCFSkeletonizationRunner&)            = delete;
    MCFSkeletonizationRunner& operator=(const MCFSkeletonizationRunner&) = delete;

    void set_input(const std::vector<MCF_Point3d>&  verts,
                   const std::vector<MCF_Triangle>& tris);

    // Synchronous. Caller spawns a worker thread for async behavior.
    void run(const MCF_Config& cfg);

    void cancel();
    bool is_cancelled() const;
    bool is_done()      const;
    bool has_error()    const;
    std::string last_error() const;

    bool poll_snapshot(int& last_generation, MCF_Snapshot& out) const;

    void get_result(MCF_Result& out) const;
    MCF_Metrics result_metrics() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_MCF_SKELETONIZATION_RUNNER_H

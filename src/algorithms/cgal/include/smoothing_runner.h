// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SMOOTHING_RUNNER_H
#define CLAW3D_SMOOTHING_RUNNER_H

/// CGAL smoothing runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/smoothing_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API SmoothingRunner {
public:
    struct Impl;

    SmoothingRunner();
    ~SmoothingRunner();

    SmoothingRunner(const SmoothingRunner&) = delete;
    SmoothingRunner& operator=(const SmoothingRunner&) = delete;

    void set_input(const std::vector<SMOOTH_Point3d>&  verts,
                   const std::vector<SMOOTH_Triangle>& tris);

    // Synchronous run. Caller spawns a worker thread if async behavior needed.
    void run(const SMOOTH_Config& cfg);

    void cancel();
    bool is_cancelled() const;
    bool is_done()      const;
    bool has_error()    const;
    std::string last_error() const;
    void set_error(const std::string& msg);

    bool poll_snapshot(int& last_generation, SMOOTH_Snapshot& out) const;

    void get_result(std::vector<SMOOTH_Point3d>&  out_verts,
                    std::vector<SMOOTH_Triangle>& out_tris) const;

    SMOOTH_ResultStats result_stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif  // CLAW3D_SMOOTHING_RUNNER_H

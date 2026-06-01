// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_GEOMETRY_ICP_H
#define CLAW3D_GEOMETRY_ICP_H

/// Geometry-only ICP alignment routines independent of UI state.

#include <easy3d/core/matrix.h>
#include <easy3d/core/types.h>

#include <limits>
#include <vector>

namespace claw3d::algo {

struct ICPResult {
    easy3d::mat4 T = easy3d::mat4::identity();
    float rms = std::numeric_limits<float>::infinity();
    int iterations_run = 0;
    int correspondences = 0;
    bool ok = false;
};

bool kabsch_rigid(const std::vector<easy3d::vec3>& src,
                  const std::vector<easy3d::vec3>& dst,
                  easy3d::mat4& out_T,
                  float& out_rms);

ICPResult run_icp(const std::vector<easy3d::vec3>& src_full,
                  const std::vector<easy3d::vec3>& dst_full,
                  int max_iter,
                  int sample_count,
                  float outlier_factor,
                  float tolerance);

} // namespace claw3d::algo

#endif // CLAW3D_GEOMETRY_ICP_H

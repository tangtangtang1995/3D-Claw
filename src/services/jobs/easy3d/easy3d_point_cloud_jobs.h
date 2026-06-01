// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_EASY3D_POINT_CLOUD_JOBS_H
#define CLAW3D_SERVICES_EASY3D_POINT_CLOUD_JOBS_H

/// Service facades for Easy3D point-cloud operations run off the UI thread.

#include "common/model_handle.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace easy3d {
class PointCloud;
}

class AlgorithmController;

namespace claw3d::services {

/// Applies synchronous Easy3D point-cloud operations owned by services.
std::size_t apply_point_cloud_grid_simplification(
    easy3d::PointCloud* cloud,
    float radius);

/// Start request captured before the backend worker is launched.
struct PointCloudNormalEstimationJobStart {
    easy3d::PointCloud* source_cloud = nullptr;
    ModelHandle source_handle;
    int k_neighbors = 16;
    bool reorient = false;
    bool normalize = true;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_point_cloud_normal_estimation_job(
    AlgorithmController& controller,
    const PointCloudNormalEstimationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_EASY3D_POINT_CLOUD_JOBS_H

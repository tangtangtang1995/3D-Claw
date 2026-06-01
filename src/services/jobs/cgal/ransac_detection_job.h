// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_RANSAC_DETECTION_JOB_H
#define CLAW3D_SERVICES_RANSAC_DETECTION_JOB_H

/// Service facade for launching and polling RANSAC detection jobs.

#include "common/model_handle.h"
#include "common/ransac_contract.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace easy3d {
class PointCloud;
}

class AlgorithmController;

namespace claw3d::services {

struct RansacDetectionJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class RansacDetectionJobHandle {
public:
    RansacDetectionJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    bool drain_live_events(std::vector<RANSAC_FrameEvent>& out_events) const;
    void cancel() const;
    bool is_cancelled() const;
    void pause() const;
    void resume() const;
    void step() const;
    bool is_paused() const;
    int total_shapes() const;
    int total_points() const;

private:
    friend RansacDetectionJobHandle start_ransac_detection_job(
        AlgorithmController& controller,
        const RansacDetectionJobStart& request);

    struct Impl;

    explicit RansacDetectionJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct RansacDetectionJobStart {
    easy3d::PointCloud* source_cloud = nullptr;
    ModelHandle source_handle;
    RANSAC_Config config;
    std::function<void()> wake_ui;
};

RansacDetectionJobHandle start_ransac_detection_job(
    AlgorithmController& controller,
    const RansacDetectionJobStart& request);

/// Geometry helpers used by the app to build primitive preview children.
void compute_ransac_convex_hull_2d(
    const double* in_pts,
    int count,
    const double plane_eq[4],
    std::vector<RANSAC_Point3d>& out_hull);

void compute_ransac_alpha_shape_2d(
    const double* in_pts,
    int count,
    const double plane_eq[4],
    double alpha,
    std::vector<RANSAC_Point3d>& out_vertices,
    std::vector<std::array<int, 3>>& out_triangles);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_RANSAC_DETECTION_JOB_H

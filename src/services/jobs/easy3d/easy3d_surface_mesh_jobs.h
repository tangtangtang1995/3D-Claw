// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_EASY3D_SURFACE_MESH_JOBS_H
#define CLAW3D_SERVICES_EASY3D_SURFACE_MESH_JOBS_H

/// Service facades for Easy3D surface-mesh operations run off the UI thread.

#include "common/model_handle.h"

#include <functional>
#include <string>

namespace easy3d {
class SurfaceMesh;
}

class AlgorithmController;

namespace claw3d::services {

/// Applies synchronous Easy3D surface-mesh operations owned by services.
bool apply_surface_mesh_curvature(easy3d::SurfaceMesh* mesh,
                                  int smooth_iterations,
                                  bool two_ring_neighborhood);

bool apply_surface_mesh_parameterization(easy3d::SurfaceMesh* mesh,
                                         int method);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshSamplingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int target_points = 0;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_sampling_job(
    AlgorithmController& controller,
    const SurfaceMeshSamplingJobStart& request);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshSimplificationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int target_vertices = 4;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_simplification_job(
    AlgorithmController& controller,
    const SurfaceMeshSimplificationJobStart& request);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshSmoothingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int scheme = 0;
    int iterations = 1;
    bool uniform_laplace = false;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_smoothing_job(
    AlgorithmController& controller,
    const SurfaceMeshSmoothingJobStart& request);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshFairingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int criterion = 0;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_fairing_job(
    AlgorithmController& controller,
    const SurfaceMeshFairingJobStart& request);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshHoleFillingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_hole_filling_job(
    AlgorithmController& controller,
    const SurfaceMeshHoleFillingJobStart& request);

/// Start request captured before the backend worker is launched.
struct SurfaceMeshRemeshingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int scheme = 0;
    float edge_length = 0.0f;
    bool use_features = false;
    int feature_angle = 30;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_surface_mesh_remeshing_job(
    AlgorithmController& controller,
    const SurfaceMeshRemeshingJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_EASY3D_SURFACE_MESH_JOBS_H

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_POISSON_RECONSTRUCTION_JOB_H
#define CLAW3D_SERVICES_POISSON_RECONSTRUCTION_JOB_H

/// Service facade for launching Poisson surface reconstruction jobs.

#include "common/model_handle.h"

#include <functional>
#include <string>

namespace easy3d {
class PointCloud;
}

class AlgorithmController;

namespace claw3d::services {

/// Start request captured before the backend worker is launched.
struct PoissonReconstructionJobStart {
    easy3d::PointCloud* source_cloud = nullptr;
    ModelHandle source_handle;
    int depth = 8;
    float samples_per_node = 1.0f;
    int cg_depth = 0;
    float scale = 1.1f;
    std::string source_name;
    std::function<void()> wake_ui;
};

bool start_poisson_reconstruction_job(
    AlgorithmController& controller,
    const PoissonReconstructionJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_POISSON_RECONSTRUCTION_JOB_H

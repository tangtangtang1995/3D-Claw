// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_PARAMETERIZATION_JOB_H
#define CLAW3D_SERVICES_PARAMETERIZATION_JOB_H

/// Service facade for launching and polling parameterization jobs.

#include "common/model_handle.h"
#include "common/parameterization_contract.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace easy3d {
class SurfaceMesh;
}

class AlgorithmController;

namespace claw3d::services {

struct ParameterizationJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class ParameterizationJobHandle {
public:
    ParameterizationJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    void get_result(PARAM_Result& out) const;

private:
    friend ParameterizationJobHandle start_parameterization_job(
        AlgorithmController& controller,
        const ParameterizationJobStart& request);

    struct Impl;

    explicit ParameterizationJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct ParameterizationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    PARAM_Config config;
    std::vector<PARAM_SeamPath> seam_paths;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

ParameterizationJobHandle start_parameterization_job(
    AlgorithmController& controller,
    const ParameterizationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_PARAMETERIZATION_JOB_H

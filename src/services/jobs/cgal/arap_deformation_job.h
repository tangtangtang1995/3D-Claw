// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_ARAP_DEFORMATION_JOB_H
#define CLAW3D_SERVICES_ARAP_DEFORMATION_JOB_H

/// Service facade for launching and polling ARAP deformation jobs.

#include "common/arap_deformation_contract.h"
#include "common/model_handle.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace easy3d {
class SurfaceMesh;
}

class AlgorithmController;

namespace claw3d::services {

struct ArapDeformationJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class ArapDeformationJobHandle {
public:
    ArapDeformationJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    bool poll_snapshot(int& last_generation, ARAP_Snapshot& out) const;
    void get_result(ARAP_Result& out) const;

private:
    friend ArapDeformationJobHandle start_arap_deformation_job(
        AlgorithmController& controller,
        const ArapDeformationJobStart& request);

    struct Impl;

    explicit ArapDeformationJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct ArapDeformationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    ARAP_Config config;
    ARAP_Selection selection;
    ARAP_ControlTransform control_transform;
    std::string source_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

ArapDeformationJobHandle start_arap_deformation_job(
    AlgorithmController& controller,
    const ArapDeformationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_ARAP_DEFORMATION_JOB_H

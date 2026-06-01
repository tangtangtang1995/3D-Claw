// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_MCF_SKELETONIZATION_JOB_H
#define CLAW3D_SERVICES_MCF_SKELETONIZATION_JOB_H

/// Service facade for launching and polling MCF skeletonization jobs.

#include "common/model_handle.h"
#include "common/mcf_skeletonization_contract.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace easy3d {
class SurfaceMesh;
}

class AlgorithmController;

namespace claw3d::services {

/// Performs cheap preflight checks before starting MCF skeletonization.
bool validate_mcf_skeletonization_input(easy3d::SurfaceMesh* mesh,
                                        std::string& reason);

struct McfSkeletonizationJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class McfSkeletonizationJobHandle {
public:
    McfSkeletonizationJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    bool poll_snapshot(int& last_generation, MCF_Snapshot& out) const;
    void get_result(MCF_Result& out) const;
    MCF_Metrics result_metrics() const;

private:
    friend McfSkeletonizationJobHandle start_mcf_skeletonization_job(
        AlgorithmController& controller,
        const McfSkeletonizationJobStart& request);

    struct Impl;

    explicit McfSkeletonizationJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct McfSkeletonizationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    MCF_Config config;
    std::string result_base_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

McfSkeletonizationJobHandle start_mcf_skeletonization_job(
    AlgorithmController& controller,
    const McfSkeletonizationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_MCF_SKELETONIZATION_JOB_H

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_VSA_APPROXIMATION_JOB_H
#define CLAW3D_SERVICES_VSA_APPROXIMATION_JOB_H

/// Service facade for launching and polling VSA approximation jobs.

#include "common/model_handle.h"
#include "common/vsa_contract.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace easy3d {
class SurfaceMesh;
}

class AlgorithmController;

namespace claw3d::services {

struct VsaApproximationJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class VsaApproximationJobHandle {
public:
    VsaApproximationJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    bool cancelled_or_failed() const;
    bool done_and_ready(const std::atomic<bool>& ready) const;
    void copy_error_if_any(std::string& error) const;
    bool poll_snapshot(int& last_generation, VSA_Snapshot& out) const;
    VSA_DebugStats debug_stats() const;

private:
    friend VsaApproximationJobHandle start_vsa_approximation_job(
        AlgorithmController& controller,
        const VsaApproximationJobStart& request);

    struct Impl;

    explicit VsaApproximationJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct VsaApproximationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    VSA_Config config;
    std::string source_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

VsaApproximationJobHandle start_vsa_approximation_job(
    AlgorithmController& controller,
    const VsaApproximationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_VSA_APPROXIMATION_JOB_H

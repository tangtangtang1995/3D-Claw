// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_ACVD_REMESHING_JOB_H
#define CLAW3D_SERVICES_ACVD_REMESHING_JOB_H

/// Service facade for launching and polling ACVD remeshing jobs.

#include "common/acvd_contract.h"
#include "common/model_handle.h"

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

struct AcvdRemeshingJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class AcvdRemeshingJobHandle {
public:
    AcvdRemeshingJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool cancelled_or_failed() const;
    bool done_and_ready(const std::atomic<bool>& ready) const;
    void copy_error_if_any(std::string& error) const;
    ACVD_DebugStats debug_stats() const;
    void get_seed_positions(std::vector<ACVD_Point3d>& out) const;
    bool poll_cluster_snapshot(int& last_generation,
                               std::vector<ACVD_Point3d>& verts,
                               std::vector<ACVD_Triangle>& tris,
                               std::vector<int>& face_cluster_ids,
                               std::vector<ACVD_Point3d>& cluster_centers) const;

private:
    friend AcvdRemeshingJobHandle start_acvd_remeshing_job(
        AlgorithmController& controller,
        const AcvdRemeshingJobStart& request);

    struct Impl;

    explicit AcvdRemeshingJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct AcvdRemeshingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    ACVD_Config config;
    std::string source_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

AcvdRemeshingJobHandle start_acvd_remeshing_job(
    AlgorithmController& controller,
    const AcvdRemeshingJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_ACVD_REMESHING_JOB_H

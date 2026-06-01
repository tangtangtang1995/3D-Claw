// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_PLANAR_PATCH_REMESHING_JOB_H
#define CLAW3D_SERVICES_PLANAR_PATCH_REMESHING_JOB_H

/// Service facade for launching and polling planar-patch remeshing jobs.

#include "common/model_handle.h"
#include "common/planar_patch_remeshing_contract.h"

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

struct PlanarPatchRemeshingJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class PlanarPatchRemeshingJobHandle {
public:
    PlanarPatchRemeshingJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    bool cancelled_or_failed() const;
    void copy_error_if_any(std::string& error) const;
    bool poll_snapshot(int& last_generation, PPR_Snapshot& out) const;
    bool has_pending_snapshot(int last_generation) const;
    PPR_DebugStats debug_stats() const;

private:
    friend PlanarPatchRemeshingJobHandle start_planar_patch_remeshing_job(
        AlgorithmController& controller,
        const PlanarPatchRemeshingJobStart& request);

    struct Impl;

    explicit PlanarPatchRemeshingJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct PlanarPatchRemeshingJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    PPR_Config config;
    std::vector<int> existing_face_patch_ids;
    std::string source_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

PlanarPatchRemeshingJobHandle start_planar_patch_remeshing_job(
    AlgorithmController& controller,
    const PlanarPatchRemeshingJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_PLANAR_PATCH_REMESHING_JOB_H

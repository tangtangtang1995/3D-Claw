// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_REGION_GROWING_JOB_H
#define CLAW3D_SERVICES_REGION_GROWING_JOB_H

/// Service facade for launching and polling region-growing jobs.

#include "common/model_handle.h"
#include "common/region_growing_contract.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace easy3d {
class PointCloud;
}

class AlgorithmController;

namespace claw3d::services {

struct RegionGrowingJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class RegionGrowingJobHandle {
public:
    RegionGrowingJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    bool drain_live_events(std::vector<RG_FrameEvent>& out_events) const;
    void cancel() const;
    bool is_cancelled() const;
    void pause() const;
    void resume() const;
    void step() const;
    bool is_paused() const;
    bool is_done() const;
    int num_regions() const;
    float progress() const;
    RG_DebugStats debug_stats() const;
    void get_region(int idx, RG_RegionResult& out) const;

private:
    friend RegionGrowingJobHandle start_region_growing_job(
        AlgorithmController& controller,
        const RegionGrowingJobStart& request);

    struct Impl;

    explicit RegionGrowingJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct RegionGrowingJobStart {
    easy3d::PointCloud* source_cloud = nullptr;
    ModelHandle source_handle;
    RG_Config config;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

RegionGrowingJobHandle start_region_growing_job(
    AlgorithmController& controller,
    const RegionGrowingJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_REGION_GROWING_JOB_H

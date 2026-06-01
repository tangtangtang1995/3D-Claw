// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_ALPHA_WRAP_JOB_H
#define CLAW3D_SERVICES_ALPHA_WRAP_JOB_H

/// Service facade for launching and polling Alpha Wrap jobs.

#include "common/alpha_wrap_contract.h"
#include "common/model_handle.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace easy3d {
class Model;
}

class AlgorithmController;

namespace claw3d::services {

struct AlphaWrapJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class AlphaWrapJobHandle {
public:
    AlphaWrapJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    bool drain_live_events(std::vector<AW3_FrameEvent>& out_events) const;
    bool drain_live_surface_snapshot(std::vector<AW3_Point3d>& verts,
                                     std::vector<AW3_Triangle>& faces) const;
    void cancel() const;
    bool is_cancelled() const;
    bool has_error() const;
    std::string last_error() const;
    void clear_error() const;

private:
    friend AlphaWrapJobHandle start_alpha_wrap_job(
        AlgorithmController& controller,
        const AlphaWrapJobStart& request);

    struct Impl;

    explicit AlphaWrapJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct AlphaWrapJobStart {
    easy3d::Model* source_model = nullptr;
    ModelHandle source_handle;
    float alpha = 0.05f;
    float offset = 0.002f;
    bool live_preview = false;
    int live_surface_interval = 0;
    std::string source_name;
    std::function<void()> wake_ui;
};

AlphaWrapJobHandle start_alpha_wrap_job(
    AlgorithmController& controller,
    const AlphaWrapJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_ALPHA_WRAP_JOB_H

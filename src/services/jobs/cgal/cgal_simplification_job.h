// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_CGAL_SIMPLIFICATION_JOB_H
#define CLAW3D_SERVICES_CGAL_SIMPLIFICATION_JOB_H

/// Service facade for launching and polling CGAL simplification jobs.

#include "common/model_handle.h"
#include "common/simplification_contract.h"

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

struct CgalSimplificationJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class CgalSimplificationJobHandle {
public:
    CgalSimplificationJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    bool drain_live_events(std::vector<SIMPL_FrameEvent>& out_events) const;
    bool poll_snapshot(int& last_generation,
                       std::vector<SIMPL_Point3d>& out_verts,
                       std::vector<SIMPL_Triangle>& out_tris) const;
    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    float progress() const;
    bool has_error() const;
    std::string last_error() const;
    bool cancelled_or_failed() const;
    bool done_and_ready(const std::atomic<bool>& ready) const;
    void copy_error_if_any(std::string& error) const;
    SIMPL_DebugStats debug_stats() const;

private:
    friend CgalSimplificationJobHandle start_cgal_simplification_job(
        AlgorithmController& controller,
        const CgalSimplificationJobStart& request);

    struct Impl;

    explicit CgalSimplificationJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct CgalSimplificationJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    SIMPL_Config config;
    std::string source_name;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

CgalSimplificationJobHandle start_cgal_simplification_job(
    AlgorithmController& controller,
    const CgalSimplificationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_CGAL_SIMPLIFICATION_JOB_H

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_GEODESIC_DISTANCE_JOB_H
#define CLAW3D_SERVICES_GEODESIC_DISTANCE_JOB_H

/// Service facades for Easy3D/CGAL geodesic distance jobs.

#include "common/geodesic_contract.h"
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

struct GeodesicFrontJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class GeodesicFrontJobHandle {
public:
    GeodesicFrontJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    bool poll_snapshot(int& last_generation, GEO_FrontSnapshot& out) const;
    GEO_FrontResultStats result_stats() const;
    void get_front_path(std::vector<float>& xyz_flat) const;
    void get_exact_path(std::vector<float>& xyz_flat) const;

private:
    friend GeodesicFrontJobHandle start_geodesic_front_job(
        AlgorithmController& controller,
        const GeodesicFrontJobStart& request);

    struct Impl;

    explicit GeodesicFrontJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct GeodesicFrontJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    std::vector<int> source_vertex_ids;
    GEO_FrontConfig config;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

GeodesicFrontJobHandle start_geodesic_front_job(
    AlgorithmController& controller,
    const GeodesicFrontJobStart& request);

#ifdef CLAW3D_HAS_CGAL

struct GeodesicExactPathJobStart;
struct GeodesicHeatMethodJobStart;

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class GeodesicCgalJobHandle {
public:
    GeodesicCgalJobHandle() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    void reset();

    void cancel() const;
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;
    GEO_CGAL_HeatResult heat_result() const;

private:
    friend GeodesicCgalJobHandle start_geodesic_exact_path_job(
        AlgorithmController& controller,
        const GeodesicExactPathJobStart& request);
    friend GeodesicCgalJobHandle start_geodesic_heat_method_job(
        AlgorithmController& controller,
        const GeodesicHeatMethodJobStart& request);

    struct Impl;

    explicit GeodesicCgalJobHandle(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

/// Start request captured before the backend worker is launched.
struct GeodesicExactPathJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    int source_vertex_id = -1;
    int target_vertex_id = -1;
    GEO_CGAL_PathResult* path_result = nullptr;
    bool* result_valid = nullptr;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

GeodesicCgalJobHandle start_geodesic_exact_path_job(
    AlgorithmController& controller,
    const GeodesicExactPathJobStart& request);

/// Start request captured before the backend worker is launched.
struct GeodesicHeatMethodJobStart {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    ModelHandle source_handle;
    std::vector<int> source_vertex_ids;
    int heat_variant = 0;
    bool* result_valid = nullptr;
    std::atomic<bool>* final_result_ready = nullptr;
    std::function<void()> wake_ui;
};

GeodesicCgalJobHandle start_geodesic_heat_method_job(
    AlgorithmController& controller,
    const GeodesicHeatMethodJobStart& request);

#endif // CLAW3D_HAS_CGAL

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_GEODESIC_DISTANCE_JOB_H

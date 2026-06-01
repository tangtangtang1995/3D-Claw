// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/easy3d/easy3d_surface_mesh_jobs.h"

#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"

#include <easy3d/algo/surface_mesh_curvature.h>
#include <easy3d/algo/surface_mesh_fairing.h>
#include <easy3d/algo/surface_mesh_features.h>
#include <easy3d/algo/surface_mesh_hole_filling.h>
#include <easy3d/algo/surface_mesh_parameterization.h>
#include <easy3d/algo/surface_mesh_remeshing.h>
#include <easy3d/algo/surface_mesh_sampler.h>
#include <easy3d/algo/surface_mesh_simplification.h>
#include <easy3d/algo/surface_mesh_smoothing.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace claw3d::services {
namespace {

template <typename Fn>
bool start_replace_source_mesh_job(AlgorithmController& controller,
                                   easy3d::SurfaceMesh* source_mesh,
                                   const ModelHandle& source_handle,
                                   AlgorithmId id,
                                   const char* label,
                                   const std::string& source_name,
                                   std::function<void()> wake_ui,
                                   Fn&& fn)
{
    if (!source_mesh) {
        LOG(WARNING) << label << ": missing source mesh";
        return false;
    }

    auto work = std::make_unique<easy3d::SurfaceMesh>(*source_mesh);

    controller.begin(id, label, source_handle, ResultDisposition::ReplaceSource);
    controller.start_worker(std::thread(
        [&controller,
         work = std::move(work),
         source_name,
         wake_ui = std::move(wake_ui),
         task = std::forward<Fn>(fn),
         label]() mutable {
            try {
                task(work.get());
                work->set_name(source_name);
                controller.push_result(std::move(work));
            } catch (const std::exception& e) {
                LOG(ERROR) << label << " failed: " << e.what();
            } catch (...) {
                LOG(ERROR) << label << " failed: unknown error";
            }

            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));
    return true;
}

} // namespace

bool apply_surface_mesh_curvature(easy3d::SurfaceMesh* mesh,
                                  int smooth_iterations,
                                  bool two_ring_neighborhood)
{
    if (!mesh)
        return false;

    easy3d::SurfaceMeshCurvature analyzer(mesh);
    analyzer.analyze_tensor(smooth_iterations, two_ring_neighborhood);
    analyzer.compute_mean_curvature();
    analyzer.compute_gauss_curvature();
    analyzer.compute_max_abs_curvature();
    return true;
}

bool apply_surface_mesh_parameterization(easy3d::SurfaceMesh* mesh,
                                         int method)
{
    if (!mesh)
        return false;

    easy3d::SurfaceMeshParameterization parameterization(mesh);
    if (method == 0)
        parameterization.lscm();
    else
        parameterization.harmonic();
    return true;
}

bool start_surface_mesh_sampling_job(
    AlgorithmController& controller,
    const SurfaceMeshSamplingJobStart& request)
{
    if (!request.source_mesh) {
        LOG(WARNING) << "Surface Mesh Sampling: missing source mesh";
        return false;
    }

    auto input = std::make_unique<easy3d::SurfaceMesh>(*request.source_mesh);
    controller.begin(AlgorithmId::SurfaceMeshSampling,
                     "Surface Mesh Sampling",
                     request.source_handle,
                     ResultDisposition::AddAsChild);
    controller.start_worker(std::thread(
        [&controller,
         input = std::move(input),
         target_points = request.target_points,
         source_name = request.source_name,
         wake_ui = request.wake_ui]() mutable {
            try {
                auto* cloud =
                    easy3d::SurfaceMeshSampler::apply(input.get(), target_points);
                if (cloud) {
                    cloud->set_name(source_name + ".sampled");
                    controller.push_owned_result(cloud);
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Surface Mesh Sampling failed: " << e.what();
            } catch (...) {
                LOG(ERROR) << "Surface Mesh Sampling failed: unknown error";
            }

            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));
    return true;
}

bool start_surface_mesh_simplification_job(
    AlgorithmController& controller,
    const SurfaceMeshSimplificationJobStart& request)
{
    return start_replace_source_mesh_job(
        controller,
        request.source_mesh,
        request.source_handle,
        AlgorithmId::SurfaceMeshSimplification,
        "Surface Mesh Simplification",
        request.source_name,
        request.wake_ui,
        [target_vertices = request.target_vertices](easy3d::SurfaceMesh* mesh) {
            easy3d::SurfaceMeshSimplification simplifier(mesh);
            simplifier.initialize(10.0f, 0.0f, 0u, 180.0f, 0.0f);
            simplifier.simplify(static_cast<unsigned int>(target_vertices));
        });
}

bool start_surface_mesh_smoothing_job(
    AlgorithmController& controller,
    const SurfaceMeshSmoothingJobStart& request)
{
    return start_replace_source_mesh_job(
        controller,
        request.source_mesh,
        request.source_handle,
        AlgorithmId::SurfaceMeshSmoothing,
        "Surface Mesh Smoothing",
        request.source_name,
        request.wake_ui,
        [scheme = request.scheme,
         iterations = request.iterations,
         uniform = request.uniform_laplace](easy3d::SurfaceMesh* mesh) {
            easy3d::SurfaceMeshSmoothing smoother(mesh);
            if (scheme == 0)
                smoother.explicit_smoothing(iterations, uniform);
            else
                smoother.implicit_smoothing(0.001f);
        });
}

bool start_surface_mesh_fairing_job(
    AlgorithmController& controller,
    const SurfaceMeshFairingJobStart& request)
{
    return start_replace_source_mesh_job(
        controller,
        request.source_mesh,
        request.source_handle,
        AlgorithmId::SurfaceMeshFairing,
        "Surface Mesh Fairing",
        request.source_name,
        request.wake_ui,
        [criterion = request.criterion](easy3d::SurfaceMesh* mesh) {
            easy3d::SurfaceMeshFairing fairing(mesh);
            if (criterion == 0)
                fairing.minimize_area();
            else
                fairing.minimize_curvature();
        });
}

bool start_surface_mesh_hole_filling_job(
    AlgorithmController& controller,
    const SurfaceMeshHoleFillingJobStart& request)
{
    return start_replace_source_mesh_job(
        controller,
        request.source_mesh,
        request.source_handle,
        AlgorithmId::SurfaceMeshHoleFilling,
        "Surface Mesh Hole Filling",
        request.source_name,
        request.wake_ui,
        [](easy3d::SurfaceMesh* mesh) {
            easy3d::SurfaceMeshHoleFilling hole_filling(mesh);
            hole_filling.fill_holes();
        });
}

bool start_surface_mesh_remeshing_job(
    AlgorithmController& controller,
    const SurfaceMeshRemeshingJobStart& request)
{
    return start_replace_source_mesh_job(
        controller,
        request.source_mesh,
        request.source_handle,
        AlgorithmId::SurfaceMeshRemeshing,
        "Surface Mesh Remeshing",
        request.source_name,
        request.wake_ui,
        [scheme = request.scheme,
         use_features = request.use_features,
         feature_angle = request.feature_angle,
         edge_length = request.edge_length](easy3d::SurfaceMesh* mesh) {
            if (use_features) {
                easy3d::SurfaceMeshFeatures features(mesh);
                features.clear();
                features.detect_angle(static_cast<float>(feature_angle));
                features.detect_boundary();
            }
            easy3d::SurfaceMeshRemeshing remeshing(mesh);
            if (scheme == 0)
                remeshing.uniform_remeshing(edge_length);
            else
                remeshing.adaptive_remeshing(
                    edge_length, edge_length * 2.0f, 0.001f);
        });
}

} // namespace claw3d::services

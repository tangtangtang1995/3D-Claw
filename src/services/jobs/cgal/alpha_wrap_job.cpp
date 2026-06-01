// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/alpha_wrap_job.h"
#include "services/core/job_handle_detail.h"
#include "alpha_wrap_runner.h"
#include "mesh_quality.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/core/model.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

void point_cloud_to_pod(const easy3d::PointCloud* cloud,
                        std::vector<AW3_Point3d>& points)
{
    points.clear();
    if (!cloud)
        return;

    points.reserve(cloud->n_vertices());
    for (auto v : cloud->vertices()) {
        const auto& p = cloud->position(v);
        points.push_back({p.x, p.y, p.z});
    }
}

} // namespace

struct AlphaWrapJobHandle::Impl {
    explicit Impl(std::shared_ptr<AlphaWrapRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<AlphaWrapRunner> runner;
};

AlphaWrapJobHandle::AlphaWrapJobHandle(
    std::shared_ptr<AlphaWrapJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool AlphaWrapJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void AlphaWrapJobHandle::reset()
{
    detail::reset_handle(impl_);
}

bool AlphaWrapJobHandle::drain_live_events(
    std::vector<AW3_FrameEvent>& out_events) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->drain_live_events(out_events);
}

bool AlphaWrapJobHandle::drain_live_surface_snapshot(
    std::vector<AW3_Point3d>& verts,
    std::vector<AW3_Triangle>& faces) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->drain_live_surface_snapshot(verts, faces);
}

void AlphaWrapJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool AlphaWrapJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool AlphaWrapJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string AlphaWrapJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

void AlphaWrapJobHandle::clear_error() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->clear_error();
}

AlphaWrapJobHandle start_alpha_wrap_job(
    AlgorithmController& controller,
    const AlphaWrapJobStart& request)
{
    auto* source_mesh = dynamic_cast<easy3d::SurfaceMesh*>(request.source_model);
    auto* source_cloud = dynamic_cast<easy3d::PointCloud*>(request.source_model);
    if (!source_mesh && !source_cloud) {
        LOG(WARNING) << "Alpha Wrap: source model is not a mesh or point cloud";
        return {};
    }

    auto mesh_copy = source_mesh
        ? std::make_unique<easy3d::SurfaceMesh>(*source_mesh)
        : nullptr;
    auto cloud_copy = source_cloud
        ? std::make_unique<easy3d::PointCloud>(*source_cloud)
        : nullptr;

    auto runner = std::make_shared<AlphaWrapRunner>();

    controller.begin(AlgorithmId::AlphaWrap3D,
                     "Alpha Wrapping 3D",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [&controller,
         runner,
         alpha = request.alpha,
         offset = request.offset,
         live_preview = request.live_preview,
         live_surface_interval = request.live_surface_interval,
         source_name = request.source_name,
         wake_ui = request.wake_ui,
         mesh_copy = std::move(mesh_copy),
         cloud_copy = std::move(cloud_copy)]() mutable {
            try {
                std::vector<AW3_Point3d> input_points;
                std::vector<AW3_Triangle> input_triangles;
                const auto* mesh = mesh_copy.get();
                const auto* cloud = cloud_copy.get();
                if (mesh) {
                    surface_mesh_to_triangle_pod(
                        mesh_copy.get(), input_points, input_triangles);
                } else if (cloud) {
                    point_cloud_to_pod(cloud, input_points);
                }

                if (input_points.empty()) {
                    runner->set_error("empty Alpha Wrap input");
                } else {
                    if (!input_triangles.empty())
                        runner->set_input_mesh(input_points, input_triangles);
                    else
                        runner->set_input_cloud(input_points);

                    const std::size_t input_size =
                        input_points.size() + input_triangles.size();
                    AW3_Config cfg{};
                    cfg.alpha = alpha;
                    cfg.offset = offset;
                    cfg.live_preview = live_preview;
                    cfg.live_surface_interval = live_surface_interval;

                    auto tune = [](std::size_t n, int base, int high, int huge) {
                        if (n >= 1000000)
                            return huge;
                        if (n >= 200000)
                            return high;
                        return base;
                    };
                    cfg.gate_event_interval =
                        tune(input_size, 25, 100, 400);
                    cfg.progress_event_interval =
                        tune(input_size, 50, 200, 800);
                    cfg.live_steiner_subsample =
                        tune(input_size, 1, 4, 16);
                    if (live_preview && live_surface_interval > 0) {
                        cfg.live_surface_interval = std::max(
                            live_surface_interval,
                            tune(input_size,
                                 live_surface_interval,
                                 live_surface_interval * 4,
                                 live_surface_interval * 16));
                    }

                    const auto started_at = std::chrono::steady_clock::now();
                    runner->run(cfg);
                    const float elapsed_seconds =
                        std::chrono::duration<float>(
                            std::chrono::steady_clock::now() - started_at)
                            .count();

                    std::vector<AW3_Point3d> result_vertices;
                    std::vector<AW3_Triangle> result_triangles;
                    runner->get_result(result_vertices, result_triangles);

                    if (!runner->is_cancelled() && !result_vertices.empty()) {
                        auto result = triangle_pod_to_surface_mesh(
                            result_vertices,
                            result_triangles,
                            source_name + ".aw3");

                        const easy3d::Model* input_model = mesh
                            ? static_cast<const easy3d::Model*>(mesh)
                            : static_cast<const easy3d::Model*>(cloud);
                        auto quality = evaluate_aw3_result(
                            input_model,
                            result.get(),
                            alpha,
                            offset,
                            elapsed_seconds);
                        controller.set_quality_context(
                            format_aw3_report_for_ai(quality));
                        controller.push_result(std::move(result));
                    }
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Alpha Wrap worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "Alpha Wrap worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));

    return AlphaWrapJobHandle(std::make_shared<AlphaWrapJobHandle::Impl>(runner));
}

} // namespace claw3d::services

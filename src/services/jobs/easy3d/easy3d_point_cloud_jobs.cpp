// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/easy3d/easy3d_point_cloud_jobs.h"

#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"

#include <easy3d/algo/point_cloud_normals.h>
#include <easy3d/algo/point_cloud_simplification.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/util/logging.h>

#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace claw3d::services {

std::size_t apply_point_cloud_grid_simplification(
    easy3d::PointCloud* cloud,
    float radius)
{
    if (!cloud)
        return 0;

    auto to_remove =
        easy3d::PointCloudSimplification::grid_simplification(cloud, radius);
    const std::size_t removed = to_remove.size();
    for (auto v : to_remove)
        cloud->delete_vertex(v);
    cloud->collect_garbage();
    return removed;
}

bool start_point_cloud_normal_estimation_job(
    AlgorithmController& controller,
    const PointCloudNormalEstimationJobStart& request)
{
    if (!request.source_cloud) {
        LOG(WARNING) << "Point Cloud Normal Estimation: missing source cloud";
        return false;
    }

    auto work = std::make_unique<easy3d::PointCloud>(*request.source_cloud);

    controller.begin(AlgorithmId::PointCloudNormalEstimation,
                     "Point Cloud Normal Estimation",
                     request.source_handle,
                     ResultDisposition::ReplaceSource);

    controller.start_worker(std::thread(
        [&controller,
         work = std::move(work),
         k = request.k_neighbors,
         reorient = request.reorient,
         normalize = request.normalize,
         source_name = request.source_name,
         wake_ui = request.wake_ui]() mutable {
            try {
                easy3d::PointCloudNormals::estimate(work.get(), k);
                if (reorient)
                    easy3d::PointCloudNormals::reorient(work.get(), k);
                if (normalize) {
                    auto normals =
                        work->get_vertex_property<easy3d::vec3>("v:normal");
                    if (normals) {
                        for (auto v : work->vertices())
                            normals[v] = easy3d::normalize(normals[v]);
                    }
                }
                work->set_name(source_name);
                controller.push_result(std::move(work));
            } catch (const std::exception& e) {
                LOG(ERROR) << "Point Cloud Normal Estimation failed: "
                           << e.what();
            } catch (...) {
                LOG(ERROR) << "Point Cloud Normal Estimation failed: unknown error";
            }

            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));

    return true;
}

} // namespace claw3d::services

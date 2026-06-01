// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/easy3d/poisson_reconstruction_job.h"

#include "mesh_quality.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"

#include <easy3d/algo/point_cloud_poisson_reconstruction.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/util/logging.h>

#include <chrono>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace claw3d::services {

bool start_poisson_reconstruction_job(
    AlgorithmController& controller,
    const PoissonReconstructionJobStart& request)
{
    if (!request.source_cloud) {
        LOG(WARNING) << "Poisson Reconstruction: missing source cloud";
        return false;
    }

    auto input = std::make_unique<easy3d::PointCloud>(*request.source_cloud);

    controller.begin(AlgorithmId::PoissonReconstruction,
                     "Poisson Reconstruction",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [&controller,
         input = std::move(input),
         depth = request.depth,
         samples_per_node = request.samples_per_node,
         cg_depth = request.cg_depth,
         scale = request.scale,
         source_name = request.source_name,
         wake_ui = request.wake_ui]() mutable {
            try {
                const auto started_at = std::chrono::steady_clock::now();
                easy3d::PoissonReconstruction poisson;
                poisson.set_depth(depth);
                poisson.set_samples_per_node(samples_per_node);
                poisson.set_cg_depth(cg_depth);
                poisson.set_scale(scale);

                auto* mesh = poisson.apply(input.get());
                const float elapsed =
                    std::chrono::duration<float>(
                        std::chrono::steady_clock::now() - started_at)
                        .count();

                if (mesh) {
                    mesh->set_name(source_name + ".poisson");
                    auto report =
                        evaluate_poisson_result(input.get(), mesh, elapsed);
                    controller.set_quality_context(
                        format_report_for_ai(
                            report, depth, samples_per_node));
                    controller.push_owned_result(mesh);
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Poisson Reconstruction failed: " << e.what();
            } catch (...) {
                LOG(ERROR) << "Poisson Reconstruction failed: unknown error";
            }

            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));

    return true;
}

} // namespace claw3d::services

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/acvd_remeshing_job.h"
#include "services/core/job_handle_detail.h"
#include "acvd_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/util/logging.h>

#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

const char* acvd_mode_slug(int mode) {
    switch (mode) {
    case ACVD_MODE_Uniform:
        return "uniform";
    case ACVD_MODE_UniformQemPostprocess:
        return "qem_post";
    case ACVD_MODE_QemEnergy:
        return "qem_energy";
    case ACVD_MODE_AdaptiveCurvature:
        return "adaptive";
    default:
        return "unknown";
    }
}

std::string acvd_result_name(const std::string& source_name,
                             const ACVD_Config& cfg)
{
    return source_name + "_acvd_" + acvd_mode_slug(cfg.mode) + "_" +
        std::to_string(cfg.target_vertices);
}

} // namespace

struct AcvdRemeshingJobHandle::Impl {
    explicit Impl(std::shared_ptr<ACVDRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<ACVDRunner> runner;
};

AcvdRemeshingJobHandle::AcvdRemeshingJobHandle(
    std::shared_ptr<AcvdRemeshingJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool AcvdRemeshingJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void AcvdRemeshingJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void AcvdRemeshingJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool AcvdRemeshingJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool AcvdRemeshingJobHandle::cancelled_or_failed() const
{
    auto runner = detail::runner_from(impl_);
    return !runner || runner->is_cancelled() || runner->has_error();
}

bool AcvdRemeshingJobHandle::done_and_ready(
    const std::atomic<bool>& ready) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done() &&
           ready.load(std::memory_order_acquire);
}

void AcvdRemeshingJobHandle::copy_error_if_any(std::string& error) const
{
    auto runner = detail::runner_from(impl_);
    if (runner && runner->has_error())
        error = runner->last_error();
}

ACVD_DebugStats AcvdRemeshingJobHandle::debug_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->debug_stats() : ACVD_DebugStats{};
}

void AcvdRemeshingJobHandle::get_seed_positions(
    std::vector<ACVD_Point3d>& out) const
{
    out.clear();
    if (auto runner = detail::runner_from(impl_))
        runner->get_seed_positions(out);
}

bool AcvdRemeshingJobHandle::poll_cluster_snapshot(
    int& last_generation,
    std::vector<ACVD_Point3d>& verts,
    std::vector<ACVD_Triangle>& tris,
    std::vector<int>& face_cluster_ids,
    std::vector<ACVD_Point3d>& cluster_centers) const
{
    auto runner = detail::runner_from(impl_);
    return runner &&
           runner->poll_cluster_snapshot(last_generation,
                                         verts,
                                         tris,
                                         face_cluster_ids,
                                         cluster_centers);
}

AcvdRemeshingJobHandle start_acvd_remeshing_job(
    AlgorithmController& controller,
    const AcvdRemeshingJobStart& request)
{
    std::vector<ACVD_Point3d> input_vertices;
    std::vector<ACVD_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "ACVD remeshing: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "ACVD remeshing: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<ACVDRunner>();
    runner->set_input(input_vertices, input_triangles);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::AcvdRemeshing,
                     "ACVD Remeshing",
                     request.source_handle,
                     ResultDisposition::HideSourceAndAddChild,
                     AlgorithmCompletionPolicy::final_preview_hold());

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         source_name = request.source_name,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);

                std::vector<ACVD_Point3d> out_vertices;
                std::vector<ACVD_Triangle> out_triangles;
                runner->get_result(out_vertices, out_triangles);

                if (!out_triangles.empty() &&
                    !runner->is_cancelled() &&
                    !runner->has_error()) {
                    auto result = triangle_pod_to_surface_mesh(
                        out_vertices,
                        out_triangles,
                        acvd_result_name(source_name, cfg));
                    controller.push_result(std::move(result));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "ACVD worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "ACVD worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            controller.mark_worker_finished();
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return AcvdRemeshingJobHandle(std::make_shared<AcvdRemeshingJobHandle::Impl>(runner));
}

} // namespace claw3d::services

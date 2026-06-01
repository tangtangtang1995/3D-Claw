// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/vsa_approximation_job.h"
#include "services/core/job_handle_detail.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"
#include "vsa_runner.h"

#include <easy3d/util/logging.h>

#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

const char* vsa_metric_slug(int metric) {
    switch (metric) {
    case VSA_METRIC_L2:
        return "l2";
    case VSA_METRIC_L21:
    default:
        return "l21";
    }
}

std::string vsa_result_name(const std::string& source_name,
                            const VSA_Config& cfg,
                            const VSA_DebugStats& stats)
{
    std::string name = source_name + ".vsa-" +
        vsa_metric_slug(cfg.metric) + "-" +
        std::to_string(cfg.target_proxies) + "p";
    if (!stats.manifold_output)
        name += "-nm";
    return name;
}

} // namespace

struct VsaApproximationJobHandle::Impl {
    explicit Impl(std::shared_ptr<VSARunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<VSARunner> runner;
};

VsaApproximationJobHandle::VsaApproximationJobHandle(
    std::shared_ptr<VsaApproximationJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool VsaApproximationJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void VsaApproximationJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void VsaApproximationJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool VsaApproximationJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool VsaApproximationJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool VsaApproximationJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string VsaApproximationJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool VsaApproximationJobHandle::cancelled_or_failed() const
{
    auto runner = detail::runner_from(impl_);
    return !runner || runner->is_cancelled() || runner->has_error();
}

bool VsaApproximationJobHandle::done_and_ready(
    const std::atomic<bool>& ready) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done() &&
           ready.load(std::memory_order_acquire);
}

void VsaApproximationJobHandle::copy_error_if_any(
    std::string& error) const
{
    auto runner = detail::runner_from(impl_);
    if (runner && runner->has_error())
        error = runner->last_error();
}

bool VsaApproximationJobHandle::poll_snapshot(
    int& last_generation,
    VSA_Snapshot& out) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

VSA_DebugStats VsaApproximationJobHandle::debug_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->debug_stats() : VSA_DebugStats{};
}

VsaApproximationJobHandle start_vsa_approximation_job(
    AlgorithmController& controller,
    const VsaApproximationJobStart& request)
{
    std::vector<VSA_Point3d> input_vertices;
    std::vector<VSA_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "VSA approximation: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "VSA approximation: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<VSARunner>();
    runner->set_input(input_vertices, input_triangles);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::VsaApproximation,
                     "VSA Approximation",
                     request.source_handle,
                     ResultDisposition::HideSourceAndAddChild);

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         source_name = request.source_name,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);

                std::vector<VSA_Point3d> out_vertices;
                std::vector<VSA_Triangle> out_triangles;
                runner->get_result(out_vertices, out_triangles);
                const VSA_DebugStats final_stats = runner->debug_stats();

                const bool have_geometry =
                    cfg.extract_mesh && !out_triangles.empty() &&
                    !runner->is_cancelled() &&
                    !runner->has_error();
                if (have_geometry) {
                    auto result = triangle_pod_to_surface_mesh(
                        out_vertices,
                        out_triangles,
                        vsa_result_name(source_name, cfg, final_stats),
                        !final_stats.manifold_output);
                    controller.push_result(std::move(result));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "VSA worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "VSA worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return VsaApproximationJobHandle(std::make_shared<VsaApproximationJobHandle::Impl>(runner));
}

} // namespace claw3d::services

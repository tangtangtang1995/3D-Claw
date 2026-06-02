// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/cgal_simplification_job.h"
#include "services/core/job_handle_detail.h"
#include "simplification_runner.h"
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

const char* simplification_strategy_slug(int strategy) {
    switch (strategy) {
    case SIMPL_STRAT_GarlandHeckbertPlane:
        return "gh";
    case SIMPL_STRAT_EdgeLengthMidpoint:
        return "el";
    case SIMPL_STRAT_GarlandHeckbertTriangle:
        return "ght";
    case SIMPL_STRAT_GarlandHeckbertProbabilisticPlane:
        return "ghpp";
    case SIMPL_STRAT_GarlandHeckbertProbabilisticTriangle:
        return "ghpt";
    case SIMPL_STRAT_LindstromTurk:
    default:
        return "lt";
    }
}

std::string simplification_result_name(const std::string& source_name,
                                       const SIMPL_Config& cfg)
{
    return source_name + ".simpl-" + simplification_strategy_slug(cfg.strategy);
}

} // namespace

struct CgalSimplificationJobHandle::Impl {
    explicit Impl(std::shared_ptr<SimplificationRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<SimplificationRunner> runner;
};

CgalSimplificationJobHandle::CgalSimplificationJobHandle(
    std::shared_ptr<CgalSimplificationJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool CgalSimplificationJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void CgalSimplificationJobHandle::reset()
{
    detail::reset_handle(impl_);
}

bool CgalSimplificationJobHandle::drain_live_events(
    std::vector<SIMPL_FrameEvent>& out_events) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->drain_live_events(out_events);
}

bool CgalSimplificationJobHandle::poll_snapshot(
    int& last_generation,
    std::vector<SIMPL_Point3d>& out_verts,
    std::vector<SIMPL_Triangle>& out_tris) const
{
    auto runner = detail::runner_from(impl_);
    return runner &&
           runner->poll_snapshot(last_generation, out_verts, out_tris);
}

void CgalSimplificationJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool CgalSimplificationJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool CgalSimplificationJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

float CgalSimplificationJobHandle::progress() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->progress() : 0.0f;
}

bool CgalSimplificationJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string CgalSimplificationJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool CgalSimplificationJobHandle::cancelled_or_failed() const
{
    auto runner = detail::runner_from(impl_);
    return !runner || runner->is_cancelled() || runner->has_error();
}

bool CgalSimplificationJobHandle::done_and_ready(
    const std::atomic<bool>& ready) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done() &&
           ready.load(std::memory_order_acquire);
}

void CgalSimplificationJobHandle::copy_error_if_any(
    std::string& error) const
{
    auto runner = detail::runner_from(impl_);
    if (runner && runner->has_error())
        error = runner->last_error();
}

SIMPL_DebugStats CgalSimplificationJobHandle::debug_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->debug_stats() : SIMPL_DebugStats{};
}

CgalSimplificationJobHandle start_cgal_simplification_job(
    AlgorithmController& controller,
    const CgalSimplificationJobStart& request)
{
    std::vector<SIMPL_Point3d> input_vertices;
    std::vector<SIMPL_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "CGAL simplification: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "CGAL simplification: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<SimplificationRunner>();
    runner->set_input(input_vertices, input_triangles);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::CgalSimplification,
                     "CGAL Simplification",
                     request.source_handle,
                     ResultDisposition::HideSourceAndAddChild,
                     AlgorithmCompletionPolicy::preview_flush());

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         source_name = request.source_name,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);

                std::vector<SIMPL_Point3d> out_vertices;
                std::vector<SIMPL_Triangle> out_triangles;
                runner->get_result(out_vertices, out_triangles);

                if (!out_triangles.empty() &&
                    !runner->is_cancelled() &&
                    !runner->has_error()) {
                    auto result = triangle_pod_to_surface_mesh(
                        out_vertices,
                        out_triangles,
                        simplification_result_name(source_name, cfg));
                    controller.push_result(std::move(result));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Simplification worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "Simplification worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            controller.mark_worker_finished();
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return CgalSimplificationJobHandle(std::make_shared<CgalSimplificationJobHandle::Impl>(runner));
}

} // namespace claw3d::services

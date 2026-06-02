// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/parameterization_job.h"
#include "services/core/job_handle_detail.h"
#include "parameterization_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/util/logging.h>

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

void invoke_parameterization_wake(void* user_data) {
    auto* wake = static_cast<std::function<void()>*>(user_data);
    if (wake && *wake)
        (*wake)();
}

} // namespace

struct ParameterizationJobHandle::Impl {
    explicit Impl(std::shared_ptr<ParameterizationRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<ParameterizationRunner> runner;
};

ParameterizationJobHandle::ParameterizationJobHandle(
    std::shared_ptr<ParameterizationJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool ParameterizationJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void ParameterizationJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void ParameterizationJobHandle::cancel() const
{
    if (auto runner =
            detail::runner_from(impl_))
        runner->cancel();
}

bool ParameterizationJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool ParameterizationJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool ParameterizationJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string ParameterizationJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

void ParameterizationJobHandle::get_result(PARAM_Result& out) const
{
    out = PARAM_Result{};
    if (auto runner =
            detail::runner_from(impl_))
        runner->get_result(out);
}

ParameterizationJobHandle start_parameterization_job(
    AlgorithmController& controller,
    const ParameterizationJobStart& request)
{
    std::vector<PARAM_Point3d> input_vertices;
    std::vector<PARAM_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "Parameterization: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "Parameterization: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<ParameterizationRunner>();
    runner->set_input(input_vertices, input_triangles);
    runner->set_config(request.config);
    if (!request.seam_paths.empty())
        runner->set_seams(request.seam_paths);
    else
        runner->clear_seams();

    auto wake_holder =
        std::make_shared<std::function<void()>>(request.wake_ui);
    runner->set_wake_callback(invoke_parameterization_wake,
                              wake_holder.get());

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::Parameterization,
                     "Parameterization",
                     request.source_handle,
                     ResultDisposition::AddAsChild,
                     AlgorithmCompletionPolicy::preview_flush());

    controller.start_worker(std::thread(
        [&controller,
         runner,
         final_result_ready = request.final_result_ready,
         wake_holder]() {
            try {
                runner->run();
            } catch (const std::exception& e) {
                runner->fail_with_exception(e.what());
            } catch (...) {
                runner->fail_with_exception(
                    "unknown parameterization worker exception");
            }

            controller.mark_worker_finished();
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (*wake_holder)
                (*wake_holder)();
        }));

    return ParameterizationJobHandle(std::make_shared<ParameterizationJobHandle::Impl>(runner));
}

} // namespace claw3d::services

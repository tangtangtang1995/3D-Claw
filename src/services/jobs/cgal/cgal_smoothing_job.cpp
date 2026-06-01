// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/cgal_smoothing_job.h"
#include "services/core/job_handle_detail.h"
#include "smoothing_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/util/logging.h>

#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

const char* smoothing_mode_slug(int mode) {
    switch (mode) {
    case SMOOTH_MODE_TangentialRelaxation:
        return "tangent";
    case SMOOTH_MODE_AngleSmoothing:
        return "angle";
    case SMOOTH_MODE_MeanCurvatureFlow:
        return "mcf";
    case SMOOTH_MODE_AngleArea:
        return "anglearea";
    default:
        return "unknown";
    }
}

std::string strip_mesh_extension(std::string name) {
    for (const char* ext : {".off", ".obj", ".ply", ".stl",
                            ".OFF", ".OBJ", ".PLY", ".STL"}) {
        const std::size_t len = std::strlen(ext);
        if (name.size() > len &&
            name.compare(name.size() - len, len, ext) == 0) {
            name.resize(name.size() - len);
            break;
        }
    }
    return name;
}

std::string smoothing_result_name(const std::string& source_name,
                                  const SMOOTH_Config& cfg)
{
    return strip_mesh_extension(source_name) + ".smooth-" +
        smoothing_mode_slug(cfg.mode) + "-" +
        std::to_string(cfg.iterations) + "i";
}

} // namespace

struct CgalSmoothingJobHandle::Impl {
    explicit Impl(std::shared_ptr<SmoothingRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<SmoothingRunner> runner;
};

CgalSmoothingJobHandle::CgalSmoothingJobHandle(
    std::shared_ptr<CgalSmoothingJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool CgalSmoothingJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void CgalSmoothingJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void CgalSmoothingJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool CgalSmoothingJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool CgalSmoothingJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool CgalSmoothingJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string CgalSmoothingJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool CgalSmoothingJobHandle::cancelled_or_failed() const
{
    auto runner = detail::runner_from(impl_);
    return !runner || runner->is_cancelled() || runner->has_error();
}

bool CgalSmoothingJobHandle::done_and_ready(
    const std::atomic<bool>& ready) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done() &&
           ready.load(std::memory_order_acquire);
}

void CgalSmoothingJobHandle::copy_error_if_any(std::string& error) const
{
    auto runner = detail::runner_from(impl_);
    if (runner && runner->has_error())
        error = runner->last_error();
}

bool CgalSmoothingJobHandle::poll_snapshot(
    int& last_generation,
    SMOOTH_Snapshot& out) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

SMOOTH_ResultStats CgalSmoothingJobHandle::result_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->result_stats() : SMOOTH_ResultStats{};
}

CgalSmoothingJobHandle start_cgal_smoothing_job(
    AlgorithmController& controller,
    const CgalSmoothingJobStart& request)
{
    std::vector<SMOOTH_Point3d> input_vertices;
    std::vector<SMOOTH_Triangle> input_triangles;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles)) {
        LOG(WARNING) << "CGAL smoothing: empty or invalid triangle input";
        return {};
    }

    auto runner = std::make_shared<SmoothingRunner>();
    runner->set_input(input_vertices, input_triangles);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::CgalSmoothing,
                     "CGAL Mesh Smoothing",
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

                std::vector<SMOOTH_Point3d> out_vertices;
                std::vector<SMOOTH_Triangle> out_triangles;
                runner->get_result(out_vertices, out_triangles);

                if (!out_triangles.empty() &&
                    !runner->is_cancelled() &&
                    !runner->has_error()) {
                    auto result = triangle_pod_to_surface_mesh(
                        out_vertices,
                        out_triangles,
                        smoothing_result_name(source_name, cfg));
                    controller.push_result(std::move(result));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Smoothing worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "Smoothing worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return CgalSmoothingJobHandle(std::make_shared<CgalSmoothingJobHandle::Impl>(runner));
}

} // namespace claw3d::services

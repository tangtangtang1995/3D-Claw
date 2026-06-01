// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/arap_deformation_job.h"
#include "services/core/job_handle_detail.h"
#include "arap_deformation_runner.h"
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

ARAP_ControlTransform with_rotation_center(
    ARAP_ControlTransform transform,
    const ARAP_Selection& selection,
    const std::vector<ARAP_Point3d>& vertices)
{
    const auto& source = !selection.roi_vertices.empty()
        ? selection.roi_vertices
        : selection.control_vertices;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int count = 0;
    for (int id : source) {
        if (id < 0 || id >= static_cast<int>(vertices.size()))
            continue;
        const auto& p = vertices[id];
        x += p.x;
        y += p.y;
        z += p.z;
        ++count;
    }
    if (count > 0) {
        transform.rot_cx = x / count;
        transform.rot_cy = y / count;
        transform.rot_cz = z / count;
    }
    return transform;
}

} // namespace

struct ArapDeformationJobHandle::Impl {
    explicit Impl(std::shared_ptr<ARAPDeformationRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<ARAPDeformationRunner> runner;
};

ArapDeformationJobHandle::ArapDeformationJobHandle(
    std::shared_ptr<ArapDeformationJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool ArapDeformationJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void ArapDeformationJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void ArapDeformationJobHandle::cancel() const
{
    if (auto runner =
            detail::runner_from(impl_))
        runner->cancel();
}

bool ArapDeformationJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool ArapDeformationJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool ArapDeformationJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string ArapDeformationJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool ArapDeformationJobHandle::poll_snapshot(
    int& last_generation,
    ARAP_Snapshot& out) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

void ArapDeformationJobHandle::get_result(ARAP_Result& out) const
{
    out = ARAP_Result{};
    if (auto runner =
            detail::runner_from(impl_))
        runner->get_result(out);
}

ArapDeformationJobHandle start_arap_deformation_job(
    AlgorithmController& controller,
    const ArapDeformationJobStart& request)
{
    std::vector<ARAP_Point3d> input_vertices;
    std::vector<ARAP_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "ARAP deformation: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "ARAP deformation: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<ARAPDeformationRunner>();
    runner->set_input(input_vertices, input_triangles);
    runner->set_selection(request.selection);
    runner->set_control_transforms({
        with_rotation_center(request.control_transform,
                             request.selection,
                             input_vertices)
    });

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::ArapDeformation,
                     "ARAP Deformation",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         source_name = request.source_name,
         input_triangles,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);

                ARAP_Result result;
                runner->get_result(result);
                if (result.error_code == ARAP_ERR_None &&
                    !runner->is_cancelled() &&
                    !result.vertices.empty()) {
                    auto mesh = triangle_pod_to_surface_mesh(
                        result.vertices,
                        input_triangles,
                        strip_mesh_extension(source_name) + ".arap");
                    controller.push_result(std::move(mesh));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "ARAP worker exception: " << e.what();
                runner->set_error(ARAP_ERR_AlgorithmException, e.what());
            } catch (...) {
                LOG(ERROR) << "ARAP worker unknown exception";
                runner->set_error(ARAP_ERR_AlgorithmException,
                                  "unknown worker exception");
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return ArapDeformationJobHandle(std::make_shared<ArapDeformationJobHandle::Impl>(runner));
}

} // namespace claw3d::services

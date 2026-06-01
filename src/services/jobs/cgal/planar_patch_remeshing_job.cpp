// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/planar_patch_remeshing_job.h"
#include "services/core/job_handle_detail.h"
#include "planar_patch_remeshing_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

const char* kPprPatchProperty = "f:patch_id";
const char* kPprOwnPatchProperty = "f:ppr_patch_id";

const char* ppr_mode_slug(int mode) {
    switch (mode) {
    case PPR_ExactPlanar:
        return "planar";
    case PPR_ExistingLabels:
        return "labels";
    case PPR_AlmostPlanar:
    default:
        return "almost";
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

void write_patch_properties(easy3d::SurfaceMesh* mesh,
                            const std::vector<int>& patch_ids)
{
    if (!mesh || patch_ids.empty())
        return;

    auto patch = mesh->get_face_property<int>(kPprPatchProperty);
    if (!patch)
        patch = mesh->add_face_property<int>(kPprPatchProperty, -1);
    auto ppr_patch = mesh->get_face_property<int>(kPprOwnPatchProperty);
    if (!ppr_patch)
        ppr_patch = mesh->add_face_property<int>(kPprOwnPatchProperty, -1);

    int i = 0;
    for (auto f : mesh->faces()) {
        if (i >= static_cast<int>(patch_ids.size()))
            break;
        patch[f] = patch_ids[i];
        ppr_patch[f] = patch_ids[i];
        ++i;
    }
}

} // namespace

struct PlanarPatchRemeshingJobHandle::Impl {
    explicit Impl(std::shared_ptr<PlanarPatchRemeshingRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<PlanarPatchRemeshingRunner> runner;
};

PlanarPatchRemeshingJobHandle::PlanarPatchRemeshingJobHandle(
    std::shared_ptr<PlanarPatchRemeshingJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool PlanarPatchRemeshingJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void PlanarPatchRemeshingJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void PlanarPatchRemeshingJobHandle::cancel() const
{
    if (auto runner =
            detail::runner_from(impl_))
        runner->cancel();
}

bool PlanarPatchRemeshingJobHandle::is_cancelled() const
{
    auto runner =
        detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool PlanarPatchRemeshingJobHandle::is_done() const
{
    auto runner =
        detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool PlanarPatchRemeshingJobHandle::has_error() const
{
    auto runner =
        detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string PlanarPatchRemeshingJobHandle::last_error() const
{
    auto runner =
        detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool PlanarPatchRemeshingJobHandle::cancelled_or_failed() const
{
    auto runner =
        detail::runner_from(impl_);
    return !runner || runner->is_cancelled() || runner->has_error();
}

void PlanarPatchRemeshingJobHandle::copy_error_if_any(
    std::string& error) const
{
    auto runner =
        detail::runner_from(impl_);
    if (runner && runner->has_error())
        error = runner->last_error();
}

bool PlanarPatchRemeshingJobHandle::poll_snapshot(
    int& last_generation,
    PPR_Snapshot& out) const
{
    auto runner =
        detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

bool PlanarPatchRemeshingJobHandle::has_pending_snapshot(
    int last_generation) const
{
    auto runner =
        detail::runner_from(impl_);
    return runner && runner->has_pending_snapshot(last_generation);
}

PPR_DebugStats PlanarPatchRemeshingJobHandle::debug_stats() const
{
    auto runner =
        detail::runner_from(impl_);
    return runner ? runner->debug_stats() : PPR_DebugStats{};
}

PlanarPatchRemeshingJobHandle start_planar_patch_remeshing_job(
    AlgorithmController& controller,
    const PlanarPatchRemeshingJobStart& request)
{
    std::vector<PPR_Point3d> input_vertices;
    std::vector<PPR_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "Planar Patch Remeshing: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "Planar Patch Remeshing: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<PlanarPatchRemeshingRunner>();
    runner->set_input(input_vertices, input_triangles);
    if (request.config.mode == PPR_ExistingLabels)
        runner->set_face_patch_ids(request.existing_face_patch_ids);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::PlanarPatchRemeshing,
                     "Planar Patch Remeshing",
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

                std::vector<PPR_Point3d> vertices;
                std::vector<PPR_Triangle> triangles;
                std::vector<int> patch_ids;
                runner->get_result(vertices, triangles, patch_ids);
                if (!vertices.empty() &&
                    !triangles.empty() &&
                    !runner->is_cancelled() &&
                    !runner->has_error()) {
                    const PPR_DebugStats stats = runner->debug_stats();
                    const std::string name = strip_mesh_extension(source_name) +
                        ".ppr-" + ppr_mode_slug(cfg.mode) + "-" +
                        std::to_string(stats.patches) + "p";
                    auto result = triangle_pod_to_surface_mesh(
                        vertices, triangles, name);
                    write_patch_properties(result.get(), patch_ids);
                    controller.push_result(std::move(result));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Planar Patch Remeshing worker exception: "
                           << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "Planar Patch Remeshing worker unknown exception";
                runner->set_error("unknown worker exception");
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return PlanarPatchRemeshingJobHandle(std::make_shared<PlanarPatchRemeshingJobHandle::Impl>(runner));
}

} // namespace claw3d::services

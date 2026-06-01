// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/mcf_skeletonization_job.h"
#include "services/core/job_handle_detail.h"
#include "mcf_skeletonization_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "services/core/algorithm_mesh_bridge.h"

#include <easy3d/algo/surface_mesh_components.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

std::unique_ptr<easy3d::Graph> skeleton_to_easy3d_graph(
    const std::vector<MCF_Point3d>& vertices,
    const std::vector<MCF_Line>& edges,
    const std::string& name)
{
    auto graph = std::make_unique<easy3d::Graph>();
    graph->set_name(name);
    (void)vertices;
    for (const auto& e : edges) {
        auto a = graph->add_vertex(easy3d::vec3(
            static_cast<float>(e.a.x),
            static_cast<float>(e.a.y),
            static_cast<float>(e.a.z)));
        auto b = graph->add_vertex(easy3d::vec3(
            static_cast<float>(e.b.x),
            static_cast<float>(e.b.y),
            static_cast<float>(e.b.z)));
        if (a != b)
            graph->add_edge(a, b);
    }
    return graph;
}

} // namespace

bool validate_mcf_skeletonization_input(easy3d::SurfaceMesh* mesh,
                                        std::string& reason)
{
    if (!mesh) {
        reason = "no active SurfaceMesh";
        return false;
    }
    if (mesh->n_vertices() == 0 || mesh->n_faces() == 0) {
        reason = "mesh is empty";
        return false;
    }
    if (!mesh->is_triangle_mesh()) {
        reason = "mesh is not pure triangle (please triangulate first)";
        return false;
    }

    int border_count = 0;
    for (auto e : mesh->edges()) {
        if (mesh->is_border(e))
            ++border_count;
    }
    if (border_count > 0) {
        reason = "mesh is not closed (" +
            std::to_string(border_count) + " border edges)";
        return false;
    }

    const std::size_t component_count =
        easy3d::SurfaceMeshComponent::extract(mesh, false).size();
    if (component_count != 1) {
        reason = "mesh has " + std::to_string(component_count) +
            " connected components (MCF requires 1)";
        return false;
    }
    return true;
}

struct McfSkeletonizationJobHandle::Impl {
    explicit Impl(std::shared_ptr<MCFSkeletonizationRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<MCFSkeletonizationRunner> runner;
};

McfSkeletonizationJobHandle::McfSkeletonizationJobHandle(
    std::shared_ptr<McfSkeletonizationJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool McfSkeletonizationJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void McfSkeletonizationJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void McfSkeletonizationJobHandle::cancel() const
{
    if (auto runner =
            detail::runner_from(impl_))
        runner->cancel();
}

bool McfSkeletonizationJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool McfSkeletonizationJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool McfSkeletonizationJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string McfSkeletonizationJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool McfSkeletonizationJobHandle::poll_snapshot(
    int& last_generation,
    MCF_Snapshot& out) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

void McfSkeletonizationJobHandle::get_result(MCF_Result& out) const
{
    out = MCF_Result{};
    if (auto runner =
            detail::runner_from(impl_))
        runner->get_result(out);
}

MCF_Metrics McfSkeletonizationJobHandle::result_metrics() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->result_metrics() : MCF_Metrics{};
}

McfSkeletonizationJobHandle start_mcf_skeletonization_job(
    AlgorithmController& controller,
    const McfSkeletonizationJobStart& request)
{
    std::vector<MCF_Point3d> input_vertices;
    std::vector<MCF_Triangle> input_triangles;
    int dropped_non_triangles = 0;
    if (!surface_mesh_to_triangle_pod(request.source_mesh,
                                      input_vertices,
                                      input_triangles,
                                      &dropped_non_triangles)) {
        LOG(WARNING) << "MCF skeletonization: empty or invalid triangle input";
        return {};
    }
    if (dropped_non_triangles > 0) {
        LOG(WARNING) << "MCF skeletonization: dropped "
                     << dropped_non_triangles << " non-triangle faces";
    }

    auto runner = std::make_shared<MCFSkeletonizationRunner>();
    runner->set_input(input_vertices, input_triangles);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::MeanCurvatureFlowSkeleton,
                     "MCF Skeletonization",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         result_base_name = request.result_base_name,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);

                MCF_Result result;
                runner->get_result(result);
                if (result.error_code == MCF_ERR_None &&
                    !result.metrics.cancelled &&
                    !result.skeleton_vertices.empty() &&
                    !result.skeleton_edges.empty()) {
                    controller.push_result(skeleton_to_easy3d_graph(
                        result.skeleton_vertices,
                        result.skeleton_edges,
                        result_base_name + ".mcf-skeleton"));
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "[MCF] worker exception: " << e.what();
            } catch (...) {
                LOG(ERROR) << "[MCF] worker unknown exception";
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return McfSkeletonizationJobHandle(std::make_shared<McfSkeletonizationJobHandle::Impl>(runner));
}

} // namespace claw3d::services

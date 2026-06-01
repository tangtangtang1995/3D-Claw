// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/region_growing_job.h"
#include "services/core/job_handle_detail.h"
#include "region_growing_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "common/primitive_preview_policy.h"
#include "ransac_runner.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

struct PointCloudPod {
    std::vector<RG_Point3d> points;
    std::vector<RG_Vector3d> normals;
    float bbox_diag = 1.0f;
};

easy3d::vec3 primitive_color(int shape_id)
{
    auto h = [](unsigned x) {
        x = (x ^ 61u) ^ (x >> 16);
        x = x + (x << 3);
        x = x ^ (x >> 4);
        x = x * 0x27d4eb2du;
        x = x ^ (x >> 15);
        return x;
    };
    unsigned r = h((unsigned)shape_id * 3u + 1u);
    unsigned g = h((unsigned)shape_id * 3u + 2u);
    unsigned b = h((unsigned)shape_id * 3u + 3u);
    return easy3d::vec3(
        ((r % 151u) + 50u) / 255.0f,
        ((g % 151u) + 50u) / 255.0f,
        ((b % 151u) + 50u) / 255.0f);
}

bool point_cloud_to_region_growing_pod(easy3d::PointCloud* cloud,
                                       PointCloudPod& pod)
{
    pod = PointCloudPod{};
    if (!cloud)
        return false;

    auto cloud_points =
        cloud->get_vertex_property<easy3d::vec3>("v:point");
    auto cloud_normals =
        cloud->get_vertex_property<easy3d::vec3>("v:normal");
    if (!cloud_points || !cloud_normals)
        return false;

    const auto& pts = cloud_points.vector();
    const auto& nms = cloud_normals.vector();
    const std::size_t n = std::min(pts.size(), nms.size());
    if (n == 0)
        return false;

    pod.points.resize(n);
    pod.normals.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        pod.points[i] = {pts[i].x, pts[i].y, pts[i].z};
        pod.normals[i] = {nms[i].x, nms[i].y, nms[i].z};
    }

    const auto& bbox = cloud->bounding_box();
    pod.bbox_diag = bbox.is_valid() ? bbox.diagonal_length() : 1.0f;
    return true;
}

void apply_region_properties(easy3d::SurfaceMesh* mesh, int region_id)
{
    if (!mesh)
        return;

    auto patch_id = mesh->add_face_property<int>("f:patch_id", region_id);
    auto region_prop = mesh->add_face_property<int>("f:region_id", region_id);
    for (auto f : mesh->faces()) {
        patch_id[f] = region_id;
        region_prop[f] = region_id;
    }
}

std::unique_ptr<easy3d::SurfaceMesh> make_region_patch_mesh(
    int region_id,
    const RG_RegionResult& result,
    const std::vector<easy3d::vec3>& points)
{
    if (points.size() < 3)
        return nullptr;

    easy3d::vec3 normal(
        (float)result.plane[0],
        (float)result.plane[1],
        (float)result.plane[2]);
    if (dot(normal, normal) < 1e-20f)
        return nullptr;
    normal = normalize(normal);

    easy3d::vec3 t1 = (std::abs(normal.x) < 0.9f)
        ? normalize(cross(normal, easy3d::vec3(1, 0, 0)))
        : normalize(cross(normal, easy3d::vec3(0, 1, 0)));
    easy3d::vec3 t2 = normalize(cross(normal, t1));

    easy3d::vec3 centroid(0, 0, 0);
    for (const auto& p : points)
        centroid += p;
    centroid /= (float)points.size();

    float min_u = std::numeric_limits<float>::max();
    float max_u = -std::numeric_limits<float>::max();
    float min_v = min_u;
    float max_v = max_u;
    for (const auto& p : points) {
        easy3d::vec3 delta = p - centroid;
        const float u = dot(delta, t1);
        const float v = dot(delta, t2);
        min_u = std::min(min_u, u);
        max_u = std::max(max_u, u);
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }

    const float pad = std::max(max_u - min_u, max_v - min_v) * 0.05f;
    min_u -= pad;
    max_u += pad;
    min_v -= pad;
    max_v += pad;

    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    char name[64];
    std::snprintf(name, sizeof(name), "rg_region_%03d", region_id);
    mesh->set_name(name);

    easy3d::vec3 corners[4] = {
        centroid + t1 * min_u + t2 * min_v,
        centroid + t1 * max_u + t2 * min_v,
        centroid + t1 * max_u + t2 * max_v,
        centroid + t1 * min_u + t2 * max_v
    };
    for (int i = 0; i < 4; ++i)
        mesh->add_vertex(corners[i]);
    mesh->add_triangle(
        easy3d::SurfaceMesh::Vertex(0),
        easy3d::SurfaceMesh::Vertex(1),
        easy3d::SurfaceMesh::Vertex(2));
    mesh->add_triangle(
        easy3d::SurfaceMesh::Vertex(0),
        easy3d::SurfaceMesh::Vertex(2),
        easy3d::SurfaceMesh::Vertex(3));

    const auto color = primitive_color(region_id);
    auto face_color = mesh->add_face_property<easy3d::vec3>(
        "f:color", color);
    for (auto f : mesh->faces())
        face_color[f] = color;
    apply_region_properties(mesh.get(), region_id);
    return mesh;
}

std::unique_ptr<easy3d::SurfaceMesh> make_region_convex_hull_mesh(
    int region_id,
    const std::vector<RANSAC_Point3d>& vertices)
{
    if (vertices.size() < 3)
        return nullptr;

    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    char name[64];
    std::snprintf(name, sizeof(name), "rg_region_%03d.ch", region_id);
    mesh->set_name(name);

    for (const auto& v : vertices)
        mesh->add_vertex(easy3d::vec3((float)v.x, (float)v.y, (float)v.z));
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
        mesh->add_triangle(
            easy3d::SurfaceMesh::Vertex(0),
            easy3d::SurfaceMesh::Vertex((int)i),
            easy3d::SurfaceMesh::Vertex((int)i + 1));
    }
    apply_region_properties(mesh.get(), region_id);
    return mesh;
}

std::unique_ptr<easy3d::SurfaceMesh> make_region_alpha_shape_mesh(
    int region_id,
    const std::vector<RANSAC_Point3d>& vertices,
    const std::vector<std::array<int, 3>>& triangles)
{
    if (vertices.empty() || triangles.empty())
        return nullptr;

    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    char name[64];
    std::snprintf(name, sizeof(name), "rg_region_%03d.as", region_id);
    mesh->set_name(name);

    for (const auto& v : vertices)
        mesh->add_vertex(easy3d::vec3((float)v.x, (float)v.y, (float)v.z));
    for (const auto& tri : triangles) {
        if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0 ||
            tri[0] >= (int)vertices.size() ||
            tri[1] >= (int)vertices.size() ||
            tri[2] >= (int)vertices.size()) {
            continue;
        }
        mesh->add_triangle(
            easy3d::SurfaceMesh::Vertex(tri[0]),
            easy3d::SurfaceMesh::Vertex(tri[1]),
            easy3d::SurfaceMesh::Vertex(tri[2]));
    }
    apply_region_properties(mesh.get(), region_id);
    return mesh;
}

void build_region_growing_result_models(
    AlgorithmController& controller,
    const std::shared_ptr<RegionGrowingRunner>& runner,
    const std::vector<RG_Point3d>& input_points,
    float bbox_diag,
    const RG_Config& cfg)
{
    if (!runner || runner->has_error() || runner->is_cancelled())
        return;

    const int region_count = runner->num_regions();
    const int point_count = (int)input_points.size();
    for (int i = 0; i < region_count; ++i) {
        RG_RegionResult result;
        runner->get_region(i, result);
        if (result.size < 3)
            continue;

        double plane_eq[4] = {
            result.plane[0],
            result.plane[1],
            result.plane[2],
            result.plane[3]
        };

        std::vector<easy3d::vec3> region_points;
        region_points.reserve(result.indices.size());

        constexpr std::size_t kSampleLimit = claw3d::primitive_preview_policy::kPrimitiveJobSampleLimit;
        const std::size_t stride = result.indices.size() > kSampleLimit
            ? (result.indices.size() + kSampleLimit - 1) / kSampleLimit
            : 1;
        std::vector<double> flat_sample;
        flat_sample.reserve(
            std::min(result.indices.size(), kSampleLimit) * 3);

        for (std::size_t pos = 0; pos < result.indices.size(); ++pos) {
            const int idx = result.indices[pos];
            if (idx < 0 || idx >= point_count)
                continue;

            const auto& p = input_points[(std::size_t)idx];
            region_points.push_back(
                easy3d::vec3((float)p.x, (float)p.y, (float)p.z));
            if ((pos % stride) == 0 && flat_sample.size() < kSampleLimit * 3) {
                flat_sample.push_back(p.x);
                flat_sample.push_back(p.y);
                flat_sample.push_back(p.z);
            }
        }

        auto patch = make_region_patch_mesh(i, result, region_points);
        if (patch)
            controller.push_result(std::move(patch));

        const int sample_size = (int)(flat_sample.size() / 3);
        if (sample_size < 3)
            continue;

        std::vector<RANSAC_Point3d> hull_vertices;
        plane_convex_hull_2d(
            flat_sample.data(), sample_size, plane_eq, hull_vertices);
        auto hull_mesh = make_region_convex_hull_mesh(i, hull_vertices);
        if (hull_mesh)
            controller.push_result(std::move(hull_mesh));

        std::vector<RANSAC_Point3d> alpha_vertices;
        std::vector<std::array<int, 3>> alpha_triangles;
        plane_alpha_shape_2d(
            flat_sample.data(),
            sample_size,
            plane_eq,
            (double)(bbox_diag * claw3d::primitive_preview_policy::kRegionGrowingPlanePatchBboxRatio),
            alpha_vertices,
            alpha_triangles);
        auto alpha_mesh = make_region_alpha_shape_mesh(
            i, alpha_vertices, alpha_triangles);
        if (alpha_mesh)
            controller.push_result(std::move(alpha_mesh));
    }

    std::ostringstream oss;
    oss << "## Region Growing Result\n\n";
    oss << "### Parameters\n";
    oss << "k_neighbors=" << cfg.k_neighbors
        << " max_distance=" << cfg.max_distance
        << " max_angle=" << cfg.max_angle_deg
        << " min_region_size=" << cfg.min_region_size << "\n\n";
    oss << "### Regions (" << region_count << " total)\n\n";
    oss << "| Region | Points | Plane (a,b,c,d) |\n";
    oss << "|--------|--------|------------------|\n";
    for (int i = 0; i < region_count; ++i) {
        RG_RegionResult result;
        runner->get_region(i, result);
        oss << "| rg_region_" << std::setfill('0') << std::setw(3) << i
            << " | " << result.size
            << " | (" << std::fixed << std::setprecision(3)
            << result.plane[0] << ", " << result.plane[1] << ", "
            << result.plane[2] << ", " << result.plane[3] << ") |\n";
    }
    oss << "\n### Notes\n";
    oss << "- Total input points: " << point_count << "\n";
    oss << "- Regions are contiguous planar patches grown from seeds.\n";
    oss << "- Larger regions with low plane residual = high confidence.\n";
    oss << "- Small regions may be noise, depending on min_region_size.\n";
    controller.set_quality_context(oss.str());
}

} // namespace

struct RegionGrowingJobHandle::Impl {
    explicit Impl(std::shared_ptr<RegionGrowingRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<RegionGrowingRunner> runner;
};

RegionGrowingJobHandle::RegionGrowingJobHandle(
    std::shared_ptr<RegionGrowingJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool RegionGrowingJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void RegionGrowingJobHandle::reset()
{
    detail::reset_handle(impl_);
}

bool RegionGrowingJobHandle::drain_live_events(
    std::vector<RG_FrameEvent>& out_events) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->drain_live_events(out_events);
}

void RegionGrowingJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool RegionGrowingJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

void RegionGrowingJobHandle::pause() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->pause();
}

void RegionGrowingJobHandle::resume() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->resume();
}

void RegionGrowingJobHandle::step() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->step();
}

bool RegionGrowingJobHandle::is_paused() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_paused();
}

bool RegionGrowingJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

int RegionGrowingJobHandle::num_regions() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->num_regions() : 0;
}

float RegionGrowingJobHandle::progress() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->progress() : 0.0f;
}

RG_DebugStats RegionGrowingJobHandle::debug_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->debug_stats() : RG_DebugStats{};
}

void RegionGrowingJobHandle::get_region(
    int idx,
    RG_RegionResult& out) const
{
    out = RG_RegionResult{};
    if (auto runner = detail::runner_from(impl_))
        runner->get_region(idx, out);
}

RegionGrowingJobHandle start_region_growing_job(
    AlgorithmController& controller,
    const RegionGrowingJobStart& request)
{
    PointCloudPod pod;
    if (!point_cloud_to_region_growing_pod(request.source_cloud, pod)) {
        LOG(WARNING) << "Region Growing: empty point cloud or missing normals";
        return {};
    }

    auto runner = std::make_shared<RegionGrowingRunner>();
    runner->set_input(pod.points, pod.normals);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::RegionGrowing,
                     "Region Growing",
                     request.source_handle,
                     ResultDisposition::AddPrimitiveChildren);

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         bbox_diag = pod.bbox_diag,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui,
         input_points = std::move(pod.points)]() {
            try {
                runner->run(cfg);
                if (!runner->is_cancelled()) {
                    build_region_growing_result_models(
                        controller, runner, input_points, bbox_diag, cfg);
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Region Growing worker exception: " << e.what();
                runner->set_error(e.what());
                controller.mark_done();
            } catch (...) {
                LOG(ERROR) << "Region Growing worker unknown exception";
                runner->set_error("unknown worker exception");
                controller.mark_done();
            }

            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return RegionGrowingJobHandle(std::make_shared<RegionGrowingJobHandle::Impl>(runner));
}

} // namespace claw3d::services

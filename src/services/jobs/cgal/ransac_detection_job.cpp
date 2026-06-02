// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/cgal/ransac_detection_job.h"
#include "services/core/job_handle_detail.h"
#include "ransac_runner.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"
#include "common/primitive_preview_policy.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/surface_mesh_builder.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace claw3d::services {
namespace {

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

bool point_cloud_to_ransac_pod(
    easy3d::PointCloud* cloud,
    std::vector<RANSAC_Point3d>& points,
    std::vector<RANSAC_Vector3d>& normals)
{
    points.clear();
    normals.clear();
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

    points.resize(n);
    normals.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        points[i] = {pts[i].x, pts[i].y, pts[i].z};
        normals[i] = {nms[i].x, nms[i].y, nms[i].z};
    }
    return true;
}

std::unique_ptr<easy3d::SurfaceMesh> make_plane_patch(
    int shape_id,
    const double plane_eq[4],
    int inliers,
    const std::vector<RANSAC_Point3d>& input_points,
    float bbox_diag,
    const easy3d::vec3& bbox_center,
    float epsilon,
    std::vector<double>& flat_sample)
{
    flat_sample.clear();

    const double a = plane_eq[0];
    const double b = plane_eq[1];
    const double c = plane_eq[2];
    const double d = plane_eq[3];
    const double len2 = a * a + b * b + c * c;
    if (len2 < 1e-20)
        return nullptr;

    easy3d::vec3 normal((float)a, (float)b, (float)c);
    normal = normalize(normal);

    easy3d::vec3 t1 = (std::abs(normal.x) < 0.9f)
        ? normalize(cross(normal, easy3d::vec3(1, 0, 0)))
        : normalize(cross(normal, easy3d::vec3(0, 1, 0)));
    easy3d::vec3 t2 = normalize(cross(normal, t1));

    const double inv_norm = 1.0 / std::sqrt(len2);
    const double dist_thresh = (epsilon > 0.0f)
        ? (double)(epsilon * claw3d::primitive_preview_policy::kRansacPlanePatchEpsilonMultiplier)
        : (double)(bbox_diag * claw3d::primitive_preview_policy::kRansacPlanePatchBboxRatio);

    const double bcx = bbox_center.x;
    const double bcy = bbox_center.y;
    const double bcz = bbox_center.z;
    const double dist_center = (a * bcx + b * bcy + c * bcz + d) *
        inv_norm;
    easy3d::vec3 ref_center = bbox_center - normal * (float)dist_center;

    double cx_acc = 0.0;
    double cy_acc = 0.0;
    double cz_acc = 0.0;
    float u_min = std::numeric_limits<float>::max();
    float u_max = -std::numeric_limits<float>::max();
    float v_min = u_min;
    float v_max = u_max;
    int found = 0;

    constexpr std::size_t kSampleLimit = claw3d::primitive_preview_policy::kPrimitiveJobSampleLimit;
    flat_sample.reserve(kSampleLimit * 3);
    for (const auto& p : input_points) {
        const double dist = std::abs(a * p.x + b * p.y + c * p.z + d) *
            inv_norm;
        if (dist > dist_thresh)
            continue;

        cx_acc += p.x;
        cy_acc += p.y;
        cz_acc += p.z;

        easy3d::vec3 diff(
            (float)p.x - ref_center.x,
            (float)p.y - ref_center.y,
            (float)p.z - ref_center.z);
        const float u = dot(diff, t1);
        const float v = dot(diff, t2);
        u_min = std::min(u_min, u);
        u_max = std::max(u_max, u);
        v_min = std::min(v_min, v);
        v_max = std::max(v_max, v);
        ++found;

        if (flat_sample.size() < kSampleLimit * 3) {
            flat_sample.push_back(p.x);
            flat_sample.push_back(p.y);
            flat_sample.push_back(p.z);
        }
    }

    easy3d::vec3 center;
    float half_u = 0.0f;
    float half_v = 0.0f;
    if (found > 8) {
        center = easy3d::vec3(
            (float)(cx_acc / found),
            (float)(cy_acc / found),
            (float)(cz_acc / found));
        const double dc = (a * (cx_acc / found) +
                           b * (cy_acc / found) +
                           c * (cz_acc / found) + d) * inv_norm;
        center = center - normal * (float)dc;
        half_u = std::max(0.05f * bbox_diag, 0.55f * (u_max - u_min));
        half_v = std::max(0.05f * bbox_diag, 0.55f * (v_max - v_min));
    } else {
        center = ref_center;
        const int total_in = (int)input_points.size();
        const float frac = total_in > 0
            ? std::min(1.0f, (float)inliers / (float)total_in * 4.0f)
            : 0.5f;
        half_u = half_v = bbox_diag * (0.05f + 0.30f * frac);
    }

    easy3d::vec3 corners[4] = {
        center - t1 * half_u - t2 * half_v,
        center + t1 * half_u - t2 * half_v,
        center + t1 * half_u + t2 * half_v,
        center - t1 * half_u + t2 * half_v
    };

    auto patch = std::make_unique<easy3d::SurfaceMesh>();
    easy3d::SurfaceMeshBuilder builder(patch.get());
    builder.begin_surface();
    auto v0 = builder.add_vertex(corners[0]);
    auto v1 = builder.add_vertex(corners[1]);
    auto v2 = builder.add_vertex(corners[2]);
    auto v3 = builder.add_vertex(corners[3]);
    builder.add_triangle(v0, v1, v2);
    builder.add_triangle(v0, v2, v3);
    builder.end_surface(false);

    char name[64];
    std::snprintf(name, sizeof(name), "plane_%d", shape_id);
    patch->set_name(name);

    const easy3d::vec3 color = primitive_color(shape_id);
    auto color_prop = patch->add_face_property<easy3d::vec3>(
        "f:color", color);
    for (auto f : patch->faces())
        color_prop[f] = color;

    return patch;
}

std::unique_ptr<easy3d::SurfaceMesh> make_convex_hull_mesh(
    const char* base_name,
    const std::vector<RANSAC_Point3d>& vertices)
{
    if (vertices.size() < 3)
        return nullptr;

    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    mesh->set_name((std::string(base_name) + ".ch").c_str());
    easy3d::SurfaceMeshBuilder builder(mesh.get());
    builder.begin_surface();
    std::vector<easy3d::SurfaceMesh::Vertex> handles;
    handles.reserve(vertices.size());
    for (const auto& v : vertices) {
        handles.push_back(builder.add_vertex(easy3d::vec3(
            (float)v.x, (float)v.y, (float)v.z)));
    }
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
        builder.add_triangle(handles[0], handles[i], handles[i + 1]);
    builder.end_surface(false);
    return mesh;
}

std::unique_ptr<easy3d::SurfaceMesh> make_alpha_shape_mesh(
    const char* base_name,
    const std::vector<RANSAC_Point3d>& vertices,
    const std::vector<std::array<int, 3>>& triangles)
{
    if (vertices.empty() || triangles.empty())
        return nullptr;

    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    mesh->set_name((std::string(base_name) + ".as").c_str());
    easy3d::SurfaceMeshBuilder builder(mesh.get());
    builder.begin_surface();
    std::vector<easy3d::SurfaceMesh::Vertex> handles;
    handles.reserve(vertices.size());
    for (const auto& v : vertices) {
        handles.push_back(builder.add_vertex(easy3d::vec3(
            (float)v.x, (float)v.y, (float)v.z)));
    }
    for (const auto& t : triangles) {
        if (t[0] < 0 || t[1] < 0 || t[2] < 0 ||
            t[0] >= (int)handles.size() ||
            t[1] >= (int)handles.size() ||
            t[2] >= (int)handles.size()) {
            continue;
        }
        builder.add_triangle(handles[t[0]], handles[t[1]], handles[t[2]]);
    }
    builder.end_surface(false);
    return mesh;
}

void build_ransac_result_models(
    AlgorithmController& controller,
    const std::shared_ptr<RansacRunner>& runner,
    const std::vector<RANSAC_Point3d>& input_points,
    float bbox_diag,
    const easy3d::vec3& bbox_center,
    const RANSAC_Config& cfg)
{
    if (!runner || runner->has_error() || runner->is_cancelled())
        return;

    const int shapes = runner->num_shapes();
    for (int i = 0; i < shapes; ++i) {
        double plane_eq[4] = {0, 0, 0, 0};
        int inliers = 0;
        runner->get_shape_plane(i, plane_eq, inliers);

        std::vector<double> flat_sample;
        auto patch = make_plane_patch(
            i, plane_eq, inliers, input_points, bbox_diag,
            bbox_center, cfg.epsilon, flat_sample);
        if (!patch)
            continue;

        const std::string base_name = patch->name();
        controller.push_result(std::move(patch));

        const int sample_size = (int)(flat_sample.size() / 3);
        if (sample_size < 3)
            continue;

        std::vector<RANSAC_Point3d> ch;
        ransac_convex_hull_2d(
            flat_sample.data(), sample_size, plane_eq, ch);
        auto ch_mesh = make_convex_hull_mesh(base_name.c_str(), ch);
        if (ch_mesh)
            controller.push_result(std::move(ch_mesh));

        std::vector<RANSAC_Point3d> as_vertices;
        std::vector<std::array<int, 3>> as_triangles;
        ransac_alpha_shape_2d(
            flat_sample.data(),
            sample_size,
            plane_eq,
            bbox_diag * claw3d::primitive_preview_policy::kRansacPlanePatchBboxRatio,
            as_vertices,
            as_triangles);
        auto as_mesh = make_alpha_shape_mesh(
            base_name.c_str(), as_vertices, as_triangles);
        if (as_mesh)
            controller.push_result(std::move(as_mesh));
    }
}

std::string build_ransac_quality_report(
    const std::shared_ptr<RansacRunner>& runner,
    const std::vector<RANSAC_Point3d>& input_points,
    const RANSAC_Config& cfg,
    float elapsed_seconds)
{
    std::ostringstream ai;
    ai << "## RANSAC Primitive Extraction Report\n\n";
    ai << "epsilon=" << cfg.epsilon
       << " normal_thresh=" << cfg.normal_threshold
       << " cluster_eps=" << cfg.cluster_epsilon
       << " min_pts=" << cfg.min_points << "\n";
    ai << "elapsed=" << elapsed_seconds << "s\n\n";
    ai << "| Shape | Type | Inliers | Plane Equation |\n";
    ai << "|-------|------|---------|----------------|\n";

    const int shapes = runner ? runner->num_shapes() : 0;
    int total_inliers = 0;
    for (int i = 0; i < shapes; ++i) {
        double eq[4] = {0, 0, 0, 0};
        int inliers = 0;
        runner->get_shape_plane(i, eq, inliers);
        total_inliers += std::max(0, inliers);
        ai << "| " << i << " | plane | " << inliers << " | "
           << eq[0] << "x+" << eq[1] << "y+" << eq[2] << "z+"
           << eq[3] << "=0 |\n";
    }
    const double coverage = input_points.empty()
        ? 0.0
        : std::min(100.0,
                   100.0 * static_cast<double>(total_inliers) /
                       static_cast<double>(input_points.size()));
    ai << "\nTotal points: " << input_points.size()
       << " | Covered points: " << total_inliers
       << " | Coverage: " << coverage << "%\n";
    return ai.str();
}

} // namespace

struct RansacDetectionJobHandle::Impl {
    explicit Impl(std::shared_ptr<RansacRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<RansacRunner> runner;
};

RansacDetectionJobHandle::RansacDetectionJobHandle(
    std::shared_ptr<RansacDetectionJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool RansacDetectionJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void RansacDetectionJobHandle::reset()
{
    detail::reset_handle(impl_);
}

bool RansacDetectionJobHandle::drain_live_events(
    std::vector<RANSAC_FrameEvent>& out_events) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->drain_live_events(out_events);
}

void RansacDetectionJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool RansacDetectionJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

void RansacDetectionJobHandle::pause() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->pause();
}

void RansacDetectionJobHandle::resume() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->resume();
}

void RansacDetectionJobHandle::step() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->step();
}

bool RansacDetectionJobHandle::is_paused() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_paused();
}

int RansacDetectionJobHandle::total_shapes() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->total_shapes() : 0;
}

int RansacDetectionJobHandle::total_points() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->total_points() : 0;
}

void compute_ransac_convex_hull_2d(
    const double* in_pts,
    int count,
    const double plane_eq[4],
    std::vector<RANSAC_Point3d>& out_hull)
{
    ::ransac_convex_hull_2d(in_pts, count, plane_eq, out_hull);
}

void compute_ransac_alpha_shape_2d(
    const double* in_pts,
    int count,
    const double plane_eq[4],
    double alpha,
    std::vector<RANSAC_Point3d>& out_vertices,
    std::vector<std::array<int, 3>>& out_triangles)
{
    ::ransac_alpha_shape_2d(
        in_pts, count, plane_eq, alpha, out_vertices, out_triangles);
}

RansacDetectionJobHandle start_ransac_detection_job(
    AlgorithmController& controller,
    const RansacDetectionJobStart& request)
{
    std::vector<RANSAC_Point3d> input_points;
    std::vector<RANSAC_Vector3d> input_normals;
    if (!point_cloud_to_ransac_pod(
            request.source_cloud, input_points, input_normals)) {
        LOG(WARNING) << "RANSAC detection: empty point cloud or missing normals";
        return {};
    }

    const auto& bbox = request.source_cloud->bounding_box();
    const float bbox_diag = bbox.is_valid() ? bbox.diagonal_length() : 1.0f;
    const easy3d::vec3 bbox_center = bbox.is_valid()
        ? bbox.center()
        : easy3d::vec3(0, 0, 0);

    auto runner = std::make_shared<RansacRunner>();
    runner->set_input(input_points, input_normals);

    controller.begin(AlgorithmId::RansacPrimitive,
                     "RANSAC Detection",
                     request.source_handle,
                     ResultDisposition::AddPrimitiveChildren,
                     request.config.live_preview
                         ? AlgorithmCompletionPolicy::preview_flush()
                         : AlgorithmCompletionPolicy::immediate());

    controller.start_worker(std::thread(
        [&controller,
         runner,
         cfg = request.config,
         bbox_diag,
         bbox_center,
         wake_ui = request.wake_ui,
         input_points = std::move(input_points)]() {
            const auto started_at = std::chrono::steady_clock::now();
            try {
                runner->run(cfg);
            } catch (const std::exception& e) {
                LOG(ERROR) << "RANSAC worker exception: " << e.what();
                runner->set_error(e.what());
            } catch (...) {
                LOG(ERROR) << "RANSAC worker unknown exception";
                runner->set_error("unknown exception");
            }

            const float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - started_at).count();
            const int shapes = runner->num_shapes();
            LOG(INFO) << "RANSAC completed: " << shapes
                      << " shapes in " << elapsed
                      << "s (bbox_diag=" << bbox_diag
                      << ", live=" << cfg.live_preview << ")";

            if (!cfg.live_preview) {
                build_ransac_result_models(
                    controller, runner, input_points, bbox_diag,
                    bbox_center, cfg);
            }
            if (!runner->has_error()) {
                controller.set_quality_context(build_ransac_quality_report(
                    runner, input_points, cfg, elapsed));
            }
            controller.mark_done();
            if (wake_ui)
                wake_ui();
        }));

    return RansacDetectionJobHandle(std::make_shared<RansacDetectionJobHandle::Impl>(runner));
}

} // namespace claw3d::services

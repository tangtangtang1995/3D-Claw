// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/jobs/easy3d/geodesic_distance_job.h"
#include "services/core/job_handle_detail.h"

#ifdef CLAW3D_HAS_CGAL
#include "geodesic_cgal_runner.h"
#endif
#include "observable_surface_mesh_geodesic.h"
#include "services/core/algorithm_controller.h"
#include "services/core/algorithm_id.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class GeoFrontRunner {
public:
    struct Impl;
    GeoFrontRunner();
    ~GeoFrontRunner();
    GeoFrontRunner(const GeoFrontRunner&) = delete;
    GeoFrontRunner& operator=(const GeoFrontRunner&) = delete;

    void set_input(easy3d::SurfaceMesh* mesh,
                   const std::vector<int>& source_vids);
    void run(const GEO_FrontConfig& cfg);
    void cancel();
    bool is_cancelled() const;
    bool is_done() const;
    bool has_error() const;
    std::string last_error() const;

    bool poll_snapshot(int& last_generation, GEO_FrontSnapshot& out) const;
    GEO_FrontResultStats result_stats() const;

    void get_front_path(std::vector<float>& xyz_flat) const;
    void get_exact_path(std::vector<float>& xyz_flat) const;

private:
    std::unique_ptr<Impl> impl_;
};

namespace {

using Clock = std::chrono::steady_clock;

bool is_live_vertex(easy3d::SurfaceMesh* mesh, int vid)
{
    if (!mesh || vid < 0 || vid >= (int)mesh->vertices_size())
        return false;
    easy3d::SurfaceMesh::Vertex v(vid);
    return mesh->is_valid(v) &&
        !(mesh->has_garbage() && mesh->is_deleted(v));
}

#ifdef CLAW3D_HAS_CGAL
void extract_pod(easy3d::SurfaceMesh* mesh,
                 std::vector<GEO_CGAL_Point3d>& out_verts,
                 std::vector<GEO_CGAL_Triangle>& out_tris,
                 std::vector<int>& vid_to_compact)
{
    out_verts.clear();
    out_tris.clear();
    if (!mesh)
        return;

    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    if (!pts)
        return;

    const int id_capacity = (int)mesh->vertices_size();
    vid_to_compact.assign(id_capacity, -1);
    int pos = 0;
    for (auto v : mesh->vertices()) {
        const int id = (int)v.idx();
        if (id < 0 || id >= id_capacity)
            continue;
        vid_to_compact[id] = pos++;
        out_verts.push_back({
            (double)pts[v].x,
            (double)pts[v].y,
            (double)pts[v].z
        });
    }

    for (auto f : mesh->faces()) {
        int vi[3] = {-1, -1, -1};
        int k = 0;
        bool valid_triangle = true;
        for (auto hv : mesh->vertices(f)) {
            if (k >= 3) {
                valid_triangle = false;
                break;
            }
            const int id = (int)hv.idx();
            if (id < 0 || id >= id_capacity || vid_to_compact[id] < 0) {
                valid_triangle = false;
                break;
            }
            vi[k++] = vid_to_compact[id];
        }
        if (valid_triangle && k == 3)
            out_tris.push_back({vi[0], vi[1], vi[2]});
    }
}
#endif

void build_and_publish_snapshot(GeoFrontRunner::Impl& impl,
                                int visited,
                                std::size_t front_size,
                                float max_dist,
                                bool force);

void interruptible_sleep(int ms, const std::atomic<bool>& cancel)
{
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end) {
        if (cancel.load(std::memory_order_relaxed))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace

struct GeoFrontRunner::Impl {
    easy3d::SurfaceMesh* mesh = nullptr;
    std::vector<int> seeds;
    GEO_FrontConfig cfg;

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    mutable std::mutex snap_mutex;
    GEO_FrontSnapshot latest_snapshot;
    std::atomic<int> snap_generation{0};

    GEO_FrontResultStats stats{};

    mutable std::mutex path_mutex;
    std::vector<float> path_xyz_flat;
    mutable std::mutex exact_path_mutex;
    std::vector<float> exact_path_xyz_flat;
    double exact_path_length_ = 0.0;
    bool exact_path_found_ = false;

    Clock::time_point last_publish_time{};

    void reset()
    {
        cancel_flag = false;
        done_flag = false;
        has_error_flag = false;
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message.clear();
        }
        {
            std::lock_guard<std::mutex> lk(snap_mutex);
            latest_snapshot = GEO_FrontSnapshot{};
        }
        snap_generation = 0;
        stats = GEO_FrontResultStats{};
        last_publish_time = Clock::time_point{};
        {
            std::lock_guard<std::mutex> lk(path_mutex);
            path_xyz_flat.clear();
        }
        {
            std::lock_guard<std::mutex> lk(exact_path_mutex);
            exact_path_xyz_flat.clear();
        }
        exact_path_length_ = 0.0;
        exact_path_found_ = false;
    }

    void set_error(const std::string& msg)
    {
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message = msg;
        }
        has_error_flag = true;
    }
};

namespace {

void build_and_publish_snapshot(GeoFrontRunner::Impl& impl,
                                int visited,
                                std::size_t front_size,
                                float max_dist,
                                bool force)
{
    if (!force && impl.cfg.snapshot_min_ms > 0 &&
        impl.last_publish_time != Clock::time_point{}) {
        const auto now = Clock::now();
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - impl.last_publish_time).count();
        if (since < impl.cfg.snapshot_min_ms)
            return;
    }
    impl.last_publish_time = Clock::now();

    GEO_FrontSnapshot snap;
    snap.visited_vertices = visited;
    snap.total_vertices = (int)impl.mesh->n_vertices();
    snap.front_size = (int)front_size;
    snap.max_distance = max_dist;
    snap.progress = (snap.total_vertices > 0)
        ? std::min(1.0f, (float)visited / (float)snap.total_vertices)
        : 0.0f;

    snap.vertex_distances.assign(snap.total_vertices, FLT_MAX);
    snap.vertex_state.assign(snap.total_vertices, 0);

    auto dprop = impl.mesh->get_vertex_property<float>(
        "v:geodesic:distance");
    if (dprop) {
        int i = 0;
        for (auto v : impl.mesh->vertices()) {
            const float d = dprop[v];
            snap.vertex_distances[i] = d;
            if (d < FLT_MAX * 0.5f)
                snap.vertex_state[i] = 2;
            ++i;
        }
    }

    const int new_gen = impl.snap_generation.load() + 1;
    snap.generation = new_gen;
    {
        std::lock_guard<std::mutex> lk(impl.snap_mutex);
        impl.latest_snapshot = std::move(snap);
    }
    impl.snap_generation.store(new_gen);
}

struct ProgressObserver
    : claw3d::algo::ObservableSurfaceMeshGeodesic::ObserverHook {
    GeoFrontRunner::Impl* impl = nullptr;
    int settled = 0;
    int peak_front = 0;
    float max_dist = 0.0f;
    int events_per_publish = 1;
    int events_since_publish = 0;
    int per_publish_sleep_ms = 0;

    void on_vertex_settled(unsigned int n,
                           easy3d::SurfaceMesh::Vertex,
                           float d,
                           std::size_t front_size) override
    {
        settled = (int)n;
        if ((int)front_size > peak_front)
            peak_front = (int)front_size;
        if (d > max_dist)
            max_dist = d;
        if (++events_since_publish >= events_per_publish) {
            build_and_publish_snapshot(
                *impl, settled, front_size, max_dist, true);
            events_since_publish = 0;
            if (per_publish_sleep_ms > 0)
                interruptible_sleep(per_publish_sleep_ms, impl->cancel_flag);
        }
    }

    bool go_further() override
    {
        return impl && !impl->cancel_flag.load(std::memory_order_relaxed);
    }
};

} // namespace

GeoFrontRunner::GeoFrontRunner() : impl_(std::make_unique<Impl>()) {}
GeoFrontRunner::~GeoFrontRunner() = default;

void GeoFrontRunner::set_input(easy3d::SurfaceMesh* mesh,
                               const std::vector<int>& source_vids)
{
    impl_->mesh = mesh;
    impl_->seeds = source_vids;
}

void GeoFrontRunner::run(const GEO_FrontConfig& cfg)
{
    impl_->reset();
    impl_->cfg = cfg;
    if (!impl_->mesh || impl_->seeds.empty()) {
        impl_->set_error("no mesh or no sources");
        impl_->done_flag = true;
        return;
    }

    const int nv = (int)impl_->mesh->n_vertices();
    const int id_capacity = (int)impl_->mesh->vertices_size();
    std::vector<easy3d::SurfaceMesh::Vertex> seed_vs;
    seed_vs.reserve(impl_->seeds.size());
    for (int vid : impl_->seeds) {
        if (is_live_vertex(impl_->mesh, vid))
            seed_vs.emplace_back(vid);
    }
    if (seed_vs.empty()) {
        impl_->set_error("all source vertex ids out of range");
        impl_->done_flag = true;
        return;
    }

    impl_->stats.input_vertices = nv;
    impl_->stats.input_faces = (int)impl_->mesh->n_faces();
    impl_->stats.source_count = (int)seed_vs.size();

    const auto t0 = Clock::now();
    try {
        claw3d::algo::ObservableSurfaceMeshGeodesic geo(
            impl_->mesh, cfg.use_virtual_edges);
        ProgressObserver obs;
        obs.impl = impl_.get();
        obs.events_per_publish = std::max(1, nv / 60);
        obs.per_publish_sleep_ms = (cfg.preview_speed == 1) ? 180 : 40;
        geo.set_observer(&obs);
        (void)geo.compute(seed_vs, cfg.max_dist);

        impl_->stats.visited_vertices = obs.settled;
        impl_->stats.max_distance = obs.max_dist;
        impl_->stats.peak_front_size = obs.peak_front;

        auto dprop = impl_->mesh->get_vertex_property<float>(
            "v:geodesic:distance");
        if (dprop) {
            double sum = 0.0;
            int count = 0;
            for (auto v : impl_->mesh->vertices()) {
                const float d = dprop[v];
                if (d < FLT_MAX * 0.5f) {
                    sum += d;
                    ++count;
                }
            }
            impl_->stats.mean_distance =
                (count > 0) ? (float)(sum / count) : 0.0f;
        }

        build_and_publish_snapshot(*impl_, obs.settled, 0u, obs.max_dist, true);

        if (is_live_vertex(impl_->mesh, cfg.target_vid) &&
            !impl_->cancel_flag.load()) {
            auto dprop = impl_->mesh->get_vertex_property<float>(
                "v:geodesic:distance");
            auto pts = impl_->mesh->get_vertex_property<easy3d::vec3>(
                "v:point");
            if (dprop && pts) {
                std::vector<bool> is_src(id_capacity, false);
                for (int sv : impl_->seeds) {
                    if (is_live_vertex(impl_->mesh, sv))
                        is_src[sv] = true;
                }

                std::vector<easy3d::vec3> path_points;
                std::vector<bool> visited_walk(id_capacity, false);
                int cur = cfg.target_vid;
                int max_steps = nv + 1;
                while (cur >= 0 && max_steps-- > 0) {
                    if (!is_live_vertex(impl_->mesh, cur))
                        break;
                    if (visited_walk[cur])
                        break;
                    visited_walk[cur] = true;
                    easy3d::SurfaceMesh::Vertex cv(cur);
                    path_points.push_back(pts[cv]);
                    if (is_src[cur])
                        break;
                    int best = -1;
                    float best_d = dprop[cv];
                    for (auto vn : impl_->mesh->vertices(cv)) {
                        const float d = dprop[vn];
                        if (d < best_d) {
                            best_d = d;
                            best = (int)vn.idx();
                        }
                    }
                    if (best < 0)
                        break;
                    cur = best;
                }
                if (path_points.size() >= 2) {
                    double length = 0.0;
                    for (std::size_t i = 1; i < path_points.size(); ++i) {
                        const auto& a = path_points[i - 1];
                        const auto& b = path_points[i];
                        const double dx = (double)b.x - (double)a.x;
                        const double dy = (double)b.y - (double)a.y;
                        const double dz = (double)b.z - (double)a.z;
                        length += std::sqrt(dx * dx + dy * dy + dz * dz);
                    }
                    {
                        std::lock_guard<std::mutex> lk(impl_->path_mutex);
                        impl_->path_xyz_flat.clear();
                        impl_->path_xyz_flat.reserve(path_points.size() * 3);
                        for (const auto& p : path_points) {
                            impl_->path_xyz_flat.push_back(p.x);
                            impl_->path_xyz_flat.push_back(p.y);
                            impl_->path_xyz_flat.push_back(p.z);
                        }
                    }
                    impl_->stats.path_found = true;
                    impl_->stats.front_path_length = (float)length;
                }
            }
        }

#ifdef CLAW3D_HAS_CGAL
        if (cfg.compare_exact_path &&
            is_live_vertex(impl_->mesh, cfg.target_vid) &&
            !impl_->cancel_flag.load()) {
            std::vector<GEO_CGAL_Point3d> pod_verts;
            std::vector<GEO_CGAL_Triangle> pod_tris;
            std::vector<int> vid_to_pos;
            extract_pod(impl_->mesh, pod_verts, pod_tris, vid_to_pos);
            if (!pod_verts.empty() && !pod_tris.empty()) {
                int src_vid = (impl_->seeds[0] >= 0 &&
                               impl_->seeds[0] < (int)vid_to_pos.size())
                    ? vid_to_pos[impl_->seeds[0]]
                    : -1;
                int tgt_vid = (cfg.target_vid >= 0 &&
                               cfg.target_vid < (int)vid_to_pos.size())
                    ? vid_to_pos[cfg.target_vid]
                    : -1;
                if (src_vid >= 0 && tgt_vid >= 0) {
                    GeodesicCGALRunner cgal_runner;
                    cgal_runner.set_input(pod_verts, pod_tris);
                    GEO_CGAL_Source src;
                    src.vertex_id = src_vid;
                    cgal_runner.set_source(src);
                    GEO_CGAL_Target tgt;
                    tgt.vertex_id = tgt_vid;
                    cgal_runner.set_target(tgt);
                    cgal_runner.run({});
                    if (!cgal_runner.is_cancelled() &&
                        !cgal_runner.has_error()) {
                        auto result = cgal_runner.result();
                        if (result.path_found) {
                            std::lock_guard<std::mutex> lk(
                                impl_->exact_path_mutex);
                            impl_->exact_path_xyz_flat.clear();
                            impl_->exact_path_xyz_flat.reserve(
                                result.points.size() * 3);
                            for (const auto& p : result.points) {
                                impl_->exact_path_xyz_flat.push_back(
                                    (float)p.x);
                                impl_->exact_path_xyz_flat.push_back(
                                    (float)p.y);
                                impl_->exact_path_xyz_flat.push_back(
                                    (float)p.z);
                            }
                            impl_->exact_path_length_ = result.length;
                            impl_->exact_path_found_ = true;
                            impl_->stats.exact_path_found = true;
                            impl_->stats.exact_path_length =
                                (float)result.length;
                        }
                    }
                }
            }
        }
#endif
    } catch (const std::exception& e) {
        impl_->set_error(std::string("Geodesic exception: ") + e.what());
    } catch (...) {
        impl_->set_error("Geodesic unknown exception");
    }

    const auto t1 = Clock::now();
    impl_->stats.ms_total =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    impl_->stats.cancelled = impl_->cancel_flag.load();
    impl_->done_flag = true;
}

void GeoFrontRunner::cancel() { impl_->cancel_flag = true; }
bool GeoFrontRunner::is_cancelled() const { return impl_->cancel_flag.load(); }
bool GeoFrontRunner::is_done() const { return impl_->done_flag.load(); }
bool GeoFrontRunner::has_error() const { return impl_->has_error_flag.load(); }

std::string GeoFrontRunner::last_error() const
{
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

bool GeoFrontRunner::poll_snapshot(int& last_generation,
                                   GEO_FrontSnapshot& out) const
{
    const int generation = impl_->snap_generation.load();
    if (generation <= last_generation)
        return false;
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    out = impl_->latest_snapshot;
    last_generation = generation;
    return true;
}

GEO_FrontResultStats GeoFrontRunner::result_stats() const
{
    return impl_->stats;
}

void GeoFrontRunner::get_front_path(std::vector<float>& xyz_flat) const
{
    std::lock_guard<std::mutex> lk(impl_->path_mutex);
    xyz_flat = impl_->path_xyz_flat;
}

void GeoFrontRunner::get_exact_path(std::vector<float>& xyz_flat) const
{
    std::lock_guard<std::mutex> lk(impl_->exact_path_mutex);
    xyz_flat = impl_->exact_path_xyz_flat;
}

namespace claw3d::services {

struct GeodesicFrontJobHandle::Impl {
    explicit Impl(std::shared_ptr<GeoFrontRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<GeoFrontRunner> runner;
};

GeodesicFrontJobHandle::GeodesicFrontJobHandle(
    std::shared_ptr<GeodesicFrontJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool GeodesicFrontJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void GeodesicFrontJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void GeodesicFrontJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool GeodesicFrontJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool GeodesicFrontJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool GeodesicFrontJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string GeodesicFrontJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

bool GeodesicFrontJobHandle::poll_snapshot(
    int& last_generation,
    GEO_FrontSnapshot& out) const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->poll_snapshot(last_generation, out);
}

GEO_FrontResultStats GeodesicFrontJobHandle::result_stats() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->result_stats() : GEO_FrontResultStats{};
}

void GeodesicFrontJobHandle::get_front_path(
    std::vector<float>& xyz_flat) const
{
    xyz_flat.clear();
    if (auto runner = detail::runner_from(impl_))
        runner->get_front_path(xyz_flat);
}

void GeodesicFrontJobHandle::get_exact_path(
    std::vector<float>& xyz_flat) const
{
    xyz_flat.clear();
    if (auto runner = detail::runner_from(impl_))
        runner->get_exact_path(xyz_flat);
}

GeodesicFrontJobHandle start_geodesic_front_job(
    AlgorithmController& controller,
    const GeodesicFrontJobStart& request)
{
    if (!request.source_mesh || request.source_vertex_ids.empty()) {
        LOG(WARNING) << "Geodesic front: no mesh or source vertices";
        return {};
    }

    auto runner = std::make_shared<GeoFrontRunner>();
    runner->set_input(request.source_mesh, request.source_vertex_ids);

    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::GeodesicDistance,
                     "Geodesic Front Propagation",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [runner,
         cfg = request.config,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run(cfg);
            } catch (const std::exception& e) {
                LOG(ERROR) << "[GEO] worker exception: " << e.what();
            }
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return GeodesicFrontJobHandle(std::make_shared<GeodesicFrontJobHandle::Impl>(runner));
}

#ifdef CLAW3D_HAS_CGAL

struct GeodesicCgalJobHandle::Impl {
    explicit Impl(std::shared_ptr<GeodesicCGALRunner> runner_in)
        : runner(std::move(runner_in))
    {
    }

    std::shared_ptr<GeodesicCGALRunner> runner;
};

GeodesicCgalJobHandle::GeodesicCgalJobHandle(
    std::shared_ptr<GeodesicCgalJobHandle::Impl> impl)
    : impl_(std::move(impl))
{
}

bool GeodesicCgalJobHandle::valid() const
{
    return detail::handle_valid(impl_);
}

void GeodesicCgalJobHandle::reset()
{
    detail::reset_handle(impl_);
}

void GeodesicCgalJobHandle::cancel() const
{
    if (auto runner = detail::runner_from(impl_))
        runner->cancel();
}

bool GeodesicCgalJobHandle::is_cancelled() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_cancelled();
}

bool GeodesicCgalJobHandle::is_done() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->is_done();
}

bool GeodesicCgalJobHandle::has_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner && runner->has_error();
}

std::string GeodesicCgalJobHandle::last_error() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->last_error() : std::string{};
}

GEO_CGAL_HeatResult GeodesicCgalJobHandle::heat_result() const
{
    auto runner = detail::runner_from(impl_);
    return runner ? runner->heat_result() : GEO_CGAL_HeatResult{};
}

GeodesicCgalJobHandle start_geodesic_exact_path_job(
    AlgorithmController& controller,
    const GeodesicExactPathJobStart& request)
{
    std::vector<GEO_CGAL_Point3d> pod_verts;
    std::vector<GEO_CGAL_Triangle> pod_tris;
    std::vector<int> vid_to_pos;
    extract_pod(request.source_mesh, pod_verts, pod_tris, vid_to_pos);
    if (pod_verts.empty() || pod_tris.empty())
        return {};

    const int source_vid =
        (request.source_vertex_id >= 0 &&
         request.source_vertex_id < (int)vid_to_pos.size())
        ? vid_to_pos[request.source_vertex_id]
        : -1;
    const int target_vid =
        (request.target_vertex_id >= 0 &&
         request.target_vertex_id < (int)vid_to_pos.size())
        ? vid_to_pos[request.target_vertex_id]
        : -1;
    if (source_vid < 0 || target_vid < 0)
        return {};

    auto runner = std::make_shared<GeodesicCGALRunner>();
    runner->set_input(pod_verts, pod_tris);
    GEO_CGAL_Source source;
    source.vertex_id = source_vid;
    runner->set_source(source);
    GEO_CGAL_Target target;
    target.vertex_id = target_vid;
    runner->set_target(target);

    if (request.result_valid)
        *request.result_valid = false;
    if (request.path_result)
        *request.path_result = GEO_CGAL_PathResult{};
    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::GeodesicDistance,
                     "Geodesic Exact Shortest Path",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [runner,
         path_result = request.path_result,
         result_valid = request.result_valid,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run({});
                if (path_result)
                    *path_result = runner->result();
                if (result_valid)
                    *result_valid = true;
            } catch (const std::exception& e) {
                LOG(ERROR) << "[GEO] exact worker exception: " << e.what();
            }
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return GeodesicCgalJobHandle(std::make_shared<GeodesicCgalJobHandle::Impl>(runner));
}

GeodesicCgalJobHandle start_geodesic_heat_method_job(
    AlgorithmController& controller,
    const GeodesicHeatMethodJobStart& request)
{
    std::vector<GEO_CGAL_Point3d> pod_verts;
    std::vector<GEO_CGAL_Triangle> pod_tris;
    std::vector<int> vid_to_pos;
    extract_pod(request.source_mesh, pod_verts, pod_tris, vid_to_pos);
    if (pod_verts.empty() || pod_tris.empty())
        return {};

    std::vector<int> remapped_sources;
    remapped_sources.reserve(request.source_vertex_ids.size());
    for (int source : request.source_vertex_ids) {
        if (source >= 0 && source < (int)vid_to_pos.size() &&
            vid_to_pos[source] >= 0) {
            remapped_sources.push_back(vid_to_pos[source]);
        }
    }
    if (remapped_sources.empty())
        return {};

    auto runner = std::make_shared<GeodesicCGALRunner>();
    runner->set_input(pod_verts, pod_tris);
    runner->set_sources(remapped_sources);

    if (request.result_valid)
        *request.result_valid = false;
    if (request.final_result_ready) {
        request.final_result_ready->store(false, std::memory_order_release);
    }

    controller.begin(AlgorithmId::GeodesicDistance,
                     "Geodesic Heat Method",
                     request.source_handle,
                     ResultDisposition::AddAsChild);

    controller.start_worker(std::thread(
        [runner,
         variant = request.heat_variant,
         result_valid = request.result_valid,
         final_result_ready = request.final_result_ready,
         wake_ui = request.wake_ui]() {
            try {
                runner->run_heat(variant);
                if (result_valid)
                    *result_valid = true;
            } catch (const std::exception& e) {
                LOG(ERROR) << "[GEO] heat worker exception: " << e.what();
            }
            if (final_result_ready) {
                final_result_ready->store(true, std::memory_order_release);
            }
            if (wake_ui)
                wake_ui();
        }));

    return GeodesicCgalJobHandle(std::make_shared<GeodesicCgalJobHandle::Impl>(runner));
}

#endif // CLAW3D_HAS_CGAL

} // namespace claw3d::services

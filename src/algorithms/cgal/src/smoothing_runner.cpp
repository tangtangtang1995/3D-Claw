// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "smoothing_runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/tangential_relaxation.h>
#include <CGAL/Polygon_mesh_processing/angle_and_area_smoothing.h>
#include <CGAL/Polygon_mesh_processing/smooth_shape.h>
#include <CGAL/Polygon_mesh_processing/detect_features.h>
#include <CGAL/Polygon_mesh_processing/measure.h>

namespace PMP    = CGAL::Polygon_mesh_processing;
namespace params = CGAL::parameters;

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using TMesh   = CGAL::Surface_mesh<Point_3>;
using Clock   = std::chrono::steady_clock;

struct SmoothingRunner::Impl {
    std::vector<SMOOTH_Point3d>  input_verts;
    std::vector<SMOOTH_Triangle> input_tris;
    bool has_input = false;

    SMOOTH_Config cfg{};

    std::vector<SMOOTH_Point3d>  result_verts;
    std::vector<SMOOTH_Triangle> result_tris;
    std::vector<double>          result_displacement;

    SMOOTH_Snapshot latest_snapshot{};
    std::atomic<int> snap_generation{0};
    mutable std::mutex snap_mutex;

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    SMOOTH_ResultStats stats{};

    Clock::time_point last_publish_time{};

    void reset_state() {
        cancel_flag    = false;
        done_flag      = false;
        has_error_flag = false;
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message.clear();
        }
        result_verts.clear();
        result_tris.clear();
        result_displacement.clear();
        {
            std::lock_guard<std::mutex> lk(snap_mutex);
            latest_snapshot = SMOOTH_Snapshot{};
        }
        snap_generation = 0;
        stats = SMOOTH_ResultStats{};
        last_publish_time = Clock::time_point{};
    }

    void set_error(const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message = msg;
        }
        has_error_flag = true;
    }
};

namespace {

// Per-triangle min angle in degrees.
double tri_min_angle_deg(const Point_3& a, const Point_3& b, const Point_3& c) {
    auto ab = b - a, ac = c - a, bc = c - b;
    auto Lab = std::sqrt(CGAL::to_double(ab.squared_length()));
    auto Lac = std::sqrt(CGAL::to_double(ac.squared_length()));
    auto Lbc = std::sqrt(CGAL::to_double(bc.squared_length()));
    if (Lab < 1e-30 || Lac < 1e-30 || Lbc < 1e-30) return 0.0;
    auto cosA = CGAL::to_double(ab * ac) / (Lab * Lac);
    auto cosB = CGAL::to_double((-ab) * bc) / (Lab * Lbc);
    auto cosC = CGAL::to_double((-ac) * (-bc)) / (Lac * Lbc);
    auto clamp = [](double x) {
        if (x > 1.0) return 1.0; if (x < -1.0) return -1.0; return x;
    };
    double A = std::acos(clamp(cosA));
    double B = std::acos(clamp(cosB));
    double C = std::acos(clamp(cosC));
    double m = std::min({A, B, C});
    return m * 180.0 / CGAL_PI;
}

double tri_aspect_ratio(const Point_3& a, const Point_3& b, const Point_3& c) {
    auto ab = b - a, ac = c - a, bc = c - b;
    double Lab = std::sqrt(CGAL::to_double(ab.squared_length()));
    double Lac = std::sqrt(CGAL::to_double(ac.squared_length()));
    double Lbc = std::sqrt(CGAL::to_double(bc.squared_length()));
    double longest = std::max({Lab, Lac, Lbc});
    auto cross = CGAL::cross_product(ab, ac);
    double area = 0.5 * std::sqrt(CGAL::to_double(cross.squared_length()));
    if (area < 1e-30) return 1e30;
    // Inscribed circle radius approximation r = 2A / (a+b+c)
    double perim = Lab + Lac + Lbc;
    double r_in = (perim > 0) ? (2.0 * area / perim) : 0.0;
    return (r_in > 0) ? (longest / (2.0 * r_in)) : 1e30;
}

void compute_stats(const TMesh& m, SMOOTH_QualityStats& s,
                   int boundary_edges_count, int sharp_edges_count) {
    s.vertices = (int)m.number_of_vertices();
    s.edges    = (int)m.number_of_edges();
    s.faces    = (int)m.number_of_faces();
    s.boundary_edges = boundary_edges_count;
    s.sharp_edges    = sharp_edges_count;
    s.closed         = CGAL::is_closed(m);

    auto vp = m.points();
    double xmin = 1e100, xmax = -1e100;
    double ymin = 1e100, ymax = -1e100;
    double zmin = 1e100, zmax = -1e100;
    for (auto v : m.vertices()) {
        const auto& p = vp[v];
        double x = CGAL::to_double(p.x());
        double y = CGAL::to_double(p.y());
        double z = CGAL::to_double(p.z());
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        zmin = std::min(zmin, z); zmax = std::max(zmax, z);
    }
    s.bbox_diag = std::sqrt(
        (xmax-xmin)*(xmax-xmin) +
        (ymax-ymin)*(ymax-ymin) +
        (zmax-zmin)*(zmax-zmin));

    double mn = 1e9, sumA = 0.0, sumAR = 0.0, maxAR = 0.0;
    int n_tri = 0;
    int n_degen = 0;
    for (auto f : m.faces()) {
        auto h = m.halfedge(f);
        auto a = vp[m.target(h)]; h = m.next(h);
        auto b = vp[m.target(h)]; h = m.next(h);
        auto c = vp[m.target(h)];
        double mAng = tri_min_angle_deg(a, b, c);
        double ar = tri_aspect_ratio(a, b, c);
        if (ar > 1e25) ++n_degen;
        mn = std::min(mn, mAng);
        sumA  += mAng;
        sumAR += ar;
        maxAR  = std::max(maxAR, ar);
        if (mAng < 10.0) ++s.bad_triangles_10deg;
        if (mAng < 15.0) ++s.bad_triangles_15deg;
        ++n_tri;
    }
    s.min_angle  = (n_tri > 0) ? mn : 0.0;
    s.mean_angle = (n_tri > 0) ? (sumA / n_tri) : 0.0;
    s.mean_aspect_ratio = (n_tri > 0) ? (sumAR / n_tri) : 0.0;
    s.max_aspect_ratio  = maxAR;
    s.degenerate_faces  = n_degen;
    s.surface_area = CGAL::to_double(PMP::area(m));
    if (s.closed) s.volume = CGAL::to_double(PMP::volume(m));
}

bool build_input_mesh(const std::vector<SMOOTH_Point3d>&  pv,
                      const std::vector<SMOOTH_Triangle>& pt,
                      TMesh& mesh,
                      std::vector<TMesh::Vertex_index>& vmap_out,
                      std::string& err) {
    mesh.clear();
    if (pv.empty() || pt.empty()) {
        err = "empty input mesh";
        return false;
    }
    const int nv = (int)pv.size();
    vmap_out.clear();
    vmap_out.reserve(nv);
    for (const auto& p : pv)
        vmap_out.push_back(mesh.add_vertex(Point_3(p.x, p.y, p.z)));
    for (const auto& t : pt) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
            t.v0 >= nv || t.v1 >= nv || t.v2 >= nv ||
            t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) {
            err = "invalid triangle indices";
            return false;
        }
        TMesh::Face_index fd =
            mesh.add_face(vmap_out[t.v0], vmap_out[t.v1], vmap_out[t.v2]);
        if (fd == TMesh::null_face())
            mesh.add_face(vmap_out[t.v0], vmap_out[t.v2], vmap_out[t.v1]);
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        err = "input is not a triangle mesh after conversion";
        return false;
    }
    return true;
}

SMOOTH_Point3d to_pod(const Point_3& p) {
    return SMOOTH_Point3d{CGAL::to_double(p.x()),
                          CGAL::to_double(p.y()),
                          CGAL::to_double(p.z())};
}

// Sleep that wakes up every 50ms to check cancel_flag. Used to throttle
// the live-preview worker so the UI thread can actually render the
// intermediate iterations instead of seeing only the final state via
// the single-buffered latest_snapshot.
void interruptible_sleep(int ms, const std::atomic<bool>& cancel) {
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end) {
        if (cancel.load(std::memory_order_relaxed)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Live snapshot: copies current mesh state + per-vertex displacement
// into a SMOOTH_Snapshot. Called after each CGAL single-iter step. Throttled
// by impl.cfg.snapshot_min_ms unless force is set (final iter always wins).
void publish_smoothing_snapshot(SmoothingRunner::Impl& impl,
                                const TMesh& mesh,
                                const std::vector<Point_3>& original,
                                int iter,
                                int total_iter,
                                bool force)
{
    if (!force && impl.cfg.snapshot_min_ms > 0 &&
        impl.last_publish_time != Clock::time_point{})
    {
        const auto now = Clock::now();
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - impl.last_publish_time).count();
        if (since < impl.cfg.snapshot_min_ms) return;
    }
    impl.last_publish_time = Clock::now();

    SMOOTH_Snapshot snap;
    snap.iteration        = iter;
    snap.total_iterations = total_iter;
    snap.progress = (total_iter > 0)
        ? std::min(1.0, (double)iter / (double)total_iter)
        : 0.0;

    // Vertices + displacement.
    const std::size_t nv = mesh.number_of_vertices();
    snap.vertices.reserve(nv);
    snap.vertex_displacement.assign(nv, 0.0);
    double sum_d = 0.0, max_d = 0.0;
    int counted = 0;
    std::vector<int> idx_remap(nv, -1);
    int next_idx = 0;
    for (auto v : mesh.vertices()) {
        const auto& b = mesh.point(v);
        idx_remap[v.idx()] = next_idx;
        snap.vertices.push_back(to_pod(b));
        if (v.idx() < (TMesh::size_type)original.size()) {
            const auto& a = original[v.idx()];
            const double dx = CGAL::to_double(b.x()) - CGAL::to_double(a.x());
            const double dy = CGAL::to_double(b.y()) - CGAL::to_double(a.y());
            const double dz = CGAL::to_double(b.z()) - CGAL::to_double(a.z());
            const double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            snap.vertex_displacement[next_idx] = d;
            sum_d += d;
            if (d > max_d) max_d = d;
            ++counted;
        }
        ++next_idx;
    }
    snap.mean_displacement = (counted > 0) ? (sum_d / counted) : 0.0;
    snap.max_displacement  = max_d;

    // Triangles.
    snap.triangles.reserve(mesh.number_of_faces());
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        int v0 = idx_remap[mesh.target(h).idx()]; h = mesh.next(h);
        int v1 = idx_remap[mesh.target(h).idx()]; h = mesh.next(h);
        int v2 = idx_remap[mesh.target(h).idx()];
        if (v0 < 0 || v1 < 0 || v2 < 0) continue;
        snap.triangles.push_back(SMOOTH_Triangle{v0, v1, v2});
    }

    // Per-iter quality stats (lightweight, no extra structure stats here;
    // dialog only consumes iteration / displacement; the heatmap is separate).
    snap.stats.vertices = (int)nv;
    snap.stats.faces    = (int)mesh.number_of_faces();
    snap.stats.bbox_diag = impl.stats.before.bbox_diag;

    const int new_gen = impl.snap_generation.load() + 1;
    snap.generation = new_gen;
    {
        std::lock_guard<std::mutex> lk(impl.snap_mutex);
        impl.latest_snapshot = std::move(snap);
    }
    impl.snap_generation.store(new_gen);
}

} // namespace

SmoothingRunner::SmoothingRunner()  : impl_(std::make_unique<Impl>()) {}
SmoothingRunner::~SmoothingRunner() = default;

void SmoothingRunner::set_input(const std::vector<SMOOTH_Point3d>&  verts,
                                const std::vector<SMOOTH_Triangle>& tris) {
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void SmoothingRunner::run(const SMOOTH_Config& cfg) {
    impl_->reset_state();
    impl_->cfg = cfg;

    if (!impl_->has_input) {
        impl_->set_error("no input mesh");
        impl_->done_flag = true;
        return;
    }
    if (cfg.mode == SMOOTH_MODE_AngleArea) {
        impl_->set_error("Angle + Area mode requires Ceres support "
                         "(disabled in this build)");
        impl_->done_flag = true;
        return;
    }

    const auto t_total_begin = Clock::now();
    const auto t_pre_begin = Clock::now();

    TMesh mesh;
    std::vector<TMesh::Vertex_index> vmap;
    std::string build_err;
    if (!build_input_mesh(impl_->input_verts, impl_->input_tris,
                          mesh, vmap, build_err)) {
        impl_->set_error(build_err);
        impl_->done_flag = true;
        return;
    }

    // Snapshot original vertex positions for displacement stats.
    std::vector<Point_3> original(mesh.number_of_vertices());
    for (auto v : mesh.vertices()) original[v.idx()] = mesh.point(v);

    // Edge/vertex constraint maps.
    auto eif = mesh.add_property_map<TMesh::Edge_index, bool>(
                   "e:is_feature_tmp", false).first;
    auto vif = mesh.add_property_map<TMesh::Vertex_index, bool>(
                   "v:is_feature_tmp", false).first;

    int boundary_edges_count = 0;
    int sharp_edges_count    = 0;
    for (auto e : mesh.edges()) if (mesh.is_border(e)) ++boundary_edges_count;

    try {
        if (cfg.preserve_sharp_edges) {
            PMP::detect_sharp_edges(mesh, cfg.sharp_angle_degrees, eif);
            for (auto e : mesh.edges()) {
                if (!eif[e]) continue;
                ++sharp_edges_count;
                vif[mesh.vertex(e, 0)] = true;
                vif[mesh.vertex(e, 1)] = true;
            }
        }
        if (cfg.preserve_boundary) {
            for (auto e : mesh.edges()) {
                if (mesh.is_border(e)) {
                    eif[e] = true;
                    vif[mesh.vertex(e, 0)] = true;
                    vif[mesh.vertex(e, 1)] = true;
                }
            }
        }
    } catch (const std::exception& e) {
        impl_->set_error(std::string("feature detection failed: ") + e.what());
        impl_->done_flag = true;
        return;
    }

    compute_stats(mesh, impl_->stats.before,
                  boundary_edges_count, sharp_edges_count);
    const auto t_pre_end = Clock::now();
    impl_->stats.ms_preprocess =
        std::chrono::duration<double, std::milli>(
            t_pre_end - t_pre_begin).count();

    if (impl_->cancel_flag.load()) {
        impl_->stats.cancelled = true;
        impl_->done_flag = true;
        return;
    }

    const auto t_sm_begin = Clock::now();
    try {
        const unsigned int nb_iter = (unsigned)std::max(1, cfg.iterations);

        // One CGAL iteration of the selected mode. Called either nb_iter
        // times (live mode) or wrapped in a single nb_iter call (fast mode).
        auto run_n_iter = [&](unsigned int n) {
            switch (cfg.mode) {
                case SMOOTH_MODE_TangentialRelaxation:
                    PMP::tangential_relaxation(mesh,
                        params::number_of_iterations(n)
                              .edge_is_constrained_map(eif)
                              .vertex_is_constrained_map(vif)
                              .relax_constraints(cfg.relax_constraints));
                    break;
                case SMOOTH_MODE_AngleSmoothing:
                    PMP::angle_and_area_smoothing(mesh,
                        params::number_of_iterations(n)
                              .use_angle_smoothing(true)
                              .use_area_smoothing(false)
                              .use_safety_constraints(cfg.safety_constraints)
                              .do_project(cfg.project_to_original)
                              .edge_is_constrained_map(eif)
                              .vertex_is_constrained_map(vif));
                    break;
                case SMOOTH_MODE_MeanCurvatureFlow:
                    PMP::smooth_shape(mesh, cfg.time_step,
                        params::number_of_iterations(n)
                              .vertex_is_constrained_map(vif)
                              .do_scale(cfg.rescale_after_smoothing));
                    break;
                default:
                    break;
            }
        };

        if (cfg.live_preview) {
            // Live mode: 1 CGAL iter at a time, publish snapshot after
            // each, check cancel between iterations. NB the Angle mode
            // rebuilds its projection tree each call, so single-iter * N
            // is not strictly identical to N-iter * 1 (plan section 15).
            //
            // Force-publish every iteration. latest_snapshot is a single
            // buffer, so the cfg.snapshot_min_ms gate would otherwise collapse
            // fast runs into only the first and final visible states.
            //
            // Slow mode: worker sleeps a few hundred ms between iters so
            // the UI thread has time to actually render each snapshot.
            // Normal mode: a short sleep so 60 fps redraws can catch each
            // iter on tiny meshes.
            const int per_iter_pause_ms =
                (cfg.preview_speed == SMOOTH_PREVIEW_Slow) ? 280 : 80;
            for (unsigned int i = 1; i <= nb_iter; ++i) {
                if (impl_->cancel_flag.load()) break;
                run_n_iter(1);
                impl_->stats.iterations_done = (int)i;
                publish_smoothing_snapshot(*impl_, mesh, original,
                                           (int)i, (int)nb_iter,
                                           /*force=*/true);
                if (i < nb_iter && per_iter_pause_ms > 0)
                    interruptible_sleep(per_iter_pause_ms, impl_->cancel_flag);
            }
        } else {
            // Fast mode: one CGAL call, no snapshots.
            run_n_iter(nb_iter);
            impl_->stats.iterations_done = (int)nb_iter;
        }
    } catch (const std::exception& e) {
        impl_->set_error(std::string("CGAL smoothing exception: ") + e.what());
    } catch (...) {
        impl_->set_error("CGAL smoothing unknown exception");
    }
    const auto t_sm_end = Clock::now();
    impl_->stats.ms_smoothing =
        std::chrono::duration<double, std::milli>(
            t_sm_end - t_sm_begin).count();

    // Compute after-stats + displacement, even if there was an error.
    compute_stats(mesh, impl_->stats.after,
                  boundary_edges_count, sharp_edges_count);
    impl_->stats.closed_mesh = impl_->stats.after.closed;
    if (impl_->stats.before.surface_area > 0.0) {
        impl_->stats.surface_area_ratio =
            impl_->stats.after.surface_area / impl_->stats.before.surface_area;
    }
    if (impl_->stats.before.closed && impl_->stats.after.closed &&
        impl_->stats.before.volume != 0.0) {
        impl_->stats.volume_ratio =
            impl_->stats.after.volume / impl_->stats.before.volume;
    }

    // Displacement.
    double sum_d = 0.0, max_d = 0.0;
    int counted = 0;
    impl_->result_displacement.assign(mesh.number_of_vertices(), 0.0);
    for (auto v : mesh.vertices()) {
        if (v.idx() >= (TMesh::size_type)original.size()) continue;
        const auto& a = original[v.idx()];
        const auto& b = mesh.point(v);
        const double dx = CGAL::to_double(b.x()) - CGAL::to_double(a.x());
        const double dy = CGAL::to_double(b.y()) - CGAL::to_double(a.y());
        const double dz = CGAL::to_double(b.z()) - CGAL::to_double(a.z());
        const double d = std::sqrt(dx*dx + dy*dy + dz*dz);
        impl_->result_displacement[v.idx()] = d;
        sum_d += d;
        if (d > max_d) max_d = d;
        ++counted;
    }
    impl_->stats.mean_displacement = (counted > 0) ? (sum_d / counted) : 0.0;
    impl_->stats.max_displacement  = max_d;

    // Convert CGAL mesh back to POD result.
    impl_->result_verts.clear();
    impl_->result_verts.reserve(mesh.number_of_vertices());
    std::vector<int> idx_remap(mesh.number_of_vertices(), -1);
    int new_idx = 0;
    for (auto v : mesh.vertices()) {
        idx_remap[v.idx()] = new_idx++;
        impl_->result_verts.push_back(to_pod(mesh.point(v)));
    }
    impl_->result_tris.clear();
    impl_->result_tris.reserve(mesh.number_of_faces());
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        int v0 = idx_remap[mesh.target(h).idx()]; h = mesh.next(h);
        int v1 = idx_remap[mesh.target(h).idx()]; h = mesh.next(h);
        int v2 = idx_remap[mesh.target(h).idx()];
        if (v0 < 0 || v1 < 0 || v2 < 0) continue;
        impl_->result_tris.push_back(SMOOTH_Triangle{v0, v1, v2});
    }

    impl_->stats.cancelled = impl_->cancel_flag.load();
    const auto t_total_end = Clock::now();
    impl_->stats.ms_total =
        std::chrono::duration<double, std::milli>(
            t_total_end - t_total_begin).count();
    impl_->done_flag = true;
}

void SmoothingRunner::cancel() { impl_->cancel_flag = true; }

bool SmoothingRunner::is_cancelled() const { return impl_->cancel_flag.load(); }
bool SmoothingRunner::is_done()      const { return impl_->done_flag.load(); }
bool SmoothingRunner::has_error()    const { return impl_->has_error_flag.load(); }

std::string SmoothingRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void SmoothingRunner::set_error(const std::string& msg) {
    impl_->set_error(msg);
}

bool SmoothingRunner::poll_snapshot(int& last_generation,
                                    SMOOTH_Snapshot& out) const {
    const int g = impl_->snap_generation.load();
    if (g <= last_generation) return false;
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    out = impl_->latest_snapshot;
    last_generation = g;
    return true;
}

void SmoothingRunner::get_result(std::vector<SMOOTH_Point3d>&  out_verts,
                                 std::vector<SMOOTH_Triangle>& out_tris) const {
    out_verts = impl_->result_verts;
    out_tris  = impl_->result_tris;
}

SMOOTH_ResultStats SmoothingRunner::result_stats() const {
    return impl_->stats;
}

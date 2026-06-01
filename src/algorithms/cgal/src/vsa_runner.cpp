// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "vsa_runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Random.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Variational_shape_approximation.h>
#include <CGAL/Surface_mesh_approximation/L2_metric_plane_proxy.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>

namespace PMP    = CGAL::Polygon_mesh_processing;
namespace VSA_NS = CGAL::Surface_mesh_approximation;
namespace params = CGAL::parameters;

using Kernel           = CGAL::Exact_predicates_inexact_constructions_kernel;
using FT               = Kernel::FT;
using Point_3          = Kernel::Point_3;
using Vector_3         = Kernel::Vector_3;
using TMesh            = CGAL::Surface_mesh<Point_3>;
using face_descriptor  = boost::graph_traits<TMesh>::face_descriptor;
using halfedge_descriptor = boost::graph_traits<TMesh>::halfedge_descriptor;
using Vertex_point_map = boost::property_map<TMesh, boost::vertex_point_t>::type;
using Clock            = std::chrono::steady_clock;

struct VSARunner::Impl {
    std::vector<VSA_Point3d>  input_verts;
    std::vector<VSA_Triangle> input_tris;
    bool has_input = false;

    VSA_Config cfg{};

    std::vector<VSA_Point3d>  result_verts;
    std::vector<VSA_Triangle> result_tris;

    std::vector<int>           face_proxy_ids;
    std::vector<VSA_ProxyInfo> proxies_info;

    VSA_Snapshot latest_snapshot{};
    std::atomic<int> snap_generation{0};
    mutable std::mutex snap_mutex;

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    VSA_DebugStats stats{};

    // Throttle: wall-clock time of the last published snapshot (worker
    // thread only; no atomic needed). publish_live_snapshot skips if the
    // gap is shorter than cfg.snapshot_min_ms unless force_publish=true.
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
        face_proxy_ids.clear();
        proxies_info.clear();
        {
            std::lock_guard<std::mutex> lk(snap_mutex);
            latest_snapshot = VSA_Snapshot{};
        }
        snap_generation = 0;
        stats = VSA_DebugStats{};
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

VSA_NS::Seeding_method map_seeding(int s) {
    switch (s) {
        case VSA_SEED_Random:       return VSA_NS::RANDOM;
        case VSA_SEED_Incremental:  return VSA_NS::INCREMENTAL;
        case VSA_SEED_Hierarchical:
        default:                    return VSA_NS::HIERARCHICAL;
    }
}

VSA_Point3d to_pod(const Point_3& p) {
    return VSA_Point3d{CGAL::to_double(p.x()),
                       CGAL::to_double(p.y()),
                       CGAL::to_double(p.z())};
}

VSA_Point3d to_pod(const Vector_3& v) {
    return VSA_Point3d{CGAL::to_double(v.x()),
                       CGAL::to_double(v.y()),
                       CGAL::to_double(v.z())};
}

bool build_input_mesh(const std::vector<VSA_Point3d>&  in_verts,
                      const std::vector<VSA_Triangle>& in_tris,
                      TMesh& mesh,
                      std::vector<face_descriptor>& fid_to_fd,
                      std::string& err) {
    mesh.clear();
    if (in_verts.empty() || in_tris.empty()) {
        err = "empty input mesh";
        return false;
    }
    const int nv = static_cast<int>(in_verts.size());
    std::vector<TMesh::Vertex_index> vmap;
    vmap.reserve(nv);
    for (const VSA_Point3d& p : in_verts) {
        vmap.push_back(mesh.add_vertex(Point_3(p.x, p.y, p.z)));
    }
    fid_to_fd.clear();
    fid_to_fd.reserve(in_tris.size());
    for (const VSA_Triangle& t : in_tris) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
            t.v0 >= nv || t.v1 >= nv || t.v2 >= nv ||
            t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) {
            err = "invalid triangle indices";
            return false;
        }
        face_descriptor fd =
            mesh.add_face(vmap[t.v0], vmap[t.v1], vmap[t.v2]);
        if (fd == TMesh::null_face()) {
            // Try reversed orientation; if still bad we leave the input
            // hole as null and skip; the runner reports a clear error
            // later if too many faces drop.
            fd = mesh.add_face(vmap[t.v0], vmap[t.v2], vmap[t.v1]);
        }
        fid_to_fd.push_back(fd);
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        err = "input is not a triangle mesh after conversion";
        return false;
    }
    return true;
}

template <typename Approximation>
void collect_final_state(Approximation& approx,
                         const TMesh& mesh,
                         const std::vector<face_descriptor>& fid_to_fd,
                         std::vector<int>& face_proxy_ids,
                         std::vector<VSA_ProxyInfo>& proxies_info) {
    using F2P_tag = CGAL::dynamic_face_property_t<std::size_t>;
    auto f2p = get(F2P_tag(), const_cast<TMesh&>(mesh));
    for (face_descriptor fd : faces(mesh)) {
        put(f2p, fd, static_cast<std::size_t>(-1));
    }
    approx.proxy_map(f2p);

    face_proxy_ids.assign(fid_to_fd.size(), -1);
    for (std::size_t i = 0; i < fid_to_fd.size(); ++i) {
        face_descriptor fd = fid_to_fd[i];
        if (fd == TMesh::null_face()) continue;
        const std::size_t pid = get(f2p, fd);
        if (pid == static_cast<std::size_t>(-1)) continue;
        face_proxy_ids[i] = static_cast<int>(pid);
    }

    using Proxy_wrapper = typename Approximation::Proxy_wrapper;
    std::vector<Proxy_wrapper> wrapped;
    approx.wrapped_proxies(std::back_inserter(wrapped));

    Vertex_point_map vpmap =
        get(boost::vertex_point, const_cast<TMesh&>(mesh));

    proxies_info.clear();
    proxies_info.reserve(wrapped.size());
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        VSA_ProxyInfo info;
        info.id    = static_cast<int>(wrapped[i].idx);
        info.error = CGAL::to_double(wrapped[i].err);

        const face_descriptor sf = wrapped[i].seed;
        if (sf != TMesh::null_face()) {
            // Map back to source face index if possible (linear scan, fine
            // for small proxy counts; proxies are typically <500).
            int source_idx = -1;
            for (std::size_t j = 0; j < fid_to_fd.size(); ++j) {
                if (fid_to_fd[j] == sf) { source_idx = static_cast<int>(j); break; }
            }
            info.seed_face = source_idx;

            const halfedge_descriptor he = halfedge(sf, mesh);
            const Point_3& p0 = vpmap[source(he, mesh)];
            const Point_3& p1 = vpmap[target(he, mesh)];
            const Point_3& p2 =
                vpmap[target(next(he, mesh), mesh)];
            info.seed_center = to_pod(CGAL::centroid(p0, p1, p2));
            Vector_3 n = CGAL::cross_product(p1 - p0, p2 - p0);
            const double L = std::sqrt(CGAL::to_double(n.squared_length()));
            if (L > 0.0) {
                info.normal = VSA_Point3d{
                    CGAL::to_double(n.x()) / L,
                    CGAL::to_double(n.y()) / L,
                    CGAL::to_double(n.z()) / L};
            }
        }
        proxies_info.push_back(info);
    }
}

void publish_final_snapshot(VSARunner::Impl& impl) {
    std::lock_guard<std::mutex> lk(impl.snap_mutex);
    impl.latest_snapshot.generation     = impl.snap_generation.load() + 1;
    impl.latest_snapshot.phase          = 2; // final
    impl.latest_snapshot.iteration      = impl.cfg.iterations;
    impl.latest_snapshot.proxies        = impl.stats.final_proxies;
    impl.latest_snapshot.target_proxies = impl.cfg.target_proxies;
    impl.latest_snapshot.faces          =
        static_cast<int>(impl.face_proxy_ids.size());
    impl.latest_snapshot.total_error    = impl.stats.final_error;
    impl.latest_snapshot.error_drop     = 0.0;
    impl.latest_snapshot.face_proxy_ids = impl.face_proxy_ids;
    impl.latest_snapshot.proxies_info   = impl.proxies_info;
    impl.snap_generation.store(impl.latest_snapshot.generation);
}

// Live snapshot: reads the current proxy_map + wrapped_proxies and
// publishes them as a VSA_Snapshot. Called after initialize_seeds (phase=0)
// and after every run(1) (phase=1). face_proxy_ids is parallel to the source
// input triangle array so the UI can recolor without re-uploading geometry.
//
// Throttled by cfg.snapshot_min_ms. force_publish overrides the gate so
// the very last snapshot of seeding / iteration always reaches the UI.
template <typename Approximation>
void publish_live_snapshot(VSARunner::Impl& impl,
                           Approximation& approx,
                           const TMesh& mesh,
                           const std::vector<face_descriptor>& fid_to_fd,
                           int iteration_index,
                           int phase,
                           double prev_error,
                           bool force_publish = false)
{
    if (!force_publish && impl.cfg.snapshot_min_ms > 0 &&
        impl.last_publish_time != Clock::time_point{})
    {
        const auto now = Clock::now();
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - impl.last_publish_time).count();
        if (since < impl.cfg.snapshot_min_ms) return;
    }
    impl.last_publish_time = Clock::now();

    VSA_Snapshot snap;
    snap.phase          = phase;
    snap.iteration      = iteration_index;
    snap.proxies        = static_cast<int>(approx.number_of_proxies());
    snap.target_proxies = impl.cfg.target_proxies;
    snap.faces          = static_cast<int>(fid_to_fd.size());
    const double cur_err = CGAL::to_double(approx.compute_total_error());
    snap.total_error    = cur_err;
    snap.error_drop     = (prev_error > 1e-30)
                              ? (prev_error - cur_err) / prev_error
                              : 0.0;

    using F2P_tag = CGAL::dynamic_face_property_t<std::size_t>;
    auto f2p = get(F2P_tag(), const_cast<TMesh&>(mesh));
    for (face_descriptor fd : faces(mesh))
        put(f2p, fd, static_cast<std::size_t>(-1));
    approx.proxy_map(f2p);

    snap.face_proxy_ids.assign(fid_to_fd.size(), -1);
    for (std::size_t i = 0; i < fid_to_fd.size(); ++i) {
        face_descriptor fd = fid_to_fd[i];
        if (fd == TMesh::null_face()) continue;
        const std::size_t pid = get(f2p, fd);
        if (pid == static_cast<std::size_t>(-1)) continue;
        snap.face_proxy_ids[i] = static_cast<int>(pid);
    }

    using Proxy_wrapper = typename Approximation::Proxy_wrapper;
    std::vector<Proxy_wrapper> wrapped;
    approx.wrapped_proxies(std::back_inserter(wrapped));

    Vertex_point_map vpmap =
        get(boost::vertex_point, const_cast<TMesh&>(mesh));
    snap.proxies_info.reserve(wrapped.size());
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        VSA_ProxyInfo info;
        info.id    = static_cast<int>(wrapped[i].idx);
        info.error = CGAL::to_double(wrapped[i].err);
        const face_descriptor sf = wrapped[i].seed;
        if (sf != TMesh::null_face()) {
            const halfedge_descriptor he = halfedge(sf, mesh);
            const Point_3& p0 = vpmap[source(he, mesh)];
            const Point_3& p1 = vpmap[target(he, mesh)];
            const Point_3& p2 = vpmap[target(next(he, mesh), mesh)];
            info.seed_center = to_pod(CGAL::centroid(p0, p1, p2));
        }
        snap.proxies_info.push_back(info);
    }

    const int new_gen = impl.snap_generation.load() + 1;
    snap.generation = new_gen;
    {
        std::lock_guard<std::mutex> lk(impl.snap_mutex);
        impl.latest_snapshot = std::move(snap);
    }
    impl.snap_generation.store(new_gen);
}

template <typename Approximation>
void run_pipeline(VSARunner::Impl& impl,
                  TMesh& mesh,
                  const std::vector<face_descriptor>& fid_to_fd,
                  Approximation& approx,
                  const VSA_NS::Seeding_method sm) {
    const auto t_seed_begin = Clock::now();
    if (impl.cfg.live_preview && impl.cfg.staged_growth &&
        impl.cfg.target_proxies > 8)
    {
        // Staged growth: seed with a small initial set, then expand in
        // batches with a few relaxations per batch. The user sees small
        // colored islands appear, then more islands, then the patches
        // expand and compete. Numerically not identical to single-shot
        // initialize_seeds, which is why this is a live-only playback
        // option (per plan section 6).
        const int initial = std::min(8, impl.cfg.target_proxies);
        approx.initialize_seeds(
            params::seeding_method(VSA_NS::HIERARCHICAL)
                  .max_number_of_proxies(static_cast<std::size_t>(initial))
                  .number_of_relaxations(
                      static_cast<std::size_t>(impl.cfg.relaxations)));
        impl.stats.initial_error =
            CGAL::to_double(approx.compute_total_error());
        publish_live_snapshot(impl, approx, mesh, fid_to_fd,
                              /*iteration_index=*/0, /*phase=*/0,
                              impl.stats.initial_error,
                              /*force_publish=*/true);

        const int batch = std::max(1, impl.cfg.target_proxies / 20);
        const int relax_per_add = std::max(1, impl.cfg.relaxations / 2);
        int current = static_cast<int>(approx.number_of_proxies());
        while (current < impl.cfg.target_proxies) {
            if (impl.cancel_flag.load()) break;
            const int remaining = impl.cfg.target_proxies - current;
            const int to_add = std::min(batch, remaining);
            const std::size_t added = approx.add_to_furthest_proxies(
                static_cast<std::size_t>(to_add),
                static_cast<std::size_t>(relax_per_add));
            if (added == 0) break;
            current = static_cast<int>(approx.number_of_proxies());
            const double cur_err =
                CGAL::to_double(approx.compute_total_error());
            publish_live_snapshot(impl, approx, mesh, fid_to_fd,
                                  /*iteration_index=*/0, /*phase=*/0,
                                  cur_err);
        }
        impl.stats.seeds_inserted =
            static_cast<int>(approx.number_of_proxies());
    } else {
        std::size_t seeds = approx.initialize_seeds(
            params::seeding_method(sm)
                  .max_number_of_proxies(
                      static_cast<std::size_t>(impl.cfg.target_proxies))
                  .number_of_relaxations(
                      static_cast<std::size_t>(impl.cfg.relaxations))
                  .min_error_drop(impl.cfg.use_error_drop
                                      ? FT(impl.cfg.min_error_drop)
                                      : FT(0.0)));
        impl.stats.seeds_inserted = static_cast<int>(seeds);
        impl.stats.initial_error =
            CGAL::to_double(approx.compute_total_error());
    }
    const auto t_seed_end = Clock::now();
    impl.stats.ms_seeding =
        std::chrono::duration<double, std::milli>(
            t_seed_end - t_seed_begin).count();

    if (impl.cancel_flag.load()) return;

    // Publish a seeding-phase snapshot so the UI sees colored cells the
    // moment seeding finishes, before the relaxation iterations start.
    // Force-publish so this final-seeding state always reaches the UI,
    // even if the throttle window would otherwise drop it.
    if (impl.cfg.live_preview) {
        publish_live_snapshot(impl, approx, mesh, fid_to_fd,
                              /*iteration_index=*/0, /*phase=*/0,
                              /*prev_error=*/impl.stats.initial_error,
                              /*force_publish=*/true);
    }

    const auto t_it_begin = Clock::now();
    if (impl.cfg.iterations > 0) {
        if (impl.cfg.live_preview) {
            // Per-iteration loop so the UI can poll snapshots and the
            // cancel flag is checked between iterations.
            double prev_err = impl.stats.initial_error;
            FT err = FT(prev_err);
            int done = 0;
            for (int i = 0; i < impl.cfg.iterations; ++i) {
                if (impl.cancel_flag.load()) break;
                err = approx.run(1);
                done = i + 1;
                const double cur = CGAL::to_double(err);
                impl.stats.final_error = cur;
                impl.stats.iterations_completed = done;
                // Force-publish on the very last iteration so the UI
                // always sees the final state regardless of the throttle.
                const bool is_last = (i + 1 == impl.cfg.iterations);
                publish_live_snapshot(impl, approx, mesh, fid_to_fd,
                                      done, /*phase=*/1, prev_err,
                                      /*force_publish=*/is_last);
                prev_err = cur;
            }
        } else {
            FT err = approx.run(
                static_cast<std::size_t>(impl.cfg.iterations));
            impl.stats.final_error = CGAL::to_double(err);
            impl.stats.iterations_completed = impl.cfg.iterations;
        }
    } else {
        impl.stats.final_error =
            CGAL::to_double(approx.compute_total_error());
    }
    const auto t_it_end = Clock::now();
    impl.stats.ms_iteration =
        std::chrono::duration<double, std::milli>(
            t_it_end - t_it_begin).count();
    impl.stats.final_proxies =
        static_cast<int>(approx.number_of_proxies());

    if (impl.cancel_flag.load()) return;

    collect_final_state(approx, mesh, fid_to_fd,
                        impl.face_proxy_ids, impl.proxies_info);

    if (!impl.cfg.extract_mesh) {
        impl.stats.extracted_mesh  = false;
        impl.stats.manifold_output = false;
        return;
    }

    const auto t_ex_begin = Clock::now();
    bool manifold = approx.extract_mesh(params::default_values());

    std::vector<Point_3> anchors;
    std::vector<std::array<std::size_t, 3>> tris;
    approx.output(params::anchors(std::back_inserter(anchors))
                       .triangles(std::back_inserter(tris)));
    const auto t_ex_end = Clock::now();
    impl.stats.ms_extraction =
        std::chrono::duration<double, std::milli>(
            t_ex_end - t_ex_begin).count();
    impl.stats.extracted_mesh  = true;
    impl.stats.manifold_output = manifold;

    if (anchors.empty() || tris.empty()) {
        impl.result_verts.clear();
        impl.result_tris.clear();
        return;
    }

    const auto t_co_begin = Clock::now();
    if (manifold) {
        PMP::orient_polygon_soup(anchors, tris);
        TMesh out_mesh;
        PMP::polygon_soup_to_polygon_mesh(anchors, tris, out_mesh);
        if (CGAL::is_closed(out_mesh) &&
            !PMP::is_outward_oriented(out_mesh)) {
            PMP::reverse_face_orientations(out_mesh);
        }

        Vertex_point_map vp =
            get(boost::vertex_point, const_cast<TMesh&>(out_mesh));
        std::vector<TMesh::Vertex_index> vlist;
        vlist.reserve(out_mesh.number_of_vertices());
        impl.result_verts.clear();
        impl.result_verts.reserve(out_mesh.number_of_vertices());
        for (auto v : vertices(out_mesh)) {
            vlist.push_back(v);
            impl.result_verts.push_back(to_pod(vp[v]));
        }
        // Vertex_index in a fresh Surface_mesh is a dense [0..N) integer,
        // so we can use vid_to_index via index conversion.
        impl.result_tris.clear();
        impl.result_tris.reserve(out_mesh.number_of_faces());
        for (auto fd : faces(out_mesh)) {
            halfedge_descriptor he = halfedge(fd, out_mesh);
            const TMesh::Vertex_index v0 = source(he, out_mesh);
            const TMesh::Vertex_index v1 = target(he, out_mesh);
            const TMesh::Vertex_index v2 =
                target(next(he, out_mesh), out_mesh);
            impl.result_tris.push_back(
                VSA_Triangle{static_cast<int>(v0),
                             static_cast<int>(v1),
                             static_cast<int>(v2)});
        }
    } else {
        // Non-manifold: keep the raw triangle soup so the caller can still
        // visualize what came out, but warn via stats.
        impl.result_verts.clear();
        impl.result_verts.reserve(anchors.size());
        for (const Point_3& p : anchors) impl.result_verts.push_back(to_pod(p));
        impl.result_tris.clear();
        impl.result_tris.reserve(tris.size());
        for (const auto& t : tris) {
            impl.result_tris.push_back(
                VSA_Triangle{static_cast<int>(t[0]),
                             static_cast<int>(t[1]),
                             static_cast<int>(t[2])});
        }
    }
    const auto t_co_end = Clock::now();
    impl.stats.ms_convert_out =
        std::chrono::duration<double, std::milli>(
            t_co_end - t_co_begin).count();
    impl.stats.final_vertices = static_cast<int>(impl.result_verts.size());
    impl.stats.final_faces    = static_cast<int>(impl.result_tris.size());
}

} // namespace

VSARunner::VSARunner()  : impl_(std::make_unique<Impl>()) {}
VSARunner::~VSARunner() = default;

void VSARunner::set_input(const std::vector<VSA_Point3d>&  verts,
                          const std::vector<VSA_Triangle>& tris) {
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void VSARunner::run(const VSA_Config& cfg) {
    impl_->reset_state();
    impl_->cfg = cfg;

    if (!impl_->has_input) {
        impl_->set_error("no input mesh set");
        impl_->done_flag = true;
        return;
    }

    const auto t_total_begin = Clock::now();

    const auto t_conv_begin = Clock::now();
    TMesh mesh;
    std::vector<face_descriptor> fid_to_fd;
    std::string build_err;
    if (!build_input_mesh(impl_->input_verts, impl_->input_tris,
                          mesh, fid_to_fd, build_err)) {
        impl_->set_error(build_err);
        impl_->done_flag = true;
        return;
    }
    const auto t_conv_end = Clock::now();
    impl_->stats.ms_convert_in =
        std::chrono::duration<double, std::milli>(
            t_conv_end - t_conv_begin).count();

    impl_->stats.initial_vertices = static_cast<int>(mesh.number_of_vertices());
    impl_->stats.initial_faces    = static_cast<int>(mesh.number_of_faces());
    impl_->stats.target_proxies   = cfg.target_proxies;

    {
        auto fcm = mesh.add_property_map<TMesh::Face_index, std::size_t>(
                        "f:cc_tmp", 0).first;
        impl_->stats.initial_components =
            static_cast<int>(PMP::connected_components(mesh, fcm));
        mesh.remove_property_map(fcm);
    }

    CGAL::get_default_random() = CGAL::Random(cfg.random_seed);
    Vertex_point_map vpmap =
        get(boost::vertex_point, const_cast<TMesh&>(mesh));
    const VSA_NS::Seeding_method sm = map_seeding(cfg.seeding);

    try {
        if (cfg.metric == VSA_METRIC_L2) {
            using L2_metric = VSA_NS::L2_metric_plane_proxy<TMesh>;
            L2_metric metric(mesh, vpmap);
            CGAL::Variational_shape_approximation<TMesh, Vertex_point_map,
                                                  L2_metric>
                approx(mesh, vpmap, metric);
            run_pipeline(*impl_, mesh, fid_to_fd, approx, sm);
        } else {
            using Approx =
                CGAL::Variational_shape_approximation<TMesh, Vertex_point_map>;
            Approx::Error_metric metric(mesh, vpmap);
            Approx approx(mesh, vpmap, metric);
            run_pipeline(*impl_, mesh, fid_to_fd, approx, sm);
        }
    } catch (const std::exception& e) {
        impl_->set_error(std::string("CGAL exception: ") + e.what());
    } catch (...) {
        impl_->set_error("CGAL unknown exception");
    }

    impl_->stats.cancelled = impl_->cancel_flag.load();
    if (!impl_->has_error_flag.load()) {
        publish_final_snapshot(*impl_);
    }

    const auto t_total_end = Clock::now();
    impl_->stats.ms_total =
        std::chrono::duration<double, std::milli>(
            t_total_end - t_total_begin).count();
    impl_->done_flag = true;
}

void VSARunner::cancel() { impl_->cancel_flag = true; }

bool VSARunner::is_cancelled() const { return impl_->cancel_flag.load(); }
bool VSARunner::is_done()      const { return impl_->done_flag.load(); }
bool VSARunner::has_error()    const { return impl_->has_error_flag.load(); }

std::string VSARunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void VSARunner::set_error(const std::string& msg) {
    impl_->set_error(msg);
}

void VSARunner::get_result(std::vector<VSA_Point3d>&  out_verts,
                           std::vector<VSA_Triangle>& out_tris) const {
    out_verts = impl_->result_verts;
    out_tris  = impl_->result_tris;
}

void VSARunner::get_face_proxy_ids(std::vector<int>& out) const {
    out = impl_->face_proxy_ids;
}

void VSARunner::get_proxies(std::vector<VSA_ProxyInfo>& out) const {
    out = impl_->proxies_info;
}

bool VSARunner::poll_snapshot(int& last_generation, VSA_Snapshot& out) const {
    const int g = impl_->snap_generation.load();
    if (g <= last_generation) return false;
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    out = impl_->latest_snapshot;
    last_generation = g;
    return true;
}

VSA_DebugStats VSARunner::debug_stats() const { return impl_->stats; }

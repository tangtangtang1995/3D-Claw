// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "planar_patch_remeshing_runner.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <deque>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/remesh_planar_patches.h>
#include <CGAL/Polygon_mesh_processing/region_growing.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>

#include <boost/property_map/vector_property_map.hpp>

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using CGAL_Mesh = CGAL::Surface_mesh<Kernel::Point_3>;
namespace PMP = CGAL::Polygon_mesh_processing;
using Clock = std::chrono::steady_clock;

struct PlanarPatchRemeshingRunner::Impl {
    std::vector<PPR_Point3d>  in_verts;
    std::vector<PPR_Triangle>  in_tris;
    std::vector<int>           in_face_patch_ids;
    bool has_input = false;

    PPR_Config cfg;
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done{false};
    std::atomic<bool> has_error_flag{false};
    std::string error_msg;
    std::mutex error_mutex;

    // Result
    std::vector<PPR_Point3d>  out_verts;
    std::vector<PPR_Triangle>  out_tris;
    std::vector<int>           out_face_patch_ids;
    std::mutex result_mutex;

    // Snapshot
    mutable std::mutex snap_mutex;
    PPR_Snapshot latest_snapshot;
    mutable std::deque<PPR_Snapshot> snapshot_queue;
    int snap_generation = 0;
    // Worker-side throttle: skip publish if last one was less than
    // cfg.snapshot_min_ms ago, unless force is true. Early structural snapshots
    // always force-publish since they fire at most twice per run anyway.
    Clock::time_point last_publish_time{};

    // Stats
    PPR_DebugStats stats;
};

PlanarPatchRemeshingRunner::PlanarPatchRemeshingRunner()
    : impl_(std::make_unique<Impl>()) {}
PlanarPatchRemeshingRunner::~PlanarPatchRemeshingRunner() = default;

void PlanarPatchRemeshingRunner::set_input(
    const std::vector<PPR_Point3d>& verts,
    const std::vector<PPR_Triangle>& tris) {
    impl_->in_verts = verts;
    impl_->in_tris  = tris;
    impl_->in_face_patch_ids.clear();
    impl_->has_input = true;
}

void PlanarPatchRemeshingRunner::set_face_patch_ids(
    const std::vector<int>& face_patch_ids) {
    impl_->in_face_patch_ids = face_patch_ids;
}

void PlanarPatchRemeshingRunner::cancel() {
    impl_->cancel_flag = true;
}
bool PlanarPatchRemeshingRunner::is_cancelled() const {
    return impl_->cancel_flag.load(std::memory_order_relaxed);
}
bool PlanarPatchRemeshingRunner::is_done() const {
    return impl_->done.load(std::memory_order_relaxed);
}
bool PlanarPatchRemeshingRunner::has_error() const {
    return impl_->has_error_flag.load(std::memory_order_relaxed);
}
std::string PlanarPatchRemeshingRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_msg;
}
void PlanarPatchRemeshingRunner::set_error(const std::string& msg) {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    impl_->has_error_flag = true;
    impl_->error_msg = msg;
}

bool PlanarPatchRemeshingRunner::poll_snapshot(int& last_gen, PPR_Snapshot& out) const {
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    while (!impl_->snapshot_queue.empty() &&
           impl_->snapshot_queue.front().generation <= last_gen) {
        impl_->snapshot_queue.pop_front();
    }
    if (impl_->snapshot_queue.empty()) return false;
    out = impl_->snapshot_queue.front();
    last_gen = out.generation;
    impl_->snapshot_queue.pop_front();
    return true;
}

bool PlanarPatchRemeshingRunner::has_pending_snapshot(int last_gen) const {
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    for (const auto& snap : impl_->snapshot_queue) {
        if (snap.generation > last_gen)
            return true;
    }
    return false;
}

void PlanarPatchRemeshingRunner::get_result(
    std::vector<PPR_Point3d>& verts,
    std::vector<PPR_Triangle>& tris,
    std::vector<int>& face_patch_ids) const {
    std::lock_guard<std::mutex> lk(impl_->result_mutex);
    verts = impl_->out_verts;
    tris  = impl_->out_tris;
    face_patch_ids = impl_->out_face_patch_ids;
}

PPR_DebugStats PlanarPatchRemeshingRunner::debug_stats() const {
    return impl_->stats;
}

namespace {

int normalize_face_patch_ids(
    const std::vector<int>& input,
    int expected_faces,
    std::vector<int>& output,
    std::string& error)
{
    if ((int)input.size() != expected_faces) {
        error = "Existing Labels mode requires one face label per input face.";
        return 0;
    }

    std::unordered_map<int, int> remap;
    output.resize(expected_faces);
    int next_id = 0;
    for (int i = 0; i < expected_faces; ++i) {
        const int raw = input[i];
        if (raw < 0) {
            error = "Existing Labels mode found an unlabeled face.";
            return 0;
        }
        auto it = remap.find(raw);
        if (it == remap.end())
            it = remap.emplace(raw, next_id++).first;
        output[i] = it->second;
    }
    return next_id;
}

void compute_patch_normals(
    const CGAL_Mesh& sm,
    const std::vector<int>& face_patch_ids,
    int nb_patches,
    std::vector<Kernel::Vector_3>& normals)
{
    normals.assign(nb_patches, Kernel::Vector_3(0, 0, 0));
    for (auto f : sm.faces()) {
        const int fi = (int)f.idx();
        if (fi < 0 || fi >= (int)face_patch_ids.size()) continue;
        const int pid = face_patch_ids[fi];
        if (pid < 0 || pid >= nb_patches) continue;

        auto h = sm.halfedge(f);
        const auto& p0 = sm.point(sm.target(h));
        const auto& p1 = sm.point(sm.target(sm.next(h)));
        const auto& p2 = sm.point(sm.target(sm.next(sm.next(h))));
        normals[pid] = normals[pid] +
            CGAL::cross_product(p1 - p0, p2 - p0);
    }

    for (auto& n : normals) {
        const double len2 = CGAL::to_double(n.squared_length());
        if (len2 > 1e-24)
            n = n / std::sqrt(len2);
        else
            n = Kernel::Vector_3(0, 0, 1);
    }
}

// Build + publish a phase snapshot. Reads worker-owned property maps and
// the CGAL mesh; called only from the worker thread. UI thread polls via
// poll_snapshot under impl->snap_mutex.
//
// rids:  face_index -> patch id (or -1 if unassigned). Empty -> skip patches.
// cids:  vertex_index -> corner id (or (size_t)-1 if not a corner). Empty
//        -> skip corner_points.
// ecm:   edge_index -> bool (constrained?). Empty -> skip edges.
//
// All three are addressed by the dense face/vertex/edge .idx() into the
// freshly-built CGAL_Mesh, which matches the source POD ordering since
// build_input_mesh add_vertex/add_face in source order.
void publish_phase_snapshot(
    PlanarPatchRemeshingRunner::Impl& impl,
    const CGAL_Mesh& sm,
    int phase,
    int patches_count,
    int corners_count,
    int constrained_edges_count,
    const std::vector<int>* rids,
    const std::vector<std::size_t>* cids,
    const std::vector<bool>* ecm,
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

    PPR_Snapshot snap;
    snap.phase = phase;
    snap.patches = patches_count;
    snap.corners = corners_count;
    snap.constrained_edges = constrained_edges_count;

    if (rids && !rids->empty()) {
        const int nf = (int)impl.in_tris.size();
        snap.face_patch_ids.assign(nf, -1);
        for (int i = 0; i < nf && i < (int)rids->size(); ++i) {
            const int pid = (*rids)[i];
            if (pid >= 0) snap.face_patch_ids[i] = pid;
        }
    }

    if (cids && !cids->empty()) {
        for (auto v : sm.vertices()) {
            const int vi = (int)v.idx();
            if (vi >= (int)cids->size()) continue;
            const std::size_t cid = (*cids)[vi];
            if (cid == (std::size_t)-1) continue;
            const auto& p = sm.point(v);
            snap.corner_points.push_back({CGAL::to_double(p.x()),
                                          CGAL::to_double(p.y()),
                                          CGAL::to_double(p.z())});
        }
    }

    if (ecm && !ecm->empty()) {
        snap.constrained_edge_endpoints.reserve(2 * constrained_edges_count);
        for (auto e : sm.edges()) {
            const int ei = (int)e.idx();
            if (ei >= (int)ecm->size() || !(*ecm)[ei]) continue;
            const auto& p0 = sm.point(sm.vertex(e, 0));
            const auto& p1 = sm.point(sm.vertex(e, 1));
            snap.constrained_edge_endpoints.push_back({
                CGAL::to_double(p0.x()), CGAL::to_double(p0.y()),
                CGAL::to_double(p0.z())});
            snap.constrained_edge_endpoints.push_back({
                CGAL::to_double(p1.x()), CGAL::to_double(p1.y()),
                CGAL::to_double(p1.z())});
        }
    }

    {
        std::lock_guard<std::mutex> lk(impl.snap_mutex);
        impl.snap_generation += 1;
        snap.generation = impl.snap_generation;
        impl.latest_snapshot = snap;
        impl.snapshot_queue.push_back(std::move(snap));
        while (impl.snapshot_queue.size() > 180)
            impl.snapshot_queue.pop_front();
    }
}

// publish_patch_progress: per-patch phase 3 snapshot during remeshing.
void publish_patch_progress(
    PlanarPatchRemeshingRunner::Impl& impl,
    const CGAL_Mesh& sm,
    int processed, int current_patch, int fallback_count,
    int nb_patches, int nb_corners, int nb_constrained_edges,
    const std::vector<int>& rids,
    const std::vector<std::size_t>& cids,
    const std::vector<bool>& ecm,
    const std::vector<unsigned char>& processed_patch_flags,
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

    PPR_Snapshot snap;
    snap.phase = 3;
    snap.patches = nb_patches;
    snap.corners = nb_corners;
    snap.constrained_edges = nb_constrained_edges;
    snap.processed_patches = processed;
    snap.current_patch_id = current_patch;
    snap.fallback_patches = fallback_count;
    snap.progress = nb_patches > 0 ? (double)processed / (double)nb_patches : 0.0;

    const int nf = (int)rids.size();
    snap.face_patch_ids.resize(nf);
    for (int i = 0; i < nf; ++i) {
        const int pid = rids[i];
        const bool is_done =
            pid >= 0 &&
            pid < (int)processed_patch_flags.size() &&
            processed_patch_flags[pid] != 0;
        snap.face_patch_ids[i] = is_done ? pid : -1;
    }

    int nv = (int)sm.number_of_vertices();
    if (!cids.empty()) {
        for (int vi = 0; vi < nv && vi < (int)cids.size(); ++vi) {
            if (cids[vi] == (std::size_t)-1) continue;
            const auto& p = sm.point(CGAL_Mesh::Vertex_index(vi));
            snap.corner_points.push_back({CGAL::to_double(p.x()),
                                          CGAL::to_double(p.y()),
                                          CGAL::to_double(p.z())});
        }
    }
    if (!ecm.empty()) {
        int ne = (int)sm.number_of_edges();
        snap.constrained_edge_endpoints.reserve(2 * nb_constrained_edges);
        for (int ei = 0; ei < ne && ei < (int)ecm.size(); ++ei) {
            if (!ecm[ei]) continue;
            auto e = CGAL_Mesh::Edge_index(ei);
            const auto& p0 = sm.point(sm.vertex(e, 0));
            const auto& p1 = sm.point(sm.vertex(e, 1));
            snap.constrained_edge_endpoints.push_back({CGAL::to_double(p0.x()), CGAL::to_double(p0.y()), CGAL::to_double(p0.z())});
            snap.constrained_edge_endpoints.push_back({CGAL::to_double(p1.x()), CGAL::to_double(p1.y()), CGAL::to_double(p1.z())});
        }
    }

    {
        std::lock_guard<std::mutex> lk(impl.snap_mutex);
        impl.snap_generation += 1;
        snap.generation = impl.snap_generation;
        impl.latest_snapshot = snap;
        impl.snapshot_queue.push_back(std::move(snap));
        while (impl.snapshot_queue.size() > 180)
            impl.snapshot_queue.pop_front();
    }
}

// RecordingVisitor: inherits CGAL Default_visitor so the existing
// visitor(pm_out) operator() stays as no-op. Receives per-patch callbacks
// from the locally patched remesh_planar_patches.h and publishes phase 3
// snapshots.
struct RecordingVisitor
    : public CGAL::Polygon_mesh_processing::Planar_segmentation::
        Default_visitor<CGAL_Mesh>
{
    PlanarPatchRemeshingRunner::Impl* impl = nullptr;
    const CGAL_Mesh* sm = nullptr;
    const std::vector<int>* rids = nullptr;
    const std::vector<std::size_t>* cids = nullptr;
    const std::vector<bool>* ecm = nullptr;
    mutable int nb_patches = 0, nb_corners = 0, nb_constrained_edges = 0;
    mutable int processed = 0, current_patch = -1, fallback_count = 0;
    mutable std::vector<unsigned char> processed_patch_flags;

    void on_patch_detection_done(std::size_t np, std::size_t nc, std::size_t nce) const {
        nb_patches = (int)np; nb_corners = (int)nc; nb_constrained_edges = (int)nce;
        processed_patch_flags.assign(nb_patches, 0);
    }
    void on_patch_begin(std::size_t pid, std::size_t nf, std::size_t nb) const {
        (void)nf;
        (void)nb;
        current_patch = (int)pid;
    }
    void on_patch_triangulated(std::size_t pid, std::size_t nf) const {
        (void)pid;
        (void)nf;
    }
    void on_patch_fallback(std::size_t pid, int reason) const {
        (void)pid;
        (void)reason;
        ++fallback_count;
    }
    void on_patch_end(std::size_t pid) const {
        const int next_processed = processed + 1;
        if ((int)pid >= 0) {
            if ((int)processed_patch_flags.size() <= (int)pid)
                processed_patch_flags.resize((int)pid + 1, 0);
            processed_patch_flags[(int)pid] = 1;
        }
        processed = next_processed;
        if (impl && sm && rids && cids && ecm) {
            const int step =
                nb_patches > 120 ? std::max(1, nb_patches / 120) : 1;
            const bool force =
                processed == 1 || processed >= nb_patches ||
                (step > 1 && processed % step == 0);
            bool should_publish = force || step == 1;
            if (should_publish) {
                publish_patch_progress(*impl, *sm,
                    processed, current_patch, fallback_count,
                    nb_patches, nb_corners, nb_constrained_edges,
                    *rids, *cids, *ecm, processed_patch_flags, force);
            }
        }
    }
    bool go_further() const {
        return impl && !impl->cancel_flag.load(std::memory_order_relaxed);
    }
};

} // namespace

void PlanarPatchRemeshingRunner::run(const PPR_Config& cfg) {
    // Full reset: every Run() must start from a blank-slate impl_ state
    // so repeated runs don't leak stale errors, stale snapshots, or stale
    // result geometry from the previous run.
    {
        std::lock_guard<std::mutex> lk(impl_->snap_mutex);
        impl_->latest_snapshot = PPR_Snapshot{};
        impl_->snapshot_queue.clear();
        impl_->snap_generation = 0;
    }
    impl_->last_publish_time = Clock::time_point{};
    impl_->cancel_flag = false;
    impl_->done = false;
    impl_->has_error_flag = false;
    {
        std::lock_guard<std::mutex> lk(impl_->error_mutex);
        impl_->error_msg.clear();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        impl_->out_verts.clear();
        impl_->out_tris.clear();
        impl_->out_face_patch_ids.clear();
    }
    impl_->stats = PPR_DebugStats{};

    if (!impl_->has_input || impl_->in_verts.empty()) {
        set_error("No input mesh");
        impl_->done = true;
        return;
    }
    impl_->cfg = cfg;

    try {

    const int nv = (int)impl_->in_verts.size();
    const int nf = (int)impl_->in_tris.size();
    const int ne = nv + nf - 2; // Euler for triangle mesh
    impl_->stats.input_vertices = nv;
    impl_->stats.input_faces    = nf;
    impl_->stats.input_edges    = ne;

    // Build CGAL mesh
    CGAL_Mesh sm;
    std::vector<CGAL_Mesh::Vertex_index> cvs;
    cvs.reserve(nv);
    for (int i = 0; i < nv; ++i) {
        auto& p = impl_->in_verts[i];
        cvs.push_back(sm.add_vertex(Kernel::Point_3(p.x, p.y, p.z)));
    }
    for (int i = 0; i < nf; ++i) {
        auto& t = impl_->in_tris[i];
        sm.add_face(cvs[t.v0], cvs[t.v1], cvs[t.v2]);
    }
    if (!CGAL::is_triangle_mesh(sm))
        PMP::triangulate_faces(sm);

    // Bbox for auto distance
    Kernel::FT bxmin, bxmax, bymin, bymax, bzmin, bzmax;
    bxmin = bymin = bzmin = 1e100;
    bxmax = bymax = bzmax = -1e100;
    for (auto& p : impl_->in_verts) {
        if (p.x < bxmin) bxmin = p.x; if (p.x > bxmax) bxmax = p.x;
        if (p.y < bymin) bymin = p.y; if (p.y > bymax) bymax = p.y;
        if (p.z < bzmin) bzmin = p.z; if (p.z > bzmax) bzmax = p.z;
    }
    const double bbox_diag = std::sqrt(
        (bxmax - bxmin) * (bxmax - bxmin) +
        (bymax - bymin) * (bymax - bymin) +
        (bzmax - bzmin) * (bzmax - bzmin));
    impl_->stats.bbox_diag = bbox_diag;

    const double max_dist = cfg.max_distance > 0 ? cfg.max_distance
        : bbox_diag * cfg.distance_ratio;

    const auto t0 = Clock::now();

    if (cfg.mode == PPR_ExactPlanar) {
        auto ifpm = sm.add_property_map<CGAL_Mesh::Face_index, int>("f:ip", -1).first;
        for (auto f : sm.faces()) ifpm[f] = -1;


        // Live visitor for exact planar (no region-growing data)
        std::vector<int> empty_rids;
        std::vector<std::size_t> empty_cids;
        std::vector<bool> empty_ecm;
        RecordingVisitor visitor;
        visitor.impl = impl_.get();
        visitor.sm = &sm;
        visitor.rids = &empty_rids;
        visitor.cids = &empty_cids;
        visitor.ecm = &empty_ecm;

        CGAL_Mesh out;
        auto ofpm = out.add_property_map<CGAL_Mesh::Face_index, int>("f:op", -1).first;
        PMP::remesh_planar_patches(sm, out,
            CGAL::parameters::cosine_of_maximum_angle(cfg.cosine_threshold)
                .face_patch_map(ifpm),
            CGAL::parameters::face_patch_map(ofpm)
                .do_not_triangulate_faces(false)
                .visitor(visitor));

        const auto t1 = Clock::now();
        impl_->stats.ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
        impl_->stats.ms_remeshing = impl_->stats.ms_total;

        // Extract result
        {
            std::lock_guard<std::mutex> lk(impl_->result_mutex);
            impl_->out_verts.clear();
            impl_->out_tris.clear();
            impl_->out_face_patch_ids.clear();
            for (auto v : out.vertices()) {
                auto& p = out.point(v);
                impl_->out_verts.push_back({CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())});
            }
            for (auto f : out.faces()) {
                auto h = out.halfedge(f);
                int v0 = (int)out.target(h).idx();
                int v1 = (int)out.target(out.next(h)).idx();
                int v2 = (int)out.target(out.next(out.next(h))).idx();
                impl_->out_tris.push_back({v0, v1, v2});
                impl_->out_face_patch_ids.push_back(ofpm[f]);
            }
            impl_->stats.output_vertices = (int)out.number_of_vertices();
            impl_->stats.output_faces    = (int)out.number_of_faces();
        }

        // Count patches
        int np = 0;
        for (auto f : out.faces()) { int pid = ofpm[f]; if (pid > np) np = pid; }
        impl_->stats.patches = np > 0 ? np + 1 : 0;
        impl_->stats.corners = 0;
        impl_->stats.constrained_edges = 0;
        impl_->stats.all_patches_remeshed = true;

    } else if (cfg.mode == PPR_ExistingLabels) {
        std::vector<int> rids;
        std::string label_error;
        const int nb_regions = normalize_face_patch_ids(
            impl_->in_face_patch_ids, nf, rids, label_error);
        if (nb_regions <= 0)
            throw std::runtime_error(label_error);

        std::vector<std::size_t> cids(num_vertices(sm), (std::size_t)-1);
        std::vector<bool> ecm(num_edges(sm), false);
        std::vector<Kernel::Vector_3> patch_normals;
        compute_patch_normals(sm, rids, nb_regions, patch_normals);
        impl_->stats.ms_region_growing = 0.0;

        std::size_t nb_corners = PMP::detect_corners_of_regions(sm,
            CGAL::make_random_access_property_map(rids), nb_regions,
            CGAL::make_random_access_property_map(cids),
            CGAL::parameters::cosine_of_maximum_angle(cfg.cosine_threshold)
                .maximum_distance(max_dist)
                .edge_is_constrained_map(CGAL::make_random_access_property_map(ecm)));

        int nb_ce = 0;
        for (auto e : sm.edges()) if (ecm[(int)e.idx()]) ++nb_ce;

        auto t_c = Clock::now();
        impl_->stats.ms_corner_detection =
            std::chrono::duration<double,std::milli>(t_c-t0).count();

        if (cfg.live_preview) {
            publish_phase_snapshot(*impl_, sm,
                /*phase=*/2, nb_regions, (int)nb_corners, nb_ce,
                /*rids=*/nullptr, &cids, &ecm,
                /*force=*/true);
        }
        if (impl_->cancel_flag.load()) {
            impl_->stats.cancelled = true;
            impl_->done = true;
            return;
        }

        RecordingVisitor visitor;
        visitor.impl = impl_.get();
        visitor.sm = &sm;
        visitor.rids = &rids;
        visitor.cids = &cids;
        visitor.ecm = &ecm;
        visitor.nb_patches = nb_regions;
        visitor.nb_corners = (int)nb_corners;
        visitor.nb_constrained_edges = nb_ce;

        CGAL_Mesh out;
        auto ofpm =
            out.add_property_map<CGAL_Mesh::Face_index, int>("f:op", -1).first;
        const bool remesh_ok = PMP::remesh_almost_planar_patches(sm, out,
            nb_regions, nb_corners,
            CGAL::make_random_access_property_map(rids),
            CGAL::make_random_access_property_map(cids),
            CGAL::make_random_access_property_map(ecm),
            CGAL::parameters::patch_normal_map(
                CGAL::make_random_access_property_map(patch_normals)),
            CGAL::parameters::face_patch_map(ofpm)
                .do_not_triangulate_faces(false)
                .visitor(visitor));

        auto t1 = Clock::now();
        impl_->stats.ms_remeshing =
            std::chrono::duration<double,std::milli>(t1-t_c).count();
        impl_->stats.ms_total =
            std::chrono::duration<double,std::milli>(t1-t0).count();

        impl_->stats.patches = nb_regions;
        impl_->stats.corners = (int)nb_corners;
        impl_->stats.constrained_edges = nb_ce;
        impl_->stats.all_patches_remeshed = remesh_ok;

        {
            std::lock_guard<std::mutex> lk(impl_->result_mutex);
            impl_->out_verts.clear();
            impl_->out_tris.clear();
            impl_->out_face_patch_ids.clear();
            for (auto v : out.vertices()) {
                auto& p = out.point(v);
                impl_->out_verts.push_back({CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())});
            }
            for (auto f : out.faces()) {
                auto h = out.halfedge(f);
                int v0 = (int)out.target(h).idx();
                int v1 = (int)out.target(out.next(h)).idx();
                int v2 = (int)out.target(out.next(out.next(h))).idx();
                impl_->out_tris.push_back({v0, v1, v2});
                impl_->out_face_patch_ids.push_back(ofpm[f]);
            }
            impl_->stats.output_vertices = (int)out.number_of_vertices();
            impl_->stats.output_faces = (int)out.number_of_faces();
        }

    } else { // AlmostPlanar
        // Region growing
        std::vector<int> rids(num_faces(sm), -1);
        std::vector<std::size_t> cids(num_vertices(sm), (std::size_t)-1);
        std::vector<bool> ecm(num_edges(sm), false);
        boost::vector_property_map<Kernel::Vector_3> nmap;

        std::size_t nb_regions = PMP::region_growing_of_planes_on_faces(sm,
            CGAL::make_random_access_property_map(rids),
            CGAL::parameters::cosine_of_maximum_angle(cfg.cosine_threshold)
                .region_primitive_map(nmap)
                .maximum_distance(max_dist)
                .postprocess_regions(cfg.postprocess_regions));

        auto t_rg = Clock::now();
        impl_->stats.ms_region_growing = std::chrono::duration<double,std::milli>(t_rg-t0).count();

        // Intentionally do not publish patch colors here. Revealing them this
        // early makes the live preview look backwards (fully colored, then
        // re-darkened). Keep this stage quiet; phase 2 shows structural cues,
        // and phase 3 owns the color fill.
        if (impl_->cancel_flag.load()) {
            impl_->stats.cancelled = true;
            impl_->done = true;
            return;
        }

        // Corner detection
        std::size_t nb_corners = PMP::detect_corners_of_regions(sm,
            CGAL::make_random_access_property_map(rids), nb_regions,
            CGAL::make_random_access_property_map(cids),
            CGAL::parameters::cosine_of_maximum_angle(cfg.cosine_threshold)
                .maximum_distance(max_dist)
                .edge_is_constrained_map(CGAL::make_random_access_property_map(ecm)));

        int nb_ce = 0;
        for (auto e : sm.edges()) if (ecm[(int)e.idx()]) ++nb_ce;

        auto t_c = Clock::now();
        impl_->stats.ms_corner_detection = std::chrono::duration<double,std::milli>(t_c-t_rg).count();

        // Structural snapshot: publish cues only. Do not publish rids here;
        // patch colors are reserved for the remeshing animation.
        if (cfg.live_preview) {
            publish_phase_snapshot(*impl_, sm,
                /*phase=*/2, (int)nb_regions, (int)nb_corners, nb_ce,
                /*rids=*/nullptr, &cids, &ecm,
                /*force=*/true);
        }
        if (impl_->cancel_flag.load()) {
            impl_->stats.cancelled = true;
            impl_->done = true;
            return;
        }


        // Live visitor with full almost-planar data
        RecordingVisitor visitor;
        visitor.impl = impl_.get();
        visitor.sm = &sm;
        visitor.rids = &rids;
        visitor.cids = &cids;
        visitor.ecm = &ecm;
        visitor.nb_patches = (int)nb_regions;
        visitor.nb_corners = (int)nb_corners;
        visitor.nb_constrained_edges = nb_ce;

        // Remesh
        CGAL_Mesh out;
        auto ofpm =
            out.add_property_map<CGAL_Mesh::Face_index, int>("f:op", -1).first;
        PMP::remesh_almost_planar_patches(sm, out,
            nb_regions, nb_corners,
            CGAL::make_random_access_property_map(rids),
            CGAL::make_random_access_property_map(cids),
            CGAL::make_random_access_property_map(ecm),
            CGAL::parameters::patch_normal_map(nmap),
            CGAL::parameters::face_patch_map(ofpm)
                .do_not_triangulate_faces(false)
                .visitor(visitor));

        auto t1 = Clock::now();
        impl_->stats.ms_remeshing = std::chrono::duration<double,std::milli>(t1-t_c).count();
        impl_->stats.ms_total = std::chrono::duration<double,std::milli>(t1-t0).count();

        impl_->stats.patches = (int)nb_regions;
        impl_->stats.corners = (int)nb_corners;
        impl_->stats.constrained_edges = nb_ce;
        impl_->stats.all_patches_remeshed = true;

        // Extract result
        {
            std::lock_guard<std::mutex> lk(impl_->result_mutex);
            impl_->out_verts.clear();
            impl_->out_tris.clear();
            impl_->out_face_patch_ids.clear();
            for (auto v : out.vertices()) {
                auto& p = out.point(v);
                impl_->out_verts.push_back({CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())});
            }
            for (auto f : out.faces()) {
                auto h = out.halfedge(f);
                int v0 = (int)out.target(h).idx();
                int v1 = (int)out.target(out.next(h)).idx();
                int v2 = (int)out.target(out.next(out.next(h))).idx();
                impl_->out_tris.push_back({v0, v1, v2});
                impl_->out_face_patch_ids.push_back(ofpm[f]);
            }
            impl_->stats.output_vertices = (int)out.number_of_vertices();
            impl_->stats.output_faces = (int)out.number_of_faces();
        }
    }

    impl_->stats.output_edges = impl_->stats.output_vertices + impl_->stats.output_faces - 2;
    if (nf > 0) impl_->stats.compression_ratio = 1.0
        - (double)impl_->stats.output_faces / (double)nf;

    } catch (const std::exception& e) {
        set_error(std::string("CGAL exception: ") + e.what());
    } catch (...) {
        set_error("CGAL unknown exception");
    }

    impl_->done = true;
}

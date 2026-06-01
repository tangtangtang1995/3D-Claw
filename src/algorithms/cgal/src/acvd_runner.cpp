// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "acvd_runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <unordered_map>

#include <CGAL/assertions_behaviour.h>
#include <CGAL/exceptions.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/approximated_centroidal_Voronoi_diagram_remeshing.h>
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/shape_predicates.h>

namespace PMP = CGAL::Polygon_mesh_processing;
namespace params = CGAL::parameters;

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using TMesh   = CGAL::Surface_mesh<Point_3>;

// ==========================================================================
// Impl
// ==========================================================================

struct ACVDRunner::Impl {
    // POD input
    std::vector<ACVD_Point3d>  input_verts;
    std::vector<ACVD_Triangle> input_tris;
    bool has_input = false;

    ACVD_Config cfg{};

    // Result
    std::vector<ACVD_Point3d>  result_verts;
    std::vector<ACVD_Triangle> result_tris;

    // State
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    // Timing
    std::atomic<double> ms_total_{0};
    std::atomic<double> ms_preprocess_{0};
    std::atomic<double> ms_clustering_{0};
    std::atomic<double> ms_output_{0};

    // Running stats
    std::atomic<int> working_vertices_{0};
    std::atomic<int> final_vertices_{0};
    std::atomic<int> final_faces_{0};
    std::atomic<int> loops_{0};
    std::atomic<int> iterations_{0};
    std::atomic<int> modifications_{0};
    std::atomic<int> assigned_vertices_{0};
    std::atomic<int> disconnected_fixes_{0};

    // Live event queue
    std::deque<ACVD_FrameEvent> live_queue;
    std::mutex live_mutex;

    // Cluster snapshot (consumed by the overlay)
    std::vector<ACVD_Point3d>  snap_verts;
    std::vector<ACVD_Triangle> snap_tris;
    std::vector<int>           snap_cluster_ids;
    std::vector<ACVD_Point3d>  snap_centers;
    std::atomic<int>           snap_generation{0};
    mutable std::mutex         snap_mutex;

    // Initial seed positions (written by RecordingVisitor::on_seed_created,
    // read by UI via get_seed_positions). Captured once per run, never
    // overwritten, unlike snap_centers which jitter as cluster membership
    // changes.
    std::vector<ACVD_Point3d>  seed_positions;
    mutable std::mutex         seeds_mutex;

    void reset_state() {
        cancel_flag = false;
        done_flag   = false;
        has_error_flag = false;
        { std::lock_guard<std::mutex> lk(error_mutex); error_message.clear(); }
        { std::lock_guard<std::mutex> lk(live_mutex);  live_queue.clear(); }
        { std::lock_guard<std::mutex> lk(snap_mutex);
          snap_verts.clear(); snap_tris.clear();
          snap_cluster_ids.clear(); snap_centers.clear(); }
        { std::lock_guard<std::mutex> lk(seeds_mutex);
          seed_positions.clear(); }
        snap_generation = 0;
        result_verts.clear();
        result_tris.clear();
        ms_total_     = 0;
        ms_preprocess_ = 0;
        ms_clustering_ = 0;
        ms_output_     = 0;
        working_vertices_ = 0;
        final_vertices_   = 0;
        final_faces_      = 0;
        loops_ = 0; iterations_ = 0; modifications_ = 0;
        assigned_vertices_ = 0; disconnected_fixes_ = 0;
    }
};

// ==========================================================================
// RecordingVisitor: captures ACVD process events
// ==========================================================================

struct RecordingVisitor : public CGAL::Polygon_mesh_processing::ACVD_default_visitor
{
    ACVDRunner::Impl* impl = nullptr;
    const std::vector<TMesh::Vertex_index>* source_vd_map = nullptr;
    int batch_counter = 0;
    int throttled_assign_count = 0;
    std::chrono::steady_clock::time_point last_snapshot_time{};
    bool has_snapshot_time = false;

    // Cheap throttle: push only every N vertex assignments.
    bool throttle_push() {
        const int iv = std::max(1, impl->cfg.event_interval);
        return (++batch_counter % iv) == 0;
    }

    template <typename M>
    void on_acvd_begin(const M&, int target) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Init;
        ev.target_vertices = target;
        push(ev);
    }

    template <typename M>
    void on_preprocessing_begin(const M&) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::PreprocessBegin;
        push(ev);
    }

    template <typename M>
    void on_preprocessing_end(const M&) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::PreprocessEnd;
        push(ev);
    }

    template <typename V, typename Pt>
    void on_seed_created(int id, V, const Pt& p) {
        // Capture the seed coordinate. With the current CGAL ACVD call site
        // this fires once per initial cluster at the start of clustering;
        // accumulating into seed_positions lets the UI show the seeds dwell
        // for the whole run (and the settle phase afterwards), not just for
        // the brief moment between this callback and the first snapshot.
        {
            std::lock_guard<std::mutex> lk(impl->seeds_mutex);
            impl->seed_positions.push_back({
                CGAL::to_double(p.x()),
                CGAL::to_double(p.y()),
                CGAL::to_double(p.z())});
        }
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Seed;
        ev.current_clusters = id + 1;
        ev.progress = 0.05;
        push(ev);
    }

    void on_clustering_loop_begin(int loop, bool qem) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Progress;
        ev.loop = loop; ev.qem_energy = qem;
        ev.target_vertices = impl->cfg.target_vertices;
        push(ev);
    }

    template <typename M, typename Pmap, typename V, typename Pt>
    void on_vertex_assigned(const M& mesh, const Pmap& cluster_pmap,
                            V, int from, int to, const Pt&) {
        throttled_assign_count++;
        if (!throttle_push()) return;
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::AssignmentBatch;
        ev.modifications = throttled_assign_count;
        impl->assigned_vertices_.store(
            impl->assigned_vertices_.load() + throttled_assign_count,
            std::memory_order_relaxed);
        throttled_assign_count = 0;

        // ACVD can assign the whole working mesh inside a single iteration.
        // If snapshots are only published at iteration boundaries, the UI
        // only ever sees the fully colored result. Publish throttled partial
        // snapshots during the assignment wave so unassigned regions remain
        // gray and the colored cells visibly spread over the surface.
        const auto now = std::chrono::steady_clock::now();
        int min_ms = std::max(0, impl->cfg.snapshot_min_ms);
        if (mesh.number_of_faces() > (std::size_t)std::max(1, impl->cfg.max_snapshot_faces))
            min_ms = std::max(min_ms, 500);
        const bool time_ok =
            !has_snapshot_time ||
            std::chrono::duration<double, std::milli>(
                now - last_snapshot_time).count() >= min_ms;
        if (impl->cfg.live_preview && time_ok) {
            ev.current_clusters = impl->cfg.target_vertices;
            ev.target_vertices = impl->cfg.target_vertices;
            build_cluster_snapshot(mesh, cluster_pmap,
                                   impl->cfg.target_vertices, ev);
            last_snapshot_time = now;
            has_snapshot_time = true;
        }
        push(ev);
    }

    template <typename M, typename Pmap>
    void on_iteration_end(const M& mesh, const Pmap& cluster_pmap,
                          int loop, int iter, int mods,
                          int assigned, int nb_clusters, bool qem) {
        impl->loops_.store(loop, std::memory_order_relaxed);
        impl->iterations_.store(iter, std::memory_order_relaxed);
        impl->modifications_.store(mods, std::memory_order_relaxed);
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::IterationEnd;
        ev.loop = loop; ev.iteration = iter;
        ev.modifications = mods;
        ev.assigned_vertices = assigned;
        ev.current_clusters = nb_clusters;
        ev.qem_energy = qem;
        ev.target_vertices = impl->cfg.target_vertices;
        // Publish cluster-color snapshot at each iteration boundary.
        // The pmap is the EXACT one ACVD writes to (passed through the
        // patched callback), so reading it here returns the real cluster
        // ids. Calling get(dynamic_vertex_property_t<int>(), mesh, -1) from
        // outside the algorithm returns a fresh storage that never sees
        // ACVD's writes; that was the previous "all-purple" bug.
        build_cluster_snapshot(mesh, cluster_pmap, nb_clusters, ev);
        push(ev);
    }

    template <typename M, typename Pmap>
    void build_cluster_snapshot(const M& mesh, const Pmap& cluster_pmap,
                                int nb_clusters, ACVD_FrameEvent& parent) {
        const auto& vpm = mesh.points();
        const std::size_t mesh_faces = mesh.number_of_faces();
        const int max_faces_cfg = impl->cfg.max_snapshot_faces;
        const std::size_t max_faces = (max_faces_cfg > 0)
            ? (std::size_t)max_faces_cfg
            : (std::numeric_limits<std::size_t>::max)();
        const bool sampled = mesh_faces > max_faces;
        const std::size_t face_stride = sampled
            ? (std::max<std::size_t>)(1, (mesh_faces + max_faces - 1) / max_faces)
            : 1;
        const bool use_source_proxy =
            sampled &&
            source_vd_map &&
            source_vd_map->size() == impl->input_verts.size() &&
            !impl->input_verts.empty() &&
            !impl->input_tris.empty() &&
            impl->input_tris.size() <= max_faces;
        std::unordered_map<unsigned, int> v2i;
        std::vector<ACVD_Point3d> verts;
        std::vector<int> cluster_ids;

        if (use_source_proxy) {
            verts = impl->input_verts;
            cluster_ids.assign(verts.size(), -1);
            for (std::size_t i = 0; i < source_vd_map->size(); ++i) {
                const auto vd = (*source_vd_map)[i];
                if (vd != TMesh::null_vertex())
                    cluster_ids[i] = get(cluster_pmap, vd);
            }
        } else if (!sampled) {
            verts.reserve(mesh.number_of_vertices());
            cluster_ids.reserve(mesh.number_of_vertices());
            for (auto v : mesh.vertices()) {
                int cid = get(cluster_pmap, v);
                const auto& p = get(vpm, v);
                v2i[(unsigned)v] = (int)verts.size();
                verts.push_back({CGAL::to_double(p.x()),
                                 CGAL::to_double(p.y()),
                                 CGAL::to_double(p.z())});
                cluster_ids.push_back(cid);
            }
        } else {
            const std::size_t reserve_faces =
                (std::min<std::size_t>)(mesh_faces, max_faces);
            verts.reserve(reserve_faces * 3);
            cluster_ids.reserve(reserve_faces * 3);
        }
        // Include EVERY face. For boundary faces (3 verts in different
        // clusters) pick the majority vertex cluster id; if all three are
        // distinct, fall back to vertex 0. -1 (unassigned vertex) is a
        // legitimate id at this point in the algorithm; render it as the
        // "still gray" portion. The earlier implementation dropped all
        // mixed-cluster faces, which made early snapshots look like a near-
        // empty mesh while clusters were still spreading.
        std::vector<ACVD_Triangle> tris;
        tris.reserve(use_source_proxy ? impl->input_tris.size()
                                      : (sampled ? max_faces : mesh_faces));
        std::vector<int> face_ids;
        face_ids.reserve(use_source_proxy ? impl->input_tris.size()
                                          : (sampled ? max_faces : mesh_faces));
        std::size_t face_counter = 0;
        if (use_source_proxy) {
            for (const auto& t : impl->input_tris) {
                const int nv = (int)cluster_ids.size();
                if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
                    t.v0 >= nv || t.v1 >= nv || t.v2 >= nv)
                    continue;
                const std::array<int, 3> vcid{
                    cluster_ids[t.v0], cluster_ids[t.v1], cluster_ids[t.v2]};
                int cid;
                if (vcid[0] == vcid[1] || vcid[0] == vcid[2])      cid = vcid[0];
                else if (vcid[1] == vcid[2])                       cid = vcid[1];
                else                                               cid = vcid[0];
                tris.push_back(t);
                face_ids.push_back(cid);
            }
        } else {
          for (auto f : mesh.faces()) {
            if (sampled && ((face_counter++ % face_stride) != 0))
                continue;
            int k = 0;
            std::array<int, 3> idx{-1, -1, -1};
            std::array<int, 3> vcid{-1, -1, -1};
            for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
                if (k >= 3) break;
                if (sampled) {
                    const unsigned key = (unsigned)v;
                    auto it = v2i.find(key);
                    if (it == v2i.end()) {
                        const auto& p = get(vpm, v);
                        const int compact = (int)verts.size();
                        it = v2i.emplace(key, compact).first;
                        verts.push_back({CGAL::to_double(p.x()),
                                         CGAL::to_double(p.y()),
                                         CGAL::to_double(p.z())});
                        cluster_ids.push_back(get(cluster_pmap, v));
                    }
                    idx[k] = it->second;
                } else {
                    idx[k] = v2i[(unsigned)v];
                }
                vcid[k] = (idx[k] >= 0 && idx[k] < (int)cluster_ids.size())
                    ? cluster_ids[idx[k]] : -1;
                ++k;
            }
            if (k != 3 || idx[0] < 0 || idx[1] < 0 || idx[2] < 0)
                continue;
            // Majority-of-three: any two-vote wins, else vertex 0's id.
            int cid;
            if (vcid[0] == vcid[1] || vcid[0] == vcid[2])      cid = vcid[0];
            else if (vcid[1] == vcid[2])                       cid = vcid[1];
            else                                               cid = vcid[0];
            tris.push_back({idx[0], idx[1], idx[2]});
            face_ids.push_back(cid);
          }
        }

        // Cluster centers (quick approximation: first vertex per cluster).
        std::vector<ACVD_Point3d> centers;
        std::vector<int> seen(nb_clusters, -1);
        if (!sampled || nb_clusters <= 20000) {
            for (size_t i = 0; i < cluster_ids.size() && i < verts.size(); ++i) {
                int c = cluster_ids[i];
                if (c >= 0 && c < nb_clusters && seen[c] < 0) {
                    seen[c] = (int)i;
                    centers.push_back(verts[i]);
                }
            }
        }

        const int gen = impl->snap_generation.load(
            std::memory_order_relaxed) + 1;
        {
            std::lock_guard<std::mutex> lk(impl->snap_mutex);
            impl->snap_verts       = std::move(verts);
            impl->snap_tris        = std::move(tris);
            impl->snap_cluster_ids = std::move(face_ids);
            impl->snap_centers     = std::move(centers);
        }
        impl->snap_generation.store(gen, std::memory_order_release);
        parent.snapshot_generation = gen;
    }

    void on_disconnected_clusters_fixed(int loop, int disc) {
        impl->disconnected_fixes_.store(disc, std::memory_order_relaxed);
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::ClusterRepair;
        ev.loop = loop; ev.disconnected_clusters = disc;
        push(ev);
    }

    void on_output_soup_built(std::size_t pts, std::size_t polys,
                               std::size_t /*nm*/) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::OutputSoup;
        ev.output_vertices = (int)pts;
        ev.output_faces    = (int)polys;
        push(ev);
    }

    void on_non_manifold_repair_begin(std::size_t bad) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Progress;
        ev.non_manifold_clusters = (int)bad;
        push(ev);
    }

    void on_non_manifold_repair_end(int new_count) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Progress;
        ev.current_clusters = new_count;
        push(ev);
    }

    template <typename M>
    void on_acvd_end(const M&, bool success) {
        ACVD_FrameEvent ev{}; ev.type = ACVD_FrameEvent::Done;
        push(ev);
    }

    template <typename M>
    bool go_further(const M&) {
        return !impl->cancel_flag.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t MAX_LIVE = 8192;

    void push(ACVD_FrameEvent& ev) {
        ev.total_vertices = impl->cfg.target_vertices;
        std::lock_guard<std::mutex> lk(impl->live_mutex);
        if (impl->live_queue.size() >= MAX_LIVE)
            impl->live_queue.pop_front();
        impl->live_queue.push_back(ev);
    }

    void push_snapshot(ACVD_FrameEvent& parent) {
        // Publish raw cluster-map snapshot for the overlay.
        // For now just bump generation; actual centroids are not yet filled.
        int gen = impl->snap_generation.fetch_add(1,
            std::memory_order_release) + 1;
        parent.type = ACVD_FrameEvent::IterationEnd;
        parent.snapshot_generation = gen;
    }
};

// ==========================================================================
// Mesh conversion
// ==========================================================================

namespace {

class CgalFailureBehaviourGuard {
public:
    CgalFailureBehaviourGuard()
        : old_error_(CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION))
    {
    }

    ~CgalFailureBehaviourGuard()
    {
        CGAL::set_error_behaviour(old_error_);
    }

private:
    CGAL::Failure_behaviour old_error_;
};

void build_cgal_mesh(TMesh& out,
                     std::vector<TMesh::Vertex_index>& vd_map,
                     const std::vector<ACVD_Point3d>&  verts,
                     const std::vector<ACVD_Triangle>& tris)
{
    out.clear();
    vd_map.clear();
    vd_map.reserve(verts.size());
    for (const auto& p : verts) {
        auto vd = out.add_vertex(Point_3(p.x, p.y, p.z));
        vd_map.push_back(vd);
    }
    const int nv = (int)vd_map.size();
    for (const auto& t : tris) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        if (t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) continue;
        out.add_face(vd_map[t.v0], vd_map[t.v1], vd_map[t.v2]);
    }
}

void emit_pod_mesh(const TMesh& mesh,
                   std::vector<ACVD_Point3d>&  out_verts,
                   std::vector<ACVD_Triangle>& out_tris)
{
    out_verts.clear();
    out_tris.clear();
    std::unordered_map<unsigned, int> v2i;
    v2i.reserve(mesh.number_of_vertices());
    out_verts.reserve(mesh.number_of_vertices());
    for (auto v : mesh.vertices()) {
        const auto& p = mesh.point(v);
        v2i[(unsigned)v] = (int)out_verts.size();
        out_verts.push_back({CGAL::to_double(p.x()),
                             CGAL::to_double(p.y()),
                             CGAL::to_double(p.z())});
    }
    out_tris.reserve(mesh.number_of_faces());
    for (auto f : mesh.faces()) {
        std::array<int, 3> idx{-1, -1, -1};
        int k = 0;
        for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
            if (k < 3) idx[k] = v2i[(unsigned)v];
            ++k;
        }
        if (k == 3 && idx[0] >= 0 && idx[1] >= 0 && idx[2] >= 0)
            out_tris.push_back({idx[0], idx[1], idx[2]});
    }
}

bool validate_acvd_mesh_quality(const TMesh& mesh, std::string& error)
{
    std::vector<TMesh::Halfedge_index> non_manifold_vertices;
    PMP::non_manifold_vertices(mesh, std::back_inserter(non_manifold_vertices));

    std::vector<TMesh::Face_index> degenerate_faces;
    PMP::degenerate_faces(mesh, std::back_inserter(degenerate_faces));

    std::vector<TMesh::Halfedge_index> border_halfedges;
    PMP::border_halfedges(mesh, std::back_inserter(border_halfedges));

    bool self_intersects = false;
    try {
        self_intersects = PMP::does_self_intersect(mesh);
    } catch (const std::exception& e) {
        error = std::string("ACVD input validation failed during "
                            "self-intersection check: ") + e.what();
        return false;
    }

    if (non_manifold_vertices.empty() && degenerate_faces.empty() &&
        !self_intersects)
        return true;

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "Input mesh is not suitable for CGAL ACVD: "
        "%zu non-manifold vertices, %zu degenerate faces, "
        "self-intersections=%s, border halfedges=%zu. "
        "Please repair the mesh before ACVD remeshing.",
        non_manifold_vertices.size(),
        degenerate_faces.size(),
        self_intersects ? "yes" : "no",
        border_halfedges.size());
    error = buf;
    return false;
}

} // namespace

// ==========================================================================
// Construction / destruction
// ==========================================================================

ACVDRunner::ACVDRunner() : impl_(std::make_unique<Impl>()) {}
ACVDRunner::~ACVDRunner() = default;

// ==========================================================================
// set_input
// ==========================================================================

void ACVDRunner::set_input(const std::vector<ACVD_Point3d>&  verts,
                           const std::vector<ACVD_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

// ==========================================================================
// run
// ==========================================================================

void ACVDRunner::run(const ACVD_Config& cfg)
{
    if (!impl_->has_input || impl_->input_verts.empty()) {
        set_error("No input mesh");
        impl_->done_flag = true;
        return;
    }

    impl_->reset_state();
    impl_->cfg = cfg;

    if (cfg.target_vertices < 10) {
        set_error("Target vertex count is too small (< 10)");
        impl_->done_flag = true;
        return;
    }

    using Clock = std::chrono::steady_clock;
    const auto t_run0 = Clock::now();

    // --- 1) Build CGAL mesh + validate ---
    TMesh mesh;
    std::vector<TMesh::Vertex_index> vd_map;
    build_cgal_mesh(mesh, vd_map, impl_->input_verts, impl_->input_tris);

    // Single connected component check
    {
        auto fcm = mesh.template add_property_map<TMesh::Face_index, std::size_t>(
            "fcm", 0).first;
        std::size_t nbcc = PMP::connected_components(mesh, fcm);
        mesh.remove_property_map(fcm);
        if (nbcc != 1) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "Input has %zu connected components. ACVD requires exactly one.",
                nbcc);
            set_error(buf);
            impl_->done_flag = true;
            return;
        }
    }

    if (!CGAL::is_triangle_mesh(mesh)) {
        set_error("Input is not a pure triangle mesh");
        impl_->done_flag = true;
        return;
    }

    {
        std::string quality_error;
        if (!validate_acvd_mesh_quality(mesh, quality_error)) {
            set_error(quality_error);
            impl_->done_flag = true;
            return;
        }
    }

    const int initial_vertices = (int)mesh.number_of_vertices();
    const int initial_faces    = (int)mesh.number_of_faces();

    const auto t_pre_end = Clock::now();
    impl_->ms_preprocess_.store(
        std::chrono::duration<double, std::milli>(t_pre_end - t_run0).count(),
        std::memory_order_relaxed);

    CGAL::get_default_random() = CGAL::Random(cfg.random_seed);

    // --- 2) Run ACVD ---
    RecordingVisitor vis;
    vis.impl = impl_.get();
    vis.source_vd_map = &vd_map;
    const bool use_visitor = cfg.live_preview;

    const auto t_cluster_start = Clock::now();
    bool vertex_count_matched = false;

    try {
        CgalFailureBehaviourGuard failure_guard;
        const auto target = (std::size_t)cfg.target_vertices;
        const double vertex_ratio =
            (cfg.vertex_count_ratio > 0.0) ? cfg.vertex_count_ratio : 0.1;
        switch (cfg.mode) {
        case ACVD_MODE_Uniform:
            if (use_visitor)
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed)
                              .visitor(vis));
            else
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed));
            break;
        case ACVD_MODE_UniformQemPostprocess:
            if (use_visitor)
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::use_postprocessing_qem(true)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed)
                              .visitor(vis));
            else
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::use_postprocessing_qem(true)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed));
            break;
        case ACVD_MODE_QemEnergy:
            if (use_visitor)
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::use_qem_based_energy(true)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed)
                              .visitor(vis));
            else
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::use_qem_based_energy(true)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed));
            break;
        case ACVD_MODE_AdaptiveCurvature:
            if (use_visitor)
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::gradation_factor(cfg.gradation_factor)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed)
                              .visitor(vis));
            else
                vertex_count_matched =
                    PMP::approximated_centroidal_Voronoi_diagram_remeshing(
                        mesh, target,
                        params::gradation_factor(cfg.gradation_factor)
                              .vertex_count_ratio(vertex_ratio)
                              .random_seed(cfg.random_seed));
            break;
        default:
            set_error("Unknown ACVD mode");
            impl_->done_flag = true;
            return;
        }
    } catch (const CGAL::Failure_exception& e) {
        set_error(std::string("ACVD CGAL failure: ") + e.what());
    } catch (const std::exception& e) {
        set_error(std::string("ACVD exception: ") + e.what());
    } catch (...) {
        set_error("ACVD unknown exception");
    }
    (void)vertex_count_matched;

    const auto t_cluster_end = Clock::now();
    impl_->ms_clustering_.store(
        std::chrono::duration<double, std::milli>(t_cluster_end - t_cluster_start).count(),
        std::memory_order_relaxed);

    // --- 3) Convert result ---
    const auto t_conv_start = Clock::now();
    if (!has_error()) {
        emit_pod_mesh(mesh, impl_->result_verts, impl_->result_tris);
    }
    const auto t_conv_end = Clock::now();
    impl_->ms_output_.store(
        std::chrono::duration<double, std::milli>(t_conv_end - t_conv_start).count(),
        std::memory_order_relaxed);

    impl_->working_vertices_ = initial_vertices;
    impl_->final_vertices_   = (int)mesh.number_of_vertices();
    impl_->final_faces_      = (int)mesh.number_of_faces();

    const auto t_run_end = Clock::now();
    impl_->ms_total_.store(
        std::chrono::duration<double, std::milli>(t_run_end - t_run0).count(),
        std::memory_order_relaxed);

    impl_->done_flag = true;

}

// ==========================================================================
// Live-preview event/snapshot accessors
// ==========================================================================

bool ACVDRunner::drain_live_events(std::vector<ACVD_FrameEvent>& out) {
    out.clear();
    std::lock_guard<std::mutex> lk(impl_->live_mutex);
    if (impl_->live_queue.empty()) return false;
    for (const auto& e : impl_->live_queue) out.push_back(e);
    impl_->live_queue.clear();
    return true;
}

bool ACVDRunner::poll_cluster_snapshot(int& last_generation,
                                        std::vector<ACVD_Point3d>&  verts,
                                        std::vector<ACVD_Triangle>& tris,
                                        std::vector<int>&            face_cluster_ids,
                                        std::vector<ACVD_Point3d>&  cluster_centers) const {
    const int cur = impl_->snap_generation.load(std::memory_order_acquire);
    if (cur <= last_generation) return false;
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    verts            = impl_->snap_verts;
    tris             = impl_->snap_tris;
    face_cluster_ids = impl_->snap_cluster_ids;
    cluster_centers  = impl_->snap_centers;
    last_generation  = cur;
    return true;
}

void ACVDRunner::get_seed_positions(std::vector<ACVD_Point3d>& out) const {
    std::lock_guard<std::mutex> lk(impl_->seeds_mutex);
    out = impl_->seed_positions;
}

// ==========================================================================
// Cancel / Error
// ==========================================================================

void ACVDRunner::cancel() { impl_->cancel_flag = true; }
bool ACVDRunner::is_cancelled() const { return impl_->cancel_flag.load(); }

bool        ACVDRunner::has_error()  const { return impl_->has_error_flag.load(); }
std::string ACVDRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}
void ACVDRunner::set_error(const std::string& msg) {
    { std::lock_guard<std::mutex> lk(impl_->error_mutex); impl_->error_message = msg; }
    impl_->has_error_flag.store(true, std::memory_order_release);
}
void ACVDRunner::clear_error() {
    impl_->has_error_flag.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(impl_->error_mutex); impl_->error_message.clear();
}

// ==========================================================================
// Status / Result
// ==========================================================================

bool ACVDRunner::is_done() const { return impl_->done_flag.load(); }

void ACVDRunner::get_result(std::vector<ACVD_Point3d>&  out_verts,
                            std::vector<ACVD_Triangle>& out_tris) const
{
    out_verts = impl_->result_verts;
    out_tris  = impl_->result_tris;
}

ACVD_DebugStats ACVDRunner::debug_stats() const {
    ACVD_DebugStats s;
    s.mode             = impl_->cfg.mode;
    s.target_vertices  = impl_->cfg.target_vertices;
    s.initial_vertices = (int)impl_->input_verts.size();
    s.initial_edges    = 0;
    s.initial_faces    = (int)impl_->input_tris.size();
    s.working_vertices_after_preprocess = impl_->working_vertices_.load();
    s.final_vertices   = impl_->final_vertices_.load();
    s.final_faces      = impl_->final_faces_.load();
    s.loops            = impl_->loops_.load();
    s.iterations       = impl_->iterations_.load();
    s.assignment_events = impl_->assigned_vertices_.load();
    s.disconnected_repairs = impl_->disconnected_fixes_.load();
    s.non_manifold_repairs = 0;
    s.output_vertex_count_matched =
        (s.final_vertices == s.target_vertices);
    s.cancelled        = impl_->cancel_flag.load();
    s.ms_total         = impl_->ms_total_.load();
    s.ms_preprocess    = impl_->ms_preprocess_.load();
    s.ms_clustering    = impl_->ms_clustering_.load();
    s.ms_output        = impl_->ms_output_.load();
    return s;
}

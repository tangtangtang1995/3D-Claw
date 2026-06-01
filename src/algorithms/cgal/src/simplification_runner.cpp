// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "simplification_runner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>

#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/Surface_mesh_simplification/Edge_collapse_visitor_base.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_count_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Edge_length_cost.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Midpoint_placement.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_plane_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_triangle_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_probabilistic_plane_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_probabilistic_triangle_policies.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Bounded_normal_change_filter.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Polyhedral_envelope_filter.h>

namespace SMS = CGAL::Surface_mesh_simplification;

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using TMesh   = CGAL::Surface_mesh<Point_3>;

// ==========================================================================
// Impl
// ==========================================================================

struct SimplificationRunner::Impl {
    Impl() {
        for (auto& c : recent_costs_) c.store(0.0, std::memory_order_relaxed);
    }

    // POD input
    std::vector<SIMPL_Point3d>  input_verts;
    std::vector<SIMPL_Triangle> input_tris;
    bool has_input = false;

    SIMPL_Config cfg{};

    // Result (POD)
    std::vector<SIMPL_Point3d>  result_verts;
    std::vector<SIMPL_Triangle> result_tris;

    // Live event queue
    std::deque<SIMPL_FrameEvent> live_queue;
    mutable std::mutex live_mutex;

    // Snapshot (worker writes, main thread reads)
    std::vector<SIMPL_Point3d>  snap_verts;
    std::vector<SIMPL_Triangle> snap_tris;
    std::atomic<int>            snap_generation{0};
    mutable std::mutex          snap_mutex;

    // State
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    // Running stats (atomic, polled by main thread)
    std::atomic<int> initial_edges_{0};
    std::atomic<int> current_edges_{0};
    std::atomic<int> initial_faces_{0};
    std::atomic<int> current_faces_{0};
    std::atomic<int> collapsed_count_{0};
    std::atomic<int> rejected_count_{0};
    std::atomic<int> selected_count_{0};
    std::atomic<bool> stop_reached_{false};

    // Cost statistics from OnSelected(). One worker thread writes, the UI
    // thread reads atomically through debug_stats().
    std::atomic<int> cost_count_{0};
    std::atomic<double> cost_sum_{0.0};
    std::atomic<double> cost_min_{0.0};
    std::atomic<double> cost_max_{0.0};
    std::array<std::atomic<double>, 32> recent_costs_{};
    std::atomic<int> recent_cost_index_{0};
    std::atomic<int> recent_cost_count_{0};

    // Timing
    std::atomic<double> ms_total_{0};
    std::atomic<double> ms_setup_{0};
    std::atomic<double> ms_collapse_{0};
    std::atomic<double> ms_convert_{0};

    void reset_state() {
        cancel_flag = false;
        done_flag = false;
        has_error_flag = false;
        { std::lock_guard<std::mutex> lk(error_mutex); error_message.clear(); }
        { std::lock_guard<std::mutex> lk(live_mutex);  live_queue.clear(); }
        { std::lock_guard<std::mutex> lk(snap_mutex);
          snap_verts.clear(); snap_tris.clear(); }
        snap_generation = 0;
        result_verts.clear();
        result_tris.clear();
        initial_edges_ = 0;
        current_edges_ = 0;
        initial_faces_ = 0;
        current_faces_ = 0;
        collapsed_count_ = 0;
        rejected_count_ = 0;
        selected_count_ = 0;
        stop_reached_ = false;
        cost_count_ = 0;
        cost_sum_ = 0;
        cost_min_ = 0;
        cost_max_ = 0;
        recent_cost_index_ = 0;
        recent_cost_count_ = 0;
        for (auto& c : recent_costs_)
            c.store(0.0, std::memory_order_relaxed);
        ms_total_ = 0;
        ms_setup_ = 0;
        ms_collapse_ = 0;
        ms_convert_ = 0;
    }

    void record_cost(double c) {
        if (!std::isfinite(c))
            return;
        const int n = cost_count_.fetch_add(1, std::memory_order_relaxed) + 1;
        cost_sum_.store(cost_sum_.load(std::memory_order_relaxed) + c,
                        std::memory_order_relaxed);
        if (n == 1) {
            cost_min_.store(c, std::memory_order_relaxed);
            cost_max_.store(c, std::memory_order_relaxed);
        } else {
            if (c < cost_min_.load(std::memory_order_relaxed))
                cost_min_.store(c, std::memory_order_relaxed);
            if (c > cost_max_.load(std::memory_order_relaxed))
                cost_max_.store(c, std::memory_order_relaxed);
        }

        const int idx = recent_cost_index_.fetch_add(1, std::memory_order_relaxed);
        recent_costs_[(std::size_t)idx % recent_costs_.size()]
            .store(c, std::memory_order_relaxed);
        const int rc = recent_cost_count_.load(std::memory_order_relaxed);
        if (rc < (int)recent_costs_.size())
            recent_cost_count_.store(rc + 1, std::memory_order_relaxed);
    }
};

// ==========================================================================
// Mesh conversion: POD <-> CGAL Surface_mesh
// ==========================================================================

namespace {

// Build a CGAL Surface_mesh from POD vertices + triangle indices.
// Returns a map old_index -> vertex_descriptor for later snapshot conversion.
void build_cgal_mesh(TMesh& out,
                     std::vector<TMesh::Vertex_index>& vd_map,
                     const std::vector<SIMPL_Point3d>&  verts,
                     const std::vector<SIMPL_Triangle>& tris)
{
    out.clear();
    vd_map.clear();
    vd_map.reserve(verts.size());
    for (auto& p : verts) {
        auto vd = out.add_vertex(Point_3(p.x, p.y, p.z));
        vd_map.push_back(vd);
    }
    for (auto& t : tris) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        const int n = (int)vd_map.size();
        if (t.v0 >= n || t.v1 >= n || t.v2 >= n) continue;
        // CGAL refuses degenerate or non-manifold edges; failure here returns
        // Surface_mesh::null_face() but does not throw.
        out.add_face(vd_map[t.v0], vd_map[t.v1], vd_map[t.v2]);
    }
}

// Walk the CGAL mesh and emit POD vertices + triangle indices.
void emit_pod_mesh(const TMesh& mesh,
                   std::vector<SIMPL_Point3d>&  out_verts,
                   std::vector<SIMPL_Triangle>& out_tris)
{
    out_verts.clear();
    out_tris.clear();
    // Map vertex_descriptor -> compact index. Surface_mesh keeps removed
    // vertices around as garbage until collect_garbage(); we walk the live
    // ones only.
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

// ==========================================================================
// Cancellable stop predicate wrapper
// ==========================================================================
//
// CGAL's edge_collapse evaluates the stop predicate every time it considers a
// candidate. Wrapping any inner predicate with a cancel-flag check lets us
// cooperatively abort: on cancel, the predicate returns true, edge_collapse
// treats that as "stop criterion satisfied" and exits cleanly. The mesh
// retains every collapse that completed before the cancel.

template <typename InnerStop>
class CancelableStop {
public:
    CancelableStop(InnerStop inner, std::atomic<bool>* cancel_flag)
        : inner_(std::move(inner)), cancel_(cancel_flag) {}

    template <typename F, typename Profile>
    bool operator()(const F& cost,
                    const Profile& profile,
                    typename Profile::edges_size_type initial_edge_count,
                    typename Profile::edges_size_type current_edge_count) const
    {
        if (cancel_ && cancel_->load(std::memory_order_relaxed))
            return true;
        return inner_(cost, profile, initial_edge_count, current_edge_count);
    }

private:
    InnerStop inner_;
    std::atomic<bool>* cancel_;
};

// ==========================================================================
// Recording visitor: counts elements and pushes progress events
// ==========================================================================

struct RecordingVisitor
    : public SMS::Edge_collapse_visitor_base<TMesh>
{
    SimplificationRunner::Impl* impl = nullptr;

    // Snapshot throttle bookkeeping (worker-thread only, no atomics needed).
    int last_snapshot_at_ = 0;
    std::chrono::steady_clock::time_point last_snapshot_time_{};

    void OnStarted(TMesh& mesh) {
        if (!impl) return;
        impl->initial_edges_ = (int)mesh.number_of_edges();
        impl->current_edges_ = (int)mesh.number_of_edges();
        impl->initial_faces_ = (int)mesh.number_of_faces();
        impl->current_faces_ = (int)mesh.number_of_faces();
        if (impl->cfg.live_preview) {
            SIMPL_FrameEvent ev{};
            ev.type = SIMPL_FrameEvent::Init;
            ev.initial_edges = impl->initial_edges_.load();
            ev.current_edges = impl->current_edges_.load();
            ev.initial_faces = impl->initial_faces_.load();
            ev.current_faces = impl->current_faces_.load();
            push_event(ev);
        }
    }

    void OnSelected(const Profile& /*p*/,
                    const std::optional<FT>& cost,
                    size_type /*initial*/,
                    size_type current_edges)
    {
        if (!impl) return;
        impl->current_edges_.store((int)current_edges,
                                   std::memory_order_relaxed);
        impl->selected_count_.fetch_add(1, std::memory_order_relaxed);
        if (cost.has_value())
            impl->record_cost(CGAL::to_double(*cost));
    }

    void OnCollapsing(const Profile& p,
                      const std::optional<Point>& placement)
    {
        if (!impl || !impl->cfg.live_preview) return;
        // Throttle Collapsing events by cfg.event_interval. The trail in the
        // UI keeps only the most recent N edges, so pushing every collapse is
        // wasted work; one in every `interval` is enough to keep the animation
        // alive without flooding the queue.
        const int interval = std::max(1, impl->cfg.event_interval);
        const int n = impl->selected_count_.load(std::memory_order_relaxed);
        if ((n % interval) != 0) return;

        SIMPL_FrameEvent ev{};
        ev.type = SIMPL_FrameEvent::Collapsing;
        ev.step = n;
        auto pt0 = p.p0();
        auto pt1 = p.p1();
        ev.p0[0] = CGAL::to_double(pt0.x());
        ev.p0[1] = CGAL::to_double(pt0.y());
        ev.p0[2] = CGAL::to_double(pt0.z());
        ev.p1[0] = CGAL::to_double(pt1.x());
        ev.p1[1] = CGAL::to_double(pt1.y());
        ev.p1[2] = CGAL::to_double(pt1.z());
        if (placement.has_value()) {
            ev.placement[0] = CGAL::to_double(placement->x());
            ev.placement[1] = CGAL::to_double(placement->y());
            ev.placement[2] = CGAL::to_double(placement->z());
        } else {
            ev.placement[0] = 0.5 * (ev.p0[0] + ev.p1[0]);
            ev.placement[1] = 0.5 * (ev.p0[1] + ev.p1[1]);
            ev.placement[2] = 0.5 * (ev.p0[2] + ev.p1[2]);
        }
        ev.collapsed_count = impl->collapsed_count_.load(std::memory_order_relaxed);
        ev.current_edges   = impl->current_edges_.load(std::memory_order_relaxed);
        ev.current_faces   = impl->current_faces_.load(std::memory_order_relaxed);
        ev.initial_edges   = impl->initial_edges_.load(std::memory_order_relaxed);
        ev.initial_faces   = impl->initial_faces_.load(std::memory_order_relaxed);
        if (ev.initial_edges > 0) {
            ev.reduction_ratio =
                1.0 - (double)ev.current_edges / (double)ev.initial_edges;
        }
        push_event(ev);
    }

    void OnCollapsed(const Profile& p,
                     const vertex_descriptor& /*new_vertex*/)
    {
        if (!impl) return;
        impl->collapsed_count_.fetch_add(1, std::memory_order_relaxed);
        // Edges drop by 3 per collapse, faces by 2; the cheap atomic update
        // gives the UI a smooth running counter without touching the mesh.
        impl->current_edges_.fetch_sub(3, std::memory_order_relaxed);
        impl->current_faces_.fetch_sub(2, std::memory_order_relaxed);

        if (!impl->cfg.live_preview) return;
        // Snapshot publishing. Twice-throttled:
        //   1. Every cfg.snapshot_interval successful collapses
        //      (default: max(50, initial_edges/20), i.e. ~5% of the mesh)
        //   2. AND at least cfg.snapshot_min_ms wall time since the previous
        //      snapshot.
        // Walking the mesh is O(faces) so on big meshes we don't want to do
        // it every collapse. The wall-time gate keeps the UI smooth even if
        // collapses are very cheap.
        const int n_coll = impl->collapsed_count_.load(std::memory_order_relaxed);
        int interval = impl->cfg.snapshot_interval;
        if (interval == 0) {
            // ~2% of initial edges so a 100k-edge mesh produces roughly
            // 50 snapshots across a run, capped at 50 collapses min so
            // tiny meshes still get a few frames of animation.
            interval = std::max(50, impl->initial_edges_.load() / 50);
        }
        if (interval < 0) return;  // snapshots disabled
        if (n_coll - last_snapshot_at_ < interval) return;
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        if (last_snapshot_time_ != Clock::time_point{}) {
            const auto dt =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_snapshot_time_).count();
            if (dt < impl->cfg.snapshot_min_ms) return;
        }
        last_snapshot_at_   = n_coll;
        last_snapshot_time_ = now;

        // Snapshot: walk the (mid-simplification) mesh and emit POD.
        // CGAL Surface_mesh iterators automatically skip removed elements,
        // so we don't need to collect_garbage() first.
        {
            std::lock_guard<std::mutex> lk(impl->snap_mutex);
            emit_pod_mesh(p.surface_mesh(),
                          impl->snap_verts, impl->snap_tris);
        }
        const int gen = impl->snap_generation.fetch_add(
            1, std::memory_order_release) + 1;

        SIMPL_FrameEvent ev{};
        ev.type = SIMPL_FrameEvent::Snapshot;
        ev.step = n_coll;
        ev.snapshot_generation = gen;
        ev.collapsed_count = n_coll;
        ev.current_edges   = impl->current_edges_.load(std::memory_order_relaxed);
        ev.current_faces   = impl->current_faces_.load(std::memory_order_relaxed);
        push_event(ev);
    }

    void OnNonCollapsable(const Profile& /*p*/) {
        if (!impl) return;
        impl->rejected_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void OnStopConditionReached(const Profile& /*p*/) {
        if (!impl) return;
        impl->stop_reached_.store(true, std::memory_order_relaxed);
        if (impl->cfg.live_preview) {
            SIMPL_FrameEvent ev{};
            ev.type = SIMPL_FrameEvent::StopReached;
            ev.collapsed_count = impl->collapsed_count_.load();
            ev.current_edges   = impl->current_edges_.load();
            ev.current_faces   = impl->current_faces_.load();
            push_event(ev);
        }
    }

    void OnFinished(TMesh& /*mesh*/) {
        if (!impl || !impl->cfg.live_preview) return;
        SIMPL_FrameEvent ev{};
        ev.type = SIMPL_FrameEvent::Done;
        ev.collapsed_count = impl->collapsed_count_.load();
        ev.rejected_count  = impl->rejected_count_.load();
        ev.current_edges   = impl->current_edges_.load();
        ev.current_faces   = impl->current_faces_.load();
        ev.initial_edges   = impl->initial_edges_.load();
        ev.initial_faces   = impl->initial_faces_.load();
        if (ev.initial_edges > 0) {
            ev.reduction_ratio =
                1.0 - (double)ev.current_edges / (double)ev.initial_edges;
        }
        push_event(ev);
    }

private:
    // Append to the live queue; bound it so worker bursts can't grow without
    // limit if the dialog is slow to drain.
    void push_event(SIMPL_FrameEvent& ev) {
        std::lock_guard<std::mutex> lk(impl->live_mutex);
        const int cap = std::max(64, impl->cfg.max_live_events);
        if ((int)impl->live_queue.size() >= cap)
            impl->live_queue.pop_front();
        impl->live_queue.push_back(ev);
    }
};

// ==========================================================================
// Strategy dispatch
// ==========================================================================

// One signature per strategy. The stop predicate is templated so we can wrap
// any of CGAL's predicates with CancelableStop transparently.
template <typename Stop>
int run_lindstrom_turk(TMesh& mesh, const Stop& stop, RecordingVisitor& vis)
{
    return SMS::edge_collapse(mesh, stop,
        CGAL::parameters::visitor(vis));
}

template <typename Stop>
int run_lindstrom_turk(TMesh& mesh,
                       const Stop& stop,
                       RecordingVisitor& vis,
                       const SIMPL_Config& cfg)
{
    if (cfg.use_polyhedral_envelope) {
        const double eps = std::max(1e-12, cfg.polyhedral_envelope_epsilon);
        if (cfg.use_bounded_normal_change) {
            SMS::Bounded_normal_change_filter<> normal_filter;
            SMS::Polyhedral_envelope_filter<
                Kernel, SMS::Bounded_normal_change_filter<> > envelope_filter(
                    eps, normal_filter);
            return SMS::edge_collapse(mesh, stop,
                CGAL::parameters::filter(envelope_filter).visitor(vis));
        }
        SMS::Polyhedral_envelope_filter<Kernel> envelope_filter(eps);
        return SMS::edge_collapse(mesh, stop,
            CGAL::parameters::filter(envelope_filter).visitor(vis));
    }

    if (cfg.use_bounded_normal_change) {
        SMS::Bounded_normal_change_filter<> normal_filter;
        return SMS::edge_collapse(mesh, stop,
            CGAL::parameters::filter(normal_filter).visitor(vis));
    }

    return run_lindstrom_turk(mesh, stop, vis);
}

template <typename Stop, typename Cost, typename Placement>
int run_with_cost_placement(TMesh& mesh,
                            const Stop& stop,
                            RecordingVisitor& vis,
                            const SIMPL_Config& cfg,
                            const Cost& cost,
                            const Placement& placement)
{
    if (cfg.use_polyhedral_envelope) {
        const double eps = std::max(1e-12, cfg.polyhedral_envelope_epsilon);
        if (cfg.use_bounded_normal_change) {
            SMS::Bounded_normal_change_filter<> normal_filter;
            SMS::Polyhedral_envelope_filter<
                Kernel, SMS::Bounded_normal_change_filter<> > envelope_filter(
                    eps, normal_filter);
            return SMS::edge_collapse(mesh, stop,
                CGAL::parameters::get_cost(cost)
                                 .get_placement(placement)
                                 .filter(envelope_filter)
                                 .visitor(vis));
        }
        SMS::Polyhedral_envelope_filter<Kernel> envelope_filter(eps);
        return SMS::edge_collapse(mesh, stop,
            CGAL::parameters::get_cost(cost)
                             .get_placement(placement)
                             .filter(envelope_filter)
                             .visitor(vis));
    }

    if (cfg.use_bounded_normal_change) {
        SMS::Bounded_normal_change_filter<> normal_filter;
        return SMS::edge_collapse(mesh, stop,
            CGAL::parameters::get_cost(cost)
                             .get_placement(placement)
                             .filter(normal_filter)
                             .visitor(vis));
    }

    return SMS::edge_collapse(mesh, stop,
        CGAL::parameters::get_cost(cost)
                         .get_placement(placement)
                         .visitor(vis));
}

template <typename Stop>
int run_garland_heckbert_plane(TMesh& mesh,
                               const Stop& stop,
                               RecordingVisitor& vis,
                               const SIMPL_Config& cfg)
{
    SMS::GarlandHeckbert_plane_policies<TMesh, Kernel> policies(mesh);
    return run_with_cost_placement(mesh, stop, vis, cfg,
        policies.get_cost(), policies.get_placement());
}

template <typename Stop>
int run_garland_heckbert_triangle(TMesh& mesh,
                                  const Stop& stop,
                                  RecordingVisitor& vis,
                                  const SIMPL_Config& cfg)
{
    SMS::GarlandHeckbert_triangle_policies<TMesh, Kernel> policies(mesh);
    return run_with_cost_placement(mesh, stop, vis, cfg,
        policies.get_cost(), policies.get_placement());
}

template <typename Stop>
int run_garland_heckbert_probabilistic_plane(TMesh& mesh,
                                             const Stop& stop,
                                             RecordingVisitor& vis,
                                             const SIMPL_Config& cfg)
{
    SMS::GarlandHeckbert_probabilistic_plane_policies<TMesh, Kernel>
        policies(mesh);
    return run_with_cost_placement(mesh, stop, vis, cfg,
        policies.get_cost(), policies.get_placement());
}

template <typename Stop>
int run_garland_heckbert_probabilistic_triangle(TMesh& mesh,
                                                const Stop& stop,
                                                RecordingVisitor& vis,
                                                const SIMPL_Config& cfg)
{
    SMS::GarlandHeckbert_probabilistic_triangle_policies<TMesh, Kernel>
        policies(mesh);
    return run_with_cost_placement(mesh, stop, vis, cfg,
        policies.get_cost(), policies.get_placement());
}

template <typename Stop>
int run_edge_length_midpoint(TMesh& mesh,
                             const Stop& stop,
                             RecordingVisitor& vis,
                             const SIMPL_Config& cfg)
{
    SMS::Edge_length_cost<TMesh>      cost;
    SMS::Midpoint_placement<TMesh>    placement;
    return run_with_cost_placement(mesh, stop, vis, cfg, cost, placement);
}

#define CLAW3D_SIMPL_DISPATCH_CASES()                                           \
        case SIMPL_STRAT_LindstromTurk:                                         \
            return run_lindstrom_turk(mesh, cstop, vis, cfg);                   \
        case SIMPL_STRAT_GarlandHeckbertPlane:                                  \
            return run_garland_heckbert_plane(mesh, cstop, vis, cfg);           \
        case SIMPL_STRAT_EdgeLengthMidpoint:                                    \
            return run_edge_length_midpoint(mesh, cstop, vis, cfg);             \
        case SIMPL_STRAT_GarlandHeckbertTriangle:                               \
            return run_garland_heckbert_triangle(mesh, cstop, vis, cfg);        \
        case SIMPL_STRAT_GarlandHeckbertProbabilisticPlane:                     \
            return run_garland_heckbert_probabilistic_plane(                    \
                mesh, cstop, vis, cfg);                                         \
        case SIMPL_STRAT_GarlandHeckbertProbabilisticTriangle:                  \
            return run_garland_heckbert_probabilistic_triangle(                 \
                mesh, cstop, vis, cfg);

// Dispatch table: returns number of edges removed, or -1 on unsupported.
int dispatch_strategy(int strategy,
                      TMesh& mesh,
                      const SIMPL_Config& cfg,
                      std::atomic<bool>& cancel_flag,
                      RecordingVisitor& vis)
{
    // Build the inner stop predicate then wrap it.
    switch (cfg.stop_mode) {
    case SIMPL_STOP_EdgeRatio: {
        SMS::Edge_count_ratio_stop_predicate<TMesh> stop(cfg.target_ratio);
        CancelableStop<decltype(stop)> cstop(stop, &cancel_flag);
        switch (strategy) {
        CLAW3D_SIMPL_DISPATCH_CASES()
        default: return -1;
        }
    }
    case SIMPL_STOP_EdgeCount: {
        SMS::Edge_count_stop_predicate<TMesh> stop(
            (typename TMesh::size_type)std::max(0, cfg.target_count));
        CancelableStop<decltype(stop)> cstop(stop, &cancel_flag);
        switch (strategy) {
        CLAW3D_SIMPL_DISPATCH_CASES()
        default: return -1;
        }
    }
    case SIMPL_STOP_FaceRatio: {
        SMS::Face_count_ratio_stop_predicate<TMesh> stop(cfg.target_ratio, mesh);
        CancelableStop<decltype(stop)> cstop(stop, &cancel_flag);
        switch (strategy) {
        CLAW3D_SIMPL_DISPATCH_CASES()
        default: return -1;
        }
    }
    case SIMPL_STOP_FaceCount: {
        SMS::Face_count_stop_predicate<TMesh> stop(
            (typename TMesh::size_type)std::max(0, cfg.target_count));
        CancelableStop<decltype(stop)> cstop(stop, &cancel_flag);
        switch (strategy) {
        CLAW3D_SIMPL_DISPATCH_CASES()
        default: return -1;
        }
    }
    default: return -1;
    }
}

#undef CLAW3D_SIMPL_DISPATCH_CASES

} // namespace

// ==========================================================================
// Construction / destruction
// ==========================================================================

SimplificationRunner::SimplificationRunner() : impl_(std::make_unique<Impl>()) {}
SimplificationRunner::~SimplificationRunner() = default;

// ==========================================================================
// set_input
// ==========================================================================

void SimplificationRunner::set_input(
    const std::vector<SIMPL_Point3d>&  verts,
    const std::vector<SIMPL_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

// ==========================================================================
// run
// ==========================================================================

void SimplificationRunner::run(const SIMPL_Config& cfg)
{
    if (!impl_->has_input || impl_->input_verts.empty()) {
        set_error("No input mesh");
        impl_->done_flag = true;
        return;
    }

    impl_->reset_state();
    impl_->cfg = cfg;

    using Clock = std::chrono::steady_clock;
    const auto t_run0 = Clock::now();

    // --- 1) build CGAL mesh ---
    TMesh mesh;
    std::vector<TMesh::Vertex_index> vd_map;
    build_cgal_mesh(mesh, vd_map, impl_->input_verts, impl_->input_tris);

    const auto t_setup_end = Clock::now();
    impl_->ms_setup_.store(
        std::chrono::duration<double, std::milli>(t_setup_end - t_run0).count(),
        std::memory_order_relaxed);

    impl_->initial_edges_ = (int)mesh.number_of_edges();
    impl_->current_edges_ = (int)mesh.number_of_edges();
    impl_->initial_faces_ = (int)mesh.number_of_faces();
    impl_->current_faces_ = (int)mesh.number_of_faces();

    // --- 2) run edge_collapse ---
    RecordingVisitor vis;
    vis.impl = impl_.get();

    const auto t_collapse_start = Clock::now();
    int removed = -1;
    try {
        removed = dispatch_strategy(cfg.strategy, mesh, cfg, impl_->cancel_flag, vis);
    } catch (const std::exception& e) {
        set_error(std::string("edge_collapse exception: ") + e.what());
    } catch (...) {
        set_error("edge_collapse unknown exception");
    }
    const auto t_collapse_end = Clock::now();
    impl_->ms_collapse_.store(
        std::chrono::duration<double, std::milli>(t_collapse_end - t_collapse_start).count(),
        std::memory_order_relaxed);

    if (removed < 0) {
        if (!has_error()) set_error("unsupported strategy/stop combination");
        impl_->done_flag = true;
        return;
    }

    // --- 3) collect garbage + convert back to POD ---
    mesh.collect_garbage();

    const auto t_convert_start = Clock::now();
    emit_pod_mesh(mesh, impl_->result_verts, impl_->result_tris);
    const auto t_convert_end = Clock::now();
    impl_->ms_convert_.store(
        std::chrono::duration<double, std::milli>(t_convert_end - t_convert_start).count(),
        std::memory_order_relaxed);

    // Authoritative final counts after garbage collection.
    impl_->current_edges_ = (int)mesh.number_of_edges();
    impl_->current_faces_ = (int)mesh.number_of_faces();

    const auto t_run_end = Clock::now();
    impl_->ms_total_.store(
        std::chrono::duration<double, std::milli>(t_run_end - t_run0).count(),
        std::memory_order_relaxed);

    impl_->done_flag = true;
}

// ==========================================================================
// Live preview + snapshot
// ==========================================================================

bool SimplificationRunner::drain_live_events(std::vector<SIMPL_FrameEvent>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lk(impl_->live_mutex);
    if (impl_->live_queue.empty()) return false;
    for (auto& ev : impl_->live_queue) out.push_back(ev);
    impl_->live_queue.clear();
    return true;
}

bool SimplificationRunner::poll_snapshot(int& last_generation,
                                         std::vector<SIMPL_Point3d>&  out_verts,
                                         std::vector<SIMPL_Triangle>& out_tris) const
{
    const int cur = impl_->snap_generation.load(std::memory_order_acquire);
    if (cur <= last_generation) return false;
    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
    out_verts = impl_->snap_verts;
    out_tris  = impl_->snap_tris;
    last_generation = cur;
    return true;
}

// ==========================================================================
// Cancel + error
// ==========================================================================

void SimplificationRunner::cancel() { impl_->cancel_flag = true; }
bool SimplificationRunner::is_cancelled() const { return impl_->cancel_flag.load(); }

bool        SimplificationRunner::has_error() const { return impl_->has_error_flag.load(); }
std::string SimplificationRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}
void SimplificationRunner::set_error(const std::string& msg) {
    { std::lock_guard<std::mutex> lk(impl_->error_mutex); impl_->error_message = msg; }
    impl_->has_error_flag.store(true, std::memory_order_release);
}
void SimplificationRunner::clear_error() {
    impl_->has_error_flag.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    impl_->error_message.clear();
}

// ==========================================================================
// Status / progress
// ==========================================================================

bool SimplificationRunner::is_done() const { return impl_->done_flag.load(); }

float SimplificationRunner::progress() const {
    int ie = impl_->initial_edges_.load();
    int ce = impl_->current_edges_.load();
    if (ie <= 0) return 0.0f;
    // Map current/initial -> 0..1 progress. target_ratio means "keep r * ie",
    // so progress = (ie - ce) / (ie - target_edges).
    double target;
    switch (impl_->cfg.stop_mode) {
    case SIMPL_STOP_EdgeRatio:
        target = (double)ie * std::clamp(impl_->cfg.target_ratio, 0.0, 1.0);
        break;
    case SIMPL_STOP_EdgeCount:
        target = (double)std::max(0, impl_->cfg.target_count);
        break;
    case SIMPL_STOP_FaceRatio: {
        int ifc = impl_->initial_faces_.load();
        target = (double)ie * std::clamp(impl_->cfg.target_ratio, 0.0, 1.0);
        (void)ifc;  // edge counter is fine for the bar
        break;
    }
    case SIMPL_STOP_FaceCount:
    default:
        target = 0.0;
        break;
    }
    double denom = (double)ie - target;
    if (denom <= 1.0) return 1.0f;
    double done = (double)(ie - ce) / denom;
    if (done < 0) done = 0;
    if (done > 1) done = 1;
    return (float)done;
}

// ==========================================================================
// Result
// ==========================================================================

void SimplificationRunner::get_result(
    std::vector<SIMPL_Point3d>&  out_verts,
    std::vector<SIMPL_Triangle>& out_tris) const
{
    out_verts = impl_->result_verts;
    out_tris  = impl_->result_tris;
}

SIMPL_DebugStats SimplificationRunner::debug_stats() const {
    SIMPL_DebugStats s;
    s.strategy_used     = impl_->cfg.strategy;
    s.initial_vertices  = (int)impl_->input_verts.size();
    s.initial_edges     = impl_->initial_edges_.load();
    s.initial_faces     = impl_->initial_faces_.load();
    // While running: current_* are the live atomic counters; result_verts is
    // empty until run() finishes. After run(): result_verts.size() is the
    // simplified vertex count.
    s.current_vertices  = impl_->done_flag.load()
                          ? (int)impl_->result_verts.size()
                          : (int)impl_->input_verts.size();
    s.current_edges     = impl_->current_edges_.load();
    s.current_faces     = impl_->current_faces_.load();
    s.collapsed_count   = impl_->collapsed_count_.load();
    s.rejected_count    = impl_->rejected_count_.load();
    s.selected_count    = impl_->selected_count_.load();
    s.cost_count        = impl_->cost_count_.load();
    if (s.cost_count > 0) {
        s.cost_min  = impl_->cost_min_.load();
        s.cost_max  = impl_->cost_max_.load();
        s.cost_mean = impl_->cost_sum_.load() / (double)s.cost_count;
        const int rc = impl_->recent_cost_count_.load();
        if (rc > 0) {
            double recent_sum = 0.0;
            for (int i = 0; i < rc && i < (int)impl_->recent_costs_.size(); ++i)
                recent_sum += impl_->recent_costs_[(std::size_t)i].load();
            s.cost_recent_mean = recent_sum / (double)rc;
        }
    }
    s.stop_reached      = impl_->stop_reached_.load();
    s.cancelled         = impl_->cancel_flag.load();
    if (s.initial_edges > 0)
        s.reduction_ratio = 1.0 - (double)s.current_edges / (double)s.initial_edges;
    s.ms_total    = impl_->ms_total_.load();
    s.ms_setup    = impl_->ms_setup_.load();
    s.ms_collapse = impl_->ms_collapse_.load();
    s.ms_convert  = impl_->ms_convert_.load();
    return s;
}

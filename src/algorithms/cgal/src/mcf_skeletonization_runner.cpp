// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "mcf_skeletonization_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Mean_curvature_flow_skeletonization.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/boost/graph/copy_face_graph.h>

#include <boost/graph/adjacency_list.hpp>

namespace PMP = CGAL::Polygon_mesh_processing;

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using TMesh   = CGAL::Surface_mesh<Point_3>;
using Skel    = CGAL::Mean_curvature_flow_skeletonization<TMesh>;
using Skeleton = Skel::Skeleton;
using Clock   = std::chrono::steady_clock;

struct MCFSkeletonizationRunner::Impl {
    std::vector<MCF_Point3d>  input_verts;
    std::vector<MCF_Triangle> input_tris;
    bool has_input = false;

    MCF_Config cfg{};

    MCF_Result result{};
    MCF_Metrics live_metrics{};

    MCF_Snapshot latest_snapshot{};
    std::atomic<int> snap_generation{0};
    mutable std::mutex snap_mutex;

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    void reset_state() {
        cancel_flag    = false;
        done_flag      = false;
        has_error_flag = false;
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message.clear();
        }
        result = MCF_Result{};
        live_metrics = MCF_Metrics{};
        {
            std::lock_guard<std::mutex> lk(snap_mutex);
            latest_snapshot = MCF_Snapshot{};
        }
        snap_generation = 0;
    }

    void set_error(MCF_ErrorCode code, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message = msg;
        }
        has_error_flag = true;
        result.error_code = code;
        std::snprintf(result.error_message, sizeof(result.error_message),
                      "%s", msg.c_str());
    }
};

namespace {

bool build_input_mesh(const std::vector<MCF_Point3d>&  pv,
                      const std::vector<MCF_Triangle>& pt,
                      TMesh& mesh,
                      std::string& err) {
    mesh.clear();
    if (pv.empty() || pt.empty()) {
        err = "empty input mesh";
        return false;
    }
    const int nv = (int)pv.size();
    std::vector<TMesh::Vertex_index> vmap;
    vmap.reserve(nv);
    for (const auto& p : pv)
        vmap.push_back(mesh.add_vertex(Point_3(p.x, p.y, p.z)));
    int dropped = 0;
    for (const auto& t : pt) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
            t.v0 >= nv || t.v1 >= nv || t.v2 >= nv ||
            t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) {
            ++dropped;
            continue;
        }
        TMesh::Face_index fd =
            mesh.add_face(vmap[t.v0], vmap[t.v1], vmap[t.v2]);
        if (fd == TMesh::null_face())
            mesh.add_face(vmap[t.v0], vmap[t.v2], vmap[t.v1]);
    }
    if (mesh.number_of_faces() == 0) {
        err = "no valid faces after conversion";
        return false;
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        err = "input is not a triangle mesh after conversion";
        return false;
    }
    return true;
}

inline MCF_Point3d to_pod(const Point_3& p) {
    return MCF_Point3d{CGAL::to_double(p.x()),
                       CGAL::to_double(p.y()),
                       CGAL::to_double(p.z())};
}

template <typename Mesh>
int count_vertices_adl(const Mesh& mesh) {
    auto range = vertices(mesh);
    return (int)std::distance(range.first, range.second);
}

template <typename Mesh>
int count_faces_adl(const Mesh& mesh) {
    auto range = faces(mesh);
    return (int)std::distance(range.first, range.second);
}

// Sleep that wakes up every 50ms to check cancel_flag. Used to pace the
// live worker on small meshes so the UI thread can render intermediate
// iterations instead of seeing only the final state via the single-buffered
// latest_snapshot. Independent of the snapshot dump cost.
void interruptible_sleep(int ms, const std::atomic<bool>& cancel) {
    if (ms <= 0) return;
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end) {
        if (cancel.load(std::memory_order_relaxed)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int per_publish_sleep_for(int preview_speed) {
    switch (preview_speed) {
        case MCF_PREVIEW_Slow:   return 360;
        case MCF_PREVIEW_Fast:   return 60;
        case MCF_PREVIEW_Normal:
        default:                 return 180;
    }
}

double degrees_to_radians(double degrees) {
    return degrees * 3.1415926535897932384626433832795 / 180.0;
}

// Dump the current meso skeleton (HalfedgeDS) into POD vertex/triangle
// arrays. We copy_face_graph it into a CGAL::Surface_mesh<Point_3> first so
// we get contiguous Vertex_index 0..n-1 to slot into MCF_Snapshot.
void dump_meso_to_snapshot(
    const CGAL::Mean_curvature_flow_skeletonization<TMesh>& mcs,
    MCF_Snapshot& snap)
{
    TMesh copy;
    CGAL::copy_face_graph(mcs.meso_skeleton(), copy);
    const auto n_v = copy.number_of_vertices();
    const auto n_f = copy.number_of_faces();
    snap.vertices.clear();    snap.vertices.reserve(n_v);
    snap.triangles.clear();   snap.triangles.reserve(n_f);

    std::vector<int> idx_remap(n_v, -1);
    int next_idx = 0;
    for (auto v : copy.vertices()) {
        idx_remap[v.idx()] = next_idx++;
        snap.vertices.push_back(to_pod(copy.point(v)));
    }
    for (auto f : copy.faces()) {
        auto h = copy.halfedge(f);
        int v0 = idx_remap[copy.target(h).idx()]; h = copy.next(h);
        int v1 = idx_remap[copy.target(h).idx()]; h = copy.next(h);
        int v2 = idx_remap[copy.target(h).idx()];
        if (v0 < 0 || v1 < 0 || v2 < 0) continue;
        snap.triangles.push_back(MCF_Triangle{v0, v1, v2});
    }
}

} // namespace

MCFSkeletonizationRunner::MCFSkeletonizationRunner()
    : impl_(std::make_unique<Impl>()) {}
MCFSkeletonizationRunner::~MCFSkeletonizationRunner() = default;

void MCFSkeletonizationRunner::set_input(
    const std::vector<MCF_Point3d>&  verts,
    const std::vector<MCF_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void MCFSkeletonizationRunner::run(const MCF_Config& cfg) {
    impl_->reset_state();
    impl_->cfg = cfg;

    if (!impl_->has_input) {
        impl_->set_error(MCF_ERR_EmptyInput, "no input mesh");
        impl_->done_flag = true;
        return;
    }

    const auto t_total_begin = Clock::now();

    // Build CGAL Surface_mesh.
    TMesh mesh;
    std::string build_err;
    if (!build_input_mesh(impl_->input_verts, impl_->input_tris, mesh, build_err)) {
        impl_->set_error(MCF_ERR_BuildFailed, build_err);
        impl_->done_flag = true;
        return;
    }

    // Validation gate (MCF strictly requires: triangle + closed + single CC).
    if (!CGAL::is_triangle_mesh(mesh)) {
        impl_->set_error(MCF_ERR_NotTriangleMesh,
                         "MCF requires a pure triangle mesh");
        impl_->done_flag = true;
        return;
    }
    if (!CGAL::is_closed(mesh)) {
        impl_->set_error(MCF_ERR_NotClosed,
                         "MCF requires a closed mesh (no borders)");
        impl_->done_flag = true;
        return;
    }
    const std::size_t nf = mesh.number_of_faces();
    if (nf < 16) {
        impl_->set_error(MCF_ERR_TooFewFaces,
                         "MCF requires a mesh with at least 16 faces");
        impl_->done_flag = true;
        return;
    }
    {
        auto fccmap = mesh.add_property_map<TMesh::Face_index, std::size_t>(
            "f:cc", 0).first;
        const std::size_t ncc = PMP::connected_components(mesh, fccmap);
        mesh.remove_property_map(fccmap);
        if (ncc != 1) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "MCF requires one connected component (mesh has %zu)",
                          ncc);
            impl_->set_error(MCF_ERR_MultipleComponents, buf);
            impl_->done_flag = true;
            return;
        }
    }

    // Cache initial metrics.
    const int nv0 = (int)mesh.number_of_vertices();
    const int nf0 = (int)nf;
    const double area0 = CGAL::to_double(PMP::area(mesh));
    impl_->live_metrics.original_vertices = nv0;
    impl_->live_metrics.original_faces    = nf0;
    impl_->live_metrics.original_area     = area0;
    impl_->live_metrics.max_iterations    = std::max(1, cfg.max_iterations);

    // Run skeletonization.
    Skeleton skeleton;
    try {
        Skel mcs(mesh);
        const int max_iter = std::max(1, cfg.max_iterations);
        mcs.set_max_iterations((std::size_t)max_iter);
        mcs.set_area_variation_factor(cfg.area_variation_factor);
        mcs.set_quality_speed_tradeoff(cfg.quality_speed_tradeoff);
        mcs.set_is_medially_centered(cfg.medially_centered);
        if (cfg.medially_centered)
            mcs.set_medially_centered_speed_tradeoff(
                cfg.medially_centered_speed_tradeoff);
        if (cfg.min_edge_length > 0.0)
            mcs.set_min_edge_length(cfg.min_edge_length);
        // else CGAL default = 0.002 * bbox_diag (handled internally by the
        // constructor). We still record what was used in metrics by querying
        // afterwards; but min_edge_length() is only available after the
        // constructor has computed it, so we just trust CGAL defaults here.
        mcs.set_max_triangle_angle(
            degrees_to_radians(cfg.max_triangle_angle_degrees));

        if (impl_->cancel_flag.load()) {
            impl_->result.metrics = impl_->live_metrics;
            impl_->result.metrics.cancelled = true;
            impl_->done_flag = true;
            return;
        }

        // Use the low-level API in both fast and live modes so final metrics
        // report the real iteration count and convergence state.
        const int sleep_ms = cfg.live_preview
            ? per_publish_sleep_for(cfg.preview_speed)
            : 0;
        double last_area = area0;
        bool converged = false;
        for (int iter = 0; iter < max_iter; ++iter) {
            if (impl_->cancel_flag.load()) break;

            mcs.contract_geometry();
            const int n_collapsed = (int)mcs.collapse_edges();
            const int n_split     = (int)mcs.split_faces();
            const int n_fixed     = (int)mcs.detect_degeneracies();

            const double area_now =
                CGAL::to_double(PMP::area(mcs.meso_skeleton()));
            const double area_ratio =
                (area0 > 0) ? std::fabs(last_area - area_now) / area0 : 0.0;

            impl_->live_metrics.iteration         = iter + 1;
            impl_->live_metrics.current_area      = area_now;
            impl_->live_metrics.area_change_ratio = area_ratio;
            impl_->live_metrics.collapsed_edges   = n_collapsed;
            impl_->live_metrics.split_faces       = n_split;
            impl_->live_metrics.fixed_vertices   += n_fixed;
            impl_->live_metrics.meso_vertices =
                count_vertices_adl(mcs.meso_skeleton());
            impl_->live_metrics.meso_faces =
                count_faces_adl(mcs.meso_skeleton());
            impl_->live_metrics.progress =
                std::min(1.0, (double)(iter + 1) / (double)max_iter);

            if (cfg.live_preview) {
                MCF_Snapshot snap;
                snap.metrics = impl_->live_metrics;
                snap.metrics.original_vertices = nv0;
                snap.metrics.original_faces    = nf0;
                snap.metrics.original_area     = area0;
                snap.metrics.max_iterations    = max_iter;
                dump_meso_to_snapshot(mcs, snap);
                snap.metrics.meso_vertices = (int)snap.vertices.size();
                snap.metrics.meso_faces    = (int)snap.triangles.size();

                const int new_gen = impl_->snap_generation.load() + 1;
                snap.generation = new_gen;
                {
                    std::lock_guard<std::mutex> lk(impl_->snap_mutex);
                    impl_->latest_snapshot = std::move(snap);
                }
                impl_->snap_generation.store(new_gen);
            }

            if (area_ratio < cfg.area_variation_factor) {
                converged = true;
                impl_->live_metrics.converged = true;
                break;
            }
            last_area = area_now;

            if (cfg.live_preview)
                interruptible_sleep(sleep_ms, impl_->cancel_flag);
        }
        impl_->live_metrics.converged = converged;

        if (impl_->cancel_flag.load()) {
            impl_->result.metrics = impl_->live_metrics;
            impl_->result.metrics.cancelled = true;
            impl_->done_flag = true;
            return;
        }

        mcs.convert_to_skeleton(skeleton);

        // Optional: collect correspondence lines (skeleton vertex -> original surface vertex).
        // This is best done WHILE we still have access to skeleton[v].vertices
        // which references the input TriangleMesh vertex_descriptors. The default keeps
        // this off unless explicitly requested.
        if (cfg.collect_correspondence) {
            const int limit = std::max(0, cfg.max_correspondence_lines);
            int count = 0;
            for (auto v : CGAL::make_range(boost::vertices(skeleton))) {
                if (count >= limit) break;
                const auto& sp = skeleton[v].point;
                for (auto vd : skeleton[v].vertices) {
                    if (count >= limit) break;
                    const auto& op = get(CGAL::vertex_point, mesh, vd);
                    MCF_Line line;
                    line.a = to_pod(sp);
                    line.b = to_pod(op);
                    impl_->result.correspondence_lines.push_back(line);
                    ++count;
                }
            }
        }

        // Optional per-input-vertex SDF (= distance to corresponding
        // skeleton point). Only input vertices appear in skeleton[sv].vertices;
        // vertices created mid-iteration by split_faces are not referenced
        // and stay at -1.0. We index by input mesh vertex idx().
        if (cfg.collect_sdf) {
            impl_->result.sdf_per_input_vertex.assign(nv0, -1.0);
            for (auto v : CGAL::make_range(boost::vertices(skeleton))) {
                const auto& sp = skeleton[v].point;
                for (auto vd : skeleton[v].vertices) {
                    const auto& op = get(CGAL::vertex_point, mesh, vd);
                    const double dx = CGAL::to_double(op.x() - sp.x());
                    const double dy = CGAL::to_double(op.y() - sp.y());
                    const double dz = CGAL::to_double(op.z() - sp.z());
                    const int idx = (int)vd.idx();
                    if (idx >= 0 && idx < nv0)
                        impl_->result.sdf_per_input_vertex[idx] =
                            std::sqrt(dx*dx + dy*dy + dz*dz);
                }
            }
        }
    } catch (const std::exception& ex) {
        impl_->set_error(MCF_ERR_AlgorithmException,
                         std::string("CGAL MCF threw: ") + ex.what());
        impl_->done_flag = true;
        return;
    } catch (...) {
        impl_->set_error(MCF_ERR_AlgorithmException,
                         "CGAL MCF threw unknown exception");
        impl_->done_flag = true;
        return;
    }

    // Final metrics.
    MCF_Metrics& m = impl_->result.metrics;
    m = impl_->live_metrics;
    m.skeleton_vertices = (int)boost::num_vertices(skeleton);
    m.skeleton_edges    = (int)boost::num_edges(skeleton);
    m.converged         = impl_->live_metrics.converged;
    m.cancelled         = false;
    m.progress          = 1.0;

    // Pack skeleton vertices + edges as POD.
    // boost::adjacency_list with vecS yields contiguous descriptors 0..n-1.
    const std::size_t skv = boost::num_vertices(skeleton);
    impl_->result.skeleton_vertices.reserve(skv);
    std::vector<MCF_Point3d> sv_pts(skv);
    for (auto v : CGAL::make_range(boost::vertices(skeleton))) {
        const std::size_t i = (std::size_t)v;
        sv_pts[i] = to_pod(skeleton[v].point);
    }
    impl_->result.skeleton_vertices = std::move(sv_pts);

    impl_->result.skeleton_edges.reserve(boost::num_edges(skeleton));
    for (auto e : CGAL::make_range(boost::edges(skeleton))) {
        const auto s = boost::source(e, skeleton);
        const auto t = boost::target(e, skeleton);
        MCF_Line line;
        line.a = to_pod(skeleton[s].point);
        line.b = to_pod(skeleton[t].point);
        impl_->result.skeleton_edges.push_back(line);
    }

    // No final meso mesh is packed here. Live mode exposes it through
    // meso_skeleton() snapshots.

    const auto t_total_end = Clock::now();
    m.elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                       t_total_end - t_total_begin).count() / 1000.0;

    impl_->done_flag = true;
}

void MCFSkeletonizationRunner::cancel() {
    impl_->cancel_flag.store(true);
}

bool MCFSkeletonizationRunner::is_cancelled() const {
    return impl_->cancel_flag.load();
}

bool MCFSkeletonizationRunner::is_done() const {
    return impl_->done_flag.load();
}

bool MCFSkeletonizationRunner::has_error() const {
    return impl_->has_error_flag.load();
}

std::string MCFSkeletonizationRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

bool MCFSkeletonizationRunner::poll_snapshot(int& last_generation,
                                             MCF_Snapshot& out) const
{
    const int cur = impl_->snap_generation.load();
    if (cur <= last_generation) return false;
    {
        std::lock_guard<std::mutex> lk(impl_->snap_mutex);
        out = impl_->latest_snapshot;
    }
    last_generation = cur;
    return true;
}

void MCFSkeletonizationRunner::get_result(MCF_Result& out) const {
    out = impl_->result;
}

MCF_Metrics MCFSkeletonizationRunner::result_metrics() const {
    return impl_->result.metrics;
}

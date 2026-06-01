// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "arap_deformation_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh_deformation.h>

#include <Eigen/Geometry>

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using TMesh   = CGAL::Surface_mesh<Point_3>;
using vertex_descriptor = boost::graph_traits<TMesh>::vertex_descriptor;
using Clock   = std::chrono::steady_clock;

template <CGAL::Deformation_algorithm_tag TAG>
using Deformer_t = CGAL::Surface_mesh_deformation<TMesh,
    CGAL::Default, CGAL::Default, TAG>;

struct ARAPDeformationRunner::Impl {
    std::vector<ARAP_Point3d>  input_verts;
    std::vector<ARAP_Triangle> input_tris;
    bool has_input = false;

    ARAP_Selection selection;
    std::vector<ARAP_ControlTransform> transforms;
    ARAP_Config cfg{};

    ARAP_Result result{};
    ARAP_Snapshot latest_snapshot{};
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
        result = ARAP_Result{};
        {
            std::lock_guard<std::mutex> lk(snap_mutex);
            latest_snapshot = ARAP_Snapshot{};
        }
        snap_generation = 0;
    }

    void set_error(ARAP_ErrorCode code, const std::string& msg) {
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

bool build_input_mesh(const std::vector<ARAP_Point3d>&  pv,
                      const std::vector<ARAP_Triangle>& pt,
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
            continue;
        }
        TMesh::Face_index fd =
            mesh.add_face(vmap_out[t.v0], vmap_out[t.v1], vmap_out[t.v2]);
        if (fd == TMesh::null_face())
            mesh.add_face(vmap_out[t.v0], vmap_out[t.v2], vmap_out[t.v1]);
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

inline ARAP_Point3d to_pod(const Point_3& p) {
    return ARAP_Point3d{CGAL::to_double(p.x()),
                        CGAL::to_double(p.y()),
                        CGAL::to_double(p.z())};
}

// Group transform: rotate around (cx,cy,cz) by ZYX Euler, then translate.
// Returns target = R * (origin - center) + center + T.
Point_3 apply_transform_to_origin(const ARAP_ControlTransform& xf,
                                  const Point_3& origin) {
    const double cx = xf.rot_cx, cy = xf.rot_cy, cz = xf.rot_cz;
    const double dx = CGAL::to_double(origin.x()) - cx;
    const double dy = CGAL::to_double(origin.y()) - cy;
    const double dz = CGAL::to_double(origin.z()) - cz;
    const double deg2rad = 3.141592653589793 / 180.0;
    Eigen::AngleAxisd ax(xf.rx_deg * deg2rad, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd ay(xf.ry_deg * deg2rad, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd az(xf.rz_deg * deg2rad, Eigen::Vector3d::UnitZ());
    Eigen::Quaterniond q = az * ay * ax;
    Eigen::Vector3d d(dx * xf.scale, dy * xf.scale, dz * xf.scale);
    Eigen::Vector3d r = q * d;
    return Point_3(r.x() + cx + xf.tx,
                   r.y() + cy + xf.ty,
                   r.z() + cz + xf.tz);
}

void compute_displacement(const std::vector<ARAP_Point3d>& orig,
                          const std::vector<ARAP_Point3d>& def,
                          std::vector<double>& disp,
                          double& max_disp, double& mean_disp) {
    const int n = (int)orig.size();
    disp.assign(n, 0.0);
    max_disp = 0.0; mean_disp = 0.0;
    if (n == 0 || (int)def.size() != n) return;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double dx = def[i].x - orig[i].x;
        const double dy = def[i].y - orig[i].y;
        const double dz = def[i].z - orig[i].z;
        const double d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        disp[i] = d;
        if (d > max_disp) max_disp = d;
        sum += d;
    }
    mean_disp = sum / (double)n;
}

// Live-preview pacing: per-snapshot sleep in milliseconds. Independent
// of the algorithm's iteration count so even tiny meshes can be inspected.
int preview_sleep_for(int speed) {
    switch (speed) {
        case ARAP_PREVIEW_Slow:   return 320;
        case ARAP_PREVIEW_Fast:   return 60;
        case ARAP_PREVIEW_Normal:
        default:                  return 140;
    }
}

void interruptible_sleep(int ms, const std::atomic<bool>& cancel) {
    if (ms <= 0) return;
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end) {
        if (cancel.load(std::memory_order_relaxed)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Mode-templated runner body. Returns true on full success.
template <CGAL::Deformation_algorithm_tag TAG>
bool run_with_tag(ARAPDeformationRunner::Impl& impl,
                  TMesh& mesh,
                  const std::vector<TMesh::Vertex_index>& vmap,
                  const std::vector<ARAP_Point3d>& orig_input,
                  std::string& err)
{
    Deformer_t<TAG> dm(mesh);

    // Insert ROI.
    for (int vid : impl.selection.roi_vertices) {
        if (vid < 0 || vid >= (int)vmap.size()) continue;
        dm.insert_roi_vertex(vmap[vid]);
    }
    // Insert control vertices.
    for (int vid : impl.selection.control_vertices) {
        if (vid < 0 || vid >= (int)vmap.size()) continue;
        dm.insert_control_vertex(vmap[vid]);
    }

    if (impl.cancel_flag.load()) return false;

    if (!dm.preprocess()) {
        err = "preprocess() failed (rank-deficient ARAP system; "
              "try adding control vertices, growing ROI, or "
              "verifying the ROI is connected)";
        impl.result.preprocess_ok = false;
        return false;
    }
    impl.result.preprocess_ok = true;

    // Group transforms keyed by group_id.
    std::unordered_map<int, ARAP_ControlTransform> xf_by_group;
    for (const auto& xf : impl.transforms)
        xf_by_group[xf.group_id] = xf;

    const int nc = (int)impl.selection.control_vertices.size();
    const int ng = (int)impl.selection.control_group_ids.size();

    // Apply targets at parameter t in [0, 1] (t=1 = full transform).
    auto set_targets_at_t = [&](double t) {
        for (int i = 0; i < nc; ++i) {
            const int vid = impl.selection.control_vertices[i];
            if (vid < 0 || vid >= (int)vmap.size()) continue;
            const int gid = (i < ng) ? impl.selection.control_group_ids[i] : 0;
            auto it = xf_by_group.find(gid);
            if (it == xf_by_group.end()) continue;
            const auto& full_xf = it->second;
            ARAP_ControlTransform xf_t = full_xf;
            xf_t.tx     = full_xf.tx     * t;
            xf_t.ty     = full_xf.ty     * t;
            xf_t.tz     = full_xf.tz     * t;
            xf_t.rx_deg = full_xf.rx_deg * t;
            xf_t.ry_deg = full_xf.ry_deg * t;
            xf_t.rz_deg = full_xf.rz_deg * t;
            xf_t.scale  = 1.0 + (full_xf.scale - 1.0) * t;
            const auto& op = orig_input[vid];
            Point_3 origin(op.x, op.y, op.z);
            Point_3 target = apply_transform_to_origin(xf_t, origin);
            dm.set_target_position(vmap[vid], target);
        }
    };

    // Publish a snapshot reading the deformer's current mesh state.
    auto publish_snapshot = [&](int step, int total) {
        const std::size_t nv = orig_input.size();
        ARAP_Snapshot snap;
        snap.step              = step;
        snap.total_steps       = total;
        snap.vertices.assign(nv, ARAP_Point3d{0, 0, 0});
        for (int i = 0; i < (int)vmap.size(); ++i)
            snap.vertices[i] = to_pod(mesh.point(vmap[i]));
        snap.vertex_displacement.assign(nv, 0.0);
        double maxd = 0.0, sumd = 0.0;
        for (std::size_t i = 0; i < nv; ++i) {
            const double dx = snap.vertices[i].x - orig_input[i].x;
            const double dy = snap.vertices[i].y - orig_input[i].y;
            const double dz = snap.vertices[i].z - orig_input[i].z;
            const double d  = std::sqrt(dx*dx + dy*dy + dz*dz);
            snap.vertex_displacement[i] = d;
            if (d > maxd) maxd = d;
            sumd += d;
        }
        snap.max_displacement  = maxd;
        snap.mean_displacement = (nv > 0) ? (sumd / (double)nv) : 0.0;

        const int gen = impl.snap_generation.load() + 1;
        snap.generation = gen;
        {
            std::lock_guard<std::mutex> lk(impl.snap_mutex);
            impl.latest_snapshot = std::move(snap);
        }
        impl.snap_generation.store(gen);
    };

    if (impl.cancel_flag.load()) return false;

    if (!impl.cfg.live_preview) {
        // Fast path: full target in one shot.
        set_targets_at_t(1.0);
        if (impl.cancel_flag.load()) return false;
        dm.deform((unsigned int)std::max(1, impl.cfg.iterations),
                  impl.cfg.tolerance);
    } else {
        // Live path: linearly interpolate the transform from identity
        // (t=0, mesh stays put) to full (t=1) over preview_steps, calling
        // deform() with a small slice of iterations per step so the user
        // sees the mesh smoothly progress toward the final position.
        const int total = std::max(2, impl.cfg.preview_steps);
        const int iter_per_step =
            std::max(1, impl.cfg.iterations / total);
        const int sleep_ms = preview_sleep_for(impl.cfg.preview_speed);
        for (int step = 1; step <= total; ++step) {
            if (impl.cancel_flag.load()) break;
            const double t = (double)step / (double)total;
            set_targets_at_t(t);
            dm.deform((unsigned int)iter_per_step, impl.cfg.tolerance);
            publish_snapshot(step, total);
            interruptible_sleep(sleep_ms, impl.cancel_flag);
        }
        // Final settle pass on the full target so the result mesh matches
        // fast mode within tolerance, regardless of how the interpolation
        // landed on the last step.
        if (!impl.cancel_flag.load()) {
            set_targets_at_t(1.0);
            dm.deform((unsigned int)std::max(1, impl.cfg.iterations),
                      impl.cfg.tolerance);
        }
    }
    return true;
}

} // namespace

ARAPDeformationRunner::ARAPDeformationRunner()
    : impl_(std::make_unique<Impl>()) {}
ARAPDeformationRunner::~ARAPDeformationRunner() = default;

void ARAPDeformationRunner::set_input(
    const std::vector<ARAP_Point3d>&  verts,
    const std::vector<ARAP_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void ARAPDeformationRunner::set_selection(const ARAP_Selection& sel) {
    impl_->selection = sel;
}

void ARAPDeformationRunner::set_control_transforms(
    const std::vector<ARAP_ControlTransform>& transforms)
{
    impl_->transforms = transforms;
}

void ARAPDeformationRunner::run(const ARAP_Config& cfg) {
    impl_->reset_state();
    impl_->cfg = cfg;

    if (!impl_->has_input) {
        impl_->set_error(ARAP_ERR_EmptyInput, "no input mesh");
        impl_->done_flag = true;
        return;
    }
    if (impl_->selection.roi_vertices.empty()) {
        impl_->set_error(ARAP_ERR_EmptyROI,
                         "no ROI vertices (pick or grow an ROI first)");
        impl_->done_flag = true;
        return;
    }
    if (impl_->selection.control_vertices.empty()) {
        impl_->set_error(ARAP_ERR_EmptyControls,
                         "no control vertices (pick at least one)");
        impl_->done_flag = true;
        return;
    }

    const auto t_begin = Clock::now();

    TMesh mesh;
    std::vector<TMesh::Vertex_index> vmap;
    std::string build_err;
    if (!build_input_mesh(impl_->input_verts, impl_->input_tris,
                          mesh, vmap, build_err)) {
        impl_->set_error(ARAP_ERR_BuildFailed, build_err);
        impl_->done_flag = true;
        return;
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        impl_->set_error(ARAP_ERR_NotTriangleMesh,
                         "input is not a pure triangle mesh");
        impl_->done_flag = true;
        return;
    }

    impl_->result.original_vertices = (int)impl_->input_verts.size();
    impl_->result.original_faces    = (int)mesh.number_of_faces();
    impl_->result.roi_count         = (int)impl_->selection.roi_vertices.size();
    impl_->result.control_count     = (int)impl_->selection.control_vertices.size();
    impl_->result.iterations        = cfg.iterations;

    std::string algo_err;
    bool ok = false;
    try {
        switch (cfg.mode) {
            case ARAP_MODE_OriginalARAP:
                ok = run_with_tag<CGAL::ORIGINAL_ARAP>(
                    *impl_, mesh, vmap, impl_->input_verts, algo_err);
                break;
            case ARAP_MODE_SRE_ARAP:
                ok = run_with_tag<CGAL::SRE_ARAP>(
                    *impl_, mesh, vmap, impl_->input_verts, algo_err);
                break;
            case ARAP_MODE_SpokesAndRims:
            default:
                ok = run_with_tag<CGAL::SPOKES_AND_RIMS>(
                    *impl_, mesh, vmap, impl_->input_verts, algo_err);
                break;
        }
    } catch (const std::exception& ex) {
        impl_->set_error(ARAP_ERR_AlgorithmException,
                         std::string("CGAL ARAP threw: ") + ex.what());
        impl_->done_flag = true;
        return;
    } catch (...) {
        impl_->set_error(ARAP_ERR_AlgorithmException,
                         "CGAL ARAP threw unknown exception");
        impl_->done_flag = true;
        return;
    }

    if (!ok) {
        if (impl_->cancel_flag.load()) {
            impl_->result.iterations = 0;
        } else if (!algo_err.empty()) {
            impl_->set_error(
                impl_->result.preprocess_ok
                    ? ARAP_ERR_AlgorithmException
                    : ARAP_ERR_PreprocessFailed,
                algo_err);
        }
        impl_->done_flag = true;
        return;
    }

    // Dump deformed positions in input-vertex-index order.
    impl_->result.vertices.assign(impl_->input_verts.size(),
                                  ARAP_Point3d{0, 0, 0});
    for (int i = 0; i < (int)vmap.size(); ++i)
        impl_->result.vertices[i] = to_pod(mesh.point(vmap[i]));

    compute_displacement(impl_->input_verts, impl_->result.vertices,
                         impl_->result.vertex_displacement,
                         impl_->result.max_displacement,
                         impl_->result.mean_displacement);

    const auto t_end = Clock::now();
    impl_->result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(
            t_end - t_begin).count() / 1000.0;

    impl_->done_flag = true;
}

void ARAPDeformationRunner::cancel() {
    impl_->cancel_flag.store(true);
}

bool ARAPDeformationRunner::is_cancelled() const {
    return impl_->cancel_flag.load();
}

bool ARAPDeformationRunner::is_done() const {
    return impl_->done_flag.load();
}

bool ARAPDeformationRunner::has_error() const {
    return impl_->has_error_flag.load();
}

std::string ARAPDeformationRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void ARAPDeformationRunner::set_error(ARAP_ErrorCode code,
                                      const std::string& msg) {
    impl_->set_error(code, msg);
}

bool ARAPDeformationRunner::poll_snapshot(int& last_generation,
                                          ARAP_Snapshot& out) const {
    const int cur = impl_->snap_generation.load();
    if (cur <= last_generation) return false;
    {
        std::lock_guard<std::mutex> lk(impl_->snap_mutex);
        out = impl_->latest_snapshot;
    }
    last_generation = cur;
    return true;
}

void ARAPDeformationRunner::get_result(ARAP_Result& out) const {
    out = impl_->result;
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "parameterization_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh_parameterization/parameterize.h>
#include <CGAL/Surface_mesh_parameterization/LSCM_parameterizer_3.h>
#include <CGAL/Surface_mesh_parameterization/ARAP_parameterizer_3.h>
#include <CGAL/Surface_mesh_parameterization/Two_vertices_parameterizer_3.h>
#include <CGAL/Surface_mesh_parameterization/Error_code.h>
#include <CGAL/Surface_mesh_parameterization/measure_distortion.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/Seam_mesh.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/border.h>

using Kernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using Point_2 = Kernel::Point_2;
using Mesh    = CGAL::Surface_mesh<Point_3>;
using vertex_descriptor = Mesh::Vertex_index;
using halfedge_descriptor = Mesh::Halfedge_index;
using face_descriptor = Mesh::Face_index;
using Clock   = std::chrono::steady_clock;

namespace SMP = CGAL::Surface_mesh_parameterization;

struct ParameterizationRunner::Impl {
    std::vector<PARAM_Point3d>  input_verts;
    std::vector<PARAM_Triangle> input_tris;
    bool has_input = false;

    PARAM_Config cfg;
    PARAM_WakeCallback wake_cb = nullptr;
    void* wake_user_data = nullptr;
    std::vector<PARAM_SeamPath> seams;

    PARAM_Result result{};
    mutable std::mutex result_mutex;

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
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            result = PARAM_Result{};
        }
    }

    void set_error(PARAM_ErrorCode code, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message = msg;
        }
        has_error_flag = true;
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            result.error_code = code;
            std::snprintf(result.error_message, sizeof(result.error_message),
                          "%s", msg.c_str());
        }
    }
};

namespace {

bool build_cgal_mesh(const std::vector<PARAM_Point3d>&  pv,
                     const std::vector<PARAM_Triangle>& pt,
                     Mesh& mesh,
                     std::vector<vertex_descriptor>& vmap,
                     std::string& err)
{
    mesh.clear();
    if (pv.empty() || pt.empty()) {
        err = "empty input mesh"; return false;
    }
    const int nv = (int)pv.size();
    vmap.clear(); vmap.reserve(nv);
    for (const auto& p : pv)
        vmap.push_back(mesh.add_vertex(Point_3(p.x, p.y, p.z)));
    for (const auto& t : pt) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
            t.v0 >= nv || t.v1 >= nv || t.v2 >= nv ||
            t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2)
            continue;
        auto fd = mesh.add_face(vmap[t.v0], vmap[t.v1], vmap[t.v2]);
        if (fd == Mesh::null_face())
            mesh.add_face(vmap[t.v0], vmap[t.v2], vmap[t.v1]);
    }
    if (mesh.number_of_faces() == 0) {
        err = "no valid faces after conversion"; return false;
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        err = "not a triangle mesh after conversion"; return false;
    }
    return true;
}

// Find a border halfedge. Prefer the border with the most edges.
halfedge_descriptor find_border_halfedge(const Mesh& mesh, std::string& err) {
    halfedge_descriptor best = boost::graph_traits<Mesh>::null_halfedge();
    int best_len = 0;
    for (auto h : halfedges(mesh)) {
        if (!CGAL::is_border(h, mesh)) continue;
        int len = 0;
        auto hh = h;
        do { ++len; hh = next(hh, mesh); } while (hh != h);
        if (len > best_len) { best_len = len; best = h; }
    }
    if (best == boost::graph_traits<Mesh>::null_halfedge())
        err = "no boundary found (mesh is closed; add a seam)";
    return best;
}

// Extract boundary vertex indices (one loop) from the border halfedge.
void extract_boundary_loop(const Mesh& mesh, halfedge_descriptor bhd,
                           std::vector<int>& out_vids) {
    out_vids.clear();
    if (bhd == boost::graph_traits<Mesh>::null_halfedge()) return;
    auto h = bhd;
    do {
        out_vids.push_back((int)source(h, mesh).idx());
        h = next(h, mesh);
    } while (h != bhd);
}

void build_face_uv_template(const Mesh& mesh,
                            std::vector<PARAM_FaceUV>& face_uvs) {
    face_uvs.clear();
    face_uvs.reserve(mesh.number_of_faces());
    for (auto f : mesh.faces()) {
        PARAM_FaceUV fu;
        fu.face_id = (int)f.idx();
        int ci = 0;
        for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
            if (ci < 3)
                fu.vertex_id[ci] = (int)v.idx();
            ++ci;
        }
        face_uvs.push_back(fu);
    }
}

double compute_face_area_3d(const Mesh& mesh, face_descriptor f) {
    return CGAL::Polygon_mesh_processing::face_area(f, mesh,
        CGAL::parameters::geom_traits(Kernel()));
}

// Compute 2D triangle area (signed).
double uv_triangle_area(double u0, double v0, double u1, double v1,
                        double u2, double v2) {
    return 0.5 * ((u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0));
}

// Distortion for a single face: assumes fu has vertex_id[3] + uv[3] filled.
// Uses base mesh vertex positions via vmap.
void compute_one_face_distortion(const Mesh& mesh,
                                  const std::vector<vertex_descriptor>& vmap,
                                  face_descriptor f,
                                  PARAM_FaceUV& fu,
                                  double& area_d_out,
                                  double& angle_d_out,
                                  bool& flipped_out)
{
    const double area3d = compute_face_area_3d(mesh, f);
    const double area2d = uv_triangle_area(
        fu.uv[0].u, fu.uv[0].v,
        fu.uv[1].u, fu.uv[1].v,
        fu.uv[2].u, fu.uv[2].v);
    flipped_out = (area2d < 0.0);
    const double abs2d = std::abs(area2d);
    if (area3d > 1e-20 && abs2d > 1e-20) {
        area_d_out = std::log(std::max(area3d / abs2d, abs2d / area3d));
    } else {
        area_d_out = (area3d < 1e-20 && abs2d < 1e-20) ? 0.0 : 10.0;
    }

    double a3d[3], a2d[3];
    {
        auto h = halfedge(f, mesh);
        for (int i = 0; i < 3; ++i) {
            const auto& p0 = mesh.point(source(h, mesh));
            const auto& p1 = mesh.point(target(h, mesh));
            const auto& p2 = mesh.point(target(next(h, mesh), mesh));
            double dx1 = CGAL::to_double(p0.x() - p1.x());
            double dy1 = CGAL::to_double(p0.y() - p1.y());
            double dz1 = CGAL::to_double(p0.z() - p1.z());
            double dx2 = CGAL::to_double(p2.x() - p1.x());
            double dy2 = CGAL::to_double(p2.y() - p1.y());
            double dz2 = CGAL::to_double(p2.z() - p1.z());
            double dot = dx1*dx2 + dy1*dy2 + dz1*dz2;
            double l1 = std::sqrt(dx1*dx1 + dy1*dy1 + dz1*dz1);
            double l2 = std::sqrt(dx2*dx2 + dy2*dy2 + dz2*dz2);
            a3d[i] = (l1 > 1e-20 && l2 > 1e-20)
                ? std::acos(std::max(-1.0, std::min(1.0, dot / (l1 * l2)))) : 0.0;
            h = next(h, mesh);
        }
    }
    for (int i = 0; i < 3; ++i) {
        const double u0 = fu.uv[i].u, v0 = fu.uv[i].v;
        const double u1 = fu.uv[(i+1)%3].u, v1 = fu.uv[(i+1)%3].v;
        const double u2 = fu.uv[(i+2)%3].u, v2 = fu.uv[(i+2)%3].v;
        double dx1 = u0 - u1, dy1 = v0 - v1;
        double dx2 = u2 - u1, dy2 = v2 - v1;
        double dot = dx1*dx2 + dy1*dy2;
        double l1 = std::sqrt(dx1*dx1 + dy1*dy1);
        double l2 = std::sqrt(dx2*dx2 + dy2*dy2);
        a2d[i] = (l1 > 1e-20 && l2 > 1e-20)
            ? std::acos(std::max(-1.0, std::min(1.0, dot / (l1 * l2)))) : 0.0;
    }
    double ang_diff_sum = 0.0;
    for (int i = 0; i < 3; ++i) ang_diff_sum += std::abs(a3d[i] - a2d[i]);
    angle_d_out = ang_diff_sum / 3.0;
}

// Open mesh path: builds face_uvs from per-vertex UV array.
void compute_per_face_distortion(const Mesh& mesh,
                                  const std::vector<vertex_descriptor>& vmap,
                                  const std::vector<Point_2>& uv_per_vertex,
                                  std::vector<PARAM_FaceUV>& face_uvs,
                                  PARAM_Result& r)
{
    face_uvs.clear();
    face_uvs.reserve(mesh.number_of_faces());

    double total_area_d = 0.0, total_angle_d = 0.0;
    int area_count = 0, angle_count = 0;
    double max_area_d = 0.0, max_angle_d = 0.0;
    int flipped = 0, degenerate = 0;

    for (auto f : mesh.faces()) {
        PARAM_FaceUV fu;
        fu.face_id = (int)f.idx();
        int ci = 0;
        for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
            if (ci < 3) {
                fu.vertex_id[ci] = (int)v.idx();
                const auto& uv = uv_per_vertex[v.idx()];
                fu.uv[ci].u = CGAL::to_double(uv.x());
                fu.uv[ci].v = CGAL::to_double(uv.y());
            }
            ++ci;
        }
        if (ci != 3) {
            fu.flipped = false; fu.area_distortion = 0; fu.angle_distortion = 0;
            face_uvs.push_back(fu);
            ++degenerate;
            continue;
        }
        compute_one_face_distortion(mesh, vmap, f, fu,
            fu.area_distortion, fu.angle_distortion, fu.flipped);
        if (fu.flipped) ++flipped;
        total_area_d += fu.area_distortion; ++area_count;
        total_angle_d += fu.angle_distortion; ++angle_count;
        if (fu.area_distortion > max_area_d) max_area_d = fu.area_distortion;
        if (fu.angle_distortion > max_angle_d) max_angle_d = fu.angle_distortion;
        fu.l2_stretch = 0.0;
        face_uvs.push_back(fu);
    }

    r.flipped_count   = flipped;
    r.degenerate_count = degenerate;
    r.mean_area_distortion  = area_count  ? total_area_d  / area_count  : 0.0;
    r.max_area_distortion   = max_area_d;
    r.mean_angle_distortion = angle_count ? total_angle_d / angle_count : 0.0;
    r.max_angle_distortion  = max_angle_d;
}

// Snapshot-collecting visitor. Defined at file scope because
// MSVC does not allow template member functions in local classes.
struct ARAPSnapVisitor {
    PARAM_Result* result = nullptr;
    std::mutex* result_mutex = nullptr;
    int nv = 0;
    const std::vector<Mesh::Vertex_index>* vmap_p = nullptr;
    int stride = 1;
    int delay_ms = 0;
    bool enabled = true;
    PARAM_WakeCallback wake_cb = nullptr;
    void* wake_user_data = nullptr;

    template <typename UVMap, typename VIMap>
    void after_iteration(unsigned int ite, double energy,
                          const Mesh& /*mesh*/,
                          const UVMap& uvmap,
                          const VIMap& /*vimap*/) const {
        if (!enabled || !result || !result_mutex || !vmap_p) return;
        const int s = stride <= 0 ? 1 : stride;
        if (ite != 0 && (int)ite % s != 0) return;
        PARAM_ARAPSnapshot snap;
        snap.iteration = (int)ite;
        snap.energy    = energy;
        snap.vertex_uvs.resize(nv);
        for (int i = 0; i < nv; ++i) {
            const Point_2& uv = uvmap[(*vmap_p)[i]];
            snap.vertex_uvs[i].u = CGAL::to_double(uv.x());
            snap.vertex_uvs[i].v = CGAL::to_double(uv.y());
        }
        {
            std::lock_guard<std::mutex> lk(*result_mutex);
            result->arap_snapshots.push_back(std::move(snap));
        }
        if (wake_cb)
            wake_cb(wake_user_data);
        if (delay_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
};

void publish_arap_final_transition(PARAM_Result& result,
                                   std::mutex& result_mutex,
                                   const std::vector<Point_2>& final_uvs,
                                   const PARAM_Config& cfg,
                                   PARAM_WakeCallback wake_cb,
                                   void* wake_user_data) {
    if (!cfg.show_process || final_uvs.empty())
        return;

    PARAM_ARAPSnapshot last;
    {
        std::lock_guard<std::mutex> lk(result_mutex);
        if (!result.arap_snapshots.empty())
            last = result.arap_snapshots.back();
    }

    PARAM_ARAPSnapshot final_snap;
    final_snap.iteration = last.iteration;
    final_snap.energy = last.energy;
    final_snap.vertex_uvs.resize(final_uvs.size());
    for (size_t i = 0; i < final_uvs.size(); ++i) {
        final_snap.vertex_uvs[i].u = CGAL::to_double(final_uvs[i].x());
        final_snap.vertex_uvs[i].v = CGAL::to_double(final_uvs[i].y());
    }

    if (last.vertex_uvs.size() != final_snap.vertex_uvs.size()) {
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            result.arap_snapshots.push_back(std::move(final_snap));
        }
        if (wake_cb)
            wake_cb(wake_user_data);
        return;
    }

    double max_delta2 = 0.0;
    for (size_t i = 0; i < final_snap.vertex_uvs.size(); ++i) {
        const double du = final_snap.vertex_uvs[i].u - last.vertex_uvs[i].u;
        const double dv = final_snap.vertex_uvs[i].v - last.vertex_uvs[i].v;
        max_delta2 = std::max(max_delta2, du * du + dv * dv);
    }
    if (max_delta2 < 1e-20)
        return;

    const int frames = cfg.arap_snapshot_delay_ms >= 150 ? 10 : 8;
    const int delay_ms = std::max(0, cfg.arap_snapshot_delay_ms);
    for (int f = 1; f <= frames; ++f) {
        const double t = (double)f / (double)frames;
        PARAM_ARAPSnapshot snap;
        snap.iteration = last.iteration;
        snap.energy = final_snap.energy;
        snap.vertex_uvs.resize(final_snap.vertex_uvs.size());
        for (size_t i = 0; i < final_snap.vertex_uvs.size(); ++i) {
            snap.vertex_uvs[i].u =
                last.vertex_uvs[i].u * (1.0 - t) + final_snap.vertex_uvs[i].u * t;
            snap.vertex_uvs[i].v =
                last.vertex_uvs[i].v * (1.0 - t) + final_snap.vertex_uvs[i].v * t;
        }
        {
            std::lock_guard<std::mutex> lk(result_mutex);
            result.arap_snapshots.push_back(std::move(snap));
        }
        if (wake_cb)
            wake_cb(wake_user_data);
        if (delay_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

} // namespace

ParameterizationRunner::ParameterizationRunner()
    : impl_(std::make_unique<Impl>()) {}
ParameterizationRunner::~ParameterizationRunner() = default;

void ParameterizationRunner::set_input(
    const std::vector<PARAM_Point3d>&  verts,
    const std::vector<PARAM_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void ParameterizationRunner::set_seams(
    const std::vector<PARAM_SeamPath>& seams)
{
    impl_->seams = seams;
}

void ParameterizationRunner::clear_seams() {
    impl_->seams.clear();
}

void ParameterizationRunner::set_config(const PARAM_Config& cfg) {
    impl_->cfg = cfg;
}

void ParameterizationRunner::set_wake_callback(PARAM_WakeCallback cb,
                                               void* user_data) {
    impl_->wake_cb = cb;
    impl_->wake_user_data = user_data;
}

void ParameterizationRunner::run() {
    impl_->reset_state();

    if (!impl_->has_input) {
        impl_->set_error(PARAM_ERR_EmptyInput, "no input mesh");
        impl_->done_flag = true;
        return;
    }

    const auto t0 = Clock::now();

    Mesh mesh;
    std::vector<vertex_descriptor> vmap;
    std::string build_err;
    if (!build_cgal_mesh(impl_->input_verts, impl_->input_tris,
                          mesh, vmap, build_err)) {
        impl_->set_error(PARAM_ERR_BuildFailed, build_err);
        impl_->done_flag = true;
        return;
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        impl_->set_error(PARAM_ERR_NotTriangleMesh,
                         "input is not a pure triangle mesh");
        impl_->done_flag = true;
        return;
    }

    // Check closed/open.
    bool has_boundary = false;
    for (auto h : halfedges(mesh)) {
        if (CGAL::is_border(h, mesh)) { has_boundary = true; break; }
    }

    if (!has_boundary && impl_->seams.empty()) {
        impl_->set_error(PARAM_ERR_ClosedNoSeam,
            "closed mesh requires a seam before UV parameterization");
        impl_->done_flag = true;
        return;
    }

    PARAM_Result& r = impl_->result;
    const int nv = (int)vmap.size();
    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        r.original_vertices = nv;
        r.original_faces    = (int)mesh.number_of_faces();
        r.is_closed         = !has_boundary;
    }

    if (!impl_->seams.empty()) {
        // Closed-mesh path via CGAL Seam_mesh. Mark consecutive vertex
        // pairs from each PARAM_SeamPath as seam edges; the seam mesh then
        // exposes a virtual border along the seam, which LSCM uses as the
        // free boundary.
        using SeamEdgePmap   = Mesh::Property_map<Mesh::Edge_index, bool>;
        using SeamVertexPmap = Mesh::Property_map<vertex_descriptor, bool>;
        using SeamMesh       = CGAL::Seam_mesh<Mesh, SeamEdgePmap, SeamVertexPmap>;

        SeamEdgePmap seam_edge_pm =
            mesh.add_property_map<Mesh::Edge_index, bool>("e:on_seam", false).first;
        SeamVertexPmap seam_vertex_pm =
            mesh.add_property_map<vertex_descriptor, bool>("v:on_seam", false).first;
        SeamMesh seam_mesh(mesh, seam_edge_pm, seam_vertex_pm);

        int added_edges = 0;
        for (const auto& sp : impl_->seams) {
            for (size_t i = 0; i + 1 < sp.vertex_ids.size(); ++i) {
                int v1 = sp.vertex_ids[i], v2 = sp.vertex_ids[i + 1];
                if (v1 < 0 || v2 < 0 || v1 >= nv || v2 >= nv) continue;
                if (seam_mesh.add_seam(vmap[v1], vmap[v2])) ++added_edges;
            }
        }
        if (added_edges == 0) {
            impl_->set_error(PARAM_ERR_BuildFailed,
                "no valid seam edges: ensure consecutive vertices are adjacent on the mesh");
            impl_->done_flag = true;
            return;
        }

        using SHalfedge = boost::graph_traits<SeamMesh>::halfedge_descriptor;
        SHalfedge bhd_seam =
            CGAL::Polygon_mesh_processing::longest_border(seam_mesh).first;
        if (bhd_seam == boost::graph_traits<SeamMesh>::null_halfedge()) {
            impl_->set_error(PARAM_ERR_NoBoundary,
                "current seams do not open the mesh -- add more seam paths");
            impl_->done_flag = true;
            return;
        }

        // UV map keyed by base mesh halfedges; LSCM writes the canonical
        // halfedge entry for each (seam) vertex.
        auto uv_pm = mesh.add_property_map<halfedge_descriptor, Point_2>(
            "h:uv", Point_2(0, 0)).first;

        SMP::Error_code seam_err;
        const char* seam_algo = "LSCM";
        if (impl_->cfg.algorithm == PARAM_ALGO_ARAP) {
            SMP::ARAP_parameterizer_3<SeamMesh> param(
                    SMP::Two_vertices_parameterizer_3<SeamMesh>(),
                    {},
                    impl_->cfg.arap_lambda,
                    (unsigned int)impl_->cfg.arap_iterations,
                    impl_->cfg.arap_tolerance);
            seam_err = SMP::parameterize(seam_mesh, param, bhd_seam, uv_pm);
            seam_algo = "ARAP";
        } else {
            SMP::LSCM_parameterizer_3<SeamMesh> param;
            seam_err = SMP::parameterize(seam_mesh, param, bhd_seam, uv_pm);
        }

        if (impl_->cancel_flag.load()) {
            impl_->done_flag = true;
            return;
        }
        if (seam_err != SMP::OK) {
            std::string msg = std::string(seam_algo) + " (with seams) failed: ";
            switch (seam_err) {
            case SMP::ERROR_NO_TOPOLOGICAL_DISC:
                msg += "seams do not produce a topological disc"; break;
            case SMP::ERROR_BORDER_TOO_SHORT: msg += "border too short"; break;
            case SMP::ERROR_CANNOT_SOLVE_LINEAR_SYSTEM:
                msg += "linear solver failed"; break;
            case SMP::ERROR_NO_1_TO_1_MAPPING: msg += "no 1-to-1 mapping"; break;
            default: msg += "code " + std::to_string((int)seam_err); break;
            }
            impl_->set_error(PARAM_ERR_AlgorithmFailed, msg);
            impl_->done_flag = true;
            return;
        }

        // Build per-face per-corner UV by walking each face's seam-mesh
        // halfedges. target(h, seam_mesh) gives a Seam_mesh_vertex_descriptor
        // that wraps the canonical base-mesh halfedge for the vertex on that
        // side of any seam, so get(uv_pm, ...) returns the right UV.
        r.face_uvs.clear();
        r.face_uvs.reserve(mesh.number_of_faces());

        double total_area_d = 0.0, total_angle_d = 0.0;
        int area_count = 0, angle_count = 0;
        double max_area_d = 0.0, max_angle_d = 0.0;
        int flipped = 0, degenerate = 0;

        for (auto f : faces(seam_mesh)) {
            PARAM_FaceUV fu;
            fu.face_id = (int)f.idx();
            int ci = 0;
            auto hf = halfedge(f, seam_mesh);
            auto h = hf;
            do {
                if (ci >= 3) break;
                auto sv = target(h, seam_mesh);
                // The seam vertex descriptor wraps the canonical base-mesh
                // halfedge for that vertex on that side of any seam; the
                // halfedge UV map keys on the base halfedge directly.
                halfedge_descriptor base_h = sv.hd.tmhd;
                vertex_descriptor base_v = target(base_h, mesh);
                fu.vertex_id[ci] = (int)base_v.idx();
                const Point_2& uv = uv_pm[base_h];
                fu.uv[ci].u = CGAL::to_double(uv.x());
                fu.uv[ci].v = CGAL::to_double(uv.y());
                ++ci;
                h = next(h, seam_mesh);
            } while (h != hf);

            if (ci != 3) {
                fu.flipped = false; fu.area_distortion = 0; fu.angle_distortion = 0;
                fu.l2_stretch = 0.0;
                r.face_uvs.push_back(fu);
                ++degenerate;
                continue;
            }
            compute_one_face_distortion(mesh, vmap, f, fu,
                fu.area_distortion, fu.angle_distortion, fu.flipped);
            if (fu.flipped) ++flipped;
            total_area_d  += fu.area_distortion;  ++area_count;
            total_angle_d += fu.angle_distortion; ++angle_count;
            if (fu.area_distortion  > max_area_d)  max_area_d  = fu.area_distortion;
            if (fu.angle_distortion > max_angle_d) max_angle_d = fu.angle_distortion;
            fu.l2_stretch = 0.0;
            r.face_uvs.push_back(fu);
        }
        r.flipped_count        = flipped;
        r.degenerate_count     = degenerate;
        r.mean_area_distortion  = area_count  ? total_area_d  / area_count  : 0.0;
        r.max_area_distortion   = max_area_d;
        r.mean_angle_distortion = angle_count ? total_angle_d / angle_count : 0.0;
        r.max_angle_distortion  = max_angle_d;

        // Boundary vertices = base vertices marked as on-seam.
        r.boundary_vertices.clear();
        for (int i = 0; i < nv; ++i) {
            if (get(seam_vertex_pm, vmap[i]))
                r.boundary_vertices.push_back(i);
        }
        // Count seam edges (each seam edge contributes two virtual border halfedges).
        int be_count = 0;
        for (auto e : edges(mesh))
            if (get(seam_edge_pm, e)) ++be_count;
        r.boundary_edge_count = be_count;

        const auto t1 = Clock::now();
        r.elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
            t1 - t0).count() / 1000.0;
        impl_->done_flag = true;
        return;
    }

    // --- Open mesh path (no seams) ---
    std::string bhd_err;
    auto bhd = find_border_halfedge(mesh, bhd_err);
    if (bhd == boost::graph_traits<Mesh>::null_halfedge()) {
        impl_->set_error(PARAM_ERR_NoBoundary, bhd_err);
        impl_->done_flag = true;
        return;
    }

    auto uv_pm = mesh.add_property_map<vertex_descriptor, Point_2>("v:uv",
        Point_2(0, 0)).first;

    std::vector<PARAM_FaceUV> face_template;
    build_face_uv_template(mesh, face_template);

    int be_count = 0;
    for (auto h : halfedges(mesh))
        if (CGAL::is_border(h, mesh)) ++be_count;

    std::vector<int> boundary_vertices;
    extract_boundary_loop(mesh, bhd, boundary_vertices);

    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        r.original_vertices = nv;
        r.original_faces = (int)mesh.number_of_faces();
        r.is_closed = false;
        r.boundary_edge_count = be_count;
        r.boundary_vertices = boundary_vertices;
        r.face_uvs = face_template;
        r.arap_snapshots.clear();
    }

    SMP::Error_code err;
    const char* algo_name = "LSCM";
    if (impl_->cfg.algorithm == PARAM_ALGO_ARAP) {
        ARAPSnapVisitor vis;
        vis.result = &r;
        vis.result_mutex = &impl_->result_mutex;
        vis.nv = nv;
        vis.vmap_p = &vmap;
        vis.enabled = impl_->cfg.show_process;
        vis.stride = impl_->cfg.arap_snapshot_stride;
        vis.delay_ms = impl_->cfg.arap_snapshot_delay_ms;
        vis.wake_cb = impl_->wake_cb;
        vis.wake_user_data = impl_->wake_user_data;

        SMP::ARAP_parameterizer_3<Mesh,
            SMP::Two_vertices_parameterizer_3<Mesh>,
            CGAL::Default, ARAPSnapVisitor> param(
                SMP::Two_vertices_parameterizer_3<Mesh>(),
                {},
                impl_->cfg.arap_lambda,
                (unsigned int)impl_->cfg.arap_iterations,
                impl_->cfg.arap_tolerance);
        param.set_visitor(vis);
        err = SMP::parameterize(mesh, param, bhd, uv_pm);
        algo_name = "ARAP";
    } else {
        SMP::LSCM_parameterizer_3<Mesh> param;
        err = SMP::parameterize(mesh, param, bhd, uv_pm);
    }

    if (impl_->cancel_flag.load()) {
        impl_->done_flag = true;
        return;
    }

    if (err != SMP::OK) {
        std::string msg = std::string(algo_name) + " failed: ";
        switch (err) {
        case SMP::ERROR_EMPTY_MESH:        msg += "empty mesh"; break;
        case SMP::ERROR_NON_TRIANGULAR_MESH: msg += "non-triangular"; break;
        case SMP::ERROR_NO_TOPOLOGICAL_DISC: msg += "not a topological disc"; break;
        case SMP::ERROR_BORDER_TOO_SHORT:    msg += "border too short"; break;
        case SMP::ERROR_NON_CONVEX_BORDER:   msg += "non-convex border"; break;
        case SMP::ERROR_CANNOT_SOLVE_LINEAR_SYSTEM:
            msg += "linear solver failed (try a different border)"; break;
        case SMP::ERROR_NO_1_TO_1_MAPPING:   msg += "no 1-to-1 mapping"; break;
        case SMP::ERROR_WRONG_PARAMETER:     msg += "wrong parameter"; break;
        default: msg += "unknown error"; break;
        }
        impl_->set_error(PARAM_ERR_AlgorithmFailed, msg);
        impl_->done_flag = true;
        return;
    }

    std::vector<Point_2> uv_per_vertex(nv, Point_2(0, 0));
    for (int i = 0; i < nv; ++i)
        uv_per_vertex[i] = uv_pm[vmap[i]];

    if (impl_->cfg.algorithm == PARAM_ALGO_ARAP)
        publish_arap_final_transition(r, impl_->result_mutex,
            uv_per_vertex, impl_->cfg, impl_->wake_cb, impl_->wake_user_data);

    const auto t1 = Clock::now();
    PARAM_Result final_result;
    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        final_result = r;
    }
    final_result.error_code = PARAM_ERR_None;
    final_result.error_message[0] = 0;
    final_result.elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        t1 - t0).count() / 1000.0;
    final_result.boundary_edge_count = be_count;
    final_result.boundary_vertices = boundary_vertices;

    compute_per_face_distortion(mesh, vmap, uv_per_vertex,
                                 final_result.face_uvs, final_result);

    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        r = std::move(final_result);
    }

    impl_->done_flag = true;
}

void ParameterizationRunner::cancel() {
    impl_->cancel_flag.store(true);
}

void ParameterizationRunner::fail_with_exception(const char* message) {
    impl_->set_error(PARAM_ERR_Exception,
        message ? message : "parameterization worker exception");
    impl_->done_flag = true;
}

bool ParameterizationRunner::is_done() const {
    return impl_->done_flag.load();
}

bool ParameterizationRunner::is_cancelled() const {
    return impl_->cancel_flag.load();
}

bool ParameterizationRunner::has_error() const {
    return impl_->has_error_flag.load();
}

std::string ParameterizationRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void ParameterizationRunner::get_result(PARAM_Result& out) const {
    std::lock_guard<std::mutex> lk(impl_->result_mutex);
    out = impl_->result;
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "geodesic_cgal_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh_shortest_path.h>
#include <CGAL/Surface_mesh_shortest_path/Surface_mesh_shortest_path_traits.h>
#include <CGAL/Heat_method_3/Surface_mesh_geodesic_distances_3.h>

using Kernel    = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3   = Kernel::Point_3;
using CGAL_Mesh = CGAL::Surface_mesh<Point_3>;
using SP_Traits = CGAL::Surface_mesh_shortest_path_traits<Kernel, CGAL_Mesh>;
using SMSP      = CGAL::Surface_mesh_shortest_path<SP_Traits>;
using Clock     = std::chrono::steady_clock;
using HM_Direct = CGAL::Heat_method_3::Surface_mesh_geodesic_distances_3<CGAL_Mesh>;
using HM_IDT    = CGAL::Heat_method_3::Surface_mesh_geodesic_distances_3<
                     CGAL_Mesh, CGAL::Heat_method_3::Intrinsic_Delaunay>;
using vertex_descriptor = boost::graph_traits<CGAL_Mesh>::vertex_descriptor;
using Vertex_distance_map =
    CGAL_Mesh::Property_map<vertex_descriptor, double>;

struct GeodesicCGALRunner::Impl {
    std::vector<GEO_CGAL_Point3d>  input_verts;
    std::vector<GEO_CGAL_Triangle> input_tris;
    bool has_input = false;

    GEO_CGAL_Source source{};
    GEO_CGAL_Target target{};
    bool has_source = false;
    bool has_target = false;

    std::vector<int> source_vids;       // Heat Method multi-source

    GEO_CGAL_Config cfg{};

    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> done_flag{false};
    std::atomic<bool> has_error_flag{false};
    mutable std::mutex error_mutex;
    std::string error_message;

    GEO_CGAL_PathResult path_result{};
    GEO_CGAL_HeatResult heat_result_{};

    void reset_state() {
        cancel_flag    = false;
        done_flag      = false;
        has_error_flag = false;
        {
            std::lock_guard<std::mutex> lk(error_mutex);
            error_message.clear();
        }
        path_result = GEO_CGAL_PathResult{};
        heat_result_ = GEO_CGAL_HeatResult{};
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

GEO_CGAL_Point3d to_pod(const Point_3& p) {
    return GEO_CGAL_Point3d{ CGAL::to_double(p.x()),
                              CGAL::to_double(p.y()),
                              CGAL::to_double(p.z()) };
}

bool build_cgal_mesh(const std::vector<GEO_CGAL_Point3d>&  pv,
                     const std::vector<GEO_CGAL_Triangle>& pt,
                     CGAL_Mesh& mesh,
                     std::vector<CGAL_Mesh::Vertex_index>& vmap_out,
                     std::string& err)
{
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
        CGAL_Mesh::Face_index fd =
            mesh.add_face(vmap_out[t.v0], vmap_out[t.v1], vmap_out[t.v2]);
        if (fd == CGAL_Mesh::null_face())
            fd = mesh.add_face(vmap_out[t.v0], vmap_out[t.v2], vmap_out[t.v1]);
        if (fd == CGAL_Mesh::null_face()) {
            err = "failed to add triangle to CGAL mesh";
            return false;
        }
    }
    if (!CGAL::is_triangle_mesh(mesh)) {
        err = "input is not a triangle mesh after conversion";
        return false;
    }
    return true;
}

} // namespace

GeodesicCGALRunner::GeodesicCGALRunner()  : impl_(std::make_unique<Impl>()) {}
GeodesicCGALRunner::~GeodesicCGALRunner() = default;

void GeodesicCGALRunner::set_input(
    const std::vector<GEO_CGAL_Point3d>&  verts,
    const std::vector<GEO_CGAL_Triangle>& tris)
{
    impl_->input_verts = verts;
    impl_->input_tris  = tris;
    impl_->has_input   = !verts.empty() && !tris.empty();
}

void GeodesicCGALRunner::set_source(const GEO_CGAL_Source& src) {
    impl_->source = src;
    impl_->has_source = (src.vertex_id >= 0 || src.face_id >= 0);
}

void GeodesicCGALRunner::set_target(const GEO_CGAL_Target& tgt) {
    impl_->target = tgt;
    impl_->has_target = (tgt.vertex_id >= 0 || tgt.face_id >= 0);
}

void GeodesicCGALRunner::run(const GEO_CGAL_Config& cfg) {
    impl_->reset_state();
    impl_->cfg = cfg;

    if (!impl_->has_input) {
        impl_->set_error("no input mesh");
        impl_->done_flag = true;
        return;
    }
    if (!impl_->has_source) {
        impl_->set_error("no source set");
        impl_->done_flag = true;
        return;
    }
    const int nv = (int)impl_->input_verts.size();
    if (impl_->source.vertex_id < 0 || impl_->source.vertex_id >= nv) {
        impl_->set_error("source vertex id out of range");
        impl_->done_flag = true;
        return;
    }

    CGAL_Mesh tmesh;
    std::vector<CGAL_Mesh::Vertex_index> vmap;
    std::string build_err;
    if (!build_cgal_mesh(impl_->input_verts, impl_->input_tris,
                         tmesh, vmap, build_err)) {
        impl_->set_error(build_err);
        impl_->done_flag = true;
        return;
    }

    if (impl_->cancel_flag.load()) {
        impl_->done_flag = true;
        return;
    }

    try {
        SMSP sp(tmesh);

        // Add source point.
        auto vit = vertices(tmesh).first;
        std::advance(vit, impl_->source.vertex_id);
        const auto src_v = *vit;

        const auto t0 = Clock::now();
        sp.add_source_point(src_v);
        sp.build_sequence_tree();
        const auto t1 = Clock::now();
        impl_->path_result.ms_build_tree =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (impl_->cancel_flag.load()) {
            impl_->done_flag = true;
            return;
        }

        // Query target if set.
        if (impl_->has_target && impl_->target.vertex_id >= 0 &&
            impl_->target.vertex_id < nv)
        {
            auto vit2 = vertices(tmesh).first;
            std::advance(vit2, impl_->target.vertex_id);
            const auto tgt_v = *vit2;

            std::vector<Point_3> pts;
            const auto t2 = Clock::now();
            sp.shortest_path_points_to_source_points(
                tgt_v, std::back_inserter(pts));
            const auto t3 = Clock::now();
            impl_->path_result.ms_query =
                std::chrono::duration<double, std::milli>(t3 - t2).count();

            if (!pts.empty()) {
                impl_->path_result.path_found = true;
                impl_->path_result.points.reserve(pts.size());
                for (const auto& p : pts)
                    impl_->path_result.points.push_back(to_pod(p));

                // Compute polyline length.
                double L = 0.0;
                for (std::size_t i = 1; i < pts.size(); ++i) {
                    const auto& a = pts[i - 1];
                    const auto& b = pts[i];
                    const double dx = CGAL::to_double(b.x()) - CGAL::to_double(a.x());
                    const double dy = CGAL::to_double(b.y()) - CGAL::to_double(a.y());
                    const double dz = CGAL::to_double(b.z()) - CGAL::to_double(a.z());
                    L += std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                impl_->path_result.length = L;
            }
        }
    } catch (const std::exception& e) {
        impl_->set_error(
            std::string("CGAL exact shortest path exception: ") + e.what());
    } catch (...) {
        impl_->set_error("CGAL exact shortest path unknown exception");
    }

    impl_->done_flag = true;
}

void GeodesicCGALRunner::cancel() { impl_->cancel_flag = true; }

bool GeodesicCGALRunner::is_cancelled() const { return impl_->cancel_flag.load(); }
bool GeodesicCGALRunner::is_done()      const { return impl_->done_flag.load(); }
bool GeodesicCGALRunner::has_error()    const { return impl_->has_error_flag.load(); }

std::string GeodesicCGALRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

GEO_CGAL_PathResult GeodesicCGALRunner::result() const {
    return impl_->path_result;
}

void GeodesicCGALRunner::set_sources(const std::vector<int>& source_vids) {
    impl_->source_vids = source_vids;
}

void GeodesicCGALRunner::run_heat(int variant) {
    impl_->reset_state();

    if (!impl_->has_input) {
        impl_->set_error("no input mesh");
        impl_->done_flag = true;
        return;
    }
    if (impl_->source_vids.empty()) {
        impl_->set_error("no sources set");
        impl_->done_flag = true;
        return;
    }
    const int nv = (int)impl_->input_verts.size();
    for (int vid : impl_->source_vids) {
        if (vid < 0 || vid >= nv) {
            impl_->set_error("source vertex id out of range");
            impl_->done_flag = true;
            return;
        }
    }

    CGAL_Mesh tmesh;
    std::vector<CGAL_Mesh::Vertex_index> vmap;
    std::string build_err;
    if (!build_cgal_mesh(impl_->input_verts, impl_->input_tris,
                         tmesh, vmap, build_err)) {
        impl_->set_error(build_err);
        impl_->done_flag = true;
        return;
    }

    if (!CGAL::is_triangle_mesh(tmesh)) {
        impl_->set_error("input is not a triangle mesh");
        impl_->done_flag = true;
        return;
    }

    if (impl_->cancel_flag.load()) {
        impl_->done_flag = true;
        return;
    }

    try {
        Vertex_distance_map vdm =
            tmesh.add_property_map<vertex_descriptor, double>(
                "v:distance_geo", 0.0).first;

        const auto t0 = Clock::now();

        if (variant == GEO_CGAL_HEAT_IntrinsicDelaunay) {
            HM_IDT hm(tmesh);
            for (int vid : impl_->source_vids) {
                auto vit = vertices(tmesh).first;
                std::advance(vit, vid);
                hm.add_source(*vit);
            }
            hm.estimate_geodesic_distances(vdm);
        } else {
            HM_Direct hm(tmesh);
            for (int vid : impl_->source_vids) {
                auto vit = vertices(tmesh).first;
                std::advance(vit, vid);
                hm.add_source(*vit);
            }
            hm.estimate_geodesic_distances(vdm);
        }

        const auto t1 = Clock::now();
        impl_->heat_result_.ms_total =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (impl_->cancel_flag.load()) {
            impl_->heat_result_.cancelled = true;
            impl_->done_flag = true;
            return;
        }

        // Extract distances.
        const std::size_t n = num_vertices(tmesh);
        impl_->heat_result_.vertex_distances.resize(n);
        double mn = std::numeric_limits<double>::infinity();
        double mx = -1.0;
        double sum = 0.0;
        std::size_t i = 0;
        for (auto v : vertices(tmesh)) {
            const double d = get(vdm, v);
            impl_->heat_result_.vertex_distances[i++] = d;
            mn = std::min(mn, d);
            if (d > mx) mx = d;
            sum += d;
        }
        impl_->heat_result_.min_distance = mn;
        impl_->heat_result_.max_distance = mx;
        impl_->heat_result_.mean_distance = (n > 0) ? (sum / n) : 0.0;

    } catch (const std::exception& e) {
        impl_->set_error(
            std::string("CGAL Heat Method exception: ") + e.what());
    } catch (...) {
        impl_->set_error("CGAL Heat Method unknown exception");
    }

    impl_->done_flag = true;
}

GEO_CGAL_HeatResult GeodesicCGALRunner::heat_result() const {
    return impl_->heat_result_;
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PLANE_ANALYSIS_H
#define CLAW3D_PLANE_ANALYSIS_H

/// Geometry-only plane analysis helpers for alignment and diagnostics.

// 2D convex hull and alpha shape on a supporting plane.
// Projects inlier points to the plane, computes in 2D, projects back to 3D.
// Reference: HSR scene_structural_analysis.cpp

#include <easy3d/core/types.h>
#include <vector>
#include <cmath>

#ifdef CLAW3D_HAS_CGAL

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/convex_hull_2.h>
#include <CGAL/Alpha_shape_2.h>
#include <CGAL/Alpha_shape_vertex_base_2.h>
#include <CGAL/Alpha_shape_face_base_2.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Projection_traits_xy_3.h>

namespace {

using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_2 = K::Point_2;
using Point_3 = K::Point_3;
using Vector_3 = K::Vector_3;

// ============================================================================
// 2D convex hull of points projected onto a plane
// ============================================================================
inline std::vector<easy3d::vec3> compute_plane_convex_hull(
    const std::vector<easy3d::vec3>& inlier_pts,
    const easy3d::vec3& plane_normal,
    float /*unused*/)
{
    std::vector<easy3d::vec3> result;
    if (inlier_pts.size() < 3) return result;

    // Build orthonormal basis on the plane
    easy3d::vec3 n = plane_normal;
    n = n / std::sqrt(dot(n, n));
    easy3d::vec3 u, v;
    if (std::abs(n.x) < 0.9f) u = cross(n, easy3d::vec3(1, 0, 0));
    else                       u = cross(n, easy3d::vec3(0, 1, 0));
    u = u / std::sqrt(dot(u, u));
    v = cross(n, u);

    // Project to 2D
    Point_3 origin(inlier_pts[0].x, inlier_pts[0].y, inlier_pts[0].z);
    std::vector<Point_2> pts2d;
    pts2d.reserve(inlier_pts.size());
    for (auto& p : inlier_pts) {
        Vector_3 d(p.x - (float)origin.x(),
                   p.y - (float)origin.y(),
                   p.z - (float)origin.z());
        double du = d.x() * u.x + d.y() * u.y + d.z() * u.z;
        double dv = d.x() * v.x + d.y() * v.y + d.z() * v.z;
        pts2d.emplace_back(du, dv);
    }

    // CGAL convex hull 2D
    std::vector<Point_2> ch;
    CGAL::convex_hull_2(pts2d.begin(), pts2d.end(), std::back_inserter(ch));

    // Project back to 3D
    for (auto& p2 : ch) {
        float x = (float)(origin.x() + p2.x() * u.x + p2.y() * v.x);
        float y = (float)(origin.y() + p2.x() * u.y + p2.y() * v.y);
        float z = (float)(origin.z() + p2.x() * u.z + p2.y() * v.z);
        result.push_back({x, y, z});
    }
    return result;
}

// ============================================================================
// 2D alpha shape of points projected onto a plane
// ============================================================================
using Alpha_shape_vb = CGAL::Alpha_shape_vertex_base_2<K>;
using Alpha_shape_fb = CGAL::Alpha_shape_face_base_2<K>;
using Alpha_shape_ds = CGAL::Triangulation_data_structure_2<Alpha_shape_vb, Alpha_shape_fb>;
using Alpha_shape_dt = CGAL::Delaunay_triangulation_2<K, Alpha_shape_ds>;
using CGAL_Alpha_shape_2 = CGAL::Alpha_shape_2<Alpha_shape_dt>;

inline void compute_plane_alpha_shape(
    const std::vector<easy3d::vec3>& inlier_pts,
    const easy3d::vec3& plane_normal,
    float alpha,
    std::vector<easy3d::vec3>& out_verts,
    std::vector<std::array<int, 2>>& out_edges)
{
    out_verts.clear(); out_edges.clear();
    if (inlier_pts.size() < 3) return;

    // Build orthonormal basis
    easy3d::vec3 n = plane_normal;
    n = n / std::sqrt(dot(n, n));
    easy3d::vec3 u, v;
    if (std::abs(n.x) < 0.9f) u = cross(n, easy3d::vec3(1, 0, 0));
    else                       u = cross(n, easy3d::vec3(0, 1, 0));
    u = u / std::sqrt(dot(u, u));
    v = cross(n, u);

    Point_3 origin(inlier_pts[0].x, inlier_pts[0].y, inlier_pts[0].z);
    std::vector<Point_2> pts2d;
    pts2d.reserve(inlier_pts.size());
    for (auto& p : inlier_pts) {
        Vector_3 d(p.x - (float)origin.x(),
                   p.y - (float)origin.y(),
                   p.z - (float)origin.z());
        double du = d.x() * u.x + d.y() * u.y + d.z() * u.z;
        double dv = d.x() * v.x + d.y() * v.y + d.z() * v.z;
        pts2d.emplace_back(du, dv);
    }

    // Alpha shape
    CGAL_Alpha_shape_2 as(pts2d.begin(), pts2d.end());
    as.set_alpha(alpha);

    // Collect vertices that belong to the alpha shape
    std::map<Point_2, int> vmap;
    for (auto vit = as.alpha_shape_vertices_begin();
         vit != as.alpha_shape_vertices_end(); ++vit) {
        auto p2 = (*vit)->point();
        if (vmap.find(p2) != vmap.end()) continue;
        int idx = (int)out_verts.size();
        vmap[p2] = idx;
        float x = (float)(origin.x() + p2.x() * u.x + p2.y() * v.x);
        float y = (float)(origin.y() + p2.x() * u.y + p2.y() * v.y);
        float z = (float)(origin.z() + p2.x() * u.z + p2.y() * v.z);
        out_verts.push_back({x, y, z});
    }

    // Collect edges
    for (auto eit = as.alpha_shape_edges_begin();
         eit != as.alpha_shape_edges_end(); ++eit) {
        auto seg = as.segment(*eit);
        auto it0 = vmap.find(seg.source());
        auto it1 = vmap.find(seg.target());
        if (it0 != vmap.end() && it1 != vmap.end())
            out_edges.push_back({it0->second, it1->second});
    }
}

} // anonymous namespace

#endif // CLAW3D_HAS_CGAL
#endif // CLAW3D_PLANE_ANALYSIS_H

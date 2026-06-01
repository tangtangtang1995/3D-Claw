// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_RANSAC_RUNNER_H
#define CLAW3D_RANSAC_RUNNER_H

/// CGAL RANSAC primitive-detection runner behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/ransac_contract.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API RansacRunner {
public:
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;

public:
    RansacRunner();
    ~RansacRunner();

    RansacRunner(const RansacRunner&) = delete;
    RansacRunner& operator=(const RansacRunner&) = delete;

    // Input: points + normals (both required; normals from PCA or file)
    void set_input(const std::vector<RANSAC_Point3d>& points,
                   const std::vector<RANSAC_Vector3d>& normals);

    // Run Efficient RANSAC.
    void run(const RANSAC_Config& cfg);

    // --- Live preview ---
    bool drain_live_events(std::vector<RANSAC_FrameEvent>& out_events);
    void cancel();
    bool is_cancelled() const;

    // --- Error reporting ---
    void set_error(const std::string& msg);
    bool has_error() const;
    std::string last_error() const;
    void clear_error();

    // --- Pause / Resume (Live Preview) ---
    void pause();
    void resume();
    void step();          // unpause for one iteration, then re-pause
    bool is_paused() const;

    // --- Running stats (atomic, valid during run) ---
    int  total_shapes() const;
    int  total_points() const;

    // --- Result ---
    bool  is_done() const;
    int   num_shapes() const;
    void  get_shape_plane(int idx, double plane_eq[4],
                          int& inlier_count) const;

    float progress() const;
};

// === Plane analysis utilities (2D CH / AS on plane, DLL-side) ===

// Compute 2D convex hull of points projected onto a plane.
// in_pts: flat array of [x0,y0,z0, x1,y1,z1, ...], n = count
// plane_eq: [a,b,c,d] for ax+by+cz+d=0
// Returns polygon vertices (CCW) in 3D.
CLAW3D_CGAL_ALGO_API void ransac_convex_hull_2d(const double* in_pts, int n,
    const double plane_eq[4],
    std::vector<RANSAC_Point3d>& out_ch);

// Compute 2D alpha shape of points projected onto a plane.
// alpha: alpha value (use bbox_diag * 0.01 as sensible default).
// out_tris: filled interior triangles (constrained Delaunay).
CLAW3D_CGAL_ALGO_API void ransac_alpha_shape_2d(const double* in_pts, int n,
    const double plane_eq[4], double alpha,
    std::vector<RANSAC_Point3d>& out_verts,
    std::vector<std::array<int,3>>& out_tris);

// === Generic plane analysis (same as ransac_*, kept for clarity) ===

inline void plane_convex_hull_2d(const double* in_pts, int n,
    const double plane_eq[4],
    std::vector<RANSAC_Point3d>& out_ch) {
    ransac_convex_hull_2d(in_pts, n, plane_eq, out_ch);
}

inline void plane_alpha_shape_2d(const double* in_pts, int n,
    const double plane_eq[4], double alpha,
    std::vector<RANSAC_Point3d>& out_verts,
    std::vector<std::array<int,3>>& out_tris) {
    ransac_alpha_shape_2d(in_pts, n, plane_eq, alpha, out_verts, out_tris);
}

#endif // CLAW3D_RANSAC_RUNNER_H

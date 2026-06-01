// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MESH_QUALITY_H
#define CLAW3D_MESH_QUALITY_H

/// Mesh-quality metrics and formatting helpers used by analysis panels.

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <string>

struct PoissonQualityReport {
    int   input_points = 0;
    float input_bbox_diag = 0;
    int   out_vertices = 0, out_faces = 0, out_edges = 0;
    int   out_borders = 0, out_non_manifold_v = 0, out_non_manifold_e = 0;
    bool  out_watertight = false;
    int   euler_characteristic = 0, genus = 0;
    float out_volume = 0, out_surface_area = 0, out_bbox_diag = 0;
    float hausdorff_max = 0, hausdorff_rms = 0, chamfer = 0;
    float normal_consistency = 0;
    float curvature_mean_avg = 0, curvature_mean_std = 0;
    float curvature_gauss_avg = 0, curvature_gauss_std = 0;
    float elapsed_sec = 0;
};

PoissonQualityReport evaluate_poisson_result(
    const easy3d::PointCloud* input, easy3d::SurfaceMesh* output, float elapsed_sec);
std::string format_report_for_ai(const PoissonQualityReport& r, int depth, float spn);

// --- AW3 Quality Evaluation (paper: Alpha Wrapping with an Offset, Portaneri et al. 2022) ---

struct AW3QualityReport {
    // Input
    int   input_vertices = 0, input_faces = 0; // mesh mode
    int   input_points = 0;                     // point cloud mode
    float input_bbox_diag = 0;

    // Output
    int   out_vertices = 0, out_faces = 0;
    float out_bbox_diag = 0;

    // Parameters (absolute + ratio to bbox diagonal)
    float alpha = 0, offset = 0;
    float alpha_ratio = 0, offset_ratio = 0;
    float elapsed_sec = 0;

    // Watertightness (paper guarantees all three, but verify)
    bool out_watertight = false;
    int  out_borders = 0;

    // One-sided Hausdorff: output to input (paper Section 5.2, Figure 15)
    // Theoretical upper bound: alpha + offset (Section 5.2)
    float hausdorff_max = 0;      // max distance from output vertex to input
    float hausdorff_rms = 0;      // RMS
    float chamfer = 0;            // mean
    float theoretical_bound = 0;  // alpha + offset
    bool  bound_satisfied = false;

    // Distance distribution: should peak near offset (Figure 7)
    float dist_median = 0;
    float dist_p90 = 0;
};

AW3QualityReport evaluate_aw3_result(
    const easy3d::Model* input,
    const easy3d::SurfaceMesh* output,
    float alpha, float offset, float elapsed_sec);

std::string format_aw3_report_for_ai(const AW3QualityReport& r);

#endif // CLAW3D_MESH_QUALITY_H

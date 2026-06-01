// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "mesh_quality.h"

#include <easy3d/algo/triangle_mesh_kdtree.h>
#include <easy3d/algo/surface_mesh_curvature.h>
#include <easy3d/algo/surface_mesh_geometry.h>
#include <easy3d/kdtree/kdtree_search_eth.h>
#include <easy3d/util/logging.h>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cfloat>

PoissonQualityReport evaluate_poisson_result(
    const easy3d::PointCloud* input,
    easy3d::SurfaceMesh* output,
    float elapsed_sec)
{
    PoissonQualityReport r;
    r.elapsed_sec = elapsed_sec;

    // --- Input ---
    r.input_points = input->n_vertices();
    r.input_bbox_diag = input->bounding_box().diagonal_length();

    // --- Output basics ---
    r.out_vertices = output->n_vertices();
    r.out_faces = output->n_faces();
    r.out_bbox_diag = output->bounding_box().diagonal_length();

    // --- Edges ---
    int n_edges = 0;
    for (auto e : output->edges()) { (void)e; n_edges++; }
    r.out_edges = n_edges;

    // --- Borders & non-manifold ---
    int borders = 0, nmanifold_v = 0, nmanifold_e = 0;
    for (auto h : output->halfedges()) {
        if (output->is_border(h)) borders++;
    }
    for (auto v : output->vertices()) {
        if (!output->is_manifold(v)) nmanifold_v++;
    }
    // Non-manifold edge: count edges incident to >2 faces
    // Use a simple vector-based counter indexed by edge idx
    {
        std::vector<int> edge_cnt(r.out_edges, 0);
        for (auto h : output->halfedges()) {
            auto e = output->edge(h);
            if (e.is_valid()) edge_cnt[e.idx()]++;
        }
        for (int c : edge_cnt) if (c > 2) nmanifold_e++;
    }
    r.out_borders = borders;
    r.out_non_manifold_v = nmanifold_v;
    r.out_non_manifold_e = nmanifold_e;
    r.out_watertight = output->is_closed() && borders == 0 && nmanifold_v == 0;

    // --- Topology ---
    r.euler_characteristic = r.out_vertices - n_edges + r.out_faces;
    r.genus = (2 - r.euler_characteristic) / 2;

    // --- Geometry ---
    r.out_volume = easy3d::geom::volume(output);
    r.out_surface_area = easy3d::geom::surface_area(output);

    // --- PC->Mesh distance (Hausdorff, Chamfer) ---
    easy3d::TriangleMeshKdTree kd(output);
    float max_d = 0, sum_d = 0, sum_sq = 0;
    int n = r.input_points;
    for (auto v : input->vertices()) {
        auto nn = kd.nearest(input->position(v));
        max_d = std::max(max_d, nn.dist);
        sum_d += nn.dist;
        sum_sq += nn.dist * nn.dist;
    }
    r.hausdorff_max = max_d;
    r.hausdorff_rms = std::sqrt(sum_sq / n);
    r.chamfer = sum_d / n;

    // --- Normal consistency ---
    auto input_normals = input->get_vertex_property<easy3d::vec3>("v:normal");
    if (input_normals) {
        float cons_sum = 0;
        int valid = 0;
        for (auto v : input->vertices()) {
            auto nn = kd.nearest(input->position(v));
            // Get face vertices and compute triangle normal
            auto fvit = output->vertices(nn.face);
            auto it = fvit.begin();
            if (it == fvit.end()) continue;
            const auto& p0 = output->position(*it); ++it;
            if (it == fvit.end()) continue;
            const auto& p1 = output->position(*it); ++it;
            if (it == fvit.end()) continue;
            const auto& p2 = output->position(*it);
            // Cross product: (p1-p0) x (p2-p0)
            float ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
            float vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len < 1e-20f) continue;
            nx /= len; ny /= len; nz /= len;
            // Manual dot product
            float d = input_normals[v].x * nx + input_normals[v].y * ny + input_normals[v].z * nz;
            float abs_d = d < 0 ? -d : d;
            if (!std::isnan(abs_d)) { cons_sum += abs_d; valid++; }
        }
        r.normal_consistency = (valid > 0) ? cons_sum / valid : 0;
    } else {
        r.normal_consistency = -1; // no normals
    }

    // --- Curvature ---
    auto curv = easy3d::SurfaceMeshCurvature(output);
    curv.compute_mean_curvature();
    curv.compute_gauss_curvature();
    auto mc = output->get_vertex_property<float>("v:curv-mean");
    auto gc = output->get_vertex_property<float>("v:curv-gauss");
    if (mc && gc) {
        float ms = 0, gs = 0, msq = 0, gsq = 0;
        int nv = r.out_vertices;
        for (auto v : output->vertices()) {
            ms += mc[v]; gs += gc[v];
            msq += mc[v] * mc[v]; gsq += gc[v] * gc[v];
        }
        r.curvature_mean_avg = ms / nv;
        r.curvature_gauss_avg = gs / nv;
        float mvar = msq / nv - r.curvature_mean_avg * r.curvature_mean_avg;
        float gvar = gsq / nv - r.curvature_gauss_avg * r.curvature_gauss_avg;
        r.curvature_mean_std = std::sqrt(std::max(0.0f, mvar));
        r.curvature_gauss_std = std::sqrt(std::max(0.0f, gvar));
    }

    return r;
}

std::string format_report_for_ai(const PoissonQualityReport& r, int depth, float spn) {
    std::ostringstream s;
    s << "## Poisson Reconstruction Quality Report\n\n";

    s << "### Parameters\n";
    s << "depth=" << depth << " samples_per_node=" << spn
      << " elapsed=" << r.elapsed_sec << "s\n\n";

    s << "### Input vs Output\n";
    s << "| Metric | Input | Output |\n";
    s << "|--------|-------|--------|\n";
    s << "| Points/Vertices | " << r.input_points << " | " << r.out_vertices << " |\n";
    s << "| Faces | -- | " << r.out_faces << " |\n";
    s << "| Edges | -- | " << r.out_edges << " |\n";
    s << "| BBox Diagonal | " << r.input_bbox_diag << " | " << r.out_bbox_diag << " |\n\n";

    s << "### Topology\n";
    s << "| Metric | Value |\n";
    s << "|--------|-------|\n";
    s << "| Watertight | " << (r.out_watertight ? "YES" : "NO") << " |\n";
    s << "| Border Edges | " << r.out_borders << " |\n";
    s << "| Non-manifold Vertices | " << r.out_non_manifold_v << " |\n";
    s << "| Non-manifold Edges | " << r.out_non_manifold_e << " |\n";
    s << "| Euler Characteristic | " << r.euler_characteristic << " |\n";
    s << "| Genus | " << r.genus << " |\n";
#ifdef CLAW3D_HAS_CGAL
    s << "| Self-Intersections | [not checked] |\n";
#else
    s << "| Self-Intersections | [no CGAL] |\n";
#endif
    s << "\n";

    s << "### Geometry\n";
    s << "| Metric | Value |\n";
    s << "|--------|-------|\n";
    s << "| Volume | " << r.out_volume << " |\n";
    s << "| Surface Area | " << r.out_surface_area << " |\n";
    float haus_pct = r.hausdorff_max / r.input_bbox_diag * 100.0f;
    float rms_pct = r.hausdorff_rms / r.input_bbox_diag * 100.0f;
    float ch_pct = r.chamfer / r.input_bbox_diag * 100.0f;
    s << "| Hausdorff Max | " << r.hausdorff_max << " (" << haus_pct << "% diag) |\n";
    s << "| Hausdorff RMS | " << r.hausdorff_rms << " (" << rms_pct << "% diag) |\n";
    s << "| Chamfer | " << r.chamfer << " (" << ch_pct << "% diag) |\n";
    s << "| Normal Consistency | "
      << (r.normal_consistency >= 0 ? std::to_string(r.normal_consistency) : "[no normals]")
      << " |\n\n";

    s << "### Curvature\n";
    s << "| Metric | Value |\n";
    s << "|--------|-------|\n";
    s << "| Mean Curvature (avg/std) | "
      << r.curvature_mean_avg << " / " << r.curvature_mean_std << " |\n";
    s << "| Gaussian Curvature (avg/std) | "
      << r.curvature_gauss_avg << " / " << r.curvature_gauss_std << " |\n\n";

    s << "### Qualitative Checks (AI should assess)\n";
    s << "- If input is non-closed (wall, leaf), Poisson force-closes it -> check for artificial back surfaces\n";
    if (r.curvature_gauss_std < 0.01f)
        s << "- **gauss_curv_std < 0.01** -> surface may be over-smoothed, sharp features possibly lost\n";
    else
        s << "- gauss_curv_std >= 0.01 -> surface sharpness looks reasonable\n";
    float bbox_vol = r.input_bbox_diag * r.input_bbox_diag * r.input_bbox_diag;
    float vol_ratio = (bbox_vol > 0) ? r.out_volume / bbox_vol : 1;
    if (vol_ratio < 0.3f)
        s << "- **Volume/bbox ratio < 0.3** -> possible low-frequency shrinkage, consider increasing depth\n";
    else
        s << "- Volume/bbox ratio = " << vol_ratio << " -> OK\n";
    if (r.normal_consistency >= 0 && r.normal_consistency < 0.7f)
        s << "- **Normal consistency < 0.7** -> normals may be inconsistent, check Normal Estimation + Reorient\n";
    else if (r.normal_consistency >= 0)
        s << "- Normal consistency OK (" << r.normal_consistency << ")\n";
    s << "- If the output shows unexpected topology (genus != 0), check input for holes or missing data\n";

    return s.str();
}

// ============================================================================
// AW3 Quality Evaluation (paper: Alpha Wrapping with an Offset, Portaneri 2022)
// ============================================================================
// Key metrics from the paper (Section 5):
//   - One-sided Hausdorff (output -> input): theoretical bound alpha + delta
//   - Output complexity (vertex/face count)
//   - Runtime
//   - Distance distribution: peak near offset value delta
//   - Mesh quality: R1 vs R2 ratio

AW3QualityReport evaluate_aw3_result(
    const easy3d::Model* input,
    const easy3d::SurfaceMesh* output,
    float alpha, float offset, float elapsed_sec)
{
    AW3QualityReport r;
    r.alpha = alpha; r.offset = offset;
    r.elapsed_sec = elapsed_sec;

    const auto& in_bbox = input->bounding_box();
    r.input_bbox_diag = in_bbox.diagonal_length();
    r.alpha_ratio = (r.input_bbox_diag > 0) ? alpha / r.input_bbox_diag : 0;
    r.offset_ratio = (r.input_bbox_diag > 0) ? offset / r.input_bbox_diag : 0;

    // Input stats
    if (auto* pc = dynamic_cast<const easy3d::PointCloud*>(input)) {
        r.input_points = pc->n_vertices();
    } else if (auto* sm = dynamic_cast<const easy3d::SurfaceMesh*>(input)) {
        r.input_vertices = sm->n_vertices();
        r.input_faces = sm->n_faces();
    }

    // Output basics
    r.out_vertices = output->n_vertices();
    r.out_faces = output->n_faces();
    r.out_bbox_diag = output->bounding_box().diagonal_length();

    // Watertightness
    int borders = 0;
    for (auto h : output->halfedges())
        if (output->is_border(h)) ++borders;
    r.out_borders = borders;
    r.out_watertight = output->is_closed() && borders == 0;

    // --- One-sided Hausdorff: output vertices -> input ---
    // Paper Figure 15: this is the key comparison metric.
    // For output->input, iterate output vertices and query input via kd-tree.
    // Input can be PointCloud (use KdTreeSearch_ETH on points) or
    // SurfaceMesh (use TriangleMeshKdTree on faces).
    std::vector<float> dists;
    dists.reserve(r.out_vertices);

    // Build kd-tree on input geometry
    if (dynamic_cast<const easy3d::PointCloud*>(input)) {
        // Use kd-tree (same as Analysis menu PC-distance path). Brute-force
        // O(N*M) loop here -- even with subsampling -- was a regression of
        // the same fix done for the PC distance tool (see change_log 2026-05-12).
        auto* in_pc = const_cast<easy3d::PointCloud*>(
            static_cast<const easy3d::PointCloud*>(input));
        easy3d::KdTreeSearch_ETH kd(in_pc);
        float max_d = 0, sum_d = 0, sum_sq = 0;
        for (auto v : output->vertices()) {
            float sq_dist;
            kd.find_closest_point(output->position(v), sq_dist);
            float d = std::sqrt(sq_dist);
            dists.push_back(d);
            max_d = std::max(max_d, d);
            sum_d += d;
            sum_sq += d * d;
        }
        r.hausdorff_max = max_d;
        r.hausdorff_rms = (r.out_vertices > 0) ? std::sqrt(sum_sq / r.out_vertices) : 0;
        r.chamfer       = (r.out_vertices > 0) ? sum_d / r.out_vertices : 0;
    } else if (dynamic_cast<const easy3d::SurfaceMesh*>(input)) {
        auto* in_sm = const_cast<easy3d::SurfaceMesh*>(
            static_cast<const easy3d::SurfaceMesh*>(input));
        // KD-tree on input mesh faces
        easy3d::TriangleMeshKdTree in_kd(in_sm);
        float max_d = 0, sum_d = 0, sum_sq = 0;
        for (auto v : output->vertices()) {
            auto nn = in_kd.nearest(output->position(v));
            dists.push_back(nn.dist);
            max_d = std::max(max_d, nn.dist);
            sum_d += nn.dist;
            sum_sq += nn.dist * nn.dist;
        }
        r.hausdorff_max = max_d;
        r.hausdorff_rms = std::sqrt(sum_sq / r.out_vertices);
        r.chamfer = sum_d / r.out_vertices;
    }

    // Theoretical bound (paper Section 5.2: upper bound = alpha + delta)
    r.theoretical_bound = alpha + offset;
    r.bound_satisfied = (r.hausdorff_max <= r.theoretical_bound * 1.01f); // 1% tolerance for floating point

    // Distance distribution
    if (!dists.empty()) {
        std::sort(dists.begin(), dists.end());
        r.dist_median = dists[dists.size() / 2];
        r.dist_p90 = dists[(int)(dists.size() * 0.9)];
    }

    return r;
}

std::string format_aw3_report_for_ai(const AW3QualityReport& r) {
    std::ostringstream s;
    s << "## Alpha Wrapping 3D Quality Report\n\n";

    s << "### Parameters\n";
    s << "alpha=" << r.alpha << " offset=" << r.offset;
    s << " (alpha/BBox=" << r.alpha_ratio << " offset/BBox=" << r.offset_ratio << ")";
    s << " elapsed=" << r.elapsed_sec << "s\n\n";

    s << "### Input vs Output\n";
    s << "| Metric | Input | Output |\n";
    s << "|--------|-------|--------|\n";
    if (r.input_points > 0)
        s << "| Points/Vertices | " << r.input_points << " pts | " << r.out_vertices << " v |\n";
    else
        s << "| Vertices | " << r.input_vertices << " | " << r.out_vertices << " |\n";
    if (r.input_faces > 0)
        s << "| Faces | " << r.input_faces << " | " << r.out_faces << " |\n";
    else
        s << "| Faces | -- | " << r.out_faces << " |\n";
    s << "| BBox Diagonal | " << r.input_bbox_diag << " | " << r.out_bbox_diag << " |\n\n";

    s << "### Validity (paper guarantees watertight + enclosing)\n";
    s << "| Metric | Value | Judgment |\n";
    s << "|--------|-------|----------|\n";
    s << "| Watertight | " << (r.out_watertight ? "YES" : "NO") << " | "
      << (r.out_watertight ? "OK (guaranteed)" : "UNEXPECTED") << " |\n";
    s << "| Border Edges | " << r.out_borders << " | "
      << (r.out_borders == 0 ? "OK" : "check") << " |\n\n";

    s << "### Approximation Error (one-sided Hausdorff, output->input)\n";
    s << "| Metric | Value | Benchmark |\n";
    s << "|--------|-------|----------|\n";
    s << "| Hausdorff Max | " << r.hausdorff_max << " | < " << r.theoretical_bound
      << " (alpha+offset)" << " |\n";
    s << "| Hausdorff RMS | " << r.hausdorff_rms << " | -- |\n";
    s << "| Chamfer (mean) | " << r.chamfer << " | ~= " << r.offset << " (offset)" << " |\n";
    s << "| Distance Median | " << r.dist_median << " | -- |\n";
    s << "| Distance P90 | " << r.dist_p90 << " | -- |\n";
    s << "| Bound Satisfied | " << (r.bound_satisfied ? "YES" : "NO") << " | hausdorff_max <= alpha+offset |\n\n";

    // Paper Section 5.2: peak distance ~= offset
    s << "### Distance Distribution (paper Figure 7)\n";
    if (std::abs(r.chamfer - r.offset) < r.offset * 0.5f) {
        s << "- Chamfer distance near offset (" << r.offset << ") -> **GOOD**: vertices are on the offset surface as expected\n";
    } else if (r.chamfer > r.offset * 1.5f) {
        s << "- **Chamfer > 1.5x offset** -> vertices are farther from input than expected. ";
        s << "Consider decreasing offset for tighter fit, or decreasing alpha for more refinement.\n";
    } else {
        s << "- Chamfer distance reasonable relative to offset\n";
    }

    if (r.bound_satisfied) {
        s << "- Hausdorff bound (alpha+offset) satisfied -> **GOOD**\n";
    } else {
        s << "- **Hausdorff exceeds theoretical bound!** " << r.hausdorff_max << " > " << r.theoretical_bound << "\n";
        s << "  This may indicate insufficient refinement. Try decreasing alpha "
          << "(current alpha/BBox=" << r.alpha_ratio << ").\n";
    }

    s << "\n### Output Complexity\n";
    s << "| Metric | Value |\n";
    s << "|--------|-------|\n";
    s << "| Vertices | " << r.out_vertices << " |\n";
    s << "| Faces | " << r.out_faces << " |\n\n";

    // Guidance based on paper
    s << "### Parameter Tuning Guidance (from paper)\n";
    s << "- **alpha**: controls minimum carving size. Larger = coarser output, faster.\n";
    s << "  Small alpha captures fine details; large alpha defeats small cavities.\n";
    if (r.alpha_ratio < 0.002f)
        s << "  Current alpha/BBox=" << r.alpha_ratio << " is VERY small -> very fine output expected.\n";
    else if (r.alpha_ratio > 0.1f)
        s << "  Current alpha/BBox=" << r.alpha_ratio << " is LARGE -> heavy defeaturing expected.\n";

    s << "- **offset**: distance from input to output vertices.\n";
    s << "  Small = tight fit, more refinement (Rule R2 dominant). Large = looser, better element quality (Rule R1 dominant).\n";
    if (r.offset_ratio < 0.001f)
        s << "  Current offset/BBox=" << r.offset_ratio << " is VERY small -> tight fit, potentially more faces.\n";
    else if (r.offset / r.alpha > 2.0f)
        s << "  **offset/alpha ratio > 2**: offset is large relative to alpha -> favoring mesh quality over tightness.\n";

    if (r.out_faces > 500000)
        s << "- **Output is complex (" << r.out_faces << " faces)**. Consider increasing alpha and/or offset to reduce.\n";
    else if (r.out_faces < 100 && (r.input_points > 10000 || r.input_vertices > 10000))
        s << "- **Output is very sparse**. Try decreasing alpha for more detail, or decreasing offset for tighter fit.\n";

    s << "- For faster execution: increase alpha and/or offset (paper Figure 11).\n";
    s << "- For tighter approximation: decrease both alpha and offset.\n";
    s << "- For better mesh quality: increase offset (more Rule R1 insertions).\n";

    return s.str();
}

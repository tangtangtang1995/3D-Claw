// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// RANSAC live-preview overlays.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/surface_mesh_builder.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <cmath>

void MainWindow::update_ransac_samples_overlay(const double pts[3][3]) {
    if (!model_is_live(viewer_, algorithm_overlay_.ransac_samples))
        algorithm_overlay_.ransac_samples = nullptr;

    auto* saved_current = viewer_.current_model();

    // Easy3D's Graph stores arbitrary vertex/edge topology. We rebuild from
    // scratch every update (3 verts + 3 edges, tiny).
    delete_model_if_live(viewer_, algorithm_overlay_.ransac_samples);
    algorithm_overlay_.ransac_samples = new easy3d::Graph;
    algorithm_overlay_.ransac_samples->set_name("ransac_samples");
    auto v0 = algorithm_overlay_.ransac_samples->add_vertex(easy3d::vec3(
        (float)pts[0][0], (float)pts[0][1], (float)pts[0][2]));
    auto v1 = algorithm_overlay_.ransac_samples->add_vertex(easy3d::vec3(
        (float)pts[1][0], (float)pts[1][1], (float)pts[1][2]));
    auto v2 = algorithm_overlay_.ransac_samples->add_vertex(easy3d::vec3(
        (float)pts[2][0], (float)pts[2][1], (float)pts[2][2]));
    algorithm_overlay_.ransac_samples->add_edge(v0, v1);
    algorithm_overlay_.ransac_samples->add_edge(v1, v2);
    algorithm_overlay_.ransac_samples->add_edge(v2, v0);
    viewer_.add_model(algorithm_overlay_.ransac_samples);
    viewer_.register_model_tree_overlay(
        algorithm_overlay_.ransac_samples, saved_current);

    auto* vd = algorithm_overlay_.ransac_samples->renderer()
        ->get_points_drawable("vertices", false);
    if (vd) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.85f, 0.10f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(12.0f);
        vd->update();
    }
    auto* ed = algorithm_overlay_.ransac_samples->renderer()
        ->get_lines_drawable("edges", false);
    if (ed) {
        ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.80f, 0.10f, 1.0f));
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(3.0f);
        ed->update();
    }

    if (model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::update_ransac_candidate_overlay(
    const double plane_eq[4], const double sample_pts[3][3], float bbox_diag)
{
    if (!model_is_live(viewer_, algorithm_overlay_.ransac_candidate))
        algorithm_overlay_.ransac_candidate = nullptr;

    double a = plane_eq[0], b = plane_eq[1], c = plane_eq[2], d = plane_eq[3];
    double len2 = a*a + b*b + c*c;
    if (len2 < 1e-20) return;

    easy3d::vec3 normal((float)a, (float)b, (float)c);
    normal = normalize(normal);
    easy3d::vec3 t1, t2;
    if (std::abs(normal.x) < 0.9f) t1 = normalize(cross(normal, easy3d::vec3(1, 0, 0)));
    else                           t1 = normalize(cross(normal, easy3d::vec3(0, 1, 0)));
    t2 = normalize(cross(normal, t1));

    // Anchor at the 3 sample points' centroid. The candidate plane was fit
    // from these 3 points, so it passes through them.
    easy3d::vec3 center(0, 0, 0);
    if (sample_pts) {
        easy3d::vec3 p0((float)sample_pts[0][0], (float)sample_pts[0][1], (float)sample_pts[0][2]);
        easy3d::vec3 p1((float)sample_pts[1][0], (float)sample_pts[1][1], (float)sample_pts[1][2]);
        easy3d::vec3 p2((float)sample_pts[2][0], (float)sample_pts[2][1], (float)sample_pts[2][2]);
        center = (p0 + p1 + p2) / 3.0f;
    } else {
        double inv_norm = 1.0 / std::sqrt(len2);
        double dist = d * inv_norm;
        center = -normal * (float)dist;
    }
    float half = 0.15f * bbox_diag;

    easy3d::vec3 c0 = center - t1*half - t2*half;
    easy3d::vec3 c1 = center + t1*half - t2*half;
    easy3d::vec3 c2 = center + t1*half + t2*half;
    easy3d::vec3 c3 = center - t1*half + t2*half;

    auto* saved_current = viewer_.current_model();

    delete_model_if_live(viewer_, algorithm_overlay_.ransac_candidate);
    algorithm_overlay_.ransac_candidate = new easy3d::SurfaceMesh;
    algorithm_overlay_.ransac_candidate->set_name("ransac_candidate");
    easy3d::SurfaceMeshBuilder bld(algorithm_overlay_.ransac_candidate);
    bld.begin_surface();
    auto vh0 = bld.add_vertex(c0); auto vh1 = bld.add_vertex(c1);
    auto vh2 = bld.add_vertex(c2); auto vh3 = bld.add_vertex(c3);
    bld.add_triangle(vh0, vh1, vh2);
    bld.add_triangle(vh0, vh2, vh3);
    bld.end_surface(false);
    viewer_.add_model(algorithm_overlay_.ransac_candidate);
    viewer_.register_model_tree_overlay(
        algorithm_overlay_.ransac_candidate, saved_current);

    auto* fd = algorithm_overlay_.ransac_candidate->renderer()
        ->get_triangles_drawable("faces", false);
    if (fd) {
        fd->set_uniform_coloring(easy3d::vec4(1.0f, 0.95f, 0.20f, 1.0f));
        fd->set_opacity(0.35f);
        fd->set_smooth_shading(false);
        fd->update();
    }
    auto* ed = algorithm_overlay_.ransac_candidate->renderer()
        ->get_lines_drawable("edges", false);
    if (ed) {
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.90f, 0.20f, 1.0f));
        ed->set_line_width(1.5f);
        ed->update();
    }

    if (model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

// Must only be called from the main thread after the running flag is false.
void MainWindow::clear_ransac_live_overlays() {
    auto* saved_current = viewer_.current_model();
    delete_model_if_live(viewer_, algorithm_overlay_.ransac_samples);
    delete_model_if_live(viewer_, algorithm_overlay_.ransac_candidate);
    if (model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

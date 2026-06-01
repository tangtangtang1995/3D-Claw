// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <vector>

void MainWindow::init_ppr_overlay(easy3d::SurfaceMesh* src) {
    clear_ppr_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.planar_patch_remeshing;
    state.source_ghost = src;
    apply_source_wireframe_ghost(
        src,
        state.source_saved_visible,
        state.source_saved_opacity,
        state.source_saved_edge_visible,
        state.source_saved_edge_coloring_method,
        state.source_saved_edge_color,
        state.source_saved_edge_width,
        easy3d::vec4(0.65f, 0.65f, 0.65f, 1.0f));

    state.patch_mesh = create_surface_overlay(viewer_, "ppr_patches");
    auto src_pts = src->get_vertex_property<easy3d::vec3>("v:point");
    std::vector<easy3d::SurfaceMesh::Vertex> vmap;
    vmap.reserve(src->n_vertices());
    for (auto v : src->vertices())
        vmap.push_back(state.patch_mesh->add_vertex(src_pts[v]));
    for (auto f : src->faces()) {
        int idx[3]{-1, -1, -1}; int k = 0;
        for (auto v : src->vertices(f)) {
            if (k < 3) idx[k] = static_cast<int>(v.idx());
            ++k;
        }
        if (k == 3) {
            state.patch_mesh->add_triangle(
                vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
        }
    }
    state.patch_mesh->add_face_property<easy3d::vec3>(
        "f:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    if (auto* fd = state.patch_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_property_coloring(easy3d::State::FACE, "f:color");
        fd->set_opacity(1.0f);
        fd->set_lighting(false);
        fd->set_distinct_back_color(false);
        fd->update();
    }
    state.patch_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::update_ppr_patch_overlay(
    const std::vector<int>& face_patch_ids)
{
    auto& state = algorithm_overlay_.planar_patch_remeshing;
    if (!state.patch_mesh || face_patch_ids.empty()) return;
    if (!model_is_live(viewer_, state.patch_mesh)) {
        state.patch_mesh = nullptr;
        return;
    }
    auto fcolor = state.patch_mesh->get_face_property<easy3d::vec3>("f:color");
    if (!fcolor) {
        fcolor = state.patch_mesh->add_face_property<easy3d::vec3>(
            "f:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    }
    const int nf = static_cast<int>(face_patch_ids.size());
    int i = 0;
    for (auto f : state.patch_mesh->faces()) {
        if (i >= nf) break;
        const int pid = face_patch_ids[i++];
        fcolor[f] = (pid < 0)
                        ? easy3d::vec3(0.18f, 0.18f, 0.20f)
                        : ransac_shape_color(pid);
    }
    if (auto* fd = state.patch_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->update();
    }
    state.patch_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::update_ppr_constraint_overlay(
    const std::vector<PPR_Point3d>& edge_endpoints)
{
    if (edge_endpoints.size() < 2) return;
    auto& state = algorithm_overlay_.planar_patch_remeshing;
    if (state.constraint_graph && !model_is_live(viewer_, state.constraint_graph))
        state.constraint_graph = nullptr;
    auto* saved_current = viewer_.current_model();

    delete_model_if_live(viewer_, state.constraint_graph);
    state.constraint_graph = create_graph_overlay(viewer_, "ppr_constraints");
    for (std::size_t i = 0; i + 1 < edge_endpoints.size(); i += 2) {
        const auto& a = edge_endpoints[i];
        const auto& b = edge_endpoints[i + 1];
        auto va = state.constraint_graph->add_vertex(
            easy3d::vec3((float)a.x, (float)a.y, (float)a.z));
        auto vb = state.constraint_graph->add_vertex(
            easy3d::vec3((float)b.x, (float)b.y, (float)b.z));
        state.constraint_graph->add_edge(va, vb);
    }
    if (auto* ld = state.constraint_graph->renderer()->get_lines_drawable(
            "edges", false)) {
        ld->set_uniform_coloring(easy3d::vec4(0.1f, 0.85f, 0.95f, 1.0f));
        ld->set_line_width(2.5f);
        ld->update();
    }
    if (auto* vd = state.constraint_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_visible(false);
    }
    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::update_ppr_corner_overlay(
    const std::vector<PPR_Point3d>& corner_points)
{
    if (corner_points.empty()) return;
    auto& state = algorithm_overlay_.planar_patch_remeshing;
    if (state.corner_graph && !model_is_live(viewer_, state.corner_graph))
        state.corner_graph = nullptr;
    auto* saved_current = viewer_.current_model();

    delete_model_if_live(viewer_, state.corner_graph);
    state.corner_graph = create_graph_overlay(viewer_, "ppr_corners");
    const std::size_t step =
        std::max<std::size_t>(1, corner_points.size() / 2000 + 1);
    for (std::size_t i = 0; i < corner_points.size(); i += step) {
        const auto& p = corner_points[i];
        state.corner_graph->add_vertex(
            easy3d::vec3((float)p.x, (float)p.y, (float)p.z));
    }
    if (auto* vd = state.corner_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.3f, 0.85f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(8.0f);
        vd->update();
    }
    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::clear_ppr_overlay() {
    auto& state = algorithm_overlay_.planar_patch_remeshing;
    delete_model_if_live(viewer_, state.patch_mesh);
    delete_model_if_live(viewer_, state.constraint_graph);
    delete_model_if_live(viewer_, state.corner_graph);
    if (state.source_ghost && model_is_live(viewer_, state.source_ghost)) {
        restore_source_wireframe_ghost(
            state.source_ghost,
            state.source_saved_visible,
            state.source_saved_opacity,
            state.source_saved_edge_visible,
            state.source_saved_edge_coloring_method,
            state.source_saved_edge_color,
            state.source_saved_edge_width);
    }
    state.source_ghost = nullptr;
    viewer_.mark_dirty();
}

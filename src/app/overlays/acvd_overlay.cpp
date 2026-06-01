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
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <vector>

void MainWindow::init_acvd_overlay(easy3d::SurfaceMesh* src) {
    auto& state = algorithm_overlay_.acvd;
    clear_acvd_overlay();
    if (!src) return;
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
    viewer_.mark_dirty();
}

void MainWindow::update_acvd_cluster_overlay(
    const std::vector<ACVD_Point3d>& verts,
    const std::vector<ACVD_Triangle>& tris,
    const std::vector<int>& face_cluster_ids)
{
    auto& state = algorithm_overlay_.acvd;
    if (verts.empty() || tris.empty() || face_cluster_ids.empty()) return;

    if (state.cluster_mesh && !model_is_live(viewer_, state.cluster_mesh))
        state.cluster_mesh = nullptr;
    auto* saved_current = viewer_.current_model();

    if (!state.cluster_mesh) {
        state.cluster_mesh = create_surface_overlay(viewer_, "acvd_clusters");
    } else {
        state.cluster_mesh->clear();
    }

    auto fcolor = state.cluster_mesh->add_face_property<easy3d::vec3>(
        "f:color", easy3d::vec3(0.12f, 0.12f, 0.12f));
    std::vector<easy3d::SurfaceMesh::Vertex> vh;
    vh.reserve(verts.size());
    for (const auto& p : verts)
        vh.push_back(state.cluster_mesh->add_vertex(
            easy3d::vec3((float)p.x, (float)p.y, (float)p.z)));
    const int nv = (int)vh.size();
    for (size_t i = 0; i < tris.size(); ++i) {
        const auto& t = tris[i];
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        if (t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) continue;
        auto fh = state.cluster_mesh->add_triangle(
            vh[t.v0], vh[t.v1], vh[t.v2]);
        if (fh.is_valid() && (int)i < (int)face_cluster_ids.size()) {
            const int cid = face_cluster_ids[i];
            fcolor[fh] = (cid < 0)
                ? easy3d::vec3(0.18f, 0.18f, 0.20f)
                : ransac_shape_color(cid);
        }
    }

    if (auto* fd = state.cluster_mesh->renderer()->get_triangles_drawable(
            "faces", false))
    {
        fd->set_property_coloring(easy3d::State::FACE, "f:color");
        fd->set_opacity(1.0f);
        fd->set_lighting(false);
        fd->set_distinct_back_color(false);
        fd->update();
    }
    state.cluster_mesh->renderer()->update();

    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::update_acvd_seed_overlay(
    const std::vector<ACVD_Point3d>& seeds)
{
    auto& state = algorithm_overlay_.acvd;
    if (seeds.empty()) return;
    if (state.seed_graph && !model_is_live(viewer_, state.seed_graph))
        state.seed_graph = nullptr;
    auto* saved_current = viewer_.current_model();

    delete_model_if_live(viewer_, state.seed_graph);
    state.seed_graph = create_graph_overlay(viewer_, "acvd_seeds");
    const size_t step = std::max<size_t>(1, seeds.size() / 500 + 1);
    std::vector<easy3d::Graph::Vertex> gv;
    for (size_t i = 0; i < seeds.size(); i += step) {
        const auto& p = seeds[i];
        gv.push_back(state.seed_graph->add_vertex(
            easy3d::vec3((float)p.x, (float)p.y, (float)p.z)));
    }
    auto* vd = state.seed_graph->renderer()->get_points_drawable("vertices", false);
    if (vd) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.3f, 0.6f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(6.0f);
        vd->update();
    }
    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::clear_acvd_overlay() {
    auto& state = algorithm_overlay_.acvd;
    delete_model_if_live(viewer_, state.cluster_mesh);
    delete_model_if_live(viewer_, state.seed_graph);
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

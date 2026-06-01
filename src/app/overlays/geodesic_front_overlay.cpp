// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Geodesic front propagation distance heatmap overlay.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"
#include "viewport/scene_lighting.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <cstdint>
#include <vector>

static easy3d::vec3 geo_distance_color(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const easy3d::vec3 c0(0.10f, 0.20f, 0.95f);
    const easy3d::vec3 c1(0.20f, 0.85f, 0.95f);
    const easy3d::vec3 c2(0.95f, 0.90f, 0.30f);
    const easy3d::vec3 c3(0.95f, 0.25f, 0.25f);
    auto lerp = [](const easy3d::vec3& a, const easy3d::vec3& b, float k) {
        return easy3d::vec3(a.x + (b.x - a.x) * k,
                            a.y + (b.y - a.y) * k,
                            a.z + (b.z - a.z) * k);
    };
    if (t < 1.0 / 3.0) return lerp(c0, c1, (float)(t * 3.0));
    if (t < 2.0 / 3.0) return lerp(c1, c2, (float)((t - 1.0 / 3.0) * 3.0));
    return lerp(c2, c3, (float)((t - 2.0 / 3.0) * 3.0));
}

void MainWindow::init_front_overlay(easy3d::SurfaceMesh* src) {
    clear_front_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.front_propagation;
    state.source_ghost = src;
    state.source_saved_visible = src->renderer()->is_visible();
    if (auto* fd = src->renderer()->get_triangles_drawable("faces", false)) {
        state.source_saved_opacity = fd->opacity();
        state.source_saved_face_visible = fd->is_visible();
        fd->set_visible(false);
        fd->update();
    }
    if (auto* ed = src->renderer()->get_lines_drawable("edges", false)) {
        state.source_saved_edge_visible = ed->is_visible();
        state.source_saved_edge_coloring_method =
            static_cast<int>(ed->coloring_method());
        state.source_saved_edge_color = ed->color();
        state.source_saved_edge_width = ed->line_width();
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.55f, 0.55f, 0.55f, 1.0f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    src->renderer()->update();

    auto src_pts = src->get_vertex_property<easy3d::vec3>("v:point");
    if (!src_pts) {
        clear_front_overlay();
        return;
    }
    state.overlay_mesh = create_surface_overlay(viewer_, "geo_front");
    std::vector<easy3d::SurfaceMesh::Vertex> vmap(
        src->vertices_size(), easy3d::SurfaceMesh::Vertex());
    for (auto v : src->vertices()) {
        const int id = (int)v.idx();
        if (id >= 0 && id < (int)vmap.size())
            vmap[id] = state.overlay_mesh->add_vertex(src_pts[v]);
    }
    for (auto f : src->faces()) {
        easy3d::SurfaceMesh::Vertex tri[3];
        int k = 0;
        bool valid_triangle = true;
        for (auto v : src->vertices(f)) {
            if (k >= 3) {
                valid_triangle = false;
                break;
            }
            const int id = static_cast<int>(v.idx());
            if (id < 0 || id >= (int)vmap.size() || !vmap[id].is_valid()) {
                valid_triangle = false;
                break;
            }
            tri[k++] = vmap[id];
        }
        if (valid_triangle && k == 3)
            state.overlay_mesh->add_triangle(tri[0], tri[1], tri[2]);
    }
    state.overlay_mesh->add_vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    if (auto* fd = state.overlay_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_opacity(1.0f);
        fd->set_lighting(claw3d::scene_lighting_enabled());
        fd->set_distinct_back_color(false);
        fd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        fd->update();
    }
    if (auto* ed = state.overlay_mesh->renderer()->get_lines_drawable(
            "edges", false)) {
        ed->set_visible(false);
    }
    state.overlay_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::update_front_overlay(const GEO_FrontSnapshot& snap) {
    auto& state = algorithm_overlay_.front_propagation;
    if (!state.overlay_mesh || snap.vertex_distances.empty()) return;
    if (!model_is_live(viewer_, state.overlay_mesh)) {
        state.overlay_mesh = nullptr;
        return;
    }
    auto col = state.overlay_mesh->get_vertex_property<easy3d::vec3>("v:color");
    if (!col) {
        col = state.overlay_mesh->add_vertex_property<easy3d::vec3>(
            "v:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    }
    const int n = (int)snap.vertex_distances.size();
    const double denom = std::max(0.001, (double)snap.max_distance);
    int i = 0;
    for (auto v : state.overlay_mesh->vertices()) {
        if (i >= n) break;
        const float d = snap.vertex_distances[i];
        const std::uint8_t st =
            (i < (int)snap.vertex_state.size()) ? snap.vertex_state[i] : 0;
        if (st == 0 || d >= 1e30f) {
            col[v] = easy3d::vec3(0.18f, 0.18f, 0.20f);
        } else {
            const double t = (double)d / denom;
            col[v] = geo_distance_color(t);
        }
        ++i;
    }
    if (auto* fd = state.overlay_mesh->renderer()->get_triangles_drawable(
            "faces", false)) fd->update();
    state.overlay_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::clear_front_overlay() {
    auto& state = algorithm_overlay_.front_propagation;
    delete_model_if_live(viewer_, state.overlay_mesh);
    if (state.source_ghost && model_is_live(viewer_, state.source_ghost)) {
        state.source_ghost->renderer()->set_visible(state.source_saved_visible);
        if (auto* fd = state.source_ghost->renderer()->get_triangles_drawable(
                "faces", false)) {
            fd->set_visible(state.source_saved_face_visible);
            fd->set_opacity(state.source_saved_opacity);
            fd->update();
        }
        if (auto* ed = state.source_ghost->renderer()->get_lines_drawable(
                "edges", false)) {
            ed->set_visible(state.source_saved_edge_visible);
            ed->set_coloring_method(
                static_cast<easy3d::State::Method>(
                    state.source_saved_edge_coloring_method));
            ed->set_color(state.source_saved_edge_color);
            ed->set_line_width(state.source_saved_edge_width);
            ed->update();
        }
        state.source_ghost->renderer()->update();
    }
    state.source_ghost = nullptr;
    viewer_.mark_dirty();
}

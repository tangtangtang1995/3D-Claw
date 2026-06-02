// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "overlays/overlay_controller.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"
#include "viewport/scene_lighting.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <vector>

namespace {

easy3d::vec3 displacement_color(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const easy3d::vec3 blue  (0.18f, 0.42f, 0.92f);
    const easy3d::vec3 yellow(1.00f, 0.95f, 0.30f);
    const easy3d::vec3 red   (0.95f, 0.20f, 0.20f);
    if (t < 0.5) {
        const float k = (float)(t / 0.5);
        return easy3d::vec3(blue.x + (yellow.x - blue.x) * k,
                            blue.y + (yellow.y - blue.y) * k,
                            blue.z + (yellow.z - blue.z) * k);
    }
    const float k = (float)((t - 0.5) / 0.5);
    return easy3d::vec3(yellow.x + (red.x - yellow.x) * k,
                        yellow.y + (red.y - yellow.y) * k,
                        yellow.z + (red.z - yellow.z) * k);
}

} // namespace

void OverlayController::init_smoothing_overlay(easy3d::SurfaceMesh* src) {
    clear_smoothing_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.smoothing;
    state.source_ghost = src;
    apply_source_wireframe_ghost(
        src,
        state.source_saved_visible,
        state.source_saved_opacity,
        state.source_saved_edge_visible,
        state.source_saved_edge_coloring_method,
        state.source_saved_edge_color,
        state.source_saved_edge_width,
        easy3d::vec4(0.55f, 0.55f, 0.55f, 1.0f));

    state.current_mesh = create_surface_overlay(viewer_, "smoothing_current");
    auto src_pts = src->get_vertex_property<easy3d::vec3>("v:point");
    std::vector<easy3d::SurfaceMesh::Vertex> vmap;
    vmap.reserve(src->n_vertices());
    for (auto v : src->vertices())
        vmap.push_back(state.current_mesh->add_vertex(src_pts[v]));
    for (auto f : src->faces()) {
        int idx[3]{-1, -1, -1}; int k = 0;
        for (auto v : src->vertices(f)) {
            if (k < 3) idx[k] = static_cast<int>(v.idx());
            ++k;
        }
        if (k == 3) {
            state.current_mesh->add_triangle(
                vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
        }
    }
    state.current_mesh->add_vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(0.5f, 0.65f, 0.85f));
    if (auto* fd = state.current_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_opacity(1.0f);
        fd->set_lighting(claw3d::scene_lighting_enabled());
        fd->set_distinct_back_color(false);
        fd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        fd->update();
    }
    if (auto* ed = state.current_mesh->renderer()->get_lines_drawable(
            "edges", false)) {
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.1f, 0.1f, 0.15f, 1.0f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    state.current_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::update_smoothing_overlay(const SMOOTH_Snapshot& snap)
{
    auto& state = algorithm_overlay_.smoothing;
    if (!state.current_mesh || snap.vertices.empty()) return;
    if (!model_is_live(viewer_, state.current_mesh)) {
        state.current_mesh = nullptr;
        return;
    }
    auto pts = state.current_mesh->get_vertex_property<easy3d::vec3>("v:point");
    auto col = state.current_mesh->get_vertex_property<easy3d::vec3>("v:color");
    if (!pts) return;
    if (!col) {
        col = state.current_mesh->add_vertex_property<easy3d::vec3>(
            "v:color", easy3d::vec3(0.5f, 0.65f, 0.85f));
    }
    const int n = (int)snap.vertices.size();
    const bool have_disp =
        !snap.vertex_displacement.empty() &&
        (int)snap.vertex_displacement.size() == n;
    const double bbox = snap.stats.bbox_diag > 0.0 ? snap.stats.bbox_diag : 1.0;
    const double denom = std::max(snap.max_displacement, bbox * 1e-6);
    int i = 0;
    for (auto v : state.current_mesh->vertices()) {
        if (i >= n) break;
        pts[v] = easy3d::vec3((float)snap.vertices[i].x,
                              (float)snap.vertices[i].y,
                              (float)snap.vertices[i].z);
        if (have_disp) {
            const double t = snap.vertex_displacement[i] / denom;
            col[v] = displacement_color(t);
        }
        ++i;
    }
    state.current_mesh->invalidate_bounding_box();
    if (auto* fd = state.current_mesh->renderer()->get_triangles_drawable(
            "faces", false)) fd->update();
    if (auto* ed = state.current_mesh->renderer()->get_lines_drawable(
            "edges", false)) ed->update();
    state.current_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::clear_smoothing_overlay() {
    auto& state = algorithm_overlay_.smoothing;
    delete_model_if_live(viewer_, state.current_mesh);
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

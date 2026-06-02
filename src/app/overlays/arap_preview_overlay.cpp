// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// ARAP deformation live-preview overlay.

#include "overlays/overlay_controller.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <vector>

static easy3d::vec3 arap_disp_color(double t) {
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

void OverlayController::init_arap_preview_overlay(easy3d::SurfaceMesh* src) {
    auto& state = algorithm_overlay_.arap_preview;
    clear_arap_preview_overlay(/*restore_source=*/true);
    if (!src) return;
    state.source_ghost = src;
    if (auto* fd = src->renderer()->get_triangles_drawable("faces", false)) {
        state.source_saved_face_visible = fd->is_visible();
        state.source_saved_opacity = fd->opacity();
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
        ed->set_uniform_coloring(easy3d::vec4(0.55f, 0.55f, 0.55f, 0.55f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    src->renderer()->update();

    state.preview_mesh = new easy3d::SurfaceMesh;
    state.preview_mesh->set_name("arap_preview");
    auto src_pts = src->get_vertex_property<easy3d::vec3>("v:point");
    std::vector<easy3d::SurfaceMesh::Vertex> vmap;
    vmap.reserve(src->n_vertices());
    for (auto v : src->vertices())
        vmap.push_back(state.preview_mesh->add_vertex(src_pts[v]));
    for (auto f : src->faces()) {
        int idx[3]{-1,-1,-1}; int k = 0;
        for (auto v : src->vertices(f)) {
            if (k < 3) idx[k] = (int)v.idx();
            ++k;
        }
        if (k == 3) state.preview_mesh->add_triangle(
            vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
    }
    state.preview_mesh->add_vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(0.18f, 0.42f, 0.92f));
    viewer_.add_model(state.preview_mesh);
    viewer_.register_model_tree_node(state.preview_mesh,
        ModelTreeNodeInfo{"_overlays", "arap_preview",
                          nullptr, ModelTreeNodeKind::Overlay, false});
    if (auto* fd = state.preview_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_visible(true);
        fd->set_opacity(1.0f);
        fd->set_lighting(false);
        fd->set_distinct_back_color(false);
        fd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        fd->update();
    }
    if (auto* ed = state.preview_mesh->renderer()->get_lines_drawable(
            "edges", false)) {
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.06f, 0.06f, 0.08f, 1.0f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    state.preview_mesh->renderer()->update();
    viewer_.set_current_model_silent(src);
    viewer_.mark_dirty();
}

bool OverlayController::has_arap_preview_overlay() {
    auto& state = algorithm_overlay_.arap_preview;
    return state.preview_mesh && model_is_live(viewer_, state.preview_mesh);
}

void OverlayController::update_arap_preview_overlay(const ARAP_Snapshot& snap) {
    auto& state = algorithm_overlay_.arap_preview;
    if (!state.preview_mesh || snap.vertices.empty()) return;
    if (!model_is_live(viewer_, state.preview_mesh)) {
        state.preview_mesh = nullptr;
        return;
    }
    auto pts = state.preview_mesh->get_vertex_property<easy3d::vec3>("v:point");
    auto col = state.preview_mesh->get_vertex_property<easy3d::vec3>("v:color");
    if (!pts) return;
    if (!col) {
        col = state.preview_mesh->add_vertex_property<easy3d::vec3>(
            "v:color", easy3d::vec3(0.18f, 0.42f, 0.92f));
    }
    const int n = (int)snap.vertices.size();
    const bool have_disp =
        !snap.vertex_displacement.empty() &&
        (int)snap.vertex_displacement.size() == n;
    const double denom =
        (snap.max_displacement > 1e-12) ? snap.max_displacement : 1.0;
    int i = 0;
    for (auto v : state.preview_mesh->vertices()) {
        if (i >= n) break;
        pts[v] = easy3d::vec3((float)snap.vertices[i].x,
                              (float)snap.vertices[i].y,
                              (float)snap.vertices[i].z);
        if (have_disp) {
            col[v] = arap_disp_color(snap.vertex_displacement[i] / denom);
        }
        ++i;
    }
    state.preview_mesh->invalidate_bounding_box();
    if (auto* fd = state.preview_mesh->renderer()->get_triangles_drawable(
            "faces", false)) fd->update();
    if (auto* ed = state.preview_mesh->renderer()->get_lines_drawable(
            "edges", false)) ed->update();
    state.preview_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::clear_arap_preview_overlay(bool restore_source) {
    auto& state = algorithm_overlay_.arap_preview;
    if (state.preview_mesh && model_is_live(viewer_, state.preview_mesh))
        viewer_.delete_model(state.preview_mesh);
    state.preview_mesh = nullptr;
    if (restore_source && state.source_ghost && model_is_live(viewer_, state.source_ghost)) {
        if (auto* fd = state.source_ghost->renderer()->get_triangles_drawable(
                "faces", false)) {
            fd->set_visible(state.source_saved_face_visible);
            fd->set_opacity(state.source_saved_opacity);
            fd->update();
        }
        if (auto* ed = state.source_ghost->renderer()->get_lines_drawable(
                "edges", false)) {
            ed->set_visible(state.source_saved_edge_visible);
            ed->set_coloring(
                static_cast<easy3d::State::Method>(state.source_saved_edge_coloring_method),
                easy3d::State::VERTEX, "v:point");
            ed->set_uniform_coloring(state.source_saved_edge_color);
            ed->set_line_width(state.source_saved_edge_width);
            ed->update();
        }
        state.source_ghost->renderer()->update();
        state.source_ghost = nullptr;
    }
    viewer_.mark_dirty();
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/model.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>


namespace {
// Internal helper: register a freshly added overlay model under the
// "_overlays" workspace with kind=Overlay. Used by both
// create_graph_overlay and create_surface_overlay so they emit
// byte-identical ModelTreeNodeInfo to the inline pattern they replace.
void register_overlay_node(ViewportCanvas& viewer,
                           easy3d::Model* model,
                           const std::string& name,
                           bool visible_in_tree) {
    viewer.register_model_tree_node(model,
        ModelTreeNodeInfo{"_overlays", name, nullptr,
                          ModelTreeNodeKind::Overlay, visible_in_tree});
}

void restore_current_model(ViewportCanvas& viewer, easy3d::Model* model) {
    if (model)
        viewer.set_current_model_silent(model);
}
} // namespace


easy3d::Graph* create_graph_overlay(ViewportCanvas& viewer,
                                    const std::string& name,
                                    bool visible_in_tree) {
    auto* saved_current = viewer.current_model();
    auto* g = new easy3d::Graph;
    g->set_name(name);
    viewer.add_model(g);
    register_overlay_node(viewer, g, name, visible_in_tree);
    restore_current_model(viewer, saved_current);
    return g;
}


easy3d::SurfaceMesh* create_surface_overlay(ViewportCanvas& viewer,
                                            const std::string& name,
                                            bool visible_in_tree) {
    auto* saved_current = viewer.current_model();
    auto* sm = new easy3d::SurfaceMesh;
    sm->set_name(name);
    viewer.add_model(sm);
    register_overlay_node(viewer, sm, name, visible_in_tree);
    restore_current_model(viewer, saved_current);
    return sm;
}


bool model_is_live(ViewportCanvas& viewer, easy3d::Model* model) {
    if (!model) return false;
    for (const auto& mp : viewer.models()) {
        if (mp.get() == model) return true;
    }
    return false;
}

namespace claw_overlay_detail {
void delete_live_model_base(ViewportCanvas& viewer, easy3d::Model* model) {
    viewer.delete_model(model);
}
} // namespace claw_overlay_detail

void apply_source_wireframe_ghost(
    easy3d::SurfaceMesh* mesh,
    bool& saved_renderer_visible,
    float& saved_face_opacity,
    bool& saved_edge_visible,
    int& saved_edge_coloring_method,
    easy3d::vec4& saved_edge_color,
    float& saved_edge_width,
    const easy3d::vec4& ghost_edge_color,
    float ghost_edge_width)
{
    if (!mesh || !mesh->renderer())
        return;

    saved_renderer_visible = mesh->renderer()->is_visible();

    if (auto* faces = mesh->renderer()->get_triangles_drawable("faces", false)) {
        saved_face_opacity = faces->opacity();
        faces->set_visible(false);
        faces->update();
    }

    if (auto* edges = mesh->renderer()->get_lines_drawable("edges", false)) {
        saved_edge_visible = edges->is_visible();
        saved_edge_coloring_method = static_cast<int>(edges->coloring_method());
        saved_edge_color = edges->color();
        saved_edge_width = edges->line_width();
        edges->set_visible(true);
        edges->set_uniform_coloring(ghost_edge_color);
        edges->set_line_width(ghost_edge_width);
        edges->update();
    }

    mesh->renderer()->update();
}

void restore_source_wireframe_ghost(
    easy3d::SurfaceMesh* mesh,
    bool saved_renderer_visible,
    float saved_face_opacity,
    bool saved_edge_visible,
    int saved_edge_coloring_method,
    const easy3d::vec4& saved_edge_color,
    float saved_edge_width)
{
    if (!mesh || !mesh->renderer())
        return;

    mesh->renderer()->set_visible(saved_renderer_visible);

    if (auto* faces = mesh->renderer()->get_triangles_drawable("faces", false)) {
        faces->set_visible(true);
        faces->set_opacity(saved_face_opacity);
        faces->update();
    }

    if (auto* edges = mesh->renderer()->get_lines_drawable("edges", false)) {
        edges->set_visible(saved_edge_visible);
        edges->set_coloring_method(
            static_cast<easy3d::State::Method>(saved_edge_coloring_method));
        edges->set_color(saved_edge_color);
        edges->set_line_width(saved_edge_width);
        edges->update();
    }

    mesh->renderer()->update();
}

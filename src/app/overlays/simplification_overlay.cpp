// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// CGAL Surface Mesh Simplification live-preview overlays.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"
#include "viewport/scene_lighting.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/state.h>

#include <vector>

// ==========================================================================
// CGAL Simplification live overlay
// ==========================================================================
//
// init: dim the source mesh and enable wireframe so the user can see triangle
//       density changing against the original silhouette.
// update: append new collapses to a ring buffer, rebuild the trail Graph.
// clear: undo the source mesh restyling. Visibility is left to completion
//        handler (which hides the source so the simplified child takes over).

void MainWindow::init_simpl_overlay(easy3d::SurfaceMesh* src) {
    clear_simpl_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.simplification;
    state.source_mesh = src;
    // Wireframe-only source ghost. Hiding source faces avoids depth-test
    // conflicts with the opaque live snapshot mesh while preserving silhouette
    // context through the edge drawable.
    apply_source_wireframe_ghost(
        src,
        state.source_saved_visible,
        state.source_saved_opacity,
        state.source_saved_edge_visible,
        state.source_saved_edge_coloring_method,
        state.source_saved_edge_color,
        state.source_saved_edge_width);
    viewer_.mark_dirty();
}

void MainWindow::update_simpl_overlay(
    const std::vector<SimplTrailEntry>& new_entries)
{
    auto& state = algorithm_overlay_.simplification;
    if (!state.source_mesh) return;

    // Append + cap. The ring buffer represents the "recent collapse history"
    //  - older entries fade out simply by being kicked from the queue.
    for (const auto& e : new_entries) {
        if ((int)state.trail_buffer.size() >= state.trail_cap)
            state.trail_buffer.pop_front();
        state.trail_buffer.push_back(e);
    }

    // Rebuild the trail Graph from the ring buffer. Cheap (cap = 64) so we
    // can do it every frame the queue changes.
    if (!model_is_live(viewer_, state.trail_graph))
        state.trail_graph = nullptr;
    auto* saved_current = viewer_.current_model();
    delete_model_if_live(viewer_, state.trail_graph);
    if (state.trail_buffer.empty()) {
        if (model_is_live(viewer_, saved_current))
            viewer_.set_current_model(saved_current);
        viewer_.mark_dirty();
        return;
    }
    state.trail_graph = create_graph_overlay(viewer_, "simpl_collapse_trail");
    // For each collapse: add p0, p1, placement as 3 vertices, and two edges
    // (p0->placement, p1->placement) showing the endpoints sliding toward
    // their collapse target.
    std::vector<easy3d::Graph::Vertex> placements;
    placements.reserve(state.trail_buffer.size());
    for (const auto& e : state.trail_buffer) {
        auto v0 = state.trail_graph->add_vertex(e.p0);
        auto v1 = state.trail_graph->add_vertex(e.p1);
        auto vp = state.trail_graph->add_vertex(e.placement);
        state.trail_graph->add_edge(v0, vp);
        state.trail_graph->add_edge(v1, vp);
        placements.push_back(vp);
    }
    // Bright orange edges, small gold placement spheres. Treated as overlay
    // (visible_in_tree=false) by create_graph_overlay above.
    if (auto* ed = state.trail_graph->renderer()
            ->get_lines_drawable("edges", false)) {
        ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.55f, 0.05f, 1.0f));
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(2.0f);
        ed->update();
    }
    if (auto* vd = state.trail_graph->renderer()
            ->get_points_drawable("vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.85f, 0.10f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(8.0f);
        vd->update();
    }

    if (model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

// Snapshot mesh: refill in place, no model churn. We keep one easy3d::
// SurfaceMesh alive for the whole live preview run; the dialog calls this
// every time the runner publishes a new snapshot generation. clear() empties
// the mesh, then we re-add vertices + triangles; the renderer keeps the
// drawables and just re-uploads the buffers.
void MainWindow::update_simpl_snapshot_mesh(
    const std::vector<SIMPL_Point3d>& verts,
    const std::vector<SIMPL_Triangle>& tris,
    float face_opacity)
{
    if (verts.empty() || tris.empty()) return;
    auto& state = algorithm_overlay_.simplification;

    if (!model_is_live(viewer_, state.snapshot_mesh))
        state.snapshot_mesh = nullptr;

    auto* saved_current = viewer_.current_model();

    if (!state.snapshot_mesh) {
        state.snapshot_mesh = create_surface_overlay(viewer_, "simpl_snapshot");
    } else {
        state.snapshot_mesh->clear();
    }

    std::vector<easy3d::SurfaceMesh::Vertex> vh;
    vh.reserve(verts.size());
    for (const auto& p : verts) {
        vh.push_back(state.snapshot_mesh->add_vertex(
            easy3d::vec3((float)p.x, (float)p.y, (float)p.z)));
    }
    const int n = (int)vh.size();
    for (const auto& t : tris) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        if (t.v0 >= n || t.v1 >= n || t.v2 >= n) continue;
        state.snapshot_mesh->add_triangle(vh[t.v0], vh[t.v1], vh[t.v2]);
    }

    // Style: plain solid green faces only. This is an intermediate mesh, so
    // keep it readable and avoid special material tricks. Easy3D's global
    // setting::surface_mesh_faces_opacity defaults to 0.6 and the renderer
    // can re-apply it on update; passing it explicitly here removes that
    // ambiguity; the snapshot now stays solid.
    (void)face_opacity;
    const float snapshot_opacity = 1.0f;
    if (auto* fd = state.snapshot_mesh->renderer()->get_triangles_drawable(
            "faces", false))
    {
        fd->set_coloring_method(easy3d::State::UNIFORM_COLOR);
        fd->set_uniform_coloring(
            easy3d::vec4(0.30f, 0.78f, 0.42f, snapshot_opacity));
        fd->set_opacity(snapshot_opacity);
        fd->set_lighting(claw3d::scene_lighting_enabled());
        fd->set_lighting_two_sides(false);
        fd->set_distinct_back_color(false);
        fd->set_material(easy3d::State::Material(
            easy3d::vec4(0.05f, 0.05f, 0.05f, 1.0f),
            easy3d::vec4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f));
        fd->update();
    }
    if (auto* ed = state.snapshot_mesh->renderer()->get_lines_drawable(
            "edges", false))
    {
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.08f, 0.28f, 0.14f, 1.0f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    if (auto* vd = state.snapshot_mesh->renderer()->get_points_drawable(
            "vertices", false))
    {
        vd->set_visible(false);
        vd->update();
    }
    state.snapshot_mesh->renderer()->update();

    // The source wireframe is only a startup reference. Once the live
    // simplified mesh exists, hide it so it does not compete with the green
    // snapshot edges.
    if (model_is_live(viewer_, state.source_mesh)) {
        if (auto* ed = state.source_mesh->renderer()->get_lines_drawable(
                "edges", false))
        {
            ed->set_visible(false);
            ed->update();
            state.source_mesh->renderer()->update();
        }
    }

    if (model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::set_simpl_snapshot_opacity(float opacity) {
    (void)opacity;
    auto& state = algorithm_overlay_.simplification;
    if (!state.snapshot_mesh)
        return;
    if (auto* fd = state.snapshot_mesh->renderer()->get_triangles_drawable(
            "faces", false))
    {
        fd->set_coloring_method(easy3d::State::UNIFORM_COLOR);
        fd->set_uniform_coloring(
            easy3d::vec4(0.30f, 0.78f, 0.42f, 1.0f));
        fd->set_opacity(1.0f);
        fd->set_lighting(claw3d::scene_lighting_enabled());
        fd->set_lighting_two_sides(false);
        fd->set_distinct_back_color(false);
        fd->set_material(easy3d::State::Material(
            easy3d::vec4(0.05f, 0.05f, 0.05f, 1.0f),
            easy3d::vec4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f));
        fd->update();
    }
    viewer_.mark_dirty();
}

void MainWindow::clear_simpl_snapshot_mesh() {
    delete_model_if_live(viewer_, algorithm_overlay_.simplification.snapshot_mesh);
    viewer_.mark_dirty();
}

void MainWindow::clear_simpl_overlay() {
    auto& state = algorithm_overlay_.simplification;
    delete_model_if_live(viewer_, state.trail_graph);
    state.trail_buffer.clear();

    // Snapshot mesh is also a live-preview-only model.
    delete_model_if_live(viewer_, state.snapshot_mesh);

    if (model_is_live(viewer_, state.source_mesh)) {
        // Restore source faces (we hid them during the ghost phase to avoid
        // depth-test interference with the snapshot mesh).
        restore_source_wireframe_ghost(
            state.source_mesh,
            state.source_saved_visible,
            state.source_saved_opacity,
            state.source_saved_edge_visible,
            state.source_saved_edge_coloring_method,
            state.source_saved_edge_color,
            state.source_saved_edge_width);
    }
    state.source_mesh = nullptr;
    viewer_.mark_dirty();
}


// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// ARAP deformation interaction and live-preview overlays.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <cmath>
#include <vector>

// ============================================================================
// ARAP deformation graph overlays.
// ============================================================================

static const easy3d::vec4 kARAPGroupColors[] = {
    easy3d::vec4(1.0f, 0.2f, 1.0f, 1.0f),   // magenta
    easy3d::vec4(1.0f, 0.5f, 0.0f, 1.0f),   // orange
    easy3d::vec4(0.2f, 1.0f, 0.2f, 1.0f),   // green
    easy3d::vec4(0.2f, 0.5f, 1.0f, 1.0f),   // blue
    easy3d::vec4(1.0f, 1.0f, 0.0f, 1.0f),   // yellow
    easy3d::vec4(0.0f, 1.0f, 1.0f, 1.0f),   // cyan
};

void MainWindow::update_arap_roi_overlay(
    const std::vector<easy3d::vec3>& pts)
{
    auto& state = algorithm_overlay_.arap_interaction;
    delete_model_if_live(viewer_, state.roi_graph);
    if (pts.empty()) { viewer_.mark_dirty(); return; }
    auto* saved_current = viewer_.current_model();
    state.roi_graph = new easy3d::Graph;
    state.roi_graph->set_name("arap_roi");
    for (const auto& p : pts) state.roi_graph->add_vertex(p);
    viewer_.add_model(state.roi_graph);
    viewer_.register_model_tree_node(state.roi_graph,
        ModelTreeNodeInfo{"_overlays", "arap_roi",
                          nullptr, ModelTreeNodeKind::Overlay, false});
    if (auto* vd = state.roi_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(0.0f, 1.0f, 0.9f, 1.0f)); // cyan
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(7.0f);
        vd->update();
    }
    if (saved_current)
        viewer_.set_current_model_silent(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::update_arap_ctrl_overlay(
    const std::vector<easy3d::vec3>& pts,
    const std::vector<int>& group_ids)
{
    auto& state = algorithm_overlay_.arap_interaction;
    delete_model_if_live(viewer_, state.control_graph);
    if (pts.empty()) { viewer_.mark_dirty(); return; }
    auto* saved_current = viewer_.current_model();
    state.control_graph = new easy3d::Graph;
    state.control_graph->set_name("arap_ctrl");
    for (const auto& p : pts) state.control_graph->add_vertex(p);
    viewer_.add_model(state.control_graph);
    viewer_.register_model_tree_node(state.control_graph,
        ModelTreeNodeInfo{"_overlays", "arap_ctrl",
                          nullptr, ModelTreeNodeKind::Overlay, false});
    if (auto* vd = state.control_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        // Per-vertex color by group via v:color property.
        const int n = (int)pts.size();
        auto vcolor = state.control_graph->vertex_property<easy3d::vec3>(
            "v:color", easy3d::vec3(1.0f, 0.2f, 1.0f));
        for (int i = 0; i < n; ++i) {
            int g = i < (int)group_ids.size() ? group_ids[i] : 0;
            if (g < 0) g = 0;
            const auto& c = kARAPGroupColors[g % 6];
            vcolor[easy3d::Graph::Vertex(i)] = easy3d::vec3(c.x, c.y, c.z);
        }
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(11.0f);
        vd->update();
    }
    if (saved_current)
        viewer_.set_current_model_silent(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::update_arap_arrow_overlay(
    const std::vector<easy3d::vec3>& from,
    const std::vector<easy3d::vec3>& to)
{
    auto& state = algorithm_overlay_.arap_interaction;
    delete_model_if_live(viewer_, state.arrow_graph);
    if (from.empty() || from.size() != to.size()) {
        viewer_.mark_dirty(); return;
    }
    // Only render arrows whose endpoints are actually different.
    // A zero-length cylinder impostor is invisible, and would otherwise
    // make the user think the overlay is broken when the active group's
    // transform is still identity.
    const float kEpsSqr = 1e-12f;
    int n_nonzero = 0;
    for (size_t i = 0; i < from.size(); ++i) {
        const easy3d::vec3 d = to[i] - from[i];
        if (d.length2() > kEpsSqr) ++n_nonzero;
    }
    if (n_nonzero == 0) {
        // No visible arrows yet (identity transform). Don't create an
        // empty graph; dialog hint tells the user to adjust translate
        // or rotate to preview targets.
        viewer_.mark_dirty();
        return;
    }
    auto* saved_current = viewer_.current_model();
    state.arrow_graph = new easy3d::Graph;
    state.arrow_graph->set_name("arap_arrows");
    // v:color so the target endpoint can wear a yellow sphere while the
    // source endpoint stays invisible (it sits underneath the magenta
    // control sphere anyway).
    auto vcol = state.arrow_graph->vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(1.0f, 0.85f, 0.2f));
    for (size_t i = 0; i < from.size(); ++i) {
        const easy3d::vec3 d = to[i] - from[i];
        if (d.length2() <= kEpsSqr) continue;
        auto v0 = state.arrow_graph->add_vertex(from[i]);
        auto v1 = state.arrow_graph->add_vertex(to[i]);
        state.arrow_graph->add_edge(v0, v1);
        vcol[v0] = easy3d::vec3(0.05f, 0.05f, 0.05f); // dark, hidden by ctrl sphere
        vcol[v1] = easy3d::vec3(1.0f, 0.85f, 0.2f);   // yellow target marker
    }
    viewer_.add_model(state.arrow_graph);
    viewer_.register_model_tree_node(state.arrow_graph,
        ModelTreeNodeInfo{"_overlays", "arap_arrows",
                          nullptr, ModelTreeNodeKind::Overlay, false});
    if (auto* ed = state.arrow_graph->renderer()->get_lines_drawable(
            "edges", false)) {
        ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.85f, 0.2f, 1.0f));
        // Cylinder impostor: consistent thickness on every GPU, unlike
        // GL_LINES width which can render as 1px regardless.
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(3.0f);
        ed->set_visible(true);
        ed->update();
    }
    // Yellow sphere marker at each target endpoint via v:color.
    if (auto* vd = state.arrow_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(8.0f);
        vd->set_visible(true);
        vd->update();
    }
    if (saved_current)
        viewer_.set_current_model_silent(saved_current);
    viewer_.mark_dirty();
}

void MainWindow::clear_arap_overlay() {
    auto& state = algorithm_overlay_.arap_interaction;
    delete_model_if_live(viewer_, state.roi_graph);
    delete_model_if_live(viewer_, state.control_graph);
    delete_model_if_live(viewer_, state.arrow_graph);
    delete_model_if_live(viewer_, state.frame_graph);
    viewer_.mark_dirty();
}

void MainWindow::update_arap_frame_overlay(
    const easy3d::vec3& origin_world,
    double tx, double ty, double tz,
    double rx_deg, double ry_deg, double rz_deg,
    float axis_length)
{
    auto& state = algorithm_overlay_.arap_interaction;
    delete_model_if_live(viewer_, state.frame_graph);
    if (axis_length <= 0.0f) {
        viewer_.mark_dirty();
        return;
    }
    // Apply translation + ZYX Euler rotation to the three local axes,
    // then render origin + 3 endpoints with 3 edges colored R/G/B for X/Y/Z.
    const double d2r = 3.141592653589793 / 180.0;
    const double sa = std::sin(rx_deg * d2r), ca = std::cos(rx_deg * d2r);
    const double sb = std::sin(ry_deg * d2r), cb = std::cos(ry_deg * d2r);
    const double sc = std::sin(rz_deg * d2r), cc = std::cos(rz_deg * d2r);
    // R = Rz * Ry * Rx (same convention as the runner's
    // apply_transform_to_origin in arap_deformation_runner.cpp).
    const double r00 = cc*cb, r01 = cc*sb*sa - sc*ca, r02 = cc*sb*ca + sc*sa;
    const double r10 = sc*cb, r11 = sc*sb*sa + cc*ca, r12 = sc*sb*ca - cc*sa;
    const double r20 = -sb,   r21 = cb*sa,            r22 = cb*ca;
    auto xform_axis = [&](double x, double y, double z) {
        easy3d::vec3 r((float)(r00*x + r01*y + r02*z),
                       (float)(r10*x + r11*y + r12*z),
                       (float)(r20*x + r21*y + r22*z));
        return easy3d::vec3(origin_world.x + (float)tx + r.x,
                            origin_world.y + (float)ty + r.y,
                            origin_world.z + (float)tz + r.z);
    };
    const easy3d::vec3 origin_t = easy3d::vec3(
        origin_world.x + (float)tx,
        origin_world.y + (float)ty,
        origin_world.z + (float)tz);
    const easy3d::vec3 x_tip = xform_axis(axis_length, 0, 0);
    const easy3d::vec3 y_tip = xform_axis(0, axis_length, 0);
    const easy3d::vec3 z_tip = xform_axis(0, 0, axis_length);

    auto* saved_current = viewer_.current_model();
    state.frame_graph = new easy3d::Graph;
    state.frame_graph->set_name("arap_frame");
    auto vcol = state.frame_graph->vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(1, 1, 1));
    auto v0 = state.frame_graph->add_vertex(origin_t);
    auto vx = state.frame_graph->add_vertex(x_tip);
    auto vy = state.frame_graph->add_vertex(y_tip);
    auto vz = state.frame_graph->add_vertex(z_tip);
    // Colors: origin = white, tips = R/G/B. Line color taken from start
    // endpoint via property coloring so each axis reads cleanly.
    vcol[v0] = easy3d::vec3(0.95f, 0.95f, 0.95f);
    vcol[vx] = easy3d::vec3(1.0f, 0.25f, 0.25f);
    vcol[vy] = easy3d::vec3(0.25f, 1.0f, 0.25f);
    vcol[vz] = easy3d::vec3(0.25f, 0.5f, 1.0f);
    // Edges from origin to each tip (Graph::add_edge expects vertex
    // descriptors; order doesn't matter for an undirected graph).
    state.frame_graph->add_edge(v0, vx);
    state.frame_graph->add_edge(v0, vy);
    state.frame_graph->add_edge(v0, vz);

    viewer_.add_model(state.frame_graph);
    viewer_.register_model_tree_node(state.frame_graph,
        ModelTreeNodeInfo{"_overlays", "arap_frame",
                          nullptr, ModelTreeNodeKind::Overlay, false});
    if (auto* ed = state.frame_graph->renderer()->get_lines_drawable(
            "edges", false)) {
        // Cylinder impostor so axis thickness is consistent on any GPU.
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(3.5f);
        ed->set_property_coloring(easy3d::State::VERTEX, "v:color");
        ed->set_visible(true);
        ed->update();
    }
    if (auto* vd = state.frame_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(7.0f);
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_visible(true);
        vd->update();
    }
    state.frame_graph->renderer()->update();
    if (saved_current)
        viewer_.set_current_model_silent(saved_current);
    viewer_.mark_dirty();
}

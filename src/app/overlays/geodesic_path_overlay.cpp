// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Geodesic path polyline overlays.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/renderer.h>

#include <string>
#include <vector>

namespace {
easy3d::Graph* build_path_graph(const std::vector<float>& xyz_flat,
                                const std::string& name)
{
    const std::size_t n = xyz_flat.size() / 3;
    if (n < 2) return nullptr;
    auto* g = new easy3d::Graph;
    g->set_name(name);
    std::vector<easy3d::Graph::Vertex> vs;
    vs.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        vs.push_back(g->add_vertex(
            easy3d::vec3(xyz_flat[i*3 + 0],
                         xyz_flat[i*3 + 1],
                         xyz_flat[i*3 + 2])));
    }
    for (std::size_t i = 1; i < n; ++i)
        g->add_edge(vs[i - 1], vs[i]);
    return g;
}
} // namespace

void MainWindow::update_geo_front_path_overlay(easy3d::SurfaceMesh* source,
    const std::vector<float>& xyz_flat)
{
    if (interaction_overlay_.geodesic_front_path &&
        !model_is_live(viewer_, interaction_overlay_.geodesic_front_path))
        interaction_overlay_.geodesic_front_path = nullptr;
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_front_path);
    auto* g = build_path_graph(xyz_flat, "geo_front_path");
    if (!g) { viewer_.mark_dirty(); return; }
    interaction_overlay_.geodesic_front_path = g;
    viewer_.add_model(interaction_overlay_.geodesic_front_path);
    viewer_.register_model_tree_node(interaction_overlay_.geodesic_front_path,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          "geo_front_path", source,
                          ModelTreeNodeKind::Annotation, true});
    if (source) {
        interaction_overlay_.geodesic_front_path->renderer()->set_visible(
            source->renderer()->is_visible());
        viewer_.set_current_model_silent(source);
    }
    if (auto* ld = interaction_overlay_.geodesic_front_path->renderer()
                       ->get_lines_drawable("edges", false)) {
        ld->set_uniform_coloring(easy3d::vec4(0.1f, 0.85f, 0.95f, 1.0f));
        ld->set_line_width(3.0f);
        ld->update();
    }
    if (auto* vd = interaction_overlay_.geodesic_front_path->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_visible(false);
    }
    viewer_.mark_dirty();
}

void MainWindow::update_geo_exact_path_overlay(easy3d::SurfaceMesh* source,
    const std::vector<float>& xyz_flat)
{
    if (interaction_overlay_.geodesic_exact_path &&
        !model_is_live(viewer_, interaction_overlay_.geodesic_exact_path))
        interaction_overlay_.geodesic_exact_path = nullptr;
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_exact_path);
    auto* g = build_path_graph(xyz_flat, "geo_exact_path");
    if (!g) { viewer_.mark_dirty(); return; }
    interaction_overlay_.geodesic_exact_path = g;
    viewer_.add_model(interaction_overlay_.geodesic_exact_path);
    viewer_.register_model_tree_node(interaction_overlay_.geodesic_exact_path,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          "geo_exact_path", source,
                          ModelTreeNodeKind::Annotation, true});
    if (source) {
        interaction_overlay_.geodesic_exact_path->renderer()->set_visible(
            source->renderer()->is_visible());
        viewer_.set_current_model_silent(source);
    }
    if (auto* ld = interaction_overlay_.geodesic_exact_path->renderer()
                       ->get_lines_drawable("edges", false)) {
        ld->set_uniform_coloring(easy3d::vec4(1.0f, 0.25f, 0.85f, 1.0f));
        ld->set_line_width(3.0f);
        ld->update();
    }
    if (auto* vd = interaction_overlay_.geodesic_exact_path->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_visible(false);
    }
    viewer_.mark_dirty();
}

void MainWindow::clear_geo_path_overlays() {
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_front_path);
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_exact_path);
    viewer_.mark_dirty();
}

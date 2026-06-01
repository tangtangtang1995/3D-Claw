// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Measurement overlay rendering helpers for MainWindow.
// Keeps finalized measurement groups and the current in-progress group as
// a child Graph under the measured model.

#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "dialogs/measurement_dialog.h"
#include "overlays/overlay_utils.h"

#include <easy3d/core/graph.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>

#include <vector>


void MainWindow::update_measurement_overlay(const MeasurementState& s) {
    easy3d::Model* source = nullptr;
    if (interaction_overlay_.measurement &&
        model_is_live(viewer_, interaction_overlay_.measurement)) {
        if (auto* info = viewer_.model_tree_info(interaction_overlay_.measurement))
            source = info->parent;
    }
    if (!source) {
        source = viewer_.current_model();
        if (source == interaction_overlay_.measurement)
            source = nullptr;
        if (auto* info = viewer_.model_tree_info(source)) {
            if (info->kind == ModelTreeNodeKind::Annotation && info->parent)
                source = info->parent;
        }
    }
    clear_measurement_overlay();
    if (s.history.empty() && s.points.empty()) return;

    interaction_overlay_.measurement = new easy3d::Graph;
    interaction_overlay_.measurement->set_name("measurement");

    // Render every finalized group as its own polyline, then the current
    // in-progress group on top. Groups are not interconnected; each group's
    // edges chain its points in pick order (so a 2-point Distance is just
    // one segment, a 3-point Angle is two segments at the apex).
    auto add_group = [&](const std::vector<MeasurePoint>& pts) {
        if (pts.empty()) return;
        std::vector<easy3d::Graph::Vertex> vs;
        vs.reserve(pts.size());
        for (auto& p : pts)
            vs.push_back(interaction_overlay_.measurement->add_vertex(p.pos));
        for (size_t i = 1; i < vs.size(); ++i)
            interaction_overlay_.measurement->add_edge(vs[i - 1], vs[i]);
    };
    for (const auto& r : s.history) add_group(r.points);
    add_group(s.points);

    viewer_.add_model(interaction_overlay_.measurement);
    viewer_.register_model_tree_node(interaction_overlay_.measurement,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          "measurement", source,
                          ModelTreeNodeKind::Annotation, true});
    if (source) {
        interaction_overlay_.measurement->renderer()->set_visible(
            source->renderer()->is_visible());
        viewer_.set_current_model_silent(source);
    }

    if (auto* vd =
            interaction_overlay_.measurement->renderer()->get_points_drawable("vertices")) {
        vd->set_uniform_coloring(easy3d::vec4(0.2f, 0.8f, 1.0f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(8.0f);
        vd->update();
    }
    if (auto* ed =
            interaction_overlay_.measurement->renderer()->get_lines_drawable("edges")) {
        ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.85f, 0.2f, 1.0f));
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(2.5f);
        ed->update();
    }
    interaction_overlay_.measurement->renderer()->update();
    viewer_.mark_dirty();
}


void MainWindow::clear_measurement_overlay() {
    delete_model_if_live(viewer_, interaction_overlay_.measurement);
}

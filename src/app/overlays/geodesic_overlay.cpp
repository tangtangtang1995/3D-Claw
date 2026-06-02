// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Geodesic source/target, front propagation, and path overlays.

#include "overlays/overlay_controller.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>

#include <vector>

// ============================================================================
// Geodesic source/target picking overlays.
// Sources: magenta sphere points (Graph).
// Target : lime green sphere point   (Graph).
// ============================================================================

void OverlayController::update_geo_source_overlay(easy3d::SurfaceMesh* source,
    const std::vector<easy3d::vec3>& points)
{
    if (interaction_overlay_.geodesic_sources &&
        !model_is_live(viewer_, interaction_overlay_.geodesic_sources))
        interaction_overlay_.geodesic_sources = nullptr;
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_sources);
    if (points.empty()) {
        viewer_.mark_dirty();
        return;
    }
    interaction_overlay_.geodesic_sources = new easy3d::Graph;
    interaction_overlay_.geodesic_sources->set_name("geo_sources");
    for (const auto& p : points)
        interaction_overlay_.geodesic_sources->add_vertex(p);
    viewer_.add_model(interaction_overlay_.geodesic_sources);
    viewer_.register_model_tree_node(interaction_overlay_.geodesic_sources,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          "geo_sources", source,
                          ModelTreeNodeKind::Annotation, true});
    if (source) {
        interaction_overlay_.geodesic_sources->renderer()->set_visible(
            source->renderer()->is_visible());
        viewer_.set_current_model_silent(source);
    }
    if (auto* vd = interaction_overlay_.geodesic_sources->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.3f, 0.85f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(10.0f);
        vd->update();
    }
    viewer_.mark_dirty();
}

void OverlayController::update_geo_target_overlay(easy3d::SurfaceMesh* source,
                                                const easy3d::vec3* p) {
    if (interaction_overlay_.geodesic_target &&
        !model_is_live(viewer_, interaction_overlay_.geodesic_target))
        interaction_overlay_.geodesic_target = nullptr;
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_target);
    if (!p) {
        viewer_.mark_dirty();
        return;
    }
    interaction_overlay_.geodesic_target = new easy3d::Graph;
    interaction_overlay_.geodesic_target->set_name("geo_target");
    interaction_overlay_.geodesic_target->add_vertex(*p);
    viewer_.add_model(interaction_overlay_.geodesic_target);
    viewer_.register_model_tree_node(interaction_overlay_.geodesic_target,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          "geo_target", source,
                          ModelTreeNodeKind::Annotation, true});
    if (source) {
        interaction_overlay_.geodesic_target->renderer()->set_visible(
            source->renderer()->is_visible());
        viewer_.set_current_model_silent(source);
    }
    if (auto* vd = interaction_overlay_.geodesic_target->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(0.2f, 1.0f, 0.4f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(12.0f);
        vd->update();
    }
    viewer_.mark_dirty();
}

void OverlayController::clear_geo_overlay() {
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_sources);
    delete_model_if_live(viewer_, interaction_overlay_.geodesic_target);
    viewer_.mark_dirty();
}

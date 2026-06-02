// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Region Growing live overlay.

#include "overlays/overlay_controller.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <vector>

void OverlayController::init_rg_overlay(easy3d::PointCloud* src) {
    clear_rg_overlays();
    if (!src || src->n_vertices() == 0) return;

    algorithm_overlay_.region_growing_source = src;
    auto* vd = src->renderer()->get_points_drawable("vertices");
    algorithm_overlay_.region_growing_saved_coloring_method =
        vd ? (int)vd->coloring_method() : 0;

    auto colors = src->vertex_property<easy3d::vec3>("v:color");
    if (!colors) {
        colors = src->add_vertex_property<easy3d::vec3>("v:color");
    }
    easy3d::vec3 gray(0.12f, 0.12f, 0.12f);
    for (auto v : src->vertices()) colors[v] = gray;

    if (vd) {
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_point_size(4.0f);
        vd->update();
    }
    src->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::update_rg_overlay(std::vector<RGColorCmd>& cmds) {
    if (!algorithm_overlay_.region_growing_source || cmds.empty()) return;
    auto colors = algorithm_overlay_.region_growing_source
        ->get_vertex_property<easy3d::vec3>("v:color");
    if (!colors) return;

    const easy3d::vec3 bg_gray(0.12f, 0.12f, 0.12f);
    const int n = (int)algorithm_overlay_.region_growing_source->n_vertices();
    for (auto& cmd : cmds) {
        if (cmd.idx < 0 || cmd.idx >= n) continue;
        colors[typename easy3d::PointCloud::Vertex(cmd.idx)] =
            (cmd.region_id < 0) ? bg_gray : ransac_shape_color(cmd.region_id);
    }
    algorithm_overlay_.region_growing_source->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::clear_rg_overlays() {
    if (!algorithm_overlay_.region_growing_source) return;
    auto* vd = algorithm_overlay_.region_growing_source->renderer()
        ->get_points_drawable("vertices");
    if (vd) {
        vd->set_coloring_method(
            static_cast<easy3d::State::Method>(
                algorithm_overlay_.region_growing_saved_coloring_method));
        vd->update();
    }
    algorithm_overlay_.region_growing_source->renderer()->update();
    algorithm_overlay_.region_growing_source = nullptr;
    viewer_.mark_dirty();
}

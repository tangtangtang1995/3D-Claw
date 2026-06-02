// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Crop overlay rendering helpers for MainWindow.
// Draws the interactive box/plane gizmo and persists accepted crop artifacts.

#include "overlays/overlay_controller.h"
#include "viewport/gizmo_constants.h"
#include "viewport/viewport_canvas.h"
#include "dialogs/crop_dialog.h"
#include "overlays/overlay_utils.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>


void OverlayController::update_crop_overlay(const CropState& s) {
    if (s.mode == CropMode::Selection) {
        clear_crop_overlay();
        return;
    }
    if (interaction_overlay_.crop_gizmo &&
        !model_is_live(viewer_, interaction_overlay_.crop_gizmo))
        interaction_overlay_.crop_gizmo = nullptr;

    if (!interaction_overlay_.crop_gizmo) {
        easy3d::Model* source = viewer_.current_model();
        interaction_overlay_.crop_gizmo = new easy3d::Graph;
        interaction_overlay_.crop_gizmo->set_name("crop_gizmo");
        viewer_.add_model(interaction_overlay_.crop_gizmo);
        viewer_.register_model_tree_overlay(
            interaction_overlay_.crop_gizmo, source, "crop_gizmo");
        viewer_.set_current_model_silent(source);
    }

    interaction_overlay_.crop_gizmo->clear();
    auto vcolor = interaction_overlay_.crop_gizmo->vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(0.15f, 0.85f, 0.25f));
    auto add_v = [&](const easy3d::vec3& p, const easy3d::vec3& color) {
        auto v = interaction_overlay_.crop_gizmo->add_vertex(p);
        vcolor[v] = color;
        return v;
    };
    auto add_edge = [&](const easy3d::vec3& a, const easy3d::vec3& b,
                        const easy3d::vec3& color) {
        interaction_overlay_.crop_gizmo->add_edge(add_v(a, color), add_v(b, color));
    };

    if (s.mode == CropMode::Box) {
        easy3d::vec3 basis[3];
        s.box_basis(basis);
        easy3d::vec3 c(s.box_center[0], s.box_center[1], s.box_center[2]);
        easy3d::vec3 corners[8];
        s.box_corners(corners);
        const easy3d::vec3 box_col(0.15f, 0.85f, 0.25f);
        const easy3d::vec3 axis_col[3] = {
            easy3d::vec3(1.0f, 0.22f, 0.18f),
            easy3d::vec3(0.20f, 0.95f, 0.25f),
            easy3d::vec3(0.25f, 0.48f, 1.0f)
        };

        auto corner = [&](int x, int y, int z) -> easy3d::vec3 {
            return corners[(x * 4) + (y * 2) + z];
        };
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                add_edge(corner(0, y, z), corner(1, y, z), box_col);
        for (int x = 0; x < 2; ++x)
            for (int z = 0; z < 2; ++z)
                add_edge(corner(x, 0, z), corner(x, 1, z), box_col);
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                add_edge(corner(x, y, 0), corner(x, y, 1), box_col);

        const float diag = easy3d::length(easy3d::vec3(
            s.box_half[0] * 2.0f, s.box_half[1] * 2.0f, s.box_half[2] * 2.0f));
        const float arrow_len = std::max(claw3d::viewport_gizmo::kMinArrowLength, diag * claw3d::viewport_gizmo::kCropArrowLengthRatio);
        const float head_r = arrow_len * 0.18f;
        auto add_arrow = [&](int dim, int sign) {
            const easy3d::vec3 dir = basis[dim] * (float)sign;
            const easy3d::vec3 face = c + dir * s.box_half[dim];
            const easy3d::vec3 tip = face + dir * arrow_len;
            const int uidx = (dim + 1) % 3;
            const int vidx = (dim + 2) % 3;
            const easy3d::vec3 base = tip - dir * (arrow_len * 0.28f);
            add_edge(face, tip, axis_col[dim]);
            add_edge(tip, base + basis[uidx] * head_r, axis_col[dim]);
            add_edge(tip, base - basis[uidx] * head_r, axis_col[dim]);
            add_edge(tip, base + basis[vidx] * head_r, axis_col[dim]);
            add_edge(tip, base - basis[vidx] * head_r, axis_col[dim]);
            add_v(tip, axis_col[dim]);
        };
        for (int dim = 0; dim < 3; ++dim) {
            add_arrow(dim, 1);
            add_arrow(dim, -1);
        }

        const int segs = 64;
        for (int dim = 0; dim < 3; ++dim) {
            const int uidx = (dim + 1) % 3;
            const int vidx = (dim + 2) % 3;
            const float radius =
                std::max(s.box_half[uidx], s.box_half[vidx]) +
                arrow_len * 0.55f;
            easy3d::vec3 prev = c + basis[uidx] * radius;
            for (int i = 1; i <= segs; ++i) {
                const float a = (float)i * 2.0f *
                    3.14159265358979323846f / (float)segs;
                const easy3d::vec3 p =
                    c + basis[uidx] * (std::cos(a) * radius) +
                    basis[vidx] * (std::sin(a) * radius);
                add_edge(prev, p, axis_col[dim]);
                prev = p;
            }
        }
    } else {
        float bmin[3], bmax[3];
        s.box_min(bmin); s.box_max(bmax);
        easy3d::vec3 n(s.plane_normal[0], s.plane_normal[1], s.plane_normal[2]);
        float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
        if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
        easy3d::vec3 pc = n * (-s.plane_offset);
        easy3d::vec3 u, v;
        if (std::abs(n.z) < 0.9f)
            u = easy3d::cross(n, easy3d::vec3(0, 0, 1));
        else
            u = easy3d::cross(n, easy3d::vec3(1, 0, 0));
        u = easy3d::normalize(u);
        v = easy3d::cross(n, u);
        float diag = easy3d::length(easy3d::vec3(
            bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2]));
        float r = diag * 0.7f;
        const easy3d::vec3 col(0.15f, 0.85f, 0.25f);
        const easy3d::vec3 p0 = pc + u * r + v * r;
        const easy3d::vec3 p1 = pc + u * r - v * r;
        const easy3d::vec3 p2 = pc - u * r - v * r;
        const easy3d::vec3 p3 = pc - u * r + v * r;
        add_edge(p0, p1, col); add_edge(p1, p2, col);
        add_edge(p2, p3, col); add_edge(p3, p0, col);
        add_edge(pc, pc + n * diag * 0.5f, col);
    }

    if (auto* ed = interaction_overlay_.crop_gizmo->renderer()
                       ->get_lines_drawable("edges", false)) {
        ed->set_property_coloring(easy3d::State::VERTEX, "v:color");
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(2.6f);
        ed->set_visible(true);
        ed->update();
    }
    if (auto* vd = interaction_overlay_.crop_gizmo->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(7.0f);
        vd->set_visible(false);
        vd->update();
    }
    interaction_overlay_.crop_gizmo->renderer()->update();
    viewer_.mark_dirty();
}


void OverlayController::clear_crop_overlay() {
    delete_model_if_live(viewer_, interaction_overlay_.crop_gizmo);
}


void OverlayController::save_crop_artifact(const CropState& s,
                                         easy3d::Model* source,
                                         const std::string& suffix) {
    if (!source || !interaction_overlay_.crop_gizmo || s.mode == CropMode::Selection)
        return;
    if (!model_is_live(viewer_, interaction_overlay_.crop_gizmo) ||
        !model_is_live(viewer_, source))
        return;

    auto* artifact = new easy3d::Graph(*interaction_overlay_.crop_gizmo);
    const int id = interaction_overlay_.crop_artifact_serial++;
    std::ostringstream name;
    name << (s.mode == CropMode::Box ? "crop_box_" : "clip_plane_")
         << id;
    artifact->set_name(name.str());

    auto* saved_current = viewer_.current_model();
    viewer_.add_model(artifact);
    viewer_.register_model_tree_node(artifact,
        ModelTreeNodeInfo{viewer_.model_tree_workspace_name(source),
                          name.str(), source,
                          ModelTreeNodeKind::Annotation, true});
    artifact->renderer()->set_visible(source->renderer()->is_visible());
    if (auto* ed = artifact->renderer()->get_lines_drawable("edges", false)) {
        ed->set_property_coloring(easy3d::State::VERTEX, "v:color");
        ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        ed->set_line_width(2.4f);
        ed->set_visible(true);
        ed->update();
    }
    if (auto* vd = artifact->renderer()->get_points_drawable("vertices", false)) {
        vd->set_property_coloring(easy3d::State::VERTEX, "v:color");
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(6.0f);
        vd->set_visible(false);
        vd->update();
    }
    artifact->renderer()->update();
    viewer_.set_current_model_silent(
        model_is_live(viewer_, saved_current) ? saved_current : source);
    viewer_.mark_dirty();
    LOG(INFO) << "saved crop artifact: " << name.str()
              << " (" << suffix << ")";
}

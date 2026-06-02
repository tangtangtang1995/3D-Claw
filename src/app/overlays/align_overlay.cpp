// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Align / Transform overlay helpers for MainWindow.
// Draws the transform gizmo and applies/restores live transform preview.

#include "overlays/overlay_controller.h"
#include "viewport/gizmo_constants.h"
#include "viewport/viewport_canvas.h"
#include "dialogs/align_dialog.h"
#include "overlays/overlay_utils.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>

#include <algorithm>
#include <cmath>


void OverlayController::clear_align_gizmo() {
    delete_model_if_live(viewer_, interaction_overlay_.align_gizmo);
}


void OverlayController::update_align_gizmo(const AlignState& s) {
    auto* target = viewer_.current_model();
    if (!target || s.tab != AlignTab::Transform || !s.transform_show_gizmo) {
        clear_align_gizmo();
        return;
    }
    const auto& bb = target->bounding_box();
    if (!bb.is_valid()) {
        clear_align_gizmo();
        return;
    }
    if (interaction_overlay_.align_gizmo &&
        !model_is_live(viewer_, interaction_overlay_.align_gizmo))
        interaction_overlay_.align_gizmo = nullptr;

    if (!interaction_overlay_.align_gizmo) {
        interaction_overlay_.align_gizmo = new easy3d::Graph;
        interaction_overlay_.align_gizmo->set_name("align_gizmo");
        viewer_.add_model(interaction_overlay_.align_gizmo);
        viewer_.register_model_tree_overlay(
            interaction_overlay_.align_gizmo, target, "align_gizmo");
        viewer_.set_current_model_silent(target);
    }
    interaction_overlay_.align_gizmo->clear();
    auto vcolor = interaction_overlay_.align_gizmo->vertex_property<easy3d::vec3>(
        "v:color", easy3d::vec3(0.85f, 0.85f, 0.85f));
    auto add_v = [&](const easy3d::vec3& p, const easy3d::vec3& col) {
        auto v = interaction_overlay_.align_gizmo->add_vertex(p);
        vcolor[v] = col;
        return v;
    };
    auto add_edge = [&](const easy3d::vec3& a, const easy3d::vec3& b,
                        const easy3d::vec3& col) {
        interaction_overlay_.align_gizmo->add_edge(add_v(a, col), add_v(b, col));
    };

    const easy3d::vec3 bb_extent = bb.max_point() - bb.min_point();
    const float diag = easy3d::length(bb_extent);
    const float arrow_len = std::max(claw3d::viewport_gizmo::kMinArrowLength, diag * claw3d::viewport_gizmo::kTransformArrowLengthRatio);
    const float ring_radius = diag * 0.45f;
    const easy3d::vec3 axis_col[3] = {
        easy3d::vec3(1.0f, 0.22f, 0.18f),
        easy3d::vec3(0.20f, 0.95f, 0.25f),
        easy3d::vec3(0.25f, 0.48f, 1.0f),
    };
    const easy3d::vec3 basis[3] = {
        easy3d::vec3(1, 0, 0),
        easy3d::vec3(0, 1, 0),
        easy3d::vec3(0, 0, 1),
    };

    // Vertices are directly transformed by apply_transform_preview each frame,
    // so bb.center() already reflects the current tx/ty/tz. Do not add tx/ty/tz
    // again; that would double-offset the gizmo from the model.
    const easy3d::vec3 c = bb.center();

    auto add_arrow = [&](int dim, int sign) {
        const easy3d::vec3 dir = basis[dim] * static_cast<float>(sign);
        const easy3d::vec3 tip = c + dir * arrow_len;
        add_edge(c, tip, axis_col[dim]);
        const int uidx = (dim + 1) % 3;
        const int vidx = (dim + 2) % 3;
        const easy3d::vec3 base = tip - dir * (arrow_len * 0.20f);
        const float head_r = arrow_len * 0.10f;
        add_edge(tip, base + basis[uidx] * head_r, axis_col[dim]);
        add_edge(tip, base - basis[uidx] * head_r, axis_col[dim]);
        add_edge(tip, base + basis[vidx] * head_r, axis_col[dim]);
        add_edge(tip, base - basis[vidx] * head_r, axis_col[dim]);
    };
    for (int dim = 0; dim < 3; ++dim) {
        add_arrow(dim, 1);
        add_arrow(dim, -1);
    }

    constexpr float kPi = 3.14159265358979323846f;
    const int segs = 64;
    for (int dim = 0; dim < 3; ++dim) {
        const int uidx = (dim + 1) % 3;
        const int vidx = (dim + 2) % 3;
        easy3d::vec3 prev = c + basis[uidx] * ring_radius;
        for (int i = 1; i <= segs; ++i) {
            const float a = static_cast<float>(i) * 2.0f * kPi /
                static_cast<float>(segs);
            const easy3d::vec3 p =
                c + basis[uidx] * (std::cos(a) * ring_radius) +
                basis[vidx] * (std::sin(a) * ring_radius);
            add_edge(prev, p, axis_col[dim]);
            prev = p;
        }
    }

    if (auto* vd = interaction_overlay_.align_gizmo->renderer()
                       ->get_points_drawable("vertices", false)) {
        vd->set_visible(false);
        vd->update();
    }
    interaction_overlay_.align_gizmo->renderer()->update();
    viewer_.mark_dirty();
}


void OverlayController::apply_transform_preview(easy3d::Model* m, AlignState& s) {
    if (!m) return;

    auto& pts = m->points();
    if (pts.empty()) return;

    if (s.preview_model != m) {
        s.preview_original_pts.assign(pts.begin(), pts.end());
        s.preview_model = m;
        s.preview_applied_rev = 0;
    }

    std::copy(s.preview_original_pts.begin(), s.preview_original_pts.end(), pts.begin());

    const bool identity =
        s.tx == 0 && s.ty == 0 && s.tz == 0 &&
        s.rx == 0 && s.ry == 0 && s.rz == 0 &&
        s.sx == 1 && s.sy == 1 && s.sz == 1;
    if (identity) {
        m->invalidate_bounding_box();
        if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
            sm->update_vertex_normals();
        m->renderer()->update();
        viewer_.set_selection_bbox_suppressed(false);
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float dr = kPi / 180.0f;
    const float rx = s.rx * dr, ry = s.ry * dr, rz = s.rz * dr;
    const float cx = std::cos(rx), sxr = std::sin(rx);
    const float cy = std::cos(ry), syr = std::sin(ry);
    const float cz = std::cos(rz), szr = std::sin(rz);
    easy3d::mat3 R(cz*cy, cz*syr*sxr-szr*cx, cz*syr*cx+szr*sxr,
                   szr*cy, szr*syr*sxr+cz*cx, szr*syr*cx-cz*sxr,
                   -syr, cy*sxr, cy*cx);
    for (int j = 0; j < 3; ++j) { R(j,0)*=s.sx; R(j,1)*=s.sy; R(j,2)*=s.sz; }
    for (auto& p : pts) {
        easy3d::vec3 q(R(0,0)*p.x+R(0,1)*p.y+R(0,2)*p.z + s.tx,
                       R(1,0)*p.x+R(1,1)*p.y+R(1,2)*p.z + s.ty,
                       R(2,0)*p.x+R(2,1)*p.y+R(2,2)*p.z + s.tz);
        p = q;
    }
    m->invalidate_bounding_box();
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
        sm->update_vertex_normals();
    m->renderer()->update();
    viewer_.set_selection_bbox_suppressed(true);
    ++s.preview_applied_rev;
}


void OverlayController::reset_transform_preview(easy3d::Model* m, AlignState& s) {
    if (!m || s.preview_model != m) return;
    auto& pts = m->points();
    if (pts.size() == s.preview_original_pts.size())
        std::copy(s.preview_original_pts.begin(), s.preview_original_pts.end(), pts.begin());
    m->invalidate_bounding_box();
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
        sm->update_vertex_normals();
    m->renderer()->update();
    viewer_.set_selection_bbox_suppressed(false);
    s.preview_model = nullptr;
    s.preview_original_pts.clear();
    s.preview_applied_rev = 0;
}

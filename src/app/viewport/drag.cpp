// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"
#include "viewport/gizmo_constants.h"

#include <easy3d/gui/picker_model.h>
#include <easy3d/renderer/camera.h>

#include "dialogs/align_dialog.h"
#include "dialogs/crop_dialog.h"
#include "viewport/input_math.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"

using claw_viewport_input::ray_axis_param;
using claw_viewport_input::sqr_len2;


bool ViewportCanvas::update_crop_gizmo_drag(int x, int y, float mouse_delta_x) {
    if (!crop_state_ || !crop_state_->gizmo_dragging || pressed_button_ != 1)
        return false;

    easy3d::Picker picker(camera_);
    auto line2 = picker.picking_line(x, y);
    easy3d::vec3 ro = line2.point();
    easy3d::vec3 rd = line2.direction();
    const int h = crop_state_->gizmo_axis;
    easy3d::vec3 c0(crop_state_->gizmo_start_center[0],
                    crop_state_->gizmo_start_center[1],
                    crop_state_->gizmo_start_center[2]);
    easy3d::vec3 basis[3];
    CropState start_box = *crop_state_;
    for (int i = 0; i < 3; ++i) {
        start_box.box_center[i] = crop_state_->gizmo_start_center[i];
        start_box.box_half[i] = crop_state_->gizmo_start_half[i];
        start_box.box_rotation_deg[i] = crop_state_->gizmo_start_rotation_deg[i];
    }
    start_box.box_basis(basis);

    if (h >= 0 && h < 6) {
        const int dim = h / 2;
        const int sign = (h % 2 == 0) ? 1 : -1;
        const easy3d::vec3 axis = basis[dim] * static_cast<float>(sign);
        const float now_t = ray_axis_param(ro, rd, c0, axis);
        const float delta = now_t - crop_state_->gizmo_start_axis_param;
        float new_bound = crop_state_->gizmo_start_bound +
            static_cast<float>(sign) * delta;
        const float opposite = crop_state_->gizmo_start_opposite;
        const float diag = easy3d::length(easy3d::vec3(
            crop_state_->gizmo_start_half[0] * 2.0f,
            crop_state_->gizmo_start_half[1] * 2.0f,
            crop_state_->gizmo_start_half[2] * 2.0f));
        const float min_width = std::max(
            claw3d::viewport_gizmo::kMinDragLineWidth,
            diag * claw3d::viewport_gizmo::kDragLineWidthRatio);
        if (sign > 0)
            new_bound = std::max(new_bound, opposite + min_width);
        else
            new_bound = std::min(new_bound, opposite - min_width);

        const float width = sign > 0 ? new_bound - opposite : opposite - new_bound;
        const float local_center = 0.5f * (new_bound + opposite);
        for (int i = 0; i < 3; ++i) {
            crop_state_->box_center[i] = crop_state_->gizmo_start_center[i];
            crop_state_->box_half[i] = crop_state_->gizmo_start_half[i];
        }
        crop_state_->box_half[dim] = std::max(width * 0.5f, min_width * 0.5f);
        const easy3d::vec3 moved_center = c0 + basis[dim] * local_center;
        crop_state_->box_center[0] = moved_center.x;
        crop_state_->box_center[1] = moved_center.y;
        crop_state_->box_center[2] = moved_center.z;
    } else if (h >= 6 && h < 9) {
        const int dim = h - 6;
        const easy3d::vec3 center_screen = camera_->projectedCoordinatesOf(c0);
        const ImVec2 c2(center_screen.x, center_screen.y);
        const ImVec2 v0(crop_state_->gizmo_start_mouse[0] - c2.x,
                        crop_state_->gizmo_start_mouse[1] - c2.y);
        const ImVec2 v1(static_cast<float>(x) - c2.x,
                        static_cast<float>(y) - c2.y);
        float delta_deg = 0.0f;
        if (sqr_len2(v0) > 16.0f && sqr_len2(v1) > 16.0f) {
            const float cross2 = v0.x * v1.y - v0.y * v1.x;
            const float dot2 = v0.x * v1.x + v0.y * v1.y;
            delta_deg = std::atan2(cross2, dot2) * 180.0f / 3.14159265358979323846f;
            if (easy3d::dot(basis[dim], camera_->viewDirection()) > 0.0f)
                delta_deg = -delta_deg;
        } else {
            delta_deg = mouse_delta_x * 0.5f;
        }
        for (int i = 0; i < 3; ++i) {
            crop_state_->box_center[i] = crop_state_->gizmo_start_center[i];
            crop_state_->box_half[i] = crop_state_->gizmo_start_half[i];
            crop_state_->box_rotation_deg[i] = crop_state_->gizmo_start_rotation_deg[i];
        }
        crop_state_->box_rotation_deg[dim] =
            crop_state_->gizmo_start_rotation_deg[dim] + delta_deg;
    }
    crop_state_->gizmo_dirty = true;
    dirty_ = true;
    return true;
}


bool ViewportCanvas::update_align_gizmo_drag(int x, int y, float mouse_delta_x) {
    if (!align_state_ || !align_state_->gizmo_dragging || pressed_button_ != 1)
        return false;

    easy3d::Picker picker(camera_);
    auto line2 = picker.picking_line(x, y);
    easy3d::vec3 ro = line2.point();
    easy3d::vec3 rd = line2.direction();
    const easy3d::vec3 c0 = align_state_->gizmo_start_center;
    const easy3d::vec3 basis[3] = {
        easy3d::vec3(1, 0, 0),
        easy3d::vec3(0, 1, 0),
        easy3d::vec3(0, 0, 1)
    };
    const int h = align_state_->gizmo_axis;
    if (h >= 0 && h < 6) {
        const int dim = h / 2;
        const int sign = (h % 2 == 0) ? 1 : -1;
        const easy3d::vec3 axis = basis[dim] * static_cast<float>(sign);
        const float now_t = ray_axis_param(ro, rd, c0, axis);
        const float delta = (now_t - align_state_->gizmo_start_axis_param) *
            static_cast<float>(sign);
        float* t = (dim == 0) ? &align_state_->tx
                 : (dim == 1) ? &align_state_->ty
                              : &align_state_->tz;
        *t = align_state_->gizmo_start_t[dim] + delta;
    } else if (h >= 6 && h < 9) {
        const int dim = h - 6;
        const easy3d::vec3 center_screen = camera_->projectedCoordinatesOf(c0);
        const ImVec2 c2(center_screen.x, center_screen.y);
        const ImVec2 v0(align_state_->gizmo_start_mouse[0] - c2.x,
                        align_state_->gizmo_start_mouse[1] - c2.y);
        const ImVec2 v1(static_cast<float>(x) - c2.x,
                        static_cast<float>(y) - c2.y);
        float delta_deg = 0.0f;
        if (sqr_len2(v0) > 16.0f && sqr_len2(v1) > 16.0f) {
            const float cross2 = v0.x * v1.y - v0.y * v1.x;
            const float dot2   = v0.x * v1.x + v0.y * v1.y;
            delta_deg = std::atan2(cross2, dot2) * 180.0f / 3.14159265358979323846f;
            if (easy3d::dot(basis[dim], camera_->viewDirection()) > 0.0f)
                delta_deg = -delta_deg;
        } else {
            delta_deg = mouse_delta_x * 0.5f;
        }
        float* r = (dim == 0) ? &align_state_->rx
                 : (dim == 1) ? &align_state_->ry
                              : &align_state_->rz;
        *r = align_state_->gizmo_start_r[dim] + delta_deg;
    }
    dirty_ = true;
    return true;
}

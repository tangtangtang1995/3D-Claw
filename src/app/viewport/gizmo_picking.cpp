// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"
#include "viewport/gizmo_constants.h"

#include <easy3d/renderer/opengl.h>

#include <easy3d/core/surface_mesh.h>
#include <easy3d/gui/picker_model.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/util/logging.h>

#include "dialogs/align_dialog.h"
#include "dialogs/crop_dialog.h"
#include "viewport/input_math.h"

#include <algorithm>
#include <cmath>


using claw_viewport_input::ray_axis_param;
using claw_viewport_input::ray_seg_dist;


bool ViewportCanvas::try_align_gizmo_pick(int x, int y) {
    if (!align_state_) return false;
    if (align_state_->tab != AlignTab::Transform) return false;
    auto* target = current_model();
    if (!target) return false;
    const auto& bb = target->bounding_box();
    if (!bb.is_valid()) return false;

    const int vp_w = static_cast<int>(viewport_max_x_ - viewport_min_x_);
    const int vp_h = static_cast<int>(viewport_max_y_ - viewport_min_y_);
    if (vp_w <= 0 || vp_h <= 0) return false;

    easy3d::Picker picker(camera_);
    auto line = picker.picking_line(x, y);
    easy3d::vec3 ro = line.point();
    easy3d::vec3 rd = line.direction();

    // Vertices already reflect the current transform (see apply_transform_preview),
    // so bb.center() IS the effective gizmo center.
    const easy3d::vec3 c = bb.center();
    const easy3d::vec3 basis[3] = {
        easy3d::vec3(1, 0, 0),
        easy3d::vec3(0, 1, 0),
        easy3d::vec3(0, 0, 1)
    };

    const float dist_cam = easy3d::length(ro - c);
    float pixel_world;
    if (camera_->type() == easy3d::Camera::ORTHOGRAPHIC) {
        float half_w = 1.0f, half_h = 1.0f;
        camera_->getOrthoWidthHeight(half_w, half_h);
        pixel_world = (half_h * 2.0f) / static_cast<float>(vp_h);
    } else {
        pixel_world = std::tan(camera_->fieldOfView() * 0.5f)
                    * std::max(dist_cam, 0.001f) * 2.0f /
                    static_cast<float>(vp_h);
    }
    const float threshold = pixel_world * 18.0f;

    const easy3d::vec3 bb_extent = bb.max_point() - bb.min_point();
    const float bb_diag = easy3d::length(bb_extent);
    const float arrow_len = std::max(claw3d::viewport_gizmo::kMinArrowLength, bb_diag * claw3d::viewport_gizmo::kTransformArrowLengthRatio);
    const float ring_radius = bb_diag * 0.45f;

    int best_handle = -1;
    float best_dist = threshold;

    // 6 translation arrows. Each arrow starts at c (gizmo origin) and ends
    // at c + dir * arrow_len.
    for (int dim = 0; dim < 3; ++dim) {
        for (int si = 0; si < 2; ++si) {
            const int sign = si == 0 ? 1 : -1;
            const easy3d::vec3 dir = basis[dim] * static_cast<float>(sign);
            const easy3d::vec3 tip = c + dir * arrow_len;
            float tr, ts;
            float d = ray_seg_dist(ro, rd, c, tip, tr, ts);
            if (d < best_dist) {
                best_dist = d;
                best_handle = dim * 2 + (sign > 0 ? 0 : 1);
            }
        }
    }

    // 3 rotation rings if no arrow was closer.
    if (best_handle < 0) {
        const int segs = 64;
        for (int dim = 0; dim < 3; ++dim) {
            const int uidx = (dim + 1) % 3;
            const int vidx = (dim + 2) % 3;
            easy3d::vec3 prev = c + basis[uidx] * ring_radius;
            for (int i = 1; i <= segs; ++i) {
                constexpr float kPi = 3.14159265358979323846f;
                const float a = static_cast<float>(i) * 2.0f * kPi /
                    static_cast<float>(segs);
                const easy3d::vec3 p = c + basis[uidx] * (std::cos(a) * ring_radius)
                                         + basis[vidx] * (std::sin(a) * ring_radius);
                float tr, ts;
                float d = ray_seg_dist(ro, rd, prev, p, tr, ts);
                if (d < best_dist) {
                    best_dist = d;
                    best_handle = 6 + dim;
                }
                prev = p;
            }
        }
    }

    if (best_handle >= 0) {
        align_state_->gizmo_axis = best_handle;
        align_state_->gizmo_dragging = true;
        align_state_->gizmo_start_mouse[0] = static_cast<float>(x);
        align_state_->gizmo_start_mouse[1] = static_cast<float>(y);
        align_state_->gizmo_start_t[0] = align_state_->tx;
        align_state_->gizmo_start_t[1] = align_state_->ty;
        align_state_->gizmo_start_t[2] = align_state_->tz;
        align_state_->gizmo_start_r[0] = align_state_->rx;
        align_state_->gizmo_start_r[1] = align_state_->ry;
        align_state_->gizmo_start_r[2] = align_state_->rz;
        align_state_->gizmo_start_center = c;
        align_state_->gizmo_target = target;
        if (best_handle < 6) {
            const int dim = best_handle / 2;
            const int sign = (best_handle % 2 == 0) ? 1 : -1;
            const easy3d::vec3 axis =
                basis[dim] * static_cast<float>(sign);
            align_state_->gizmo_start_axis_param =
                ray_axis_param(ro, rd, c, axis);
        }
        dirty_ = true;
        return true;
    }
    return false;
}


bool ViewportCanvas::try_crop_gizmo_pick(int x, int y) {
    if (!crop_state_ || crop_state_->mode != CropMode::Box) return false;

    const int vp_w = static_cast<int>(viewport_max_x_ - viewport_min_x_);
    const int vp_h = static_cast<int>(viewport_max_y_ - viewport_min_y_);
    if (vp_w <= 0 || vp_h <= 0) return false;

    easy3d::Picker picker(camera_);
    auto line = picker.picking_line(x, y);
    easy3d::vec3 ro = line.point();
    easy3d::vec3 rd = line.direction();

    easy3d::vec3 c(crop_state_->box_center[0], crop_state_->box_center[1], crop_state_->box_center[2]);
    easy3d::vec3 basis[3];
    crop_state_->box_basis(basis);

    float dist_cam = easy3d::length(ro - c);
    // Camera::fieldOfView() is already in radians. The previous code treated
    // it as degrees, shrinking the hit threshold so much that handles were
    // hard to click.
    float pixel_world = 1.0f;
    if (camera_->type() == easy3d::Camera::ORTHOGRAPHIC) {
        float half_w = 1.0f, half_h = 1.0f;
        camera_->getOrthoWidthHeight(half_w, half_h);
        pixel_world = (half_h * 2.0f) / static_cast<float>(vp_h);
    } else {
        pixel_world = std::tan(camera_->fieldOfView() * 0.5f)
                    * std::max(dist_cam, 0.001f) * 2.0f /
                    static_cast<float>(vp_h);
    }
    float threshold = pixel_world * 18.0f;
    const float diag = easy3d::length(easy3d::vec3(
        crop_state_->box_half[0] * 2.0f,
        crop_state_->box_half[1] * 2.0f,
        crop_state_->box_half[2] * 2.0f));
    const float arrow_len = std::max(claw3d::viewport_gizmo::kMinArrowLength, diag * claw3d::viewport_gizmo::kCropArrowLengthRatio);

    int best_handle = -1;
    float best_dist = threshold;
    for (int dim = 0; dim < 3; ++dim) {
        for (int si = 0; si < 2; ++si) {
            const int sign = si == 0 ? 1 : -1;
            const easy3d::vec3 dir = basis[dim] * static_cast<float>(sign);
            const easy3d::vec3 face = c + dir * crop_state_->box_half[dim];
            const easy3d::vec3 tip = face + dir * arrow_len;
            float tr, ts;
            float d = ray_seg_dist(ro, rd, face, tip, tr, ts);
            if (d < best_dist) {
                best_dist = d;
                best_handle = dim * 2 + (sign > 0 ? 0 : 1);
            }
        }
    }

    if (best_handle < 0) {
        const int segs = 64;
        for (int dim = 0; dim < 3; ++dim) {
            const int uidx = (dim + 1) % 3;
            const int vidx = (dim + 2) % 3;
            const float radius = std::max(crop_state_->box_half[uidx],
                                          crop_state_->box_half[vidx]) + arrow_len * 0.55f;
            easy3d::vec3 prev = c + basis[uidx] * radius;
            for (int i = 1; i <= segs; ++i) {
                constexpr float kPi = 3.14159265358979323846f;
                const float a = static_cast<float>(i) * 2.0f * kPi /
                    static_cast<float>(segs);
                const easy3d::vec3 p = c + basis[uidx] * (std::cos(a) * radius)
                                         + basis[vidx] * (std::sin(a) * radius);
                float tr, ts;
                float d = ray_seg_dist(ro, rd, prev, p, tr, ts);
                if (d < best_dist) {
                    best_dist = d;
                    best_handle = 6 + dim;
                }
                prev = p;
            }
        }
    }

    if (best_handle >= 0) {
        crop_state_->gizmo_axis = best_handle;
        crop_state_->gizmo_dragging = true;
        crop_state_->gizmo_start_mouse[0] = static_cast<float>(x);
        crop_state_->gizmo_start_mouse[1] = static_cast<float>(y);
        for (int i = 0; i < 3; ++i) {
            crop_state_->gizmo_start_center[i] = crop_state_->box_center[i];
            crop_state_->gizmo_start_half[i] = crop_state_->box_half[i];
            crop_state_->gizmo_start_rotation_deg[i] = crop_state_->box_rotation_deg[i];
        }
        if (best_handle < 6) {
            const int dim = best_handle / 2;
            const int sign = (best_handle % 2 == 0) ? 1 : -1;
            const easy3d::vec3 axis =
                basis[dim] * static_cast<float>(sign);
            crop_state_->gizmo_start_bound =
                static_cast<float>(sign) * crop_state_->box_half[dim];
            crop_state_->gizmo_start_opposite =
                -static_cast<float>(sign) * crop_state_->box_half[dim];
            crop_state_->gizmo_start_center_val = crop_state_->box_center[dim];
            crop_state_->gizmo_start_half_val = crop_state_->box_half[dim];
            crop_state_->gizmo_start_axis_param = ray_axis_param(ro, rd, c, axis);
            LOG(INFO) << "crop face handle " << best_handle << " picked";
        } else {
            LOG(INFO) << "crop rotation handle " << (best_handle - 6) << " picked";
        }
        dirty_ = true;
        return true;
    }
    return false;
}



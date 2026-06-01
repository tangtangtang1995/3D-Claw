// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/gui/picker_model.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/renderer/manipulated_camera_frame.h>
#include <easy3d/renderer/manipulated_frame.h>
#include <easy3d/util/logging.h>

#include "dialogs/align_dialog.h"
#include "dialogs/crop_dialog.h"
#include "selection/selection_manager.h"
#include "ui/walk_through.h"
#include <cmath>

#include "imgui.h"

void ViewportCanvas::handle_input() {
    if (input_locked_)
        return;
    if (!viewport_hovered_ && !viewport_focused_)
        return;

    handle_mouse_press();
    handle_mouse_release();
    handle_mouse_drag();
    handle_mouse_wheel();
    handle_keyboard_shortcuts();
    handle_walkthrough_keyframe();
}


void ViewportCanvas::handle_mouse_press() {
    ImGuiIO& io = ImGui::GetIO();
    easy3d::ManipulatedCameraFrame* frame = camera_->frame();

    for (int btn = 0; btn < 3; ++btn) {
        if (ImGui::IsMouseClicked(btn) && pressed_button_ == 0) {
            pressed_button_ = btn + 1;
            mouse_pressed_x_ = io.MousePos.x;
            mouse_pressed_y_ = io.MousePos.y;
            const int px =
                static_cast<int>(io.MousePos.x - viewport_min_x_);
            const int py =
                static_cast<int>(io.MousePos.y - viewport_min_y_);
            if (try_start_gizmo_drag_on_press(btn, px, py)) {
                dirty_ = true;
                continue;
            }
            frame->action_start();
            dirty_ = true;
        }
    }
}


bool ViewportCanvas::try_start_gizmo_drag_on_press(int button, int x, int y) {
    if (button != 0)
        return false;

    if (crop_state_ && crop_state_->mode == CropMode::Box && try_crop_gizmo_pick(x, y))
        return true;

    if (align_state_ && try_align_gizmo_pick(x, y))
        return true;

    return false;
}


void ViewportCanvas::handle_mouse_release() {
    if (pressed_button_ <= 0 ||
        !ImGui::IsMouseReleased(pressed_button_ - 1))
        return;

    ImGuiIO& io = ImGui::GetIO();
    easy3d::ManipulatedCameraFrame* frame = camera_->frame();

    int btn = pressed_button_;  // snapshot before reset
    bool is_left_click = (btn == 1)
        && (std::abs(io.MousePos.x - mouse_pressed_x_) < 3.0f)
        && (std::abs(io.MousePos.y - mouse_pressed_y_) < 3.0f);
    bool is_right_click = (btn == 2)
        && (std::abs(io.MousePos.x - mouse_pressed_x_) < 3.0f)
        && (std::abs(io.MousePos.y - mouse_pressed_y_) < 3.0f);
    frame->action_end();

    bool finished_rect = finish_rectangle_selection_on_release(btn);

    pressed_button_ = 0;
    rect_dragging_ = false;
    const bool consumed_gizmo = stop_active_gizmo_drag();
    dirty_ = true;
    if (viewport_hovered_ && !finished_rect && !consumed_gizmo) {
        const int px = static_cast<int>(io.MousePos.x - viewport_min_x_);
        const int py = static_cast<int>(io.MousePos.y - viewport_min_y_);
        dispatch_viewport_release_click(is_left_click, is_right_click, px, py);
    }
}


bool ViewportCanvas::finish_rectangle_selection_on_release(int button) {
    bool finished_left_rect = false;
    if (rect_dragging_ && button == 1) {
        rect_dragging_ = false;
        float sx = mouse_pressed_x_, sy = mouse_pressed_y_;
        float ex = rect_end_x_, ey = rect_end_y_;
        float drag_dist = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
        if (drag_dist > 5.0f) {
            const int vp_sx = static_cast<int>(sx - viewport_min_x_);
            const int vp_sy = static_cast<int>(sy - viewport_min_y_);
            const int vp_ex = static_cast<int>(ex - viewport_min_x_);
            const int vp_ey = static_cast<int>(ey - viewport_min_y_);
            easy3d::Rect rect(
                static_cast<float>(vp_sx), static_cast<float>(vp_ex),
                static_cast<float>(vp_sy), static_cast<float>(vp_ey));
            if (selection_mode_ &&
                *selection_mode_ == SelectionMode::RectangleSurfaceFace)
                try_rect_pick_faces(rect);
            else if (selection_mode_ &&
                     *selection_mode_ == SelectionMode::RectanglePointCloudPoint)
                try_rect_pick_points(rect, false);
            finished_left_rect = true;
        }
    }

    if (rect_dragging_ && button == 2) {
        rect_dragging_ = false;
        float sx = mouse_pressed_x_, sy = mouse_pressed_y_;
        float ex = rect_end_x_, ey = rect_end_y_;
        float drag_dist = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
        if (drag_dist > 5.0f && selection_mode_) {
            const int vp_sx = static_cast<int>(sx - viewport_min_x_);
            const int vp_sy = static_cast<int>(sy - viewport_min_y_);
            const int vp_ex = static_cast<int>(ex - viewport_min_x_);
            const int vp_ey = static_cast<int>(ey - viewport_min_y_);
            easy3d::Rect rect(
                static_cast<float>(vp_sx), static_cast<float>(vp_ex),
                static_cast<float>(vp_sy), static_cast<float>(vp_ey));
            if (*selection_mode_ == SelectionMode::RectangleSurfaceFace) {
                auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(current_model());
                if (mesh) {
                    easy3d::SurfaceMeshPicker picker(camera_);
                    auto faces = picker.pick_faces(mesh, rect);
                    for (auto f : faces)
                        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceFace,
                                                         f.idx(), false);
                    selection_manager_->bump_revision();
                    LOG(INFO) << "rect face deselect: " << faces.size() << " faces";
                }
            } else if (*selection_mode_ == SelectionMode::RectanglePointCloudPoint) {
                try_rect_pick_points(rect, true);
            }
        }
    }

    return finished_left_rect;
}


bool ViewportCanvas::stop_active_gizmo_drag() {
    const bool consumed_crop_gizmo = crop_state_ && crop_state_->gizmo_dragging;
    if (crop_state_ && crop_state_->gizmo_dragging) {
        crop_state_->gizmo_dragging = false;
        crop_state_->gizmo_axis = -1;
    }
    const bool consumed_align_gizmo = align_state_ && align_state_->gizmo_dragging;
    if (align_state_ && align_state_->gizmo_dragging) {
        align_state_->gizmo_dragging = false;
        align_state_->gizmo_axis = -1;
        align_state_->gizmo_target = nullptr;
    }
    return consumed_crop_gizmo || consumed_align_gizmo;
}


void ViewportCanvas::dispatch_viewport_release_click(bool is_left_click, bool is_right_click, int x, int y) {
    if (is_left_click) {
        if (dispatch_walkthrough_click(x, y))
            return;

        if (try_measurement_pick(x, y))
            return;

        if (selection_mode_ &&
            *selection_mode_ == SelectionMode::PickSurfaceFace) {
            try_pick_face(x, y);
        } else if (selection_mode_ &&
                   *selection_mode_ == SelectionMode::PickSurfaceVertex) {
            try_pick_vertex(x, y);
        } else if (selection_mode_ &&
                   *selection_mode_ == SelectionMode::PickPointCloudPoint) {
            try_pick_point(x, y);
        } else {
            try_pick_model(x, y);
        }
    } else if (is_right_click) {
        if (selection_mode_ &&
            *selection_mode_ == SelectionMode::PickSurfaceFace) {
            try_deselect_face(x, y);
        } else if (selection_mode_ &&
                   *selection_mode_ == SelectionMode::PickSurfaceVertex) {
            try_deselect_vertex(x, y);
        } else if (selection_mode_ &&
                   *selection_mode_ == SelectionMode::PickPointCloudPoint) {
            try_deselect_point(x, y);
        }
    }
}


bool ViewportCanvas::dispatch_walkthrough_click(int x, int y) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyAlt || !walk_through_)
        return false;

    if (walk_through_->status() == WalkThrough::WALKING_MODE &&
        !walk_through_->interpolator()->is_interpolation_started()) {
        walk_through_->set_scene(models_);
        easy3d::SurfaceMeshPicker sp(camera_);
        auto* m = current_model();
        if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(m)) {
            auto face = sp.pick_face(mesh, x, y);
            if (face.is_valid()) {
                auto p = sp.picked_point(mesh, face, x, y);
                walk_through_->walk_to(p);
                dirty_ = true;
                LOG(INFO) << "walk to: " << p;
            }
        }
    } else if (walk_through_->status() == WalkThrough::ROTATE_AROUND_AXIS) {
        if (walk_through_->interpolator()->number_of_keyframes() > 0)
            walk_through_->interpolator()->delete_path();
        walk_through_->set_scene(models_);
        easy3d::Picker picker(camera_);
        auto line = picker.picking_line(x, y);
        walk_through_->generate_camera_path(line);
        dirty_ = true;
        LOG(INFO) << "camera path generated around axis: "
                  << walk_through_->interpolator()->number_of_keyframes()
                  << " keyframes";
    }
    return true;
}


void ViewportCanvas::handle_mouse_drag() {
    if (pressed_button_ <= 0 || !ImGui::IsMouseDragging(pressed_button_ - 1))
        return;

    ImGuiIO& io = ImGui::GetIO();
    float dx = io.MouseDelta.x;
    float dy = io.MouseDelta.y;

    const int drag_x = static_cast<int>(io.MousePos.x - viewport_min_x_);
    const int drag_y = static_cast<int>(io.MousePos.y - viewport_min_y_);
    if (update_crop_gizmo_drag(drag_x, drag_y, io.MouseDelta.x))
        return;

    if (update_align_gizmo_drag(drag_x, drag_y, io.MouseDelta.x))
        return;

    if (update_rectangle_drag()) {
        dirty_ = true;
        return;
    }

    apply_camera_drag(dx, dy);
    dirty_ = true;
}


bool ViewportCanvas::update_rectangle_drag() {
    ImGuiIO& io = ImGui::GetIO();
    bool in_rect_mode = selection_mode_ &&
        (*selection_mode_ == SelectionMode::RectangleSurfaceFace ||
         *selection_mode_ == SelectionMode::RectanglePointCloudPoint);
    if (pressed_button_ == 1 && in_rect_mode) {
        if (!rect_dragging_) {
            rect_start_x_ = mouse_pressed_x_;
            rect_start_y_ = mouse_pressed_y_;
        }
        rect_dragging_ = true;
        rect_end_x_ = io.MousePos.x;
        rect_end_y_ = io.MousePos.y;
        return true;
    }
    return false;
}


void ViewportCanvas::apply_camera_drag(float dx, float dy) {
    ImGuiIO& io = ImGui::GetIO();
    easy3d::ManipulatedCameraFrame* frame = camera_->frame();

    if (pressed_button_ == 1) {
        frame->action_rotate(static_cast<int>(io.MousePos.x),
                             static_cast<int>(io.MousePos.y),
                             static_cast<int>(dx),
                             static_cast<int>(dy),
                             camera_, easy3d::ManipulatedFrame::NONE);
    } else if (pressed_button_ == 2) {
        frame->action_translate(static_cast<int>(io.MousePos.x),
                                static_cast<int>(io.MousePos.y),
                                static_cast<int>(dx),
                                static_cast<int>(dy),
                                camera_, easy3d::ManipulatedFrame::NONE);
    } else if (pressed_button_ == 3 && dy != 0) {
        frame->action_zoom(dy > 0 ? 1 : -1, camera_);
    }
}


void ViewportCanvas::handle_mouse_wheel() {
    ImGuiIO& io = ImGui::GetIO();
    easy3d::ManipulatedCameraFrame* frame = camera_->frame();

    if (viewport_hovered_ && io.MouseWheel != 0 && !io.KeyCtrl) {
        frame->action_zoom(io.MouseWheel > 0 ? 1 : -1, camera_);
        dirty_ = true;
    }
}


void ViewportCanvas::handle_keyboard_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    easy3d::ManipulatedCameraFrame* frame = camera_->frame();

    if (viewport_focused_) {
        if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl) {
            fit_screen(current_model());
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) && io.KeyCtrl) {
            frame->action_zoom(-1, camera_);
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) && io.KeyCtrl) {
            frame->action_zoom(1, camera_);
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            camera_->frame()->action_turn(1.0f * 3.14159f / 180.0f, camera_);
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            camera_->frame()->action_turn(-1.0f * 3.14159f / 180.0f, camera_);
            dirty_ = true;
        }
    }
}


void ViewportCanvas::handle_walkthrough_keyframe() {
    ImGuiIO& io = ImGui::GetIO();

    if (walk_through_ &&
        walk_through_->status() == WalkThrough::FREE_MODE &&
        !walk_through_->interpolator()->is_interpolation_started() &&
        !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_K) && !io.KeyCtrl && !io.KeyAlt) {
        easy3d::Frame f(camera_->position(), camera_->orientation());
        walk_through_->add_keyframe(f);
        dirty_ = true;
        LOG(INFO) << "keyframe added: " << walk_through_->interpolator()->number_of_keyframes() << " total";
    }
}

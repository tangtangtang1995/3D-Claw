// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/shader_manager.h>
#include <easy3d/renderer/text_renderer.h>
#include <easy3d/renderer/texture_manager.h>


ViewportCanvas::ViewportCanvas()
    : camera_(nullptr)
    , background_color_(0.22f, 0.22f, 0.25f, 1.0f)
    , model_idx_(-1)
    , fbo_(0), fbo_texture_(0), fbo_depth_(0)
    , fbo_width_(0), fbo_height_(0)
    , opengl_initialized_(false)
    , dirty_(true)
    , dpi_scaling_(1.0f)
    , drawable_axes_(nullptr)
    , texter_(nullptr)
    , show_frame_rate_(false)
    , pressed_button_(0)
    , modifiers_(0)
    , mouse_x_(0), mouse_y_(0)
    , mouse_pressed_x_(0), mouse_pressed_y_(0)
    , viewport_hovered_(false)
    , viewport_focused_(false)
{
    camera_ = new easy3d::Camera;
    camera_->setType(easy3d::Camera::PERSPECTIVE);
    camera_->setUpVector(easy3d::vec3(0, 0, 1));
    camera_->setViewDirection(easy3d::vec3(-1, 0, 0));
    camera_->showEntireScene();
}


ViewportCanvas::~ViewportCanvas() {
    destroy_fbo();
    delete camera_;
    delete drawable_axes_;
    delete selection_bbox_drawable_;
    delete texter_;
    models_.clear();
    easy3d::ShaderManager::terminate();
    easy3d::TextureManager::terminate();
}

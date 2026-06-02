// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include "services/resources/resource_paths.h"

#include <easy3d/renderer/opengl.h>
#include <easy3d/renderer/opengl_error.h>

#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/renderer/opengl_util.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/text_renderer.h>
#include <easy3d/util/logging.h>

#include "ui/walk_through.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

float mix_float(float a, float b, float t) {
    return a + (b - a) * t;
}

void mix_color(const easy3d::vec4& base,
               const float tint[3],
               float strength,
               float out[3]) {
    out[0] = mix_float(base[0], tint[0], strength);
    out[1] = mix_float(base[1], tint[1], strength);
    out[2] = mix_float(base[2], tint[2], strength);
}

void draw_background_gradient(int width,
                              int height,
                              const easy3d::vec4& base) {
    if (width <= 0 || height <= 0)
        return;

    static const float kBottomTint[3] = {0.21f, 0.20f, 0.31f};
    static const float kTopTint[3] = {0.55f, 0.52f, 0.72f};
    float bottom[3], top[3];
    mix_color(base, kBottomTint, 0.85f, bottom);
    mix_color(base, kTopTint, 0.85f, top);

    GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint scissor_box[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);

    const int stripe_count = height < 96 ? height : 96;
    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < stripe_count; ++i) {
        const int y0 = (i * height) / stripe_count;
        const int y1 = ((i + 1) * height) / stripe_count;
        const float t = (stripe_count > 1)
            ? static_cast<float>(i) / static_cast<float>(stripe_count - 1)
            : 0.0f;
        glScissor(0, y0, width, y1 - y0);
        glClearColor(mix_float(bottom[0], top[0], t),
                     mix_float(bottom[1], top[1], t),
                     mix_float(bottom[2], top[2], t),
                     base[3]);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (scissor_enabled) {
        glScissor(scissor_box[0], scissor_box[1],
                  scissor_box[2], scissor_box[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

} // namespace


void ViewportCanvas::init_opengl() {
    if (opengl_initialized_)
        return;

    easy3d::OpenglUtil::init();
#ifndef NDEBUG
    easy3d::opengl::setup_gl_debug_callback();
#endif

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepthf(1.0f);
    glClearColor(background_color_[0], background_color_[1], background_color_[2], background_color_[3]);

    VLOG(1) << "OpenGL vendor: " << glGetString(GL_VENDOR);
    VLOG(1) << "OpenGL renderer: " << glGetString(GL_RENDERER);
    VLOG(1) << "OpenGL version: " << glGetString(GL_VERSION);

    texter_ = new easy3d::TextRenderer(dpi_scaling_);
    texter_->add_font(claw3d::resources::easy3d_resource_path(
        "fonts/en_Earth-Normal.ttf"));
    texter_->add_font(claw3d::resources::easy3d_resource_path(
        "fonts/en_Roboto-Medium.ttf"));

    opengl_initialized_ = true;
}


void ViewportCanvas::create_fbo(int w, int h) {
    if (w <= 0 || h <= 0) return;

    destroy_fbo();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &fbo_texture_);
    glBindTexture(GL_TEXTURE_2D, fbo_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_texture_, 0);

    glGenTextures(1, &fbo_depth_);
    glBindTexture(GL_TEXTURE_2D, fbo_depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbo_depth_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        LOG(ERROR) << "FBO incomplete: 0x" << std::hex << status;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fbo_width_ = w;
    fbo_height_ = h;
}


void ViewportCanvas::destroy_fbo() {
    if (fbo_)         { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (fbo_texture_) { glDeleteTextures(1, &fbo_texture_); fbo_texture_ = 0; }
    if (fbo_depth_)   { glDeleteTextures(1, &fbo_depth_); fbo_depth_ = 0; }
    fbo_width_ = fbo_height_ = 0;
}


void ViewportCanvas::set_background_color(const easy3d::vec4& c) {
    background_color_ = c;
    glClearColor(c[0], c[1], c[2], c[3]);
}

void ViewportCanvas::run_with_panel_gl_viewport(
        int width, int height, const std::function<void()>& callback) {
    if (!callback)
        return;

    GLint previous_viewport[4];
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glViewport(0, 0, width, height);
    callback();
    glViewport(previous_viewport[0], previous_viewport[1],
               previous_viewport[2], previous_viewport[3]);
}


void ViewportCanvas::pre_draw() {
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    draw_background_gradient(fbo_width_, fbo_height_, background_color_);
}


void ViewportCanvas::draw_scene() {
    if (models_.empty()) return;

    for (const auto& m : models_) {
        if (!m->renderer()->is_visible())
            continue;

        std::size_t line_count = 0;
        for (auto& d : m->renderer()->lines_drawables()) {
            if (d->is_visible()) {
                d->draw(camera_);
                ++line_count;
            }
        }

        for (auto& d : m->renderer()->points_drawables()) {
            if (d->is_visible())
                d->draw(camera_);
        }

        if (line_count > 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0.5f, -0.0001f);
        }
        std::vector<easy3d::TrianglesDrawable*> transparent;
        for (auto& d : m->renderer()->triangles_drawables()) {
            if (!d->is_visible()) continue;
            if (d->opacity() < 1.0f)
                transparent.push_back(d.get());
            else
                d->draw(camera_);
        }
        if (!transparent.empty()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            for (auto* d : transparent) {
                glBlendColor(0, 0, 0, d->opacity());
                d->draw(camera_);
            }
            glDisable(GL_BLEND);
        }
        if (line_count > 0)
            glDisable(GL_POLYGON_OFFSET_FILL);
    }

    draw_selection_bbox();

    if (walk_through_ && !walk_through_->interpolator()->is_interpolation_started())
        walk_through_->draw();
}


void ViewportCanvas::post_draw() {
    if (show_frame_rate_ && texter_ && texter_->num_fonts() >= 2) {
        static int fps_count = 0;
        static std::chrono::steady_clock::time_point last_time;
        static bool has_last_time = false;
        static double fps = 0.0;
        static std::string fps_str = "fps: ??";
        if (++fps_count == 40) {
            const auto now = std::chrono::steady_clock::now();
            if (has_last_time) {
                const double elapsed =
                    std::chrono::duration<double>(now - last_time).count();
                if (elapsed > 0.0)
                    fps = 40.0 / elapsed;
                char buf[32];
                snprintf(buf, sizeof(buf), "fps: %.0f", fps);
                fps_str = buf;
            }
            last_time = now;
            has_last_time = true;
            fps_count = 0;
        }
        texter_->draw(fps_str, 20.0f * dpi_scaling_, 50.0f * dpi_scaling_, 16, 1);
    }

    draw_corner_axes();
}


std::uintptr_t ViewportCanvas::render_scene_texture(int width, int height) {
    if (!opengl_initialized_)
        init_opengl();

    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    if (width != fbo_width_ || height != fbo_height_) {
        create_fbo(width, height);
        dirty_ = true;
    }

    camera_->setScreenWidthAndHeight(width, height);

    if (!fbo_)
        return 0;

    handle_input();

    if (walk_through_ && walk_through_->interpolator()->is_interpolation_started())
        dirty_ = true;

    if (dirty_) {
        GLint prev_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
        GLint prev_viewport[4];
        glGetIntegerv(GL_VIEWPORT, prev_viewport);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, fbo_width_, fbo_height_);

        pre_draw();
        draw_scene();
        post_draw();

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
        glViewport(prev_viewport[0], prev_viewport[1],
                   prev_viewport[2], prev_viewport[3]);

        dirty_ = false;
    }

    return static_cast<std::uintptr_t>(fbo_texture_);
}

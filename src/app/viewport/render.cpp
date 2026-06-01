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
#include <3rd_party/stb/stb_image_write.h>
#include <3rd_party/stb/stb_image.h>

#include <easy3d/core/point_cloud.h>
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

#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>

#include "imgui.h"

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


ImU32 colormap_color(float t) {
    static unsigned char* cmap = nullptr;
    static int cmap_w = 0, cmap_h = 0;
    if (!cmap) {
        std::string path = claw3d::resources::easy3d_resource_path(
            "colormaps/default.png");
        int ch;
        cmap = stbi_load(path.c_str(), &cmap_w, &cmap_h, &ch, 4);
    }
    if (!cmap || cmap_w <= 0) return IM_COL32(128, 128, 128, 255);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const int x = static_cast<int>(t * (cmap_w - 1));
    const int y = cmap_h / 2;
    const int idx = (y * cmap_w + x) * 4;
    return IM_COL32(cmap[idx], cmap[idx+1], cmap[idx+2], cmap[idx+3]);
}


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
        static double last_time = 0.0;
        static double fps = 0.0;
        static std::string fps_str = "fps: ??";
        if (++fps_count == 40) {
            double now = glfwGetTime();
            if (last_time > 0) {
                fps = 40.0 / (now - last_time);
                char buf[32];
                snprintf(buf, sizeof(buf), "fps: %.0f", fps);
                fps_str = buf;
            }
            last_time = now;
            fps_count = 0;
        }
        texter_->draw(fps_str, 20.0f * dpi_scaling_, 50.0f * dpi_scaling_, 16, 1);
    }

    draw_corner_axes();
}


void ViewportCanvas::render() {
    if (!opengl_initialized_)
        init_opengl();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar);

    viewport_hovered_ = ImGui::IsWindowHovered();
    viewport_focused_ = ImGui::IsWindowFocused();

    ImVec2 size = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(size.x);
    int h = static_cast<int>(size.y);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    if (w != fbo_width_ || h != fbo_height_) {
        create_fbo(w, h);
        dirty_ = true;
    }

    camera_->setScreenWidthAndHeight(w, h);

    if (fbo_) {
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
            glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

            dirty_ = false;
        }

        ImGui::Image((ImTextureID)(intptr_t)fbo_texture_, size, ImVec2(0, 1), ImVec2(1, 0));
        viewport_min_x_ = ImGui::GetItemRectMin().x;
        viewport_min_y_ = ImGui::GetItemRectMin().y;
        viewport_max_x_ = ImGui::GetItemRectMax().x;
        viewport_max_y_ = ImGui::GetItemRectMax().y;

        draw_axes_gizmo();

        if (!models_.empty()) {
            auto* m = current_model();
            if (m) {
                auto* pc = dynamic_cast<easy3d::PointCloud*>(m);
                if (pc) {
                    auto dist = pc->get_vertex_property<float>("v:dist");
                    auto drawable = pc->renderer()->get_points_drawable("vertices");
                    if (dist && drawable && drawable->coloring_method() == easy3d::State::SCALAR_FIELD) {
                        float dmin = 1e30f, dmax = -1e30f;
                        for (auto v : pc->vertices()) {
                            float d = dist[v];
                            if (d < dmin) dmin = d;
                            if (d > dmax) dmax = d;
                        }
                        if (dmax - dmin < 1e-6f) dmax = dmin + 1e-6f;

                        ImVec2 vp = ImGui::GetWindowPos();
                        ImVec2 vs = ImGui::GetWindowSize();
                        float bar_w = 18, bar_x = vp.x + vs.x - bar_w - 22;
                        float bar_h = (vs.y - 80) / 3.0f;
                        float bar_top = vp.y + 40;
                        float bar_bottom = bar_top + bar_h;

                        ImDrawList* dl = ImGui::GetForegroundDrawList();

                        dl->AddRectFilled(ImVec2(bar_x - 2, bar_top - 12), ImVec2(bar_x + bar_w + 38, bar_bottom + 8),
                            IM_COL32(0,0,0,160), 4.0f);

                        int nbins = 60;
                        std::vector<int> bins(nbins, 0);
                        for (auto v : pc->vertices()) {
                            const int bi = static_cast<int>(
                                (dist[v] - dmin) / (dmax - dmin) *
                                (nbins - 1));
                            if (bi >= 0 && bi < nbins) bins[bi]++;
                        }
                        int max_count = 1;
                        for (int c : bins) if (c > max_count) max_count = c;

                        float hist_w = 30, hist_max_h = bar_h;
                        for (int i = 0; i < nbins; i++) {
                            const float hh = (max_count > 0)
                                ? (static_cast<float>(bins[i]) / max_count *
                                   hist_max_h)
                                : 0.0f;
                            float y = bar_bottom - hh;
                            float x = bar_x - hist_w - 4;
                            const float bx =
                                x + static_cast<float>(i) / nbins * hist_w;
                            const float bw = hist_w / static_cast<float>(nbins);
                            const float t =
                                static_cast<float>(i) / (nbins - 1);
                            ImU32 col = colormap_color(t);
                            col = IM_COL32((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF, 200);
                            dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, bar_bottom), col);
                        }

                        for (int i = 0; i < static_cast<int>(bar_h); i++) {
                            const float t =
                                1.0f - static_cast<float>(i) / bar_h;
                            dl->AddRectFilled(ImVec2(bar_x, bar_top + i), ImVec2(bar_x + bar_w, bar_top + i + 1),
                                colormap_color(t));
                        }

                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.4f", dmax);
                        dl->AddText(ImVec2(bar_x + bar_w + 4, bar_top - 4), IM_COL32(255,255,255,200), buf);
                        snprintf(buf, sizeof(buf), "%.4f", dmin);
                        dl->AddText(ImVec2(bar_x + bar_w + 4, bar_bottom - 10), IM_COL32(255,255,255,200), buf);
                    }
                }
            }
        }
    }

    if (rect_dragging_) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 r_min(rect_start_x_ < rect_end_x_ ? rect_start_x_ : rect_end_x_,
                      rect_start_y_ < rect_end_y_ ? rect_start_y_ : rect_end_y_);
        ImVec2 r_max(rect_start_x_ < rect_end_x_ ? rect_end_x_ : rect_start_x_,
                      rect_start_y_ < rect_end_y_ ? rect_end_y_ : rect_start_y_);
        dl->AddRectFilled(r_min, r_max, IM_COL32(255, 220, 50, 50));
        dl->AddRect(r_min, r_max, IM_COL32(255, 220, 50, 200), 0.0f, 0, 1.5f);
    }

    if (walk_through_ && walk_through_->interpolator()->is_interpolation_started())
        dirty_ = true;

    ImGui::End();
    ImGui::PopStyleVar();
}

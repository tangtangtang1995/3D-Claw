// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/renderer/opengl.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <3rd_party/stb/stb_image_write.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

void ViewportCanvas::copy_camera() {
    const auto pos = camera_->position();
    const auto q = camera_->orientation();
    char buf[256];
    snprintf(buf, sizeof(buf), "%.6f %.6f %.6f %.6f %.6f %.6f %.6f",
             pos[0], pos[1], pos[2], q[0], q[1], q[2], q[3]);
    glfwSetClipboardString(nullptr, buf);
    LOG(INFO) << "camera copied to clipboard";
}

void ViewportCanvas::paste_camera() {
    const char* str = glfwGetClipboardString(nullptr);
    if (!str)
        return;

    float v[7];
    int n = sscanf(str, "%f %f %f %f %f %f %f",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    if (n != 7) {
        LOG(WARNING) << "camera not available in clipboard";
        return;
    }

    easy3d::vec3 pos(v[0], v[1], v[2]);
    easy3d::quat quat(v[3], v[4], v[5], v[6]);
    camera_->setPosition(pos);
    camera_->setOrientation(quat);
    dirty_ = true;
    LOG(INFO) << "camera pasted from clipboard";
}

bool ViewportCanvas::snapshot(const std::string& file_path, int w, int h,
                                int /*samples*/, int background,
                                bool /*expand*/) {
    if (w <= 0 || h <= 0)
        return false;

    GLuint snap_fbo, snap_tex, snap_depth;
    glGenFramebuffers(1, &snap_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, snap_fbo);

    glGenTextures(1, &snap_tex);
    glBindTexture(GL_TEXTURE_2D, snap_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           snap_tex, 0);

    glGenTextures(1, &snap_depth);
    glBindTexture(GL_TEXTURE_2D, snap_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           snap_depth, 0);

    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    float prev_w = static_cast<float>(camera_->screenWidth());
    float prev_h = static_cast<float>(camera_->screenHeight());

    glBindFramebuffer(GL_FRAMEBUFFER, snap_fbo);
    glViewport(0, 0, w, h);
    camera_->setScreenWidthAndHeight(w, h);

    if (background == 1) {
        glClearColor(1, 1, 1, 1);
    } else if (background == 2) {
        glClearColor(0, 0, 0, 0);
    } else {
        glClearColor(background_color_[0], background_color_[1],
                     background_color_[2], background_color_[3]);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_scene();

    std::vector<unsigned char> pixels(w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    camera_->setScreenWidthAndHeight(static_cast<int>(prev_w),
                                     static_cast<int>(prev_h));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteFramebuffers(1, &snap_fbo);
    glDeleteTextures(1, &snap_tex);
    glDeleteTextures(1, &snap_depth);

    std::vector<unsigned char> flipped(w * h * 4);
    for (int y = 0; y < h; ++y)
        memcpy(&flipped[y * w * 4], &pixels[(h - 1 - y) * w * 4], w * 4);

    int ret = stbi_write_png(file_path.c_str(), w, h, 4, flipped.data(), w * 4);
    return ret != 0;
}

void ViewportCanvas::save_camera_state(const std::string& file_path) {
    std::ofstream out(file_path);
    if (!out) {
        LOG(ERROR) << "cannot write: " << file_path;
        return;
    }
    const auto& pos = camera_->position();
    const auto& q = camera_->orientation();
    out << pos[0] << " " << pos[1] << " " << pos[2] << " "
        << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << " "
        << camera_->sceneCenter()[0] << " " << camera_->sceneCenter()[1] << " "
        << camera_->sceneCenter()[2] << " "
        << camera_->sceneRadius() << " "
        << camera_->fieldOfView() << "\n";
    LOG(INFO) << "camera state saved to: " << file_path;
}

void ViewportCanvas::restore_camera_state(const std::string& file_path) {
    std::ifstream in(file_path);
    if (!in) {
        LOG(ERROR) << "cannot read: " << file_path;
        return;
    }
    float px, py, pz, qw, qx, qy, qz, sx, sy, sz, sr, fov;
    in >> px >> py >> pz >> qw >> qx >> qy >> qz >> sx >> sy >> sz >> sr >> fov;
    if (!in) {
        LOG(ERROR) << "invalid camera state file";
        return;
    }
    camera_->setPosition(easy3d::vec3(px, py, pz));
    camera_->setOrientation(easy3d::quat(qw, qx, qy, qz));
    camera_->setSceneCenter(easy3d::vec3(sx, sy, sz));
    camera_->setSceneRadius(sr);
    camera_->setFieldOfView(fov);
    dirty_ = true;
    LOG(INFO) << "camera state restored from: " << file_path;
}

void ViewportCanvas::fit_screen(const easy3d::Model* model) {
    if (!model && models_.empty()) {
        camera_->showEntireScene();
        return;
    }

    auto visual_box = [](const easy3d::Model* m) -> easy3d::Box3 {
        easy3d::Box3 box = m->bounding_box();
        for (auto& d : m->renderer()->points_drawables())
            box.grow(d->bounding_box());
        for (auto& d : m->renderer()->lines_drawables())
            box.grow(d->bounding_box());
        for (auto& d : m->renderer()->triangles_drawables())
            box.grow(d->bounding_box());
        return box;
    };

    easy3d::Box3 box;
    if (model) {
        box = visual_box(model);
    } else {
        for (auto& m : models_)
            box.grow(visual_box(m.get()));
    }

    if (box.is_valid()) {
        camera_->setSceneBoundingBox(box.min_point(), box.max_point());
        camera_->showEntireScene();
        dirty_ = true;
    }
}

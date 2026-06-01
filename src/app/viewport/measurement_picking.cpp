// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/renderer/opengl.h>

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/gui/picker_point_cloud.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/util/logging.h>

#include "dialogs/measurement_dialog.h"

#include <limits>


bool ViewportCanvas::try_measurement_pick(int x, int y) {
    if (!measurement_state_ || !measurement_state_->picking) return false;

    auto* model = current_model();
    if (!model) return false;

    easy3d::vec3 picked(0, 0, 0);
    bool hit = false;
    const bool snap_to_vertex = measurement_state_->snap_to_vertex;

    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        GLint prev_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
        GLint prev_vp[4];
        glGetIntegerv(GL_VIEWPORT, prev_vp);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, fbo_width_, fbo_height_);

        easy3d::SurfaceMeshPicker picker(camera_);
        // Pick the face first -- both modes need it; vertex snap then
        // chooses the nearest of the face's 3 vertices in screen space,
        // surface mode asks the picker for the exact ray-surface point.
        auto face = picker.pick_face(mesh, x, y);
        if (face.is_valid()) {
            if (snap_to_vertex) {
                auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
                easy3d::SurfaceMesh::Vertex closest_v;
                float min_d = std::numeric_limits<float>::max();
                const easy3d::vec2 target(static_cast<float>(x),
                                          static_cast<float>(y));
                for (auto v : mesh->vertices(face)) {
                    auto wp = pts[v];
                    auto sp = camera_->projectedCoordinatesOf(wp);
                    float d = easy3d::distance2(easy3d::vec2(sp.x, sp.y), target);
                    if (d < min_d) { min_d = d; closest_v = v; }
                }
                if (closest_v.is_valid()) {
                    picked = pts[closest_v];
                    hit = true;
                }
            } else {
                picked = picker.picked_point(mesh, face, x, y);
                hit = true;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        GLint prev_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
        GLint prev_vp[4];
        glGetIntegerv(GL_VIEWPORT, prev_vp);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, fbo_width_, fbo_height_);

        easy3d::PointCloudPicker picker(camera_);
        auto v = picker.pick_vertex(cloud, x, y);
        if (v.is_valid()) {
            auto pts = cloud->get_vertex_property<easy3d::vec3>("v:point");
            picked = pts[v];
            hit = true;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    }

    if (hit) {
        measurement_state_->add_point(picked);
        dirty_ = true;
        LOG(INFO) << "measurement pick: " << picked;
        return true;
    }
    return false;
}



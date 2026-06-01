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
#include <easy3d/gui/picker_model.h>
#include <easy3d/gui/picker_point_cloud.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/util/logging.h>

#include "selection/selection_manager.h"

#include <limits>


void ViewportCanvas::try_pick_model(int x, int y) {
    if (models_.empty()) {
        if (selection_active_) {
            selection_active_ = false;
            dirty_ = true;
        }
        return;
    }
    if (fbo_width_ <= 0 || fbo_height_ <= 0) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::ModelPicker picker(camera_);
    easy3d::Model* picked = picker.pick(models_, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    // If the picked entity is a selection / measurement overlay,
    // do NOT treat it as a user-pickable model. Promoting an overlay to
    // current_model would point subsequent element-pick / measurement-pick
    // calls at the overlay (e.g. try_pick_point on an overlay PointCloud),
    // which writes selection state under the wrong Model* and produces
    // cascading overlays-of-overlays. Fall through to the source model
    // selection or treat as miss.
    if (picked) {
        if (auto* info = model_tree_info(picked)) {
            if (info->kind == ModelTreeNodeKind::Overlay) {
                if (info->parent) picked = info->parent; // promote to source
                else              picked = nullptr;
            }
        }
    }

    if (picked) {
        set_current_model(picked);   // also sets selection_active_ = true
        selected_drawable_type_ = SelectedDrawableType::None;
    } else {
        selection_active_ = false;
    }
    dirty_ = true;
}


void ViewportCanvas::try_pick_face(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::SurfaceMeshPicker picker(camera_);
    easy3d::SurfaceMesh::Face face = picker.pick_face(mesh, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (face.is_valid()) {
        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceFace,
                                         face.idx(), true);
        dirty_ = true;
        LOG(INFO) << "face selected: " << face.idx();
    }
}


void ViewportCanvas::try_deselect_face(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::SurfaceMeshPicker picker(camera_);
    easy3d::SurfaceMesh::Face face = picker.pick_face(mesh, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (face.is_valid()) {
        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceFace,
                                         face.idx(), false);
        dirty_ = true;
        LOG(INFO) << "face deselected: " << face.idx();
    }
}


// Pick the screen-space-nearest vertex of `face`. Unlike SurfaceMeshPicker::
// pick_vertex(mesh, x, y), this never enforces hit_resolution_, so a click
// anywhere inside the face still picks the closest of its 3 vertices --
// matches "snap to vertex" UX in CloudCompare / MeshLab.
static easy3d::SurfaceMesh::Vertex
nearest_face_vertex_to_screen(easy3d::SurfaceMesh* mesh,
                              easy3d::SurfaceMesh::Face face,
                              const easy3d::Camera* camera,
                              int x, int y)
{
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    easy3d::SurfaceMesh::Vertex closest;
    float min_d = std::numeric_limits<float>::max();
    const easy3d::vec2 target(static_cast<float>(x), static_cast<float>(y));
    for (auto v : mesh->vertices(face)) {
        auto wp = pts[v];
        auto sp = camera->projectedCoordinatesOf(wp);
        float d = easy3d::distance2(easy3d::vec2(sp.x, sp.y), target);
        if (d < min_d) { min_d = d; closest = v; }
    }
    return closest;
}


bool ViewportCanvas::pick_surface_vertex(easy3d::SurfaceMesh* mesh,
                                           int x,
                                           int y,
                                           int& vertex_id)
{
    vertex_id = -1;
    if (!mesh || fbo_width_ <= 0 || fbo_height_ <= 0)
        return false;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    const int prev_sw = camera_->screenWidth();
    const int prev_sh = camera_->screenHeight();
    camera_->setScreenWidthAndHeight(fbo_width_, fbo_height_);

    easy3d::SurfaceMeshPicker picker(camera_);
    auto face = picker.pick_face(mesh, x, y);
    easy3d::SurfaceMesh::Vertex v;
    if (face.is_valid())
        v = nearest_face_vertex_to_screen(mesh, face, camera_, x, y);

    camera_->setScreenWidthAndHeight(prev_sw, prev_sh);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (!v.is_valid())
        return false;

    vertex_id = v.idx();
    return true;
}


void ViewportCanvas::try_pick_vertex(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::SurfaceMeshPicker picker(camera_);
    auto face = picker.pick_face(mesh, x, y);
    easy3d::SurfaceMesh::Vertex v;
    if (face.is_valid())
        v = nearest_face_vertex_to_screen(mesh, face, camera_, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (v.is_valid()) {
        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceVertex,
                                         v.idx(), true);
        dirty_ = true;
        LOG(INFO) << "vertex selected: " << v.idx();
    }
}


void ViewportCanvas::try_deselect_vertex(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::SurfaceMeshPicker picker(camera_);
    auto face = picker.pick_face(mesh, x, y);
    easy3d::SurfaceMesh::Vertex v;
    if (face.is_valid())
        v = nearest_face_vertex_to_screen(mesh, face, camera_, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (v.is_valid()) {
        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceVertex,
                                         v.idx(), false);
        dirty_ = true;
        LOG(INFO) << "vertex deselected: " << v.idx();
    }
}


void ViewportCanvas::try_pick_point(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
    if (!cloud) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::PointCloudPicker picker(camera_);
    easy3d::PointCloud::Vertex v = picker.pick_vertex(cloud, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (v.is_valid()) {
        selection_manager_->set_selected(cloud, SelectionElementType::PointCloudPoint,
                                         v.idx(), true);
        dirty_ = true;
        LOG(INFO) << "point selected: " << v.idx();
    }
}


void ViewportCanvas::try_deselect_point(int x, int y) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
    if (!cloud) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbo_width_, fbo_height_);

    easy3d::PointCloudPicker picker(camera_);
    easy3d::PointCloud::Vertex v = picker.pick_vertex(cloud, x, y);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (v.is_valid()) {
        selection_manager_->set_selected(cloud, SelectionElementType::PointCloudPoint,
                                         v.idx(), false);
        dirty_ = true;
        LOG(INFO) << "point deselected: " << v.idx();
    }
}


void ViewportCanvas::try_rect_pick_faces(const easy3d::Rect& rect) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh) return;

    easy3d::SurfaceMeshPicker picker(camera_);
    auto faces = picker.pick_faces(mesh, rect);
    for (auto f : faces)
        selection_manager_->set_selected(mesh, SelectionElementType::SurfaceFace,
                                         f.idx(), true);
    selection_manager_->bump_revision();
    dirty_ = true;
    LOG(INFO) << "rect face select: " << faces.size() << " faces in rect";
}


void ViewportCanvas::try_rect_pick_points(const easy3d::Rect& rect, bool deselect) {
    if (!selection_manager_) return;
    auto* model = current_model();
    auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
    if (!cloud) return;

    easy3d::PointCloudPicker picker(camera_);
    picker.pick_vertices(cloud, rect, deselect);

    // Transfer from transient v:select to SelectionManager
    auto vselect = cloud->get_vertex_property<int>("v:select");
    if (vselect) {
        int count = 0;
        for (auto v : cloud->vertices()) {
            if (vselect[v]) {
                selection_manager_->set_selected(cloud, SelectionElementType::PointCloudPoint,
                                                 v.idx(), !deselect);
                ++count;
            }
        }
        cloud->remove_vertex_property(vselect);
        selection_manager_->bump_revision();
        LOG(INFO) << "rect point " << (deselect ? "deselect" : "select") << ": " << count << " points";
    }
    dirty_ = true;
}



// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/core/model.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_lines.h>

#include <vector>


void ViewportCanvas::rebuild_selection_bbox(easy3d::Model* model) {
    using namespace easy3d;
    delete selection_bbox_drawable_;
    selection_bbox_drawable_ = nullptr;
    selection_bbox_last_model_ = model;
    if (!model) return;
    const Box3& bbox = model->bounding_box();
    if (!bbox.is_valid()) return;
    const vec3& mn = bbox.min_point();
    const vec3& mx = bbox.max_point();
    vec3 c[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
        {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
        {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
    };
    static const int idx[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    std::vector<vec3> pts;
    pts.reserve(24);
    for (int i = 0; i < 12; ++i) {
        pts.push_back(c[idx[i][0]]);
        pts.push_back(c[idx[i][1]]);
    }
    selection_bbox_drawable_ = new LinesDrawable("selection_bbox");
    selection_bbox_drawable_->update_vertex_buffer(pts);
    selection_bbox_drawable_->set_uniform_coloring(vec4(1.0f, 0.85f, 0.0f, 1.0f));
    selection_bbox_drawable_->set_line_width(2.0f);
}


void ViewportCanvas::set_selection_bbox_suppressed(bool suppress) {
    if (selection_bbox_suppressed_ == suppress)
        return;
    selection_bbox_suppressed_ = suppress;
    if (suppress) {
        delete selection_bbox_drawable_;
        selection_bbox_drawable_ = nullptr;
        selection_bbox_last_model_ = nullptr;
    }
    dirty_ = true;
}


void ViewportCanvas::draw_selection_bbox() {
    if (!show_selection_bbox_ || !selection_active_ || selection_bbox_suppressed_) return;
    auto* m = current_model();
    if (!m) return;
    if (m != selection_bbox_last_model_)
        rebuild_selection_bbox(m);
    if (selection_bbox_drawable_)
        selection_bbox_drawable_->draw(camera_);
}

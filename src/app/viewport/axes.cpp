// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/shader_manager.h>
#include <easy3d/renderer/shader_program.h>
#include <easy3d/renderer/shape.h>

#include <algorithm>
#include <vector>

#include "imgui.h"


void ViewportCanvas::draw_axes_gizmo() {
    if (!show_axes_gizmo_ || !camera_) return;
    if (viewport_max_x_ <= viewport_min_x_ || viewport_max_y_ <= viewport_min_y_) return;

    const float size = 70.0f;
    const float pad  = 18.0f;
    ImVec2 cz(viewport_max_x_ - size * 0.5f - pad,
              viewport_max_y_ - size * 0.5f - pad);

    const easy3d::mat4& mv = camera_->modelViewMatrix();
    easy3d::vec3 ax[3] = {
        { mv(0,0), mv(1,0), mv(2,0) },
        { mv(0,1), mv(1,1), mv(2,1) },
        { mv(0,2), mv(1,2), mv(2,2) }
    };
    const ImU32 col[3] = {
        IM_COL32(230,  60,  60, 255),
        IM_COL32( 60, 200,  60, 255),
        IM_COL32( 80, 110, 230, 255),
    };
    const char* lbl[3] = { "X", "Y", "Z" };

    auto to_screen = [&](const easy3d::vec3& v) {
        return ImVec2(cz.x + v.x * (size * 0.42f),
                      cz.y - v.y * (size * 0.42f));
    };

    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int a, int b) {
        return ax[a].z < ax[b].z;
    });

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddCircleFilled(cz, size * 0.5f, IM_COL32(20, 20, 20, 120));

    for (int i : order) {
        ImVec2 tip = to_screen(ax[i]);
        dl->AddLine(cz, tip, col[i], 2.5f);
        dl->AddCircleFilled(tip, 4.5f, col[i]);
        dl->AddText(ImVec2(tip.x + 5, tip.y - 8), col[i], lbl[i]);
    }
}


void ViewportCanvas::draw_corner_axes() {
    using namespace easy3d;

    ShaderProgram* program = ShaderManager::get_program("surface/surface");
    if (!program) {
        std::vector<ShaderProgram::Attribute> attributes;
        attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::POSITION, "vtx_position"));
        attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::TEXCOORD, "vtx_texcoord"));
        attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::COLOR, "vtx_color"));
        attributes.emplace_back(ShaderProgram::Attribute(ShaderProgram::NORMAL, "vtx_normal"));
        program = ShaderManager::create_program_from_files("surface/surface", attributes);
    }
    if (!program) return;

    if (!drawable_axes_) {
        const float base = 0.5f;
        const float head = 0.2f;
        std::vector<vec3> points, normals, colors;
        shape::create_cylinder(0.03, 10, vec3(0,0,0), vec3(base,0,0), vec3(1,0,0), points, normals, colors);
        shape::create_cylinder(0.03, 10, vec3(0,0,0), vec3(0,base,0), vec3(0,1,0), points, normals, colors);
        shape::create_cylinder(0.03, 10, vec3(0,0,0), vec3(0,0,base), vec3(0,0,1), points, normals, colors);
        shape::create_cone(0.06, 20, vec3(base,0,0), vec3(base+head,0,0), vec3(1,0,0), points, normals, colors);
        shape::create_cone(0.06, 20, vec3(0,base,0), vec3(0,base+head,0), vec3(0,1,0), points, normals, colors);
        shape::create_cone(0.06, 20, vec3(0,0,base), vec3(0,0,base+head), vec3(0,0,1), points, normals, colors);
        shape::create_sphere(vec3(0,0,0), 0.06, 20, 20, vec3(0,1,1), points, normals, colors);
        drawable_axes_ = new TrianglesDrawable("corner_axes");
        drawable_axes_->update_vertex_buffer(points);
        drawable_axes_->update_normal_buffer(normals);
        drawable_axes_->update_color_buffer(colors);
        drawable_axes_->set_property_coloring(State::VERTEX);
    }
}

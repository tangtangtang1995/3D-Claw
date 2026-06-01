// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/prerequisites.h"
#include "ui/status_colors.h"

#include <algorithm>
#include <cfloat>
#include "viewport/viewport_canvas.h"

#include <easy3d/core/model.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>


static easy3d::Model* cur(ViewportCanvas* v) {
    return v ? v->current_model() : nullptr;
}


Prereq prereq_any_model(ViewportCanvas* v) {
    if (!cur(v))
        return Prereq::fail("Requires a model - load or select one in the Model List.");
    return Prereq::pass();
}


Prereq prereq_surface_mesh(ViewportCanvas* v) {
    auto* m = cur(v);
    if (!m) return Prereq::fail("Requires a Surface Mesh - none is loaded / selected.");
    if (!dynamic_cast<easy3d::SurfaceMesh*>(m))
        return Prereq::fail("Requires a Surface Mesh - current model is not a mesh.");
    return Prereq::pass();
}


Prereq prereq_point_cloud(ViewportCanvas* v) {
    auto* m = cur(v);
    if (!m) return Prereq::fail("Requires a Point Cloud - none is loaded / selected.");
    if (!dynamic_cast<easy3d::PointCloud*>(m))
        return Prereq::fail("Requires a Point Cloud - current model is not a point cloud.");
    return Prereq::pass();
}


Prereq prereq_pc_with_normals(ViewportCanvas* v) {
    auto* m = cur(v);
    if (!m) return Prereq::fail("Requires a Point Cloud with normals - none loaded.");
    auto* pc = dynamic_cast<easy3d::PointCloud*>(m);
    if (!pc) return Prereq::fail("Requires a Point Cloud - current model is not a point cloud.");
    auto n = pc->get_vertex_property<easy3d::vec3>("v:normal");
    if (!n) return Prereq::fail("Requires per-point normals - run 'Point Cloud > Estimate Normals' first.");
    return Prereq::pass();
}


Prereq prereq_mesh_or_pc(ViewportCanvas* v) {
    auto* m = cur(v);
    if (!m) return Prereq::fail("Requires a Surface Mesh or Point Cloud - none loaded.");
    if (!dynamic_cast<easy3d::SurfaceMesh*>(m) &&
        !dynamic_cast<easy3d::PointCloud*>(m))
        return Prereq::fail("Requires a Surface Mesh or Point Cloud.");
    return Prereq::pass();
}


Prereq prereq_graph_model(ViewportCanvas* v) {
    auto* m = cur(v);
    if (!m) return Prereq::fail("Requires a Graph model - none loaded.");
    if (!dynamic_cast<easy3d::Graph*>(m))
        return Prereq::fail("Requires a Graph model - current model is not a graph.");
    return Prereq::pass();
}


Prereq prereq_two_models(ViewportCanvas* v) {
    if (!v || v->models().size() < 2)
        return Prereq::fail("Requires at least 2 models loaded (source + target).");
    return Prereq::pass();
}


void prereq_begin(const Prereq& p) {
    if (!p.ok) {
        ImGui::TextColored(claw_ui::status_error_color(), "%s", p.hint.c_str());
        ImGui::BeginDisabled();
    }
}


void prereq_end(const Prereq& p) {
    if (!p.ok) ImGui::EndDisabled();
}


void prereq_hint_only(const Prereq& p) {
    if (!p.ok)
        ImGui::TextColored(claw_ui::status_error_color(), "%s", p.hint.c_str());
}


void prepare_dialog_window(float w, float h) {
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(w, 320.0f), std::min(h, 180.0f)),
        ImVec2(FLT_MAX, FLT_MAX));
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 c = vp->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

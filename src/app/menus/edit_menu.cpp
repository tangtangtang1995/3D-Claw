// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Edit menu section for MainWindow.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/surface_mesh_builder.h>
#include <easy3d/renderer/manipulator.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/transform.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

#include <vector>


void MainWindow::render_menu_edit() {
    if (ImGui::BeginMenu("Edit")) {
        menu_group_ai_item(this, "Ask AI: which edit action is safe?",
                           "Edit",
                           "Explain recentering, adding noise, and baking or discarding manipulator transforms.",
                           "Current model or scene with one or more loaded models.",
                           "Some actions permanently change vertex coordinates.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::MenuItem("Translational Recenter")) {
            const auto& models = viewer_.models();
            if (!models.empty()) {
                easy3d::vec3 origin = models[0]->bounding_box().center();
                for (auto& m : models) {
                    auto* model = m.get();
                    // Translate based on actual model type.
                    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
                        auto pts = sm->get_vertex_property<easy3d::vec3>("v:point");
                        for (auto v : sm->vertices()) pts[v] -= origin;
                    } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(model)) {
                        auto pts = pc->get_vertex_property<easy3d::vec3>("v:point");
                        for (auto v : pc->vertices()) pts[v] -= origin;
                    } else if (auto* g = dynamic_cast<easy3d::Graph*>(model)) {
                        auto pts = g->get_vertex_property<easy3d::vec3>("v:point");
                        for (auto v : g->vertices()) pts[v] -= origin;
                    } else if (auto* pm = dynamic_cast<easy3d::PolyMesh*>(model)) {
                        auto pts = pm->get_vertex_property<easy3d::vec3>("v:point");
                        for (auto v : pm->vertices()) pts[v] -= origin;
                    }
                    model->manipulator()->reset();
                    model->renderer()->update();
                }
                viewer_.fit_screen();
                viewer_.mark_dirty();
                LOG(INFO) << "translational recenter done";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Translate all models so their bounding-box center moves to world origin.\n"
                              "Resets the interactive manipulator afterward.");
        ImGui::Separator();
        if (ImGui::MenuItem("Add Gaussian Noise..."))
            open_dialog(dlg_gaussian_noise_, st_gaussian_noise_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Perturb vertex positions with Gaussian (normal-distribution) noise.\n"
                              "Useful for robustness testing and data augmentation.");
        ImGui::Separator();
        if (ImGui::MenuItem("Apply Manipulated Transformation")) {
            for (auto& m : viewer_.models()) {
                auto* model = m.get();
                easy3d::mat4 manip = model->manipulator()->matrix();
                auto& pts = model->points();
                for (auto& p : pts) p = manip * p;
                if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model))
                    sm->update_vertex_normals();
                else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
                    auto normal = cloud->get_vertex_property<easy3d::vec3>("v:normal");
                    if (normal) {
                        easy3d::mat3 N = easy3d::transform::normal_matrix(manip);
                        for (auto v : cloud->vertices()) normal[v] = N * normal[v];
                    }
                }
                model->manipulator()->reset();
                model->renderer()->update();
            }
            viewer_.mark_dirty();
            LOG(INFO) << "manipulated transformation applied";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bake the interactive gizmo transform (rotation, translation, scale)\n"
                              "permanently into vertex coordinates. Also transforms normals on point clouds.\n"
                              "Resets the manipulator to identity afterward.");
        if (ImGui::MenuItem("Give Up Manipulated Transformation")) {
            for (auto& m : viewer_.models())
                m->manipulator()->reset();
            viewer_.mark_dirty();
            LOG(INFO) << "manipulated transformation discarded";
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Discard the interactive gizmo transform without modifying vertex positions.\n"
                              "Resets the manipulator to identity.");
        ImGui::Separator();
        // Align and Crop / Clip are nested as submenus of Edit.
        render_menu_align();
        render_menu_crop_clip();
        ImGui::Separator();
        // Extract Boundary moved here from the former Poly Mesh menu.
        if (ImGui::MenuItem("Extract Boundary")) {
            auto* poly = dynamic_cast<easy3d::PolyMesh*>(viewer_.current_model());
            if (poly) {
                std::vector<std::vector<easy3d::PolyMesh::Vertex>> faces;
                poly->extract_boundary(faces);
                if (!faces.empty()) {
                    auto* mesh = new easy3d::SurfaceMesh;
                    easy3d::SurfaceMeshBuilder builder(mesh);
                    builder.begin_surface();
                    for (const auto& face : faces) {
                        std::vector<easy3d::SurfaceMesh::Vertex> vts;
                        for (auto pv : face)
                            vts.push_back(builder.add_vertex(poly->position(pv)));
                        builder.add_face(vts);
                    }
                    builder.end_surface();
                    mesh->set_name(poly->name() + ".boundary");
                    viewer_.add_model(mesh);
                    viewer_.mark_dirty();
                    LOG(INFO) << "boundary extracted: " << mesh->n_faces() << " faces";
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Extract the boundary surface of a volumetric PolyMesh as a triangle SurfaceMesh.\n"
                              "The boundary faces form the outer hull of the tetrahedral mesh.");
        ImGui::EndMenu();
    }
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Selection overlay rendering helpers for MainWindow.
// These are the small overlays that live in the scene as helper Models
// for face/vertex/point highlights.
//
// Extracted out of main_window.cpp during the 2024-2025 split.

#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "overlays/overlay_utils.h"
#include "selection/selection_manager.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>

#include <sstream>
#include <unordered_map>
#include <vector>


void MainWindow::clear_selection_overlays() {
    delete_model_if_live(viewer_, interaction_overlay_.selection_faces);
    delete_model_if_live(viewer_, interaction_overlay_.selection_vertices);
    delete_model_if_live(viewer_, interaction_overlay_.selection_points);
    interaction_overlay_.selection_source = nullptr;
}


std::string MainWindow::current_selection_summary() {
    auto* model = viewer_.current_model();
    if (!model)
        return "no current model";

    std::ostringstream ss;
    ss << "model=" << model->name();
    bool any = false;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        const int nf = selection_manager_.selected_count(
            mesh, SelectionElementType::SurfaceFace);
        const int nv = selection_manager_.selected_count(
            mesh, SelectionElementType::SurfaceVertex);
        if (nf > 0) { ss << ", faces=" << nf; any = true; }
        if (nv > 0) { ss << ", vertices=" << nv; any = true; }
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        const int np = selection_manager_.selected_count(
            cloud, SelectionElementType::PointCloudPoint);
        if (np > 0) { ss << ", points=" << np; any = true; }
    }
    if (!any)
        ss << ", none";
    return ss.str();
}

// has_current_selection / select_scalar_range / clear_current_selection /
// request_delete_selection_confirmation / extract_selection / delete_selection
// moved to selection_queries.cpp.


void MainWindow::update_selection_overlays() {
    int rev = selection_manager_.revision();
    auto* model = viewer_.current_model();

    if (!interaction_overlay_.selection_visible) {
        if (interaction_overlay_.selection_faces ||
            interaction_overlay_.selection_vertices ||
            interaction_overlay_.selection_points)
            clear_selection_overlays();
        selection_revision_ = rev;
        return;
    }

    // Check if source model still alive
    if (interaction_overlay_.selection_source &&
        interaction_overlay_.selection_source != model) {
        if (!model_is_live(viewer_, interaction_overlay_.selection_source))
            clear_selection_overlays();
    }

    if (rev == selection_revision_ &&
        model == interaction_overlay_.selection_source) return;
    selection_revision_ = rev;

    clear_selection_overlays();
    if (!model) return;

    // --- Face overlay ---
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (mesh) {
        int nf = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceFace);
        if (nf > 0) {
            auto* sel = selection_manager_.get(mesh);
            auto* overlay = new easy3d::SurfaceMesh;
            overlay->set_name(mesh->name() + "_sel_faces");

            auto points = mesh->get_vertex_property<easy3d::vec3>("v:point");
            std::unordered_map<int, easy3d::SurfaceMesh::Vertex> vmap;
            vmap.reserve(nf * 3 / 2);

            for (auto f : mesh->faces()) {
                if (!sel->surface_faces[f.idx()]) continue;
                std::vector<easy3d::SurfaceMesh::Vertex> fv;
                for (auto v : mesh->vertices(f)) {
                    auto it = vmap.find(v.idx());
                    if (it == vmap.end()) {
                        auto nv = overlay->add_vertex(points[v]);
                        vmap[v.idx()] = nv;
                        fv.push_back(nv);
                    } else {
                        fv.push_back(it->second);
                    }
                }
                if (fv.size() >= 3)
                    overlay->add_face(fv);
            }

            // add_model creates the renderer; configure drawables AFTER
            auto* saved = viewer_.current_model();
            viewer_.add_model(overlay);
            viewer_.register_model_tree_overlay(overlay, mesh,
                mesh->name() + "_sel");
            viewer_.set_current_model_silent(saved);

            if (auto* fd = overlay->renderer()->get_triangles_drawable("faces")) {
                fd->set_uniform_coloring(easy3d::vec4(0.25f, 0.90f, 0.25f, 0.45f));
                fd->set_lighting(false);
                fd->set_distinct_back_color(false);
            }
            if (auto* ed = overlay->renderer()->get_lines_drawable("edges"))
                ed->set_visible(false);
            overlay->renderer()->update();

            interaction_overlay_.selection_faces = overlay;
            interaction_overlay_.selection_source = model;
        }

        // --- Vertex overlay ---
        int nv = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceVertex);
        if (nv > 0) {
            auto* sel = selection_manager_.get(mesh);
            auto* overlay = new easy3d::Graph;
            overlay->set_name(mesh->name() + "_sel_verts");
            auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
            for (auto v : mesh->vertices())
                if (sel->surface_vertices[v.idx()])
                    overlay->add_vertex(pts[v]);

            auto* saved = viewer_.current_model();
            viewer_.add_model(overlay);
            viewer_.register_model_tree_overlay(overlay, mesh,
                mesh->name() + "_sel");
            viewer_.set_current_model_silent(saved);

            if (auto* vd = overlay->renderer()->get_points_drawable("vertices", false)) {
                vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.78f, 0.20f, 1.0f));
                vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
                vd->set_point_size(8.0f);
                vd->update();
            }
            overlay->renderer()->update();
            interaction_overlay_.selection_vertices = overlay;
            interaction_overlay_.selection_source = model;
        }
    }

    // --- Point cloud overlay ---
    auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
    if (cloud) {
        int np = selection_manager_.selected_count(cloud, SelectionElementType::PointCloudPoint);
        if (np > 0) {
            auto* sel = selection_manager_.get(cloud);
            auto* overlay = new easy3d::PointCloud;
            overlay->set_name(cloud->name() + "_sel_points");
            auto pts = cloud->get_vertex_property<easy3d::vec3>("v:point");
            for (auto v : cloud->vertices())
                if (sel->pointcloud_points[v.idx()])
                    overlay->add_vertex(pts[v]);

            auto* saved = viewer_.current_model();
            viewer_.add_model(overlay);
            viewer_.register_model_tree_overlay(overlay, cloud,
                cloud->name() + "_sel");
            viewer_.set_current_model_silent(saved);

            if (auto* vd = overlay->renderer()->get_points_drawable("vertices")) {
                vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.78f, 0.20f, 1.0f));
                vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
                vd->set_point_size(6.0f);
                vd->update();
            }
            overlay->renderer()->update();
            interaction_overlay_.selection_points = overlay;
            interaction_overlay_.selection_source = model;
        }
    }
}

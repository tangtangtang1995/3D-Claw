// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Selection query, scalar-range selection, and clear/confirmation helpers.
// Extracted out of main_window.cpp during the 2024-2025 split.

#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "selection/selection_manager.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <string>


bool MainWindow::has_current_selection() {
    auto* model = viewer_.current_model();
    if (!model)
        return false;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        return selection_manager_.selected_count(mesh, SelectionElementType::SurfaceFace) > 0 ||
               selection_manager_.selected_count(mesh, SelectionElementType::SurfaceVertex) > 0;
    }
    if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model))
        return selection_manager_.selected_count(cloud, SelectionElementType::PointCloudPoint) > 0;
    return false;
}

int MainWindow::select_scalar_range(const std::string& property_name,
                                         bool face_property,
                                         double min_value,
                                         double max_value,
                                         bool clear_first) {
    auto* model = viewer_.current_model();
    if (!model || property_name.empty())
        return 0;
    if (min_value > max_value)
        std::swap(min_value, max_value);

    auto in_range = [&](double v) {
        return v >= min_value && v <= max_value;
    };

    int selected = 0;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        auto* sel = selection_manager_.get(mesh);
        if (!sel)
            return 0;

        if (face_property) {
            if (clear_first)
                selection_manager_.clear_all_selections(mesh);
            auto apply = [&](auto prop) {
                int count = 0;
                for (auto f : mesh->faces()) {
                    if (in_range((double)prop[f])) {
                        sel->surface_faces[f.idx()] = 1;
                        ++count;
                    }
                }
                return count;
            };
            if (auto p = mesh->get_face_property<float>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_face_property<double>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_face_property<int>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_face_property<unsigned int>(property_name)) selected = apply(p);
            else return 0;
            selection_mode_ = SelectionMode::PickSurfaceFace;
        } else {
            if (clear_first)
                selection_manager_.clear_all_selections(mesh);
            auto apply = [&](auto prop) {
                int count = 0;
                for (auto v : mesh->vertices()) {
                    if (in_range((double)prop[v])) {
                        sel->surface_vertices[v.idx()] = 1;
                        ++count;
                    }
                }
                return count;
            };
            if (auto p = mesh->get_vertex_property<float>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_vertex_property<double>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_vertex_property<int>(property_name)) selected = apply(p);
            else if (auto p = mesh->get_vertex_property<unsigned int>(property_name)) selected = apply(p);
            else return 0;
            selection_mode_ = SelectionMode::PickSurfaceVertex;
        }
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        if (face_property)
            return 0;
        auto* sel = selection_manager_.get(cloud);
        if (!sel)
            return 0;
        if (clear_first)
            selection_manager_.clear_all_selections(cloud);
        auto apply = [&](auto prop) {
            int count = 0;
            for (auto v : cloud->vertices()) {
                if (in_range((double)prop[v])) {
                    sel->pointcloud_points[v.idx()] = 1;
                    ++count;
                }
            }
            return count;
        };
        if (auto p = cloud->get_vertex_property<float>(property_name)) selected = apply(p);
        else if (auto p = cloud->get_vertex_property<double>(property_name)) selected = apply(p);
        else if (auto p = cloud->get_vertex_property<int>(property_name)) selected = apply(p);
        else if (auto p = cloud->get_vertex_property<unsigned int>(property_name)) selected = apply(p);
        else return 0;
        selection_mode_ = SelectionMode::PickPointCloudPoint;
    }

    if (selected > 0 || clear_first) {
        selection_manager_.bump_revision();
        viewer_.mark_dirty();
    }
    LOG(INFO) << "selected by scalar range: " << selected
              << " element(s), property=" << property_name
              << ", range=[" << min_value << ", " << max_value << "]";
    return selected;
}

void MainWindow::clear_current_selection() {
    auto* model = viewer_.current_model();
    if (!model)
        return;
    selection_manager_.clear_all_selections(model);
    selection_manager_.bump_revision();
    viewer_.mark_dirty();
    LOG(INFO) << "selection cleared for current model";
}

void MainWindow::request_delete_selection_confirmation() {
    dlg_confirm_delete_sel_ = true;
}

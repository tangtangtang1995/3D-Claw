// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Delete selected elements from the current model.

#include "window/main_window.h"
#include <model_health.h>
#include "viewport/viewport_canvas.h"
#include "selection/selection_manager.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <vector>

void MainWindow::delete_selection() {
    auto* model = viewer_.current_model();
    if (!model) return;

    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        auto* sel = selection_manager_.get(mesh);
        if (!sel) return;
        int nf = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceFace);
        int nv = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceVertex);
        int removed_f = 0, removed_v = 0;
        if (nf > 0) {
            std::vector<easy3d::SurfaceMesh::Face> doomed;
            doomed.reserve(nf);
            for (auto f : mesh->faces())
                if (sel->surface_faces[f.idx()]) doomed.push_back(f);
            for (auto f : doomed) {
                mesh->delete_face(f);
                ++removed_f;
            }
            mesh->collect_garbage();
        } else if (nv > 0) {
            std::vector<easy3d::SurfaceMesh::Vertex> doomed;
            doomed.reserve(nv);
            for (auto v : mesh->vertices())
                if (sel->surface_vertices[v.idx()]) doomed.push_back(v);
            for (auto v : doomed) {
                mesh->delete_vertex(v);
                ++removed_v;
            }
            mesh->collect_garbage();
        } else {
            return;
        }
        selection_manager_.clear_all_selections(mesh);
        selection_manager_.bump_revision();
        ModelHealthRegistry::instance().invalidate(mesh);
        mesh->renderer()->update();
        viewer_.mark_dirty();
        LOG(INFO) << "deleted from " << mesh->name() << ": "
                  << removed_f << " face(s), " << removed_v << " vertex/vertices";
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        auto* sel = selection_manager_.get(cloud);
        if (!sel) return;
        int np = selection_manager_.selected_count(cloud, SelectionElementType::PointCloudPoint);
        if (np <= 0) return;
        std::vector<easy3d::PointCloud::Vertex> doomed;
        doomed.reserve(np);
        for (auto v : cloud->vertices())
            if (sel->pointcloud_points[v.idx()]) doomed.push_back(v);
        for (auto v : doomed) cloud->delete_vertex(v);
        cloud->collect_garbage();
        selection_manager_.clear_all_selections(cloud);
        selection_manager_.bump_revision();
        ModelHealthRegistry::instance().invalidate(cloud);
        cloud->renderer()->update();
        viewer_.mark_dirty();
        LOG(INFO) << "deleted from " << cloud->name() << ": " << np << " point(s)";
    }
}

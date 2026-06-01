// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Extract selected elements into new child models.

#include "window/main_window.h"
#include "window/window_helpers.h"
#include "viewport/viewport_canvas.h"
#include "selection/selection_manager.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/logging.h>

#include <string>
#include <unordered_map>
#include <vector>

void MainWindow::extract_selection() {
    auto* model = viewer_.current_model();
    if (!model) return;

    auto add_extracted = [&](easy3d::Model* extracted) {
        if (!extracted)
            return;
        viewer_.add_model(extracted);
        apply_surface_mesh_texture(extracted);
        const auto* info = viewer_.model_tree_info(model);
        viewer_.register_model_tree_node(extracted, ModelTreeNodeInfo{
            info ? info->workspace_name : model->name(),
            extracted->name(),
            model,
            ModelTreeNodeKind::Reconstruction,
            true});
        viewer_.mark_dirty();
    };

    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (mesh) {
        int nf = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceFace);
        int nv = selection_manager_.selected_count(mesh, SelectionElementType::SurfaceVertex);
        if (nf > 0) {
            auto* sel = selection_manager_.get(mesh);
            auto* extracted = new easy3d::SurfaceMesh;
            extracted->set_name(model->name() + "_sel_faces");

            auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
            SurfaceMeshAttributeCopier attrs(mesh, extracted);
            std::unordered_map<int, easy3d::SurfaceMesh::Vertex> vmap;

            for (auto f : mesh->faces()) {
                if (!sel->surface_faces[f.idx()]) continue;
                std::vector<easy3d::SurfaceMesh::Vertex> fv;
                for (auto v : mesh->vertices(f)) {
                    auto it = vmap.find(v.idx());
                    if (it == vmap.end()) {
                        auto nv = extracted->add_vertex(pts[v]);
                        attrs.copy_vertex(v, nv);
                        vmap[v.idx()] = nv;
                        fv.push_back(nv);
                    } else {
                        fv.push_back(it->second);
                    }
                }
                if (fv.size() >= 3) {
                    auto nf_new = extracted->add_face(fv);
                    attrs.copy_face(f, nf_new);
                }
            }

            add_extracted(extracted);
            LOG(INFO) << "extracted: " << extracted->n_faces() << " faces from selected " << nf;
        }
        if (nv > 0 && nf == 0) {
            auto* sel = selection_manager_.get(mesh);
            auto* extracted = new easy3d::PointCloud;
            extracted->set_name(model->name() + "_sel_verts");
            auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
            auto sn = mesh->get_vertex_property<easy3d::vec3>("v:normal");
            auto sc = mesh->get_vertex_property<easy3d::vec3>("v:color");
            auto st = mesh->get_vertex_property<easy3d::vec2>("v:texcoord");
            auto dn = sn ? extracted->add_vertex_property<easy3d::vec3>("v:normal")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
            auto dc = sc ? extracted->add_vertex_property<easy3d::vec3>("v:color")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
            auto dt = st ? extracted->add_vertex_property<easy3d::vec2>("v:texcoord")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec2>();
            for (auto v : mesh->vertices()) {
                if (!sel->surface_vertices[v.idx()])
                    continue;
                auto nv_new = extracted->add_vertex(pts[v]);
                if (sn) dn[nv_new] = sn[v];
                if (sc) dc[nv_new] = sc[v];
                if (st) dt[nv_new] = st[v];
            }

            add_extracted(extracted);
            LOG(INFO) << "extracted: " << extracted->n_vertices() << " vertices from selected " << nv;
        }
    }

    auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
    if (cloud) {
        int np = selection_manager_.selected_count(cloud, SelectionElementType::PointCloudPoint);
        if (np > 0) {
            auto* sel = selection_manager_.get(cloud);
            auto* extracted = new easy3d::PointCloud;
            extracted->set_name(model->name() + "_sel_points");
            auto pts = cloud->get_vertex_property<easy3d::vec3>("v:point");
            auto sn = cloud->get_vertex_property<easy3d::vec3>("v:normal");
            auto sc = cloud->get_vertex_property<easy3d::vec3>("v:color");
            auto st = cloud->get_vertex_property<easy3d::vec2>("v:texcoord");
            auto dn = sn ? extracted->add_vertex_property<easy3d::vec3>("v:normal")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
            auto dc = sc ? extracted->add_vertex_property<easy3d::vec3>("v:color")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
            auto dt = st ? extracted->add_vertex_property<easy3d::vec2>("v:texcoord")
                         : easy3d::PointCloud::VertexProperty<easy3d::vec2>();
            for (auto v : cloud->vertices()) {
                if (!sel->pointcloud_points[v.idx()])
                    continue;
                auto nv_new = extracted->add_vertex(pts[v]);
                if (sn) dn[nv_new] = sn[v];
                if (sc) dc[nv_new] = sc[v];
                if (st) dt[nv_new] = st[v];
            }

            add_extracted(extracted);
            LOG(INFO) << "extracted: " << extracted->n_vertices() << " points from selected " << np;
        }
    }
}

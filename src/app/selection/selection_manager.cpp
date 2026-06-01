// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "selection/selection_manager.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/poly_mesh.h>

namespace {

int count_faces(easy3d::Model* m) {
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) return sm->n_faces();
    return 0;
}
int count_vertices(easy3d::Model* m) {
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) return sm->n_vertices();
    if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m))  return pc->n_vertices();
    return 0;
}
int count_edges(easy3d::Model* m) {
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) return sm->n_edges();
    return 0;
}
int count_points(easy3d::Model* m) {
    if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) return pc->n_vertices();
    return 0;
}

} // namespace

bool PerModelSelection::empty() const {
    for (auto v : surface_vertices) if (v) return false;
    for (auto v : surface_faces)    if (v) return false;
    for (auto v : surface_edges)    if (v) return false;
    for (auto v : pointcloud_points) if (v) return false;
    return true;
}

void PerModelSelection::clear() {
    surface_vertices.clear();
    surface_faces.clear();
    surface_edges.clear();
    pointcloud_points.clear();
    geometry_revision = 0;
}

void SelectionManager::add_model(easy3d::Model* m) {
    if (selections_.find(m) != selections_.end()) return;
    PerModelSelection s;
    resize_if_needed(m, s);
    selections_[m] = std::move(s);
}

void SelectionManager::sync_with_viewer(const std::vector<easy3d::Model*>& models) {
    // Remove entries for models that no longer exist in the viewer
    std::vector<easy3d::Model*> to_remove;
    for (auto& kv : selections_) {
        bool found = false;
        for (auto* m : models) {
            if (m == kv.first) { found = true; break; }
        }
        if (!found) to_remove.push_back(kv.first);
    }
    for (auto* m : to_remove) selections_.erase(m);

    // Add entries for new models
    for (auto* m : models)
        if (selections_.find(m) == selections_.end())
            add_model(m);
}

void SelectionManager::remove_model(easy3d::Model* m) {
    selections_.erase(m);
}

PerModelSelection* SelectionManager::get(easy3d::Model* m) {
    auto it = selections_.find(m);
    if (it == selections_.end()) return nullptr;
    resize_if_needed(m, it->second);
    return &it->second;
}

const PerModelSelection* SelectionManager::get(easy3d::Model* m) const {
    auto it = selections_.find(m);
    if (it == selections_.end()) return nullptr;
    return &it->second;
}

void SelectionManager::clear_selection(easy3d::Model* m, SelectionElementType type) {
    auto* s = get(m);
    if (!s) return;
    switch (type) {
        case SelectionElementType::SurfaceVertex: std::fill(s->surface_vertices.begin(), s->surface_vertices.end(), 0); break;
        case SelectionElementType::SurfaceFace:   std::fill(s->surface_faces.begin(), s->surface_faces.end(), 0); break;
        case SelectionElementType::SurfaceEdge:   std::fill(s->surface_edges.begin(), s->surface_edges.end(), 0); break;
        case SelectionElementType::PointCloudPoint: std::fill(s->pointcloud_points.begin(), s->pointcloud_points.end(), 0); break;
        default: break;
    }
}

void SelectionManager::clear_all_selections(easy3d::Model* m) {
    auto* s = get(m);
    if (!s) return;
    std::fill(s->surface_vertices.begin(), s->surface_vertices.end(), 0);
    std::fill(s->surface_faces.begin(), s->surface_faces.end(), 0);
    std::fill(s->surface_edges.begin(), s->surface_edges.end(), 0);
    std::fill(s->pointcloud_points.begin(), s->pointcloud_points.end(), 0);
}

void SelectionManager::clear_all() {
    selections_.clear();
}

bool SelectionManager::is_selected(easy3d::Model* m, SelectionElementType type, int index) const {
    auto* s = get(m);
    if (!s) return false;
    switch (type) {
        case SelectionElementType::SurfaceVertex:  return index >= 0 && index < (int)s->surface_vertices.size() && s->surface_vertices[index];
        case SelectionElementType::SurfaceFace:    return index >= 0 && index < (int)s->surface_faces.size()    && s->surface_faces[index];
        case SelectionElementType::SurfaceEdge:    return index >= 0 && index < (int)s->surface_edges.size()    && s->surface_edges[index];
        case SelectionElementType::PointCloudPoint: return index >= 0 && index < (int)s->pointcloud_points.size() && s->pointcloud_points[index];
        default: return false;
    }
}

void SelectionManager::set_selected(easy3d::Model* m, SelectionElementType type, int index, bool sel) {
    auto* s = get(m);
    if (!s) return;
    std::vector<uint8_t>* vec = nullptr;
    switch (type) {
        case SelectionElementType::SurfaceVertex:  vec = &s->surface_vertices; break;
        case SelectionElementType::SurfaceFace:    vec = &s->surface_faces; break;
        case SelectionElementType::SurfaceEdge:    vec = &s->surface_edges; break;
        case SelectionElementType::PointCloudPoint: vec = &s->pointcloud_points; break;
        default: return;
    }
    if (index >= 0 && index < (int)vec->size())
        (*vec)[index] = sel ? 1 : 0;
    ++revision_;
}

void SelectionManager::toggle_selected(easy3d::Model* m, SelectionElementType type, int index) {
    set_selected(m, type, index, !is_selected(m, type, index));
}

int SelectionManager::selected_count(easy3d::Model* m, SelectionElementType type) const {
    auto* s = get(m);
    if (!s) return 0;
    const std::vector<uint8_t>* vec = nullptr;
    switch (type) {
        case SelectionElementType::SurfaceVertex:  vec = &s->surface_vertices; break;
        case SelectionElementType::SurfaceFace:    vec = &s->surface_faces; break;
        case SelectionElementType::SurfaceEdge:    vec = &s->surface_edges; break;
        case SelectionElementType::PointCloudPoint: vec = &s->pointcloud_points; break;
        default: return 0;
    }
    int n = 0;
    for (auto v : *vec) if (v) ++n;
    return n;
}

bool SelectionManager::has_any_selection(easy3d::Model* m) const {
    auto* s = get(m);
    if (!s) return false;
    return !s->empty();
}

bool SelectionManager::has_any_selection() const {
    for (const auto& kv : selections_)
        if (!kv.second.empty()) return true;
    return false;
}

void SelectionManager::resize_if_needed(easy3d::Model* m, PerModelSelection& s) {
    int nv = count_vertices(m);
    int nf = count_faces(m);
    int ne = count_edges(m);
    int np = count_points(m);

    if ((int)s.surface_vertices.size() != nv) s.surface_vertices.assign(nv, 0);
    if ((int)s.surface_faces.size()    != nf) s.surface_faces.assign(nf, 0);
    if ((int)s.surface_edges.size()    != ne) s.surface_edges.assign(ne, 0);
    if ((int)s.pointcloud_points.size() != np) s.pointcloud_points.assign(np, 0);
}

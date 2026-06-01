// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "model_health.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/algo/surface_mesh_components.h>

#include <chrono>
#include <memory>
#include <sstream>

namespace {

// Walk boundary halfedges to count distinct boundary loops.
int count_boundary_loops(easy3d::SurfaceMesh* m) {
    using Halfedge = easy3d::SurfaceMesh::Halfedge;
    auto visited = m->add_halfedge_property<bool>("h:health_visited_loop", false);
    int loops = 0;
    for (auto h : m->halfedges()) {
        if (visited[h]) continue;
        if (!m->is_border(h)) continue;
        // walk the loop
        Halfedge start = h;
        Halfedge cur = start;
        do {
            visited[cur] = true;
            cur = m->next(cur);
        } while (cur.is_valid() && cur != start);
        ++loops;
    }
    m->remove_halfedge_property(visited);
    return loops;
}

float estimate_memory_mb(easy3d::SurfaceMesh* m) {
    // Per-vertex: position (vec3) + maybe normal/color = 32 bytes baseline.
    // Per-halfedge: connectivity ~16 bytes; 2 halfedges per edge.
    // Per-face: connectivity + indices ~24 bytes.
    double bytes = double(m->n_vertices()) * 32.0
                 + double(m->n_edges())    * 32.0   // 2 halfedges * 16
                 + double(m->n_faces())    * 24.0;
    return float(bytes / (1024.0 * 1024.0));
}

float estimate_memory_mb(easy3d::PointCloud* p) {
    double bytes = double(p->n_vertices()) * 32.0;
    return float(bytes / (1024.0 * 1024.0));
}

float estimate_memory_mb(easy3d::Graph* g) {
    double bytes = double(g->n_vertices()) * 32.0
                 + double(g->n_edges())    * 16.0;
    return float(bytes / (1024.0 * 1024.0));
}

void compute_surface_mesh(easy3d::SurfaceMesh* m, ModelHealth& h) {
    h.has_vertex_normals = (bool)m->get_vertex_property<easy3d::vec3>("v:normal");
    h.has_vertex_colors  = (bool)m->get_vertex_property<easy3d::vec3>("v:color");
    h.has_face_colors    = (bool)m->get_face_property<easy3d::vec3>("f:color");
    h.has_uv = (bool)m->get_vertex_property<easy3d::vec2>("v:texcoord")
            || (bool)m->get_halfedge_property<easy3d::vec2>("h:texcoord");

    int b_edges = 0;
    for (auto e : m->edges())
        if (m->is_border(e)) ++b_edges;
    h.boundary_edges = b_edges;
    h.boundary_loops = count_boundary_loops(m);

    int degen = 0;
    for (auto f : m->faces())
        if (m->is_degenerate(f)) ++degen;
    h.degenerate_faces = degen;

    int iso = 0, nm = 0;
    for (auto v : m->vertices()) {
        if (m->is_isolated(v)) ++iso;
        else if (!m->is_manifold(v)) ++nm;
    }
    h.isolated_vertices = iso;
    h.non_manifold_vertices = nm;

    auto comps = easy3d::SurfaceMeshComponent::extract(m, false);
    h.connected_components = (int)comps.size();

    h.estimated_memory_mb = estimate_memory_mb(m);
}

void compute_point_cloud(easy3d::PointCloud* p, ModelHealth& h) {
    h.has_vertex_normals = (bool)p->get_vertex_property<easy3d::vec3>("v:normal");
    h.has_vertex_colors  = (bool)p->get_vertex_property<easy3d::vec3>("v:color");
    h.estimated_memory_mb = estimate_memory_mb(p);
    // Connected-components / boundary concepts don't apply to a raw cloud.
    h.connected_components = 1;
}

void compute_graph(easy3d::Graph* g, ModelHealth& h) {
    h.has_vertex_colors = (bool)g->get_vertex_property<easy3d::vec3>("v:color");
    h.estimated_memory_mb = estimate_memory_mb(g);
    // Connected components for a graph would need BFS; skip for the MVP.
    h.connected_components = 1;
}

} // namespace


ModelHealthRegistry& ModelHealthRegistry::instance() {
    static ModelHealthRegistry s;
    return s;
}


const ModelHealth* ModelHealthRegistry::get(easy3d::Model* m) const {
    if (!m) return nullptr;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = cache_.find(m);
    return (it == cache_.end()) ? nullptr : it->second.get();
}


void ModelHealthRegistry::request_compute(easy3d::Model* m) {
    if (!m) return;

    ModelHealth* h = nullptr;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(m);
        if (it == cache_.end()) {
            auto p = std::make_unique<ModelHealth>();
            h = p.get();
            cache_.emplace(m, std::move(p));
        } else {
            h = it->second.get();
        }
        int expected = ModelHealth::NotComputed;
        if (!h->state.compare_exchange_strong(expected, ModelHealth::Computing)) {
            // Already Ready / Failed -- nothing to do.
            return;
        }
    }

    // Keep this on the UI thread. The first async implementation captured a
    // raw Model* and also touched mesh properties from a worker thread, which
    // could race with deletion, crop/delete-selection, rendering, or algorithms.
    // Health checks are user-triggered and cheap enough for the MVP.
    auto t0 = std::chrono::steady_clock::now();
    try {
        if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
            compute_surface_mesh(sm, *h);
        else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m))
            compute_point_cloud(pc, *h);
        else if (auto* g = dynamic_cast<easy3d::Graph*>(m))
            compute_graph(g, *h);
        h->compute_time_sec = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - t0).count();
        h->state.store(ModelHealth::Ready);
    } catch (const std::exception& e) {
        h->error_message = e.what();
        h->state.store(ModelHealth::Failed);
    } catch (...) {
        h->error_message = "unknown exception";
        h->state.store(ModelHealth::Failed);
    }
}


void ModelHealthRegistry::invalidate(easy3d::Model* m) {
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    cache_.erase(m);
}


void ModelHealthRegistry::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    cache_.clear();
}


std::string ModelHealthRegistry::format_for_ai(easy3d::Model* m) const {
    auto* h = get(m);
    if (!h) return {};
    if (h->state.load() != ModelHealth::Ready) return {};
    std::ostringstream os;
    os << "Boundary edges: " << h->boundary_edges
       << " (in " << h->boundary_loops << " loop(s))\n"
       << "Degenerate faces: " << h->degenerate_faces << "\n"
       << "Isolated vertices: " << h->isolated_vertices << "\n"
       << "Non-manifold vertices: " << h->non_manifold_vertices << "\n"
       << "Connected components: " << h->connected_components << "\n"
       << "Memory estimate: ~" << h->estimated_memory_mb << " MB";
    return os.str();
}

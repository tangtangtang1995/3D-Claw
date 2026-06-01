// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_ALGORITHM_MESH_BRIDGE_H
#define CLAW3D_SERVICES_ALGORITHM_MESH_BRIDGE_H

/// Conversion helpers between Easy3D models and backend algorithm DTOs.

#include <easy3d/core/surface_mesh.h>

#include <memory>
#include <string>
#include <vector>

namespace claw3d::services {

/// Converts an Easy3D surface mesh into compact vertex/triangle POD arrays.
template <typename Point3dT, typename TriangleT>
bool surface_mesh_to_triangle_pod(easy3d::SurfaceMesh* mesh,
                                  std::vector<Point3dT>& out_vertices,
                                  std::vector<TriangleT>& out_triangles,
                                  int* dropped_non_triangles = nullptr)
{
    out_vertices.clear();
    out_triangles.clear();
    if (dropped_non_triangles)
        *dropped_non_triangles = 0;
    if (!mesh)
        return false;

    auto points = mesh->get_vertex_property<easy3d::vec3>("v:point");
    if (!points)
        return false;

    const int id_capacity = static_cast<int>(mesh->vertices_size());
    std::vector<int> vertex_to_compact(id_capacity, -1);

    out_vertices.reserve(mesh->n_vertices());
    int compact = 0;
    for (auto v : mesh->vertices()) {
        const int id = static_cast<int>(v.idx());
        if (id < 0 || id >= id_capacity)
            continue;
        const auto& p = points[v];
        vertex_to_compact[id] = compact++;
        out_vertices.push_back({static_cast<double>(p.x),
                                static_cast<double>(p.y),
                                static_cast<double>(p.z)});
    }

    out_triangles.reserve(mesh->n_faces());
    for (auto f : mesh->faces()) {
        int idx[3]{-1, -1, -1};
        int k = 0;
        bool valid_triangle = true;
        for (auto v : mesh->vertices(f)) {
            if (k >= 3) {
                valid_triangle = false;
                break;
            }
            const int id = static_cast<int>(v.idx());
            if (id < 0 || id >= id_capacity || vertex_to_compact[id] < 0) {
                valid_triangle = false;
                break;
            }
            idx[k++] = vertex_to_compact[id];
        }
        if (valid_triangle && k == 3) {
            out_triangles.push_back({idx[0], idx[1], idx[2]});
        } else if (dropped_non_triangles) {
            ++(*dropped_non_triangles);
        }
    }

    return !out_vertices.empty() && !out_triangles.empty();
}

/// Builds an Easy3D surface mesh from contract POD vertex/triangle arrays.
template <typename Point3dT, typename TriangleT>
std::unique_ptr<easy3d::SurfaceMesh> triangle_pod_to_surface_mesh(
    const std::vector<Point3dT>& vertices,
    const std::vector<TriangleT>& triangles,
    const std::string& name,
    bool force_triangle_soup = false)
{
    auto mesh = std::make_unique<easy3d::SurfaceMesh>();
    mesh->set_name(name);

    const int nv = static_cast<int>(vertices.size());
    if (force_triangle_soup) {
        for (const auto& t : triangles) {
            if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
                t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) {
                continue;
            }
            const auto& p0 = vertices[t.v0];
            const auto& p1 = vertices[t.v1];
            const auto& p2 = vertices[t.v2];
            auto a = mesh->add_vertex(easy3d::vec3(
                static_cast<float>(p0.x),
                static_cast<float>(p0.y),
                static_cast<float>(p0.z)));
            auto b = mesh->add_vertex(easy3d::vec3(
                static_cast<float>(p1.x),
                static_cast<float>(p1.y),
                static_cast<float>(p1.z)));
            auto c = mesh->add_vertex(easy3d::vec3(
                static_cast<float>(p2.x),
                static_cast<float>(p2.y),
                static_cast<float>(p2.z)));
            mesh->add_triangle(a, b, c);
        }
        return mesh;
    }

    std::vector<easy3d::SurfaceMesh::Vertex> handles;
    handles.reserve(nv);
    for (const auto& p : vertices) {
        handles.push_back(mesh->add_vertex(easy3d::vec3(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z))));
    }

    for (const auto& t : triangles) {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0 ||
            t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) {
            continue;
        }
        mesh->add_triangle(handles[t.v0], handles[t.v1], handles[t.v2]);
    }

    return mesh;
}

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_ALGORITHM_MESH_BRIDGE_H

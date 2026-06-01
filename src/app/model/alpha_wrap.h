// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALPHA_WRAP_H
#define CLAW3D_ALPHA_WRAP_H

/// Legacy application include for Alpha Wrap model-level helpers.

#include "common/alpha_wrap_contract.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/surface_mesh_builder.h>

#include <unordered_map>
#include <vector>

inline void extract_to_pod(const easy3d::PointCloud* cloud,
                           std::vector<AW3_Point3d>& points) {
    points.clear();
    if (!cloud)
        return;
    points.reserve(cloud->n_vertices());
    for (auto v : cloud->vertices()) {
        const auto& p = cloud->position(v);
        points.push_back({p.x, p.y, p.z});
    }
}

inline void extract_to_pod(const easy3d::SurfaceMesh* mesh,
                           std::vector<AW3_Point3d>& points,
                           std::vector<AW3_Triangle>& faces) {
    points.clear();
    faces.clear();
    if (!mesh)
        return;

    std::unordered_map<int, int> vertex_map;
    vertex_map.reserve(mesh->n_vertices());
    points.reserve(mesh->n_vertices());
    for (auto v : mesh->vertices()) {
        const auto& p = mesh->position(v);
        vertex_map[v.idx()] = static_cast<int>(points.size());
        points.push_back({p.x, p.y, p.z});
    }

    faces.reserve(mesh->n_faces());
    for (auto f : mesh->faces()) {
        std::vector<int> ids;
        ids.reserve(3);
        for (auto v : mesh->vertices(f)) {
            const auto it = vertex_map.find(v.idx());
            if (it != vertex_map.end())
                ids.push_back(it->second);
        }
        if (ids.size() == 3)
            faces.push_back({ids[0], ids[1], ids[2]});
    }
}

inline easy3d::SurfaceMesh* convert_pod_to_easy3d(const std::vector<AW3_Point3d>& points,
                                                  const std::vector<AW3_Triangle>& faces) {
    auto* mesh = new easy3d::SurfaceMesh;
    easy3d::SurfaceMeshBuilder builder(mesh);
    builder.begin_surface();

    std::vector<easy3d::SurfaceMesh::Vertex> vertices;
    vertices.reserve(points.size());
    for (const auto& p : points)
        vertices.push_back(builder.add_vertex(easy3d::vec3(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z))));

    for (const auto& f : faces) {
        if (f.v0 < 0 || f.v1 < 0 || f.v2 < 0 ||
            f.v0 >= static_cast<int>(vertices.size()) ||
            f.v1 >= static_cast<int>(vertices.size()) ||
            f.v2 >= static_cast<int>(vertices.size()))
            continue;
        builder.add_face({vertices[f.v0], vertices[f.v1], vertices[f.v2]});
    }

    builder.end_surface(false);
    return mesh;
}

#endif // CLAW3D_ALPHA_WRAP_H

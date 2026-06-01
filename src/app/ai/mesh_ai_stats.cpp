// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/mesh_ai_stats.h"

#include "ai/ai_prompt_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/surface_mesh.h>

#include <cmath>
#include <iomanip>
#include <sstream>

namespace claw_ai {

SurfaceMeshAIStats collect_surface_mesh_ai_stats(easy3d::SurfaceMesh* mesh) {
    SurfaceMeshAIStats m;
    if (!mesh)
        return m;

    m.name = ascii_only(mesh->name());
    m.vertices = static_cast<int>(mesh->n_vertices());
    m.edges = static_cast<int>(mesh->n_edges());
    m.faces = static_cast<int>(mesh->n_faces());
    m.triangle_mesh = mesh->is_triangle_mesh();
    m.closed = mesh->is_closed();

    const auto& bbox = mesh->bounding_box();
    m.bbox_valid = bbox.is_valid();
    if (m.bbox_valid) {
        m.bbox_diag = static_cast<double>(bbox.diagonal_length());
        m.bbox_min[0] = bbox.min_point().x;
        m.bbox_min[1] = bbox.min_point().y;
        m.bbox_min[2] = bbox.min_point().z;
        m.bbox_max[0] = bbox.max_point().x;
        m.bbox_max[1] = bbox.max_point().y;
        m.bbox_max[2] = bbox.max_point().z;
    }

    auto points = mesh->get_vertex_property<easy3d::vec3>("v:point");
    double edge_sum = 0.0;
    int edge_len_count = 0;
    for (auto e : mesh->edges()) {
        if (mesh->is_border(e))
            ++m.boundary_edges;
        if (points) {
            const auto v0 = mesh->vertex(e, 0);
            const auto v1 = mesh->vertex(e, 1);
            const auto& p0 = points[v0];
            const auto& p1 = points[v1];
            const double dx = static_cast<double>(p0.x) - static_cast<double>(p1.x);
            const double dy = static_cast<double>(p0.y) - static_cast<double>(p1.y);
            const double dz = static_cast<double>(p0.z) - static_cast<double>(p1.z);
            edge_sum += std::sqrt(dx * dx + dy * dy + dz * dz);
            ++edge_len_count;
        }
    }
    if (edge_len_count > 0)
        m.avg_edge_length = edge_sum / static_cast<double>(edge_len_count);

    for (auto v : mesh->vertices()) {
        if (mesh->is_border(v))
            ++m.boundary_vertices;
        const bool isolated = mesh->is_isolated(v);
        if (isolated)
            ++m.isolated_vertices;
        if (!isolated && !mesh->is_manifold(v))
            ++m.non_manifold_vertices;
    }

    for (auto f : mesh->faces()) {
        int degree = 0;
        for (auto v : mesh->vertices(f)) {
            (void)v;
            ++degree;
        }
        if (degree != 3)
            ++m.non_triangle_faces;
        if (mesh->is_degenerate(f))
            ++m.degenerate_faces;
    }

    try {
        const auto summary =
            claw3d::services::compute_surface_mesh_component_summary(mesh);
        m.connected_components = summary.connected_components;
        m.largest_component_faces = summary.largest_component_faces;
    } catch (...) {
        m.connected_components = -1;
    }
    return m;
}

void append_surface_mesh_metadata(std::ostringstream& oss,
                                  const SurfaceMeshAIStats& m,
                                  bool include_bbox_extents,
                                  bool include_closed)
{
    oss << "Source mesh metadata:\n";
    oss << "- Name: " << m.name << "\n";
    oss << "- Counts: vertices=" << m.vertices
        << " edges=" << m.edges << " faces=" << m.faces << "\n";
    oss << "- Triangle mesh: " << (m.triangle_mesh ? "yes" : "no");
    if (include_closed)
        oss << " | Closed: " << (m.closed ? "yes" : "no");
    oss << "\n";
    oss << "- Boundary edges: " << m.boundary_edges
        << " boundary vertices: " << m.boundary_vertices << "\n";
    oss << "- Connected components: ";
    if (m.connected_components >= 0)
        oss << m.connected_components
            << " largest_component_faces=" << m.largest_component_faces << "\n";
    else
        oss << "unknown\n";
    oss << "- Non-triangle faces: " << m.non_triangle_faces
        << " degenerate faces: " << m.degenerate_faces
        << " isolated vertices: " << m.isolated_vertices
        << " non-manifold vertices: " << m.non_manifold_vertices << "\n";

    oss << std::fixed << std::setprecision(6);
    if (m.bbox_valid) {
        oss << "- BBox diagonal: " << m.bbox_diag << "\n";
        if (include_bbox_extents) {
            oss << "- BBox min: (" << m.bbox_min[0] << ", " << m.bbox_min[1]
                << ", " << m.bbox_min[2] << ")\n";
            oss << "- BBox max: (" << m.bbox_max[0] << ", " << m.bbox_max[1]
                << ", " << m.bbox_max[2] << ")\n";
        }
    } else if (include_bbox_extents) {
        oss << "- BBox: invalid\n";
    }
    oss << "- Average edge length: " << m.avg_edge_length << "\n";
    oss.unsetf(std::ios::floatfield);
}

std::string build_surface_mesh_metadata_prompt(easy3d::SurfaceMesh* mesh,
                                               bool include_bbox_extents,
                                               bool include_closed)
{
    std::ostringstream oss;
    append_surface_mesh_metadata(
        oss, collect_surface_mesh_ai_stats(mesh),
        include_bbox_extents, include_closed);
    return ascii_only(oss.str());
}

} // namespace claw_ai

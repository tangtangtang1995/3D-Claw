// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MESH_AI_STATS_H
#define CLAW3D_MESH_AI_STATS_H

/// Mesh and point-cloud statistics formatted for AI context.

#include <iosfwd>
#include <string>

namespace easy3d { class SurfaceMesh; }

namespace claw_ai {

struct SurfaceMeshAIStats {
    std::string name;
    int vertices = 0;
    int edges = 0;
    int faces = 0;
    int boundary_edges = 0;
    int boundary_vertices = 0;
    int isolated_vertices = 0;
    int non_manifold_vertices = 0;
    int non_triangle_faces = 0;
    int degenerate_faces = 0;
    int connected_components = -1;
    int largest_component_faces = 0;
    double bbox_diag = 0.0;
    double bbox_min[3] = {0, 0, 0};
    double bbox_max[3] = {0, 0, 0};
    double avg_edge_length = 0.0;
    bool bbox_valid = false;
    bool triangle_mesh = false;
    bool closed = false;
};

SurfaceMeshAIStats collect_surface_mesh_ai_stats(easy3d::SurfaceMesh* mesh);
void append_surface_mesh_metadata(std::ostringstream& oss,
                                  const SurfaceMeshAIStats& stats,
                                  bool include_bbox_extents = true,
                                  bool include_closed = false);
std::string build_surface_mesh_metadata_prompt(
    easy3d::SurfaceMesh* mesh,
    bool include_bbox_extents = true,
    bool include_closed = false);

} // namespace claw_ai

#endif // CLAW3D_MESH_AI_STATS_H

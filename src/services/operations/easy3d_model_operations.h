// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_EASY3D_MODEL_OPERATIONS_H
#define CLAW3D_SERVICES_EASY3D_MODEL_OPERATIONS_H

/// Synchronous Easy3D model operations exposed through the services layer.

#include <cstddef>
#include <string>

namespace easy3d {
class Model;
class PointCloud;
class PolyMesh;
class SurfaceMesh;
}

namespace claw3d::services {

struct DistanceStats {
    float max = 0.0f;
    float mean = 0.0f;
    float rms = 0.0f;
    float stddev = 0.0f;
};

/// Minimal connected-component summary used by health and topology panels.
struct SurfaceMeshComponentSummary {
    int connected_components = -1;
    int largest_component_faces = 0;
};

/// Supported Easy3D subdivision algorithms.
enum class SubdivisionScheme {
    CatmullClark,
    Loop,
    Sqrt3,
};

/// Applies in-place noise to point, graph, or mesh vertex coordinates.
bool apply_gaussian_noise(easy3d::Model* model, float sigma);

/// Reorients point normals by local neighborhood consistency.
bool reorient_point_cloud_normals(easy3d::PointCloud* cloud, int neighbors);
/// Normalizes existing point-cloud normals in place.
bool normalize_point_cloud_normals(easy3d::PointCloud* cloud);

easy3d::SurfaceMesh* build_delaunay_xy_mesh(const easy3d::PointCloud* cloud);
easy3d::PolyMesh* build_delaunay_tetra_mesh(const easy3d::PointCloud* cloud);

bool compute_height_fields(easy3d::Model* model);
SurfaceMeshComponentSummary compute_surface_mesh_component_summary(
    easy3d::SurfaceMesh* mesh);
std::string build_topology_statistics_report(easy3d::SurfaceMesh* mesh,
                                             std::size_t max_components);

bool apply_surface_mesh_subdivision(easy3d::SurfaceMesh* mesh,
                                    SubdivisionScheme scheme);
bool apply_surface_mesh_stitching(easy3d::SurfaceMesh* mesh);
bool reverse_surface_mesh_orientation(easy3d::SurfaceMesh* mesh);
bool remove_isolated_vertices(easy3d::SurfaceMesh* mesh);

bool extract_connected_components(easy3d::SurfaceMesh* mesh);
bool apply_dual_mesh(easy3d::SurfaceMesh* mesh);
bool compute_planar_partition(easy3d::SurfaceMesh* mesh, float angle_degrees);
bool apply_polygonization(easy3d::SurfaceMesh* mesh);
bool apply_triangulation(easy3d::SurfaceMesh* mesh);
easy3d::PolyMesh* build_tetrahedralization(easy3d::SurfaceMesh* mesh);

bool compute_quick_vertex_zero_geodesic(easy3d::SurfaceMesh* mesh);

float compute_surface_area(easy3d::SurfaceMesh* mesh);
float compute_volume(easy3d::SurfaceMesh* mesh);

bool compute_point_cloud_mesh_distance(easy3d::PointCloud* source,
                                       easy3d::SurfaceMesh* target,
                                       DistanceStats& stats);
bool compute_point_cloud_distance(easy3d::PointCloud* source,
                                  easy3d::PointCloud* target,
                                  DistanceStats& stats);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_EASY3D_MODEL_OPERATIONS_H

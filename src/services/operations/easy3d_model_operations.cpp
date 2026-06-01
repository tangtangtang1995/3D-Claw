// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/operations/easy3d_model_operations.h"

#include "observable_surface_mesh_geodesic.h"

#include <easy3d/algo/delaunay_2d.h>
#include <easy3d/algo/delaunay_3d.h>
#include <easy3d/algo/gaussian_noise.h>
#include <easy3d/algo/point_cloud_normals.h>
#include <easy3d/algo/surface_mesh_components.h>
#include <easy3d/algo/surface_mesh_enumerator.h>
#include <easy3d/algo/surface_mesh_geometry.h>
#include <easy3d/algo/surface_mesh_polygonization.h>
#include <easy3d/algo/surface_mesh_stitching.h>
#include <easy3d/algo/surface_mesh_subdivision.h>
#include <easy3d/algo/surface_mesh_tetrahedralization.h>
#include <easy3d/algo/surface_mesh_topology.h>
#include <easy3d/algo/surface_mesh_triangulation.h>
#include <easy3d/algo/triangle_mesh_kdtree.h>
#include <easy3d/core/model.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/kdtree/kdtree_search_eth.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace claw3d::services {
namespace {

void set_stats_from_sums(float max_distance,
                         float sum_distance,
                         float sum_square_distance,
                         int count,
                         DistanceStats& stats)
{
    if (count <= 0) {
        stats = {};
        return;
    }
    stats.max = max_distance;
    stats.mean = sum_distance / static_cast<float>(count);
    stats.rms = std::sqrt(sum_square_distance / static_cast<float>(count));
    const float variance =
        (sum_square_distance / static_cast<float>(count)) - stats.mean * stats.mean;
    stats.stddev = std::sqrt(std::max(0.0f, variance));
}

} // namespace

bool apply_gaussian_noise(easy3d::Model* model, float sigma)
{
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        easy3d::GaussianNoise::apply(mesh, sigma);
        return true;
    }
    if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        easy3d::GaussianNoise::apply(cloud, sigma);
        return true;
    }
    return false;
}

bool reorient_point_cloud_normals(easy3d::PointCloud* cloud, int neighbors)
{
    if (!cloud || neighbors <= 0)
        return false;
    easy3d::PointCloudNormals::reorient(cloud, neighbors);
    return true;
}

bool normalize_point_cloud_normals(easy3d::PointCloud* cloud)
{
    if (!cloud)
        return false;
    auto normals = cloud->get_vertex_property<easy3d::vec3>("v:normal");
    if (!normals)
        return false;
    for (auto v : cloud->vertices())
        normals[v] = easy3d::normalize(normals[v]);
    return true;
}

easy3d::SurfaceMesh* build_delaunay_xy_mesh(const easy3d::PointCloud* cloud)
{
    if (!cloud)
        return nullptr;
    const auto& points = cloud->points();
    std::vector<easy3d::vec2> points2d;
    points2d.reserve(points.size());
    for (const auto& point : points)
        points2d.emplace_back(point.x, point.y);

    easy3d::Delaunay2 delaunay;
    delaunay.set_vertices(points2d);

    auto* mesh = new easy3d::SurfaceMesh;
    mesh->set_name(cloud->name() + ".delaunay-xy");
    for (std::size_t i = 0; i < points2d.size(); ++i)
        mesh->add_vertex(easy3d::vec3(points2d[i], points[i].z));
    for (unsigned int i = 0; i < delaunay.nb_triangles(); ++i) {
        std::vector<easy3d::SurfaceMesh::Vertex> vertices(3);
        for (int j = 0; j < 3; ++j)
            vertices[j] = easy3d::SurfaceMesh::Vertex(delaunay.tri_vertex(i, j));
        mesh->add_face(vertices);
    }
    return mesh;
}

easy3d::PolyMesh* build_delaunay_tetra_mesh(const easy3d::PointCloud* cloud)
{
    if (!cloud)
        return nullptr;
    easy3d::Delaunay3 delaunay;
    delaunay.set_vertices(cloud->points());

    auto* mesh = new easy3d::PolyMesh;
    mesh->set_name(cloud->name() + ".delaunay-3d");
    for (const auto& point : cloud->points())
        mesh->add_vertex(point);
    for (unsigned int i = 0; i < delaunay.nb_tets(); ++i) {
        std::vector<easy3d::PolyMesh::Vertex> vertices(4);
        for (int j = 0; j < 4; ++j)
            vertices[j] = easy3d::PolyMesh::Vertex(delaunay.tet_vertex(i, j));
        mesh->add_tetra(vertices[0], vertices[1], vertices[2], vertices[3]);
    }
    return mesh;
}

bool compute_height_fields(easy3d::Model* model)
{
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        auto height_x = mesh->vertex_property<float>("v:height_x");
        auto height_y = mesh->vertex_property<float>("v:height_y");
        auto height_z = mesh->vertex_property<float>("v:height_z");
        for (auto vertex : mesh->vertices()) {
            const auto position = mesh->position(vertex);
            height_x[vertex] = position.x;
            height_y[vertex] = position.y;
            height_z[vertex] = position.z;
        }
        return true;
    }
    if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        auto points = cloud->get_vertex_property<easy3d::vec3>("v:point");
        if (!points)
            return false;
        auto height_x = cloud->vertex_property<float>("v:height_x");
        auto height_y = cloud->vertex_property<float>("v:height_y");
        auto height_z = cloud->vertex_property<float>("v:height_z");
        for (auto vertex : cloud->vertices()) {
            const auto position = points[vertex];
            height_x[vertex] = position.x;
            height_y[vertex] = position.y;
            height_z[vertex] = position.z;
        }
        return true;
    }
    return false;
}

std::string build_topology_statistics_report(easy3d::SurfaceMesh* mesh,
                                             std::size_t max_components)
{
    if (!mesh)
        return {};
    std::stringstream stream;
    const auto& components = easy3d::SurfaceMeshComponent::extract(mesh);
    stream << "model has " << components.size() << " connected components" << std::endl;
    const std::size_t count = std::min(components.size(), max_components);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& component = components[i];
        easy3d::SurfaceMeshTopology topology(&component);
        std::string type = "unknown";
        if (topology.is_sphere())
            type = "sphere";
        else if (topology.is_disc())
            type = "disc";
        else if (topology.is_cylinder())
            type = "cylinder";
        else if (topology.is_torus())
            type = "torus";
        else if (topology.is_closed())
            type = "unknown closed";
        stream << "  " << i << ": " << type
               << ", F=" << component.n_faces()
               << ", V=" << component.n_vertices()
               << ", E=" << component.n_edges()
               << ", B=" << topology.number_of_borders() << std::endl;
    }
    return stream.str();
}

SurfaceMeshComponentSummary compute_surface_mesh_component_summary(
    easy3d::SurfaceMesh* mesh)
{
    SurfaceMeshComponentSummary summary;
    if (!mesh)
        return summary;
    const auto& components = easy3d::SurfaceMeshComponent::extract(mesh);
    summary.connected_components = static_cast<int>(components.size());
    for (const auto& component : components) {
        summary.largest_component_faces = std::max(
            summary.largest_component_faces,
            static_cast<int>(component.n_faces()));
    }
    return summary;
}

bool apply_surface_mesh_subdivision(easy3d::SurfaceMesh* mesh,
                                    SubdivisionScheme scheme)
{
    if (!mesh)
        return false;
    switch (scheme) {
    case SubdivisionScheme::CatmullClark:
        easy3d::SurfaceMeshSubdivision::catmull_clark(mesh);
        break;
    case SubdivisionScheme::Loop:
        easy3d::SurfaceMeshSubdivision::loop(mesh);
        break;
    case SubdivisionScheme::Sqrt3:
        easy3d::SurfaceMeshSubdivision::sqrt3(mesh);
        break;
    }
    return true;
}

bool apply_surface_mesh_stitching(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    easy3d::SurfaceMeshStitching stitch(mesh);
    stitch.apply();
    return true;
}

bool reverse_surface_mesh_orientation(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    mesh->reverse_orientation();
    return true;
}

bool remove_isolated_vertices(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    for (auto vertex : mesh->vertices()) {
        if (mesh->is_isolated(vertex))
            mesh->delete_vertex(vertex);
    }
    mesh->collect_garbage();
    return true;
}

bool extract_connected_components(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    easy3d::SurfaceMeshComponent::extract(mesh);
    return true;
}

bool apply_dual_mesh(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    easy3d::geom::dual(mesh);
    return true;
}

bool compute_planar_partition(easy3d::SurfaceMesh* mesh, float angle_degrees)
{
    if (!mesh)
        return false;
    auto planar_segments = mesh->face_property<int>("f:planar_partition", -1);
    easy3d::SurfaceMeshEnumerator::enumerate_planar_components(
        mesh, planar_segments, angle_degrees);
    return true;
}

bool apply_polygonization(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    easy3d::SurfaceMeshPolygonization polygonizer;
    polygonizer.apply(mesh);
    return true;
}

bool apply_triangulation(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return false;
    easy3d::SurfaceMeshTriangulation triangulation(mesh);
    triangulation.triangulate(easy3d::SurfaceMeshTriangulation::MIN_AREA);
    return true;
}

easy3d::PolyMesh* build_tetrahedralization(easy3d::SurfaceMesh* mesh)
{
    if (!mesh)
        return nullptr;
    easy3d::SurfaceMeshTetrehedralization tetrahedralization;
    auto* result = tetrahedralization.apply(mesh);
    if (result)
        result->set_name(mesh->name() + ".tetra");
    return result;
}

bool compute_quick_vertex_zero_geodesic(easy3d::SurfaceMesh* mesh)
{
    if (!mesh || mesh->n_vertices() == 0)
        return false;
    claw3d::algo::ObservableSurfaceMeshGeodesic geodesic(mesh);
    geodesic.compute({easy3d::SurfaceMesh::Vertex(0)});
    return true;
}

float compute_surface_area(easy3d::SurfaceMesh* mesh)
{
    return mesh ? easy3d::geom::surface_area(mesh) : 0.0f;
}

float compute_volume(easy3d::SurfaceMesh* mesh)
{
    return mesh ? easy3d::geom::volume(mesh) : 0.0f;
}

bool compute_point_cloud_mesh_distance(easy3d::PointCloud* source,
                                       easy3d::SurfaceMesh* target,
                                       DistanceStats& stats)
{
    if (!source || !target || source->n_vertices() <= 0)
        return false;

    easy3d::TriangleMeshKdTree tree(target);
    float max_distance = 0.0f;
    float sum_distance = 0.0f;
    float sum_square_distance = 0.0f;
    auto distance = source->vertex_property<float>("v:dist", 0.0f);
    for (auto vertex : source->vertices()) {
        const auto nearest = tree.nearest(source->position(vertex));
        max_distance = std::max(max_distance, nearest.dist);
        sum_distance += nearest.dist;
        sum_square_distance += nearest.dist * nearest.dist;
        distance[vertex] = nearest.dist;
    }
    set_stats_from_sums(max_distance,
                        sum_distance,
                        sum_square_distance,
                        source->n_vertices(),
                        stats);
    return true;
}

bool compute_point_cloud_distance(easy3d::PointCloud* source,
                                  easy3d::PointCloud* target,
                                  DistanceStats& stats)
{
    if (!source || !target || source->n_vertices() <= 0)
        return false;

    easy3d::KdTreeSearch_ETH tree(target);
    float max_distance = 0.0f;
    float sum_distance = 0.0f;
    float sum_square_distance = 0.0f;
    auto distance = source->vertex_property<float>("v:dist", 0.0f);
    for (auto vertex : source->vertices()) {
        float sq_distance = 0.0f;
        tree.find_closest_point(source->position(vertex), sq_distance);
        const float value = std::sqrt(sq_distance);
        max_distance = std::max(max_distance, value);
        sum_distance += value;
        sum_square_distance += value * value;
        distance[vertex] = value;
    }
    set_stats_from_sums(max_distance,
                        sum_distance,
                        sum_square_distance,
                        source->n_vertices(),
                        stats);
    return true;
}

} // namespace claw3d::services

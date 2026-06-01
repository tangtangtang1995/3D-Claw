// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_GEODESIC_CONTRACT_H
#define CLAW3D_COMMON_GEODESIC_CONTRACT_H

/// Typed DTO contract shared by geodesic-distance UI, services, and runners.

#include "common/preview_policy.h"

#include <cstdint>
#include <vector>

struct GEO_FrontSnapshot {
    int generation = 0;
    int visited_vertices = 0;
    int total_vertices = 0;
    int front_size = 0;
    float max_distance = 0.0f;
    float progress = 0.0f;
    std::vector<float> vertex_distances;
    std::vector<std::uint8_t> vertex_state;
};

struct GEO_FrontResultStats {
    int input_vertices = 0;
    int input_faces = 0;
    int source_count = 0;
    int visited_vertices = 0;
    float max_distance = 0.0f;
    float mean_distance = 0.0f;
    int peak_front_size = 0;
    bool cancelled = false;
    double ms_total = 0.0;

    bool path_found = false;
    float front_path_length = 0.0f;

    bool exact_path_found = false;
    float exact_path_length = 0.0f;
};

struct GEO_FrontConfig {
    bool use_virtual_edges = true;
    int preview_speed = 0;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    float max_dist = 1e30f;
    int target_vid = -1;
    bool compare_exact_path = false;
};

struct GEO_CGAL_Point3d {
    double x;
    double y;
    double z;
};

struct GEO_CGAL_Triangle {
    int v0;
    int v1;
    int v2;
};

struct GEO_CGAL_Source {
    int vertex_id = -1;
    int face_id = -1;
    double barycentric[3] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
};

struct GEO_CGAL_Target {
    int vertex_id = -1;
    int face_id = -1;
    double barycentric[3] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
};

struct GEO_CGAL_Config {
    bool use_robust_traits = false;
};

struct GEO_CGAL_PathResult {
    std::vector<GEO_CGAL_Point3d> points;
    double length = 0.0;
    double ms_build_tree = 0.0;
    double ms_query = 0.0;
    bool path_found = false;
};

enum GEO_CGAL_HeatVariant : int {
    GEO_CGAL_HEAT_Direct = 0,
    GEO_CGAL_HEAT_IntrinsicDelaunay = 1
};

struct GEO_CGAL_HeatResult {
    std::vector<double> vertex_distances;
    double min_distance = 0.0;
    double max_distance = 0.0;
    double mean_distance = 0.0;
    double ms_total = 0.0;
    bool cancelled = false;
};

#endif // CLAW3D_COMMON_GEODESIC_CONTRACT_H

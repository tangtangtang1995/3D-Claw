// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_SMOOTHING_CONTRACT_H
#define CLAW3D_COMMON_SMOOTHING_CONTRACT_H

/// Typed DTO contract shared by CGAL smoothing UI, services, and runner code.

#include "common/preview_policy.h"

#include <vector>

struct SMOOTH_Point3d {
    double x;
    double y;
    double z;
};

struct SMOOTH_Triangle {
    int v0;
    int v1;
    int v2;
};

enum SMOOTH_Mode : int {
    SMOOTH_MODE_TangentialRelaxation = 0,
    SMOOTH_MODE_AngleSmoothing = 1,
    SMOOTH_MODE_MeanCurvatureFlow = 2,
    SMOOTH_MODE_AngleArea = 3
};

enum SMOOTH_PreviewSpeed : int {
    SMOOTH_PREVIEW_Normal = 0,
    SMOOTH_PREVIEW_Slow = 1
};

struct SMOOTH_Config {
    int mode = SMOOTH_MODE_TangentialRelaxation;
    int iterations = 10;
    double time_step = 0.0001;
    bool preserve_boundary = true;
    bool preserve_sharp_edges = true;
    double sharp_angle_degrees = 60.0;
    bool relax_constraints = false;
    bool safety_constraints = true;
    bool project_to_original = true;
    bool rescale_after_smoothing = true;

    bool live_preview = false;
    int preview_speed = SMOOTH_PREVIEW_Normal;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int max_vectors = 500;
};

struct SMOOTH_QualityStats {
    int vertices = 0;
    int edges = 0;
    int faces = 0;
    int boundary_edges = 0;
    int sharp_edges = 0;
    int degenerate_faces = 0;
    int bad_triangles_10deg = 0;
    int bad_triangles_15deg = 0;
    double min_angle = 0.0;
    double mean_angle = 0.0;
    double mean_aspect_ratio = 0.0;
    double max_aspect_ratio = 0.0;
    double surface_area = 0.0;
    double volume = 0.0;
    double bbox_diag = 0.0;
    bool closed = false;
};

struct SMOOTH_Snapshot {
    int generation = 0;
    int iteration = 0;
    int total_iterations = 0;
    double progress = 0.0;
    double mean_displacement = 0.0;
    double max_displacement = 0.0;
    double surface_area_ratio = 1.0;
    double volume_ratio = 1.0;
    std::vector<SMOOTH_Point3d> vertices;
    std::vector<SMOOTH_Triangle> triangles;
    std::vector<double> vertex_displacement;
    std::vector<SMOOTH_Point3d> constrained_edge_endpoints;
    std::vector<SMOOTH_Point3d> vector_from;
    std::vector<SMOOTH_Point3d> vector_to;
    SMOOTH_QualityStats stats;
};

struct SMOOTH_ResultStats {
    SMOOTH_QualityStats before;
    SMOOTH_QualityStats after;
    int iterations_done = 0;
    bool cancelled = false;
    bool used_triangulated_copy = false;
    bool closed_mesh = false;
    double mean_displacement = 0.0;
    double max_displacement = 0.0;
    double surface_area_ratio = 1.0;
    double volume_ratio = 1.0;
    double ms_total = 0.0;
    double ms_preprocess = 0.0;
    double ms_smoothing = 0.0;
};

#endif // CLAW3D_COMMON_SMOOTHING_CONTRACT_H

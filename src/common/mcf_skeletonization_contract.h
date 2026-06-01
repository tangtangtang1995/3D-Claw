// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_MCF_SKELETONIZATION_CONTRACT_H
#define CLAW3D_COMMON_MCF_SKELETONIZATION_CONTRACT_H

/// Typed DTO contract shared by MCF skeletonization UI, services, and runner code.

#include "common/preview_policy.h"

#include <vector>

struct MCF_Point3d {
    double x;
    double y;
    double z;
};

struct MCF_Triangle {
    int v0;
    int v1;
    int v2;
};

struct MCF_Line {
    MCF_Point3d a;
    MCF_Point3d b;
};

enum MCF_PreviewSpeed : int {
    MCF_PREVIEW_Fast = 0,
    MCF_PREVIEW_Normal = 1,
    MCF_PREVIEW_Slow = 2
};

/// Domain-specific mean-curvature-flow skeletonization failure code.
enum class MCF_ErrorCode : int {
    None = 0,
    EmptyInput = 1,
    NotTriangleMesh = 2,
    NotClosed = 3,
    MultipleComponents = 4,
    BuildFailed = 5,
    AlgorithmException = 6,
    TooFewFaces = 7
};

constexpr MCF_ErrorCode MCF_ERR_None = MCF_ErrorCode::None;
constexpr MCF_ErrorCode MCF_ERR_EmptyInput = MCF_ErrorCode::EmptyInput;
constexpr MCF_ErrorCode MCF_ERR_NotTriangleMesh = MCF_ErrorCode::NotTriangleMesh;
constexpr MCF_ErrorCode MCF_ERR_NotClosed = MCF_ErrorCode::NotClosed;
constexpr MCF_ErrorCode MCF_ERR_MultipleComponents = MCF_ErrorCode::MultipleComponents;
constexpr MCF_ErrorCode MCF_ERR_BuildFailed = MCF_ErrorCode::BuildFailed;
constexpr MCF_ErrorCode MCF_ERR_AlgorithmException = MCF_ErrorCode::AlgorithmException;
constexpr MCF_ErrorCode MCF_ERR_TooFewFaces = MCF_ErrorCode::TooFewFaces;

inline int mcf_error_code_value(MCF_ErrorCode code) {
    return static_cast<int>(code);
}

struct MCF_Config {
    int max_iterations = 500;
    double area_variation_factor = 1e-4;
    double quality_speed_tradeoff = 0.1;
    double medially_centered_speed_tradeoff = 0.2;
    double min_edge_length = -1.0;
    double max_triangle_angle_degrees = 110.0;
    bool medially_centered = true;

    bool collect_correspondence = false;
    bool collect_final_meso = false;
    bool collect_sdf = false;
    int max_correspondence_lines = 3000;

    bool live_preview = false;
    int preview_speed = MCF_PREVIEW_Normal;
    int snapshot_every_iterations = 1;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int max_snapshot_vertices = 80000;
};

struct MCF_Metrics {
    int iteration = 0;
    int max_iterations = 0;
    int original_vertices = 0;
    int original_faces = 0;
    int meso_vertices = 0;
    int meso_faces = 0;
    int collapsed_edges = 0;
    int split_faces = 0;
    int fixed_vertices = 0;
    int skeleton_vertices = 0;
    int skeleton_edges = 0;
    double original_area = 0.0;
    double current_area = 0.0;
    double area_change_ratio = 0.0;
    double progress = 0.0;
    double elapsed_ms = 0.0;
    bool converged = false;
    bool cancelled = false;
};

struct MCF_Snapshot {
    int generation = 0;
    MCF_Metrics metrics;
    std::vector<MCF_Point3d> vertices;
    std::vector<MCF_Triangle> triangles;
    std::vector<int> fixed_vertex_indices;
};

struct MCF_Result {
    /// None means the result is usable; other values explain why it failed.
    MCF_ErrorCode error_code = MCF_ERR_None;
    char error_message[512] = {};
    MCF_Metrics metrics;
    std::vector<MCF_Point3d> skeleton_vertices;
    std::vector<MCF_Line> skeleton_edges;
    std::vector<MCF_Line> correspondence_lines;
    std::vector<double> sdf_per_input_vertex;
    std::vector<MCF_Point3d> final_meso_vertices;
    std::vector<MCF_Triangle> final_meso_triangles;
};

#endif // CLAW3D_COMMON_MCF_SKELETONIZATION_CONTRACT_H

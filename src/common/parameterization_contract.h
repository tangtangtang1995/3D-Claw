// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_PARAMETERIZATION_CONTRACT_H
#define CLAW3D_COMMON_PARAMETERIZATION_CONTRACT_H

/// Typed DTO contract shared by parameterization UI, services, and runner code.

#include <vector>

struct PARAM_Point3d {
    double x;
    double y;
    double z;
};

struct PARAM_Triangle {
    int v0;
    int v1;
    int v2;
};

struct PARAM_UV {
    double u;
    double v;
};

struct PARAM_FaceUV {
    int face_id = -1;
    int vertex_id[3] = {-1, -1, -1};
    PARAM_UV uv[3]{};
    double area_distortion = 0.0;
    double angle_distortion = 0.0;
    double l2_stretch = 0.0;
    bool flipped = false;
};

struct PARAM_SeamPath {
    int start_vertex = -1;
    int end_vertex = -1;
    std::vector<int> vertex_ids;
    double length = 0.0;
};

enum PARAM_Algorithm : int {
    PARAM_ALGO_LSCM = 0,
    PARAM_ALGO_ARAP = 1
};

struct PARAM_Config {
    int algorithm = PARAM_ALGO_LSCM;
    int arap_iterations = 50;
    double arap_tolerance = 1e-6;
    double arap_lambda = 1000.0;
    bool show_process = true;
    int arap_snapshot_stride = 1;
    int arap_snapshot_delay_ms = 60;
};

struct PARAM_ARAPSnapshot {
    int iteration = 0;
    double energy = 0.0;
    std::vector<PARAM_UV> vertex_uvs;
};

/// Domain-specific parameterization failure code.
enum class PARAM_ErrorCode : int {
    None = 0,
    EmptyInput = 1,
    NotTriangleMesh = 2,
    ClosedNoSeam = 3,
    BuildFailed = 4,
    NoBoundary = 5,
    AlgorithmFailed = 6,
    Exception = 7
};

constexpr PARAM_ErrorCode PARAM_ERR_None = PARAM_ErrorCode::None;
constexpr PARAM_ErrorCode PARAM_ERR_EmptyInput = PARAM_ErrorCode::EmptyInput;
constexpr PARAM_ErrorCode PARAM_ERR_NotTriangleMesh = PARAM_ErrorCode::NotTriangleMesh;
constexpr PARAM_ErrorCode PARAM_ERR_ClosedNoSeam = PARAM_ErrorCode::ClosedNoSeam;
constexpr PARAM_ErrorCode PARAM_ERR_BuildFailed = PARAM_ErrorCode::BuildFailed;
constexpr PARAM_ErrorCode PARAM_ERR_NoBoundary = PARAM_ErrorCode::NoBoundary;
constexpr PARAM_ErrorCode PARAM_ERR_AlgorithmFailed = PARAM_ErrorCode::AlgorithmFailed;
constexpr PARAM_ErrorCode PARAM_ERR_Exception = PARAM_ErrorCode::Exception;

inline int param_error_code_value(PARAM_ErrorCode code) {
    return static_cast<int>(code);
}

struct PARAM_Result {
    /// None means the result is usable; other values explain why it failed.
    PARAM_ErrorCode error_code = PARAM_ERR_None;
    char error_message[512] = {};
    int original_vertices = 0;
    int original_faces = 0;
    int boundary_edge_count = 0;
    bool is_closed = false;
    double elapsed_ms = 0.0;
    std::vector<int> boundary_vertices;
    std::vector<PARAM_FaceUV> face_uvs;
    int flipped_count = 0;
    int degenerate_count = 0;
    double mean_area_distortion = 0.0;
    double max_area_distortion = 0.0;
    double mean_angle_distortion = 0.0;
    double max_angle_distortion = 0.0;
    std::vector<PARAM_ARAPSnapshot> arap_snapshots;
};

using PARAM_WakeCallback = void (*)(void*);

#endif // CLAW3D_COMMON_PARAMETERIZATION_CONTRACT_H

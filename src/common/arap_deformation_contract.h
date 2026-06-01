// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_ARAP_DEFORMATION_CONTRACT_H
#define CLAW3D_COMMON_ARAP_DEFORMATION_CONTRACT_H

/// Typed DTO contract shared by ARAP deformation UI, services, and runner code.

#include <vector>

struct ARAP_Point3d {
    double x;
    double y;
    double z;
};

struct ARAP_Triangle {
    int v0;
    int v1;
    int v2;
};

enum ARAP_Mode : int {
    ARAP_MODE_SpokesAndRims = 0,
    ARAP_MODE_OriginalARAP = 1,
    ARAP_MODE_SRE_ARAP = 2
};

enum ARAP_PreviewSpeed : int {
    ARAP_PREVIEW_Fast = 0,
    ARAP_PREVIEW_Normal = 1,
    ARAP_PREVIEW_Slow = 2
};

/// Domain-specific ARAP deformation failure code.
enum class ARAP_ErrorCode : int {
    None = 0,
    EmptyInput = 1,
    NotTriangleMesh = 2,
    BuildFailed = 3,
    EmptyROI = 4,
    EmptyControls = 5,
    PreprocessFailed = 6,
    AlgorithmException = 7
};

constexpr ARAP_ErrorCode ARAP_ERR_None = ARAP_ErrorCode::None;
constexpr ARAP_ErrorCode ARAP_ERR_EmptyInput = ARAP_ErrorCode::EmptyInput;
constexpr ARAP_ErrorCode ARAP_ERR_NotTriangleMesh = ARAP_ErrorCode::NotTriangleMesh;
constexpr ARAP_ErrorCode ARAP_ERR_BuildFailed = ARAP_ErrorCode::BuildFailed;
constexpr ARAP_ErrorCode ARAP_ERR_EmptyROI = ARAP_ErrorCode::EmptyROI;
constexpr ARAP_ErrorCode ARAP_ERR_EmptyControls = ARAP_ErrorCode::EmptyControls;
constexpr ARAP_ErrorCode ARAP_ERR_PreprocessFailed = ARAP_ErrorCode::PreprocessFailed;
constexpr ARAP_ErrorCode ARAP_ERR_AlgorithmException = ARAP_ErrorCode::AlgorithmException;

inline int arap_error_code_value(ARAP_ErrorCode code) {
    return static_cast<int>(code);
}

struct ARAP_ControlTransform {
    int group_id = 0;
    double tx = 0.0;
    double ty = 0.0;
    double tz = 0.0;
    double rx_deg = 0.0;
    double ry_deg = 0.0;
    double rz_deg = 0.0;
    double rot_cx = 0.0;
    double rot_cy = 0.0;
    double rot_cz = 0.0;
    double scale = 1.0;
};

struct ARAP_Selection {
    std::vector<int> roi_vertices;
    std::vector<int> control_vertices;
    std::vector<int> control_group_ids;
};

struct ARAP_Config {
    int mode = ARAP_MODE_SpokesAndRims;
    int iterations = 10;
    double tolerance = 1e-4;
    bool live_preview = false;
    int preview_speed = ARAP_PREVIEW_Normal;
    int preview_steps = 16;
    int preview_min_ms = 60;
};

struct ARAP_Snapshot {
    int generation = 0;
    int step = 0;
    int total_steps = 0;
    double elapsed_ms = 0.0;
    double max_displacement = 0.0;
    double mean_displacement = 0.0;
    std::vector<ARAP_Point3d> vertices;
    std::vector<double> vertex_displacement;
};

struct ARAP_Result {
    /// None means the result is usable; other values explain why it failed.
    ARAP_ErrorCode error_code = ARAP_ERR_None;
    char error_message[512] = {};
    int original_vertices = 0;
    int original_faces = 0;
    int roi_count = 0;
    int control_count = 0;
    int iterations = 0;
    double elapsed_ms = 0.0;
    double max_displacement = 0.0;
    double mean_displacement = 0.0;
    bool preprocess_ok = false;
    std::vector<ARAP_Point3d> vertices;
    std::vector<double> vertex_displacement;
};

#endif // CLAW3D_COMMON_ARAP_DEFORMATION_CONTRACT_H

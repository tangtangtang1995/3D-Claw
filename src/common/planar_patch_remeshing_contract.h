// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_PLANAR_PATCH_REMESHING_CONTRACT_H
#define CLAW3D_COMMON_PLANAR_PATCH_REMESHING_CONTRACT_H

/// Typed DTO contract shared by planar-patch remeshing UI, services, and runner code.

#include "common/preview_policy.h"

#include <vector>

struct PPR_Point3d {
    double x;
    double y;
    double z;
};

struct PPR_Triangle {
    int v0;
    int v1;
    int v2;
};

enum PPR_Mode : int {
    PPR_ExactPlanar = 0,
    PPR_AlmostPlanar = 1,
    PPR_ExistingLabels = 2
};

struct PPR_Config {
    int mode = PPR_AlmostPlanar;
    double cosine_threshold = 0.98;
    double max_distance = 0.0;
    double distance_ratio = 0.002;
    bool postprocess_regions = true;
    bool live_preview = false;
    int preview_speed = 0;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
};

struct PPR_Snapshot {
    int generation = 0;
    int phase = 0;
    int patches = 0;
    int corners = 0;
    int constrained_edges = 0;
    double progress = 0.0;
    std::vector<int> face_patch_ids;
    std::vector<PPR_Point3d> corner_points;
    std::vector<PPR_Point3d> constrained_edge_endpoints;
    int processed_patches = 0;
    int current_patch_id = -1;
    int fallback_patches = 0;
};

struct PPR_DebugStats {
    int input_vertices = 0;
    int input_edges = 0;
    int input_faces = 0;
    int output_vertices = 0;
    int output_edges = 0;
    int output_faces = 0;
    int patches = 0;
    int corners = 0;
    int constrained_edges = 0;
    int fallback_patches = 0;
    bool all_patches_remeshed = true;
    bool cancelled = false;
    double bbox_diag = 0.0;
    double compression_ratio = 0.0;
    double ms_total = 0.0;
    double ms_region_growing = 0.0;
    double ms_corner_detection = 0.0;
    double ms_remeshing = 0.0;
};

#endif // CLAW3D_COMMON_PLANAR_PATCH_REMESHING_CONTRACT_H

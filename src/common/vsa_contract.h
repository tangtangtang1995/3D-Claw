// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_VSA_CONTRACT_H
#define CLAW3D_COMMON_VSA_CONTRACT_H

/// Typed DTO contract shared by VSA approximation UI, services, and runner code.

#include "common/preview_policy.h"

#include <vector>

struct VSA_Point3d {
    double x;
    double y;
    double z;
};

struct VSA_Triangle {
    int v0;
    int v1;
    int v2;
};

enum VSA_Metric : int {
    VSA_METRIC_L21 = 0,
    VSA_METRIC_L2 = 1
};

enum VSA_Seeding : int {
    VSA_SEED_Hierarchical = 0,
    VSA_SEED_Incremental = 1,
    VSA_SEED_Random = 2
};

struct VSA_Config {
    int metric = VSA_METRIC_L21;
    int seeding = VSA_SEED_Hierarchical;
    int target_proxies = 100;
    int iterations = 30;
    int relaxations = 5;
    bool use_error_drop = false;
    double min_error_drop = 0.1;
    bool extract_mesh = true;
    unsigned int random_seed = 1;

    bool live_preview = false;
    bool staged_growth = false;
    int preview_speed = 0;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int max_snapshot_faces = claw3d::preview_policy::kDefaultMaxSnapshotFaces;
};

struct VSA_ProxyInfo {
    int id = -1;
    int seed_face = -1;
    double error = 0.0;
    VSA_Point3d seed_center{};
    VSA_Point3d normal{};
};

struct VSA_Snapshot {
    int generation = 0;
    int phase = 0;
    int iteration = 0;
    int proxies = 0;
    int target_proxies = 0;
    int faces = 0;
    double total_error = 0.0;
    double error_drop = 0.0;
    std::vector<int> face_proxy_ids;
    std::vector<VSA_ProxyInfo> proxies_info;
};

struct VSA_DebugStats {
    int initial_vertices = 0;
    int initial_faces = 0;
    int initial_components = 0;
    int final_vertices = 0;
    int final_faces = 0;
    int target_proxies = 0;
    int final_proxies = 0;
    int iterations_completed = 0;
    int seeds_inserted = 0;
    bool extracted_mesh = false;
    bool manifold_output = false;
    bool cancelled = false;
    double initial_error = 0.0;
    double final_error = 0.0;
    double ms_total = 0.0;
    double ms_convert_in = 0.0;
    double ms_seeding = 0.0;
    double ms_iteration = 0.0;
    double ms_extraction = 0.0;
    double ms_convert_out = 0.0;
};

#endif // CLAW3D_COMMON_VSA_CONTRACT_H

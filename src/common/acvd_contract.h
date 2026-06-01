// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_ACVD_CONTRACT_H
#define CLAW3D_COMMON_ACVD_CONTRACT_H

/// Typed DTO contract shared by ACVD remeshing UI, services, and runner code.

#include "common/preview_policy.h"

struct ACVD_Point3d {
    double x;
    double y;
    double z;
};

struct ACVD_Triangle {
    int v0;
    int v1;
    int v2;
};

enum ACVD_Mode : int {
    ACVD_MODE_Uniform = 0,
    ACVD_MODE_UniformQemPostprocess = 1,
    ACVD_MODE_QemEnergy = 2,
    ACVD_MODE_AdaptiveCurvature = 3
};

struct ACVD_Config {
    int target_vertices = 1000;
    int mode = ACVD_MODE_Uniform;
    double gradation_factor = 0.8;
    double vertex_count_ratio = 0.1;
    unsigned int random_seed = 1;

    bool live_preview = false;
    int event_interval = 1;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int snapshot_interval = 0;
    int max_live_events = 8192;
    int max_snapshot_faces = claw3d::preview_policy::kDefaultMaxSnapshotFaces;
    bool slow_visual_playback = false;
};

struct ACVD_FrameEvent {
    enum Type : int {
        Init = 0,
        PreprocessBegin = 1,
        PreprocessEnd = 2,
        Seed = 3,
        AssignmentBatch = 4,
        IterationEnd = 5,
        ClusterRepair = 6,
        OutputSoup = 7,
        Snapshot = 8,
        Progress = 9,
        Done = 10,
        Error = 11
    };
    int type = Init;
    int loop = 0;
    int iteration = 0;
    int target_vertices = 0;
    int current_clusters = 0;
    int assigned_vertices = 0;
    int total_vertices = 0;
    int modifications = 0;
    int disconnected_clusters = 0;
    int non_manifold_clusters = 0;
    int output_vertices = 0;
    int output_faces = 0;
    bool qem_energy = false;
    double progress = 0.0;
    int snapshot_generation = -1;
    char error_msg[256] = {};
};

struct ACVD_DebugStats {
    int mode = 0;
    int target_vertices = 0;
    int initial_vertices = 0;
    int initial_edges = 0;
    int initial_faces = 0;
    int working_vertices_after_preprocess = 0;
    int final_vertices = 0;
    int final_faces = 0;
    int loops = 0;
    int iterations = 0;
    int assignment_events = 0;
    int disconnected_repairs = 0;
    int non_manifold_repairs = 0;
    bool output_vertex_count_matched = false;
    bool cancelled = false;
    double ms_total = 0.0;
    double ms_preprocess = 0.0;
    double ms_clustering = 0.0;
    double ms_output = 0.0;
};

#endif // CLAW3D_COMMON_ACVD_CONTRACT_H

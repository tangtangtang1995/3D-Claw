// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_SIMPLIFICATION_CONTRACT_H
#define CLAW3D_COMMON_SIMPLIFICATION_CONTRACT_H

/// Typed DTO contract shared by CGAL simplification UI, services, and runner code.

#include "common/preview_policy.h"

struct SIMPL_Point3d {
    double x;
    double y;
    double z;
};

struct SIMPL_Triangle {
    int v0;
    int v1;
    int v2;
};

enum SIMPL_Strategy : int {
    SIMPL_STRAT_LindstromTurk = 0,
    SIMPL_STRAT_GarlandHeckbertPlane = 1,
    SIMPL_STRAT_EdgeLengthMidpoint = 2,
    SIMPL_STRAT_GarlandHeckbertTriangle = 3,
    SIMPL_STRAT_GarlandHeckbertProbabilisticPlane = 4,
    SIMPL_STRAT_GarlandHeckbertProbabilisticTriangle = 5
};

enum SIMPL_StopMode : int {
    SIMPL_STOP_EdgeRatio = 0,
    SIMPL_STOP_EdgeCount = 1,
    SIMPL_STOP_FaceRatio = 2,
    SIMPL_STOP_FaceCount = 3
};

struct SIMPL_Config {
    int strategy = SIMPL_STRAT_LindstromTurk;
    int stop_mode = SIMPL_STOP_EdgeRatio;
    double target_ratio = 0.5;
    int target_count = 0;
    bool live_preview = false;
    int event_interval = 1;
    int snapshot_interval = 0;
    int snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int max_live_events = 8192;
    bool use_bounded_normal_change = false;
    bool use_polyhedral_envelope = false;
    double polyhedral_envelope_epsilon = 0.0;
};

struct SIMPL_FrameEvent {
    enum Type : int {
        Init = 0,
        Selected = 1,
        Collapsing = 2,
        Collapsed = 3,
        Rejected = 4,
        Snapshot = 5,
        Progress = 6,
        StopReached = 7,
        Done = 8,
        Error = 9
    };

    int type = Init;
    int step = 0;
    double p0[3] = {0, 0, 0};
    double p1[3] = {0, 0, 0};
    double placement[3] = {0, 0, 0};
    double cost = 0.0;
    int initial_edges = 0;
    int current_edges = 0;
    int initial_faces = 0;
    int current_faces = 0;
    int collapsed_count = 0;
    int rejected_count = 0;
    double reduction_ratio = 0.0;
    int snapshot_generation = -1;
};

struct SIMPL_DebugStats {
    int strategy_used = 0;
    int initial_vertices = 0;
    int initial_edges = 0;
    int initial_faces = 0;
    int current_vertices = 0;
    int current_edges = 0;
    int current_faces = 0;
    int collapsed_count = 0;
    int rejected_count = 0;
    int selected_count = 0;
    int cost_count = 0;
    double reduction_ratio = 0.0;
    double cost_min = 0.0;
    double cost_max = 0.0;
    double cost_mean = 0.0;
    double cost_recent_mean = 0.0;
    bool stop_reached = false;
    bool cancelled = false;
    double ms_total = 0.0;
    double ms_setup = 0.0;
    double ms_collapse = 0.0;
    double ms_convert = 0.0;
};

#endif // CLAW3D_COMMON_SIMPLIFICATION_CONTRACT_H

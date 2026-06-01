// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_REGION_GROWING_CONTRACT_H
#define CLAW3D_COMMON_REGION_GROWING_CONTRACT_H

/// Typed DTO contract shared by region-growing UI, services, and runner code.

#include <vector>

struct RG_Point3d {
    double x;
    double y;
    double z;
};

struct RG_Vector3d {
    double x;
    double y;
    double z;
};

struct RG_Config {
    int k_neighbors = 32;
    double max_distance = -1.0;
    double max_angle_deg = 25.0;
    int min_region_size = -1;
    bool live_preview = false;
    bool live_throttle = false;
    bool live_show_tentative = false;

    int pacing_batch = 50;
    int pacing_sleep_ms = 5;
    int accepted_event_interval = 32;
    int neighbor_event_interval = 64;
    int progress_event_interval = 256;
    int max_live_points = 50000;
};

struct RG_FrameEvent {
    enum Type : int {
        Init = 0,
        SeedSelected = 1,
        FrontierItem = 2,
        NeighborAccepted = 4,
        NeighborRejected = 5,
        PrimitiveRefit = 6,
        RegionAccepted = 7,
        RegionRejected = 8,
        Progress = 9,
        Done = 10,
        Error = 11
    };

    int type = Init;
    int step = 0;
    int region_id = -1;
    int seed_index = -1;
    int item_index = -1;
    int neighbor_index = -1;
    int region_size = 0;
    int accepted_regions = 0;
    int visited_points = 0;
    int total_points = 0;
    double plane[4] = {0, 0, 0, 0};
    double point[3] = {0, 0, 0};
    double normal[3] = {0, 0, 0};
};

struct RG_RegionResult {
    int region_id = -1;
    int size = 0;
    double plane[4] = {0, 0, 0, 0};
    std::vector<int> indices;
};

struct RG_DebugStats {
    int total_points = 0;
    int seed_attempts = 0;
    int scan_progress = 0;
    int visited_points = 0;
    int accepted_regions = 0;
    int rejected_regions = 0;
    int current_region_size = 0;
    int current_seed = -1;
    int frontier_items = 0;
    int neighbor_accepted = 0;
    int neighbor_rejected = 0;
    int primitive_refits = 0;
    bool cancel_requested = false;

    double ms_total = 0.0;
    double ms_setup = 0.0;
    double ms_accepted_regions = 0.0;
    double ms_rejected_seeds = 0.0;
    double ms_visitor_overhead = 0.0;
};

#endif // CLAW3D_COMMON_REGION_GROWING_CONTRACT_H

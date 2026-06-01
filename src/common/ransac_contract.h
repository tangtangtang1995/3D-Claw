// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_RANSAC_CONTRACT_H
#define CLAW3D_COMMON_RANSAC_CONTRACT_H

/// Typed DTO contract shared by RANSAC detection UI, services, and runner code.

struct RANSAC_Point3d {
    double x;
    double y;
    double z;
};

struct RANSAC_Vector3d {
    double x;
    double y;
    double z;
};

struct RANSAC_Config {
    float epsilon = -1;           // inlier distance threshold (<0 = auto)
    float normal_threshold = 0.9f; // normal deviation cos(theta) threshold
    float cluster_epsilon = -1;   // connected-component cluster epsilon (<0 = auto)
    int min_points = -1;          // minimum inlier count (<0 = auto 1%)
    bool live_preview = false;
    bool live_throttle = true;

    // Guardrails (auto-tuned by runner based on input size).
    int candidate_event_interval = 10;
    int sample_event_interval = 50;
    int max_live_inliers = 50000;
};

struct RANSAC_FrameEvent {
    enum Type : int {
        Init = 0,
        Sampling = 1,
        Candidate = 2,
        InlierVote = 3,
        ShapeAccepted = 4,
        Peel = 5,
        Progress = 6,
        Done = 7,
        Error = 8
    };

    int type = Init;
    int step = 0;

    double sample_pts[3][3] = {};
    double plane_eq[4] = {};
    int inlier_count = 0;
    int shape_id = 0;

    int remaining_points = 0;
    int total_points = 0;
    int shapes_found = 0;
    double elapsed_sec = 0.0;
};

#endif // CLAW3D_COMMON_RANSAC_CONTRACT_H

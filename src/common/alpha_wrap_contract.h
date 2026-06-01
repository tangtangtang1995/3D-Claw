// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_ALPHA_WRAP_CONTRACT_H
#define CLAW3D_COMMON_ALPHA_WRAP_CONTRACT_H

/// Typed DTO contract shared by Alpha Wrap UI, services, and runner code.

struct AW3_Point3d {
    double x;
    double y;
    double z;
};

struct AW3_Triangle {
    int v0;
    int v1;
    int v2;
};

struct AW3_Config {
    float alpha = 0.05f;
    float offset = 0.002f;
    bool live_preview = false;
    int live_surface_interval = 0;
    int gate_event_interval = 25;
    int progress_event_interval = 50;
    int live_steiner_subsample = 1;
};

struct AW3_FrameEvent {
    enum Type : int {
        Init = 0,
        SteinerR1 = 1,
        SteinerR2 = 2,
        Carve = 3,
        Progress = 4,
        Done = 5,
        Gate = 6,
        SurfaceSnapshot = 7,
        Error = 8
    };

    int type = Init;
    int step = 0;
    double point[3] = {0, 0, 0};
    int num_steiner_total = 0;
    int num_carved_total = 0;
    double gate_p0[3] = {0, 0, 0};
    double gate_p1[3] = {0, 0, 0};
    double gate_p2[3] = {0, 0, 0};
    double gate_radius = 0.0;
    int gate_queue_size = 0;
    int surface_vertices = 0;
    int surface_faces = 0;
};

#endif // CLAW3D_COMMON_ALPHA_WRAP_CONTRACT_H

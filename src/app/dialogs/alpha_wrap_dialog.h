// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALPHA_WRAP_DIALOG_H
#define CLAW3D_ALPHA_WRAP_DIALOG_H

/// State and render entry point for Alpha Wrap reconstruction controls.

#include <atomic>

#include "services/jobs/cgal/alpha_wrap_job.h"

class ViewportCanvas;

struct AlphaWrappingState {
    float alpha = 0.05f;
    float offset = 0.002f;
    bool live_preview = false;
    int  live_display_mode = 1;
    int  live_recent_count = 300;
    bool live_show_gate = true;
    int  live_gate_trail_count = 32;
    bool live_show_surface = false;
    int  live_surface_interval = 250;
    float live_surface_opacity = 0.28f;
    bool live_surface_wireframe = true;
    bool live_clear_on_finish = false;

    std::atomic<int> running_step{0};
    std::atomic<int> running_steiner{0};
    std::atomic<int> running_carved{0};
    std::atomic<int> running_queue{0};
    std::atomic<int> running_surface_vertices{0};
    std::atomic<int> running_surface_faces{0};
    float running_start_time = 0;

    claw3d::services::AlphaWrapJobHandle runner;
};

void renderDialogAlphaWrapping(ViewportCanvas* viewer, AlphaWrappingState& s, bool& open);

#endif // CLAW3D_ALPHA_WRAP_DIALOG_H

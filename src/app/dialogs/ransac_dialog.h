// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_RANSAC_DIALOG_H
#define CLAW3D_RANSAC_DIALOG_H

/// State and render entry point for RANSAC primitive detection controls.

#include <cctype>
#include <string>

#include <easy3d/core/types.h>

#include "services/jobs/cgal/ransac_detection_job.h"
#include "overlays/overlay_palette.h"

class ViewportCanvas;


inline int parse_ransac_shape_id(const std::string& name) {
    if (name.rfind("plane_", 0) != 0) return -1;
    size_t i = 6;
    if (i >= name.size() || !std::isdigit((unsigned char)name[i])) return -1;
    int n = 0;
    while (i < name.size() && std::isdigit((unsigned char)name[i])) {
        n = n * 10 + (name[i] - '0');
        ++i;
    }
    return n;
}

struct RansacState {
    float epsilon = -1;
    float normal_threshold = 0.9f;
    float cluster_epsilon = -1;
    int   min_points = -1;
    bool  live_preview = false;
    bool  live_throttle = true;
    int   running_step = 0;
    int   running_shapes = 0;
    int   running_remaining = 0;
    float running_start_time = 0;
    int   num_shapes = 0;

    claw3d::services::RansacDetectionJobHandle runner;
    int  live_shapes_added = 0;
};

void renderDialogRansac(ViewportCanvas* viewer, RansacState& s, bool& open);

#endif // CLAW3D_RANSAC_DIALOG_H

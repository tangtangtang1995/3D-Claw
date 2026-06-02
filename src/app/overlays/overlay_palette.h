// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_OVERLAY_PALETTE_H
#define CLAW3D_OVERLAY_PALETTE_H

/// Deterministic colors shared by algorithm result and live-preview overlays.

#include <easy3d/core/types.h>

inline easy3d::vec3 overlay_shape_color(int shape_id) {
    auto h = [](unsigned x) {
        x = (x ^ 61u) ^ (x >> 16);
        x = x + (x << 3);
        x = x ^ (x >> 4);
        x = x * 0x27d4eb2du;
        x = x ^ (x >> 15);
        return x;
    };
    unsigned r = h(static_cast<unsigned>(shape_id) * 3u + 1u);
    unsigned g = h(static_cast<unsigned>(shape_id) * 3u + 2u);
    unsigned b = h(static_cast<unsigned>(shape_id) * 3u + 3u);
    return easy3d::vec3(
        ((r % 151u) + 50u) / 255.0f,
        ((g % 151u) + 50u) / 255.0f,
        ((b % 151u) + 50u) / 255.0f);
}

inline easy3d::vec3 ransac_shape_color(int shape_id) {
    return overlay_shape_color(shape_id);
}

#endif // CLAW3D_OVERLAY_PALETTE_H
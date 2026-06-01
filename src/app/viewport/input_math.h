// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#pragma once

/// Low-level geometric helpers for viewport picking and transform input.

#include <easy3d/core/types.h>

#include <cmath>

#include "imgui.h"


namespace claw_viewport_input {

inline float ray_seg_dist(const easy3d::vec3& ro, const easy3d::vec3& rd,
                          const easy3d::vec3& s0, const easy3d::vec3& s1,
                          float& tr, float& ts) {
    easy3d::vec3 sd = s1 - s0;
    float a = easy3d::dot(rd, rd), b = easy3d::dot(rd, sd), c = easy3d::dot(sd, sd);
    float d = easy3d::dot(rd, ro - s0), e = easy3d::dot(sd, ro - s0);
    float det = a * c - b * b;
    if (std::abs(det) < 1e-10f) {
        tr = 0.0f;
        ts = 0.0f;
        return 1e10f;
    }
    tr = (b * e - c * d) / det;
    ts = (a * e - b * d) / det;
    ts = ts < 0 ? 0 : (ts > 1 ? 1 : ts);
    easy3d::vec3 cp = s0 + ts * sd;
    tr = easy3d::dot(cp - ro, rd);
    easy3d::vec3 rp = ro + tr * rd;
    return easy3d::length(cp - rp);
}

inline float ray_axis_param(const easy3d::vec3& ro, const easy3d::vec3& rd,
                            const easy3d::vec3& origin, const easy3d::vec3& axis) {
    const easy3d::vec3 w = ro - origin;
    const float aa = easy3d::dot(rd, rd);
    const float bb = easy3d::dot(rd, axis);
    const float cc = easy3d::dot(axis, axis);
    const float dd = easy3d::dot(rd, w);
    const float ee = easy3d::dot(axis, w);
    const float det = aa * cc - bb * bb;
    if (std::abs(det) < 1e-10f)
        return 0.0f;
    return (aa * ee - bb * dd) / det;
}

inline float sqr_len2(const ImVec2& v) {
    return v.x * v.x + v.y * v.y;
}

} // namespace claw_viewport_input

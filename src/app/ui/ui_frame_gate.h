// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_UI_FRAME_GATE_H
#define CLAW3D_UI_FRAME_GATE_H

/// Frame-local guards for UI work that should happen once per visible frame.

namespace claw_ui {

inline bool frame_due(double now, double& last_time, double interval_ms) {
    if (interval_ms <= 0.0) {
        last_time = now;
        return true;
    }
    if (last_time <= 0.0 || (now - last_time) * 1000.0 >= interval_ms) {
        last_time = now;
        return true;
    }
    return false;
}

inline bool frame_waiting(double now, double last_time, double interval_ms) {
    return interval_ms > 0.0 &&
           last_time > 0.0 &&
           (now - last_time) * 1000.0 < interval_ms;
}

inline void mark_frame_displayed(double now, double& last_time) {
    last_time = now;
}

inline void reset_frame_gate(double& last_time) {
    last_time = 0.0;
}

} // namespace claw_ui

#endif // CLAW3D_UI_FRAME_GATE_H
